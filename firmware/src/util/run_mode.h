/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * runtime mode: which machine the board serves, and so where each kind of
 * input goes.
 *
 *   RUN_MODE_AMIGA  keyboards to the amiga keyboard port, the mouse and pads to
 *                   the de9 ports (the default)
 *   RUN_MODE_C64    keyboards and the mouse to the usb-c device port (a
 *                   commodore 64 ultimate takes those over usb), pads to the
 *                   de9 ports as c64 joysticks
 *   RUN_MODE_USB    everything to the usb-c device port, for a pc or any other
 *                   usb host
 *
 * code asks the routing questions below rather than comparing modes. a switch
 * releases everything held on whatever stops receiving, so no key, button or
 * direction stays down on a machine that no longer gets reports. the mode is
 * kept in the settings and restored at boot.
 */

#ifndef _UTIL_RUN_MODE_H
#define _UTIL_RUN_MODE_H

#include <stdbool.h>

#include "config.h"

// numbered like enum settings_mode (run_mode.c asserts it)
typedef enum
{
    RUN_MODE_AMIGA = 0,
    RUN_MODE_C64,
    RUN_MODE_USB,
    RUN_MODE_COUNT
} run_mode_t;

/**
 * @return the current mode
 */
run_mode_t run_mode_get(void);

/**
 * @brief Switch mode, releasing whatever the old destinations still hold,
 *        update the status led and the display, and remember the mode.
 */
void run_mode_set(run_mode_t mode);

/**
 * @brief Step to the next mode, wrapping (the user button long-press).
 */
void run_mode_next(void);

/**
 * @return a short lowercase name for the mode, for logs and the display
 */
char const *run_mode_name(run_mode_t mode);

/**
 * @brief Switch to the mode the settings remember. Call once at boot, after
 *        settings_init() and once everything a mode switch touches is up.
 */
void run_mode_restore(void);

/**
 * @return true if keyboards go to the usb-c port in this mode (else the amiga)
 */
bool run_mode_keyboard_usb(run_mode_t mode);

/**
 * @return true if the mouse goes to the usb-c port in this mode (else a de9
 *         port)
 */
bool run_mode_mouse_usb(run_mode_t mode);

/**
 * @return true if pads go to the usb-c port in this mode (else the de9 ports)
 */
bool run_mode_pads_usb(run_mode_t mode);

#endif // _UTIL_RUN_MODE_H
