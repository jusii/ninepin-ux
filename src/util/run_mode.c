/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * runtime output mode.
 */

#include "config.h"
#include "util/run_mode.h"

#ifdef HAS_USB_DEVICE

#include "input_bridge.h"
#include "usb_device.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/status_led.h"

static run_mode_t current_mode = RUN_MODE_AMIGA_OUTPUT;

run_mode_t run_mode_get(void)
{
    return current_mode;
}

char const *run_mode_name(run_mode_t mode)
{
    switch (mode) {
        case RUN_MODE_AMIGA_OUTPUT:             return "amiga";
        case RUN_MODE_USB_DEVICE_PASSTHROUGH:   return "usb-dev";
        default:                                return "?";
    }
}

void run_mode_set(run_mode_t mode)
{
    if ((mode >= RUN_MODE_COUNT) || (mode == current_mode))
        return;

    // let go of everything on the side we are leaving
    switch (current_mode) {
        case RUN_MODE_AMIGA_OUTPUT:
            input_bridge_release_all();
            break;
        case RUN_MODE_USB_DEVICE_PASSTHROUGH:
            usb_device_release_all();
            break;
        default:
            break;
    }

    current_mode = mode;

    status_led_mode(mode != RUN_MODE_AMIGA_OUTPUT);
    dbgcons_mode(run_mode_name(mode));
    ahprintf("[mode] output to %s\n", run_mode_name(mode));
}

void run_mode_next(void)
{
    run_mode_set((run_mode_t)((current_mode + 1) % RUN_MODE_COUNT));
}

#endif // HAS_USB_DEVICE
