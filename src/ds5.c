/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * sony dualsense (ps5) pad. report layouts after the linux hid-playstation
 * driver (struct dualsense_input_report, dualsense_output_report_common); the
 * touchpad-as-mouse scheme follows stefan reinauer's bt_ds4.c.
 *
 * input, from the byte after the report id (usb 0x01) or after id + one header
 * byte (bluetooth 0x31):
 *   0 lx, 1 ly, 2 rx, 3 ry, 4 l2, 5 r2, 6 sequence,
 *   7 hat (low nibble) + square 0x10, cross 0x20, circle 0x40, triangle 0x80,
 *   8 l1 0x01, r1 0x02, l2 0x04, r2 0x08, create 0x10, options 0x20, l3 0x40, r3 0x80,
 *   9 ps 0x01, touchpad click 0x02, mute 0x04,
 *   32.. two touch points of 4 bytes: contact (bit 7 set: not touching),
 *        x 12 bits, y 12 bits
 */

#include "ds5.h"

#include <string.h>

// the pad's own button numbers, as its usb hid descriptor numbers them
enum {
    DS5_SQUARE = 1, DS5_CROSS, DS5_CIRCLE, DS5_TRIANGLE, DS5_L1, DS5_R1, DS5_L2, DS5_R2,
    DS5_CREATE, DS5_OPTIONS, DS5_L3, DS5_R3, DS5_PS, DS5_TOUCH, DS5_MUTE,
};

#define DS5_INPUT_BYTES         41      // up to the second touch point
#define DS5_BT_FULL_LEN         78
#define DS5_TOUCH_WIDTH         1920
#define DS5_TOUCH_HEIGHT        1080

// output flags
#define DS5_FLAG1_LIGHTBAR      (1u << 2)
#define DS5_FLAG1_PLAYER_LEDS   (1u << 4)
#define DS5_FLAG2_LIGHTBAR_SETUP (1u << 1)
#define DS5_LIGHTBAR_LIGHT_OUT  (1u << 1)

bool ds5_matches(uint16_t vid, uint16_t pid)
{
    return (vid == DS5_VID) && ((pid == DS5_PID) || (pid == DS5_EDGE_PID));
}

static uint32_t crc_byte(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; bit++)
        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    return crc;
}

