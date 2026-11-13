/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the settings page's html; see web_page.h. every choice is a <select> whose
 * values are the setting's numbers, named as in the oled menu
 * (settings_names.c); the remap slots are listed, not edited. the pad combos
 * and the page password are set here only.
 */

#include "web_page.h"
#include "config.h"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/bootrom.h"

#include "bt_hid.h"
#include "build_stamp.h"
#include "http_server.h"
#include "keyboard_map.h"
#include "ota.h"
#include "pad.h"
#include "profiles.h"
#include "settings_names.h"
#include "util/run_mode.h"
#include "web_auth.h"
#include "wifi.h"

//--------------------------------------------------------------------+
// the choices, in page order
//--------------------------------------------------------------------+

typedef struct {
    char const *group;              // non-NULL: a new group (a details box) starts here
    char const *key;
    char const *label;
    uint16_t offset;
    settings_names_t const *names;
} web_choice_t;

#define CHOICE(group, key, label, field, names) \
    { group, key, label, offsetof(settings_t, field), &settings_names_##names }

static const web_choice_t choices[] = {
    CHOICE("Mode", "mode", "Mode", mode, mode),
    CHOICE("Ports in Amiga mode", "amiga_port1", "Port 1", amiga_port[0], amiga_port),
    CHOICE(NULL, "amiga_port2", "Port 2", amiga_port[1], amiga_port),
    CHOICE("Ports in C64 mode", "c64_port1", "Port 1", c64_port[0], c64_port),
    CHOICE(NULL, "c64_port2", "Port 2", c64_port[1], c64_port),
    CHOICE("Pads", "pad_swap", "Swap players", pad_swap, no_yes),
    CHOICE(NULL, "pad_merge", "Merge all pads", pad_merge, no_yes),
    CHOICE(NULL, "pad_directions", "Pad directions", pad_directions, directions),
    CHOICE(NULL, "pad_deadzone", "Stick deadzone", pad_deadzone, deadzone),
    CHOICE(NULL, "pad_triangle", "Triangle button", pad_triangle, triangle),
    CHOICE(NULL, "pad_touchpad", "Touchpad mouse", pad_touchpad, off_on),
    CHOICE("Mouse", "wheel_enabled", "Mouse wheel", wheel_enabled, off_on),
    CHOICE(NULL, "wheel_reverse", "Wheel direction", wheel_reverse, wheel_dir),
    CHOICE(NULL, "mouse_speed", "Mouse speed", mouse_speed, mouse_speed),
    CHOICE("PC keyboard to the Amiga", "kb_preset", "Keyboard preset", kb_preset, kb_preset),
    CHOICE(NULL, "kb_help", "Help key", kb_help, kb_help),
    CHOICE(NULL, "kb_delete", "Del key", kb_delete, kb_delete),
    CHOICE(NULL, "kb_iso", "ISO key", kb_iso, kb_iso),
    CHOICE("A4000 keyboard to USB", "ak_parens", "Numpad ( )", ak_parens, ak_parens),
    CHOICE(NULL, "ak_cad", "Ctrl-Amiga-Amiga", ak_cad, ak_cad),
    CHOICE("Bluetooth", "bt_enabled", "Bluetooth", bt_enabled, off_on),
    CHOICE(NULL, "bt_pairing", "Pairing", bt_pairing, bt_pairing),
    CHOICE("Board", "display", "Display", display, display),
    CHOICE(NULL, "watchdog", "Watchdog", watchdog, watchdog),
    CHOICE("Wi-Fi", "wifi_mode", "Wi-Fi", wifi_mode, wifi),
    CHOICE("Network control", "net_control", "Network control", net_control, off_on),
    CHOICE(NULL, "net_screen", "Target screen (DE9 mouse)", net_screen, net_screen),
    CHOICE(NULL, "net_scale", "Mouse counts per pixel", net_scale, net_scale),
};

#define CHOICE_COUNT (sizeof(choices) / sizeof(choices[0]))

static char boot_slot[16] = "?";

void web_page_init(void)
{
#if !PICO_RP2040
    boot_info_t boot;

    if (rom_get_boot_info(&boot))
        snprintf(boot_slot, sizeof(boot_slot), "%s", (boot.partition == 0) ? "A" : (boot.partition == 1) ? "B"
            : "no partitions");
#endif
}

//--------------------------------------------------------------------+
// the pad combos: buttons and keys by name
//--------------------------------------------------------------------+

// a pad's own buttons; the names are the playstation pads' (ds5.c, bt_ds4.c)
static char const *const combo_buttons[SETTINGS_PAD_COMBO_BUTTONS + 1] = {
    "-", "1 square", "2 cross", "3 circle", "4 triangle", "5 L1", "6 R1", "7 L2", "8 R2",
    "9 create", "10 options", "11 L3", "12 R3", "13 PS", "14 touchpad", "15 mute", "16",
};

// the form's fields for a combo's outputs: c<slot><letter>, in pad.h's bit order
static char const combo_letters[] = "udlrfgh";
static char const *const combo_heads[] = { "&uarr;", "&darr;", "&larr;", "&rarr;", "F1", "F2", "F3" };

typedef struct {
    char const *name;
    uint8_t usage;
} web_key_t;

// hid keyboard usages; letters, digits, f1..f12 and 0xNN are worked out
static const web_key_t keys[] = {
    { "space", 0x2c }, { "enter", 0x28 }, { "esc", 0x29 }, { "backspace", 0x2a }, { "tab", 0x2b },
    { "up", 0x52 }, { "down", 0x51 }, { "left", 0x50 }, { "right", 0x4f },
    { "del", 0x4c }, { "insert", 0x49 }, { "home", 0x4a }, { "end", 0x4d }, { "pgup", 0x4b }, { "pgdn", 0x4e },
    { "lshift", 0xe1 }, { "rshift", 0xe5 }, { "lctrl", 0xe0 }, { "lalt", 0xe2 }, { "ralt", 0xe6 },
    { "lgui", 0xe3 }, { "rgui", 0xe7 }, { "capslock", 0x39 },
    { "minus", 0x2d }, { "equals", 0x2e }, { "comma", 0x36 }, { "period", 0x37 }, { "slash", 0x38 },
};

#define KEY_COUNT (sizeof(keys) / sizeof(keys[0]))

static char const *key_name(uint8_t usage, char buf[8])
{
    for (size_t i = 0; i < KEY_COUNT; i++)
        if (keys[i].usage == usage)
            return keys[i].name;

    if (!usage)
        buf[0] = 0;
    else if (usage >= 0x04 && usage <= 0x1d)
        snprintf(buf, 8, "%c", 'a' + (usage - 0x04));
    else if (usage >= 0x1e && usage <= 0x27)
        snprintf(buf, 8, "%c", (usage == 0x27) ? '0' : '1' + (usage - 0x1e));
    else if (usage >= 0x3a && usage <= 0x45)
        snprintf(buf, 8, "f%u", usage - 0x3a + 1u);
    else
        snprintf(buf, 8, "0x%02x", usage);
    return buf;
}

/**
 * @return the usage a key name stands for, 0 for none (empty), -1 if there is no such key
 */
static int key_usage(char const *name)
{
    size_t length = strlen(name);
    char *end;
    unsigned long number;

    if (!length)
        return 0;
    for (size_t i = 0; i < KEY_COUNT; i++)
        if (strcasecmp(name, keys[i].name) == 0)
            return keys[i].usage;

    if (length == 1 && isalpha((unsigned char)name[0]))
        return 0x04 + (tolower((unsigned char)name[0]) - 'a');
    if (length == 1 && isdigit((unsigned char)name[0]))
        return (name[0] == '0') ? 0x27 : 0x1e + (name[0] - '1');
    if ((name[0] == 'f' || name[0] == 'F') && isdigit((unsigned char)name[1])) {
        number = strtoul(name + 1, &end, 10);
        return (!*end && number >= 1 && number <= 12) ? (int)(0x3a + number - 1) : -1;
    }
    if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X') && name[2]) {
        number = strtoul(name + 2, &end, 16);
        return (!*end && number >= 0x04 && number <= 0xe7) ? (int)number : -1;
    }
    return -1;
}

