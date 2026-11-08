/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the settings page's html; see web_page.h. every choice is a <select> whose
 * values are the setting's numbers, named as in the oled menu
 * (settings_names.c); the remap slots are listed, not edited.
 */

#include "web_page.h"
#include "config.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/bootrom.h"

#include "bt_hid.h"
#include "http_server.h"
#include "keyboard_map.h"
#include "pad.h"
#include "settings_names.h"
#include "util/run_mode.h"
#include "wifi.h"

//--------------------------------------------------------------------+
// the choices, in page order
//--------------------------------------------------------------------+

typedef struct {
    char const *group;              // non-NULL: a new fieldset starts here
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
        "fieldset{margin:.8em 0;border:1px solid #999}"
        "label{display:flex;justify-content:space-between;gap:1em;margin:.3em 0}"
        "td{padding:.1em 1em .1em 0;vertical-align:top}"
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
    snprintf(line, sizeof(line), "%s %s, slot %s", __DATE__, __TIME__, boot_slot);
    put_status_row(&w, "Firmware", line, sizeof(line));
    for (uint8_t player = 0; player < 2; player++) {
        pad_player_t const *p = pads_player(player);

        put(&w, "<tr><td>Player %u<td>", player + 1);
        if (p && p->active)
            put_text(&w, p->name, PAD_NAME_LEN - 1);
        else
            put(&w, "-");
    }
    put_status_row(&w, "Bluetooth", bt_hid_status(), 32);
    put_status_row(&w, "Wi-Fi", wifi_status(), 48);
    put_status_row(&w, "Last save here", wifi_page_result(), 64);
    put(&w, "</table>");

    // settings
    put(&w, "<h2>Settings</h2><form method=post action=/settings>"
        "<input type=hidden name=t value=%s>", http_server_token());

    for (size_t i = 0; i < CHOICE_COUNT; i++) {
        web_choice_t const *c = &choices[i];
        uint8_t value = *((uint8_t const *)s + c->offset);

        if (c->group) {
            if (i)
                put(&w, "</fieldset>");
            put(&w, "<fieldset><legend>%s</legend>", c->group);
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
    }

    // the wi-fi group continues: the network to join
    put(&w, "<label>Network (join) <input name=wifi_ssid maxlength=32 value=\"");
    put_text(&w, s->wifi_ssid, sizeof(s->wifi_ssid));
    put(&w, "\"></label>"
        "<label>Its password <input type=password name=wifi_password maxlength=63 autocomplete=off "
        "placeholder=\"%s\"></label>"
        "<label>Open network, no password <input type=checkbox name=wifi_open value=1></label>"
        "<p><small>Join mode answers at http://", s->wifi_password[0] ? "(stored; empty keeps it)" : "(none)");
    put_text(&w, wifi_name(), 32);
    put(&w, ".local/ and asks for the page password (board menu: Show page password; any user name). "
        "The access point is open and switches off after 15 minutes without a client.</small>"
        "</fieldset><p><button>Save</button></form>");

    // firmware
    put(&w, "<h2>Firmware update</h2>"
        "<form method=post action=\"/update?t=%s\" enctype=multipart/form-data>"
        "<input type=file name=firmware accept=.uf2> <button>Upload</button></form>"
        "<p><small>Updates need a BOOT press: %s (set on the board only).</small>",
        http_server_token(), s->ota_needs_boot ? "yes" : "no");

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
            } else if (strcmp(pair, "wifi_password") == 0) {
                password = value;
            } else if (strcmp(pair, "wifi_open") == 0) {
                open_network = strcmp(value, "1") == 0;
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

    // board only, and the page password is the board's to set
    settings->ota_needs_boot = current->ota_needs_boot;
    memcpy(settings->page_password, current->page_password, sizeof(settings->page_password));

    if (!settings_valid(settings)) {
        snprintf(error, error_size, "Those settings do not go together.");
        return false;
    }
    return true;
}
