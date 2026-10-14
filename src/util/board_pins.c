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

#if defined(BOARD_PULLUP_PINS) || defined(HAS_USB_MUX) || defined(HAS_AMIGA_SENSE)

#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#ifdef BOARD_PULLUP_PINS
static const uint8_t board_pullup_pins[] = BOARD_PULLUP_PINS;
#endif

void board_pins_init(void)
{
#ifdef BOARD_PULLUP_PINS
    // every one of these is an open-drain line the drivers only ever pull low
    // or release, so released (sio input) plus pull-up is the idle state.
    // gpio_init() also lifts the rp2350's pad isolation, without which the
    // pull-up setting would not reach the pad.
    for (unsigned int i = 0; i < count_of(board_pullup_pins); i++) {
        gpio_init(board_pullup_pins[i]);
        gpio_pull_up(board_pullup_pins[i]);
    }
#endif

#ifdef HAS_USB_MUX
    // the board's pull-down already holds the mux on the device port through
    // reset; drive it there too until the host stack is ready for the switch
    gpio_init(USB_MUX_SEL);
    gpio_put(USB_MUX_SEL, !USB_MUX_SEL_HOST);
    gpio_set_dir(USB_MUX_SEL, GPIO_OUT);
#endif

#ifdef HAS_AMIGA_SENSE
    // what adc_gpio_init() does, without pulling in hardware_adc before there
    // is a reader: no function, no pulls (the reset pull-down would load the
    // 100k divider), digital input off
    gpio_set_function(AMIGA_SENSE_PIN, GPIO_FUNC_NULL);
    gpio_disable_pulls(AMIGA_SENSE_PIN);
    gpio_set_input_enabled(AMIGA_SENSE_PIN, false);
#endif
}

void board_pins_usb_host(void)
{
#ifdef HAS_USB_MUX
    gpio_put(USB_MUX_SEL, USB_MUX_SEL_HOST);
#endif
}

#endif
