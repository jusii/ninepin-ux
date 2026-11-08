/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * sony dualshock 4 over bluetooth: recognising it by its report descriptor,
 * and decoding its short (0x01) and full (0x11) input reports into a
 * pad_input_t. ported from stefan reinauer's bt_ds4.c (reinauer/amigahid-pico,
 * features branch, EPL-2.0).
 */

#ifndef _BT_DS4_H
#define _BT_DS4_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pad.h"

typedef struct {
    bool touching;
    uint8_t contact;
    uint16_t x, y;
    int16_t remainder_x, remainder_y;
} bt_ds4_t;

/**
 * @return true if the descriptor is the ds4's own bluetooth one (a dualsense
 *         also calls itself "Wireless Controller", so the name cannot tell)
 */
bool bt_ds4_matches_descriptor(uint8_t const *descriptor, size_t length);

/**
 * @brief Decode an input report (report id first, no 0xa1 header): the short
 *        0x01 a pad sends until asked for more, or the full 0x11 (crc checked)
 *        with the touchpad.
 */
bool bt_ds4_decode(bt_ds4_t *pad, uint8_t const *report, size_t length, pad_input_t *input);

#endif // _BT_DS4_H
