/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * pad profiles; see profiles.h. stored as settings.c stores its record: the
 * whole table in one record with a sequence number and a crc, written to the
 * two sectors in turn, the newest valid one read back at boot.
 */

#include "profiles.h"
#include "config.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "flash_map.h"

#define PROFILES_MAGIC  0x50484141u     // "AAHP"
#define PROFILES_SCHEMA 2u

// schema 1: 16 profiles without the machine tag. read once and carried over
// (as "any"); the next store writes schema 2
#define PROFILES_V1_MAX 16u

typedef struct {
    char name[PROFILE_NAME_LEN];
    uint8_t pad_triangle;
    uint8_t pad_directions;
    settings_combo_t combo[SETTINGS_PAD_COMBOS];
} profile_v1_t;

typedef struct {
    uint32_t magic;
    uint32_t schema;
    uint32_t length;
    uint32_t sequence;
    profile_v1_t table[PROFILES_V1_MAX];
    uint32_t crc;
} profiles_record_v1_t;

typedef struct {
    uint32_t magic;
    uint32_t schema;
    uint32_t length;
    uint32_t sequence;
    profile_t table[PROFILES_MAX];
    uint32_t crc;
} profiles_record_t;

// the record, rounded up to whole flash pages
#define PROFILES_WRITE_SIZE ((sizeof(profiles_record_t) + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE)

_Static_assert(PROFILES_WRITE_SIZE <= FLASH_SECTOR_SIZE, "the profiles must fit one flash sector");

static profile_t table[PROFILES_MAX];
static int current_bank = -1;
static uint32_t current_sequence;

static uint32_t bytes_crc(uint8_t const *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;

    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return ~crc;
}

static uint32_t record_crc(profiles_record_t const *record)
{
    return bytes_crc((uint8_t const *)record, offsetof(profiles_record_t, crc));
}

static profiles_record_t const *bank_record(unsigned bank)
{
    return (profiles_record_t const *)(FLASH_MAP_READ_BASE + FLASH_MAP_PROFILES_OFFSET + bank * FLASH_SECTOR_SIZE);
}

bool profiles_name_ok(char const *name)
{
    size_t length = strlen(name);

    if (!length || (length >= PROFILE_NAME_LEN) || (name[0] == ' ') || (name[length - 1] == ' '))
        return false;
    for (size_t i = 0; i < length; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(" -_.+", c)))
            return false;
    }
    return true;
}

static bool profile_valid(profile_t const *profile)
{
    if (!profile->name[0])
        return true;
    if (!memchr(profile->name, 0, sizeof(profile->name)) || !profiles_name_ok(profile->name))
        return false;
    if ((profile->pad_triangle >= SETTINGS_PAD_TRIANGLE_COUNT) || (profile->pad_directions >= SETTINGS_PAD_DIR_COUNT)
        || (profile->machine >= PROFILE_FOR_COUNT))
        return false;
    for (unsigned i = 0; i < SETTINGS_PAD_COMBOS; i++)
        if ((profile->combo[i].button > SETTINGS_PAD_COMBO_BUTTONS)
            || (profile->combo[i].outputs & (uint8_t)~SETTINGS_PAD_COMBO_OUTPUTS))
            return false;
    return true;
}

static bool record_valid(profiles_record_t const *record)
{
    if ((record->magic != PROFILES_MAGIC) || (record->schema != PROFILES_SCHEMA)
        || (record->length != sizeof(record->table)) || (record->crc != record_crc(record)))
        return false;
    for (unsigned i = 0; i < PROFILES_MAX; i++)
        if (!profile_valid(&record->table[i]))
            return false;
    return true;
}

/**
 * a schema 1 record into table form; false if it is not one, or not sound
 */
