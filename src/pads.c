/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the player manager: pads become players by connection order, players become
 * outputs (de9 joystick / cd32 ports, or the usb-c gamepads), all under the
 * settings. the button mapping and deadzone policy follow stefan reinauer's
 * gamepad_config.c (reinauer/amigahid-pico, features branch, EPL-2.0); the
 * players, the touchpad mouse and the menu hooks are this tree's own.
 */

#include "pad.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "input_bridge.h"
#include "menu.h"
#include "settings.h"
#include "util/output.h"
#include "util/run_mode.h"

#define PADS_PLAYERS        2
#define PADS_HOLD_MS        30000   // a player's place waits this long for its pad
#define PADS_MENU_HOLD_MS   1000    // home held this long opens the menu

typedef struct {
    bool present;
    bool quarantine;                // after a release: ignored until at rest
    uint32_t identity;
    char name[PAD_NAME_LEN];
    pad_player_hook_t hook;
    pad_input_t input;
    uint16_t state;                 // digital, under the settings
    uint32_t home_since;            // 0: not held
    uint8_t touch_buttons;
} pad_slot_t;

typedef struct {
    int16_t slot;                   // -1: none
    uint32_t identity;
    bool connected;
    uint32_t gone_since;
} player_place_t;

static pad_slot_t slots[INPUT_BRIDGE_MAX_SLOTS];
static player_place_t places[PADS_PLAYERS];
static pad_player_t out[PADS_PLAYERS];
static int16_t latest_slot = -1;    // the pad that reported last (merge, usb)

// what went out last, so outputs only see changes
static uint16_t sent_de9[PADS_PLAYERS];
static pad_player_t sent_usb[PADS_PLAYERS];
static bool usb_routed;

void __attribute__((weak)) pads_output_de9(uint8_t player, uint16_t state)
{
    (void)player;
    (void)state;
}

void __attribute__((weak)) pads_output_usb(uint8_t player, pad_player_t const *source)
{
    (void)player;
    (void)source;
}

static uint32_t now_ms(void)
{
    uint32_t ms = to_ms_since_boot(get_absolute_time());
    return ms ? ms : 1;
}

//--------------------------------------------------------------------+
// one pad -> digital state
//--------------------------------------------------------------------+

static uint8_t stick_directions(int8_t x, int8_t y)
{
    static uint8_t const zones[3] = { 25, 50, 75 };   // percent of full throw
    int threshold = (127 * zones[settings_get()->pad_deadzone % 3]) / 100;
    uint8_t dirs = 0;

    if (x < -threshold) dirs |= PAD_LEFT;
    if (x > threshold) dirs |= PAD_RIGHT;
    if (y < -threshold) dirs |= PAD_UP;
    if (y > threshold) dirs |= PAD_DOWN;
    return dirs;
}

static uint16_t pad_digital(pad_input_t const *in)
{
    settings_t const *s = settings_get();
    uint16_t state = 0;

    if (s->pad_directions != SETTINGS_PAD_DIR_STICK)
        state |= in->dpad;
    if (s->pad_directions != SETTINGS_PAD_DIR_DPAD)
        state |= stick_directions(in->lx, in->ly);

    // opposite directions cancel
    if ((state & (PAD_UP | PAD_DOWN)) == (PAD_UP | PAD_DOWN))
        state &= (uint16_t)~(PAD_UP | PAD_DOWN);
    if ((state & (PAD_LEFT | PAD_RIGHT)) == (PAD_LEFT | PAD_RIGHT))
        state &= (uint16_t)~(PAD_LEFT | PAD_RIGHT);

    for (unsigned i = 0; i < PAD_LOGICAL_BUTTONS; i++) {
        unsigned own = s->pad_buttons[i] ? s->pad_buttons[i] : in->defaults[i];

        if ((own >= 1) && (own <= 16) && (in->buttons & (1u << (own - 1))))
            state |= (uint16_t)(PAD_FIRE << i);
    }

    return state;
}

//--------------------------------------------------------------------+
// players and outputs
//--------------------------------------------------------------------+

static void notify_player(uint8_t slot, int8_t player)
{
    if (slots[slot].hook)
        slots[slot].hook(slot, player);
}

static int8_t player_of(uint8_t slot)
{
    for (unsigned p = 0; p < PADS_PLAYERS; p++)
        if (places[p].connected && (places[p].slot == slot))
            return (int8_t)p;
    return -1;
}

