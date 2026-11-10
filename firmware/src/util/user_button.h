/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * BOOT button readback.
 *
 * the BOOT button pulls the flash chip select (qspi_ss) low through 1k; the
 * bootrom samples it at reset. at runtime it can be read with the "bootsel
 * trick" (pico-examples picoboard/button): run from ram, float the chip select,
 * wait for it to settle and read it back. constraints that come with it:
 *
 *  - for ~100us the flash is unreachable. interrupts are off on core0 for that
 *    long, which delays the pio-usb sof / transfer interrupt, the usb device
 *    interrupt and the cyw43 background work by the same amount.
 *  - core1 must not execute from flash meanwhile, so core1 is parked with the
 *    sdk's multicore lockout. core1 (the quadrature mouse loop) registers as a
 *    lockout victim; if the lockout cannot be
 *    taken the read is skipped, never forced.
 *  - user_button_task() samples at most once per USER_BUTTON_POLL_MS (50 ms)
 *    and must only ever be called from the core0 main loop - never from an
 *    interrupt, and never whilst something else is programming the flash.
 *    the sdk's flash_safe_execute() (used by btstack's link-key store) takes
 *    the same multicore lockout, so if the two ever collide the second one to
 *    arrive fails its lockout: a skipped button sample, or a flash write that
 *    reports a timeout. neither touches the flash with the other in progress.
 */

#ifndef _UTIL_USER_BUTTON_H
#define _UTIL_USER_BUTTON_H

#include "config.h"

typedef enum
{
    USER_BUTTON_EVENT_NONE = 0,
    USER_BUTTON_EVENT_PRESS,        // released before the long-press time
    USER_BUTTON_EVENT_LONG_PRESS,   // held for USER_BUTTON_LONG_PRESS_MS (fires once, whilst still held)
    USER_BUTTON_EVENT_LONG_RELEASE, // released after the long-press time, before the hold time
    USER_BUTTON_EVENT_HOLD,         // held for USER_BUTTON_HOLD_PRESS_MS (3 s; fires once, whilst still held)
} user_button_event_t;

void user_button_init(void);

/**
 * @brief Sample the button (rate-limited) and report a completed gesture.
 *
 * @return the event, or USER_BUTTON_EVENT_NONE
 */
user_button_event_t user_button_task(void);

#endif // _UTIL_USER_BUTTON_H
