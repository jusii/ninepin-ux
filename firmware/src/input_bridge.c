/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * shared hid input bridging for usb and bluetooth sources.
 */

#include "input_bridge.h"
#include "menu.h"
#include "settings.h"

#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "platform/amiga/keyboard_serial_io.h"
#include "platform/amiga/quad_mouse.h"
#include "util/debug_cons.h"
#include "util/run_mode.h"
#include "util/status_led.h"

#include "usb_device.h"

typedef struct
{
    hid_keyboard_report_t keyboard;
    hid_mouse_report_t mouse;
    uint8_t led_report;
} input_bridge_state_t;

static input_bridge_state_t bridge_state[INPUT_BRIDGE_MAX_SLOTS];

static inline bool _ib_key_pressed(hid_keyboard_report_t const *report, uint8_t keycode)
{
    for (uint8_t pos = 0; pos < 6; pos++)
        if (report->keycode[pos] == keycode)
            return true;

    return false;
}

static void _ib_sync_keyboard_modifiers(hid_keyboard_report_t const *last_report,
    hid_keyboard_report_t const *report)
{
    bool last_ctrl = (last_report->modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) != 0;
    bool new_ctrl = (report->modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) != 0;

    if (new_ctrl && !last_ctrl)
        amiga_hid_modifier(KEYBOARD_MODIFIER_LEFTCTRL, false);
    if (!new_ctrl && last_ctrl)
        amiga_hid_modifier(KEYBOARD_MODIFIER_LEFTCTRL, true);

    for (uint8_t bit = 1; bit < 8; bit++) {
        hid_keyboard_modifier_bm_t mask = (hid_keyboard_modifier_bm_t)(1u << bit);

        if ((report->modifier & mask) && !(last_report->modifier & mask))
            amiga_hid_modifier(mask, false);
        if (!(report->modifier & mask) && (last_report->modifier & mask))
            amiga_hid_modifier(mask, true);
    }
}

static void _ib_sync_keyboard_keys(hid_keyboard_report_t const *last_report,
    hid_keyboard_report_t const *report)
{
    for (uint8_t pos = 0; pos < 6; pos++) {
        if (report->keycode[pos] && !_ib_key_pressed(last_report, report->keycode[pos]))
            amiga_hid_send(report->keycode[pos], false);

        if (last_report->keycode[pos] && !_ib_key_pressed(report, last_report->keycode[pos]))
            amiga_hid_send(last_report->keycode[pos], true);
    }
}

static void _ib_update_keyboard_leds(input_bridge_state_t *state, input_bridge_keyboard_sink_t const *sink)
{
    uint8_t led_report = amiga_caps_lock() ? KEYBOARD_LED_CAPSLOCK : 0;

    if ((sink != NULL) && (sink->set_leds != NULL) && (state->led_report != led_report))
        sink->set_leds(sink->ctx, led_report);

    state->led_report = led_report;
}

static void _ib_release_mouse_buttons(hid_mouse_report_t const *last_report)
{
    if (last_report->buttons & MOUSE_BUTTON_LEFT)
        amiga_quad_mouse_button(AQM_LEFT, false);
    if (last_report->buttons & MOUSE_BUTTON_MIDDLE)
        amiga_quad_mouse_button(AQM_MIDDLE, false);
    if (last_report->buttons & MOUSE_BUTTON_RIGHT)
        amiga_quad_mouse_button(AQM_RIGHT, false);
}

void input_bridge_reset(uint8_t slot)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    memset(&bridge_state[slot], 0, sizeof(bridge_state[slot]));
}

/**
 * release everything a slot holds down on the amiga and forget its state.
 */
static void _ib_release_amiga(uint8_t slot)
{
    static const hid_keyboard_report_t empty_keyboard = { 0, 0, {0} };

    _ib_sync_keyboard_keys(&bridge_state[slot].keyboard, &empty_keyboard);
    _ib_sync_keyboard_modifiers(&bridge_state[slot].keyboard, &empty_keyboard);
    _ib_release_mouse_buttons(&bridge_state[slot].mouse);
    memset(&bridge_state[slot], 0, sizeof(bridge_state[slot]));
}

void input_bridge_disconnect(uint8_t slot)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    // in passthrough the amiga-side state is already empty (run_mode_set()
    // released it), so this is a no-op there; the device side lets go itself
    _ib_release_amiga(slot);
    usb_device_release_slot(slot);
}

