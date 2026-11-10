/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * key names and keymaps for the kvmd-compatible api; see kvmd_keys.h.
 */

#include "kvmd_keys.h"

#include <string.h>

//--------------------------------------------------------------------+
// web key names -> usb hid usages
//--------------------------------------------------------------------+

typedef struct {
    char const *name;
    uint8_t usage;
} key_name_t;

static key_name_t const key_names[] = {
    { "Enter", 0x28 }, { "Escape", 0x29 }, { "Backspace", 0x2a }, { "Tab", 0x2b }, { "Space", 0x2c },
    { "Minus", 0x2d }, { "Equal", 0x2e }, { "BracketLeft", 0x2f }, { "BracketRight", 0x30 },
    { "Backslash", 0x31 }, { "IntlHash", 0x32 }, { "Semicolon", 0x33 }, { "Quote", 0x34 },
    { "Backquote", 0x35 }, { "Comma", 0x36 }, { "Period", 0x37 }, { "Slash", 0x38 }, { "CapsLock", 0x39 },
    { "F1", 0x3a }, { "F2", 0x3b }, { "F3", 0x3c }, { "F4", 0x3d }, { "F5", 0x3e }, { "F6", 0x3f },
    { "F7", 0x40 }, { "F8", 0x41 }, { "F9", 0x42 }, { "F10", 0x43 }, { "F11", 0x44 }, { "F12", 0x45 },
    { "PrintScreen", 0x46 }, { "ScrollLock", 0x47 }, { "Pause", 0x48 }, { "Insert", 0x49 },
    { "Home", 0x4a }, { "PageUp", 0x4b }, { "Delete", 0x4c }, { "End", 0x4d }, { "PageDown", 0x4e },
    { "ArrowRight", 0x4f }, { "ArrowLeft", 0x50 }, { "ArrowDown", 0x51 }, { "ArrowUp", 0x52 },
    { "NumLock", 0x53 }, { "NumpadDivide", 0x54 }, { "NumpadMultiply", 0x55 }, { "NumpadSubtract", 0x56 },
    { "NumpadAdd", 0x57 }, { "NumpadEnter", 0x58 }, { "Numpad1", 0x59 }, { "Numpad2", 0x5a },
    { "Numpad3", 0x5b }, { "Numpad4", 0x5c }, { "Numpad5", 0x5d }, { "Numpad6", 0x5e }, { "Numpad7", 0x5f },
    { "Numpad8", 0x60 }, { "Numpad9", 0x61 }, { "Numpad0", 0x62 }, { "NumpadDecimal", 0x63 },
    { "IntlBackslash", 0x64 }, { "ContextMenu", 0x65 }, { "Power", 0x66 }, { "NumpadEqual", 0x67 },
    { "F13", 0x68 }, { "F14", 0x69 }, { "F15", 0x6a }, { "F16", 0x6b }, { "F17", 0x6c }, { "F18", 0x6d },
    { "F19", 0x6e }, { "F20", 0x6f }, { "F21", 0x70 }, { "F22", 0x71 }, { "F23", 0x72 }, { "F24", 0x73 },
    { "AudioVolumeMute", 0x7f }, { "AudioVolumeUp", 0x80 }, { "AudioVolumeDown", 0x81 },
    { "NumpadComma", 0x85 }, { "IntlRo", 0x87 }, { "KanaMode", 0x88 }, { "IntlYen", 0x89 },
    { "Convert", 0x8a }, { "NonConvert", 0x8b }, { "Lang1", 0x90 }, { "Lang2", 0x91 },
    { "NumpadParenLeft", 0xb6 }, { "NumpadParenRight", 0xb7 },
    { "ControlLeft", 0xe0 }, { "ShiftLeft", 0xe1 }, { "AltLeft", 0xe2 }, { "MetaLeft", 0xe3 },
    { "ControlRight", 0xe4 }, { "ShiftRight", 0xe5 }, { "AltRight", 0xe6 }, { "MetaRight", 0xe7 },
};

