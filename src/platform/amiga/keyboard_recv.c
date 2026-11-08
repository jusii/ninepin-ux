/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * real amiga keyboard receive path.
 *
 * on boards with a keyboard input (HAS_KBD_IN: a dedicated pin pair on rev6, or
 * controller port 2 repurposed on rev4 with PORT2_ROLE=KEYBOARD), a physically
 * attached amiga keyboard can drive the amiga alongside the usb/bluetooth path.
 * the pico acts as the host: a real keyboard is the clock master and clocks out
 * keycodes; a pio state machine samples them and drives the ~85us handshake
 * back autonomously. in amiga mode, decoded keycodes are pushed into the same
 * output queue the usb/bluetooth path feeds, so the amiga-facing transmitter
 * cannot tell a real keypress from a usb one. on boards with a usb device port,
 * the c64 and usb modes send them to the usb host instead, as a hid keyboard merged with
 * the usb keyboards (see the routing section below).
 *
 * the amiga keyboard frame is the mirror of the transmit path in
 * keyboard_serial_io.c: 8 bits, msb first, active low, with the byte rotated
 * left by one so the up/down flag is the last (lsb) bit on the wire. we undo
 * the active-low inversion and the rotate on the cpu side here.
 *
 * hard reset is signalled out of band: a real keyboard holds kclk low for at
 * least 500ms, which on big-box machines (a2000/3000/4000) is the only reset
 * signalling there is. the state machine just sees silence, so the hold is
 * detected here by polling the line and forwarded with amiga_kbdin_reset().
 *
 * the hold also strands a sampled bit in the state machine, so the receiver is
 * resynchronised after every reset. that is deliberately not tied to observing
 * the hold: a ctrl-amiga-amiga typed on the real keyboard is recognised from the
 * key events first, and asserting reset blocks the main loop for 500ms, so the
 * hold can begin and end without a single poll landing on it.
 */

#include "config.h"
#include "keyboard_recv.h"

#ifdef HAS_KBD_IN

#include "keyboard.h"
#include "keyboard_serial_io.h"
#include "keyboard_recv.pio.h" // generated at compile time
#include "input_bridge_bt.h"
#include "menu.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/run_mode.h"
#include "util/status_led.h"
#ifdef HAS_USB_DEVICE
#include "usb_device.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

// how long kclk must sit low before we call it a reset rather than a data
// strobe. keyboards hold for >=500ms, a bit strobe is low for ~20us, so
// anything in between works; overridable from the build for bench tuning.
#ifndef KBDIN_RESET_HOLD_MS
#  define KBDIN_RESET_HOLD_MS 250
#endif

// a keyboard which never lets go of kclk is unplugged, unpowered or broken
// rather than asking for a reset. give up after this long: a reset also mutes
// the usb and bluetooth paths, so an unbounded hold would take the whole adapter
// down until it was power cycled.
#ifndef KBDIN_RESET_HOLD_MAX_MS
#  define KBDIN_RESET_HOLD_MAX_MS 3000
#endif

// the poll below runs from the main loop, which blocks for milliseconds at a
// time whilst it clocks a keycode out to the amiga - and for half a second
// whilst it asserts reset. two low samples either side of such a gap say nothing
// about the line in between (kclk is low a third of the time whilst a keyboard
// transmits), so only samples close enough together to count as one continuous
// observation may extend a hold.
#define KBDIN_POLL_GAP_MAX_MS 10

// how long the wire must have been quiet before a pending resynchronisation is
// safe to perform. restarting mid-byte would only move the misalignment, so wait
// until nothing can be in flight; a byte plus its handshake is well under 1ms.
#define KBDIN_RESYNC_IDLE_MS 5

// pio is otherwise unused on this platform, so pio0 is ours to take
static PIO recv_pio = pio0;
static uint recv_sm;
static uint recv_offset;

