/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the names of the settings' values, one per value in enum order, as the oled
 * menu and the wi-fi settings page show them.
 */

#ifndef _SETTINGS_NAMES_H
#define _SETTINGS_NAMES_H

#include <stdint.h>

typedef struct {
    char const *const *names;
    uint8_t count;
} settings_names_t;

extern const settings_names_t settings_names_mode;
extern const settings_names_t settings_names_amiga_port;
extern const settings_names_t settings_names_c64_port;
extern const settings_names_t settings_names_no_yes;
extern const settings_names_t settings_names_off_on;
extern const settings_names_t settings_names_deadzone;
extern const settings_names_t settings_names_directions;
extern const settings_names_t settings_names_triangle;
extern const settings_names_t settings_names_wheel_dir;
extern const settings_names_t settings_names_page_open;
extern const settings_names_t settings_names_mouse_speed;
extern const settings_names_t settings_names_display;
extern const settings_names_t settings_names_watchdog;
extern const settings_names_t settings_names_bt_pairing;
extern const settings_names_t settings_names_wifi;
extern const settings_names_t settings_names_net_screen;
extern const settings_names_t settings_names_net_scale;
extern const settings_names_t settings_names_kb_preset;
extern const settings_names_t settings_names_kb_help;
extern const settings_names_t settings_names_kb_delete;
extern const settings_names_t settings_names_kb_iso;
extern const settings_names_t settings_names_ak_parens;
extern const settings_names_t settings_names_ak_cad;

#endif // _SETTINGS_NAMES_H
