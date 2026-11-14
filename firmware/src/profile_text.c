/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * pad profiles as text; see profile_text.h.
 */

#include "profile_text.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pad.h"
#include "web_page.h"

#define HEAD "ninepin-ux pad profile"

static char const *const buttons[SETTINGS_PAD_COMBO_BUTTONS] = {
    "west", "south", "east", "north", "l1", "r1", "l2", "r2",
    "select", "start", "l3", "r3", "home", "touchpad", "mute", "b16",
};

// in pad.h's bit order, then SETTINGS_PAD_COMBO_AUTO
static char const *const outputs[8] = { "up", "down", "left", "right", "fire1", "fire2", "fire3", "auto" };

static char const *const machines[PROFILE_FOR_COUNT] = { "any", "amiga", "c64" };
static char const *const directions[SETTINGS_PAD_DIR_COUNT] = { "both", "dpad", "stick" };
static char const *const triangles[SETTINGS_PAD_TRIANGLE_COUNT] = { "up", "autofire", "off" };
static char const *const rates[SETTINGS_PAD_AUTOFIRE_COUNT] = { "5", "8", "10", "15", "20" };

typedef struct {
    char *out;
    size_t size, length;
    bool full;
} writer_t;

static void put(writer_t *w, char const *format, ...) __attribute__((format(printf, 2, 3)));

static void put(writer_t *w, char const *format, ...)
{
    va_list args;
    int n;

    if (w->full)
        return;
    va_start(args, format);
    n = vsnprintf(w->out + w->length, w->size - w->length, format, args);
    va_end(args);
    if ((n < 0) || ((size_t)n >= w->size - w->length))
        w->full = true;
    else
        w->length += (size_t)n;
}

size_t profile_text_write(profile_t const *profile, char *out, size_t size)
{
    writer_t w = { out, size, 0, size == 0 };
    char key[8];

    put(&w, HEAD "\nname=%s\nmachine=%s\ndirections=%s\ntriangle=%s\nautofire=%s\n", profile->name,
        machines[profile->machine % PROFILE_FOR_COUNT], directions[profile->pad_directions % SETTINGS_PAD_DIR_COUNT],
        triangles[profile->pad_triangle % SETTINGS_PAD_TRIANGLE_COUNT],
        rates[profile->pad_autofire % SETTINGS_PAD_AUTOFIRE_COUNT]);

    for (unsigned i = 0; i < SETTINGS_PAD_COMBOS; i++) {
        settings_combo_t const *c = &profile->combo[i];

        if ((c->button < 1) || (c->button > SETTINGS_PAD_COMBO_BUTTONS))
            continue;
        put(&w, "combo=%s", buttons[c->button - 1]);
        for (unsigned bit = 0; bit < 8; bit++)
            if (c->outputs & (1u << bit))
                put(&w, " %s", outputs[bit]);
        if (c->key)
            put(&w, " key=%s", web_page_key_name(c->key, key));
        put(&w, "\n");
    }
    return w.full ? 0 : w.length;
}

static int lookup(char const *word, char const *const *list, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        if (strcasecmp(word, list[i]) == 0)
            return (int)i;
    return -1;
}

static bool fail(char *error, size_t size, unsigned line, char const *what)
{
    snprintf(error, size, "Line %u: %s.", line, what);
    return false;
}

static bool read_combo(char *value, settings_combo_t *combo)
{
    char *save = NULL;
    char *word = strtok_r(value, " \t", &save);
    int found;

    memset(combo, 0, sizeof(*combo));
    if (!word)
        return false;

    found = lookup(word, buttons, SETTINGS_PAD_COMBO_BUTTONS);
    if (found < 0) {
        char *end;
        unsigned long number = strtoul(word, &end, 10);

        if (*end || (number < 1) || (number > SETTINGS_PAD_COMBO_BUTTONS))
            return false;
        found = (int)number - 1;
    }
    combo->button = (uint8_t)(found + 1);

    while ((word = strtok_r(NULL, " \t", &save)) != NULL) {
        if (strncasecmp(word, "key=", 4) == 0) {
            int usage = web_page_key_usage(word + 4);

            if (usage <= 0)
                return false;
            combo->key = (uint8_t)usage;
            continue;
        }
        found = lookup(word, outputs, 8);
        if (found < 0)
            return false;
        combo->outputs |= (uint8_t)(1u << found);
    }
    return true;
}