static void route(void)
{
    settings_t const *s = settings_get();
    bool to_usb = run_mode_pads_usb(run_mode_get());
    bool captured = menu_is_open();
    pad_player_t next[PADS_PLAYERS];

    memset(next, 0, sizeof(next));

    if (s->pad_merge) {
        // every pad drives player 1; its report (for a usb host) is the last one
        for (unsigned i = 0; i < INPUT_BRIDGE_MAX_SLOTS; i++) {
            if (slots[i].present && !slots[i].quarantine) {
                next[0].active = true;
                next[0].state |= slots[i].state;
            }
        }
        if (next[0].active && (latest_slot >= 0) && slots[latest_slot].present) {
            next[0].input = slots[latest_slot].input;
            snprintf(next[0].name, sizeof(next[0].name), "%s", slots[latest_slot].name);
        }
    } else {
        for (unsigned p = 0; p < PADS_PLAYERS; p++) {
            int16_t slot = places[p].slot;

            if (!places[p].connected || (slot < 0) || !slots[slot].present)
                continue;
            next[p].active = true;
            next[p].state = slots[slot].quarantine ? 0 : slots[slot].state;
            next[p].input = slots[slot].input;
            snprintf(next[p].name, sizeof(next[p].name), "%s", slots[slot].name);
        }
    }

    if (s->pad_swap) {
        pad_player_t t = next[0];
        next[0] = next[1];
        next[1] = t;
    }

    for (unsigned p = 0; p < PADS_PLAYERS; p++) {
        if (captured) {
            next[p].state = 0;
            memset(&next[p].input, 0, sizeof(next[p].input));
            next[p].input.hat = PAD_HAT_CENTRED;
        }
        out[p] = next[p];
    }

    // a mode switch between the de9 ports and usb lets go of the side left
    if (to_usb != usb_routed) {
        for (unsigned p = 0; p < PADS_PLAYERS; p++) {
            if (usb_routed) {
                memset(&sent_usb[p], 0, sizeof(sent_usb[p]));
                sent_usb[p].input.hat = PAD_HAT_CENTRED;
                pads_output_usb((uint8_t)p, &sent_usb[p]);
            } else if (sent_de9[p]) {
                pads_output_de9((uint8_t)p, 0);
                sent_de9[p] = 0;
            }
        }
        usb_routed = to_usb;
    }

    for (unsigned p = 0; p < PADS_PLAYERS; p++) {
        if (to_usb) {
            if (memcmp(&out[p], &sent_usb[p], sizeof(out[p])) != 0) {
                pads_output_usb((uint8_t)p, &out[p]);
                sent_usb[p] = out[p];
            }
        } else if (out[p].state != sent_de9[p]) {
            pads_output_de9((uint8_t)p, out[p].state);
            sent_de9[p] = out[p].state;
        }
    }
}

static void assign(uint8_t slot)
{
    uint32_t now = now_ms();
    int chosen = -1;

    // a place held for this very pad comes back to it
    for (unsigned p = 0; p < PADS_PLAYERS; p++) {
        if ((places[p].slot >= 0) && !places[p].connected && (places[p].identity == slots[slot].identity)
            && ((uint32_t)(now - places[p].gone_since) < PADS_HOLD_MS)) {
            chosen = (int)p;
            break;
        }
    }

    // else the first free place (never used, or its hold has run out)
    for (unsigned p = 0; (chosen < 0) && (p < PADS_PLAYERS); p++) {
        if ((places[p].slot < 0)
            || (!places[p].connected && ((uint32_t)(now - places[p].gone_since) >= PADS_HOLD_MS)))
            chosen = (int)p;
    }

    if (chosen < 0) {
        ahprintf("[pads] %s: both players taken\n", slots[slot].name);
        notify_player(slot, -1);
        return;
    }

    places[chosen].slot = slot;
    places[chosen].identity = slots[slot].identity;
    places[chosen].connected = true;
    ahprintf("[pads] %s -> player %d\n", slots[slot].name, chosen + 1);
    notify_player(slot, (int8_t)chosen);
}

void pads_init(void)
{
    memset(slots, 0, sizeof(slots));
    memset(out, 0, sizeof(out));
    for (unsigned p = 0; p < PADS_PLAYERS; p++) {
        places[p].slot = -1;
        places[p].connected = false;
    }
    usb_routed = run_mode_pads_usb(run_mode_get());
}

void pads_connected(uint8_t slot, uint32_t identity, char const *name, pad_player_hook_t hook)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    memset(&slots[slot], 0, sizeof(slots[slot]));
    slots[slot].present = true;
    slots[slot].identity = identity;
    slots[slot].hook = hook;
    slots[slot].input.hat = PAD_HAT_CENTRED;
    snprintf(slots[slot].name, sizeof(slots[slot].name), "%s", name ? name : "pad");

    assign(slot);
    route();
}

