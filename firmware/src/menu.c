/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the settings menu on the oled; see menu.h for the controls. the approach to
 * taking the inputs over (release on open, ignore a keyboard after close until
 * it lets go) follows stefan reinauer's runtime_menu.c (reinauer/amigahid-pico,
 * features branch); the controls, the item table and the drawing are this
 * tree's own.
 */

#include "config.h"
#include "menu.h"
#include "apply.h"
#include "pad.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "display/disp_ssd.h"
#include "input_bridge.h"
#include "keyboard_map.h"
#include "platform/amiga/keyboard.h"
#include "platform/amiga/de9.h"
#include "platform/amiga/keyboard_recv.h"
#include "ota.h"
#include "settings.h"
#include "settings_names.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/run_mode.h"
#include "usb_device.h"
#ifdef ENABLE_BLUETOOTH_HID
#include "bt_hid.h"
#endif
#include "profiles.h"
#ifdef HAS_WIFI
#include "wifi.h"
#endif

#define MENU_HOLD_OPEN_MS       1000    // help held this long opens the menu
#define MENU_DOUBLE_PRESS_MS    500     // two BOOT presses this close open it
#define MENU_ENCODER_LEAVE_MS   1000    // the encoder switch held this long leaves
#define MENU_CONFIRM_MS         5000    // a second select this soon confirms

//--------------------------------------------------------------------+
// the items
//--------------------------------------------------------------------+

typedef enum { ITEM_CHOICE, ITEM_ACTION, ITEM_REMAP, ITEM_PROFILE, ITEM_GROUP, ITEM_BACK } item_kind_t;

// the menu has two levels: the top one, and a group of items behind each of
// its ITEM_GROUP entries
typedef enum { G_TOP = 0, G_PADS, G_MOUSE, G_KEYBOARD, G_BT, G_WIFI, G_BOARD, G_COUNT, G_ANY = 0xff } group_t;

static char const *const group_titles[G_COUNT] = {
    "SETTINGS", "PADS", "MOUSE", "KEYBOARD", "BLUETOOTH", "WI-FI", "BOARD",
};

typedef struct {
    char const *label;
    item_kind_t kind;
    uint16_t offset;                // ITEM_CHOICE: offsetof(settings_t, field)
    settings_names_t const *names;  // ITEM_CHOICE: one per value
    bool (*visible)(void);          // NULL: always
    void (*action)(void);           // ITEM_ACTION
    uint8_t slot;                   // ITEM_REMAP: slot number; ITEM_GROUP: the group it opens
    uint8_t group;                  // the level it shows on (G_ANY: every group, not the top)
    bool from_amiga;                // ITEM_REMAP: amiga keyboard -> usb (else usb -> amiga)
} menu_item_t;

static settings_t edited;

static bool visible_amiga(void) { return edited.mode == SETTINGS_MODE_AMIGA; }
static bool visible_c64(void) { return edited.mode == SETTINGS_MODE_C64; }

static void action_defaults(void);
static void action_save(void);
static void action_cancel(void);
#ifdef ENABLE_BLUETOOTH_HID
static void action_bt_pair(void);
static void action_bt_forget(void);
#endif
#ifdef HAS_WIFI
static void action_page_password_show(void);
static void action_page_password_new(void);
#endif
static void action_previous_firmware(void);

