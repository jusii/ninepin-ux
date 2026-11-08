/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the cyw43 radio (pico w, rm2): bluetooth and wi-fi share one chip and one
 * cyw43_arch_init(), whichever of them starts first.
 */

#ifndef _RADIO_H
#define _RADIO_H

#include <stdbool.h>

/**
 * @brief Bring the cyw43 driver up, once. Main loop. A failure (no radio
 *        fitted) is remembered: later calls do not try again.
 *
 * @return true if the driver is up
 */
bool radio_init(void);

#endif // _RADIO_H
