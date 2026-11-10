/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * board-level pad setup which belongs to no single driver.
 *
 * everything here is driven from the pin map in config.h and compiles to
 * nothing on a board which does not ask for it:
 *   BOARD_PULLUP_PINS - internal pull-ups on lines with no external pull-up
 *   HAS_USB_MUX       - a usb mux in front of the rp2xxx usb phy
 *   HAS_AMIGA_SENSE   - an adc input watching the amiga's 5v
 */

#ifndef _UTIL_BOARD_PINS_H
#define _UTIL_BOARD_PINS_H

#include "config.h"

#if defined(BOARD_PULLUP_PINS) || defined(HAS_USB_MUX) || defined(HAS_AMIGA_SENSE)

/**
 * @brief Pull up the floating lines, park the usb mux on its device port and
 *        the sense pin as an analogue input.
 *
 * Call once, straight after board_init() and before any driver claims a pin.
 * Drivers which later gpio_init() / pio_gpio_init() these pins only change the
 * pin function and direction, never the pad pulls, so the pull-ups survive.
 */
void board_pins_init(void);

/**
 * @brief Switch the usb mux over to the host (usb-a) port.
 *
 * Call immediately before bringing tinyusb up in host mode. No-op on boards
 * without HAS_USB_MUX.
 */
void board_pins_usb_host(void);

#else

static inline void board_pins_init(void) {}
static inline void board_pins_usb_host(void) {}

#endif

#endif // _UTIL_BOARD_PINS_H
