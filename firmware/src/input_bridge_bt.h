/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * bluetooth-safe declarations for shared hid input bridging.
 */

#ifndef _INPUT_BRIDGE_BT_H
#define _INPUT_BRIDGE_BT_H

#include <stdint.h>

#include "tusb_config.h"

#define INPUT_BRIDGE_BT_CLASSIC_SLOTS 2
#define INPUT_BRIDGE_BT_LE_SLOTS      1

// one slot for a physically attached real amiga keyboard, which keyboard_recv.c
// fills when it forwards to the usb-c device port. reserved on every board; it
// just stays empty where there is no keyboard input or no device port
#define INPUT_BRIDGE_REAL_KBD_SLOTS   1

#define INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE CFG_TUH_HID
#define INPUT_BRIDGE_BT_LE_SLOT_BASE      (INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE + INPUT_BRIDGE_BT_CLASSIC_SLOTS)
#define INPUT_BRIDGE_BT_SLOT_END          (INPUT_BRIDGE_BT_LE_SLOT_BASE + INPUT_BRIDGE_BT_LE_SLOTS)
#define INPUT_BRIDGE_REAL_KBD_SLOT        INPUT_BRIDGE_BT_SLOT_END

// one slot for network input (kvmd_api.c: a keyboard and a mouse driven over
// wi-fi); reserved on every board, empty without wi-fi
#define INPUT_BRIDGE_NET_SLOTS        1
#define INPUT_BRIDGE_NET_SLOT         (INPUT_BRIDGE_REAL_KBD_SLOT + INPUT_BRIDGE_REAL_KBD_SLOTS)

// one slot for the keys pad buttons stand for (pads.c, the pad combos)
#define INPUT_BRIDGE_PAD_KEY_SLOT     (INPUT_BRIDGE_NET_SLOT + INPUT_BRIDGE_NET_SLOTS)
#define INPUT_BRIDGE_MAX_SLOTS        (INPUT_BRIDGE_PAD_KEY_SLOT + 1)

void input_bridge_reset(uint8_t slot);
void input_bridge_disconnect(uint8_t slot);
void input_bridge_handle_keyboard_boot(uint8_t slot, uint8_t modifier, uint8_t const keycode[6]);
void input_bridge_handle_mouse_boot(uint8_t slot, uint8_t buttons, int8_t x, int8_t y, int8_t wheel);

#endif // _INPUT_BRIDGE_BT_H
