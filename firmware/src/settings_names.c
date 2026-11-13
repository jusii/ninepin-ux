/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the names of the settings' values; see settings_names.h. the counts are
 * checked against settings.h, so a value added there needs its name here.
 */

#include "settings_names.h"

#include "settings.h"

#define NAMES(id, expected, ...) \
    static char const *const id##_list[] = { __VA_ARGS__ }; \
    _Static_assert(sizeof(id##_list) / sizeof(id##_list[0]) == (expected), #id " does not match settings.h"); \
    const settings_names_t settings_names_##id = { id##_list, (uint8_t)(expected) }

NAMES(mode,         SETTINGS_MODE_COUNT,        "amiga", "c64", "usb");
NAMES(amiga_port,   SETTINGS_AMIGA_PORT_COUNT,  "mouse", "joystick", "cd32 pad", "off");
NAMES(c64_port,     SETTINGS_C64_PORT_COUNT,    "joystick", "1351 mouse", "off");
NAMES(no_yes,       2,                          "no", "yes");
NAMES(off_on,       2,                          "off", "on");
NAMES(deadzone,     SETTINGS_DEADZONE_COUNT,    "25%", "50%", "75%");
NAMES(directions,   SETTINGS_PAD_DIR_COUNT,     "d-pad + stick", "d-pad", "stick");
NAMES(triangle,     SETTINGS_PAD_TRIANGLE_COUNT, "up (jump)", "autofire", "off");
NAMES(wheel_dir,    2,                          "normal", "reversed");
NAMES(page_open,    2,                          "asked for", "off");
NAMES(mouse_speed,  SETTINGS_MOUSE_SPEED_COUNT, "300 us", "200 us", "150 us", "100 us");
NAMES(display,      SETTINGS_DISPLAY_COUNT,     "status", "hid diag", "mouse diag", "off");
NAMES(watchdog,     SETTINGS_WATCHDOG_COUNT,    "off", "2 s", "5 s");
NAMES(bt_pairing,   SETTINGS_BT_PAIRING_COUNT,  "automatic", "paired only");
NAMES(wifi,         SETTINGS_WIFI_COUNT,        "off", "access point", "join network");
NAMES(net_screen,   SETTINGS_NET_SCREEN_COUNT,  "640x256", "640x512", "640x200", "640x400",
                                                "800x600", "1024x768", "1280x1024", "1920x1080");
NAMES(net_scale,    SETTINGS_NET_SCALE_COUNT,   "50%", "75%", "100%", "150%", "200%");
NAMES(kb_preset,    SETTINGS_KB_PRESET_COUNT,   "original", "Y<->Z", "A<->Q W<->Z");
NAMES(kb_help,      SETTINGS_KB_HELP_COUNT,     "insert", "home", "page up", "F11", "none");
NAMES(kb_delete,    SETTINGS_KB_DELETE_COUNT,   "delete", "backspace", "none");
NAMES(kb_iso,       SETTINGS_KB_ISO_COUNT,      "backslash", "amiga iso key");
NAMES(ak_parens,    SETTINGS_AK_PARENS_COUNT,   "keypad keys", "US/UK", "Nordic/DE", "French");
NAMES(ak_cad,       2,                          "nothing", "ctrl-alt-del");
