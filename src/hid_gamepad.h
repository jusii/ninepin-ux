/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * bounded hid gamepad decoding: finds the controls of a game pad / joystick
 * application collection in a report descriptor and decodes reports into a
 * pad_input_t. ported from stefan reinauer's hid_gamepad.c
 * (reinauer/amigahid-pico, features branch, EPL-2.0); the decode side now also
 * keeps the analog axes, which a usb host gets passed through.
 */

#ifndef _HID_GAMEPAD_H
#define _HID_GAMEPAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pad.h"

#define GAMEPAD_MAX_REPORTS         4
#define GAMEPAD_MAX_FIELDS          32
#define GAMEPAD_MAX_REPORT_BYTES    64
#define GAMEPAD_AXES                6   // x, y, z, rz, rx, ry

typedef struct {
    uint16_t offset, kind;
    uint8_t size, report;
    int32_t minimum, maximum;
} gamepad_field_t;

typedef struct {
    uint16_t bits;
    uint8_t id;
    uint8_t dpad;                   // directions last seen in this report
    uint8_t hat;
    uint16_t buttons;
} gamepad_report_t;

typedef struct {
    gamepad_report_t reports[GAMEPAD_MAX_REPORTS];
    gamepad_field_t fields[GAMEPAD_MAX_FIELDS];
    uint8_t report_count, field_count;
    bool report_ids;
    bool has_axis[GAMEPAD_AXES];
    int8_t axes[GAMEPAD_AXES];      // last value of each axis, -127..127
    uint8_t defaults[PAD_LOGICAL_BUTTONS];
} hid_gamepad_t;

/**
 * @return true if the descriptor has a game pad or joystick with something
 *         usable in it
 */
bool hid_gamepad_parse(hid_gamepad_t *pad, uint8_t const *descriptor, size_t length);

/**
 * @brief Decode one input report (report id first, if the pad uses them).
 *
 * @return true if the report carried pad controls (input filled in)
 */
bool hid_gamepad_decode(hid_gamepad_t *pad, uint8_t const *report, size_t length, pad_input_t *input);

#endif // _HID_GAMEPAD_H
