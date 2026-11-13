/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * persistent settings. the storage scheme is ported from stefan reinauer's
 * settings.c (reinauer/amigahid-pico, features branch, EPL-2.0): two
 * alternating flash sectors, each holding one record with a sequence number and
 * a crc32; the newest valid record wins, and a save always goes to the other
 * sector, so a write cut short by power loss leaves the previous record intact.
 *
 * differences from the original: this tree's own record layout (schema 1, no
 * migrations), and records are read through the untranslated xip window, since
 * on rev7 the image may run from partition slot B, which the bootrom maps onto
 * XIP_BASE in place of the start of flash.
 */

#include "settings.h"
#include "profiles.h"
#include "config.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "flash_map.h"

// two sectors directly below btstack's pairing bank (flash_map.h)
#define SETTINGS_FLASH_OFFSET FLASH_MAP_SETTINGS_OFFSET
#define SETTINGS_READ_BASE    FLASH_MAP_READ_BASE

#define SETTINGS_MAGIC  0x53484141u     // "AAHS"
#define SETTINGS_SCHEMA 1u

typedef struct {
    uint32_t magic;
    uint32_t schema;
    uint32_t length;
    uint32_t sequence;
    settings_t values;
    uint32_t crc;
} settings_record_t;

_Static_assert(FLASH_SECTOR_SIZE == 4096, "settings assume 4K flash sectors");
_Static_assert(sizeof(settings_t) == 224, "settings_t changed size: bump SETTINGS_SCHEMA");
_Static_assert(sizeof(settings_record_t) <= FLASH_PAGE_SIZE, "a settings record must fit one flash page");

static settings_t active;
static int current_bank = -1;
static uint32_t current_sequence;
static uint32_t generation;

void settings_defaults(settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));

    settings->mode = SETTINGS_MODE_AMIGA;
    settings->amiga_port[0] = SETTINGS_AMIGA_PORT_MOUSE;
    settings->amiga_port[1] = SETTINGS_AMIGA_PORT_JOYSTICK;
    settings->c64_port[0] = SETTINGS_C64_PORT_JOYSTICK;
    settings->c64_port[1] = SETTINGS_C64_PORT_JOYSTICK;

    settings->pad_deadzone = 1;                         // 50 %
    settings->pad_directions = SETTINGS_PAD_DIR_BOTH;
    settings->pad_triangle = SETTINGS_PAD_TRIANGLE_UP;
    settings->pad_touchpad = 1;

    settings->kb_preset = SETTINGS_KB_PRESET_ORIGINAL;
    settings->kb_help = SETTINGS_KB_HELP_INSERT;
    settings->kb_delete = SETTINGS_KB_DELETE_DELETE;
    settings->kb_iso = SETTINGS_KB_ISO_BACKSLASH;

    settings->ak_parens = SETTINGS_AK_PARENS_KEYPAD;

    settings->bt_enabled = 1;
    settings->bt_pairing = SETTINGS_BT_PAIRING_AUTO;

    settings->wifi_mode = SETTINGS_WIFI_OFF;
    settings->ota_needs_boot = 1;

    settings->wheel_enabled = 1;
    settings->display = SETTINGS_DISPLAY_STATUS;
    settings->watchdog = 2;                             // 5 s
    settings->net_scale = 2;                            // 100 %
}

static bool string_ok(char const *s, size_t size)
{
    return memchr(s, 0, size) != NULL;
}

bool settings_device_name_ok(char const *name)
{
    size_t length = strlen(name);

    if (length >= SETTINGS_DEVICE_NAME_LEN)
        return false;
    for (size_t i = 0; i < length; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && i && (i + 1 < length))))
            return false;
    }
    return true;
}

static bool remaps_ok(settings_remap_t const *remap)
{
    for (unsigned i = 0; i < SETTINGS_REMAP_SLOTS; i++) {
        if (!remap[i].source)
            continue;
        for (unsigned j = 0; j < i; j++)
            if (remap[j].source == remap[i].source)
                return false;
    }
    return true;
}

bool settings_valid(settings_t const *s)
{
    if (s == NULL)
        return false;

    for (unsigned i = 0; i < 2; i++)
        if ((s->amiga_port[i] >= SETTINGS_AMIGA_PORT_COUNT) || (s->c64_port[i] >= SETTINGS_C64_PORT_COUNT))
            return false;

    for (unsigned i = 0; i < SETTINGS_PAD_BUTTONS; i++)
        if (s->pad_buttons[i] > SETTINGS_PAD_BUTTON_MAX)
            return false;

    for (unsigned i = 0; i < SETTINGS_PAD_COMBOS; i++)
        if ((s->pad_combo[i].button > SETTINGS_PAD_COMBO_BUTTONS)
            || (s->pad_combo[i].outputs & (uint8_t)~SETTINGS_PAD_COMBO_OUTPUTS))
            return false;

    for (unsigned i = 0; i < sizeof(s->reserved); i++)
        if (s->reserved[i])
            return false;

    return (s->mode < SETTINGS_MODE_COUNT)
        && (s->pad_swap <= 1) && (s->pad_merge <= 1) && (s->pad_touchpad <= 1)
        && (s->pad_deadzone < SETTINGS_DEADZONE_COUNT)
        && (s->pad_directions < SETTINGS_PAD_DIR_COUNT)
        && (s->pad_triangle < SETTINGS_PAD_TRIANGLE_COUNT)
        && (s->kb_preset < SETTINGS_KB_PRESET_COUNT) && (s->kb_help < SETTINGS_KB_HELP_COUNT)
        && (s->kb_delete < SETTINGS_KB_DELETE_COUNT) && (s->kb_iso < SETTINGS_KB_ISO_COUNT)
        && remaps_ok(s->kb_remap)
        && (s->ak_parens < SETTINGS_AK_PARENS_COUNT) && (s->ak_cad <= 1)
        && remaps_ok(s->ak_remap)
        && (s->bt_enabled <= 1) && (s->bt_pairing < SETTINGS_BT_PAIRING_COUNT)
        && (s->wifi_mode < SETTINGS_WIFI_COUNT) && (s->ota_needs_boot <= 1)
        && (s->wheel_enabled <= 1) && (s->wheel_reverse <= 1)
        && (s->mouse_speed < SETTINGS_MOUSE_SPEED_COUNT)
        && (s->display < SETTINGS_DISPLAY_COUNT) && (s->watchdog < SETTINGS_WATCHDOG_COUNT)
        && (s->page_open <= 1) && (s->pad_profile <= SETTINGS_PAD_PROFILES)
        && (s->pad_profile_mode[0] <= SETTINGS_PAD_PROFILES) && (s->pad_profile_mode[1] <= SETTINGS_PAD_PROFILES)
        && (s->pad_profile_mode[2] <= SETTINGS_PAD_PROFILES)
        && (s->net_control <= 1) && (s->net_screen < SETTINGS_NET_SCREEN_COUNT)
        && (s->net_scale < SETTINGS_NET_SCALE_COUNT)
        && string_ok(s->wifi_ssid, sizeof(s->wifi_ssid))
        && string_ok(s->wifi_password, sizeof(s->wifi_password))
        && string_ok(s->page_password, sizeof(s->page_password))
        && string_ok(s->device_name, sizeof(s->device_name)) && settings_device_name_ok(s->device_name);
}

