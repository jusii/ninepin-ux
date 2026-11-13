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

// the machine a profile is for: it is offered in that mode only (and in usb
// mode, which offers them all)
enum { PROFILE_FOR_ANY = 0, PROFILE_FOR_AMIGA, PROFILE_FOR_C64, PROFILE_FOR_COUNT };

typedef struct {
    char name[PROFILE_NAME_LEN];    // empty: slot unused
    uint8_t pad_triangle;
    uint8_t pad_directions;
    settings_combo_t combo[SETTINGS_PAD_COMBOS];
    uint8_t machine;                // PROFILE_FOR_*
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
 * @return true if the profile is offered in this mode (SETTINGS_MODE_*)
 */
bool profiles_shown(profile_t const *profile, uint8_t mode);

/**
 * @return how many profiles are offered in this mode
 */
unsigned profiles_count_shown(uint8_t mode);

/**
 * @return the tag a profile saved in this mode gets unless told otherwise
 */
uint8_t profiles_machine_of(uint8_t mode);

/**
 * @brief Settings about to be stored: note the pad profile for the mode it
 *        was used in, and on a change of mode bring the new mode's own back
 *        (none: no combos, if there was a profile). settings.c.
 */
void profiles_follow_mode(settings_t *next, settings_t const *stored);

/**
 * @brief Copy the pad settings into a profile / a profile into the settings.
 */
void profiles_from_settings(profile_t *profile, char const *name, uint8_t machine, settings_t const *settings);
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
