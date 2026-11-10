/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb/bluetooth keyboard -> amiga mapping with the user's settings applied.
 * ported from stefan reinauer's keyboard_map.c (reinauer/amigahid-pico,
 * features branch, EPL-2.0), adapted to this tree's settings fields; the hid
 * names gained a few keys the amiga lacks.
 */

#include "keyboard_map.h"

#include <stdio.h>

#include "tusb.h"

#include "platform/amiga/keyboard.h"
#include "settings.h"

uint8_t keyboard_map_key(uint8_t hid)
{
    static uint8_t const help[SETTINGS_KB_HELP_COUNT] = { HID_KEY_INSERT, HID_KEY_HOME, HID_KEY_PAGE_UP, HID_KEY_F11, 0 };
    static uint8_t const del[SETTINGS_KB_DELETE_COUNT] = { HID_KEY_DELETE, HID_KEY_BACKSPACE, 0 };
    settings_t const *s = settings_get();

    if (!hid)
        return AMIGA_UNKNOWN;

    // a remap slot wins over everything else
    for (unsigned i = 0; i < SETTINGS_REMAP_SLOTS; i++) {
        if (s->kb_remap[i].source == hid) {
            uint8_t target = s->kb_remap[i].target;
            return (target == KEYBOARD_MAP_DISABLED) ? AMIGA_UNKNOWN : target;
        }
    }

    // the keys chosen for help and del; insert and delete only mean them when
    // chosen (backspace keeps its own meaning when it is not del)
    if (hid == help[s->kb_help])
        return AMIGA_HELP;
    if (hid == del[s->kb_delete])
        return AMIGA_DELETE;
    if ((hid == HID_KEY_INSERT) || (hid == HID_KEY_DELETE))
        return AMIGA_UNKNOWN;

    if ((s->kb_iso == SETTINGS_KB_ISO_AMIGA) && (hid == HID_KEY_EUROPE_2))
        return AMIGA_INTLSHIFT;

    if (s->kb_preset == SETTINGS_KB_PRESET_YZ) {
        if (hid == HID_KEY_Y) return AMIGA_Z;
        if (hid == HID_KEY_Z) return AMIGA_Y;
    } else if (s->kb_preset == SETTINGS_KB_PRESET_AQWZ) {
        if (hid == HID_KEY_A) return AMIGA_Q;
        if (hid == HID_KEY_Q) return AMIGA_A;
        if (hid == HID_KEY_W) return AMIGA_Z;
        if (hid == HID_KEY_Z) return AMIGA_W;
    }

    return mapHidToAmiga[hid];
}