//--------------------------------------------------------------------+
// writing html
//--------------------------------------------------------------------+

typedef struct {
    char *buf;
    size_t size, len;
    bool full;
} writer_t;

static void put(writer_t *w, char const *fmt, ...) __attribute__((format(printf, 2, 3)));

static void put(writer_t *w, char const *fmt, ...)
{
    va_list args;
    int n;

    if (w->full)
        return;
    va_start(args, fmt);
    n = vsnprintf(w->buf + w->len, w->size - w->len, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= w->size - w->len) {
        w->full = true;
        return;
    }
    w->len += (size_t)n;
}

/**
 * text into html, escaped
 */
static void put_text(writer_t *w, char const *text, size_t max)
{
    for (size_t i = 0; i < max && text[i] && !w->full; i++) {
        switch (text[i]) {
            case '&':  put(w, "&amp;"); break;
            case '<':  put(w, "&lt;"); break;
            case '>':  put(w, "&gt;"); break;
            case '"':  put(w, "&quot;"); break;
            case '\'': put(w, "&#39;"); break;
            default:   put(w, "%c", text[i]); break;
        }
    }
}

static void put_head(writer_t *w, char const *title)
{
    put(w, "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\"><title>");
    put_text(w, title, 64);
    put(w, "</title><style>"
        "body{font-family:sans-serif;max-width:36em;margin:1em auto;padding:0 1em;line-height:1.4}"
        "details{margin:.8em 0;border:1px solid #999;padding:.3em .8em}"
        "summary{font-weight:bold;cursor:pointer}"
        "label{display:flex;flex-wrap:wrap;justify-content:space-between;gap:.2em 1em;margin:.3em 0}"
        "td,th{padding:.1em 1em .1em 0;vertical-align:top;text-align:left}"
        ".c{table-layout:fixed;width:100%%}.c td,.c th{padding-right:.2em}"
        ".c select,.c [list]{width:100%%;box-sizing:border-box}.c input{margin-left:0}.c th{font-size:.8em}"
        "small{color:#666}"
        "</style></head><body>");
}

