/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb device role on the native phy: cdc debug console and hid passthrough.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/critical_section.h"
#include "pico/stdio.h"
#include "pico/time.h"
#include "pico/stdio/driver.h"
#include "tusb.h"

#include "input_bridge_bt.h"
#include "usb_device.h"
#include "pad.h"
#include "util/output.h"

//--------------------------------------------------------------------+
// cdc console: an sdk stdio driver writing into a ring buffer which the main
// loop drains into the cdc fifo. the driver may be entered from interrupt
// context (bluetooth status printing), where tinyusb must not be called, hence
// the buffer in between.
//--------------------------------------------------------------------+

// enough for the boot banner and a few screens of hotplug chatter
#define CONSOLE_BUFSIZE 4096

static char console_buf[CONSOLE_BUFSIZE];
static uint32_t console_head, console_tail; // head: next write, tail: next read
static critical_section_t console_lock;

// everything printed, kept apart from the drain buffer so the console's log
// command can replay it (boot messages are gone from the drain buffer the
// moment anything opens the port)
#define CONSOLE_HISTORY 2048
static char console_history[CONSOLE_HISTORY];
static uint32_t history_head;           // next write
static bool history_wrapped;

static void console_out_chars(char const *buf, int len)
{
    critical_section_enter_blocking(&console_lock);

    for (int i = 0; i < len; i++) {
        console_history[history_head] = buf[i];
        history_head = (history_head + 1) % CONSOLE_HISTORY;
        if (history_head == 0)
            history_wrapped = true;
    }

    for (int i = 0; i < len; i++) {
        uint32_t next = (console_head + 1) % CONSOLE_BUFSIZE;

        // full: drop the rest of this write rather than tear older lines
        if (next == console_tail)
            break;

        console_buf[console_head] = buf[i];
        console_head = next;
    }

    critical_section_exit(&console_lock);
}

