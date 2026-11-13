/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the two de9 controller ports; see de9.h. the joystick line handling and the
 * pio1 reservation follow stefan reinauer's quad_mouse.c / cd32.c joystick
 * output (reinauer/amigahid-pico, features branch, EPL-2.0); the roles per
 * mode, the c64 outputs and the 1351 are this tree's own.
 */

#include "config.h"
#include "de9.h"

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"

#include "c64_pot.pio.h"
#include "cd32.h"
#include "pad.h"
#include "quad_mouse.h"
#include "settings.h"
#include "util/output.h"
#include "util/run_mode.h"

#define DE9_PORTS           2
#define DE9_AUTOFIRE_MS     50      // half a period: 10 shots a second

// 1351: the sid reads 64 + 2 * (position & 63), plus the 256 us of discharge
#define C64_POT_DISCHARGE_US    256
#define C64_POT_BASE            64

typedef enum {
    ROLE_OFF = 0,
    ROLE_MOUSE,                     // amiga quadrature mouse (quad_mouse.c)
    ROLE_JOYSTICK,                  // amiga joystick
    ROLE_CD32,                      // amiga cd32 pad
    ROLE_C64_JOYSTICK,
    ROLE_C64_1351,
} de9_role_t;

static char const *const role_names[] = { "off", "mouse", "joystick", "cd32 pad", "c64 joystick", "1351 mouse" };

typedef struct {
    uint8_t up, down, left, right, fire, fire2, fire3;
} de9_pins_t;

// joystick wiring: up pin 1 (v), down 2 (h), left 3 (vq), right 4 (hq),
// fire 6 (b1), fire 2 on pin 9 (b2, pot x), fire 3 on pin 5 (b3, pot y)
static de9_pins_t const pins[DE9_PORTS] = {
    { QM1_AMIGA_V, QM1_AMIGA_H, QM1_AMIGA_VQ, QM1_AMIGA_HQ, QM1_AMIGA_B1, QM1_AMIGA_B2, QM1_AMIGA_B3 },
    { QM2_AMIGA_V, QM2_AMIGA_H, QM2_AMIGA_VQ, QM2_AMIGA_HQ, QM2_AMIGA_B1, QM2_AMIGA_B2, QM2_AMIGA_B3 },
};

static PIO const de9_pio = pio1;
static de9_role_t roles[DE9_PORTS];
static uint16_t player_state[2];     // latest digital state per player
static bool autofire_on;
static uint32_t autofire_at;

static bool pot_loaded;
static unsigned pot_offset;
static uint8_t pos_1351[DE9_PORTS][2];   // x, y counters (6 bits used)
static bool sm_running[4];

//--------------------------------------------------------------------+
// lines
//--------------------------------------------------------------------+

// open-drain: low when active, released (pulled up on the board) when not
static void line(uint pin, bool low)
{
    if (low) {
        gpio_put(pin, 0);
        gpio_set_dir(pin, GPIO_OUT);
    } else {
        gpio_set_dir(pin, GPIO_IN);
    }
}

static void lines_idle(unsigned port)
{
    uint8_t const *pin = &pins[port].up;

    for (unsigned i = 0; i < sizeof(de9_pins_t); i++) {
        gpio_set_function(pin[i], GPIO_FUNC_SIO);
        gpio_put(pin[i], 0);
        gpio_set_dir(pin[i], GPIO_IN);
    }
}

static int8_t player_for_port(unsigned port)
{
    // player 1 on port 2, player 2 on port 1
    return (port == 1) ? 0 : 1;
}

static uint16_t with_yellow(uint16_t state)
{
    if (!(state & PAD_FIRE4))
        return state;

    switch (settings_get()->pad_triangle) {
        case SETTINGS_PAD_TRIANGLE_UP:
            if (!(state & PAD_DOWN))
                state |= PAD_UP;
            break;
        case SETTINGS_PAD_TRIANGLE_AUTOFIRE:
            if (autofire_on)
                state |= PAD_FIRE;
            else
                state &= (uint16_t)~PAD_FIRE;
            break;
        default:
            break;
    }
    return state;
}