bool profile_text_read(char const *text, size_t length, profile_t *profile, char *error, size_t error_size)
{
    unsigned line_number = 0, combos = 0;
    bool headed = false;
    size_t at = 0;

    memset(profile, 0, sizeof(*profile));
    profile->pad_directions = SETTINGS_PAD_DIR_BOTH;
    profile->pad_triangle = SETTINGS_PAD_TRIANGLE_UP;
    profile->pad_autofire = SETTINGS_PAD_AUTOFIRE_10;

    // a utf-8 byte order mark, as some editors put first
    if ((length >= 3) && (memcmp(text, "\xef\xbb\xbf", 3) == 0))
        at = 3;

    while (at < length) {
        char line[96];
        size_t end = at, used;
        char *value;
        int found;

        while ((end < length) && (text[end] != '\n'))
            end++;
        used = end - at;
        line_number++;
        if (used >= sizeof(line))
            return fail(error, error_size, line_number, "too long");
        memcpy(line, text + at, used);
        at = end + 1;

        // spaces and the cr at the end do not count
        while (used && ((line[used - 1] == '\r') || (line[used - 1] == ' ') || (line[used - 1] == '\t')))
            used--;
        line[used] = 0;
        if (!used || (line[0] == '#'))
            continue;

        if (!headed) {
            if (strcasecmp(line, HEAD) != 0)
                return fail(error, error_size, line_number, "not a pad profile (the first line says \"" HEAD "\")");
            headed = true;
            continue;
        }

        value = strchr(line, '=');
        if (!value)
            return fail(error, error_size, line_number, "not understood");
        *value++ = 0;

        if (strcasecmp(line, "name") == 0) {
            if (!profiles_name_ok(value))
                return fail(error, error_size, line_number, "a name has 1 to 12 letters, digits, spaces and - _ . +");
            snprintf(profile->name, sizeof(profile->name), "%s", value);
        } else if (strcasecmp(line, "machine") == 0) {
            if ((found = lookup(value, machines, PROFILE_FOR_COUNT)) < 0)
                return fail(error, error_size, line_number, "the machine is any, amiga or c64");
            profile->machine = (uint8_t)found;
        } else if (strcasecmp(line, "directions") == 0) {
            if ((found = lookup(value, directions, SETTINGS_PAD_DIR_COUNT)) < 0)
                return fail(error, error_size, line_number, "directions are both, dpad or stick");
            profile->pad_directions = (uint8_t)found;
        } else if (strcasecmp(line, "triangle") == 0) {
            if ((found = lookup(value, triangles, SETTINGS_PAD_TRIANGLE_COUNT)) < 0)
                return fail(error, error_size, line_number, "triangle is up, autofire or off");
            profile->pad_triangle = (uint8_t)found;
        } else if (strcasecmp(line, "autofire") == 0) {
            if ((found = lookup(value, rates, SETTINGS_PAD_AUTOFIRE_COUNT)) < 0)
                return fail(error, error_size, line_number, "autofire is 5, 8, 10, 15 or 20");
            profile->pad_autofire = (uint8_t)found;
        } else if (strcasecmp(line, "combo") == 0) {
            if (combos >= SETTINGS_PAD_COMBOS)
                return fail(error, error_size, line_number, "more combos than a profile holds");
            if (!read_combo(value, &profile->combo[combos]))
                return fail(error, error_size, line_number, "no such button, output or key");
            combos++;
        } else {
            return fail(error, error_size, line_number, "not understood");
        }
    }

    if (!headed) {
        snprintf(error, error_size, "That is not a pad profile file.");
        return false;
    }
    if (!profile->name[0]) {
        snprintf(error, error_size, "The profile has no name.");
        return false;
    }
    return true;
}