// kclk low-hold tracking; kbd_reset_held describes the state of the *wire*,
// whether or not we ended up being the one to assert reset (a real keyboard
// also holds kclk after a ctrl-amiga-amiga, which the key events themselves
// have already actioned in keyboard_serial_io.c).
static bool clk_was_low = false;
static bool clk_seen_idle = false;
static uint32_t clk_low_since = 0;
static uint32_t clk_last_poll = 0;
static bool kbd_reset_held = false;
static bool kbd_reset_abandoned = false;

// the hold seen in a usb mode (c64, usb): the keyboard belongs to the usb host then, so
// the amiga is left running and only the receiver and the keys are dealt with
static bool kbd_reset_local = false;

// resynchronisation tracking: armed when a reset begins, performed once the wire
// has been quiet for long enough that restarting cannot eat a byte in flight.
static bool reset_was_active = false;
static bool resync_pending = false;
static uint32_t recv_last_active = 0;

//--------------------------------------------------------------------+
// routing. the run mode decides where the real keyboard goes, as it does for
// the usb keyboards: amiga mode keeps the direct path into amiga_send_rawkey(),
// the c64 and usb modes (boards with a device port) build a hid report for the keyboard's
// own input slot, which usb_device.c merges with the usb keyboards.
//--------------------------------------------------------------------+

// keys down, per side, one bit per amiga keycode. a release goes to the side
// that saw the press; a mode switch releases everything on the side being left
// (amiga_keyboard_recv_mode_changed()), so a key held across the switch has its
// release dropped. caps lock latches on the keyboard and is not tracked here
static uint32_t held_amiga[4];
static uint32_t held_usb[4];

// the keyboard's own caps lamp: on with a caps down code, off with caps up. a
// keyboard comes out of power-up or a reset with it off
static bool caps_lamp = false;

// any byte received since boot. until then there may be no keyboard at all, and
// a mode switch leaves both sides' caps lock alone
static bool kbd_seen = false;

static inline bool held_get(uint32_t const *set, uint8_t code)
{
    return (set[code >> 5] >> (code & 31u)) & 1u;
}

static inline void held_put(uint32_t *set, uint8_t code, bool down)
{
    if (down)
        set[code >> 5] |= 1u << (code & 31u);
    else
        set[code >> 5] &= ~(1u << (code & 31u));
}

static inline bool held_any(uint32_t const *set)
{
    return set[0] | set[1] | set[2] | set[3];
}

#ifdef HAS_USB_DEVICE
// hid usage table, keyboard page: "keyboard errorrollover"
#define KBDIN_HID_ERROR_ROLLOVER 0x01

/**
 * amiga keycode -> hid keyboard usage, by key position, so the host's own
 * layout applies as it would to a pc keyboard. the standard keys are the
 * reverse of mapHidToAmiga. the amiga-only keys: help -> insert (the reverse of
 * insert -> help), the amiga keys -> gui, numpad ( ) -> keypad ( ) (linux maps
 * those to parens whatever the layout; windows and macos drop them). usages
 * 0xe0-0xe7 are the modifiers; 0 is no key (the spare codes). caps lock
 * latches and is handled on its own.
 */