static bool record_v1(profiles_record_v1_t const *old, profile_t out[PROFILES_MAX])
{
    if ((old->magic != PROFILES_MAGIC) || (old->schema != 1u) || (old->length != sizeof(old->table))
        || (old->crc != bytes_crc((uint8_t const *)old, offsetof(profiles_record_v1_t, crc))))
        return false;

    memset(out, 0, sizeof(profile_t) * PROFILES_MAX);
    for (unsigned i = 0; i < PROFILES_V1_MAX; i++) {
        memcpy(out[i].name, old->table[i].name, sizeof(out[i].name));
        out[i].pad_triangle = old->table[i].pad_triangle;
        out[i].pad_directions = old->table[i].pad_directions;
        memcpy(out[i].combo, old->table[i].combo, sizeof(out[i].combo));
        out[i].machine = PROFILE_FOR_ANY;
        if (!profile_valid(&out[i]))
            return false;
    }
    return true;
}

void profiles_init(void)
{
    static profiles_record_t record;    // too large for an early stack
    static profiles_record_v1_t old;
    static profile_t carried[PROFILES_MAX];
    bool current = false;               // a schema 2 record was found

    memset(table, 0, sizeof(table));
    current_bank = -1;
    current_sequence = 0;

    for (unsigned bank = 0; bank < 2; bank++) {
        // one copy out of flash, then everything is checked against the copy
        memcpy(&record, bank_record(bank), sizeof(record));
        if (!record_valid(&record)) {
            // an older firmware's record, only whilst there is nothing newer
            memcpy(&old, bank_record(bank), sizeof(old));
            if (!current && record_v1(&old, carried)
                && ((current_bank < 0) || ((int32_t)(old.sequence - current_sequence) > 0))) {
                current_bank = (int)bank;
                current_sequence = old.sequence;
                memcpy(table, carried, sizeof(table));
            }
            continue;
        }

        // newest wins, across sequence number rollover; any of this schema
        // over one carried from the old
        if (!current || ((int32_t)(record.sequence - current_sequence) > 0)) {
            current = true;
            current_bank = (int)bank;
            current_sequence = record.sequence;
            memcpy(table, record.table, sizeof(table));
        }
    }
}

profile_t const *profiles_get(unsigned slot)
{
    return ((slot < PROFILES_MAX) && table[slot].name[0]) ? &table[slot] : NULL;
}

int profiles_find(char const *name)
{
    for (unsigned i = 0; i < PROFILES_MAX; i++)
        if (table[i].name[0] && (strcmp(table[i].name, name) == 0))
            return (int)i;
    return -1;
}

unsigned profiles_count(void)
{
    unsigned count = 0;

    for (unsigned i = 0; i < PROFILES_MAX; i++)
        if (table[i].name[0])
            count++;
    return count;
}

bool profiles_shown(profile_t const *profile, uint8_t mode)
{
    return (profile->machine == PROFILE_FOR_ANY) || (mode >= SETTINGS_MODE_USB)
        || (profile->machine == profiles_machine_of(mode));
}

unsigned profiles_count_shown(uint8_t mode)
{
    unsigned count = 0;

    for (unsigned i = 0; i < PROFILES_MAX; i++)
        if (table[i].name[0] && profiles_shown(&table[i], mode))
            count++;
    return count;
}

uint8_t profiles_machine_of(uint8_t mode)
{
    return (mode == SETTINGS_MODE_AMIGA) ? PROFILE_FOR_AMIGA : (mode == SETTINGS_MODE_C64) ? PROFILE_FOR_C64
        : PROFILE_FOR_ANY;
}

void profiles_follow_mode(settings_t *next, settings_t const *stored)
{
    profile_t const *profile;
    uint8_t wanted;

    if ((next->mode >= SETTINGS_MODE_COUNT) || (stored->mode >= SETTINGS_MODE_COUNT))
        return;

    if (next->mode == stored->mode) {
        next->pad_profile_mode[next->mode] = next->pad_profile;
        return;
    }

    // what was in use belongs to the mode being left
    next->pad_profile_mode[stored->mode] = next->pad_profile;
    wanted = next->pad_profile_mode[next->mode];
    profile = wanted ? profiles_get(wanted - 1u) : NULL;

    if (profile && profiles_shown(profile, next->mode)) {
        profiles_to_settings(profile, next);
        next->pad_profile = wanted;
        return;
    }

    // this mode had none: the other mode's combos do not come along
    if (next->pad_profile)
        memset(next->pad_combo, 0, sizeof(next->pad_combo));
    next->pad_profile = 0;
    next->pad_profile_mode[next->mode] = 0;
}

