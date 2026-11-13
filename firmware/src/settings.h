/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * persistent settings.
 *
 * the flash mechanics (two alternating sectors, a crc32 per record, the newest
 * valid record wins, flash-safe writes with core1 locked out) follow stefan
 * reinauer's settings.c on the features branch of reinauer/amigahid-pico; the
 * record layout below is this tree's own (schema 1).
 *
 * one settings_t covers every option the rev7 firmware batch introduces, so the
 * schema does not churn while the features land. fields are fixed-width bytes,
 * never compiler-sized enums, and the struct is padded to a fixed size: a change
 * to the layout needs a new schema number.
 */

#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

// where keyboards, mice and pads go (Rev7 Firmware Batch, "modes")
enum settings_mode {
    SETTINGS_MODE_AMIGA = 0,
    SETTINGS_MODE_C64,
    SETTINGS_MODE_USB,
    SETTINGS_MODE_COUNT
};

// a de9 port in amiga mode
enum settings_amiga_port {
    SETTINGS_AMIGA_PORT_MOUSE = 0,
    SETTINGS_AMIGA_PORT_JOYSTICK,
    SETTINGS_AMIGA_PORT_CD32,
    SETTINGS_AMIGA_PORT_OFF,
    SETTINGS_AMIGA_PORT_COUNT
};

// a de9 port in c64 mode
enum settings_c64_port {
    SETTINGS_C64_PORT_JOYSTICK = 0,
    SETTINGS_C64_PORT_1351,
    SETTINGS_C64_PORT_OFF,
    SETTINGS_C64_PORT_COUNT
};

enum settings_pad_directions {
    SETTINGS_PAD_DIR_BOTH = 0,      // d-pad and left stick
    SETTINGS_PAD_DIR_DPAD,
    SETTINGS_PAD_DIR_STICK,
    SETTINGS_PAD_DIR_COUNT
};

enum settings_pad_triangle {
    SETTINGS_PAD_TRIANGLE_UP = 0,   // a "jump" button
    SETTINGS_PAD_TRIANGLE_AUTOFIRE,
    SETTINGS_PAD_TRIANGLE_OFF,
    SETTINGS_PAD_TRIANGLE_COUNT
};

// usb/bt keyboard -> amiga
enum settings_kb_preset {
    SETTINGS_KB_PRESET_ORIGINAL = 0,
    SETTINGS_KB_PRESET_YZ,          // y <-> z
    SETTINGS_KB_PRESET_AQWZ,        // a <-> q, w <-> z
    SETTINGS_KB_PRESET_COUNT
};

enum settings_kb_help {
    SETTINGS_KB_HELP_INSERT = 0,
    SETTINGS_KB_HELP_HOME,
    SETTINGS_KB_HELP_PAGEUP,
    SETTINGS_KB_HELP_F11,
    SETTINGS_KB_HELP_NONE,
    SETTINGS_KB_HELP_COUNT
};

enum settings_kb_delete {
    SETTINGS_KB_DELETE_DELETE = 0,
    SETTINGS_KB_DELETE_BACKSPACE,
    SETTINGS_KB_DELETE_NONE,
    SETTINGS_KB_DELETE_COUNT
};

enum settings_kb_iso {
    SETTINGS_KB_ISO_BACKSLASH = 0,
    SETTINGS_KB_ISO_AMIGA,
    SETTINGS_KB_ISO_COUNT
};

// amiga keyboard -> usb: how the numpad ( ) reach the host
enum settings_ak_parens {
    SETTINGS_AK_PARENS_KEYPAD = 0,  // keypad ( ) usages (linux, any layout)
    SETTINGS_AK_PARENS_US,          // shift+9, shift+0 (us/uk)
    SETTINGS_AK_PARENS_NORDIC,      // shift+8, shift+9 (fi/se/no/dk/de)
    SETTINGS_AK_PARENS_FRENCH,      // 5 and - unshifted (azerty)
    SETTINGS_AK_PARENS_COUNT
};

enum settings_bt_pairing {
    SETTINGS_BT_PAIRING_AUTO = 0,
    SETTINGS_BT_PAIRING_PAIRED_ONLY,
    SETTINGS_BT_PAIRING_COUNT
};

enum settings_wifi {
    SETTINGS_WIFI_OFF = 0,
    SETTINGS_WIFI_AP,
    SETTINGS_WIFI_JOIN,
    SETTINGS_WIFI_COUNT
};

enum settings_display {
    SETTINGS_DISPLAY_STATUS = 0,
    SETTINGS_DISPLAY_HID,
    SETTINGS_DISPLAY_MOUSE,
    SETTINGS_DISPLAY_OFF,
    SETTINGS_DISPLAY_COUNT
};