static const uint8_t mapAmigaToHid[AMIGA_RAMIGA + 1] = {
    [AMIGA_BACKTICK]  = HID_KEY_GRAVE,
    [AMIGA_ONE]       = HID_KEY_1,
    [AMIGA_TWO]       = HID_KEY_2,
    [AMIGA_THREE]     = HID_KEY_3,
    [AMIGA_FOUR]      = HID_KEY_4,
    [AMIGA_FIVE]      = HID_KEY_5,
    [AMIGA_SIX]       = HID_KEY_6,
    [AMIGA_SEVEN]     = HID_KEY_7,
    [AMIGA_EIGHT]     = HID_KEY_8,
    [AMIGA_NINE]      = HID_KEY_9,
    [AMIGA_ZERO]      = HID_KEY_0,
    [AMIGA_DASH]      = HID_KEY_MINUS,
    [AMIGA_EQUALS]    = HID_KEY_EQUAL,
    [AMIGA_BACKSLASH] = HID_KEY_BACKSLASH,
    [AMIGA_KPZERO]    = HID_KEY_KEYPAD_0,
    [AMIGA_Q]         = HID_KEY_Q,
    [AMIGA_W]         = HID_KEY_W,
    [AMIGA_E]         = HID_KEY_E,
    [AMIGA_R]         = HID_KEY_R,
    [AMIGA_T]         = HID_KEY_T,
    [AMIGA_Y]         = HID_KEY_Y,
    [AMIGA_U]         = HID_KEY_U,
    [AMIGA_I]         = HID_KEY_I,
    [AMIGA_O]         = HID_KEY_O,
    [AMIGA_P]         = HID_KEY_P,
    [AMIGA_OSQPARENS] = HID_KEY_BRACKET_LEFT,
    [AMIGA_CSQPARENS] = HID_KEY_BRACKET_RIGHT,
    [AMIGA_KPONE]     = HID_KEY_KEYPAD_1,
    [AMIGA_KPTWO]     = HID_KEY_KEYPAD_2,
    [AMIGA_KPTHREE]   = HID_KEY_KEYPAD_3,
    [AMIGA_A]         = HID_KEY_A,
    [AMIGA_S]         = HID_KEY_S,
    [AMIGA_D]         = HID_KEY_D,
    [AMIGA_F]         = HID_KEY_F,
    [AMIGA_G]         = HID_KEY_G,
    [AMIGA_H]         = HID_KEY_H,
    [AMIGA_J]         = HID_KEY_J,
    [AMIGA_K]         = HID_KEY_K,
    [AMIGA_L]         = HID_KEY_L,
    [AMIGA_SEMICOLON] = HID_KEY_SEMICOLON,
    [AMIGA_QUOTE]     = HID_KEY_APOSTROPHE,
    [AMIGA_INTLRET]   = HID_KEY_EUROPE_1,     // left of return on international boards
    [AMIGA_KPFOUR]    = HID_KEY_KEYPAD_4,
    [AMIGA_KPFIVE]    = HID_KEY_KEYPAD_5,
    [AMIGA_KPSIX]     = HID_KEY_KEYPAD_6,
    [AMIGA_INTLSHIFT] = HID_KEY_EUROPE_2,     // right of left shift on international boards
    [AMIGA_Z]         = HID_KEY_Z,
    [AMIGA_X]         = HID_KEY_X,
    [AMIGA_C]         = HID_KEY_C,
    [AMIGA_V]         = HID_KEY_V,
    [AMIGA_B]         = HID_KEY_B,
    [AMIGA_N]         = HID_KEY_N,
    [AMIGA_M]         = HID_KEY_M,
    [AMIGA_COMMA]     = HID_KEY_COMMA,
    [AMIGA_PERIOD]    = HID_KEY_PERIOD,
    [AMIGA_SLASH]     = HID_KEY_SLASH,
    [AMIGA_KPPERIOD]  = HID_KEY_KEYPAD_DECIMAL,
    [AMIGA_KPSEVEN]   = HID_KEY_KEYPAD_7,
    [AMIGA_KPEIGHT]   = HID_KEY_KEYPAD_8,
    [AMIGA_KPNINE]    = HID_KEY_KEYPAD_9,
    [AMIGA_SPACE]     = HID_KEY_SPACE,
    [AMIGA_BACKSP]    = HID_KEY_BACKSPACE,
    [AMIGA_TAB]       = HID_KEY_TAB,
    [AMIGA_KPENTER]   = HID_KEY_KEYPAD_ENTER,
    [AMIGA_RETURN]    = HID_KEY_ENTER,
    [AMIGA_ESC]       = HID_KEY_ESCAPE,
    [AMIGA_DELETE]    = HID_KEY_DELETE,
    [AMIGA_KPDASH]    = HID_KEY_KEYPAD_SUBTRACT,
    [AMIGA_UP]        = HID_KEY_ARROW_UP,
    [AMIGA_DOWN]      = HID_KEY_ARROW_DOWN,
    [AMIGA_RIGHT]     = HID_KEY_ARROW_RIGHT,
    [AMIGA_LEFT]      = HID_KEY_ARROW_LEFT,
    [AMIGA_F1]        = HID_KEY_F1,
    [AMIGA_F2]        = HID_KEY_F2,
    [AMIGA_F3]        = HID_KEY_F3,
    [AMIGA_F4]        = HID_KEY_F4,
    [AMIGA_F5]        = HID_KEY_F5,
    [AMIGA_F6]        = HID_KEY_F6,
    [AMIGA_F7]        = HID_KEY_F7,
    [AMIGA_F8]        = HID_KEY_F8,
    [AMIGA_F9]        = HID_KEY_F9,
    [AMIGA_F10]       = HID_KEY_F10,
    [AMIGA_KPOPAREN]  = HID_KEY_KEYPAD_LEFT_PARENTHESIS,
    [AMIGA_KPCPAREN]  = HID_KEY_KEYPAD_RIGHT_PARENTHESIS,
    [AMIGA_KPSLASH]   = HID_KEY_KEYPAD_DIVIDE,
    [AMIGA_KPAST]     = HID_KEY_KEYPAD_MULTIPLY,
    [AMIGA_KPPLUS]    = HID_KEY_KEYPAD_ADD,
    [AMIGA_HELP]      = HID_KEY_INSERT,
    [AMIGA_LSHIFT]    = HID_KEY_SHIFT_LEFT,
    [AMIGA_RSHIFT]    = HID_KEY_SHIFT_RIGHT,
    [AMIGA_CTRL]      = HID_KEY_CONTROL_LEFT,
    [AMIGA_LALT]      = HID_KEY_ALT_LEFT,
    [AMIGA_RALT]      = HID_KEY_ALT_RIGHT,
    [AMIGA_LAMIGA]    = HID_KEY_GUI_LEFT,
    [AMIGA_RAMIGA]    = HID_KEY_GUI_RIGHT,
};