static void output_port(unsigned port)
{
    de9_pins_t const *p = &pins[port];
    uint16_t state = player_state[player_for_port(port)];

    switch (roles[port]) {
        case ROLE_JOYSTICK:
            state = with_yellow(state);
            line(p->up, state & PAD_UP);
            line(p->down, state & PAD_DOWN);
            line(p->left, state & PAD_LEFT);
            line(p->right, state & PAD_RIGHT);
            line(p->fire, state & PAD_FIRE);
            line(p->fire2, state & PAD_FIRE2);
            line(p->fire3, state & PAD_FIRE3);
            break;

        case ROLE_CD32:
            // directions stay plain lines; the buttons go through the pio
            line(p->up, state & PAD_UP);
            line(p->down, state & PAD_DOWN);
            line(p->left, state & PAD_LEFT);
            line(p->right, state & PAD_RIGHT);
            cd32_update(port, state);
            break;

        case ROLE_C64_JOYSTICK:
            state = with_yellow(state);
            line(p->up, state & PAD_UP);
            line(p->down, state & PAD_DOWN);
            line(p->left, state & PAD_LEFT);
            line(p->right, state & PAD_RIGHT);
            line(p->fire, state & PAD_FIRE);
            // the pot lines read "pressed" when released: hold them low unless
            // pressed
            line(p->fire2, !(state & PAD_FIRE2));
            line(p->fire3, !(state & PAD_FIRE3));
            break;

        default:
            break;
    }
}

//--------------------------------------------------------------------+
// the 1351
//--------------------------------------------------------------------+

static void pot_sms(unsigned port, unsigned *sm_x, unsigned *sm_y)
{
    *sm_x = port * 2;
    *sm_y = port * 2 + 1;
}

static void pot_push(unsigned port)
{
    unsigned sm_x, sm_y;
    uint32_t x = C64_POT_DISCHARGE_US + C64_POT_BASE + 2u * (pos_1351[port][0] & 63u);
    uint32_t y = C64_POT_DISCHARGE_US + C64_POT_BASE + 2u * (pos_1351[port][1] & 63u);

    pot_sms(port, &sm_x, &sm_y);
    if (!pio_sm_is_tx_fifo_full(de9_pio, sm_x))
        pio_sm_put(de9_pio, sm_x, x);
    if (!pio_sm_is_tx_fifo_full(de9_pio, sm_y))
        pio_sm_put(de9_pio, sm_y, y);
}

// a usb mouse counts several times finer than a 1351 did, and the c64 reads
// the 6-bit counters once a frame, so they must move less than half way round
// (32) between two reads. the motion is scaled down (the mouse speed setting
// picks by how much), kept as steps still owed, and paid out one a millisecond
static int32_t owed_1351[2];        // x, y: counter steps not yet made
static int16_t rest_1351[2];        // x, y: what the scaling left over
static uint32_t paid_1351_ms;

static void mouse_1351_motion(int16_t dx, int16_t dy)
{
    static int8_t const divisor[SETTINGS_MOUSE_SPEED_COUNT] = { 12, 8, 6, 4 };
    int div = divisor[settings_get()->mouse_speed % SETTINGS_MOUSE_SPEED_COUNT];
    int16_t const delta[2] = { dx, (int16_t)-dy };

    for (unsigned axis = 0; axis < 2; axis++) {
        int total = rest_1351[axis] + delta[axis];

        owed_1351[axis] += total / div;
        rest_1351[axis] = (int16_t)(total % div);
        // a pointer far behind the hand is worse than a lost step
        if (owed_1351[axis] > 400) owed_1351[axis] = 400;
        if (owed_1351[axis] < -400) owed_1351[axis] = -400;
    }
}

