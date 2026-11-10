/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * runtime mode: which machine the board serves (see run_mode.h).
 */

#include "config.h"
#include "util/run_mode.h"

#include "input_bridge.h"
#include "platform/amiga/keyboard_recv.h"
#include "platform/amiga/de9.h"
#include "settings.h"
#include "usb_device.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/status_led.h"

_Static_assert((RUN_MODE_AMIGA == (int)SETTINGS_MODE_AMIGA) && (RUN_MODE_C64 == (int)SETTINGS_MODE_C64)
    && (RUN_MODE_USB == (int)SETTINGS_MODE_USB) && (RUN_MODE_COUNT == (int)SETTINGS_MODE_COUNT),
    "run modes and settings modes must be numbered alike");

static run_mode_t current_mode = RUN_MODE_AMIGA;

run_mode_t run_mode_get(void)
{
    return current_mode;
}

char const *run_mode_name(run_mode_t mode)
{
    switch (mode) {
        case RUN_MODE_AMIGA:    return "amiga";
        case RUN_MODE_C64:      return "c64";
        case RUN_MODE_USB:      return "usb";
        default:                return "?";
    }
}

bool run_mode_keyboard_usb(run_mode_t mode)
{
    return mode != RUN_MODE_AMIGA;
}

bool run_mode_mouse_usb(run_mode_t mode)
{
    // c64 mode: a port set to 1351 takes the mouse, else usb-c does
    if (mode == RUN_MODE_C64)
        return !de9_c64_has_1351();
    return mode != RUN_MODE_AMIGA;
}

bool run_mode_pads_usb(run_mode_t mode)
{
    return mode == RUN_MODE_USB;
}

/**
 * keep the mode across power cycles. a save is a flash write, so it only
 * happens when the stored mode actually differs.
 */
static void run_mode_remember(run_mode_t mode)
{
    settings_t settings = *settings_get();

    if (settings.mode == (uint8_t)mode)
        return;

    settings.mode = (uint8_t)mode;
    if (!settings_save(&settings))
        ahprintf("[mode] could not save the mode\n");
}

void run_mode_set(run_mode_t mode)
{
    run_mode_t old = current_mode;

    if ((mode >= RUN_MODE_COUNT) || (mode == old))
        return;

    // let go of everything on whatever stops receiving. the amiga side holds
    // keyboard and mouse state together (input_bridge), so either moving away
    // releases both; the usb-c side likewise
    if ((!run_mode_keyboard_usb(old) && run_mode_keyboard_usb(mode))
        || (!run_mode_mouse_usb(old) && run_mode_mouse_usb(mode)))
        input_bridge_release_all();

    if ((run_mode_keyboard_usb(old) && !run_mode_keyboard_usb(mode))
        || (run_mode_mouse_usb(old) && !run_mode_mouse_usb(mode)))
        usb_device_release_all();

    // the real amiga keyboard keeps its own record of what it holds where
    amiga_keyboard_recv_mode_changed(old, mode);

    current_mode = mode;

    status_led_mode(mode != RUN_MODE_AMIGA);
    dbgcons_mode(run_mode_name(mode));
    ahprintf("[mode] %s\n", run_mode_name(mode));

    // the de9 ports are something else in every mode
    de9_apply();

    run_mode_remember(mode);
}

void run_mode_restore(void)
{
    uint8_t stored = settings_get()->mode;

    run_mode_set((stored < RUN_MODE_COUNT) ? (run_mode_t)stored : RUN_MODE_AMIGA);
}

void run_mode_next(void)
{
    run_mode_set((run_mode_t)((current_mode + 1) % RUN_MODE_COUNT));
}