static void put_status_row(writer_t *w, char const *name, char const *value, size_t max)
{
    put(w, "<tr><td>%s<td>", name);
    put_text(w, value, max);
}

static void put_remaps(writer_t *w, settings_remap_t const *remap, bool from_amiga)
{
    bool any = false;

    put(w, "<p><small>Remaps (set on the board): ");
    for (unsigned i = 0; i < SETTINGS_REMAP_SLOTS; i++) {
        if (!remap[i].source)
            continue;
        if (any)
            put(w, ", ");
        any = true;
        if (from_amiga) {
            put_text(w, keyboard_map_amiga_name((uint8_t)(remap[i].source - 1)), 24);
            put(w, " &gt; ");
            put_text(w, keyboard_map_hid_name(remap[i].target), 24);
        } else {
            put_text(w, keyboard_map_hid_name(remap[i].source), 24);
            put(w, " &gt; ");
            put_text(w, keyboard_map_amiga_name(remap[i].target), 24);
        }
    }
    put(w, "%s</small>", any ? "" : "none");
}

/**
 * the wi-fi group continues: the board's name, the network to join
 */
static void put_wifi(writer_t *w, settings_t const *s)
{
    put(w, "<label>Device name <input name=device_name maxlength=15 pattern=\"[A-Za-z0-9\\-]*\" value=\"");
    put_text(w, s->device_name, sizeof(s->device_name));
    put(w, "\" placeholder=\"");
    put_text(w, wifi_default_name(), 32);
    put(w, "\"></label>");
    put(w, "<label>Network (join) <input name=wifi_ssid maxlength=32 value=\"");
    put_text(w, s->wifi_ssid, sizeof(s->wifi_ssid));
    put(w, "\"></label>"
        "<label>Its password <input type=password name=wifi_password maxlength=63 autocomplete=off "
        "placeholder=\"%s\"></label>"
        "<label>Open network, no password <input type=checkbox name=wifi_open value=1></label>"
        "<p><small>The device name is the access point's name and the address in join mode (letters, "
        "digits and -, up to 15; empty: the board's own). Join mode answers at http://",
        s->wifi_password[0] ? "(stored; empty keeps it)" : "(none)");
    put_text(w, wifi_name(), 32);
    put(w, ".local/ and asks for the page password (below). "
        "The access point is open and switches off after 15 minutes without a client.</small>");
}

/**
 * the pad profiles: which one is loaded, load / delete one, save the form's as one
 */
