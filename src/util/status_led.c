/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * status led indicators.
 */

#include "config.h"
#include "util/status_led.h"

#ifdef HAS_STATUS_LEDS

#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#ifdef HAS_STATUS_WS2812
#include "hardware/pio.h"
#include "ws2812.pio.h" // generated at compile time
#endif

#ifndef HAS_STATUS_WS2812
// the leds sit on the 3v3 side behind a series resistor, so a high output
// lights them
#define STATUS_LED_ON   1
#define STATUS_LED_OFF  0
#else
// one rgb led stands in for both: led 1 ("am i usable") is the green channel
// and led 2 ("am i doing something") the blue one, so attached + idle is green,
// an input event flashes cyan and a pairing prompt blinks blue over it. kept
// dim; a 2020 package at full drive is uncomfortable to look at
#define STATUS_RGB_LEVEL    0x20

// the led latches a frame after 280us of idle line; never send faster than
// this, or two frames run together and the second lands in a led that is not
// there
#define STATUS_RGB_LATCH_US 300

static PIO rgb_pio;
static uint rgb_sm;
static uint rgb_offset;

// what the led currently shows, and when it was last sent
static bool rgb_painted;
static uint32_t rgb_painted_grb;
static uint64_t rgb_painted_at;
#endif

// led 1: half-period of the "nothing attached yet" heartbeat
#define STATUS_LED_HEARTBEAT_MS 500

// led 2: how long one forwarded input event lights the activity led. short
// enough that a burst of key events still reads as a flicker rather than a
// solid lamp, long enough to be visible at all
#define STATUS_LED_ACTIVITY_MS  40

// led 2: half-period of the pairing blink, deliberately faster than the
// heartbeat so the two cannot be confused
#define STATUS_LED_PAIRING_MS   120

// the bluetooth call sites run from the cyw43 background interrupt (see
// status_led.h), so everything a setter writes is volatile: the task must reread
// it rather than cache a copy from before the preemption
static volatile bool source_present[STATUS_LED_SOURCE_COUNT];

static uint32_t heartbeat_since;
static bool heartbeat_lit;

static volatile bool activity_pending;
static volatile uint32_t activity_since;

static volatile bool pairing_active;
static volatile uint32_t pairing_since;
static volatile bool pairing_lit;

// output mode other than the amiga (see util/run_mode.h)
static volatile bool mode_alternate;

static inline bool status_led_any_source(void)
{
    for (uint8_t source = 0; source < STATUS_LED_SOURCE_COUNT; source++)
        if (source_present[source])
            return true;

    return false;
}

#ifdef HAS_STATUS_WS2812
/**
 * claim a state machine and room for the ws2812 program on one pio, lowest
 * numbered first. the sdk's pio_claim_free_sm_and_add_program_for_gpio_range()
 * searches from the highest pio down, which on the rp2350 is pio2: the pio the
 * usb host (PIO_USB_PIO) needs whole, all 32 instructions and sms 0-2, and
 * claims later in boot with a panic if any of it is taken. so that pio is
 * skipped here, and the keyboard receiver shares pio0 or pio1 with the led.
 *
 * @return true if the program is loaded and a state machine claimed
 */
static bool status_led_rgb_claim(void)
{
    for (uint pio_num = 0; pio_num < NUM_PIOS; pio_num++) {
        PIO pio = pio_get_instance(pio_num);
        int sm;

#ifdef HAS_PIO_USB_HOST
        if (pio_num == PIO_USB_PIO)
            continue;
#endif

        if (!pio_can_add_program(pio, &ws2812_program))
            continue;

        sm = pio_claim_unused_sm(pio, false);
        if (sm < 0)
            continue;

        rgb_pio = pio;
        rgb_sm = (uint)sm;
        rgb_offset = pio_add_program(pio, &ws2812_program);
        return true;
    }

    return false;
}

/**
 * send one grb frame, unless it is already on display or the previous frame has
 * not latched yet; a deferred frame goes out on a later pass of the task.
 */
static void status_led_rgb_put(uint32_t grb)
{
    uint64_t now = time_us_64();

    if (rgb_pio == NULL)
        return;

    if (rgb_painted && (grb == rgb_painted_grb))
        return;

    if (rgb_painted && ((now - rgb_painted_at) < STATUS_RGB_LATCH_US))
        return;

    // the frame is left-justified in the 32-bit fifo word; autopull takes 24
    pio_sm_put_blocking(rgb_pio, rgb_sm, grb << 8);
    rgb_painted = true;
    rgb_painted_grb = grb;
    rgb_painted_at = now;
}
#endif