uint8_t kvmd_key_usage(char const *name)
{
    size_t n;

    if (!name)
        return 0;
    n = strlen(name);
    // KeyA-KeyZ and Digit0-Digit9 by rule
    if ((n == 4) && !strncmp(name, "Key", 3) && (name[3] >= 'A') && (name[3] <= 'Z'))
        return (uint8_t)(0x04 + (name[3] - 'A'));
    if ((n == 6) && !strncmp(name, "Digit", 5) && (name[5] >= '0') && (name[5] <= '9'))
        return (name[5] == '0') ? 0x27 : (uint8_t)(0x1e + (name[5] - '1'));
    for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++)
        if (!strcmp(name, key_names[i].name))
            return key_names[i].usage;
    return 0;
}

//--------------------------------------------------------------------+
// keymaps: characters -> strokes
//--------------------------------------------------------------------+

#define DEAD "\x01"     // marks a dead-key character in a cell

typedef struct {
    uint8_t usage;
    char const *base, *shift, *altgr;   // utf-8, NULL or "" for none
} key_row_t;

typedef struct {
    char const *name;
    char const *letters;    // what KeyA..KeyZ type ('#': not a letter here)
    key_row_t const *rows;
    size_t row_count;
} keymap_t;

#define ROWS(r) r, (sizeof(r) / sizeof(r[0]))

static key_row_t const us_rows[] = {
    { 0x35, "`", "~", NULL }, { 0x1e, "1", "!", NULL }, { 0x1f, "2", "@", NULL }, { 0x20, "3", "#", NULL },
    { 0x21, "4", "$", NULL }, { 0x22, "5", "%", NULL }, { 0x23, "6", "^", NULL }, { 0x24, "7", "&", NULL },
    { 0x25, "8", "*", NULL }, { 0x26, "9", "(", NULL }, { 0x27, "0", ")", NULL }, { 0x2d, "-", "_", NULL },
    { 0x2e, "=", "+", NULL }, { 0x2f, "[", "{", NULL }, { 0x30, "]", "}", NULL }, { 0x31, "\\", "|", NULL },
    { 0x33, ";", ":", NULL }, { 0x34, "'", "\"", NULL }, { 0x36, ",", "<", NULL }, { 0x37, ".", ">", NULL },
    { 0x38, "/", "?", NULL },
};

static key_row_t const gb_rows[] = {
    { 0x35, "`", "\xc2\xac", "\xc2\xa6" }, { 0x1e, "1", "!", NULL }, { 0x1f, "2", "\"", NULL },
    { 0x20, "3", "\xc2\xa3", NULL }, { 0x21, "4", "$", "\xe2\x82\xac" }, { 0x22, "5", "%", NULL },
    { 0x23, "6", "^", NULL }, { 0x24, "7", "&", NULL }, { 0x25, "8", "*", NULL }, { 0x26, "9", "(", NULL },
    { 0x27, "0", ")", NULL }, { 0x2d, "-", "_", NULL }, { 0x2e, "=", "+", NULL }, { 0x2f, "[", "{", NULL },
    { 0x30, "]", "}", NULL }, { 0x32, "#", "~", NULL }, { 0x33, ";", ":", NULL }, { 0x34, "'", "@", NULL },
    { 0x64, "\\", "|", NULL }, { 0x36, ",", "<", NULL }, { 0x37, ".", ">", NULL }, { 0x38, "/", "?", NULL },
};

// finnish and swedish share the pc layout
static key_row_t const nordic_rows[] = {
    { 0x35, "\xc2\xa7", "\xc2\xbd", NULL }, { 0x1e, "1", "!", NULL }, { 0x1f, "2", "\"", "@" },
    { 0x20, "3", "#", "\xc2\xa3" }, { 0x21, "4", "\xc2\xa4", "$" }, { 0x22, "5", "%", "\xe2\x82\xac" },
    { 0x23, "6", "&", NULL }, { 0x24, "7", "/", "{" }, { 0x25, "8", "(", "[" }, { 0x26, "9", ")", "]" },
    { 0x27, "0", "=", "}" }, { 0x2d, "+", "?", "\\" }, { 0x2e, DEAD "\xc2\xb4", DEAD "`", NULL },
    { 0x2f, "\xc3\xa5", "\xc3\x85", NULL }, { 0x30, DEAD "\xc2\xa8", DEAD "^", DEAD "~" },
    { 0x33, "\xc3\xb6", "\xc3\x96", NULL }, { 0x34, "\xc3\xa4", "\xc3\x84", NULL }, { 0x32, "'", "*", NULL },
    { 0x64, "<", ">", "|" }, { 0x36, ",", ";", NULL }, { 0x37, ".", ":", NULL }, { 0x38, "-", "_", NULL },
    { 0x08, NULL, NULL, "\xe2\x82\xac" }, { 0x10, NULL, NULL, "\xc2\xb5" },
};

