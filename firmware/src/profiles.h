/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * pad profiles: named copies of the pad settings a game wants (the combos,
 * the triangle button, the directions), kept in their own flash sectors
 * (flash_map.h). a profile is a copy and nothing more: loading one writes it
 * into the settings, which stay what the pads go by; settings_t.pad_profile
 * remembers which one that was.
 *
 * the table is read from any context; profiles_put() and profiles_remove()
 * are for the main loop, with the lwip lock held where there is a page that
 * reads the table (wifi.c).
 */

#ifndef _PROFILES_H
#define _PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

#define PROFILES_MAX        SETTINGS_PAD_PROFILES
#define PROFILE_NAME_LEN    13      // 12 + nul

typedef struct {
    char name[PROFILE_NAME_LEN];    // empty: slot unused
    uint8_t pad_triangle;
    uint8_t pad_directions;
    settings_combo_t combo[SETTINGS_PAD_COMBOS];
} profile_t;

/**
 * @brief Load the table from flash (empty if there is none). Once, early in
 *        main(), after settings_init().
 */
void profiles_init(void);

/**
 * @return the profile in a slot, NULL if the slot is unused or out of range
 */
profile_t const *profiles_get(unsigned slot);

/**
 * @return the slot of the profile with this name, or -1
 */
int profiles_find(char const *name);

/**
 * @return true if the text can be a profile's name: 1 to 12 letters, digits,
 *         spaces, '-', '_', '.', '+', not starting or ending with a space
 */
bool profiles_name_ok(char const *name);

/**
 * @return how many slots are in use
 */
unsigned profiles_count(void);

/**
 * @brief Copy the pad settings into a profile / a profile into the settings.
 */
void profiles_from_settings(profile_t *profile, char const *name, settings_t const *settings);
void profiles_to_settings(profile_t const *profile, settings_t *settings);

/**
 * @return true if the settings' pad values are the profile's
 */
bool profiles_matches(profile_t const *profile, settings_t const *settings);

/**
 * @brief Store a profile under its name: over the one of that name, else in a
 *        free slot. Writes flash.
 *
 * @return its slot, -1 if every slot is taken, -2 if the write failed
 */
int profiles_put(profile_t const *profile);

/**
 * @brief Empty a slot. Writes flash.
 *
 * @return false if the write failed
 */
bool profiles_remove(unsigned slot);

#endif // _PROFILES_H
