/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * a pad profile as a text file, to keep and to hand on:
 *
 *     ninepin-ux pad profile
 *     # a comment
 *     name=Blue Max
 *     machine=c64                 any, amiga, c64
 *     directions=both             both, dpad, stick
 *     triangle=up                 up, autofire, off
 *     autofire=10                 5, 8, 10, 15, 20 a second
 *     combo=r1 down fire1         a button, then what it stands for:
 *     combo=l2 key=space            up down left right fire1 fire2 fire3
 *     combo=r2 fire1 auto           auto key=<name>
 *
 * buttons go by their place on the pad, whatever the make: west south east
 * north (the four on the right), l1 r1 l2 r2, select start, l3 r3 (the stick
 * clicks), home, touchpad, mute, b16; or a number 1 to 16. key names are the
 * page's. settings left out are as the factory has them.
 */

#ifndef _PROFILE_TEXT_H
#define _PROFILE_TEXT_H

#include <stdbool.h>
#include <stddef.h>

#include "profiles.h"

#define PROFILE_TEXT_MAX 512    // more than the longest profile writes

/**
 * @return the text's length (no nul counted), 0 if it does not fit
 */
size_t profile_text_write(profile_t const *profile, char *out, size_t size);

/**
 * @brief Read a profile from text (not nul terminated; lf or cr lf lines).
 *
 * @return false, with error filled in, if it is not a sound profile
 */
bool profile_text_read(char const *text, size_t length, profile_t *profile, char *error, size_t error_size);

#endif // _PROFILE_TEXT_H
