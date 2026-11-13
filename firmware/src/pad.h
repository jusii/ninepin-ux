/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * game pads: the record every pad decoder fills in (usb generic hid, the
 * playstation pads, bluetooth), and the player manager that turns pads into
 * players and players into outputs (pads.c).
 *
 * the digital state bits follow stefan reinauer's hid_gamepad.h
 * (reinauer/amigahid-pico, features branch), so his decoders and cd32 output
 * fit unchanged.
 */

#ifndef _PAD_H
#define _PAD_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

// digital state of one player, as the de9 ports want it
enum pad_state {
    PAD_UP      = 1u << 0,
    PAD_DOWN    = 1u << 1,
    PAD_LEFT    = 1u << 2,
    PAD_RIGHT   = 1u << 3,
    PAD_FIRE    = 1u << 4,      // fire 1 / cd32 red
    PAD_FIRE2   = 1u << 5,      // fire 2 / cd32 blue
    PAD_FIRE3   = 1u << 6,      // fire 3 / cd32 green
    PAD_FIRE4   = 1u << 7,      // cd32 yellow; on a joystick the triangle action
    PAD_L       = 1u << 8,
    PAD_R       = 1u << 9,
    PAD_PLAY    = 1u << 10,
    PAD_DIRECTIONS = 0x00fu,
    PAD_BUTTONS = 0x7f0u,
};

// the seven logical buttons, in pad_buttons[] / defaults[] order
#define PAD_LOGICAL_BUTTONS 7

#define PAD_HAT_CENTRED 8

/**
 * one report from one pad, normalised. a decoder fills in what its pad has and
 * leaves the rest zero (hat: PAD_HAT_CENTRED).
 */
typedef struct {
    int8_t lx, ly, rx, ry;          // sticks, -127 (left / up) .. 127
    uint8_t lt, rt;                 // analog triggers, 0 .. 255
    uint8_t hat;                    // 0 = up, clockwise to 7 = up-left; PAD_HAT_CENTRED
    uint8_t dpad;                   // PAD_UP/DOWN/LEFT/RIGHT from a hat or d-pad buttons
    uint16_t buttons;               // the pad's own buttons 1..16 in bits 0..15
    bool home;                      // ps / guide button
    int8_t touch_dx, touch_dy;      // touchpad motion since the last report
    uint8_t touch_buttons;          // bit 0 left half clicked, bit 1 right half
    bool has_touchpad;
    uint8_t defaults[PAD_LOGICAL_BUTTONS]; // own button (1..16) for fire 1, 2, 3, yellow, l, r, play
} pad_input_t;

/**
 * a pad's own hook for showing its player number (the dualsense's player leds
 * and light bar); player 0 or 1, or -1 for none.
 */
typedef void (*pad_player_hook_t)(uint8_t slot, int8_t player);

#define PAD_NAME_LEN 16

/**
 * how a player is wired out: one digital state for the de9 ports, and the
 * pad's own report for a usb host.
 */
typedef struct {
    bool active;                    // a pad is assigned and connected
    uint16_t state;                 // digital, pad_state bits
    pad_input_t input;              // the latest report (usb passthrough)
    char name[PAD_NAME_LEN];
} pad_player_t;

void pads_init(void);

/**
 * @brief A pad appeared in an input slot (usb hid or bluetooth). Main loop.
 *
 * @param identity  stays the same when the same pad comes back (usb vid:pid,
 *                  a bluetooth address hash): a player's place is kept for it
 *                  for 30 s after a disconnect
 */
void pads_connected(uint8_t slot, uint32_t identity, char const *name, pad_player_hook_t hook);

void pads_disconnected(uint8_t slot);

/**
 * @brief A report from a connected pad. Main loop.
 */
void pads_report(uint8_t slot, pad_input_t const *input);

/**
 * @brief Expire held places, autofire, keep outputs current. Main loop.
 */
void pads_task(void);

/**
 * @brief Let go of every output (menu opening, mode switch); a pad then counts
 *        again only once it is back at rest.
 */
void pads_release_all(void);

/**
 * @return the player wired out as player 0 or 1 (after swap and merge)
 */
pad_player_t const *pads_player(uint8_t player);

/**
 * @brief Describe the pads and players for the console.
 */
void pads_describe(void);

/**
 * outputs, filled in by the de9 port and usb device code; weak no-ops until
 * those exist. called from the main loop whenever a player's output changes.
 */
void pads_output_de9(uint8_t player, uint16_t state);
void pads_output_usb(uint8_t player, pad_player_t const *source);

/**
 * @brief Home with the d-pad left or right: on to the previous / next saved
 *        pad profile (apply.c; a weak no-op in pads.c).
 */
void pads_profile_step(int step);

#endif // _PAD_H
