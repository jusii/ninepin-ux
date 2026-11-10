/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the cyw43 radio, shared by bluetooth and wi-fi; see radio.h.
 */

#include "radio.h"

#include "pico/cyw43_arch.h"

#include "util/output.h"

static enum { RADIO_DOWN, RADIO_UP, RADIO_FAILED } radio_state;

bool radio_init(void)
{
    if (radio_state == RADIO_DOWN) {
        if (cyw43_arch_init() == 0) {
            radio_state = RADIO_UP;
        } else {
            ahprintf("[radio] cyw43 init failed\n");
            radio_state = RADIO_FAILED;
        }
    }

    return radio_state == RADIO_UP;
}
