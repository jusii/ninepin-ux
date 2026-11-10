/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * cd32 pad output. ported from stefan reinauer's cd32.c (reinauer/amigahid-
 * pico, features branch, EPL-2.0; protocol reference: amiga test kit,
 * testkit/joymouse.c read_gamepad()). changes: the pio and its state machines
 * belong to de9.c, which claims all of pio1 at boot, and the programs load and
 * unload with the mode, so pio1 can hold the c64 1351 program instead.
 */

#include "config.h"
#include "cd32.h"

#include "cd32.pio.h"
#include "pad.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"

static PIO const output_pio = pio1;
static unsigned data_offset, fire_offset;
static bool loaded;

static struct {
    unsigned clock, data, load, data_sm, fire_sm;
    uint16_t data_state, fire_state;
    bool enabled;
} ports[] = {
    { .clock = QM1_AMIGA_B1, .data = QM1_AMIGA_B2, .load = QM1_AMIGA_B3, .data_sm = 0, .fire_sm = 1 },
    { .clock = QM2_AMIGA_B1, .data = QM2_AMIGA_B2, .load = QM2_AMIGA_B3, .data_sm = 2, .fire_sm = 3 },
};

#define PORT_COUNT (sizeof(ports) / sizeof(ports[0]))

void cd32_load(void)
{
    if (loaded)
        return;

    data_offset = pio_add_program(output_pio, &cd32_data_program);
    fire_offset = pio_add_program(output_pio, &cd32_fire_program);
    for (unsigned port = 0; port < PORT_COUNT; port++) {
        ports[port].data_state = ports[port].fire_state = UINT16_MAX;
        ports[port].enabled = false;
    }
    loaded = true;
}

void cd32_unload(void)
{
    if (!loaded)
        return;

    for (unsigned port = 0; port < PORT_COUNT; port++)
        cd32_enable(port, false);
    pio_remove_program(output_pio, &cd32_data_program, data_offset);
    pio_remove_program(output_pio, &cd32_fire_program, fire_offset);
    loaded = false;
}

static void start_sm(unsigned sm, unsigned offset, unsigned pin, unsigned load, unsigned clock, bool serial)
{
    pio_sm_config config;

    pio_sm_set_enabled(output_pio, sm, false);
    config = serial ? cd32_data_program_get_default_config(offset) : cd32_fire_program_get_default_config(offset);
    sm_config_set_out_pins(&config, pin, 1);
    sm_config_set_set_pins(&config, pin, 1);
    sm_config_set_jmp_pin(&config, load);
    sm_config_set_in_pins(&config, clock);
    sm_config_set_in_shift(&config, false, false, 32);
    sm_config_set_out_shift(&config, true, false, serial ? 8 : 32);
    pio_sm_init(output_pio, sm, offset, &config);
    pio_sm_set_pins_with_mask(output_pio, sm, 0, 1u << pin);
    pio_sm_set_pindirs_with_mask(output_pio, sm, 0, 1u << pin);
    pio_sm_exec(output_pio, sm, pio_encode_set(pio_x, 0));
    pio_sm_put(output_pio, sm, 0);
    pio_gpio_init(output_pio, pin);
    pio_sm_set_enabled(output_pio, sm, true);
}

void cd32_enable(unsigned port, bool enabled)
{
    unsigned clock, data;

    if ((port >= PORT_COUNT) || !loaded || (ports[port].enabled == enabled))
        return;

    clock = ports[port].clock;
    data = ports[port].data;
    gpio_set_dir(ports[port].load, GPIO_IN);

    if (enabled) {
        // hand over released pins. pio drives data high only during serial
        // reads; fire/clock and ordinary joystick outputs remain open-drain
        gpio_set_dir(clock, GPIO_IN);
        gpio_set_dir(data, GPIO_IN);
        start_sm(ports[port].data_sm, data_offset, data, ports[port].load, clock, true);
        start_sm(ports[port].fire_sm, fire_offset, clock, ports[port].load, clock, false);
    } else {
        pio_sm_set_enabled(output_pio, ports[port].data_sm, false);
        pio_sm_set_enabled(output_pio, ports[port].fire_sm, false);
        gpio_set_function(clock, GPIO_FUNC_SIO);
        gpio_set_function(data, GPIO_FUNC_SIO);
        gpio_set_dir(clock, GPIO_IN);
        gpio_set_dir(data, GPIO_IN);
    }

    ports[port].enabled = enabled;
    ports[port].data_state = ports[port].fire_state = UINT16_MAX;
}

void cd32_update(unsigned port, uint16_t state)
{
    uint32_t serial;

    if ((port >= PORT_COUNT) || !ports[port].enabled)
        return;

    // wire order: blue, red, yellow, green, forward, rewind, play, released id
    serial = ((state & PAD_FIRE2) ? 1u : 0u) | ((state & PAD_FIRE) ? 2u : 0u)
        | ((state & PAD_FIRE4) ? 4u : 0u) | ((state & PAD_FIRE3) ? 8u : 0u)
        | ((state & PAD_R) ? 16u : 0u) | ((state & PAD_L) ? 32u : 0u)
        | ((state & PAD_PLAY) ? 64u : 0u);

    if ((state != ports[port].data_state) && !pio_sm_is_tx_fifo_full(output_pio, ports[port].data_sm)) {
        pio_sm_put(output_pio, ports[port].data_sm, serial);
        ports[port].data_state = state;
    }
    if ((state != ports[port].fire_state) && !pio_sm_is_tx_fifo_full(output_pio, ports[port].fire_sm)) {
        pio_sm_put(output_pio, ports[port].fire_sm, (state & PAD_FIRE) ? 1u : 0u);
        ports[port].fire_state = state;
    }
}

