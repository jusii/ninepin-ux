/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * the board's test of itself; see selftest.h.
 */

#include "selftest.h"
#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "tusb.h"

#include "build_stamp.h"
#include "display/disp_ssd.h"
#include "platform/amiga/de9.h"
#include "platform/amiga/keyboard_recv.h"
#include "platform/amiga/keyboard_serial_io.h"
#include "settings.h"
#include "util/amiga_sense.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/status_led.h"

#ifdef ENABLE_BLUETOOTH_HID
#include "bt_hid.h"
#include "radio.h"
#endif

#define LISTEN_MS       8000u   // how long the radio is given to hear anything
#define STEP_MS         10000u  // each wait for a hand
#define COLOUR_MS       600u    // the led's red, green and blue
#define VERDICT_MS      15000u  // the led stays on the result this long
#define FLASH_BYTES_MIN (16u * 1024u * 1024u)

typedef enum {
    PHASE_IDLE = 0,
    PHASE_LISTEN,       // the checks that ask nothing; the radio listens
    PHASE_BOOT,         // BOOT within 10 s: the button, and the checks by hand
    PHASE_ENCODER,
    PHASE_LED,
    PHASE_DISPLAY,
} phase_t;

typedef enum { PASS, FAIL, SKIP, NOTE } kind_t;

static char report[1800];
static size_t report_len;
static unsigned failed, skipped;

static volatile bool wanted;
static volatile bool running;
static phase_t phase;
static uint32_t phase_since;
#ifdef ENABLE_BLUETOOTH_HID
static uint32_t heard_before;
#endif

// the encoder's three signs of life
static bool enc_cw, enc_ccw, enc_press;

// the led after the run: the result, for a while
static uint32_t verdict_until;

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static void say(kind_t kind, char const *format, ...) __attribute__((format(printf, 2, 3)));

static void say(kind_t kind, char const *format, ...)
{
    static char const *const heads[] = { "pass  ", "FAIL  ", "--    ", "      " };
    char line[200];
    va_list args;
    int n;

    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);

    if (kind == FAIL)
        failed++;
    else if (kind == SKIP)
        skipped++;

    ahprintf("[test] %s%s\n", heads[kind], line);
    n = snprintf(report + report_len, sizeof(report) - report_len, "%s%s\n", heads[kind], line);
    if ((n > 0) && ((size_t)n < sizeof(report) - report_len))
        report_len += (size_t)n;
}

/**
 * what the person at the board is asked for: the console and the display
 */
static void prompt(char const *text)
{
    char line[22];

    ahprintf("[test] >> %s\n", text);
    snprintf(line, sizeof(line), "%-21.21s", text);
    disp_write(0, 3, line);
}

static void enter(phase_t next)
{
    phase = next;
    phase_since = now_ms();
}

//--------------------------------------------------------------------+
// the checks that ask nothing
//--------------------------------------------------------------------+

static void read_flash_id(void *context)
{
    uint8_t tx[4] = { 0x9f, 0, 0, 0 }, rx[4] = { 0, 0, 0, 0 };

    flash_do_cmd(tx, rx, sizeof(tx));
    memcpy(context, rx + 1, 3);
}

static void check_flash(void)
{
    uint8_t id[3] = { 0, 0, 0 };
    uint32_t bytes;

    if (flash_safe_execute(read_flash_id, id, 500) != PICO_OK) {
        say(FAIL, "flash: its id could not be read");
        return;
    }
    bytes = ((id[2] >= 16) && (id[2] <= 28)) ? (1u << id[2]) : 0;
    if (bytes >= FLASH_BYTES_MIN)
        say(PASS, "flash: %u MB (id %02x%02x%02x)", (unsigned)(bytes >> 20), id[0], id[1], id[2]);
    else
        say(FAIL, "flash: id %02x%02x%02x, not the 16 MB part", id[0], id[1], id[2]);
}

