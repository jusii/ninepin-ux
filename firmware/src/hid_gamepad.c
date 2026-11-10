/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb hid 1.11 short-item parser for the controls in game pad / joystick
 * application collections. the parser is stefan reinauer's hid_gamepad.c
 * (reinauer/amigahid-pico, features branch, EPL-2.0), unchanged but for the
 * extra analog axes (z, rz, rx, ry); hid_gamepad_decode() is rewritten to fill
 * a pad_input_t with the analog values as well as the digital ones.
 */
#include "hid_gamepad.h"

#include <limits.h>
#include <string.h>

#define DESKTOP_PAGE 1u
#define BUTTON_PAGE 9u
#define USAGE(page, id) (((page) << 16) | (id))
#define FIELD_HAT 0x80u
#define FIELD_X 0x81u
#define FIELD_Y 0x82u
#define FIELD_Z 0x83u
#define FIELD_RZ 0x84u
#define FIELD_RX 0x85u
#define FIELD_RY 0x86u
#define FIELD_BUTTON 0x100u
#define MAX_USAGES 32u
#define MAX_DEPTH 8

typedef struct {
    uint32_t page, size, count;
    int32_t minimum, maximum;
    uint8_t id;
} globals_t;

typedef struct {
    uint32_t usages[MAX_USAGES], minimum;
    uint8_t count;
    bool range;
} locals_t;

static int32_t signed_value(uint32_t value, unsigned bits)
{
    if (bits == 0)
        return 0;
    if (bits < 32 && (value & (1u << (bits - 1))))
        value |= UINT32_MAX << bits;
    return (int32_t)value;
}

static int report_index(hid_gamepad_t *pad, uint8_t id)
{
    for (unsigned i = 0; i < pad->report_count; i++)
        if (pad->reports[i].id == id)
            return (int)i;
    if (pad->report_count == GAMEPAD_MAX_REPORTS)
        return -1;
    unsigned i = pad->report_count++;
    pad->reports[i].id = id;
    return (int)i;
}

static uint16_t field_kind(uint32_t usage)
{
    if ((usage >> 16) == BUTTON_PAGE && (usage & 0xffff) >= 1 && (usage & 0xffff) <= 16)
        return FIELD_BUTTON + (usage & 0xffff) - 1;
    switch (usage) {
        case USAGE(DESKTOP_PAGE, 0x30): return FIELD_X;
        case USAGE(DESKTOP_PAGE, 0x31): return FIELD_Y;
        case USAGE(DESKTOP_PAGE, 0x32): return FIELD_Z;
        case USAGE(DESKTOP_PAGE, 0x35): return FIELD_RZ;
        case USAGE(DESKTOP_PAGE, 0x33): return FIELD_RX;
        case USAGE(DESKTOP_PAGE, 0x34): return FIELD_RY;
        case USAGE(DESKTOP_PAGE, 0x39): return FIELD_HAT;
        case USAGE(DESKTOP_PAGE, 0x90): return PAD_UP;
        case USAGE(DESKTOP_PAGE, 0x91): return PAD_DOWN;
        case USAGE(DESKTOP_PAGE, 0x92): return PAD_RIGHT;
        case USAGE(DESKTOP_PAGE, 0x93): return PAD_LEFT;
        default: return 0;
    }
}

