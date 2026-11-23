/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * how long a pad's report takes through the board: from the bluetooth or usb
 * host stack handing it over, to the de9 pins written or the usb host having
 * collected it. always on; only reports that change an output are counted.
 */

#ifndef _LAG_H
#define _LAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { LAG_FROM_BT, LAG_FROM_USB, LAG_FROMS };
enum { LAG_TO_DE9, LAG_TO_USB, LAG_TOS };

#define LAG_REPORT_MAX  1800

/**
 * @return the clock the stamps below are on, microseconds, wrapping
 */
uint32_t lag_now(void);

/**
 * @brief A pad's report, handed over at that time, is being worked on until
 *        lag_origin_end(): outputs it changes are timed from then.
 */
void lag_origin(uint8_t from, uint32_t at_us);
void lag_origin_end(void);

/**
 * @brief The de9 pins have just been written for a pad.
 */
void lag_out_de9(void);

/**
 * @brief A player's usb report has changed and waits for the host.
 */
void lag_out_usb(uint8_t player);

/**
 * @brief The report is about to be handed to the usb stack; or was refused.
 */
void lag_usb_sending(uint8_t player);
void lag_usb_unsent(uint8_t player);

/**
 * @brief The host has collected a player's report. From the usb interrupt.
 */
void lag_usb_collected(uint8_t player);

/**
 * @brief No host is collecting: forget what waits.
 */
void lag_usb_idle(void);

/**
 * @brief The main loop has reached a stage (reset_stage_t); and its end.
 */
void lag_stage(uint8_t stage);
void lag_pass_end(void);

void lag_reset(void);

/**
 * @brief The figures as text, for the console and the page.
 *
 * @return its length
 */
size_t lag_report(char *out, size_t size);

#endif // _LAG_H