static inline bool kbdin_to_usb(void)
{
    return run_mode_keyboard_usb(run_mode_get());
}

/**
 * @return true if the code is a key the usb host gets. not every code a
 *         keyboard sends is a key: an a4000 keyboard's reset warning has been
 *         seen arriving as 0x7c, not the documented 0x78, and must not be held
 *         down on the usb side
 */
static inline bool kbdin_usb_maps(uint8_t keycode)
{
    return (keycode < sizeof(mapAmigaToHid)) && (mapAmigaToHid[keycode] != 0);
}

/**
 * hand the keys held on the usb side to usb_device.c as one boot report.
 */
static void kbdin_usb_report(void)
{
    hid_keyboard_report_t report;
    uint8_t keys = 0;
    bool overflow = false;

    memset(&report, 0, sizeof(report));

    for (uint8_t code = 0; code < sizeof(mapAmigaToHid); code++) {
        uint8_t usage;

        if (!held_get(held_usb, code))
            continue;

        usage = mapAmigaToHid[code];
        if (usage == 0)
            continue;

        if (usage >= HID_KEY_CONTROL_LEFT)
            report.modifier |= (uint8_t)(1u << (usage - HID_KEY_CONTROL_LEFT));
        else if (keys < sizeof(report.keycode))
            report.keycode[keys++] = usage;
        else
            overflow = true;
    }

    // more than six keys: boot protocol's answer is phantom state
    if (overflow)
        memset(report.keycode, KBDIN_HID_ERROR_ROLLOVER, sizeof(report.keycode));

    usb_device_keyboard(INPUT_BRIDGE_REAL_KBD_SLOT, &report);
}

/**
 * bring the host's caps lock to the keyboard's lamp, using the led state the
 * host reports. with nothing reported yet, a caps event just toggles the host
 * (a tap per lamp change) and anything else leaves it alone.
 *
 * @param on_event  true for a caps code from the keyboard, false for a sync on
 *                  a mode switch or a keyboard restart
 */
static void kbdin_usb_caps_sync(bool on_event)
{
    int host = usb_device_host_caps_lock();
    bool host_on;

    if (host < 0) {
        if (on_event)
            usb_device_caps_tap();
        return;
    }

    // a tap still in flight is about to flip it
    host_on = (host != 0) != usb_device_caps_tap_pending();

    if (host_on != caps_lamp)
        usb_device_caps_tap();
}

