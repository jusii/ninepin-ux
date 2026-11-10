/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * debug console routines.
 */

#include <stdint.h>
#include <stdio.h>

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "config.h"
#include "debug_cons.h"
#include "display/disp_ssd.h"
#include "output.h"

#define DBGCONS_OLED_COLS 21

struct
{
    uint8_t hid_keyboard, hid_mouse, hid_controller;
    uint8_t plug_events, unplug_events;
} debug_counters;

#ifdef ENABLE_BLUETOOTH_HID
static bool bt_passkey_active;
// line 3 when no bluetooth prompt is up: a firmware update (ota.c) over the
// wi-fi state (wifi.c)
static char wifi_text[DBGCONS_OLED_COLS + 1];
static char ota_text[DBGCONS_OLED_COLS + 1];
static bool net_active;

static char const *line3_text(void)
{
    if (ota_text[0])
        return ota_text;
    return net_active ? "net ctl active" : wifi_text;
}
#endif

// the display setting (settings.h, enum settings_display): 0 status, 1 hid
// diagnostics, 2 mouse diagnostics, 3 off. the DEBUG_* build flags force the
// diagnostics on regardless
#define DBGCONS_DISPLAY_STATUS  0
#define DBGCONS_DISPLAY_HID     1
#define DBGCONS_DISPLAY_MOUSE   2
#define DBGCONS_DISPLAY_OFF     3

static uint8_t display_mode = DBGCONS_DISPLAY_STATUS;

static bool diag_hid(void)
{
#ifdef DEBUG_HID_STATUS
    return true;
#else
    return display_mode == DBGCONS_DISPLAY_HID;
#endif
}

static bool diag_mouse(void)
{
#ifdef DEBUG_MOUSE
    return true;
#else
    return display_mode == DBGCONS_DISPLAY_MOUSE;
#endif
}

// what lines 1-3 last showed, for a redraw after the display comes back on
static char line_cache[4][DBGCONS_OLED_COLS + 1];

static void dbgcons_write_padded_line(uint8_t line, char const *message)
{
    char linebuf[DBGCONS_OLED_COLS + 1] = "";

    snprintf(linebuf, sizeof(linebuf), "%-*.*s", DBGCONS_OLED_COLS, DBGCONS_OLED_COLS,
        message != NULL ? message : "");
    if (line < 4)
        memcpy(line_cache[line], linebuf, sizeof(linebuf));
    disp_write(0, line, linebuf);
}

// the board status line: output mode and amiga power. it shares oled line 2
// with the DEBUG_HID_STATUS readout, which wins until the next status change
#define DBGCONS_STATUS_LINE 2

static char const *status_mode = "amiga";
static bool status_amiga_on = true;

static void dbgcons_status_line(void)
{
    char linebuf[DBGCONS_OLED_COLS + 1] = "";

    snprintf(linebuf, sizeof(linebuf), "out:%s amiga:%s", status_mode, status_amiga_on ? "on" : "off");
    dbgcons_write_padded_line(DBGCONS_STATUS_LINE, linebuf);
}

void dbgcons_init()
{
    ahprintf(
        VT_ED_CLS "Ninepin UX, https://github.com/jusii/ninepin-ux, based on amigahid-pico by nine <nine@aphlor.org>"
    );

    debug_counters.hid_keyboard = 0;
    debug_counters.hid_mouse = 0;
    debug_counters.hid_controller = 0;
    debug_counters.plug_events = 0;
    debug_counters.unplug_events = 0;

    dbgcons_print_counters();
#ifdef ENABLE_BLUETOOTH_HID
    dbgcons_bt_status("bt off");
#endif
#ifdef DEBUG_HID_STATUS
    disp_write(0, 2, "hid --");
#endif
#ifdef ENABLE_BLUETOOTH_HID
    dbgcons_bt_passkey_clear();
#elif defined(DEBUG_MOUSE)
    disp_write(0, 3, "mouse --");
#endif
}

void dbgcons_print_counters()
{
    char linebuf[32] = "";

    ahprintf(
        VT_CUP_POS VT_EL_LIN
        "[system] key: %02x mouse: %02x joy: %02x total plug: %02x total unplug: %02x\n",
        3, 1,
        debug_counters.hid_keyboard,
        debug_counters.hid_mouse,
        debug_counters.hid_controller,
        debug_counters.plug_events,
        debug_counters.unplug_events
    );

    sprintf(
        linebuf,
        "usb    k:%02x m:%02x j:%02x",
        debug_counters.hid_keyboard,
        debug_counters.hid_mouse,
        debug_counters.hid_controller
    );

    disp_write(0, 0, linebuf);
}

