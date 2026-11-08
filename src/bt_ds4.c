/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * sony dualshock 4 over bluetooth. ported from stefan reinauer's bt_ds4.c
 * (reinauer/amigahid-pico, features branch, EPL-2.0): the descriptor match,
 * the crc check and the touchpad tracking are his; the decode now fills a
 * pad_input_t (sticks, triggers, hat, the pad's own buttons) and leaves the
 * button mapping and the touchpad mouse to the player manager (pads.c).
 * wire format references: hid-tools hidtools/device/sony_gamepad.py and
 * bluepad32 uni_hid_parser_ds4.c.
 *
 * input, from the byte after the report id (short 0x01) or after id + two
 * header bytes (full 0x11):
 *   0 lx, 1 ly, 2 rx, 3 ry,
 *   4 hat (low nibble) + square 0x10, cross 0x20, circle 0x40, triangle 0x80,
 *   5 l1 0x01, r1 0x02, l2 0x04, r2 0x08, share 0x10, options 0x20, l3 0x40, r3 0x80,
 *   6 ps 0x01, touchpad click 0x02,
 *   7 l2, 8 r2 (analog)
 * full report only: byte 35 of the report is the touch sample count, 37.. two
 * touch points of 4 bytes: contact (bit 7 set: not touching), x 12 bits,
 * y 12 bits.
 */

#include "bt_ds4.h"

#include <string.h>

// the pad's own button numbers, in the ds4's usb hid descriptor order
enum {
    DS4_SQUARE = 1, DS4_CROSS, DS4_CIRCLE, DS4_TRIANGLE, DS4_L1, DS4_R1, DS4_L2, DS4_R2,
    DS4_SHARE, DS4_OPTIONS, DS4_L3, DS4_R3, DS4_PS, DS4_TOUCH,
};

#define DS4_SHORT_LEN       10
#define DS4_FULL_LEN        78
#define DS4_TOUCH_WIDTH     1920
#define DS4_TOUCH_HEIGHT    943

bool bt_ds4_matches_descriptor(uint8_t const *descriptor, size_t length)
{
    // the ds4's complete simple input layout and calibration feature, plus its
    // vendor-defined 78-byte bluetooth report
    static const uint8_t simple[] = {
        0x05,0x01,0x09,0x05,0xa1,0x01,0x85,0x01,0x09,0x30,0x09,0x31,
        0x09,0x32,0x09,0x35,0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,
        0x04,0x81,0x02,0x09,0x39,0x15,0x00,0x25,0x07,0x75,0x04,0x95,
        0x01,0x81,0x42,0x05,0x09,0x19,0x01,0x29,0x0e,0x15,0x00,0x25,
        0x01,0x75,0x01,0x95,0x0e,0x81,0x02,0x75,0x06,0x95,0x01,0x81,
        0x01,0x05,0x01,0x09,0x33,0x09,0x34,0x15,0x00,0x26,0xff,0x00,
        0x75,0x08,0x95,0x02,0x81,0x02,0x06,0x04,0xff,0x85,0x02,0x09,
        0x24,0x95,0x24,0xb1,0x02,
    };
    static const uint8_t extended[] = {
        0x06,0x00,0xff,0x85,0x11,0x09,0x20,0x15,0x00,0x26,0xff,0x00,
        0x75,0x08,0x95,0x4d,0x81,0x02,
    };

    return descriptor && (length > 180 + sizeof(extended))
        && (descriptor[length - 1] == 0xc0)
        && !memcmp(descriptor, simple, sizeof(simple))
        && !memcmp(descriptor + 180, extended, sizeof(extended));
}

static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; bit++)
        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    return crc;
}

static bool valid_full_report(uint8_t const *report)
{
    uint32_t crc = crc_byte(UINT32_MAX, 0xa1);
    uint32_t received = (uint32_t)report[74] | ((uint32_t)report[75] << 8)
        | ((uint32_t)report[76] << 16) | ((uint32_t)report[77] << 24);

    for (unsigned i = 0; i < 74; i++)
        crc = crc_byte(crc, report[i]);
    return (~crc == received) && (report[35] <= 4);
}