static void mouse_1351_task(uint32_t now_ms)
{
    uint32_t steps = now_ms - paid_1351_ms;

    if (!steps)
        return;
    paid_1351_ms = now_ms;
    if (steps > 4)
        steps = 4;      // a late loop pass does not jump the counters

    for (unsigned axis = 0; axis < 2; axis++) {
        int32_t step = owed_1351[axis];

        if (step > (int32_t)steps) step = (int32_t)steps;
        if (step < -(int32_t)steps) step = -(int32_t)steps;
        if (!step)
            continue;
        owed_1351[axis] -= step;
        for (unsigned port = 0; port < DE9_PORTS; port++)
            if (roles[port] == ROLE_C64_1351)
                pos_1351[port][axis] = (uint8_t)(pos_1351[port][axis] + step);
    }

    for (unsigned port = 0; port < DE9_PORTS; port++)
        if (roles[port] == ROLE_C64_1351)
            pot_push(port);
}

static void mouse_1351_button(enum amiga_quad_mouse_buttons button, bool pressed)
{
    for (unsigned port = 0; port < DE9_PORTS; port++) {
        if (roles[port] != ROLE_C64_1351)
            continue;
        // left on fire (pin 6), right on up (pin 1)
        if (button == AQM_LEFT)
            line(pins[port].fire, pressed);
        else if (button == AQM_RIGHT)
            line(pins[port].up, pressed);
    }
}

static void pot_load(void)
{
    if (!pot_loaded) {
        pot_offset = pio_add_program(de9_pio, &c64_pot_program);
        pot_loaded = true;
    }
}

static void pot_unload(void)
{
    if (!pot_loaded)
        return;
    for (unsigned sm = 0; sm < 4; sm++) {
        if (sm_running[sm]) {
            pio_sm_set_enabled(de9_pio, sm, false);
            sm_running[sm] = false;
        }
    }
    pio_remove_program(de9_pio, &c64_pot_program, pot_offset);
    pot_loaded = false;
}

static void pot_start(unsigned port)
{
    unsigned sm_x, sm_y;
    float clkdiv = (float)clock_get_hz(clk_sys) / 1000000.0f;

    pot_sms(port, &sm_x, &sm_y);
    c64_pot_program_init(de9_pio, sm_x, pot_offset, pins[port].fire2, clkdiv);
    c64_pot_program_init(de9_pio, sm_y, pot_offset, pins[port].fire3, clkdiv);
    sm_running[sm_x] = sm_running[sm_y] = true;
    pot_push(port);
}

static void pot_stop(unsigned port)
{
    unsigned sm_x, sm_y;

    pot_sms(port, &sm_x, &sm_y);
    pio_sm_set_enabled(de9_pio, sm_x, false);
    pio_sm_set_enabled(de9_pio, sm_y, false);
    sm_running[sm_x] = sm_running[sm_y] = false;
}

//--------------------------------------------------------------------+
// roles
//--------------------------------------------------------------------+

static de9_role_t role_for(unsigned port)
{
    settings_t const *s = settings_get();

    switch (run_mode_get()) {
        case RUN_MODE_AMIGA:
            switch (s->amiga_port[port]) {
                case SETTINGS_AMIGA_PORT_MOUSE:     return ROLE_MOUSE;
                case SETTINGS_AMIGA_PORT_JOYSTICK:  return ROLE_JOYSTICK;
                case SETTINGS_AMIGA_PORT_CD32:      return ROLE_CD32;
                default:                            return ROLE_OFF;
            }
        case RUN_MODE_C64:
            switch (s->c64_port[port]) {
                case SETTINGS_C64_PORT_JOYSTICK:    return ROLE_C64_JOYSTICK;
                case SETTINGS_C64_PORT_1351:        return ROLE_C64_1351;
                default:                            return ROLE_OFF;
            }
        default:
            return ROLE_OFF;
    }
}

bool de9_c64_has_1351(void)
{
    settings_t const *s = settings_get();

    return (s->c64_port[0] == SETTINGS_C64_PORT_1351) || (s->c64_port[1] == SETTINGS_C64_PORT_1351);
}

static void teardown(unsigned port)
{
    switch (roles[port]) {
        case ROLE_CD32:     cd32_enable(port, false); break;
        case ROLE_C64_1351: pot_stop(port); break;
        default:            break;
    }
    if (roles[port] != ROLE_MOUSE)
        lines_idle(port);
}