static void check_keyboard_loop(void)
{
    static uint8_t const codes[] = { 0x55, 0x2a, 0x00, 0x7f, 0xd5, 0xaa, 0x33, 0xcc };
    unsigned wrong = 0, unacked = 0, missing = 0;
    uint8_t got[4];
    bool there, false_ack;

    amiga_keyboard_recv_selftest(true);

    // is there a cable? the output's data line held low, with no clock: no
    // amiga takes that for anything
    amiga_selftest_data(true);
    busy_wait_us(300);
    there = amiga_keyboard_recv_selftest_data_low();
    amiga_selftest_data(false);
    busy_wait_us(500);

    if (!there) {
        amiga_keyboard_recv_selftest(false);
        say(SKIP, "keyboard ports: not tested (no cable from KBD OUT to KBD IN)");
        return;
    }

    // the output is the keyboard, the input the amiga: each code must arrive
    // as sent, and be acknowledged
    amiga_keyboard_recv_selftest(true);
    for (unsigned i = 0; i < sizeof(codes); i++) {
        unsigned count;

        if (!amiga_selftest_send(codes[i]))
            unacked++;
        amiga_keyboard_recv_task();
        count = amiga_keyboard_recv_selftest_take(got, sizeof(got));
        if (!count)
            missing++;
        else if ((count != 1) || (got[0] != codes[i]))
            wrong++;
    }
    // and the other way about: with the input stopped nothing acknowledges,
    // and the output must notice (or its "acknowledged" above means nothing)
    amiga_keyboard_recv_selftest_deaf(true);
    false_ack = amiga_selftest_send(0x55);
    amiga_keyboard_recv_selftest_deaf(false);
    amiga_keyboard_recv_selftest(false);

    if (false_ack)
        say(FAIL, "keyboard ports: KBD OUT sees an acknowledge when there is none (its data line is slow to rise)");
    else if (!wrong && !unacked && !missing)
        say(PASS, "keyboard ports: %u codes out of KBD OUT, all in at KBD IN and acknowledged",
            (unsigned)sizeof(codes));
    else
        say(FAIL, "keyboard ports: of %u codes %u did not arrive, %u arrived wrong, %u were not acknowledged",
            (unsigned)sizeof(codes), missing, wrong, unacked);
}

static void check_de9_loop(void)
{
    static char faults[400];

    switch (de9_selftest(faults, sizeof(faults))) {
        case DE9_SELFTEST_PASS:
            say(PASS, "de9 ports: 7 lines each way, port 1 to port 2");
            break;
        case DE9_SELFTEST_FAIL:
            say(FAIL, "de9 ports: %s", faults);
            break;
        default:
            if (!amiga_sense_is_on())
                say(SKIP, "de9 ports: not tested (no 5 V on the amiga side: it comes with the keyboard cable)");
            else
                say(SKIP, "de9 ports: not tested (no cable from port 1 to port 2; %s)", faults);
            break;
    }
}

static void check_usb_a(void)
{
    for (uint8_t address = 1; address <= CFG_TUH_DEVICE_MAX; address++) {
        if (tuh_mounted(address)) {
            say(PASS, "usb-a: a device is plugged in and answers");
            return;
        }
    }
    say(SKIP, "usb-a: not tested (nothing plugged in)");
}

static void begin(void)
{
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];

    report_len = 0;
    report[0] = 0;
    failed = skipped = 0;
    enc_cw = enc_ccw = enc_press = false;
    verdict_until = 0;

    pico_get_unique_board_id_string(serial, sizeof(serial));
    say(NOTE, "Ninepin UX self-test: board %s", serial);
    say(NOTE, "firmware %s, built %s", ahp_version, ahp_build_stamp);

    // the test's own colour whilst it runs
    status_led_override(true, false, false, true);

#if !PICO_RP2040
    say(PASS, "chip: rp2350 rev a%u", rp2350_chip_version());