void pads_disconnected(uint8_t slot)
{
    int8_t player;

    if ((slot >= INPUT_BRIDGE_MAX_SLOTS) || !slots[slot].present)
        return;

    player = player_of(slot);
    if (player >= 0) {
        places[player].connected = false;
        places[player].gone_since = now_ms();
        ahprintf("[pads] %s gone; player %d kept for it 30 s\n", slots[slot].name, player + 1);
    }

    slots[slot].present = false;
    if (latest_slot == slot)
        latest_slot = -1;
    route();
}

/**
 * the menu: home held opens it; whilst open the pads drive it.
 */
static void pad_menu(uint8_t slot, uint16_t before, uint16_t after)
{
    uint16_t pressed = after & (uint16_t)~before;

    if (!menu_is_open())
        return;

    if (pressed & PAD_UP)       menu_input(MENU_IN_PREV);
    if (pressed & PAD_DOWN)     menu_input(MENU_IN_NEXT);
    if (pressed & PAD_LEFT)     menu_input(MENU_IN_DEC);
    if (pressed & PAD_RIGHT)    menu_input(MENU_IN_INC);
    if (pressed & PAD_FIRE)     menu_input(MENU_IN_ENTER);
    if (pressed & PAD_FIRE2)    menu_input(MENU_IN_LEAVE);
    (void)slot;
}

void pads_report(uint8_t slot, pad_input_t const *input)
{
    pad_slot_t *pad;
    uint16_t before;

    if ((slot >= INPUT_BRIDGE_MAX_SLOTS) || !slots[slot].present || !input)
        return;

    pad = &slots[slot];
    before = pad->state;
    pad->input = *input;
    pad->state = pad_digital(input);
    latest_slot = slot;

    // home held: open the menu (pads_task() times it)
    if (input->home) {
        if (!pad->home_since)
            pad->home_since = now_ms();
    } else {
        pad->home_since = 0;
    }

    pad_menu(slot, before, pad->state);

    // a pad let go of everything leaves quarantine
    if (pad->quarantine && !pad->state && !input->home)
        pad->quarantine = false;

    // the touchpad as a mouse, through the same path as any mouse
    if (input->has_touchpad && settings_get()->pad_touchpad && !menu_is_open()
        && (input->touch_dx || input->touch_dy || (input->touch_buttons != pad->touch_buttons))) {
        hid_mouse_report_t mouse;

        memset(&mouse, 0, sizeof(mouse));
        mouse.buttons = input->touch_buttons;
        mouse.x = input->touch_dx;
        mouse.y = input->touch_dy;
        pad->touch_buttons = input->touch_buttons;
        input_bridge_handle_mouse(slot, &mouse);
    }

    route();
}

void pads_release_all(void)
{
    for (unsigned i = 0; i < INPUT_BRIDGE_MAX_SLOTS; i++) {
        if (slots[i].present)
            slots[i].quarantine = true;
        slots[i].home_since = 0;
    }
    route();
}

void pads_task(void)
{
    uint32_t now = now_ms();

    for (unsigned i = 0; i < INPUT_BRIDGE_MAX_SLOTS; i++) {
        if (slots[i].present && slots[i].home_since && !menu_is_open()
            && ((uint32_t)(now - slots[i].home_since) >= PADS_MENU_HOLD_MS)) {
            slots[i].home_since = 0;
            menu_input(MENU_IN_OPEN);
        }
    }

    // keep the outputs current: the menu closing, a mode switch or a settings
    // save change them without any pad reporting
    route();
}

pad_player_t const *pads_player(uint8_t player)
{
    return (player < PADS_PLAYERS) ? &out[player] : NULL;
}

void pads_describe(void)
{
    settings_t const *s = settings_get();
    unsigned count = 0;

    for (unsigned i = 0; i < INPUT_BRIDGE_MAX_SLOTS; i++) {
        if (!slots[i].present)
            continue;
        count++;
        ahprintf("slot %u: %s, player %d, state 0x%03x%s, lx %d ly %d hat %u buttons 0x%04x\n",
            i, slots[i].name, player_of((uint8_t)i) + 1, slots[i].state,
            slots[i].quarantine ? " (released)" : "", slots[i].input.lx, slots[i].input.ly,
            slots[i].input.hat, slots[i].input.buttons);
    }
    if (!count)
        ahprintf("no pads\n");

    for (unsigned p = 0; p < PADS_PLAYERS; p++)
        ahprintf("player %u: %s%s, state 0x%03x\n", p + 1, out[p].active ? out[p].name : "-",
            ((p == 0) && s->pad_merge) ? " (all pads merged)" : "", out[p].state);
    ahprintf("swap %u, merge %u, out to %s\n", s->pad_swap, s->pad_merge,
        run_mode_pads_usb(run_mode_get()) ? "usb-c" : "de9 ports");
}
