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

#ifdef HAS_MENU

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "display/disp_ssd.h"
#include "input_bridge.h"
#include "platform/amiga/keyboard.h"
#include "platform/amiga/keyboard_recv.h"
#include "settings.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/run_mode.h"
#ifdef HAS_USB_DEVICE
#include "usb_device.h"
#endif

#define MENU_HOLD_OPEN_MS       1000    // help held this long opens the menu
#define MENU_DOUBLE_PRESS_MS    500     // two BOOT presses this close open it
#define MENU_ENCODER_LEAVE_MS   1000    // the encoder switch held this long leaves

//--------------------------------------------------------------------+
// the items
//--------------------------------------------------------------------+

typedef enum { ITEM_CHOICE, ITEM_ACTION } item_kind_t;

typedef struct {
    char const *label;
    item_kind_t kind;
    uint16_t offset;                // ITEM_CHOICE: offsetof(settings_t, field)
    uint8_t count;                  // ITEM_CHOICE: number of values
    char const *const *names;       // ITEM_CHOICE: one per value
    bool (*visible)(void);          // NULL: always
    void (*action)(void);           // ITEM_ACTION
} menu_item_t;

static char const *const names_mode[] = { "amiga", "c64", "usb" };
static char const *const names_amiga_port[] = { "mouse", "joystick", "cd32 pad", "off" };
static char const *const names_c64_port[] = { "joystick", "1351 mouse", "off" };
static char const *const names_no_yes[] = { "no", "yes" };
static char const *const names_off_on[] = { "off", "on" };
static char const *const names_deadzone[] = { "25%", "50%", "75%" };
static char const *const names_directions[] = { "d-pad + stick", "d-pad", "stick" };
static char const *const names_triangle[] = { "up (jump)", "autofire", "off" };
static char const *const names_wheel_dir[] = { "normal", "reversed" };
static char const *const names_mouse_speed[] = { "300 us", "200 us", "150 us", "100 us" };
static char const *const names_display[] = { "status", "hid diag", "mouse diag", "off" };
static char const *const names_watchdog[] = { "off", "2 s", "5 s" };
static char const *const names_bt_pairing[] = { "automatic", "paired only" };
static char const *const names_wifi[] = { "off", "access point", "join network" };

static settings_t edited;

static bool visible_amiga(void) { return edited.mode == SETTINGS_MODE_AMIGA; }
static bool visible_c64(void) { return edited.mode == SETTINGS_MODE_C64; }

static void action_defaults(void);
static void action_save(void);
static void action_cancel(void);

#define CHOICE(label, field, names, visible) \
    { label, ITEM_CHOICE, offsetof(settings_t, field), count_of(names), names, visible, NULL }
#define ACTION(label, fn) \
    { label, ITEM_ACTION, 0, 0, NULL, NULL, fn }

static const menu_item_t items[] = {
    CHOICE("Mode",              mode,           names_mode,         NULL),
    CHOICE("Port 1",            amiga_port[0],  names_amiga_port,   visible_amiga),
    CHOICE("Port 2",            amiga_port[1],  names_amiga_port,   visible_amiga),
    CHOICE("Port 1",            c64_port[0],    names_c64_port,     visible_c64),
    CHOICE("Port 2",            c64_port[1],    names_c64_port,     visible_c64),
    CHOICE("Swap players",      pad_swap,       names_no_yes,       NULL),
    CHOICE("Merge all pads",    pad_merge,      names_no_yes,       NULL),
    CHOICE("Pad directions",    pad_directions, names_directions,   NULL),
    CHOICE("Stick deadzone",    pad_deadzone,   names_deadzone,     NULL),
    CHOICE("Triangle button",   pad_triangle,   names_triangle,     NULL),
    CHOICE("Touchpad mouse",    pad_touchpad,   names_off_on,       NULL),
    CHOICE("Mouse wheel",       wheel_enabled,  names_off_on,       NULL),
    CHOICE("Wheel direction",   wheel_reverse,  names_wheel_dir,    NULL),
    CHOICE("Mouse speed",       mouse_speed,    names_mouse_speed,  NULL),
    CHOICE("Display",           display,        names_display,      NULL),
    CHOICE("Watchdog",          watchdog,       names_watchdog,     NULL),
    CHOICE("Bluetooth",         bt_enabled,     names_off_on,       NULL),
    CHOICE("BT pairing",        bt_pairing,     names_bt_pairing,   NULL),
    CHOICE("Wi-Fi",             wifi_mode,      names_wifi,         NULL),
    CHOICE("Updates need BOOT", ota_needs_boot, names_no_yes,       NULL),
    ACTION("Factory defaults",  action_defaults),
    ACTION("Save & exit",       action_save),
    ACTION("Cancel",            action_cancel),
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
static char const *hint = "";
static char const *notice;          // one-off message on the hint line

static void (*real_write)(uint8_t x, uint8_t y, char *message);
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
    return (items[index].visible == NULL) || items[index].visible();
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

    snprintf(lines[0], MENU_COLS + 1, "%-15s%2u/%-2u%s", "SETTINGS", position, shown, dirty ? "*" : " ");
    snprintf(lines[1], MENU_COLS + 1, "%-*s", MENU_COLS, item->label);

    if (item->kind == ITEM_CHOICE) {
        char const *value = item->names[*item_value(cursor)];
        snprintf(lines[2], MENU_COLS + 1, editing ? " [ %s ]" : " < %s >", value);
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
#ifdef HAS_USB_DEVICE
    usb_device_release_all();
#endif
    amiga_keyboard_recv_release_all();
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
    cursor = 0;
    notice = NULL;
    hint = how;

    release_everything();

    // take the display: everyone else writes into the void until we close
    disp_save(saved_screen);
    real_write = disp_write;
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

    disp_write = real_write;
    disp_restore(saved_screen);
    dbgcons_print_counters();

    ahprintf("[menu] closed%s\n", (save && dirty) ? ", saved" : "");

    // the new mode last, with the display back for its status line
    if (mode_changed)
        run_mode_set((run_mode_t)edited.mode);
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

    if (item->kind != ITEM_CHOICE)
        return;

    value = item_value(cursor);
    *value = (uint8_t)((*value + item->count + step) % item->count);
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

    switch (input) {
        case MENU_IN_NEXT:  editing = false; move(+1); break;
        case MENU_IN_PREV:  editing = false; move(-1); break;
        case MENU_IN_INC:   change(+1); break;
        case MENU_IN_DEC:   change(-1); break;

        case MENU_IN_ENTER:
            if (items[cursor].kind == ITEM_ACTION) {
                items[cursor].action();
                return;     // may have closed
            }
            change(+1);
            break;

        case MENU_IN_LEAVE:
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
        case USER_BUTTON_EVENT_LONG_RELEASE:    menu_input(MENU_IN_ENTER); break;
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

    if (items[cursor].kind == ITEM_ACTION) {
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

#endif // HAS_MENU