static stdio_driver_t console_driver = {
    .out_chars = console_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

// console input: printable characters gather into a line; return or newline
// hands it over (usb_device_console_line()). one finished line is held until
// the main loop takes it, and anything typed meanwhile is dropped
static char rx_line[USB_DEVICE_CONSOLE_LINE_MAX];
static uint8_t rx_length;
static char rx_done[USB_DEVICE_CONSOLE_LINE_MAX];
static volatile bool rx_ready;

static void console_receive(void)
{
    while (tud_cdc_available()) {
        int32_t c = tud_cdc_read_char();

        if (c < 0)
            break;

        if ((c == '\r') || (c == '\n')) {
            if (rx_length && !rx_ready) {
                memcpy(rx_done, rx_line, rx_length);
                rx_done[rx_length] = 0;
                rx_ready = true;
            }
            rx_length = 0;
        } else if ((c == 0x08) || (c == 0x7f)) {
            if (rx_length)
                rx_length--;
        } else if ((c >= 0x20) && (c < 0x7f) && (rx_length < (USB_DEVICE_CONSOLE_LINE_MAX - 1))) {
            rx_line[rx_length++] = (char)c;
        }
    }
}

/**
 * move whatever fits from the ring buffer into the cdc fifo; only whilst a
 * terminal is listening (dtr), so boot output waits for somebody to read it.
 */
static void console_drain(void)
{
    uint8_t chunk[64];
    uint32_t avail, count = 0;

    console_receive();

    if (!tud_cdc_connected())
        return;

    avail = tud_cdc_write_available();
    if (avail > sizeof(chunk))
        avail = sizeof(chunk);

    critical_section_enter_blocking(&console_lock);
    while ((count < avail) && (console_tail != console_head)) {
        chunk[count++] = (uint8_t)console_buf[console_tail];
        console_tail = (console_tail + 1) % CONSOLE_BUFSIZE;
    }
    critical_section_exit(&console_lock);

    if (count) {
        tud_cdc_write(chunk, count);
        tud_cdc_write_flush();
    }
}

//--------------------------------------------------------------------+
// hid passthrough state. the endpoint may still be busy with the previous
// report when a new one arrives, so the latest state is kept here and sent from
// the task when the endpoint is free: keys and buttons are levels (the newest
// report supersedes), mouse motion is accumulated so none is lost.
//--------------------------------------------------------------------+

static hid_keyboard_report_t slot_keyboard[INPUT_BRIDGE_MAX_SLOTS];
static uint8_t slot_buttons[INPUT_BRIDGE_MAX_SLOTS];
static bool keyboard_dirty;
static bool mouse_buttons_dirty;
static int32_t mouse_dx, mouse_dy, mouse_wheel, mouse_pan;

static bool stack_up;
static bool connected;

// the two gamepads' latest reports, sent when their endpoints are free
static hid_gamepad_report_t gamepad_report[2];
static uint16_t abs_x, abs_y;
static uint8_t abs_buttons;
static bool abs_dirty;
static bool gamepad_dirty[2];

// the "1200 baud touch": a host that sets the console to this rate asks for a
// reboot into the bootrom's usb boot mode (BOOTSEL), so new firmware can be
// copied over without anyone holding the button. the reboot waits a moment so
// the host's set-line-coding request completes before the device vanishes
#define USB_DEVICE_BOOTSEL_BAUD     1200
#define USB_DEVICE_BOOTSEL_DELAY_US 20000

static absolute_time_t bootsel_at;
static bool bootsel_pending;

// the host's caps lock, from the led output report it sends the keyboard
// interface: -1 until the host has said, else 0 / 1
static int8_t host_caps_lock = -1;

// a key tap in flight (caps lock, ctrl-alt-del): the press goes out in one
// report and the release in the next, since the slot state above keeps only
// the newest level and would fold a press and its release into nothing
typedef enum { CAPS_TAP_NONE = 0, CAPS_TAP_PRESS, CAPS_TAP_RELEASE } caps_tap_t;
static caps_tap_t caps_tap;
static uint8_t tap_modifier;        // the tap's own modifiers and key
static uint8_t tap_key;

// hid usage table, keyboard page: "keyboard errorrollover"
#define HID_USAGE_KEYBOARD_ERROR_ROLLOVER 0x01

static void hid_wake_host(void)
{
    // a suspended host which allowed remote wakeup gets woken by input
    if (tud_suspended())
        tud_remote_wakeup();
}

static void hid_merge_keyboard(hid_keyboard_report_t *out)
{
    uint8_t keys = 0;

    memset(out, 0, sizeof(*out));

    for (uint8_t slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++) {
        out->modifier |= slot_keyboard[slot].modifier;

        for (uint8_t pos = 0; pos < 6; pos++) {
            uint8_t code = slot_keyboard[slot].keycode[pos];
            bool dup = false;

            if (code == 0)
                continue;

            for (uint8_t k = 0; k < keys; k++)
                if (out->keycode[k] == code)
                    dup = true;

            if (dup)
                continue;

            // more than six keys down across all keyboards: boot protocol's
            // answer is phantom state (all keys = errorrollover)
            if (keys == 6) {
                memset(out->keycode, HID_USAGE_KEYBOARD_ERROR_ROLLOVER, sizeof(out->keycode));
                return;
            }

            out->keycode[keys++] = code;
        }
    }
}

static uint8_t hid_merge_buttons(void)
{
    uint8_t buttons = 0;

    for (uint8_t slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++)
        buttons |= slot_buttons[slot];

    return buttons;
}

static int8_t hid_take_axis(int32_t *accum)
{
    int32_t step = *accum;

    if (step > 127)
        step = 127;
    if (step < -127)
        step = -127;

    *accum -= step;
    return (int8_t)step;
}

static void hid_flush(void)
{
    if (!tud_mounted()) {
        // nobody to send to: levels stay (and go out on mount), motion is stale,
        // and a tap or the host's led state means nothing to the next host
        mouse_dx = mouse_dy = mouse_wheel = mouse_pan = 0;
        caps_tap = CAPS_TAP_NONE;
        host_caps_lock = -1;
        return;
    }

    if ((caps_tap != CAPS_TAP_NONE) && tud_hid_n_ready(USB_DEVICE_HID_KEYBOARD)) {
        hid_keyboard_report_t merged;

        hid_merge_keyboard(&merged);

        if (caps_tap == CAPS_TAP_PRESS) {
            // into the first free position; with six keys already down the
            // last one gives way for this one report
            uint8_t pos = 0;

            while ((pos < 5) && (merged.keycode[pos] != 0))
                pos++;
            merged.keycode[pos] = tap_key;

            if (tud_hid_n_keyboard_report(USB_DEVICE_HID_KEYBOARD, 0, merged.modifier | tap_modifier, merged.keycode))
                caps_tap = CAPS_TAP_RELEASE;
        } else if (tud_hid_n_keyboard_report(USB_DEVICE_HID_KEYBOARD, 0, merged.modifier, merged.keycode)) {
            // the release report carries the current state, so it also
            // delivers anything that changed whilst the tap was in flight
            caps_tap = CAPS_TAP_NONE;
            keyboard_dirty = false;
        }
    }

    if (keyboard_dirty && (caps_tap == CAPS_TAP_NONE) && tud_hid_n_ready(USB_DEVICE_HID_KEYBOARD)) {
        hid_keyboard_report_t merged;

        hid_merge_keyboard(&merged);
        if (tud_hid_n_keyboard_report(USB_DEVICE_HID_KEYBOARD, 0, merged.modifier, merged.keycode))
            keyboard_dirty = false;
    }

    for (uint8_t player = 0; player < 2; player++) {
        uint8_t instance = player ? USB_DEVICE_HID_GAMEPAD2 : USB_DEVICE_HID_GAMEPAD;

        if (gamepad_dirty[player] && tud_hid_n_ready(instance)
            && tud_hid_n_report(instance, 0, &gamepad_report[player], sizeof(gamepad_report[player])))
            gamepad_dirty[player] = false;
    }

    if (abs_dirty && tud_hid_n_ready(USB_DEVICE_HID_ABS)
        && tud_hid_n_abs_mouse_report(USB_DEVICE_HID_ABS, 0, abs_buttons, (int16_t)abs_x, (int16_t)abs_y, 0, 0))
        abs_dirty = false;

    if ((mouse_buttons_dirty || mouse_dx || mouse_dy || mouse_wheel || mouse_pan)
        && tud_hid_n_ready(USB_DEVICE_HID_MOUSE)) {
        int32_t dx = mouse_dx, dy = mouse_dy, wheel = mouse_wheel, pan = mouse_pan;
        int8_t x = hid_take_axis(&dx), y = hid_take_axis(&dy);
        int8_t v = hid_take_axis(&wheel), h = hid_take_axis(&pan);

        // only consume the motion once the report is actually queued
        if (tud_hid_n_mouse_report(USB_DEVICE_HID_MOUSE, 0, hid_merge_buttons(), x, y, v, h)) {
            mouse_dx = dx;
            mouse_dy = dy;
            mouse_wheel = wheel;
            mouse_pan = pan;
            mouse_buttons_dirty = false;
        }
    }
}

//--------------------------------------------------------------------+
// public api
//--------------------------------------------------------------------+

void usb_device_init(void)
{
    critical_section_init(&console_lock);
    stdio_set_driver_enabled(&console_driver, true);
}

void usb_device_task(bool vbus_present)
{
    if (!stack_up) {
        // dcd_init() presents the pull-up at once, so the stack is not even
        // initialised until there is a powered port to present it to
        if (!vbus_present)
            return;

        tusb_rhport_init_t const dev_init = {
            .role = TUSB_ROLE_DEVICE,
            .speed = TUSB_SPEED_AUTO,
        };

        if (!tusb_init(BOARD_TUD_RHPORT, &dev_init)) {
            ahprintf("[usbd] device stack failed to start\n");
            return;
        }

        stack_up = true;
        connected = true;
        ahprintf("[usbd] usb-c vbus present, device stack up\n");
    } else if (vbus_present && !connected) {
        tud_connect();
        connected = true;
        ahprintf("[usbd] usb-c vbus present, connecting\n");
    } else if (!vbus_present && connected) {
        tud_disconnect();
        connected = false;
        ahprintf("[usbd] usb-c vbus gone, disconnected\n");
    }

    tud_task();

    if (bootsel_pending && time_reached(bootsel_at)) {
        // no return: the bootrom takes over and the board comes back as the
        // RP2350 BOOTSEL drive; every gpio is released on the way
        reset_usb_boot(0, 0);
    }

    if (!connected)
        return;

    console_drain();
    hid_flush();
}

#ifdef USB_DEVICE_BRINGUP_SERVICE
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

// above the default 0x80, so the console is served even from under a stuck
// default-priority handler
#define BRINGUP_IRQ_PRIORITY 0x40

static alarm_pool_t *bringup_pool;
static repeating_timer_t bringup_timer;

static volatile uint32_t bringup_ticks;

static bool bringup_service_cb(repeating_timer_t *rt)
{
    (void)rt;
    bringup_ticks++;
    watchdog_update();
    usb_device_task(true);
    return true;
}

uint32_t usb_device_bringup_ticks(void)
{
    return bringup_ticks;
}

void usb_device_bringup_service_start(void)
{
    // stack up from thread context first
    usb_device_task(true);
    irq_set_priority(USBCTRL_IRQ, BRINGUP_IRQ_PRIORITY);

    bringup_pool = alarm_pool_create_with_unused_hardware_alarm(2);
    irq_set_priority(hardware_alarm_get_irq_num(alarm_pool_hardware_alarm_num(bringup_pool)),
        BRINGUP_IRQ_PRIORITY);
    alarm_pool_add_repeating_timer_ms(bringup_pool, 1, bringup_service_cb, NULL, &bringup_timer);

    ahprintf("[usbd] bring-up service: console served from a timer irq\n");
}
#endif

bool usb_device_keyboard_idle(void)
{
    return !keyboard_dirty && (caps_tap == CAPS_TAP_NONE);
}

bool usb_device_mounted(void)
{
    return stack_up && tud_mounted();
}

void usb_device_keyboard(uint8_t slot, hid_keyboard_report_t const *report)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    slot_keyboard[slot] = *report;
    keyboard_dirty = true;
    hid_wake_host();
}

