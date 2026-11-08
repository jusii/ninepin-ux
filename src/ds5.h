/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * sony dualsense (ps5) pad: input reports over usb (0x01) and bluetooth
 * (0x31), and the output report that sets its player leds and light bar.
 * layouts after the linux hid-playstation driver.
 */

#ifndef _DS5_H
#define _DS5_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pad.h"

#define DS5_VID             0x054c
#define DS5_PID             0x0ce6
#define DS5_EDGE_PID        0x0df2

#define DS5_USB_OUTPUT_LEN  63      // report 0x02, id included
#define DS5_BT_OUTPUT_LEN   78      // report 0x31, id and crc included

typedef struct {
    bool touching;
    uint8_t contact;
    uint16_t x, y;
    int16_t remainder_x, remainder_y;
} ds5_t;

bool ds5_matches(uint16_t vid, uint16_t pid);

/**
 * @brief Decode a usb input report (report id 0x01 first).
 */
bool ds5_decode_usb(ds5_t *pad, uint8_t const *report, size_t length, pad_input_t *input);

/**
 * @brief Decode a bluetooth input report (id first, no 0xa1 header): the full
 *        report 0x31 (crc checked), or the short 0x01 a pad sends until asked
 *        for more.
 */
bool ds5_decode_bt(ds5_t *pad, uint8_t const *report, size_t length, pad_input_t *input);

/**
 * @brief Build the output report showing a player (0, 1; -1 for none).
 *
 * @return the report length (id included), 0 if out is too small
 */
size_t ds5_output_usb(uint8_t *out, size_t size, int8_t player);
size_t ds5_output_bt(uint8_t *out, size_t size, int8_t player, uint8_t sequence);

#endif // _DS5_H