void de9_apply(void)
{
    de9_role_t next[DE9_PORTS];
    int8_t mouse_port = -1;
    bool any_cd32 = false, any_1351 = false;

    for (unsigned port = 0; port < DE9_PORTS; port++) {
        next[port] = role_for(port);
        // one quadrature mouse: the first port asking for it
        if (next[port] == ROLE_MOUSE) {
            if (mouse_port < 0)
                mouse_port = (int8_t)port;
            else
                next[port] = ROLE_OFF;
        }
        any_cd32 |= next[port] == ROLE_CD32;
        any_1351 |= next[port] == ROLE_C64_1351;
    }

    // take down whatever changes; the mouse lets go of its pins first
    for (unsigned port = 0; port < DE9_PORTS; port++)
        if ((roles[port] != next[port]) && (roles[port] != ROLE_MOUSE))
            teardown(port);

    if (!amiga_quad_mouse_port(mouse_port, 100000))
        ahprintf("[de9] the mouse core did not move ports\n");

    for (unsigned port = 0; port < DE9_PORTS; port++)
        if ((roles[port] == ROLE_MOUSE) && (next[port] != ROLE_MOUSE))
            lines_idle(port);

    // pio1 holds either the cd32 programs or the 1351's
    if (!any_1351)
        pot_unload();
    if (!any_cd32)
        cd32_unload();
    if (any_cd32)
        cd32_load();
    if (any_1351)
        pot_load();

    for (unsigned port = 0; port < DE9_PORTS; port++) {
        de9_role_t was = roles[port];

        roles[port] = next[port];
        if (was == next[port])
            continue;

        switch (next[port]) {
            case ROLE_CD32:     cd32_enable(port, true); break;
            case ROLE_C64_1351: pot_start(port); break;
            default:            break;
        }
    }

    // the mouse goes to the 1351 in c64 mode, else to the quadrature mouse
    amiga_quad_mouse_redirect(any_1351 ? mouse_1351_motion : NULL, any_1351 ? mouse_1351_button : NULL);

    for (unsigned port = 0; port < DE9_PORTS; port++)
        output_port(port);
}

void de9_init(void)
{
    // all of pio1: cd32 needs four state machines and most of the program
    // space, the 1351 two to four; claiming it now leaves cyw43 to pio0
    for (unsigned sm = 0; sm < 4; sm++)
        pio_sm_claim(de9_pio, sm);

    for (unsigned port = 0; port < DE9_PORTS; port++)
        roles[port] = (port == 0) ? ROLE_MOUSE : ROLE_OFF;   // as quad_mouse starts
    lines_idle(1);
    de9_apply();
}

void pads_output_de9(uint8_t player, uint16_t state)
{
    if (player > 1)
        return;

    player_state[player] = state;
    for (unsigned port = 0; port < DE9_PORTS; port++)
        if (player_for_port(port) == player)
            output_port(port);
}

void de9_task(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool yellow = (player_state[0] | player_state[1]) & PAD_FIRE4;

    if (pot_loaded)
        mouse_1351_task(now);

    if (!yellow || (settings_get()->pad_triangle != SETTINGS_PAD_TRIANGLE_AUTOFIRE))
        return;

    if ((uint32_t)(now - autofire_at) < DE9_AUTOFIRE_MS)
        return;

    autofire_at = now;
    autofire_on = !autofire_on;
    for (unsigned port = 0; port < DE9_PORTS; port++)
        output_port(port);
}

void de9_describe(void)
{
    for (unsigned port = 0; port < DE9_PORTS; port++)
        ahprintf("port %u: %s, player %d, state 0x%03x\n", port + 1, role_names[roles[port]],
            player_for_port(port) + 1, player_state[player_for_port(port)]);
    if (amiga_quad_mouse_get_port() < 0)
        ahprintf("mouse core on no port\n");
    else
        ahprintf("mouse core on port %d\n", amiga_quad_mouse_get_port() + 1);
}