#endif
    check_flash();

    if (io_expander_present())
        say(PASS, "i/o expander: answers%s", io_expander_vbus_present() ? ", usb-c power seen" : "");
    else
        say(FAIL, "i/o expander: no answer");

    if (disp_present())
        say(PASS, "display: answers");
    else
        say(SKIP, "display: not tested (none answers: not fitted?)");

    say(NOTE, "amiga side: %u mV on the sense input, read as %s", amiga_sense_millivolts(),
        amiga_sense_is_on() ? "on" : "off");

#ifdef ENABLE_BLUETOOTH_HID
    if (radio_up())
        say(PASS, "radio: started");
    else
        say(FAIL, "radio: did not start");
    heard_before = bt_hid_heard();
#else
    say(SKIP, "radio: not tested (this firmware is built without it)");
#endif

    enter(PHASE_LISTEN);
}

/**
 * the listening is over: the rest of what asks nothing, then the button
 */
static void automatic_rest(bool heard)
{
#ifdef ENABLE_BLUETOOTH_HID
    if (heard)
        say(PASS, "bluetooth: hears other devices");
    else if (!settings_get()->bt_enabled)
        say(SKIP, "bluetooth reception: not tested (Bluetooth is switched off)");
    else
        say(SKIP, "bluetooth reception: not tested (nothing heard in %u s: no device near, or not searching)",
            LISTEN_MS / 1000u);
#else
    (void)heard;
#endif

    check_keyboard_loop();
    check_de9_loop();
    check_usb_a();

    prompt("BOOT: manual checks");
    enter(PHASE_BOOT);
}

//--------------------------------------------------------------------+
// the end
//--------------------------------------------------------------------+

static void finish(void)
{
    if (failed)
        say(NOTE, "RESULT: FAILED (%u failed, %u not tested)", failed, skipped);
    else if (skipped)
        say(NOTE, "RESULT: passed, %u not tested", skipped);
    else
        say(NOTE, "RESULT: all passed");
    say(NOTE, "not covered: the a500 headers, 5 V in on de9 pin 7, a real machine");

    // red: a failure; red and green (amber): passed but not everything; green
    status_led_override(true, failed || skipped, !failed, false);
    verdict_until = now_ms() + VERDICT_MS;
    if (!verdict_until)
        verdict_until = 1;

    dbgcons_redraw();
    dbgcons_notice(failed ? "selftest: FAILED" : skipped ? "selftest: incomplete" : "selftest: passed");

    phase = PHASE_IDLE;
    running = false;
}

static void next_manual(phase_t done)
{
    switch (done) {
        case PHASE_BOOT:
            prompt("encoder: turn, press");
            ahprintf("[test]    (turn it both ways and press it; BOOT if there is none)\n");
            enter(PHASE_ENCODER);
            return;

        case PHASE_ENCODER:
            prompt("LED r,g,b? BOOT=yes");
            enter(PHASE_LED);
            return;

        case PHASE_LED:
            status_led_override(true, false, false, true);
            if (!disp_present()) {
                finish();
                return;
            }
            disp_write(0, 0, "#####################");
            disp_write(0, 1, "ABCDEFGHIJKLMNOPQRSTU");
            disp_write(0, 2, "0123456789 +-*/=<>()[");
            prompt("display ok? BOOT=yes");
            enter(PHASE_DISPLAY);
            return;

        default:
            finish();
            return;
    }
}

//--------------------------------------------------------------------+
// the board's own controls
//--------------------------------------------------------------------+