static void kbdin_usb_release(void)
{
    if (!held_any(held_usb))
        return;

    memset(held_usb, 0, sizeof(held_usb));
    kbdin_usb_report();
}
#else
static inline bool kbdin_to_usb(void) { return false; }
static inline bool kbdin_usb_maps(uint8_t keycode) { (void)keycode; return false; }
static inline void kbdin_usb_report(void) {}
static inline void kbdin_usb_caps_sync(bool on_event) { (void)on_event; }
static inline void kbdin_usb_release(void) { memset(held_usb, 0, sizeof(held_usb)); }
#endif

/**
 * let go of every key the amiga holds from the real keyboard.
 */
static void kbdin_amiga_release(void)
{
    for (uint8_t code = 0; code < 128; code++) {
        if (held_get(held_amiga, code)) {
            held_put(held_amiga, code, false);
            amiga_send_rawkey(code, true);
        }
    }
}

/**
 * the keyboard is restarting (its reset warning, a kclk hold, or a power-up
 * stream): it will never send the releases for what it had down, and its caps
 * lamp comes back off.
 *
 * @param tell_amiga  send the amiga up codes for its held keys; false when the
 *                    amiga is being reset anyway
 */
static void kbdin_keyboard_restarting(bool tell_amiga)
{
    if (tell_amiga)
        kbdin_amiga_release();
    else
        memset(held_amiga, 0, sizeof(held_amiga));

    kbdin_usb_release();

    caps_lamp = false;
    if (kbdin_to_usb())
        kbdin_usb_caps_sync(false);
}

/**
 * one key event from the real keyboard, routed by the run mode.
 */
static void kbdin_key(uint8_t keycode, bool up)
{
    if (keycode == AMIGA_CAPSLOCK) {
        caps_lamp = !up;

        // the menu owns the keyboards; the sides catch up on the next caps
        // press or mode switch
        if (menu_is_open())
            return;

        if (kbdin_to_usb()) {
            kbdin_usb_caps_sync(true);
            status_led_activity();
        } else {
            amiga_send_rawkey(keycode, up);
        }
        return;
    }

    // a held help opens the settings menu, which then keeps the keys
    if (menu_keyboard_amiga(keycode, up))
        return;

    if (!up) {
        if (kbdin_to_usb()) {
            if (!kbdin_usb_maps(keycode))
                return;

            held_put(held_usb, keycode, true);
            kbdin_usb_report();
            status_led_activity();
        } else {
            held_put(held_amiga, keycode, true);
            amiga_send_rawkey(keycode, false);
        }
        return;
    }

    // the release follows the press; a press from before a mode switch was
    // already released there, so its own release goes nowhere
    if (held_get(held_amiga, keycode)) {
        held_put(held_amiga, keycode, false);
        amiga_send_rawkey(keycode, true);
    } else if (held_get(held_usb, keycode)) {
        held_put(held_usb, keycode, false);
        kbdin_usb_report();
    }
}

void amiga_keyboard_recv_init(void)
{
    recv_offset = pio_add_program(recv_pio, &amiga_recv_program);
    recv_sm = pio_claim_unused_sm(recv_pio, true);

    amiga_recv_pio_init(recv_pio, recv_sm, recv_offset, KBD_REAL_CLK, KBD_REAL_DAT);
}

/**
 * bring the receiver back to a known idle state. a kclk hold leaves a single
 * sampled bit stranded in the isr, so every frame after the hold would be
 * assembled from one stale bit plus seven new ones, permanently one bit out.
 * restarting discards the partial frame and realigns on the next byte - which is
 * only true if nothing is on the wire at the time, hence the idle check in
 * amiga_keyboard_recv_resync_task().
 */
