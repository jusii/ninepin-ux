/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * board-level pad setup which belongs to no single driver.
 */

#include "config.h"
#include "util/board_pins.h"

#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

static const uint8_t board_pullup_pins[] = BOARD_PULLUP_PINS;

void board_pins_init(void)
{
    // every one of these is an open-drain line the drivers only ever pull low
    // or release, so released (sio input) plus pull-up is the idle state.
    // gpio_init() also lifts the rp2350's pad isolation, without which the
    // pull-up setting would not reach the pad.
    for (unsigned int i = 0; i < count_of(board_pullup_pins); i++) {
        gpio_init(board_pullup_pins[i]);
        gpio_pull_up(board_pullup_pins[i]);
    }

    // park the pad as adc_gpio_init() would, from the first moment: no
    // function, no pulls (the reset pull-down would load the 100k divider),
    // digital input off. util/amiga_sense.c claims it properly with
    // hardware_adc once main() starts the reader.
    gpio_set_function(AMIGA_SENSE_PIN, GPIO_FUNC_NULL);
    gpio_disable_pulls(AMIGA_SENSE_PIN);
    gpio_set_input_enabled(AMIGA_SENSE_PIN, false);
}