static key_row_t const de_rows[] = {
    { 0x35, DEAD "^", "\xc2\xb0", NULL }, { 0x1e, "1", "!", NULL }, { 0x1f, "2", "\"", "\xc2\xb2" },
    { 0x20, "3", "\xc2\xa7", "\xc2\xb3" }, { 0x21, "4", "$", NULL }, { 0x22, "5", "%", NULL },
    { 0x23, "6", "&", NULL }, { 0x24, "7", "/", "{" }, { 0x25, "8", "(", "[" }, { 0x26, "9", ")", "]" },
    { 0x27, "0", "=", "}" }, { 0x2d, "\xc3\x9f", "?", "\\" }, { 0x2e, DEAD "\xc2\xb4", DEAD "`", NULL },
    { 0x2f, "\xc3\xbc", "\xc3\x9c", NULL }, { 0x30, "+", "*", "~" }, { 0x33, "\xc3\xb6", "\xc3\x96", NULL },
    { 0x34, "\xc3\xa4", "\xc3\x84", NULL }, { 0x32, "#", "'", NULL }, { 0x64, "<", ">", "|" },
    { 0x36, ",", ";", NULL }, { 0x37, ".", ":", NULL }, { 0x38, "-", "_", NULL },
    { 0x14, NULL, NULL, "@" }, { 0x08, NULL, NULL, "\xe2\x82\xac" }, { 0x10, NULL, NULL, "\xc2\xb5" },
};

static key_row_t const fr_rows[] = {
    { 0x35, "\xc2\xb2", NULL, NULL }, { 0x1e, "&", "1", NULL }, { 0x1f, "\xc3\xa9", "2", DEAD "~" },
    { 0x20, "\"", "3", "#" }, { 0x21, "'", "4", "{" }, { 0x22, "(", "5", "[" }, { 0x23, "-", "6", "|" },
    { 0x24, "\xc3\xa8", "7", DEAD "`" }, { 0x25, "_", "8", "\\" }, { 0x26, "\xc3\xa7", "9", "^" },
    { 0x27, "\xc3\xa0", "0", "@" }, { 0x2d, ")", "\xc2\xb0", "]" }, { 0x2e, "=", "+", "}" },
    { 0x2f, DEAD "^", DEAD "\xc2\xa8", NULL }, { 0x30, "$", "\xc2\xa3", "\xc2\xa4" }, { 0x33, "m", "M", NULL },
    { 0x34, "\xc3\xb9", "%", NULL }, { 0x32, "*", "\xc2\xb5", NULL }, { 0x64, "<", ">", NULL },
    { 0x10, ",", "?", NULL }, { 0x36, ";", ".", NULL }, { 0x37, ":", "/", NULL }, { 0x38, "!", "\xc2\xa7", NULL },
    { 0x08, NULL, NULL, "\xe2\x82\xac" },
};

static keymap_t const keymaps[] = {
    { "en-us", "abcdefghijklmnopqrstuvwxyz", ROWS(us_rows) },
    { "en-gb", "abcdefghijklmnopqrstuvwxyz", ROWS(gb_rows) },
    { "fi",    "abcdefghijklmnopqrstuvwxyz", ROWS(nordic_rows) },
    { "sv",    "abcdefghijklmnopqrstuvwxyz", ROWS(nordic_rows) },
    { "de",    "abcdefghijklmnopqrstuvwxzy", ROWS(de_rows) },
    { "fr",    "qbcdefghijkl#noparstuvzxyw", ROWS(fr_rows) },
};

static char const *const keymap_names[] = { "en-us", "en-gb", "fi", "sv", "de", "fr", NULL };