static bool parse(hid_gamepad_t *pad, uint8_t const *descriptor, size_t length)
{
    globals_t global = {0}, stack[MAX_DEPTH];
    locals_t local = {0};
    bool collections[MAX_DEPTH], controller = false;
    unsigned global_depth = 0, depth = 0;
    size_t cursor = 0;
    while (cursor < length) {
        uint8_t prefix = descriptor[cursor++];
        if (prefix == 0xfe) // Long items and alternative usage sets are unsupported.
            return false;
        unsigned size = prefix & 3u;
        if (size == 3)
            size = 4;
        if (size > length - cursor)
            return false;
        uint32_t value = 0;
        for (unsigned byte = 0; byte < size; byte++)
            value |= (uint32_t)descriptor[cursor++] << (8 * byte);
        unsigned type = (prefix >> 2) & 3u, tag = prefix >> 4;
        if (type == 1) {
            switch (tag) {
                case 0:
                    if (value > UINT16_MAX) return false;
                    global.page = value;
                    break;
                case 1: global.minimum = signed_value(value, size * 8); break;
                case 2:
                    if (global.minimum >= 0 && value > INT32_MAX) return false;
                    global.maximum = global.minimum < 0 ? signed_value(value, size * 8) : (int32_t)value;
                    break;
                case 7: global.size = value; break;
                case 8:
                    if (value == 0 || value > UINT8_MAX) return false;
                    global.id = (uint8_t)value;
                    pad->report_ids = true;
                    break;
                case 9: global.count = value; break;
                case 10:
                    if (global_depth == MAX_DEPTH) return false;
                    stack[global_depth++] = global;
                    break;
                case 11:
                    if (!global_depth) return false;
                    global = stack[--global_depth];
                    break;
                default: break;
            }
        } else if (type == 2) {
            uint32_t usage = size == 4 ? value : USAGE(global.page, value);
            switch (tag) {
                case 0:
                    if (local.count == MAX_USAGES) return false;
                    local.usages[local.count++] = usage;
                    break;
                case 1: local.minimum = usage; local.range = true; break;
                case 2:
                    if (!local.range || usage < local.minimum ||
                        usage - local.minimum >= MAX_USAGES - local.count)
                        return false;
                    for (uint32_t i = local.minimum;; i++) {
                        local.usages[local.count++] = i;
                        if (i == usage) break;
                    }
                    local.range = false;
                    break;
                case 10: return false;
                default: break;
            }
        } else if (type == 0) {
            if (local.range)
                return false;
            if (tag == 10) {
                if (depth == MAX_DEPTH) return false;
                collections[depth++] = controller;
                if (value == 1) { // An Application collection starts a new device context.
                    uint32_t usage = local.count ? local.usages[0] : 0;
                    controller = usage == USAGE(DESKTOP_PAGE, 4) || usage == USAGE(DESKTOP_PAGE, 5);
                }
            } else if (tag == 12) {
                if (!depth) return false;
                controller = collections[--depth];
            } else if (tag == 8) { // Input offsets include constants and unrelated controls.
                if (!depth || global.size == 0 || global.size > 32 || global.count == 0 ||
                    global.count > GAMEPAD_MAX_REPORT_BYTES * 8u / global.size)
                    return false;
                int report = report_index(pad, global.id);
                if (report < 0) return false;
                gamepad_report_t *layout = &pad->reports[report];
                uint32_t bits = global.size * global.count;
                if (layout->bits + bits > GAMEPAD_MAX_REPORT_BYTES * 8u)
                    return false;
                if (controller && (value & 7u) == 2u && local.count) {
                    for (uint32_t i = 0; i < global.count; i++) {
                        uint32_t usage = local.usages[i < local.count ? i : local.count - 1u];
                        uint16_t kind = field_kind(usage);
                        if (!kind) continue;
                        int64_t range = (int64_t)global.maximum - global.minimum;
                        if (range < 0 || (kind == FIELD_HAT && range != 3 && range != 7) ||
                            ((kind >= FIELD_X) && (kind <= FIELD_RY) && range < 2))
                            continue;
                        if (pad->field_count == GAMEPAD_MAX_FIELDS) return false;
                        pad->fields[pad->field_count++] = (gamepad_field_t) {
                            .offset = (uint16_t)(layout->bits + i * global.size),
                            .size = (uint8_t)global.size,
                            .report = (uint8_t)report,
                            .kind = kind,
                            .minimum = global.minimum,
                            .maximum = global.maximum,
                        };
                    }
                }
                layout->bits += bits;
            }
            // HID local items never carry over to the next Main item.
            memset(&local, 0, sizeof(local));
        }
    }
    if (depth || global_depth || local.range || !pad->field_count)
        return false;
    uint16_t buttons = 0;
    for (unsigned i = 0; i < pad->field_count; i++)
        if (pad->fields[i].kind >= FIELD_BUTTON)
            buttons |= 1u << (pad->fields[i].kind - FIELD_BUTTON);
    uint8_t defaults[] = {1, 2, (buttons & 4) ? 3 : 4, 4, 5, 6, 10};
    // Stadia's HID usages skip 3 and 6; its Menu button is usage 12.
    if (!(buttons & (1u << 2)) && !(buttons & (1u << 5)) &&
        (buttons & 0x08dbu) == 0x08dbu) {
        uint8_t stadia[] = {1, 2, 4, 5, 7, 8, 12};
        memcpy(defaults, stadia, sizeof(defaults));
    }
    memcpy(pad->defaults, defaults, sizeof(defaults));
    for (unsigned i = 0; i < pad->report_count; i++)
        if ((pad->report_ids && !pad->reports[i].id) ||
            (pad->reports[i].bits + 7u) / 8u + (pad->report_ids ? 1u : 0u) > GAMEPAD_MAX_REPORT_BYTES)
            return false;
    return true;
}

