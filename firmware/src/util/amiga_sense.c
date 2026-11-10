/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * amiga-on sense reader.
 */

#include "config.h"
#include "util/amiga_sense.h"

#ifdef HAS_AMIGA_SENSE

#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"

#include "util/output.h"

#if AMIGA_SENSE_OFF_MV >= AMIGA_SENSE_ON_MV
#  error AMIGA_SENSE_OFF_MV must be below AMIGA_SENSE_ON_MV
#endif

// one averaged reading every this many milliseconds
#define AMIGA_SENSE_PERIOD_MS   50

// samples per reading; the 100n on the divider already filters, this only
// takes the adc's own noise out
#define AMIGA_SENSE_SAMPLES     8

static bool sense_on;
static uint16_t sense_mv;
static uint32_t sense_last_ms;

static uint16_t amiga_sense_read_mv(void)
{
    uint32_t sum = 0;

    // nothing else uses the adc, but select every time rather than rely on it
    adc_select_input(AMIGA_SENSE_ADC);

    for (unsigned int i = 0; i < AMIGA_SENSE_SAMPLES; i++)
        sum += adc_read();

    // 12-bit result against adc_avdd
    return (uint16_t)(((sum / AMIGA_SENSE_SAMPLES) * AMIGA_SENSE_VREF_MV) / 4096u);
}

void amiga_sense_init(void)
{
    adc_init();

    // function null, pulls off and the digital input disabled (board_pins_init()
    // has already done the same; this is the adc driver's own claim on the pad)
    adc_gpio_init(AMIGA_SENSE_PIN);

    sense_mv = amiga_sense_read_mv();
    sense_on = sense_mv >= AMIGA_SENSE_ON_MV;
    sense_last_ms = to_ms_since_boot(get_absolute_time());

    ahprintf("[sense] amiga i/o rail %u mV at the adc: amiga %s\n", sense_mv, sense_on ? "on" : "off");
}

bool amiga_sense_task(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool was_on = sense_on;

    if ((uint32_t)(now - sense_last_ms) < AMIGA_SENSE_PERIOD_MS)
        return false;

    sense_last_ms = now;
    sense_mv = amiga_sense_read_mv();

    if (!sense_on && (sense_mv >= AMIGA_SENSE_ON_MV))
        sense_on = true;
    else if (sense_on && (sense_mv < AMIGA_SENSE_OFF_MV))
        sense_on = false;

    if (sense_on == was_on)
        return false;

    ahprintf("[sense] amiga i/o rail %u mV at the adc: amiga %s\n", sense_mv, sense_on ? "on" : "off");
    return true;
}

bool amiga_sense_is_on(void)
{
    return sense_on;
}

uint16_t amiga_sense_millivolts(void)
{
    return sense_mv;
}

#endif // HAS_AMIGA_SENSE