void input_bridge_release_all(void)
{
    for (uint8_t slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++)
        _ib_release_amiga(slot);
}

void input_bridge_handle_keyboard(uint8_t slot, hid_keyboard_report_t const *report,
    input_bridge_keyboard_sink_t const *sink)
{
    input_bridge_state_t *state;

    if ((slot >= INPUT_BRIDGE_MAX_SLOTS) || (report == NULL))
        return;

    // the settings menu sees every keyboard first: a held help key opens it,
    // and whilst it is open (or a keyboard still holds keys from it) it keeps them
    if (menu_keyboard_hid(slot, report))
        return;

    // passthrough hands the report on untouched; the amiga-side state stays
    // empty, so switching back starts from nothing held
    if (run_mode_keyboard_usb(run_mode_get())) {
        usb_device_keyboard(slot, report);
        status_led_activity();
        return;
    }

    state = &bridge_state[slot];

    _ib_sync_keyboard_keys(&state->keyboard, report);
    _ib_sync_keyboard_modifiers(&state->keyboard, report);
    _ib_update_keyboard_leds(state, sink);
    state->keyboard = *report;
}

void input_bridge_handle_keyboard_boot(uint8_t slot, uint8_t modifier, uint8_t const keycode[6])
{
    hid_keyboard_report_t report = { 0, 0, {0} };

    report.modifier = modifier;
    memcpy(report.keycode, keycode, sizeof(report.keycode));
    input_bridge_handle_keyboard(slot, &report, NULL);
}

void input_bridge_handle_mouse(uint8_t slot, hid_mouse_report_t const *report)
{
    input_bridge_state_t *state;

    if ((slot >= INPUT_BRIDGE_MAX_SLOTS) || (report == NULL))
        return;

    // the settings menu owns the inputs whilst it is open
    if (menu_is_open())
        return;

    if (run_mode_mouse_usb(run_mode_get())) {
        usb_device_mouse(slot, report);
        status_led_activity();
        return;
    }

    state = &bridge_state[slot];

    if ((report->buttons & MOUSE_BUTTON_LEFT) && !(state->mouse.buttons & MOUSE_BUTTON_LEFT))
        amiga_quad_mouse_button(AQM_LEFT, true);
    if (!(report->buttons & MOUSE_BUTTON_LEFT) && (state->mouse.buttons & MOUSE_BUTTON_LEFT))
        amiga_quad_mouse_button(AQM_LEFT, false);

    if ((report->buttons & MOUSE_BUTTON_MIDDLE) && !(state->mouse.buttons & MOUSE_BUTTON_MIDDLE))
        amiga_quad_mouse_button(AQM_MIDDLE, true);
    if (!(report->buttons & MOUSE_BUTTON_MIDDLE) && (state->mouse.buttons & MOUSE_BUTTON_MIDDLE))
        amiga_quad_mouse_button(AQM_MIDDLE, false);

    if ((report->buttons & MOUSE_BUTTON_RIGHT) && !(state->mouse.buttons & MOUSE_BUTTON_RIGHT))
        amiga_quad_mouse_button(AQM_RIGHT, true);
    if (!(report->buttons & MOUSE_BUTTON_RIGHT) && (state->mouse.buttons & MOUSE_BUTTON_RIGHT))
        amiga_quad_mouse_button(AQM_RIGHT, false);

    dbgcons_mouse_report(report->x, report->y, report->buttons);

    if (report->x || report->y)
        amiga_quad_mouse_set_motion(report->x, report->y);

    // the tankmouse/cocolino wheel, if the settings want it, either way round
    if (report->wheel && settings_get()->wheel_enabled)
        amiga_quad_mouse_wheel(settings_get()->wheel_reverse ? (int8_t)-report->wheel : report->wheel);

    // the keyboard path pulses the activity indicator as it queues; do the same
    // for anything the mouse actually handed to the amiga (an idle report which
    // moved nothing and changed no button is not activity)
    if (report->x || report->y || report->wheel || (report->buttons != state->mouse.buttons))
        status_led_activity();

    state->mouse = *report;
}

void input_bridge_handle_mouse_boot(uint8_t slot, uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
    hid_mouse_report_t report = { 0 };

    report.buttons = buttons;
    report.x = x;
    report.y = y;
    report.wheel = wheel;
    input_bridge_handle_mouse(slot, &report);
}