static void put_profiles(writer_t *w, settings_t const *s)
{
    profile_t const *loaded = s->pad_profile ? profiles_get(s->pad_profile - 1u) : NULL;

    put(w, "<p>Profile: ");
    if (loaded) {
        put(w, "<b>");
        put_text(w, loaded->name, PROFILE_NAME_LEN);
        put(w, "</b>%s", profiles_matches(loaded, s) ? "" : " (changed since)");
    } else {
        put(w, "none");
    }

    if (profiles_count_shown(s->mode)) {
        put(w, "<label>Saved profiles <span><select name=profile>");
        for (unsigned i = 0; i < PROFILES_MAX; i++) {
            profile_t const *p = profiles_get(i);

            if (!p || !profiles_shown(p, s->mode))
                continue;
            put(w, "<option value=%u%s>", i + 1, (p == loaded) ? " selected" : "");
            put_text(w, p->name, PROFILE_NAME_LEN);
        }
        put(w, "</select> <button formaction=/profile-load>Load</button>"
            " <button formaction=/profile-delete>Delete</button></span></label>");
    }
    put(w, "<label>Save as <span><input name=profile_name size=12 maxlength=12 value=\"");
    if (loaded)
        put_text(w, loaded->name, PROFILE_NAME_LEN);
    put(w, "\"> <select name=profile_for>");
    {
        static char const *const names[PROFILE_FOR_COUNT] = { "any machine", "Amiga", "C64" };
        uint8_t machine = loaded ? loaded->machine : profiles_machine_of(s->mode);

        for (unsigned m = 0; m < PROFILE_FOR_COUNT; m++)
            put(w, "<option value=%u%s>%s", m, (m == machine) ? " selected" : "", names[m]);
    }
    put(w, "</select> <button formaction=/profile-save>Save as profile</button></span></label>");
}

static void put_combos(writer_t *w, settings_t const *s)
{
    char buf[8];

    put(w, "<details open><summary>Pad button combos</summary><input type=hidden name=combos value=1>"
        "<table class=c><tr><th style=width:30%%>Button");
    for (size_t o = 0; o < sizeof(combo_letters) - 1; o++)
        put(w, "<th>%s", combo_heads[o]);
    put(w, "<th style=width:22%%>Key");

    for (unsigned i = 0; i < SETTINGS_PAD_COMBOS; i++) {
        settings_combo_t const *c = &s->pad_combo[i];

        put(w, "<tr><td><select name=c%ub>", i + 1);
        for (unsigned b = 0; b <= SETTINGS_PAD_COMBO_BUTTONS; b++)
            put(w, "<option value=%u%s>%s", b, (b == c->button) ? " selected" : "", combo_buttons[b]);
        put(w, "</select>");
        for (size_t o = 0; o < sizeof(combo_letters) - 1; o++)
            put(w, "<td><input type=checkbox name=c%u%c value=1%s>", i + 1, combo_letters[o],
                (c->outputs & (1u << o)) ? " checked" : "");
        put(w, "<td><input name=c%uk size=7 maxlength=9 list=keys value=\"%s\">", i + 1, key_name(c->key, buf));
    }

    put(w, "</table><datalist id=keys>");
    for (size_t i = 0; i < KEY_COUNT; i++)
        put(w, "<option>%s", keys[i].name);
    put(w, "</datalist>");
    put_profiles(w, s);
    put(w, "<p><small>Whilst the pad button is held, the ticked directions and fire buttons are "
        "held together (a direction wins over the stick), and the key is down on the keyboard. The button "
        "then no longer does what it did. Button names are a PlayStation pad's; on other pads, hold a "
        "button and reload: Status shows its number. Keys: a letter, a digit, f1 to f12, or a name from "
        "the list. A profile keeps the combos, Pad directions and Triangle button under a name: Save as "
        "stores the whole form and the profile, Load puts a profile back. A profile for the Amiga or "
        "the C64 is offered in that mode only, and each mode comes back with the profile it had last."
        "</small></details>");
}

//--------------------------------------------------------------------+
// the page
//--------------------------------------------------------------------+