static uint32_t crc_report(uint8_t seed, uint8_t const *data, size_t length)
{
    uint32_t crc = crc_byte(UINT32_MAX, seed);

    for (size_t i = 0; i < length; i++)
        crc = crc_byte(crc, data[i]);
    return ~crc;
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

static void decode_touch(ds5_t *pad, uint8_t const *point, pad_input_t *input)
{
    uint16_t x = (uint16_t)(point[1] | ((point[2] & 0x0f) << 8));
    uint16_t y = (uint16_t)((point[2] >> 4) | (point[3] << 4));
    uint8_t contact = point[0] & 0x7f;

    if ((point[0] & 0x80) || (x >= DS5_TOUCH_WIDTH) || (y >= DS5_TOUCH_HEIGHT)) {
        memset(pad, 0, sizeof(*pad));
        return;
    }

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

/**
 * the common part: data starts at lx.
 */
static void decode(ds5_t *pad, uint8_t const *d, bool with_touch, pad_input_t *input)
{
    static uint8_t const hat_directions[8] = { PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_RIGHT | PAD_DOWN,
        PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_LEFT | PAD_UP };
    static uint8_t const defaults[PAD_LOGICAL_BUTTONS] = {
        DS5_CROSS, DS5_CIRCLE, DS5_SQUARE, DS5_TRIANGLE, DS5_L1, DS5_R1, DS5_OPTIONS };
    uint16_t buttons = 0;
    uint8_t hat = d[7] & 0x0f;

    memset(input, 0, sizeof(*input));

    input->lx = centre(d[0]);
    input->ly = centre(d[1]);
    input->rx = centre(d[2]);
    input->ry = centre(d[3]);
    input->lt = d[4];
    input->rt = d[5];

    input->hat = (hat < 8) ? hat : PAD_HAT_CENTRED;
    input->dpad = (hat < 8) ? hat_directions[hat] : 0;

    if (d[7] & 0x10) buttons |= 1u << (DS5_SQUARE - 1);
    if (d[7] & 0x20) buttons |= 1u << (DS5_CROSS - 1);
    if (d[7] & 0x40) buttons |= 1u << (DS5_CIRCLE - 1);
    if (d[7] & 0x80) buttons |= 1u << (DS5_TRIANGLE - 1);
    for (unsigned bit = 0; bit < 8; bit++)
        if (d[8] & (1u << bit))
            buttons |= 1u << (DS5_L1 - 1 + bit);
    if (d[9] & 0x01) buttons |= 1u << (DS5_PS - 1);
    if (d[9] & 0x02) buttons |= 1u << (DS5_TOUCH - 1);
    if (d[9] & 0x04) buttons |= 1u << (DS5_MUTE - 1);

    input->buttons = buttons;
    input->home = (d[9] & 0x01) != 0;
    memcpy(input->defaults, defaults, sizeof(defaults));

    if (with_touch) {
        input->has_touchpad = true;
        decode_touch(pad, d + 32, input);

        // a touchpad click is the left or right mouse button by its half
        if (d[9] & 0x02)
            input->touch_buttons = (pad->touching && (pad->x >= DS5_TOUCH_WIDTH / 2)) ? 0x02 : 0x01;
    }
}

bool ds5_decode_usb(ds5_t *pad, uint8_t const *report, size_t length, pad_input_t *input)
{
    if (!pad || !report || !input || (length < 1 + DS5_INPUT_BYTES) || (report[0] != 0x01))
        return false;

    decode(pad, report + 1, true, input);
    return true;
}

bool ds5_decode_bt(ds5_t *pad, uint8_t const *report, size_t length, pad_input_t *input)
{
    if (!pad || !report || !input || !length)
        return false;

    if ((report[0] == 0x31) && (length >= DS5_BT_FULL_LEN)) {
        uint32_t received = (uint32_t)report[74] | ((uint32_t)report[75] << 8)
            | ((uint32_t)report[76] << 16) | ((uint32_t)report[77] << 24);

        if (crc_report(0xa1, report, 74) != received)
            return false;
        decode(pad, report + 2, true, input);
        return true;
    }

    // the short report a pad sends before it has been asked for the full one:
    // sticks, then hat + face buttons, shoulders, ps; no touchpad
    if ((report[0] == 0x01) && (length >= 10)) {
        uint8_t d[DS5_INPUT_BYTES];

        memset(d, 0, sizeof(d));
        d[0] = report[1];
        d[1] = report[2];
        d[2] = report[3];
        d[3] = report[4];
        d[7] = report[5];
        d[8] = report[6];
        d[9] = report[7] & 0x03;
        d[4] = report[8];
        d[5] = report[9];
        decode(pad, d, false, input);
        return true;
    }

    return false;
}

static void output_common(uint8_t *common, int8_t player)
{
    // centre led for player 1, the two either side of it for player 2
    static uint8_t const leds[2] = { 0x04, 0x0a };
    static uint8_t const colours[2][3] = { { 0, 0, 64 }, { 64, 0, 0 } };

    common[1] = DS5_FLAG1_LIGHTBAR | DS5_FLAG1_PLAYER_LEDS;
    common[38] = DS5_FLAG2_LIGHTBAR_SETUP;
    common[41] = DS5_LIGHTBAR_LIGHT_OUT;
    common[42] = 0;                         // led brightness: full

    if ((player == 0) || (player == 1)) {
        common[43] = leds[player];
        common[44] = colours[player][0];
        common[45] = colours[player][1];
        common[46] = colours[player][2];
    } else {
        common[43] = 0;
        common[44] = common[45] = common[46] = 8;   // no player: dim white
    }
}

size_t ds5_output_usb(uint8_t *out, size_t size, int8_t player)
{
    if (!out || (size < DS5_USB_OUTPUT_LEN))
        return 0;

    memset(out, 0, DS5_USB_OUTPUT_LEN);
    out[0] = 0x02;
    output_common(out + 1, player);
    return DS5_USB_OUTPUT_LEN;
}

size_t ds5_output_bt(uint8_t *out, size_t size, int8_t player, uint8_t sequence)
{
    uint32_t crc;

    if (!out || (size < DS5_BT_OUTPUT_LEN))
        return 0;

    memset(out, 0, DS5_BT_OUTPUT_LEN);
    out[0] = 0x31;
    out[1] = (uint8_t)((sequence & 0x0f) << 4);
    out[2] = 0x10;                          // output tag
    output_common(out + 3, player);

    crc = crc_report(0xa2, out, 74);
    out[74] = (uint8_t)crc;
    out[75] = (uint8_t)(crc >> 8);
    out[76] = (uint8_t)(crc >> 16);
    out[77] = (uint8_t)(crc >> 24);
    return DS5_BT_OUTPUT_LEN;
}