void usb_device_mouse_abs(uint8_t buttons, uint16_t x, uint16_t y)
{
    abs_x = (x > 32767) ? 32767 : x;
    abs_y = (y > 32767) ? 32767 : y;
    abs_buttons = buttons;
    abs_dirty = true;
    hid_wake_host();
}

void usb_device_mouse(uint8_t slot, hid_mouse_report_t const *report)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    if (slot_buttons[slot] != report->buttons) {
        slot_buttons[slot] = report->buttons;
        mouse_buttons_dirty = true;
    }

    mouse_dx += report->x;
    mouse_dy += report->y;
    mouse_wheel += report->wheel;
    mouse_pan += report->pan;

    hid_wake_host();
}

/**
 * a player's pad, passed through to the host as it is: sticks, triggers (as
 * rx / ry), the hat (a d-pad without one becomes one) and the pad's own
 * buttons in its own order. the host maps them.
 */
void pads_output_usb(uint8_t player, pad_player_t const *source)
{
    static uint8_t const hat_for_dpad[16] = {
        GAMEPAD_HAT_CENTERED, GAMEPAD_HAT_UP, GAMEPAD_HAT_DOWN, GAMEPAD_HAT_CENTERED,
        GAMEPAD_HAT_LEFT, GAMEPAD_HAT_UP_LEFT, GAMEPAD_HAT_DOWN_LEFT, GAMEPAD_HAT_LEFT,
        GAMEPAD_HAT_RIGHT, GAMEPAD_HAT_UP_RIGHT, GAMEPAD_HAT_DOWN_RIGHT, GAMEPAD_HAT_RIGHT,
        GAMEPAD_HAT_CENTERED, GAMEPAD_HAT_UP, GAMEPAD_HAT_DOWN, GAMEPAD_HAT_CENTERED,
    };
    hid_gamepad_report_t report;
    pad_input_t const *in;

    if ((player > 1) || !source)
        return;

    in = &source->input;
    memset(&report, 0, sizeof(report));

    if (source->active) {
        report.x = in->lx;
        report.y = in->ly;
        report.z = in->rx;
        report.rz = in->ry;
        report.rx = (int8_t)((in->lt > 0) ? ((int)in->lt - 128) : -127);
        report.ry = (int8_t)((in->rt > 0) ? ((int)in->rt - 128) : -127);
        // our hat counts 0 = up clockwise, 8 = centred; tinyusb's 1 = up, 0 = centred
        report.hat = (in->hat < PAD_HAT_CENTRED) ? (uint8_t)(in->hat + 1) : hat_for_dpad[in->dpad & 0x0f];
        report.buttons = in->buttons;
    }

    gamepad_report[player] = report;
    gamepad_dirty[player] = true;
    hid_wake_host();
}