static void amiga_keyboard_recv_restart(void)
{
    pio_sm_set_enabled(recv_pio, recv_sm, false);
    pio_sm_clear_fifos(recv_pio, recv_sm);
    pio_sm_restart(recv_pio, recv_sm);
    pio_sm_clkdiv_restart(recv_pio, recv_sm);

    // the program's first instruction returns kdat to hi-z, so a jump to the
    // start also undoes a handshake the hold may have interrupted
    pio_sm_exec(recv_pio, recv_sm, pio_encode_jmp(recv_offset));
    pio_sm_set_enabled(recv_pio, recv_sm, true);
}

/**
 * perform a resynchronisation armed by a reset, once it is safe to do so.
 */
static void amiga_keyboard_recv_resync_task(uint32_t now)
{
    if (!resync_pending)
        return;

    if ((uint32_t)(now - recv_last_active) < (KBDIN_RESYNC_IDLE_MS * 1000u))
        return;

    if (!pio_sm_is_rx_fifo_empty(recv_pio, recv_sm))
        return;

    resync_pending = false;
    amiga_keyboard_recv_restart();
    amiga_kbdin_restarted();
    ahprintf("[akb] real keyboard receiver resynchronised\n");
}

/**
 * watch kclk for the long low the keyboard uses to demand a hard reset.
 */
static void amiga_keyboard_recv_reset_task(void)
{
    uint32_t now = time_us_32();
    bool clk_low = gpio_get(KBD_REAL_CLK) == 0;
    bool continuous = (uint32_t)(now - clk_last_poll) <= (KBDIN_POLL_GAP_MAX_MS * 1000u);
    bool reset_active = amiga_in_reset();

    clk_last_poll = now;

    // every hard reset ends with the keyboard holding kclk low and restarting
    // itself, which strands a sampled bit in the state machine. arm the
    // resynchronisation on the assertion edge rather than on seeing the hold
    // end: whoever asserted, the hold can pass entirely inside the half second
    // amiga_assert_reset() blocks the main loop for.
    if (reset_active && !reset_was_active)
        resync_pending = true;
    reset_was_active = reset_active;

    if (clk_low)
        recv_last_active = now;

    if (!clk_low) {
        clk_was_low = false;
        clk_seen_idle = true;

        if (kbd_reset_held) {
            // keyboard has let go of kclk: bring the amiga back out of reset
            // (unless the hold was kept from it in a usb mode)
            kbd_reset_held = false;
            kbd_reset_abandoned = false;
            if (!kbd_reset_local) {
                amiga_kbdin_reset(false);
                ahprintf("[akb] real keyboard released reset\n");
            }
            kbd_reset_local = false;
        }

        amiga_keyboard_recv_resync_task(now);
        return;
    }

    if (kbd_reset_held) {
        if (kbd_reset_abandoned || kbd_reset_local)
            return;

        // nothing legitimate holds the line down this long; let the amiga run
        // rather than leaving every input source muted behind a keyboard which
        // is never going to let go
        if ((uint32_t)(now - clk_low_since) >= (KBDIN_RESET_HOLD_MAX_MS * 1000u)) {
            kbd_reset_abandoned = true;
            amiga_kbdin_reset(false);
            ahprintf("[akb] kclk still low; giving up on the reset\n");
            return;
        }

        // re-offer the assert: it does nothing whilst another source owns the
        // reset, but takes it over the moment that source lets go, so the amiga
        // cannot come back up whilst the keyboard is still demanding a reset
        amiga_kbdin_reset(true);
        return;
    }

    if (!clk_was_low || !continuous) {
        clk_was_low = true;
        clk_low_since = now;
        return;
    }

    // a line that has been low ever since boot is an unconnected or unpowered
    // input, not a reset request; holding the amiga down forever over that would
    // be a poor failure mode, so wait until we have seen the bus idle once
    if (!clk_seen_idle)
        return;

    if ((uint32_t)(now - clk_low_since) < (KBDIN_RESET_HOLD_MS * 1000u))
        return;

    kbd_reset_held = true;

    if (kbdin_to_usb()) {
        // c64 / usb mode: the keyboard belongs to the usb host, so the amiga keeps
        // running. the keyboard is restarting all the same: let go of its keys
        // there and resynchronise the receiver once the hold is over (no reset
        // assertion edge will arm that)
        kbd_reset_local = true;
        resync_pending = true;
        kbdin_keyboard_restarting(true);
        ahprintf("[akb] real keyboard held kclk low; %s mode, amiga left running\n", run_mode_name(run_mode_get()));
        return;
    }

    kbdin_keyboard_restarting(false);
    ahprintf("[akb] real keyboard held kclk low; asserting reset\n");
    amiga_kbdin_reset(true);
}