void dbgcons_plug(enum debug_plug_types devtype)
{
    switch (devtype) {
        case AP_H_KEYBOARD:
            debug_counters.hid_keyboard++;
            break;
        case AP_H_MOUSE:
            debug_counters.hid_mouse++;
            break;
        case AP_H_CONTROLLER:
            debug_counters.hid_controller++;
            break;
        case AP_H_UNKNOWN:
        default:
            break;
    }
    debug_counters.plug_events++;
    dbgcons_print_counters();
}

void dbgcons_unplug(enum debug_plug_types devtype)
{
    switch (devtype) {
        case AP_H_KEYBOARD:
            debug_counters.hid_keyboard--;
            break;
        case AP_H_MOUSE:
            debug_counters.hid_mouse--;
            break;
        case AP_H_CONTROLLER:
            debug_counters.hid_controller--;
            break;
        case AP_H_UNKNOWN:
        default:
            break;
    }
    debug_counters.unplug_events++;
    dbgcons_print_counters();
}

void dbgcons_amiga_key(uint8_t incode, uint8_t outcode, char *updown)
{
#ifdef DEBUG_KEYBOARD
    char linebuf[32] = "";

    ahprintf(
        VT_CUP_POS VT_EL_LIN
        "[amigak] hid in: %02x amiga out: %02x up/down: %s\n",
        4, 1,
        incode, outcode, updown
    );

    sprintf(
        linebuf,
        "amikb hid:%02x ami:%02x %s",
        incode, outcode, updown
    );

#ifndef ENABLE_BLUETOOTH_HID
    disp_write(0, 1, linebuf);
#endif
#else
    (void)incode;
    (void)outcode;
    (void)updown;
#endif
}

void dbgcons_amiga_mod(uint8_t outcode, char updown)
{
    // ls rs cl ct la ra lam ram
}

void dbgcons_hid_status(uint8_t dev_addr, uint8_t instance, uint8_t hid_protocol, bool receive_ok, uint8_t report_count, bool mounted)
{
    if (diag_hid()) {
    char linebuf[32] = "";

    snprintf(
        linebuf,
        sizeof(linebuf),
        "hid %c a%02x i%02x p%02x r%u",
        mounted ? '+' : '-',
        dev_addr,
        instance,
        hid_protocol,
        report_count
    );

    ahprintf(
        VT_CUP_POS VT_EL_LIN
        "[hid] %s addr:%02x inst:%02x proto:%02x reports:%u rx:%s\n",
        5, 1,
        mounted ? "mount" : "umount",
        dev_addr,
        instance,
        hid_protocol,
        report_count,
        receive_ok ? "ok" : "fail"
    );

    disp_write(0, 2, linebuf);

    snprintf(
        linebuf,
        sizeof(linebuf),
        "rx %s",
        receive_ok ? "ok" : "fail"
    );
    disp_write(17, 2, linebuf);
    }
}

void dbgcons_mouse_report(int16_t x, int16_t y, uint8_t buttons)
{
    if (diag_mouse()) {
    char linebuf[32] = "";

    snprintf(
        linebuf,
        sizeof(linebuf),
        "m x%+04d y%+04d b%02x",
        x,
        y,
        buttons
    );

    ahprintf(
        VT_CUP_POS VT_EL_LIN
        "[mouse] x:%d y:%d buttons:%02x\n",
        6, 1,
        x,
        y,
        buttons
    );

#ifdef ENABLE_BLUETOOTH_HID
    if (!bt_passkey_active)
        disp_write(0, 3, linebuf);
#else
    disp_write(0, 3, linebuf);
#endif
    }
}

void dbgcons_mouse_wheel(int8_t wheel)
{
    if (diag_mouse()) {
    char linebuf[32] = "";

    snprintf(linebuf, sizeof(linebuf), "wheel %+d", wheel);

    ahprintf(
        VT_CUP_POS VT_EL_LIN
        "[mouse] wheel:%d\n",
        7, 1,
        wheel
    );

#ifdef ENABLE_BLUETOOTH_HID
    if (!bt_passkey_active)
        disp_write(0, 4, linebuf);
#else
    disp_write(0, 4, linebuf);
#endif
    }
}