char const *keyboard_map_amiga_name(uint8_t amiga)
{
    static char const *const names[AMIGA_RAMIGA + 1] = {
        [AMIGA_BACKTICK] = "`",         [AMIGA_ONE] = "1",              [AMIGA_TWO] = "2",
        [AMIGA_THREE] = "3",            [AMIGA_FOUR] = "4",             [AMIGA_FIVE] = "5",
        [AMIGA_SIX] = "6",              [AMIGA_SEVEN] = "7",            [AMIGA_EIGHT] = "8",
        [AMIGA_NINE] = "9",             [AMIGA_ZERO] = "0",             [AMIGA_DASH] = "-",
        [AMIGA_EQUALS] = "=",           [AMIGA_BACKSLASH] = "\\",       [AMIGA_KPZERO] = "kp0",
        [AMIGA_Q] = "Q", [AMIGA_W] = "W", [AMIGA_E] = "E", [AMIGA_R] = "R", [AMIGA_T] = "T",
        [AMIGA_Y] = "Y", [AMIGA_U] = "U", [AMIGA_I] = "I", [AMIGA_O] = "O", [AMIGA_P] = "P",
        [AMIGA_OSQPARENS] = "[",        [AMIGA_CSQPARENS] = "]",
        [AMIGA_KPONE] = "kp1",          [AMIGA_KPTWO] = "kp2",          [AMIGA_KPTHREE] = "kp3",
        [AMIGA_A] = "A", [AMIGA_S] = "S", [AMIGA_D] = "D", [AMIGA_F] = "F", [AMIGA_G] = "G",
        [AMIGA_H] = "H", [AMIGA_J] = "J", [AMIGA_K] = "K", [AMIGA_L] = "L",
        [AMIGA_SEMICOLON] = ";",        [AMIGA_QUOTE] = "'",            [AMIGA_INTLRET] = "iso ret",
        [AMIGA_KPFOUR] = "kp4",         [AMIGA_KPFIVE] = "kp5",         [AMIGA_KPSIX] = "kp6",
        [AMIGA_INTLSHIFT] = "iso shift",
        [AMIGA_Z] = "Z", [AMIGA_X] = "X", [AMIGA_C] = "C", [AMIGA_V] = "V", [AMIGA_B] = "B",
        [AMIGA_N] = "N", [AMIGA_M] = "M",
        [AMIGA_COMMA] = ",",            [AMIGA_PERIOD] = ".",           [AMIGA_SLASH] = "/",
        [AMIGA_KPPERIOD] = "kp.",       [AMIGA_KPSEVEN] = "kp7",        [AMIGA_KPEIGHT] = "kp8",
        [AMIGA_KPNINE] = "kp9",         [AMIGA_SPACE] = "space",        [AMIGA_BACKSP] = "bksp",
        [AMIGA_TAB] = "tab",            [AMIGA_KPENTER] = "kp enter",   [AMIGA_RETURN] = "return",
        [AMIGA_ESC] = "esc",            [AMIGA_DELETE] = "del",         [AMIGA_KPDASH] = "kp-",
        [AMIGA_UP] = "up",              [AMIGA_DOWN] = "down",          [AMIGA_RIGHT] = "right",
        [AMIGA_LEFT] = "left",
        [AMIGA_F1] = "F1", [AMIGA_F2] = "F2", [AMIGA_F3] = "F3", [AMIGA_F4] = "F4", [AMIGA_F5] = "F5",
        [AMIGA_F6] = "F6", [AMIGA_F7] = "F7", [AMIGA_F8] = "F8", [AMIGA_F9] = "F9", [AMIGA_F10] = "F10",
        [AMIGA_KPOPAREN] = "kp(",       [AMIGA_KPCPAREN] = "kp)",       [AMIGA_KPSLASH] = "kp/",
        [AMIGA_KPAST] = "kp*",          [AMIGA_KPPLUS] = "kp+",         [AMIGA_HELP] = "help",
        [AMIGA_LSHIFT] = "lshift",      [AMIGA_RSHIFT] = "rshift",      [AMIGA_CAPSLOCK] = "caps",
        [AMIGA_CTRL] = "ctrl",          [AMIGA_LALT] = "lalt",          [AMIGA_RALT] = "ralt",
        [AMIGA_LAMIGA] = "lamiga",      [AMIGA_RAMIGA] = "ramiga",
    };

    if (amiga == KEYBOARD_MAP_DISABLED)
        return "off";
    return ((amiga <= AMIGA_RAMIGA) && names[amiga]) ? names[amiga] : "?";
}

char const *keyboard_map_hid_name(uint8_t hid)
{
    static char unknown[8];

    switch (hid) {
        case 0:                     return "none";
        case KEYBOARD_MAP_DISABLED: return "off";
        case HID_KEY_INSERT:        return "insert";
        case HID_KEY_HOME:          return "home";
        case HID_KEY_PAGE_UP:       return "pgup";
        case HID_KEY_PAGE_DOWN:     return "pgdn";
        case HID_KEY_END:           return "end";
        case HID_KEY_F11:           return "F11";
        case HID_KEY_F12:           return "F12";
        case HID_KEY_PRINT_SCREEN:  return "prtsc";
        case HID_KEY_SCROLL_LOCK:   return "scrlk";
        case HID_KEY_PAUSE:         return "pause";
        case HID_KEY_NUM_LOCK:      return "numlk";
        case HID_KEY_DELETE:        return "delete";
        case HID_KEY_APPLICATION:   return "menu";
        case HID_KEY_CONTROL_LEFT:  return "lctrl";
        case HID_KEY_CONTROL_RIGHT: return "rctrl";
        case HID_KEY_SHIFT_LEFT:    return "lshift";
        case HID_KEY_SHIFT_RIGHT:   return "rshift";
        case HID_KEY_ALT_LEFT:      return "lalt";
        case HID_KEY_ALT_RIGHT:     return "altgr";
        case HID_KEY_GUI_LEFT:      return "lgui";
        case HID_KEY_GUI_RIGHT:     return "rgui";
        default:
            break;
    }

    if (mapHidToAmiga[hid] != AMIGA_UNKNOWN)
        return keyboard_map_amiga_name(mapHidToAmiga[hid]);

    snprintf(unknown, sizeof(unknown), "0x%02x", hid);
    return unknown;
}