static uint32_t record_crc(settings_record_t const *record)
{
    uint8_t const *bytes = (uint8_t const *)record;
    size_t length = offsetof(settings_record_t, crc);
    uint32_t crc = UINT32_MAX;

    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0);
    }
    return ~crc;
}

static settings_record_t const *bank_record(unsigned bank)
{
    return (settings_record_t const *)(SETTINGS_READ_BASE + SETTINGS_FLASH_OFFSET + bank * FLASH_SECTOR_SIZE);
}

static bool record_decode(settings_record_t const *stored, settings_t *settings)
{
    settings_record_t record;

    // one copy out of flash, then everything is checked against the copy
    memcpy(&record, stored, sizeof(record));

    if ((record.magic != SETTINGS_MAGIC) || (record.schema != SETTINGS_SCHEMA)
        || (record.length != sizeof(settings_t)) || (record.crc != record_crc(&record)))
        return false;

    if (!settings_valid(&record.values))
        return false;

    *settings = record.values;
    return true;
}

void settings_init(void)
{
    settings_defaults(&active);
    current_bank = -1;
    current_sequence = 0;

    for (unsigned bank = 0; bank < 2; bank++) {
        settings_record_t const *record = bank_record(bank);
        settings_t decoded;

        if (!record_decode(record, &decoded))
            continue;

        // newest wins, across sequence number rollover
        if ((current_bank < 0) || ((int32_t)(record->sequence - current_sequence) > 0)) {
            current_bank = (int)bank;
            current_sequence = record->sequence;
            active = decoded;
        }
    }
}

settings_t const *settings_get(void)
{
    return &active;
}

uint32_t settings_generation(void)
{
    return generation;
}

char const *settings_source(void)
{
    return (current_bank < 0) ? "defaults" : "flash";
}

typedef struct {
    uint32_t offset;
    uint8_t page[FLASH_PAGE_SIZE];
} settings_write_t;

static void write_record(void *context)
{
    settings_write_t const *write = context;

    flash_range_erase(write->offset, FLASH_SECTOR_SIZE);
    flash_range_program(write->offset, write->page, FLASH_PAGE_SIZE);
}

static void publish(settings_t const *settings)
{
    // bluetooth callbacks may read settings from interrupt context: swap the
    // whole validated set in at once
    uint32_t interrupts = save_and_disable_interrupts();
    active = *settings;
    generation++;
    restore_interrupts(interrupts);
}

bool settings_save(settings_t const *given)
{
    settings_t adjusted;
    settings_t const *settings = &adjusted;
    settings_t stored_values;
    settings_write_t write;
    settings_record_t record;
    unsigned bank;
    int result;

    if (!settings_valid(given))
        return false;

    // each mode keeps its own pad profile: a change of mode brings its one back
    adjusted = *given;
    profiles_follow_mode(&adjusted, settings_get());

    // unchanged: no flash wear
    if ((current_bank >= 0) && record_decode(bank_record((unsigned)current_bank), &stored_values)
        && (memcmp(settings, &stored_values, sizeof(*settings)) == 0)) {
        publish(settings);
        return true;
    }

    bank = (current_bank == 0) ? 1u : 0u;

    memset(&record, 0, sizeof(record));
    record.magic = SETTINGS_MAGIC;
    record.schema = SETTINGS_SCHEMA;
    record.length = sizeof(settings_t);
    record.sequence = current_sequence + 1u;
    record.values = *settings;
    record.crc = record_crc(&record);

    write.offset = SETTINGS_FLASH_OFFSET + bank * FLASH_SECTOR_SIZE;
    memset(write.page, 0xff, sizeof(write.page));
    memcpy(write.page, &record, sizeof(record));

    // core1 (the mouse core) is registered as a lockout victim, so
    // flash_safe_execute() parks it in ram while xip is off
    result = flash_safe_execute(write_record, &write, 500);
    if ((result != PICO_OK) || !record_decode(bank_record(bank), &stored_values)
        || (memcmp(&stored_values, settings, sizeof(*settings)) != 0))
        return false;

    current_bank = (int)bank;
    current_sequence = record.sequence;
    publish(settings);
    return true;
}