size_t web_page_render(char *out, size_t size)
{
    writer_t w = { out, size, 0, false };
    settings_t const *s = settings_get();
    char line[48];

    put_head(&w, wifi_name());
    put(&w, "<h1>");
    put_text(&w, wifi_name(), 32);
    put(&w, "</h1>");

    // status
    put(&w, "<h2>Status</h2><table>");
    put_status_row(&w, "Mode", run_mode_name(run_mode_get()), 16);
    snprintf(line, sizeof(line), "%s, slot %s%s", ahp_build_stamp, boot_slot, ota_trial() ? ", on trial" : "");
    put_status_row(&w, "Firmware", line, sizeof(line));
    put_status_row(&w, "Update", ota_status(), 48);
    for (uint8_t player = 0; player < 2; player++) {
        pad_player_t const *p = pads_player(player);

        put(&w, "<tr><td>Player %u<td>", player + 1);
        if (p && p->active) {
            put_text(&w, p->name, PAD_NAME_LEN - 1);
            // the numbers the pad combos go by
            if (p->input.buttons) {
                put(&w, ", holding button");
                for (unsigned b = 0; b < 16; b++)
                    if (p->input.buttons & (1u << b))
                        put(&w, " %u", b + 1);
            }
        } else {
            put(&w, "-");
        }
    }
    put_status_row(&w, "Bluetooth", bt_hid_status(), 32);
    put_status_row(&w, "Wi-Fi", wifi_status(), 48);
    put_status_row(&w, "Last save here", wifi_page_result(), 64);
    put(&w, "</table>");

    // settings
    // the form's first button is what Enter in a field presses: the plain save,
    // not a profile button further down
    put(&w, "<h2>Settings</h2><form method=post action=/settings>"
        "<input type=hidden name=t value=%s>"
        "<button tabindex=-1 style=\"position:absolute;left:-99em\">Save</button>", http_server_token());

    for (size_t i = 0; i < CHOICE_COUNT; i++) {
        web_choice_t const *c = &choices[i];
        uint8_t value = *((uint8_t const *)s + c->offset);

        if (c->group) {
            // what is changed most stays open: the mode, its ports, the pads
            bool open = (strcmp(c->key, "mode") == 0) || (strcmp(c->key, "pad_swap") == 0)
                || ((strcmp(c->key, "amiga_port1") == 0) && (s->mode == SETTINGS_MODE_AMIGA))
                || ((strcmp(c->key, "c64_port1") == 0) && (s->mode == SETTINGS_MODE_C64));

            if (i)
                put(&w, "</details>");
            // the combos follow the pads
            if (strcmp(c->key, "wheel_enabled") == 0)
                put_combos(&w, s);
            put(&w, "<details%s><summary>%s</summary>", open ? " open" : "", c->group);
        }

        put(&w, "<label>%s <select name=%s>", c->label, c->key);
        for (uint8_t v = 0; v < c->names->count; v++) {
            put(&w, "<option value=%u%s>", v, (v == value) ? " selected" : "");
            put_text(&w, c->names->names[v], 24);
        }
        put(&w, "</select></label>");

        // read-only extras at the end of their groups
        if (strcmp(c->key, "kb_iso") == 0)
            put_remaps(&w, s->kb_remap, false);
        else if (strcmp(c->key, "ak_cad") == 0)
            put_remaps(&w, s->ak_remap, true);
        else if (strcmp(c->key, "wifi_mode") == 0)
            put_wifi(&w, s);
#ifdef ENABLE_BLUETOOTH_HID
        else if (strcmp(c->key, "bt_pairing") == 0)
            put(&w, "<label>New device <button formaction=/bt-pair>Pair (2 min)</button></label>"
                "<p><small>With a pad in use the board searches for another only for 30 s after that "
                "pad connected. Pair opens a 2 minute search at any time; then put the device in its "
                "pairing mode: DualSense Create + PS, DualShock 4 Share + PS.</small>");
#endif
    }

    put(&w, "</details><p><button>Save</button></form>");

    // the page password
    put(&w, "<h2>Page password</h2><form method=post action=/password>"
        "<input type=hidden name=t value=%s>"
        "<input name=username autocomplete=username value=\"", http_server_token());
    put_text(&w, wifi_name(), 32);
    put(&w, "\" hidden>"
        "<label>New password <input type=password name=new maxlength=16 autocomplete=new-password "
        "placeholder=\"(empty keeps it)\"></label>"
        "<label>Again <input type=password name=again maxlength=16 autocomplete=new-password></label>"
        "<label>In join mode <select name=open>"
        "<option value=0%s>ask for the password"
        "<option value=1%s>no password"
        "</select></label>"
        "<p><small>%u to 16 characters. Without a password everyone on your network can change these "
        "settings and, with network control on, type on the machine; a firmware update then always waits "
        "for a BOOT press. The board menu shows the password (Show page password), makes a new one and "
        "switches it back on.</small>"
        "<p><button>Change</button></form>",
        s->page_open ? "" : " selected", s->page_open ? " selected" : "", WEB_AUTH_PASSWORD_MIN);
    if (wifi_page_needs_password())
        put(&w, "<form method=post action=/logout><input type=hidden name=t value=%s>"
            "<p><button>Log out</button></form>", http_server_token());

    // firmware
    put(&w, "<h2>Firmware update</h2>"
        "<form method=post action=\"/update?t=%s\" enctype=multipart/form-data>"
        "<input type=file name=firmware accept=.uf2> <button>Upload</button></form>"
        "<p><small>Updates need a BOOT press: %s (set on the board only; always yes without a page "
        "password).</small>",
        http_server_token(),
        (s->ota_needs_boot || (s->page_open && s->wifi_mode == SETTINGS_WIFI_JOIN)) ? "yes" : "no");

    put(&w, "</body></html>");

    return w.full ? 0 : w.len;
}