#define CHOICE(group, label, field, names, visible) \
    { label, ITEM_CHOICE, offsetof(settings_t, field), &settings_##names, visible, NULL, 0, group, false }
#define ACTION(group, label, fn) \
    { label, ITEM_ACTION, 0, NULL, NULL, fn, 0, group, false }
#define GROUP(label, group) \
    { label, ITEM_GROUP, 0, NULL, NULL, NULL, group, G_TOP, false }
#define REMAP_PC(n) \
    { "PC remap " #n, ITEM_REMAP, offsetof(settings_t, kb_remap), NULL, NULL, NULL, (n) - 1, G_KEYBOARD, false }
#define REMAP_A4000(n) \
    { "A4000 remap " #n, ITEM_REMAP, offsetof(settings_t, ak_remap), NULL, NULL, NULL, (n) - 1, G_KEYBOARD, true }

// the top level first, what is changed most often at its head; "save & exit"
// and "cancel" stay the last two (ITEM_SAVE)
static const menu_item_t items[] = {
    CHOICE(G_TOP, "Mode",               mode,           names_mode,         NULL),
    { "Pad profile", ITEM_PROFILE, offsetof(settings_t, pad_profile), NULL, NULL, NULL, 0, G_TOP, false },
    CHOICE(G_TOP, "Port 1",             amiga_port[0],  names_amiga_port,   visible_amiga),
    CHOICE(G_TOP, "Port 2",             amiga_port[1],  names_amiga_port,   visible_amiga),
    CHOICE(G_TOP, "Port 1",             c64_port[0],    names_c64_port,     visible_c64),
    CHOICE(G_TOP, "Port 2",             c64_port[1],    names_c64_port,     visible_c64),
    GROUP("Pads",       G_PADS),
    GROUP("Mouse",      G_MOUSE),
    GROUP("Keyboard",   G_KEYBOARD),
    GROUP("Bluetooth",  G_BT),
    GROUP("Wi-Fi",      G_WIFI),
    GROUP("Board",      G_BOARD),

    CHOICE(G_PADS, "Swap players",      pad_swap,       names_no_yes,       NULL),
    CHOICE(G_PADS, "Merge all pads",    pad_merge,      names_no_yes,       NULL),
    CHOICE(G_PADS, "Pad directions",    pad_directions, names_directions,   NULL),
    CHOICE(G_PADS, "Stick deadzone",    pad_deadzone,   names_deadzone,     NULL),
    CHOICE(G_PADS, "Triangle button",   pad_triangle,   names_triangle,     NULL),
    CHOICE(G_PADS, "Touchpad mouse",    pad_touchpad,   names_off_on,       NULL),

    CHOICE(G_MOUSE, "Mouse wheel",      wheel_enabled,  names_off_on,       NULL),
    CHOICE(G_MOUSE, "Wheel direction",  wheel_reverse,  names_wheel_dir,    NULL),
    CHOICE(G_MOUSE, "Mouse speed",      mouse_speed,    names_mouse_speed,  NULL),

    CHOICE(G_KEYBOARD, "Keyboard preset", kb_preset,    names_kb_preset,    NULL),
    CHOICE(G_KEYBOARD, "Help key",      kb_help,        names_kb_help,      NULL),
    CHOICE(G_KEYBOARD, "Del key",       kb_delete,      names_kb_delete,    NULL),
    CHOICE(G_KEYBOARD, "ISO key",       kb_iso,         names_kb_iso,       NULL),
    CHOICE(G_KEYBOARD, "A4000 numpad ( )", ak_parens,   names_ak_parens,    NULL),
    CHOICE(G_KEYBOARD, "Ctrl-Amiga-Amiga", ak_cad,      names_ak_cad,       NULL),
    REMAP_PC(1), REMAP_PC(2), REMAP_PC(3), REMAP_PC(4),
    REMAP_PC(5), REMAP_PC(6), REMAP_PC(7), REMAP_PC(8),
    REMAP_A4000(1), REMAP_A4000(2), REMAP_A4000(3), REMAP_A4000(4),
    REMAP_A4000(5), REMAP_A4000(6), REMAP_A4000(7), REMAP_A4000(8),

    CHOICE(G_BT, "Bluetooth",           bt_enabled,     names_off_on,       NULL),
    CHOICE(G_BT, "BT pairing",          bt_pairing,     names_bt_pairing,   NULL),
#ifdef ENABLE_BLUETOOTH_HID
    ACTION(G_BT, "BT pair (2 min)",     action_bt_pair),
    ACTION(G_BT, "Forget BT devices",   action_bt_forget),
#endif

    CHOICE(G_WIFI, "Wi-Fi",             wifi_mode,      names_wifi,         NULL),
#ifdef HAS_WIFI
    CHOICE(G_WIFI, "Page password",     page_open,      names_page_open,    NULL),
    ACTION(G_WIFI, "Show page password", action_page_password_show),
    ACTION(G_WIFI, "New page password", action_page_password_new),
    CHOICE(G_WIFI, "Network control",   net_control,    names_off_on,       NULL),
    CHOICE(G_WIFI, "Net screen",        net_screen,     names_net_screen,   NULL),
    CHOICE(G_WIFI, "Net mouse scale",   net_scale,      names_net_scale,    NULL),
#endif

    CHOICE(G_BOARD, "Display",          display,        names_display,      NULL),
    CHOICE(G_BOARD, "Watchdog",         watchdog,       names_watchdog,     NULL),
    CHOICE(G_BOARD, "Updates need BOOT", ota_needs_boot, names_no_yes,      NULL),
    ACTION(G_BOARD, "Previous firmware", action_previous_firmware),
    ACTION(G_BOARD, "Factory defaults", action_defaults),

    { "Back", ITEM_BACK, 0, NULL, NULL, NULL, 0, G_ANY, false },
    ACTION(G_TOP, "Save & exit",        action_save),
    ACTION(G_TOP, "Cancel",             action_cancel),
};

#define ITEM_COUNT      count_of(items)
#define ITEM_SAVE       (ITEM_COUNT - 2)

//--------------------------------------------------------------------+
// state
//--------------------------------------------------------------------+

static bool open;
static bool dirty;                  // edits not saved yet
static bool editing;                // encoder: a push turned the knob to the value
static unsigned cursor;
static uint8_t group;               // the level the cursor is on
static unsigned group_cursor[G_COUNT]; // where the cursor was on each level
static unsigned resume;             // the last setting visited: the next open starts there
static char const *hint = "";

// teaching a remap slot: 0 idle, 1 waiting for the key to change, 2 for its
// new meaning
static unsigned learning;
static settings_t learn_backup;      // edits as they were before teaching began
static char const *notice;          // one-off message on the hint line
static uint32_t confirm_since;      // an action waiting for its second select; 0: none

static void (*real_write)(uint8_t x, uint8_t y, char *message);
static void (*saved_write)(uint8_t x, uint8_t y, char *message);
static uint8_t saved_screen[DISP_FRAMEBUFFER_BYTES];

// keyboards: the last report per slot (edges), help holds, and slots ignored
// after closing until they let go of everything
static hid_keyboard_report_t last_hid[INPUT_BRIDGE_MAX_SLOTS];
static uint32_t hid_help_since[INPUT_BRIDGE_MAX_SLOTS];     // 0: not held
static bool quarantine[INPUT_BRIDGE_MAX_SLOTS];
static uint32_t amiga_help_since;

static uint32_t last_button_press;
static uint32_t encoder_down_since;
static bool encoder_down;

static uint32_t now_ms(void)
{
    uint32_t ms = to_ms_since_boot(get_absolute_time());
    return ms ? ms : 1;     // 0 means "not held"
}

static bool item_visible(unsigned index)
{
    if ((items[index].group != group) && !((items[index].group == G_ANY) && (group != G_TOP)))
        return false;
    return (items[index].visible == NULL) || items[index].visible();
}

/**
 * the cursor on to a level: where it was there last, else the level's first item
 */
static void level(uint8_t to, unsigned at)
{
    group_cursor[group] = cursor;
    group = to;
    cursor = at;
    for (unsigned i = 0; !item_visible(cursor) && (i < ITEM_COUNT); i++)
        cursor = i;
}

static void group_enter(void)
{
    uint8_t to = items[cursor].slot;

    level(to, group_cursor[to]);
}

static void group_leave(void)
{
    unsigned at = 0;

    for (unsigned i = 0; i < ITEM_COUNT; i++)
        if ((items[i].kind == ITEM_GROUP) && (items[i].slot == group))
            at = i;
    level(G_TOP, at);
}

static uint8_t *item_value(unsigned index)
{
    return (uint8_t *)&edited + items[index].offset;
}

//--------------------------------------------------------------------+
// drawing
//--------------------------------------------------------------------+

static void render(char lines[MENU_LINES][MENU_COLS + 1])
{
    menu_item_t const *item = &items[cursor];
    unsigned shown = 0, position = 0;

    for (unsigned i = 0; i < ITEM_COUNT; i++) {
        if (!item_visible(i))
            continue;
        shown++;
        if (i == cursor)
            position = shown;
    }

    snprintf(lines[0], MENU_COLS + 1, "%-15s%2u/%-2u%s", group_titles[group], position, shown, dirty ? "*" : " ");
    snprintf(lines[1], MENU_COLS + 1, "%-*s", MENU_COLS, item->label);

    if (item->kind == ITEM_CHOICE) {
        char const *value = item->names->names[*item_value(cursor)];
        snprintf(lines[2], MENU_COLS + 1, editing ? " [ %s ]" : " < %s >", value);
    } else if (item->kind == ITEM_PROFILE) {
        // the pad profiles are made on the page; here one is picked
        profile_t const *profile = edited.pad_profile ? profiles_get(edited.pad_profile - 1u) : NULL;

        if (!profiles_count())
            snprintf(lines[2], MENU_COLS + 1, " (none saved)");
        else
            snprintf(lines[2], MENU_COLS + 1, editing ? " [ %s%s ]" : " < %s%s >", profile ? profile->name : "-",
                (profile && !profiles_matches(profile, &edited)) ? "*" : "");
    } else if (item->kind == ITEM_REMAP) {
        settings_remap_t const *remap = (settings_remap_t const *)item_value(cursor) + item->slot;

        if (learning == 1)
            snprintf(lines[2], MENU_COLS + 1, item->from_amiga ? " press A4000 key" : " press PC key");
        else if (learning == 2)
            snprintf(lines[2], MENU_COLS + 1, " press its new key");
        else if (!remap->source)
            snprintf(lines[2], MENU_COLS + 1, " (empty)");
        else if (item->from_amiga)
            snprintf(lines[2], MENU_COLS + 1, " %s > %s", keyboard_map_amiga_name((uint8_t)(remap->source - 1)),
                keyboard_map_hid_name(remap->target));
        else
            snprintf(lines[2], MENU_COLS + 1, " %s > %s", keyboard_map_hid_name(remap->source),
                keyboard_map_amiga_name(remap->target));
    } else if (item->kind == ITEM_GROUP) {
        snprintf(lines[2], MENU_COLS + 1, " > select to open");
    } else if (item->kind == ITEM_BACK) {
        snprintf(lines[2], MENU_COLS + 1, " > select to go up");
    } else {
        snprintf(lines[2], MENU_COLS + 1, " > select to run");
    }

    snprintf(lines[3], MENU_COLS + 1, "%-*s", MENU_COLS, notice ? notice : hint);

    for (unsigned i = 0; i < MENU_LINES; i++) {
        size_t length = strlen(lines[i]);
        memset(lines[i] + length, ' ', MENU_COLS - length);
        lines[i][MENU_COLS] = 0;
    }
}

static void draw(void)
{
    char lines[MENU_LINES][MENU_COLS + 1];

    if (!open)
        return;

    render(lines);
    for (unsigned i = 0; i < MENU_LINES; i++)
        real_write(0, (uint8_t)i, lines[i]);
}

void menu_lines(char lines[MENU_LINES][MENU_COLS + 1])
{
    if (!open) {
        for (unsigned i = 0; i < MENU_LINES; i++)
            lines[i][0] = 0;
        return;
    }
    render(lines);
}

//--------------------------------------------------------------------+
// open / close
//--------------------------------------------------------------------+

static void release_everything(void)
{
    input_bridge_release_all();
    usb_device_release_all();
    amiga_keyboard_recv_release_all();
    pads_release_all();
}

static void menu_open(char const *how)
{
    if (open)
        return;

    if (!disp_present()) {
        ahprintf("[menu] no display, not opening\n");
        return;
    }

    edited = *settings_get();
    dirty = false;
    editing = false;
    // back to the setting visited last, on its level
    group = G_TOP;
    cursor = 0;
    level(items[resume].group, resume);
    notice = NULL;
    confirm_since = 0;
    hint = how;

    release_everything();

    // take the display: everyone else writes into the void until we close
    disp_save(saved_screen);
    real_write = disp_writer();     // reaches the panel even with the display set to off
    saved_write = disp_write;
    disp_write = _noop_disp_write;
    disp_clear();

    open = true;
    ahprintf("[menu] open\n");
    draw();
}

static void menu_close(bool save)
{
    bool mode_changed = false;

    if (!open)
        return;

    if (save && dirty) {
        mode_changed = edited.mode != settings_get()->mode;
        if (!settings_save(&edited)) {
            notice = "save failed";
            draw();
            return;
        }
    }

    open = false;
    editing = false;

    // keyboards still holding keys are ignored until they let go of them all
    for (unsigned slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++) {
        quarantine[slot] = true;
        hid_help_since[slot] = 0;
    }
    amiga_help_since = 0;

    disp_write = saved_write;
    disp_restore(saved_screen);
    dbgcons_print_counters();

    ahprintf("[menu] closed%s\n", (save && dirty) ? ", saved" : "");

    // the new mode last, with the display back for its status line; the de9
    // ports follow the saved port settings either way
    if (mode_changed)
        run_mode_set((run_mode_t)edited.mode);
    if (save)
        apply_settings();
}

static void action_defaults(void)
{
    settings_defaults(&edited);
    dirty = true;
    notice = "defaults loaded";
}

static void action_save(void)
{
    menu_close(true);
}

static void action_cancel(void)
{
    menu_close(false);
}

#ifdef ENABLE_BLUETOOTH_HID
/**
 * the bluetooth actions work on the radio as it runs now, not on the edits
 */
static void action_bt_pair(void)
{
    if (!settings_get()->bt_enabled) {
        notice = "Bluetooth is off";
        return;
    }
    bt_hid_pair_window();
    notice = "pairing open 2 min";
}

static void action_bt_forget(void)
{
    uint32_t now = now_ms();

    if (confirm_since && ((uint32_t)(now - confirm_since) < MENU_CONFIRM_MS)) {
        confirm_since = 0;
        if (!settings_get()->bt_enabled) {
            notice = "Bluetooth is off";
            return;
        }
        bt_hid_forget_all();
        notice = "BT devices forgotten";
        return;
    }

    confirm_since = now;
    notice = "select again: forget";
}
#endif

#ifdef HAS_WIFI
/**
 * the page password works on the stored settings, like the bluetooth actions;
 * a new one goes into the edits as well, so saving them does not undo it
 */
static char page_password_notice[MENU_COLS + 1];

static void action_page_password_show(void)
{
    char const *password = settings_get()->page_password;

    snprintf(page_password_notice, sizeof(page_password_notice), "pw %s", *password ? password : "(none yet)");
    notice = page_password_notice;
}

static void action_page_password_new(void)
{
    if (!wifi_new_page_password()) {
        notice = "save failed";
        return;
    }
    memcpy(edited.page_password, settings_get()->page_password, sizeof(edited.page_password));
    edited.page_open = settings_get()->page_open;
    snprintf(page_password_notice, sizeof(page_password_notice), "new: %s", settings_get()->page_password);
    notice = page_password_notice;
}
#endif

/**
 * back to the firmware before the last update, if it was saved; asks twice
 */
static void action_previous_firmware(void)
{
    static char message[MENU_COLS + 1];
    uint32_t now = now_ms();

    if (!ota_previous_available()) {
        notice = "no previous firmware";
        return;
    }
    if (confirm_since && ((uint32_t)(now - confirm_since) < MENU_CONFIRM_MS)) {
        confirm_since = 0;
        notice = "restarting...";
        draw();
        ota_boot_previous(message, sizeof(message));
        notice = message;   // only reached when it did not happen
        return;
    }
    confirm_since = now;
    notice = "select again: restart";
}

//--------------------------------------------------------------------+
// inputs
//--------------------------------------------------------------------+

static void move(int step)
{
    for (unsigned tries = 0; tries < ITEM_COUNT; tries++) {
        cursor = (cursor + ITEM_COUNT + (unsigned)step) % ITEM_COUNT;
        if (item_visible(cursor))
            return;
    }
}

static void change(int step)
{
    menu_item_t const *item = &items[cursor];
    uint8_t *value;

    if (item->kind == ITEM_REMAP) {
        settings_remap_t *remap = (settings_remap_t *)item_value(cursor) + item->slot;

        if (remap->source) {
            remap->source = remap->target = 0;
            dirty = true;
            notice = "slot cleared";
        }
        return;
    }

    if (item->kind == ITEM_PROFILE) {
        // on to the next saved profile, which goes into the edits at once
        unsigned slot = edited.pad_profile ? edited.pad_profile - 1u : ((step > 0) ? PROFILES_MAX - 1u : 0u);

        for (unsigned tries = 0; tries < PROFILES_MAX; tries++) {
            profile_t const *profile;

            slot = (slot + ((step > 0) ? 1u : PROFILES_MAX - 1u)) % PROFILES_MAX;
            profile = profiles_get(slot);
            if (profile) {
                edited.pad_profile = (uint8_t)(slot + 1);
                profiles_to_settings(profile, &edited);
                dirty = true;
                return;
            }
        }
        return;
    }

    if (item->kind != ITEM_CHOICE)
        return;

    value = item_value(cursor);
    *value = (uint8_t)((*value + item->names->count + step) % item->names->count);
    dirty = true;
}

void menu_input(menu_input_t input)
{
    if (input == MENU_IN_OPEN) {
        menu_open(hint && *hint ? hint : "");
        return;
    }

    if (!open)
        return;

    notice = NULL;

    // a confirmation is only for the very next select, on the same item
    if ((input != MENU_IN_ENTER) && (input != MENU_IN_CYCLE))
        confirm_since = 0;

    if (learning) {
        learning = 0;
        edited = learn_backup;
        notice = "remap cancelled";
        draw();
        return;
    }

    switch (input) {
        case MENU_IN_NEXT:  editing = false; move(+1); break;
        case MENU_IN_PREV:  editing = false; move(-1); break;
        case MENU_IN_INC:
            if (items[cursor].kind == ITEM_GROUP)
                group_enter();
            else
                change(+1);
            break;
        case MENU_IN_DEC:   change(-1); break;

        case MENU_IN_ENTER:
            if (items[cursor].kind == ITEM_GROUP) {
                group_enter();
                break;
            }
            if (items[cursor].kind == ITEM_BACK) {
                group_leave();
                break;
            }
            if (items[cursor].kind == ITEM_ACTION) {
                items[cursor].action();
                if (!open)
                    return;     // it closed the menu
                break;          // show its notice
            }
            if (items[cursor].kind == ITEM_REMAP) {
                learn_backup = edited;
                learning = 1;
                notice = "Esc cancels";
                break;
            }
            // enter does not change a value: right after left/right it would
            // undo the change
            notice = "Save & exit stores";
            break;

        case MENU_IN_CYCLE:
            if ((items[cursor].kind == ITEM_CHOICE) || (items[cursor].kind == ITEM_PROFILE)) {
                change(+1);
                break;
            }
            menu_input(MENU_IN_ENTER);
            return;

        case MENU_IN_LEAVE:
            // up a level first; the top level leaves the menu
            if (group != G_TOP) {
                editing = false;
                group_leave();
                break;
            }
            if (!dirty) {
                menu_close(false);
                return;
            }
            if (cursor != ITEM_SAVE) {
                // first leave with edits: offer the save
                cursor = ITEM_SAVE;
                notice = "unsaved: save?";
                break;
            }
            menu_close(false);
            return;

        default:
            break;
    }

    if ((items[cursor].kind == ITEM_CHOICE) || (items[cursor].kind == ITEM_PROFILE) || (items[cursor].kind == ITEM_REMAP))
        resume = cursor;

    draw();
}

bool menu_is_open(void)
{
    return open;
}

static uint8_t hid_help_key(void)
{
    switch (settings_get()->kb_help) {
        case SETTINGS_KB_HELP_HOME:     return HID_KEY_HOME;
        case SETTINGS_KB_HELP_PAGEUP:   return HID_KEY_PAGE_UP;
        case SETTINGS_KB_HELP_F11:      return HID_KEY_F11;
        default:                        return HID_KEY_INSERT;
    }
}

static bool report_has(hid_keyboard_report_t const *report, uint8_t key)
{
    for (unsigned i = 0; i < 6; i++)
        if (report->keycode[i] == key)
            return true;
    return false;
}

static bool report_empty(hid_keyboard_report_t const *report)
{
    if (report->modifier)
        return false;
    for (unsigned i = 0; i < 6; i++)
        if (report->keycode[i])
            return false;
    return true;
}


//--------------------------------------------------------------------+
// teaching remap slots
//--------------------------------------------------------------------+

// what a pc key means on the amiga before any remap (help sits on insert)
static uint8_t hid_to_amiga_base(uint8_t usage)
{
    static uint8_t const modifiers[8] = { AMIGA_CTRL, AMIGA_LSHIFT, AMIGA_LALT, AMIGA_LAMIGA,
        AMIGA_CTRL, AMIGA_RSHIFT, AMIGA_RALT, AMIGA_RAMIGA };

    if (usage >= HID_KEY_CONTROL_LEFT)
        return (usage <= HID_KEY_GUI_RIGHT) ? modifiers[usage - HID_KEY_CONTROL_LEFT] : AMIGA_UNKNOWN;
    return mapHidToAmiga[usage];
}

static void learn_cancel(void)
{
    learning = 0;
    edited = learn_backup;
    notice = "remap cancelled";
    draw();
}

/**
 * one key for the slot being taught.
 *
 * @param pc_usage      the key as a hid usage, if it came from a pc keyboard
 * @param amiga_code    the key as an amiga keycode, if it came from the a4000
 */
static void learn_key(bool from_pc, uint8_t pc_usage, uint8_t amiga_code)
{
    menu_item_t const *item = &items[cursor];
    settings_remap_t *slots = (settings_remap_t *)item_value(cursor);
    uint8_t source;

    if (learning == 1) {
        // the key to change comes from the keyboard the slot belongs to; pc
        // modifiers never pass through the key map, so they cannot be sources
        if (from_pc == item->from_amiga)
            return;
        if (from_pc && (pc_usage >= HID_KEY_CONTROL_LEFT))
            return;

        source = from_pc ? pc_usage : (uint8_t)(amiga_code + 1);

        // a key can only be remapped once
        for (unsigned j = 0; j < SETTINGS_REMAP_SLOTS; j++) {
            if ((j != item->slot) && (slots[j].source == source))
                slots[j].source = slots[j].target = 0;
        }

        slots[item->slot].source = source;
        slots[item->slot].target = KEYBOARD_MAP_DISABLED;
        learning = 2;
        notice = "Esc cancels";
        draw();
        return;
    }

    // its new meaning, pressed on either keyboard
    if (item->from_amiga)
        slots[item->slot].target = from_pc ? pc_usage : amiga_keyboard_recv_hid_usage(amiga_code);
    else
        slots[item->slot].target = from_pc ? hid_to_amiga_base(pc_usage) : amiga_code;

    if ((item->from_amiga && !slots[item->slot].target)
        || (!item->from_amiga && (slots[item->slot].target == AMIGA_UNKNOWN))) {
        slots[item->slot].target = KEYBOARD_MAP_DISABLED;
        notice = "no such key there";
        draw();
        return;
    }

    learning = 0;
    dirty = true;
    notice = "remapped";
    draw();
}

static void learn_hid(hid_keyboard_report_t const *report, hid_keyboard_report_t const *previous)
{
    uint8_t usage = 0;
    uint8_t new_modifiers = report->modifier & (uint8_t)~previous->modifier;

    for (unsigned i = 0; i < 6; i++) {
        uint8_t key = report->keycode[i];

        if (key && !report_has(previous, key)) {
            usage = key;
            break;
        }
    }

    if (!usage && new_modifiers)
        usage = (uint8_t)(HID_KEY_CONTROL_LEFT + __builtin_ctz(new_modifiers));

    if (!usage)
        return;

    if (usage == HID_KEY_ESCAPE)
        learn_cancel();
    else
        learn_key(true, usage, 0);
}

bool menu_keyboard_hid(uint8_t slot, hid_keyboard_report_t const *report)
{
    hid_keyboard_report_t previous;
    uint8_t help = hid_help_key();

    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return false;

    previous = last_hid[slot];
    last_hid[slot] = *report;

    if (!open) {
        if (quarantine[slot]) {
            if (!report_empty(report))
                return true;
            quarantine[slot] = false;
        }

        // the help key held opens the menu (menu_task() times it)
        if (report_has(report, help)) {
            if (!report_has(&previous, help))
                hid_help_since[slot] = now_ms();
        } else {
            hid_help_since[slot] = 0;
        }
        return false;
    }

    hint = "arrows  Enter  Esc";

    if (learning) {
        learn_hid(report, &previous);
        return true;
    }

    // act on keys newly down in this report
    for (unsigned i = 0; i < 6; i++) {
        uint8_t key = report->keycode[i];

        if (!key || report_has(&previous, key))
            continue;

        switch (key) {
            case HID_KEY_ARROW_UP:      menu_input(MENU_IN_PREV); break;
            case HID_KEY_ARROW_DOWN:    menu_input(MENU_IN_NEXT); break;
            case HID_KEY_ARROW_LEFT:    menu_input(MENU_IN_DEC); break;
            case HID_KEY_ARROW_RIGHT:   menu_input(MENU_IN_INC); break;
            case HID_KEY_ENTER:
            case HID_KEY_KEYPAD_ENTER:  menu_input(MENU_IN_ENTER); break;
            case HID_KEY_ESCAPE:        menu_input(MENU_IN_LEAVE); break;
            default:                    break;
        }

        if (!open)
            break;
    }
    return true;
}

bool menu_keyboard_amiga(uint8_t keycode, bool up)
{
    if (!open) {
        if (keycode == AMIGA_HELP)
            amiga_help_since = up ? 0 : now_ms();
        return false;
    }

    hint = "arrows  Return  Esc";

    if (up)
        return true;

    if (learning) {
        if (keycode == AMIGA_ESC)
            learn_cancel();
        else
            learn_key(false, 0, keycode);
        return true;
    }

    switch (keycode) {
        case AMIGA_UP:          menu_input(MENU_IN_PREV); break;
        case AMIGA_DOWN:        menu_input(MENU_IN_NEXT); break;
        case AMIGA_LEFT:        menu_input(MENU_IN_DEC); break;
        case AMIGA_RIGHT:       menu_input(MENU_IN_INC); break;
        case AMIGA_RETURN:
        case AMIGA_KPENTER:     menu_input(MENU_IN_ENTER); break;
        case AMIGA_ESC:         menu_input(MENU_IN_LEAVE); break;
        default:                break;
    }
    return true;
}

bool menu_button(user_button_event_t event)
{
    uint32_t now = now_ms();

    if (!open) {
        // a double-press opens; the first press of a pair does nothing
        if (event == USER_BUTTON_EVENT_PRESS) {
            if (last_button_press && ((uint32_t)(now - last_button_press) <= MENU_DOUBLE_PRESS_MS)) {
                last_button_press = 0;
                hint = "tap next  hold change";
                menu_open(hint);
                return true;
            }
            last_button_press = now;
            return true;
        }
        return false;   // the 1 s hold still switches the mode
    }

    hint = "tap next  hold change";

    switch (event) {
        case USER_BUTTON_EVENT_PRESS:           menu_input(MENU_IN_NEXT); break;
        case USER_BUTTON_EVENT_LONG_RELEASE:    menu_input(MENU_IN_CYCLE); break;
        case USER_BUTTON_EVENT_HOLD:            menu_input(MENU_IN_LEAVE); break;
        default:                                break;
    }
    return true;
}

void menu_encoder_turn(int8_t direction)
{
    if (!open)
        return;

    hint = "turn  push = edit";
    if (editing)
        menu_input((direction > 0) ? MENU_IN_INC : MENU_IN_DEC);
    else
        menu_input((direction > 0) ? MENU_IN_NEXT : MENU_IN_PREV);
}

void menu_encoder_switch(bool down)
{
    uint32_t now = now_ms();
    uint32_t held;

    if (down) {
        encoder_down = true;
        encoder_down_since = now;
        return;
    }

    if (!encoder_down)
        return;
    encoder_down = false;
    held = now - encoder_down_since;

    if (!open) {
        hint = "turn  push = edit";
        menu_open(hint);
        return;
    }

    if (held >= MENU_ENCODER_LEAVE_MS) {
        menu_input(MENU_IN_LEAVE);
        return;
    }

    if ((items[cursor].kind == ITEM_ACTION) || (items[cursor].kind == ITEM_GROUP) || (items[cursor].kind == ITEM_BACK)) {
        menu_input(MENU_IN_ENTER);
        return;
    }

    editing = !editing;
    draw();
}

void menu_init(void)
{
    open = false;
}

void menu_task(void)
{
    uint32_t now = now_ms();

    if (open)
        return;

    for (unsigned slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++) {
        if (hid_help_since[slot] && ((uint32_t)(now - hid_help_since[slot]) >= MENU_HOLD_OPEN_MS)) {
            hid_help_since[slot] = 0;
            hint = "arrows  Enter  Esc";
            menu_open(hint);
            return;
        }
    }

    if (amiga_help_since && ((uint32_t)(now - amiga_help_since) >= MENU_HOLD_OPEN_MS)) {
        amiga_help_since = 0;
        hint = "arrows  Return  Esc";
        menu_open(hint);
    }
}