void dbgcons_tankmouse_status(uint16_t queued, uint16_t requests, uint16_t responses)
{
#ifdef DEBUG_MOUSE
    char linebuf[32] = "";

    snprintf(
        linebuf,
        sizeof(linebuf),
        "tm q%u rq%u rs%u",
        queued,
        requests,
        responses
    );

    ahprintf(
        VT_CUP_POS VT_EL_LIN
        "[tankmouse] queued:%u requests:%u responses:%u\n",
        8, 1,
        queued,
        requests,
        responses
    );

#ifdef ENABLE_BLUETOOTH_HID
    if (!bt_passkey_active)
        disp_write(0, 5, linebuf);
#else
    disp_write(0, 5, linebuf);
#endif
#else
    (void)queued;
    (void)requests;
    (void)responses;
#endif
}

void dbgcons_mode(char const *mode)
{
    status_mode = (mode != NULL) ? mode : "?";
    dbgcons_status_line();
}

void dbgcons_amiga_power(bool on)
{
    status_amiga_on = on;
    dbgcons_status_line();
}

void dbgcons_boot_stage(char const *stage)
{
    // bring-up breadcrumb on oled line 3: the last one shown is where boot
    // stopped. the wait lets the frame (~25 ms at 400 kHz) reach the panel
    // before the next step runs, in case that step never returns
    char linebuf[DBGCONS_OLED_COLS + 1] = "";

    snprintf(linebuf, sizeof(linebuf), "%s", stage != NULL ? stage : "?");
    dbgcons_write_padded_line(3, linebuf);
    ahprintf("[boot] %s\n", stage != NULL ? stage : "?");

    // a cycle loop, not sleep_ms(): the wait must not depend on timer alarms
    busy_wait_at_least_cycles(clock_get_hz(clk_sys) / 25);
}

void dbgcons_usb_c(char const *state, uint32_t iox_attempts)
{
    // oled line 1 belongs to the bluetooth status on bluetooth builds, and is
    // borrowed by DEBUG_KEYBOARD's key readout until the next usb-c change
#ifndef ENABLE_BLUETOOTH_HID
    char linebuf[DBGCONS_OLED_COLS + 1] = "";

    snprintf(linebuf, sizeof(linebuf), "usb-c:%-8s try:%lu",
        state != NULL ? state : "?", (unsigned long)iox_attempts);
    dbgcons_write_padded_line(1, linebuf);
#else
    (void)state;
    (void)iox_attempts;
#endif
}

void dbgcons_bt_status(char const *status)
{
#ifdef ENABLE_BLUETOOTH_HID
    dbgcons_write_padded_line(1, status);
#else
    (void)status;
#endif
}

void dbgcons_bt_passkey(char const *message)
{
#ifdef ENABLE_BLUETOOTH_HID
    bt_passkey_active = true;
    dbgcons_write_padded_line(3, message);
#else
    (void)message;
#endif
}

void dbgcons_bt_passkey_clear(void)
{
#ifdef ENABLE_BLUETOOTH_HID
    bt_passkey_active = false;
#ifdef DEBUG_MOUSE
    dbgcons_write_padded_line(3, line3_text()[0] ? line3_text() : "mouse --");
#else
    dbgcons_write_padded_line(3, line3_text());
#endif
#endif
}

void dbgcons_wifi(char const *text)
{
#ifdef ENABLE_BLUETOOTH_HID
    snprintf(wifi_text, sizeof(wifi_text), "%s", text != NULL ? text : "");
    // a passkey prompt keeps the line; clearing it puts this back
    if (!bt_passkey_active)
        dbgcons_write_padded_line(3, line3_text());
#else
    (void)text;
#endif
}

void dbgcons_net(bool active)
{
#ifdef ENABLE_BLUETOOTH_HID
    net_active = active;
    if (!bt_passkey_active)
        dbgcons_write_padded_line(3, line3_text());
#else
    (void)active;
#endif
}

void dbgcons_ota(char const *text)
{
#ifdef ENABLE_BLUETOOTH_HID
    snprintf(ota_text, sizeof(ota_text), "%s", text != NULL ? text : "");
    if (!bt_passkey_active)
        dbgcons_write_padded_line(3, line3_text());
#else
    (void)text;
#endif
}

void dbgcons_display_mode(uint8_t mode)
{
    bool was_off = display_mode == DBGCONS_DISPLAY_OFF;

    display_mode = mode;

    if (mode == DBGCONS_DISPLAY_OFF) {
        disp_blank(true);
        return;
    }

    if (!was_off)
        return;

    // back on: draw what the status screen showed before it went dark
    disp_blank(false);
    dbgcons_print_counters();
    for (uint8_t line = 1; line < 4; line++)
        if (line_cache[line][0])
            disp_write(0, line, line_cache[line]);
}
