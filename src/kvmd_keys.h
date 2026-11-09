/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * key names and keymaps for the kvmd-compatible api (kvmd_api.c): web
 * KeyboardEvent.code names to usb hid usages, and characters to key strokes
 * for the target's keyboard layout. tables written for this project from the
 * w3c ui events code list, the usb hid usage tables and the published layouts.
 */

#ifndef _KVMD_KEYS_H
#define _KVMD_KEYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KVMD_STROKE_SHIFT   0x01
#define KVMD_STROKE_ALTGR   0x02
#define KVMD_STROKE_DEAD    0x04    // a dead key: a space after it gives the character

typedef struct {
    uint8_t usage;      // usb hid usage
    uint8_t flags;      // KVMD_STROKE_*
} kvmd_stroke_t;

/**
 * @return the usb hid usage for a web key name ("KeyA", "ShiftLeft"), 0 if
 *         unknown; modifiers are 0xe0-0xe7
 */
uint8_t kvmd_key_usage(char const *name);

/**
 * @return the keymap names, NULL-terminated; the first is the default
 */
char const *const *kvmd_keymaps(void);

/**
 * @return true if name is a known keymap
 */
bool kvmd_keymap_known(char const *name);

/**
 * @brief The stroke that types codepoint on the given keymap.
 *
 * @return false if the layout cannot type it
 */
bool kvmd_keymap_stroke(char const *keymap, uint32_t codepoint, kvmd_stroke_t *stroke);

/**
 * @brief Decode one utf-8 character.
 *
 * @return bytes used (1-4), or 0 at the end; a malformed byte gives U+FFFD
 */
size_t kvmd_utf8_next(char const *text, size_t len, uint32_t *codepoint);

#endif // _KVMD_KEYS_H
