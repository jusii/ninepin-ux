/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * tca9554 i2c i/o expander (boards with HAS_IO_EXPANDER).
 *
 * on rev7 the expander carries an optional ec11 rotary encoder with push switch
 * (P0 a, P1 b, P2 switch, P3 spare; all pulled up by the tca9554, the encoder
 * pulls them to ground) and the usb-c vbus sense (P4, high with a host or
 * charger on the usb-c port). its open-drain /INT goes low on any input change
 * and stays low until the input register is read.
 *
 * the /INT edge only sets a flag; every i2c transfer happens from
 * io_expander_task() in the main loop, borrowing the display's controller (see
 * disp_i2c_bus_acquire()). results come out as events the main loop polls, plus
 * the debounced vbus level.
 */

#ifndef _UTIL_IO_EXPANDER_H
#define _UTIL_IO_EXPANDER_H

#include <stdbool.h>

#include "config.h"

typedef enum
{
    IO_EXPANDER_EVENT_NONE = 0,
    IO_EXPANDER_EVENT_ENC_CW,       // one detent clockwise
    IO_EXPANDER_EVENT_ENC_CCW,      // one detent anticlockwise
    IO_EXPANDER_EVENT_ENC_PRESS,    // encoder switch pressed (debounced)
    IO_EXPANDER_EVENT_ENC_RELEASE,  // encoder switch released (debounced)
    IO_EXPANDER_EVENT_VBUS_ON,      // usb-c vbus appeared (debounced)
    IO_EXPANDER_EVENT_VBUS_OFF,     // usb-c vbus went away (debounced)
} io_expander_event_t;

#ifdef HAS_IO_EXPANDER

/**
 * @brief Configure the expander and arm the /INT interrupt.
 *
 * Call after disp_ssd_init(), which owns and initialises the shared i2c
 * controller. Leaves P0-P4 as inputs and P5-P7 as outputs driven low. If the
 * expander does not answer, the module stays inert: no events, and
 * io_expander_vbus_present() reports false so nothing is ever presented to the
 * usb-c port on a guess.
 *
 * @return true if the expander answered
 */
bool io_expander_init(void);

/**
 * @brief Service the expander: read it when /INT asked for it (or a debounce
 *        or the slow safety poll is due) and turn the changes into events.
 *
 * Main loop only; non-blocking apart from a single short i2c transfer.
 */
void io_expander_task(void);

/**
 * @brief Take the oldest pending event.
 *
 * @return the event, or IO_EXPANDER_EVENT_NONE when there is none
 */
io_expander_event_t io_expander_poll_event(void);

/**
 * @return true if the expander answered at init
 */
bool io_expander_present(void);

/**
 * @return true whilst usb-c vbus is present (debounced); false if unknown
 */
bool io_expander_vbus_present(void);

#else

static inline bool io_expander_init(void) { return false; }
static inline void io_expander_task(void) {}
static inline io_expander_event_t io_expander_poll_event(void) { return IO_EXPANDER_EVENT_NONE; }
static inline bool io_expander_present(void) { return false; }
static inline bool io_expander_vbus_present(void) { return false; }

#endif // HAS_IO_EXPANDER

#endif // _UTIL_IO_EXPANDER_H