/**
 * put the two logical leds on the hardware.
 */
static void status_led_paint(bool led1_lit, bool led2_lit)
{
#ifndef HAS_STATUS_WS2812
    gpio_put(LED_STATUS1, led1_lit ? STATUS_LED_ON : STATUS_LED_OFF);
    gpio_put(LED_STATUS2, led2_lit ? STATUS_LED_ON : STATUS_LED_OFF);
#else
    uint32_t green = led1_lit ? STATUS_RGB_LEVEL : 0;
    uint32_t blue = led2_lit ? STATUS_RGB_LEVEL : 0;

    // the red channel is the output mode: lit whilst input goes somewhere other
    // than the amiga, so attached + idle reads yellow instead of green. it is
    // steady, so it shows through the heartbeat and the activity flashes
    uint32_t red = mode_alternate ? STATUS_RGB_LEVEL : 0;

    status_led_rgb_put((green << 16) | (red << 8) | blue);
#endif
}

void status_led_init(void)
{
#ifndef HAS_STATUS_WS2812
    gpio_init(LED_STATUS1);
    gpio_set_dir(LED_STATUS1, GPIO_OUT);
    gpio_put(LED_STATUS1, STATUS_LED_OFF);

    gpio_init(LED_STATUS2);
    gpio_set_dir(LED_STATUS2, GPIO_OUT);
    gpio_put(LED_STATUS2, STATUS_LED_OFF);
#else
    // a pio other than the usb host's with a free sm and room for four
    // instructions; this runs before the keyboard receiver and the cyw43
    // driver claim theirs. a status led is not worth a boot panic, so if there
    // is no room the led just stays dark
    if (!status_led_rgb_claim()) {
        rgb_pio = NULL;
    } else {
        ws2812_program_init(rgb_pio, rgb_sm, rgb_offset, LED_STATUS_WS2812);
#ifdef LED_STATUS_WS2812_INVERT
        // the board drives the led through an open-drain inverter (q1): undo it here
        gpio_set_outover(LED_STATUS_WS2812, GPIO_OVERRIDE_INVERT);
#endif
        status_led_rgb_put(0);
    }
#endif

    heartbeat_since = to_ms_since_boot(get_absolute_time());
    pairing_since = heartbeat_since;
}

void status_led_source(status_led_source_t source, bool present)
{
    if (source >= STATUS_LED_SOURCE_COUNT)
        return;

    source_present[source] = present;
}

void status_led_mode(bool alternate)
{
    mode_alternate = alternate;
}

void status_led_activity(void)
{
    // just latch the time; the task owns the pins so that a pairing prompt can
    // take the activity led over without the two fighting for it
    activity_since = to_ms_since_boot(get_absolute_time());
    activity_pending = true;
}

void status_led_pairing(bool active)
{
    // the prompt is (re)painted by several bluetooth paths; only a change of
    // state restarts the blink, or it would never leave its first half-period
    if (pairing_active == active)
        return;

    pairing_active = active;
    pairing_since = to_ms_since_boot(get_absolute_time());
    pairing_lit = active;
}

/**
 * led 1: solid once anything is attached, heartbeat until then.
 */
static bool status_led_source_task(uint32_t now)
{
    if (status_led_any_source()) {
        heartbeat_lit = true;
        heartbeat_since = now;
        return true;
    }

    if ((uint32_t)(now - heartbeat_since) >= STATUS_LED_HEARTBEAT_MS) {
        heartbeat_since = now;
        heartbeat_lit = !heartbeat_lit;
    }

    return heartbeat_lit;
}

/**
 * led 2: activity flashes, overridden by the pairing blink.
 */
static bool status_led_activity_task(uint32_t now)
{
    if (pairing_active) {
        if ((uint32_t)(now - pairing_since) >= STATUS_LED_PAIRING_MS) {
            pairing_since = now;
            pairing_lit = !pairing_lit;
        }

        return pairing_lit;
    }

    if (activity_pending && ((uint32_t)(now - activity_since) >= STATUS_LED_ACTIVITY_MS))
        activity_pending = false;

    return activity_pending;
}

void status_led_task(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool led1_lit = status_led_source_task(now);
    bool led2_lit = status_led_activity_task(now);

    status_led_paint(led1_lit, led2_lit);
}

#endif // HAS_STATUS_LEDS