size_t web_page_message(char *out, size_t size, char const *title, char const *text)
{
    writer_t w = { out, size, 0, false };

    put_head(&w, title);
    put(&w, "<h1>");
    put_text(&w, title, 64);
    put(&w, "</h1><p>");
    put_text(&w, text, 512);
    put(&w, "<p><a href=/>Back to the settings</a></body></html>");

    if (w.full) {
        // keep what fits, closed off
        w.len = size - 1;
        out[w.len] = 0;
    }
    return w.len;
}

size_t web_page_login(char *out, size_t size, char const *note)
{
    writer_t w = { out, size, 0, false };

    put_head(&w, wifi_name());
    put(&w, "<h1>");
    put_text(&w, wifi_name(), 32);
    put(&w, "</h1>");
    if (note) {
        put(&w, "<p><b>");
        put_text(&w, note, 96);
        put(&w, "</b>");
    }
    put(&w, "<form method=post action=/login><label>Board <input name=username autocomplete=username value=\"");
    put_text(&w, wifi_name(), 32);
    put(&w, "\" readonly></label>"
        "<label>Page password <input type=password name=password maxlength=16 "
        "autocomplete=current-password autofocus required></label>"
        "<p><button>Log in</button></form>"
        "<p><small>The board shows it: menu, Show page password.</small></body></html>");

    return w.full ? 0 : w.len;
}

//--------------------------------------------------------------------+
// the form
//--------------------------------------------------------------------+

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * x-www-form-urlencoded, in place: '+' is a space, %xx a byte
 */
static void url_decode(char *text)
{
    char *out = text;

    for (char *in = text; *in; in++) {
        if (*in == '+') {
            *out++ = ' ';
        } else if (*in == '%' && hex_value(in[1]) >= 0 && hex_value(in[2]) >= 0) {
            *out++ = (char)(hex_value(in[1]) * 16 + hex_value(in[2]));
            in += 2;
        } else {
            *out++ = *in;
        }
    }
    *out = 0;
}

bool web_page_field(char const *body, char const *name, char *out, size_t size)
{
    size_t name_len = strlen(name);

    while (body && *body) {
        char const *end = strchr(body, '&');
        size_t len = end ? (size_t)(end - body) : strlen(body);

        if ((len > name_len) && strncmp(body, name, name_len) == 0 && body[name_len] == '=') {
            size_t value_len = len - name_len - 1;

            if (value_len >= size)
                return false;
            memcpy(out, body + name_len + 1, value_len);
            out[value_len] = 0;
            url_decode(out);
            return true;
        }
        body = end ? end + 1 : NULL;
    }
    return false;
}

/**
 * one field of a pad combo, "c<slot><letter>"
 *
 * @return false with error filled in if its value will not do
 */
static bool parse_combo(char const *pair, char const *value, settings_t *settings, char *error, size_t error_size)
{
    unsigned slot = (unsigned)(pair[1] - '1');
    settings_combo_t *c = &settings->pad_combo[slot];
    char const *letter = strchr(combo_letters, pair[2]);

    if (pair[2] == 'b') {
        char *end;
        unsigned long number = strtoul(value, &end, 10);

        if (!*value || *end || number > SETTINGS_PAD_COMBO_BUTTONS) {
            snprintf(error, error_size, "Combo %u: no such pad button.", slot + 1);
            return false;
        }
        c->button = (uint8_t)number;
    } else if (pair[2] == 'k') {
        int usage;

        // spaces around a typed name do not matter
        while (*value == ' ')
            value++;
        usage = key_usage(value);
        if (usage < 0) {
            snprintf(error, error_size, "Combo %u: no such key.", slot + 1);
            return false;
        }
        c->key = (uint8_t)usage;
    } else if (letter && strcmp(value, "1") == 0) {
        c->outputs |= (uint8_t)(1u << (letter - combo_letters));
    }
    return true;
}

