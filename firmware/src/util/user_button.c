/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * BOOT button readback via the bootsel trick; see user_button.h for the rules.
 */

#include "config.h"
#include "util/user_button.h"

#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"

// sample period; also the debounce: a level must be seen on two consecutive
// samples to count
#define USER_BUTTON_POLL_MS         50
#define USER_BUTTON_LONG_PRESS_MS   1000
#define USER_BUTTON_HOLD_PRESS_MS   3000

// how long to wait for core1 to park before giving up on this sample
#define USER_BUTTON_LOCKOUT_US      2000

static uint32_t last_poll_ms;
static bool last_raw;
static bool pressed;
static uint32_t pressed_since;
static bool long_fired;
static bool hold_fired;

/**
 * float qspi_ss and read it. runs from ram with interrupts off: nothing on this
 * core may touch the flash until the chip select is driven again.
 */
static bool __no_inline_not_in_flash_func(user_button_read_raw)(void)
{
    const uint cs_pin_index = 1;
    uint32_t flags = save_and_disable_interrupts();

    // chip select to hi-z
    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // let it settle through the pull-up; no sleep_*() here, those live in flash
    for (volatile int i = 0; i < 1000; ++i)
        ;

#if PICO_RP2040
    bool button_down = !(sio_hw->gpio_hi_in & (1u << cs_pin_index));
#else
    bool button_down = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#endif

    // and back to normal before anything returns to flash
    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_down;
}

/**
 * @return true and the level in *down, or false if core1 could not be parked
 */
static bool user_button_sample(bool *down)
{
    if (!multicore_lockout_start_timeout_us(USER_BUTTON_LOCKOUT_US))
        return false;

    *down = user_button_read_raw();

    // the lockout end only fails if core1 never acknowledges; it is parked in
    // ram either way, so there is nothing better to do than carry on
    multicore_lockout_end_timeout_us(USER_BUTTON_LOCKOUT_US);
    return true;
}

void user_button_init(void)
{
    last_poll_ms = to_ms_since_boot(get_absolute_time());
    last_raw = false;
    pressed = false;
    long_fired = false;
    hold_fired = false;
}

user_button_event_t user_button_task(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool raw;

    if ((uint32_t)(now - last_poll_ms) < USER_BUTTON_POLL_MS)
        return USER_BUTTON_EVENT_NONE;

    last_poll_ms = now;

    if (!user_button_sample(&raw))
        return USER_BUTTON_EVENT_NONE;

    // two equal samples in a row, 50 ms apart, make a debounced level
    if (raw != last_raw) {
        last_raw = raw;
        return USER_BUTTON_EVENT_NONE;
    }

    if (raw && !pressed) {
        pressed = true;
        pressed_since = now;
        long_fired = false;
        hold_fired = false;
        return USER_BUTTON_EVENT_NONE;
    }

    if (raw && pressed && !long_fired && ((uint32_t)(now - pressed_since) >= USER_BUTTON_LONG_PRESS_MS)) {
        long_fired = true;
        return USER_BUTTON_EVENT_LONG_PRESS;
    }

    if (raw && pressed && !hold_fired && ((uint32_t)(now - pressed_since) >= USER_BUTTON_HOLD_PRESS_MS)) {
        hold_fired = true;
        return USER_BUTTON_EVENT_HOLD;
    }

    if (!raw && pressed) {
        pressed = false;

        // a long press already reported itself; its release is not a short
        // one. a release between the long-press and hold times is reported on
        // its own, for users that act on the release instead
        if (!long_fired)
            return USER_BUTTON_EVENT_PRESS;
        if (!hold_fired)
            return USER_BUTTON_EVENT_LONG_RELEASE;
    }

    return USER_BUTTON_EVENT_NONE;
}