size_t usb_device_console_history(char *out, size_t size)
{
    size_t length, start, n;

    critical_section_enter_blocking(&console_lock);
    length = history_wrapped ? CONSOLE_HISTORY : history_head;
    start = history_wrapped ? history_head : 0;
    n = (length < size) ? length : size;
    for (size_t i = 0; i < n; i++)
        out[i] = console_history[(start + (length - n) + i) % CONSOLE_HISTORY];
    critical_section_exit(&console_lock);

    return n;
}

bool usb_device_console_line(char *line, size_t size)
{
    if (!rx_ready || (size == 0))
        return false;

    strncpy(line, rx_done, size - 1);
    line[size - 1] = 0;
    rx_ready = false;
    return true;
}

int usb_device_host_caps_lock(void)
{
    return host_caps_lock;
}

void usb_device_key_tap(uint8_t modifier, uint8_t key)
{
    if (caps_tap != CAPS_TAP_NONE)
        return;

    tap_modifier = modifier;
    tap_key = key;
    caps_tap = CAPS_TAP_PRESS;
    hid_wake_host();
}

void usb_device_caps_tap(void)
{
    usb_device_key_tap(0, HID_KEY_CAPS_LOCK);
}

bool usb_device_caps_tap_pending(void)
{
    return caps_tap != CAPS_TAP_NONE;
}