bool hid_gamepad_parse(hid_gamepad_t *pad, uint8_t const *descriptor, size_t length)
{
    if (!pad)
        return false;
    memset(pad, 0, sizeof(*pad));
    if (!descriptor || !parse(pad, descriptor, length)) {
        memset(pad, 0, sizeof(*pad));
        return false;
    }
    for (unsigned i = 0; i < pad->field_count; i++) {
        uint16_t kind = pad->fields[i].kind;
        if ((kind >= FIELD_X) && (kind <= FIELD_RY))
            pad->has_axis[kind - FIELD_X] = true;
    }
    for (unsigned i = 0; i < pad->report_count; i++)
        pad->reports[i].hat = PAD_HAT_CENTRED;
    return true;
}

static int8_t axis_value(int64_t value, int32_t minimum, int32_t maximum)
{
    int64_t range = (int64_t)maximum - minimum;
    int64_t scaled;

    if (range <= 0)
        return 0;
    scaled = ((value - minimum) * 254) / range - 127;
    if (scaled < -127) scaled = -127;
    if (scaled > 127) scaled = 127;
    return (int8_t)scaled;
}

bool hid_gamepad_decode(hid_gamepad_t *pad, uint8_t const *report, size_t length, pad_input_t *input)
{
    static uint8_t const hat_directions[8] = { PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_RIGHT | PAD_DOWN,
        PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_LEFT | PAD_UP };
    uint8_t id, dpad = 0, hat = PAD_HAT_CENTRED;
    uint16_t buttons = 0;
    bool relevant = false;
    int index = -1;

    if (!pad || !report || !input || !length || !pad->field_count)
        return false;

    id = pad->report_ids ? *report++ : 0;
    if (pad->report_ids)
        length--;

    for (unsigned i = 0; i < pad->report_count; i++)
        if (pad->reports[i].id == id)
            index = (int)i;
    if ((index < 0) || (length < (pad->reports[index].bits + 7u) / 8u))
        return false;

    for (unsigned i = 0; i < pad->field_count; i++) {
        gamepad_field_t const *field = &pad->fields[i];
        uint32_t bits = 0;
        int64_t value;

        if (field->report != index)
            continue;
        relevant = true;

        for (unsigned bit = 0; bit < field->size; bit++) {
            unsigned pos = field->offset + bit;
            bits |= (uint32_t)((report[pos / 8] >> (pos % 8)) & 1u) << bit;
        }
        value = (field->minimum < 0) ? signed_value(bits, field->size) : (int64_t)bits;
        if ((value < field->minimum) || (value > field->maximum))
            continue;   // includes a hat's null (centred) value

        if ((field->kind >= FIELD_X) && (field->kind <= FIELD_RY)) {
            pad->axes[field->kind - FIELD_X] = axis_value(value, field->minimum, field->maximum);
        } else if (field->kind == FIELD_HAT) {
            unsigned position = (unsigned)(value - field->minimum);
            if (field->maximum - field->minimum == 3)
                position *= 2;
            hat = (uint8_t)position;
            dpad |= hat_directions[position];
        } else if (value != 0) {
            if (field->kind >= FIELD_BUTTON)
                buttons |= (uint16_t)(1u << (field->kind - FIELD_BUTTON));
            else
                dpad |= (uint8_t)field->kind;
        }
    }

    if (!relevant)
        return false;

    pad->reports[index].dpad = dpad;
    pad->reports[index].hat = hat;
    pad->reports[index].buttons = buttons;

    memset(input, 0, sizeof(*input));
    input->hat = PAD_HAT_CENTRED;
    for (unsigned i = 0; i < pad->report_count; i++) {
        input->dpad |= pad->reports[i].dpad;
        input->buttons |= pad->reports[i].buttons;
        if (pad->reports[i].hat != PAD_HAT_CENTRED)
            input->hat = pad->reports[i].hat;
    }
    input->lx = pad->axes[0];
    input->ly = pad->axes[1];
    input->rx = pad->axes[2];
    input->ry = pad->axes[3];
    input->lt = (uint8_t)(pad->axes[4] + 127);
    input->rt = (uint8_t)(pad->axes[5] + 127);
    if (!pad->has_axis[4]) input->lt = 0;
    if (!pad->has_axis[5]) input->rt = 0;
    memcpy(input->defaults, pad->defaults, sizeof(input->defaults));
    return true;
}
