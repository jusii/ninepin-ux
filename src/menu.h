/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the settings menu on the oled (boards with HAS_MENU).
 *
 * one item at a time: its name, its value, and a hint for the controls in use.
 * edits work on a copy of the settings; "save & exit" stores and applies them,
 * "cancel" or leaving without saving throws them away. whilst the menu is open
 * it owns the display and every keyboard, mouse and pad: whatever was held is
 * released on opening, and a keyboard still holding keys when it closes is
 * ignored until it lets go of them all.
 *
 * the same abstract inputs come from every control:
 *
 *   control         open                  move        change          leave
 *   keyboard        hold help 1 s         up/down     left/right      esc
 *                   (insert on a pc kbd)              enter on actions
 *   BOOT button     double-press          press       release after   hold 3 s
 *                                                     a 1 s hold
 *                                                     (next value)
 *   encoder         push                  turn        push, turn,     hold push
 *                                                     push
 *   console         menu open             menu next   menu inc/dec    menu leave
 *
 * leaving with unsaved edits first moves to "save & exit"; leaving again there
 * throws the edits away.
 */

#ifndef _MENU_H
#define _MENU_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "tusb.h"

#include "util/user_button.h"

typedef enum {
    MENU_IN_OPEN = 0,
    MENU_IN_NEXT,
    MENU_IN_PREV,
    MENU_IN_INC,
    MENU_IN_DEC,
    MENU_IN_ENTER,
    MENU_IN_LEAVE,
    MENU_IN_CYCLE,      // the BOOT button's one "change": the next value, or enter
} menu_input_t;

// what the four oled lines show, also for the console
#define MENU_LINES  4
#define MENU_COLS   21

#ifdef HAS_MENU

void menu_init(void);

/**
 * @brief Open the menu from a keyboard hold, redraw. Main loop.
 */
void menu_task(void);

bool menu_is_open(void);

/**
 * @brief One abstract input (the console, and the adapters below).
 */
void menu_input(menu_input_t input);

/**
 * @brief A usb or bluetooth keyboard report (input_bridge.c), before it goes
 *        anywhere else.
 *
 * @return true if the menu took it: do not forward
 */
bool menu_keyboard_hid(uint8_t slot, hid_keyboard_report_t const *report);

/**
 * @brief A key from the real amiga keyboard (keyboard_recv.c).
 *
 * @return true if the menu took it: do not forward
 */
bool menu_keyboard_amiga(uint8_t keycode, bool up);

/**
 * @brief A BOOT button event.
 *
 * @return true if the menu took it
 */
bool menu_button(user_button_event_t event);

/**
 * @brief Encoder: a detent (+1 clockwise, -1 anticlockwise).
 */
void menu_encoder_turn(int8_t direction);

/**
 * @brief Encoder switch pressed (true) or released (false).
 */
void menu_encoder_switch(bool down);

/**
 * @brief The text of the four lines, as the oled shows them (empty when closed).
 */
void menu_lines(char lines[MENU_LINES][MENU_COLS + 1]);

#else

static inline void menu_init(void) {}
static inline void menu_task(void) {}
static inline bool menu_is_open(void) { return false; }
static inline void menu_input(menu_input_t input) { (void)input; }
static inline bool menu_keyboard_hid(uint8_t slot, hid_keyboard_report_t const *report) { (void)slot; (void)report; return false; }
static inline bool menu_keyboard_amiga(uint8_t keycode, bool up) { (void)keycode; (void)up; return false; }
static inline bool menu_button(user_button_event_t event) { (void)event; return false; }
static inline void menu_encoder_turn(int8_t direction) { (void)direction; }
static inline void menu_encoder_switch(bool down) { (void)down; }

#endif // HAS_MENU

#endif // _MENU_H
