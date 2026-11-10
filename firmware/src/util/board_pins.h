/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * board-level pad setup which belongs to no single driver.
 *
 * driven from the pin map in config.h:
 *   BOARD_PULLUP_PINS - internal pull-ups on lines with no external pull-up
 *   AMIGA_SENSE_PIN   - the adc input watching the amiga's 5v
 */

#ifndef _UTIL_BOARD_PINS_H
#define _UTIL_BOARD_PINS_H

#include "config.h"

/**
 * @brief Pull up the floating lines and park the sense pin as an analogue input.
 *
 * Call once, early in main() and before any driver claims a pin.
 * Drivers which later gpio_init() / pio_gpio_init() these pins only change the
 * pin function and direction, never the pad pulls, so the pull-ups survive.
 */
void board_pins_init(void);

#endif // _UTIL_BOARD_PINS_H
