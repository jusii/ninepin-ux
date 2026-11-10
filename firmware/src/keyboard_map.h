/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb/bluetooth keyboard -> amiga key mapping with the user's settings applied
 * (remap slots, help/del keys, iso key, letter presets), and key names for the
 * menu. ported from stefan reinauer's keyboard_map.c (reinauer/amigahid-pico,
 * features branch, EPL-2.0).
 */

#ifndef _KEYBOARD_MAP_H
#define _KEYBOARD_MAP_H

#include <stdint.h>

// a remap target that swallows the key
#define KEYBOARD_MAP_DISABLED 0xff

/**
 * @return the amiga keycode for a hid usage under the current settings, or
 *         AMIGA_UNKNOWN if the key does nothing on the amiga
 */
uint8_t keyboard_map_key(uint8_t hid);

/**
 * @return a short name for an amiga keycode ("Disabled", "?" for unused codes)
 */
char const *keyboard_map_amiga_name(uint8_t amiga);

/**
 * @return a short name for a hid keyboard usage
 */
char const *keyboard_map_hid_name(uint8_t hid);

#endif // _KEYBOARD_MAP_H