bool selftest_button(user_button_event_t event)
{
    if (!running)
        return false;

    // a press however long: BOOT means "yes" and nothing else for the while
    if ((event != USER_BUTTON_EVENT_PRESS) && (event != USER_BUTTON_EVENT_LONG_PRESS))
        return true;

    switch (phase) {
        case PHASE_BOOT:
            say(PASS, "BOOT button");
            next_manual(PHASE_BOOT);
            break;
        case PHASE_ENCODER:
            if (enc_cw || enc_ccw || enc_press)
                say(FAIL, "encoder: only%s%s%s seen", enc_cw ? " clockwise" : "", enc_ccw ? " anticlockwise" : "",
                    enc_press ? " its switch" : "");
            else
                say(SKIP, "encoder: not tested (none fitted)");
            next_manual(PHASE_ENCODER);
            break;
        case PHASE_LED:
            say(PASS, "led: red, green and blue seen");
            next_manual(PHASE_LED);
            break;
        case PHASE_DISPLAY:
            say(PASS, "display: the pattern seen");
            next_manual(PHASE_DISPLAY);
            break;
        default:
            break;
    }
    return true;
}

bool selftest_encoder(io_expander_event_t event)
{
    if (!running)
        return false;

    if (phase == PHASE_ENCODER) {
        if (event == IO_EXPANDER_EVENT_ENC_CW) enc_cw = true;
        if (event == IO_EXPANDER_EVENT_ENC_CCW) enc_ccw = true;
        if (event == IO_EXPANDER_EVENT_ENC_PRESS) enc_press = true;
        // each sign of life gives the next its time
        phase_since = now_ms();
        if (enc_cw && enc_ccw && enc_press) {
            say(PASS, "encoder: both ways and its switch");
            next_manual(PHASE_ENCODER);
        }
    }
    // the encoder drives no menu during a run (usb-c power events pass)
    return (event != IO_EXPANDER_EVENT_VBUS_ON) && (event != IO_EXPANDER_EVENT_VBUS_OFF);
}

//--------------------------------------------------------------------+
// main loop
//--------------------------------------------------------------------+

bool selftest_start(void)
{
    if (running || wanted)
        return false;
    wanted = true;
    return true;
}

bool selftest_running(void)
{
    return running || wanted;
}

char const *selftest_report(void)
{
    return report;
}

void selftest_task(void)
{
    uint32_t now = now_ms(), spent;

    // the result has been on the led long enough
    if (verdict_until && !running && ((int32_t)(now - verdict_until) >= 0)) {
        verdict_until = 0;
        status_led_override(false, false, false, false);
    }

    if (wanted && !running) {
        running = true;
        wanted = false;
        begin();
        return;
    }
    if (!running)
        return;

    spent = now - phase_since;

    switch (phase) {
        case PHASE_LISTEN: {
#ifdef ENABLE_BLUETOOTH_HID
            bool heard = bt_hid_heard() != heard_before;

            if (heard || !settings_get()->bt_enabled || (spent >= LISTEN_MS))
                automatic_rest(heard);
#else
            automatic_rest(false);
#endif
            break;
        }

        case PHASE_BOOT:
            // the led asks: blue, blinking
            status_led_override(true, false, false, ((spent / 250u) & 1u) == 0);
            if (spent >= STEP_MS) {
                say(SKIP, "BOOT button, encoder, led colours, display pattern: not tested (BOOT not pressed)");
                finish();
            }
            break;

        case PHASE_ENCODER:
            if (spent >= STEP_MS) {
                if (enc_cw || enc_ccw || enc_press)
                    say(FAIL, "encoder: only%s%s%s seen", enc_cw ? " clockwise" : "",
                        enc_ccw ? " anticlockwise" : "", enc_press ? " its switch" : "");
                else
                    say(SKIP, "encoder: not tested (nothing from it in %u s)", STEP_MS / 1000u);
                next_manual(PHASE_ENCODER);
            }
            break;

        case PHASE_LED: {
            unsigned colour = (spent / COLOUR_MS) % 3u;

            status_led_override(true, colour == 0, colour == 1, colour == 2);
            if (spent >= STEP_MS) {
                say(SKIP, "led colours: not confirmed");
                next_manual(PHASE_LED);
            }
            break;
        }

        case PHASE_DISPLAY:
            if (spent >= STEP_MS) {
                say(SKIP, "display pattern: not confirmed");
                next_manual(PHASE_DISPLAY);
            }
            break;

        default:
            break;
    }
}