void amiga_keyboard_recv_task(void)
{
    while (!pio_sm_is_rx_fifo_empty(recv_pio, recv_sm)) {
        uint32_t word = pio_sm_get(recv_pio, recv_sm);

        // the wire is busy; a resynchronisation has to wait for a gap
        recv_last_active = time_us_32();

        // undo active-low sampling, then rotate right by one to recover the
        // original amiga code (bit 7 = up/down, bits 6..0 = keycode).
        uint8_t logical = (uint8_t)(~(uint8_t)(word & 0xff));
        uint8_t code = (uint8_t)((logical >> 1) | (logical << 7));

        kbd_seen = true;

        switch (code) {
            // keyboard-mcu housekeeping codes: consume, never forward to the
            // amiga (we synthesise our own power-up codes on the output side).
            case AMIGA_LOSTSYNC:
            case AMIGA_OBOFLOW:
            case AMIGA_CTRFAIL:
            case AMIGA_KBDFAIL:
            case AMIGA_TERMPOWER:
            case AMIGA_UNKNOWN:
                dbgcons_amiga_key(0, code, "r");
                break;

            // a power-up stream: the keyboard (re)started, with nothing of
            // what it had down before still down and its caps lamp off
            case AMIGA_INITPOWER:
                dbgcons_amiga_key(0, code, "r");
                kbdin_keyboard_restarting(true);
                break;

            // ctrl-amiga-amiga: the amiga side has already acted on the key
            // events (amiga_send_rawkey's combo tracking, mirroring the usb
            // path) and is resetting. in the usb modes the keys went to the usb host
            // instead, which must not be left holding ctrl and both guis while
            // the keyboard restarts
            case AMIGA_RESET:
                dbgcons_amiga_key(0, code, "r");
                kbdin_keyboard_restarting(kbdin_to_usb());
                break;

            default: {
                bool up = (code & 0x80) != 0;
                uint8_t keycode = code & 0x7f;

                dbgcons_amiga_key(0, keycode, up ? "ru" : "rd");
                kbdin_key(keycode, up);
                break;
            }
        }
    }

    // drain first, act on the line state second: the key events leading up to a
    // ctrl-amiga-amiga reach the combo tracking before the keyboard's kclk hold
    // is considered, so the two paths agree on who owns the reset
    amiga_keyboard_recv_reset_task();
}

void amiga_keyboard_recv_release_all(void)
{
    kbdin_amiga_release();
    kbdin_usb_release();
}

void amiga_keyboard_recv_mode_changed(run_mode_t from, run_mode_t to)
{
    bool usb_before = run_mode_keyboard_usb(from);
    bool usb_after = run_mode_keyboard_usb(to);

    // keyboards keep going where they went (c64 <-> usb): nothing to do
    if (usb_before == usb_after)
        return;

    // let go of the side being left; the new side starts from nothing
    if (usb_before)
        kbdin_usb_release();
    else
        kbdin_amiga_release();

    // no keyboard seen yet: nothing says what its lamp shows, leave caps alone
    if (!kbd_seen)
        return;

    // the lamp is the truth; bring the side now receiving into line with it
    if (usb_after)
        kbdin_usb_caps_sync(false);
    else if (amiga_caps_lock() != caps_lamp)
        amiga_send_rawkey(AMIGA_CAPSLOCK, !caps_lamp);
}

#else // !HAS_KBD_IN

void amiga_keyboard_recv_init(void) {}
void amiga_keyboard_recv_task(void) {}
void amiga_keyboard_recv_mode_changed(run_mode_t from, run_mode_t to) { (void)from; (void)to; }
void amiga_keyboard_recv_release_all(void) {}

#endif // HAS_KBD_IN