void profiles_from_settings(profile_t *profile, char const *name, uint8_t machine, settings_t const *settings)
{
    memset(profile, 0, sizeof(*profile));
    profile->machine = (machine < PROFILE_FOR_COUNT) ? machine : PROFILE_FOR_ANY;
    strncpy(profile->name, name, sizeof(profile->name) - 1);
    profile->pad_triangle = settings->pad_triangle;
    profile->pad_directions = settings->pad_directions;
    memcpy(profile->combo, settings->pad_combo, sizeof(profile->combo));
}

void profiles_to_settings(profile_t const *profile, settings_t *settings)
{
    settings->pad_triangle = profile->pad_triangle;
    settings->pad_directions = profile->pad_directions;
    memcpy(settings->pad_combo, profile->combo, sizeof(settings->pad_combo));
}

bool profiles_matches(profile_t const *profile, settings_t const *settings)
{
    return (profile->pad_triangle == settings->pad_triangle)
        && (profile->pad_directions == settings->pad_directions)
        && (memcmp(profile->combo, settings->pad_combo, sizeof(profile->combo)) == 0);
}

//--------------------------------------------------------------------+
// writing
//--------------------------------------------------------------------+

typedef struct {
    uint32_t offset;
    uint8_t pages[PROFILES_WRITE_SIZE];
} profiles_write_t;

static void write_record(void *context)
{
    profiles_write_t const *write = context;

    flash_range_erase(write->offset, FLASH_SECTOR_SIZE);
    flash_range_program(write->offset, write->pages, sizeof(write->pages));
}

/**
 * store next as the table; table itself changes only once it is in flash
 */
static bool store(profile_t const next[PROFILES_MAX])
{
    static profiles_write_t write;      // main loop only
    static profiles_record_t record;
    unsigned bank = (current_bank == 0) ? 1u : 0u;
    int result;

    memset(&record, 0, sizeof(record));
    record.magic = PROFILES_MAGIC;
    record.schema = PROFILES_SCHEMA;
    record.length = sizeof(record.table);
    record.sequence = current_sequence + 1u;
    memcpy(record.table, next, sizeof(record.table));
    record.crc = record_crc(&record);

    write.offset = FLASH_MAP_PROFILES_OFFSET + bank * FLASH_SECTOR_SIZE;
    memset(write.pages, 0xff, sizeof(write.pages));
    memcpy(write.pages, &record, sizeof(record));

    // as settings_save(): core1 is parked in ram whilst xip is off
    result = flash_safe_execute(write_record, &write, 500);
    if ((result != PICO_OK) || (memcmp(bank_record(bank), &record, sizeof(record)) != 0))
        return false;

    current_bank = (int)bank;
    current_sequence = record.sequence;
    memcpy(table, next, sizeof(table));
    return true;
}

int profiles_put(profile_t const *profile)
{
    static profile_t next[PROFILES_MAX];
    int slot = profiles_find(profile->name);

    if (!profile_valid(profile) || !profile->name[0])
        return -2;

    for (unsigned i = 0; (slot < 0) && (i < PROFILES_MAX); i++)
        if (!table[i].name[0])
            slot = (int)i;
    if (slot < 0)
        return -1;

    // unchanged: no flash wear
    if (memcmp(&table[slot], profile, sizeof(*profile)) == 0)
        return slot;

    memcpy(next, table, sizeof(next));
    next[slot] = *profile;
    return store(next) ? slot : -2;
}

bool profiles_remove(unsigned slot)
{
    static profile_t next[PROFILES_MAX];

    if ((slot >= PROFILES_MAX) || !table[slot].name[0])
        return true;

    memcpy(next, table, sizeof(next));
    memset(&next[slot], 0, sizeof(next[slot]));
    return store(next);
}
