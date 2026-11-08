/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * runtime output mode: where keyboard and mouse input goes.
 *
 *   RUN_MODE_AMIGA_OUTPUT            - the amiga keyboard and mouse port (default,
 *                                      and the only mode on boards without
 *                                      HAS_USB_DEVICE)
 *   RUN_MODE_USB_DEVICE_PASSTHROUGH  - the usb-c device port, as a usb keyboard
 *                                      and mouse for another host (e.g. a
 *                                      commodore 64 ultimate)
 *
 * switching releases everything held on the side being left, so no key or
 * button stays down on a machine that stops receiving reports. the mode is not
 * persisted; every boot starts in RUN_MODE_AMIGA_OUTPUT.
 */

#ifndef _UTIL_RUN_MODE_H
#define _UTIL_RUN_MODE_H

#include "config.h"

typedef enum
{
    RUN_MODE_AMIGA_OUTPUT = 0,
    RUN_MODE_USB_DEVICE_PASSTHROUGH,
    RUN_MODE_COUNT
} run_mode_t;

#ifdef HAS_USB_DEVICE

/**
 * @return the current mode
 */
run_mode_t run_mode_get(void);

/**
 * @brief Switch mode, releasing whatever the old destination still holds, and
 *        update the status led and the display.
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
 * @brief Store the mode in the settings if it differs from the stored one
 *        (run_mode_set() does this itself).
 */
void run_mode_remember(run_mode_t mode);

/**
 * @brief Switch to the mode the settings remember. Call once at boot, after
 *        settings_init() and once everything a mode switch touches is up.
 */
void run_mode_restore(void);

#else

static inline run_mode_t run_mode_get(void) { return RUN_MODE_AMIGA_OUTPUT; }
static inline void run_mode_set(run_mode_t mode) { (void)mode; }
static inline void run_mode_next(void) {}
static inline char const *run_mode_name(run_mode_t mode) { (void)mode; return "amiga"; }
static inline void run_mode_remember(run_mode_t mode) { (void)mode; }
static inline void run_mode_restore(void) {}

#endif // HAS_USB_DEVICE

#endif // _UTIL_RUN_MODE_H