static int8_t centre(uint8_t value)
{
    int v = (int)value - 128;
    return (int8_t)((v < -127) ? -127 : v);
}

static int8_t touch_delta(int value, int16_t *remainder)
{
    int delta;

    // four touchpad units per mouse count; slow movement is kept, not lost
    value += *remainder;
    delta = value / 4;
    *remainder = (int16_t)(value - delta * 4);
    if (delta < -127) delta = -127;
    if (delta > 127) delta = 127;
    return (int8_t)delta;
}

static void decode_touch(bt_ds4_t *pad, uint8_t const *report, pad_input_t *input)
{
    uint8_t const *point = report + 37;     // first (newest) touch sample
    uint8_t const *second = point + 4;
    uint16_t x, y;
    uint8_t contact;

    // a count of zero: no new touch sample; the buttons are still current
    if (!report[35])
        return;

    // follow the same contact if the pad swaps the two point slots
    if (!(second[0] & 0x80) && ((point[0] & 0x80)
            || (pad->touching && ((second[0] & 0x7f) == pad->contact))))
        point = second;

    x = (uint16_t)(point[1] | ((point[2] & 0x0f) << 8));
    y = (uint16_t)((point[2] >> 4) | (point[3] << 4));
    if ((point[0] & 0x80) || (x >= DS4_TOUCH_WIDTH) || (y >= DS4_TOUCH_HEIGHT)) {
        memset(pad, 0, sizeof(*pad));
        return;
    }

    contact = point[0] & 0x7f;
    if (pad->touching && (pad->contact == contact)) {
        input->touch_dx = touch_delta((int)x - pad->x, &pad->remainder_x);
        input->touch_dy = touch_delta((int)y - pad->y, &pad->remainder_y);
    } else {
        pad->remainder_x = pad->remainder_y = 0;
    }

    pad->touching = true;
    pad->contact = contact;
    pad->x = x;
    pad->y = y;
}

bool bt_ds4_decode(bt_ds4_t *pad, uint8_t const *report, size_t length, pad_input_t *input)
{
    static uint8_t const hat_directions[8] = { PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_RIGHT | PAD_DOWN,
        PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_LEFT | PAD_UP };
    static uint8_t const defaults[PAD_LOGICAL_BUTTONS] = {
        DS4_CROSS, DS4_CIRCLE, DS4_SQUARE, DS4_TRIANGLE, DS4_L1, DS4_R1, DS4_OPTIONS };
    uint8_t const *d;
    uint16_t buttons;
    uint8_t hat;
    bool full;

    if (!pad || !report || !input)
        return false;

    if ((length == DS4_SHORT_LEN) && (report[0] == 0x01))
        full = false;
    else if ((length == DS4_FULL_LEN) && (report[0] == 0x11) && valid_full_report(report))
        full = true;
    else
        return false;

    d = report + (full ? 3 : 1);
    memset(input, 0, sizeof(*input));

    input->lx = centre(d[0]);
    input->ly = centre(d[1]);
    input->rx = centre(d[2]);
    input->ry = centre(d[3]);
    input->lt = d[7];
    input->rt = d[8];

    hat = d[4] & 0x0f;
    input->hat = (hat < 8) ? hat : PAD_HAT_CENTRED;
    input->dpad = (hat < 8) ? hat_directions[hat] : 0;

    // square, cross, circle, triangle, then l1 .. r3: buttons 1 .. 12
    buttons = (uint16_t)((d[4] >> 4) | (d[5] << 4));
    if (d[6] & 0x01) buttons |= 1u << (DS4_PS - 1);
    if (d[6] & 0x02) buttons |= 1u << (DS4_TOUCH - 1);
    input->buttons = buttons;
    input->home = (d[6] & 0x01) != 0;
    memcpy(input->defaults, defaults, sizeof(defaults));

    if (!full) {
        // no touchpad in the short report
        memset(pad, 0, sizeof(*pad));
        return true;
    }

    input->has_touchpad = true;
    decode_touch(pad, report, input);

    // a touchpad click is the left or right mouse button by its half
    if (d[6] & 0x02)
        input->touch_buttons = (pad->touching && (pad->x >= DS4_TOUCH_WIDTH / 2)) ? 0x02 : 0x01;
    return true;
}