char const *const *kvmd_keymaps(void)
{
    return keymap_names;
}

static keymap_t const *find_keymap(char const *name)
{
    for (size_t i = 0; i < sizeof(keymaps) / sizeof(keymaps[0]); i++)
        if (name && !strcmp(name, keymaps[i].name))
            return &keymaps[i];
    return NULL;
}

bool kvmd_keymap_known(char const *name)
{
    return find_keymap(name) != NULL;
}

size_t kvmd_utf8_next(char const *text, size_t len, uint32_t *codepoint)
{
    uint8_t const *s = (uint8_t const *)text;
    size_t need;
    uint32_t cp;

    if (!len)
        return 0;
    if (s[0] < 0x80) {
        *codepoint = s[0];
        return 1;
    }
    if ((s[0] & 0xe0) == 0xc0) { need = 2; cp = s[0] & 0x1f; }
    else if ((s[0] & 0xf0) == 0xe0) { need = 3; cp = s[0] & 0x0f; }
    else if ((s[0] & 0xf8) == 0xf0) { need = 4; cp = s[0] & 0x07; }
    else { *codepoint = 0xfffd; return 1; }

    if (need > len) {
        *codepoint = 0xfffd;
        return len;
    }
    for (size_t i = 1; i < need; i++) {
        if ((s[i] & 0xc0) != 0x80) {
            *codepoint = 0xfffd;
            return i;
        }
        cp = (cp << 6) | (s[i] & 0x3f);
    }
    *codepoint = cp;
    return need;
}

/**
 * @return true if cell types codepoint; *dead says whether it is a dead key
 */
static bool cell_types(char const *cell, uint32_t codepoint, bool *dead)
{
    uint32_t cp = 0;
    size_t len;

    if (!cell || !*cell)
        return false;
    *dead = (*cell == '\x01');
    if (*dead)
        cell++;
    len = strlen(cell);
    return (kvmd_utf8_next(cell, len, &cp) == len) && (cp == codepoint);
}

bool kvmd_keymap_stroke(char const *keymap, uint32_t codepoint, kvmd_stroke_t *stroke)
{
    keymap_t const *map = find_keymap(keymap ? keymap : keymap_names[0]);
    bool found_dead = false;
    kvmd_stroke_t dead_stroke = { 0, 0 };

    if (!map)
        return false;

    // control characters every layout has
    if (codepoint == '\n' || codepoint == '\r') { stroke->usage = 0x28; stroke->flags = 0; return true; }
    if (codepoint == '\t') { stroke->usage = 0x2b; stroke->flags = 0; return true; }
    if (codepoint == ' ') { stroke->usage = 0x2c; stroke->flags = 0; return true; }

    // letters, upper case with shift
    for (unsigned i = 0; i < 26; i++) {
        char letter = map->letters[i];

        if (letter == '#' || letter == ' ')
            continue;
        if (codepoint == (uint32_t)letter) { stroke->usage = (uint8_t)(0x04 + i); stroke->flags = 0; return true; }
        if (codepoint == (uint32_t)(letter - 'a' + 'A')) {
            stroke->usage = (uint8_t)(0x04 + i);
            stroke->flags = KVMD_STROKE_SHIFT;
            return true;
        }
    }

    // the rest; a direct key wins over a dead one for the same character
    for (size_t i = 0; i < map->row_count; i++) {
        key_row_t const *row = &map->rows[i];
        char const *cells[3] = { row->base, row->shift, row->altgr };
        uint8_t const flags[3] = { 0, KVMD_STROKE_SHIFT, KVMD_STROKE_ALTGR };

        for (unsigned level = 0; level < 3; level++) {
            bool dead;

            if (!cell_types(cells[level], codepoint, &dead))
                continue;
            if (!dead) {
                stroke->usage = row->usage;
                stroke->flags = flags[level];
                return true;
            }
            if (!found_dead) {
                found_dead = true;
                dead_stroke.usage = row->usage;
                dead_stroke.flags = (uint8_t)(flags[level] | KVMD_STROKE_DEAD);
            }
        }
    }
    if (found_dead) {
        *stroke = dead_stroke;
        return true;
    }
    return false;
}
