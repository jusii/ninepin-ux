/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * amiga-on sense.
 *
 * the board watches the amiga's i/o rail through a 100k/100k divider on an adc
 * input: about 2.2-2.4 v with an amiga connected and switched on, 0 v without.
 * the reader averages a burst of samples every AMIGA_SENSE_PERIOD_MS and applies
 * the AMIGA_SENSE_ON_MV / AMIGA_SENSE_OFF_MV hysteresis from config.h, so the
 * state only flips on a real power change, not on noise around one threshold.
 *
 * boards without the sense input report the amiga as permanently on, which is
 * what they have always assumed.
 */

#ifndef _UTIL_AMIGA_SENSE_H
#define _UTIL_AMIGA_SENSE_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

/**
 * @brief Claim the adc and take a first reading, so amiga_sense_is_on() is
 *        meaningful from the start.
 */
void amiga_sense_init(void);

/**
 * @brief Take a new averaged reading if the sample period has elapsed.
 *
 * Main loop only. Costs a few microseconds of adc conversions per period.
 *
 * @return true if amiga_sense_is_on() changed on this call
 */
bool amiga_sense_task(void);

/**
 * @return true whilst the amiga is on (with hysteresis)
 */
bool amiga_sense_is_on(void);

/**
 * @return the last averaged reading at the adc pin, in millivolts
 */
uint16_t amiga_sense_millivolts(void);

#endif // _UTIL_AMIGA_SENSE_H