bool web_page_parse(char *body, settings_t *settings, char *error, size_t error_size)
{
    settings_t const *current = settings_get();
    bool token_ok = false;
    bool open_network = false;
    char *password = NULL;
    char *pair = body;

    while (pair && *pair) {
        char *next = strchr(pair, '&');
        char *value;

        if (next)
            *next++ = 0;
        value = strchr(pair, '=');
        if (value) {
            *value++ = 0;
            url_decode(pair);
            url_decode(value);

            if (strcmp(pair, "t") == 0) {
                token_ok = strcmp(value, http_server_token()) == 0;
            } else if (strcmp(pair, "wifi_ssid") == 0) {
                if (strlen(value) >= sizeof(settings->wifi_ssid)) {
                    snprintf(error, error_size, "The network name is longer than 32 characters.");
                    return false;
                }
                memset(settings->wifi_ssid, 0, sizeof(settings->wifi_ssid));
                strcpy(settings->wifi_ssid, value);
            } else if (strcmp(pair, "device_name") == 0) {
                for (char *c = value; *c; c++)
                    *c = (char)tolower((unsigned char)*c);
                if (!settings_device_name_ok(value)) {
                    snprintf(error, error_size, "A device name has up to 15 letters, digits and -, "
                        "and no - at either end.");
                    return false;
                }
                memset(settings->device_name, 0, sizeof(settings->device_name));
                strcpy(settings->device_name, value);
            } else if (strcmp(pair, "wifi_password") == 0) {
                password = value;
            } else if (strcmp(pair, "wifi_open") == 0) {
                open_network = strcmp(value, "1") == 0;
            } else if (strcmp(pair, "combos") == 0) {
                // the combos follow, whole: a box not ticked is not sent
                memset(settings->pad_combo, 0, sizeof(settings->pad_combo));
            } else if (pair[0] == 'c' && pair[1] >= '1' && pair[1] < '1' + SETTINGS_PAD_COMBOS && pair[2] && !pair[3]) {
                if (!parse_combo(pair, value, settings, error, error_size))
                    return false;
            } else {
                for (size_t i = 0; i < CHOICE_COUNT; i++) {
                    char *end;
                    unsigned long number;

                    if (strcmp(pair, choices[i].key) != 0)
                        continue;
                    number = strtoul(value, &end, 10);
                    if (!*value || *end || number >= choices[i].names->count) {
                        snprintf(error, error_size, "%s: no such value.", choices[i].label);
                        return false;
                    }
                    *((uint8_t *)settings + choices[i].offset) = (uint8_t)number;
                    break;
                }
            }
        }
        pair = next;
    }

    if (!token_ok) {
        snprintf(error, error_size, "The page is out of date: reload it and try again.");
        return false;
    }

    if (open_network) {
        memset(settings->wifi_password, 0, sizeof(settings->wifi_password));
    } else if (password && *password) {
        size_t length = strlen(password);

        // a wpa2 passphrase
        if (length < 8 || length >= sizeof(settings->wifi_password)) {
            snprintf(error, error_size, "A Wi-Fi password has 8 to 63 characters.");
            return false;
        }
        memset(settings->wifi_password, 0, sizeof(settings->wifi_password));
        strcpy(settings->wifi_password, password);
    }

    // a combo without its pad button is an empty slot
    for (unsigned i = 0; i < SETTINGS_PAD_COMBOS; i++)
        if (!settings->pad_combo[i].button)
            memset(&settings->pad_combo[i], 0, sizeof(settings->pad_combo[i]));

    // board only; the page password has its own form
    settings->ota_needs_boot = current->ota_needs_boot;
    settings->page_open = current->page_open;
    memcpy(settings->page_password, current->page_password, sizeof(settings->page_password));

    if (!settings_valid(settings)) {
        snprintf(error, error_size, "Those settings do not go together.");
        return false;
    }
    return true;
}