void usb_device_release_slot(uint8_t slot)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    if (memcmp(&slot_keyboard[slot], &(hid_keyboard_report_t){ 0 }, sizeof(slot_keyboard[slot])) != 0) {
        memset(&slot_keyboard[slot], 0, sizeof(slot_keyboard[slot]));
        keyboard_dirty = true;
    }

    if (slot_buttons[slot]) {
        slot_buttons[slot] = 0;
        mouse_buttons_dirty = true;
    }
}

void usb_device_release_all(void)
{
    for (uint8_t slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++)
        usb_device_release_slot(slot);

    mouse_dx = mouse_dy = mouse_wheel = mouse_pan = 0;
}

//--------------------------------------------------------------------+
// tinyusb cdc device callbacks
//--------------------------------------------------------------------+

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding)
{
    (void)itf;

    if ((p_line_coding != NULL) && (p_line_coding->bit_rate == USB_DEVICE_BOOTSEL_BAUD)) {
        ahprintf("[usbd] 1200 baud touch: rebooting into BOOTSEL\n");
        bootsel_at = make_timeout_time_us(USB_DEVICE_BOOTSEL_DELAY_US);
        bootsel_pending = true;
    }
}

//--------------------------------------------------------------------+
// tinyusb hid device callbacks
//--------------------------------------------------------------------+

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
    uint8_t *buffer, uint16_t reqlen)
{
    // nothing to poll; a stall tells the host to use the interrupt endpoint
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
    uint8_t const *buffer, uint16_t bufsize)
{
    // the host's keyboard leds arrive here. the usb-a keyboards follow the
    // amiga's caps lock, not this host's, so only the caps state is kept: a
    // real amiga keyboard forwarded to this host syncs its latched caps against
    // it (keyboard_recv.c)
    (void)report_id;

    if ((instance == USB_DEVICE_HID_KEYBOARD) && (report_type == HID_REPORT_TYPE_OUTPUT) && (bufsize >= 1))
        host_caps_lock = (buffer[0] & KEYBOARD_LED_CAPSLOCK) ? 1 : 0;
}