#define SETTINGS_MOUSE_SPEED_COUNT  4   // 300, 200, 150, 100 us
#define SETTINGS_NET_SCREEN_COUNT   8   // 640x256 ... 1920x1080 (settings_names.c)
#define SETTINGS_NET_SCALE_COUNT    5   // 50, 75, 100, 150, 200 % counts per pixel
#define SETTINGS_WATCHDOG_COUNT     3   // off, 2 s, 5 s
#define SETTINGS_DEADZONE_COUNT     3   // 25, 50, 75 %
#define SETTINGS_PAD_BUTTONS        7   // fire 1-3 / cd32 red blue green yellow l r play
#define SETTINGS_PAD_BUTTON_MAX     17  // 0 auto, 1..16 hid button, 17 off
#define SETTINGS_REMAP_SLOTS        8
#define SETTINGS_PAD_COMBOS         6
#define SETTINGS_PAD_COMBO_BUTTONS  16  // a pad's own buttons 1..16
#define SETTINGS_PAD_COMBO_OUTPUTS  0x7fu // up down left right, fire 1 2 3 (pad.h's bits)

#define SETTINGS_WIFI_SSID_LEN      33  // 32 + nul
#define SETTINGS_WIFI_PASSWORD_LEN  64  // 63 + nul (wpa2 passphrase)
#define SETTINGS_PAGE_PASSWORD_LEN  17  // 16 + nul
#define SETTINGS_DEVICE_NAME_LEN    16  // 15 + nul

typedef struct {
    uint8_t source, target;         // source 0: slot unused
} settings_remap_t;

// a pad button that stands for directions and fire buttons held together,
// and / or a keyboard key
typedef struct {
    uint8_t button;                 // the pad's own button 1..16; 0: slot unused
    uint8_t outputs;                // PAD_UP .. PAD_FIRE3 (pad.h)
    uint8_t key;                    // hid keyboard usage; 0: none
} settings_combo_t;

typedef struct {
    uint8_t mode;                                   // enum settings_mode
    uint8_t amiga_port[2];                          // [0] port 1, [1] port 2
    uint8_t c64_port[2];

    uint8_t pad_swap;                               // 0/1: swap players 1 and 2
    uint8_t pad_merge;                              // 0/1: every pad drives player 1
    uint8_t pad_deadzone;
    uint8_t pad_directions;
    uint8_t pad_triangle;
    uint8_t pad_touchpad;                           // 0/1: touchpad drives the mouse
    uint8_t pad_buttons[SETTINGS_PAD_BUTTONS];

    uint8_t kb_preset;
    uint8_t kb_help;
    uint8_t kb_delete;
    uint8_t kb_iso;
    settings_remap_t kb_remap[SETTINGS_REMAP_SLOTS]; // hid source -> amiga target (0xff off)

    uint8_t ak_parens;
    uint8_t ak_cad;                                 // 0/1: ctrl-amiga-amiga -> ctrl-alt-del
    settings_remap_t ak_remap[SETTINGS_REMAP_SLOTS]; // amiga source (+1) -> hid target (0xff off)

    uint8_t bt_enabled;
    uint8_t bt_pairing;

    uint8_t wifi_mode;
    uint8_t ota_needs_boot;                         // 0/1: updates need a BOOT press

    uint8_t wheel_enabled;
    uint8_t wheel_reverse;
    uint8_t mouse_speed;
    uint8_t display;
    uint8_t watchdog;

    char wifi_ssid[SETTINGS_WIFI_SSID_LEN];
    char wifi_password[SETTINGS_WIFI_PASSWORD_LEN];
    char page_password[SETTINGS_PAGE_PASSWORD_LEN];

    uint8_t net_control;                            // 0/1: kvmd-compatible keyboard / mouse api
    uint8_t net_screen;                             // target screen, for absolute moves on de9
    uint8_t net_scale;                              // mouse counts per screen pixel

    uint8_t page_open;                              // 0/1: join mode asks for no page password
    settings_combo_t pad_combo[SETTINGS_PAD_COMBOS];

    char device_name[SETTINGS_DEVICE_NAME_LEN];     // access point and .local name; empty: ninepin-xxxx

    uint8_t reserved[7];                            // zero; pads the struct to 224 bytes
} settings_t;

/**
 * @brief Fill in the factory defaults.
 */
void settings_defaults(settings_t *settings);

/**
 * @return true if the text can be a device name: empty, or 1 to 15 of a-z,
 *         0-9 and '-', not starting or ending with '-'
 */
bool settings_device_name_ok(char const *name);

/**
 * @return true if every field is in range and every string terminated
 */
bool settings_valid(settings_t const *settings);

/**
 * @brief Load the newest valid record from flash, or the defaults if there is
 *        none. Call once, early in main(), before anything reads a setting.
 */
void settings_init(void);

/**
 * @return the settings in effect
 */
settings_t const *settings_get(void);

/**
 * @brief Write settings to flash (when they differ from the stored record) and
 *        make them the ones in effect. Core0 main loop only, never an irq.
 *
 * The flash write runs with core1 locked out and interrupts off for one sector
 * erase plus one page program, tens of milliseconds: rare, user-driven saves
 * only. A usb-a device may see a brief pause in its frame timing.
 *
 * @return true if stored (or already stored) and applied
 */
bool settings_save(settings_t const *settings);

/**
 * @return a count of the successful settings_save() calls since boot, stored
 *         or unchanged alike: "the settings were saved again"
 */
uint32_t settings_generation(void);

/**
 * @return where the active settings came from: "flash", "defaults"
 */
char const *settings_source(void);

#endif // _SETTINGS_H
