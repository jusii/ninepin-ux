/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * see reset_info.h.
 *
 * scratch0: [31:20] RESET_MAGIC, [19] fault, [7:0] main loop stage
 * scratch1: stacked pc   scratch2: stacked lr   scratch3: cfsr
 * (the sdk's own watchdog_reboot() uses scratch 4 to 7)
 */

#include "config.h"
#include "util/reset_info.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#if !PICO_RP2040
#include "hardware/structs/powman.h"
#endif

#define RESET_MAGIC     0x9A1u
#define RESET_FAULT_BIT (1u << 19)

static char reason[72] = "?";

static char const *const stage_names[RESET_STAGE_COUNT] = {
    "start-up", "usb host", "usb device", "console", "menu", "pads", "apply", "bluetooth", "wifi", "updates",
    "amiga", "controls",
};

void reset_info_stage(reset_stage_t stage)
{
    watchdog_hw->scratch[0] = (RESET_MAGIC << 20) | (uint32_t)stage;
}

#ifndef USB_DEVICE_BRINGUP_SERVICE
/**
 * a hard fault: note where, then restart through the watchdog, which leaves
 * the scratch registers alone. (the bring-up build has its own in main.c.)
 */
void __attribute__((used)) __not_in_flash_func(reset_info_hardfault_c)(uint32_t *frame)
{
    watchdog_hw->scratch[0] = (watchdog_hw->scratch[0] & 0xffu) | (RESET_MAGIC << 20) | RESET_FAULT_BIT;
    watchdog_hw->scratch[1] = frame[6];
    watchdog_hw->scratch[2] = frame[5];
    watchdog_hw->scratch[3] = *(volatile uint32_t *)0xE000ED28u;   // cfsr

    watchdog_reboot(0, 0, 1);
    for (;;)
        ;
}

void __attribute__((naked)) __not_in_flash_func(isr_hardfault)(void)
{
    __asm volatile(
        "movs r0, #4        \n"
        "mov  r1, lr        \n"
        "tst  r0, r1        \n"
        "beq  1f            \n"
        "mrs  r0, psp       \n"
        "b    reset_info_hardfault_c \n"
        "1:                 \n"
        "mrs  r0, msp       \n"
        "b    reset_info_hardfault_c \n");
}
#endif

void reset_info_init(void)
{
    uint32_t s0 = watchdog_hw->scratch[0];
    bool ours = (s0 >> 20) == RESET_MAGIC;
    unsigned stage = s0 & 0xffu;
    char const *where = (ours && (stage < RESET_STAGE_COUNT)) ? stage_names[stage] : "?";

    if (ours && (s0 & RESET_FAULT_BIT)) {
        snprintf(reason, sizeof(reason), "fault pc %08lx lr %08lx cfsr %08lx (in %s)",
            (unsigned long)watchdog_hw->scratch[1], (unsigned long)watchdog_hw->scratch[2],
            (unsigned long)watchdog_hw->scratch[3], where);
    } else if (watchdog_enable_caused_reboot()) {
        // the main loop stopped coming round
        snprintf(reason, sizeof(reason), "watchdog (in %s)", where);
    } else if (watchdog_caused_reboot()) {
        snprintf(reason, sizeof(reason), "restart asked for");
    } else {
#if !PICO_RP2040
        uint32_t chip = powman_hw->chip_reset;

        snprintf(reason, sizeof(reason), "%s",
            (chip & POWMAN_CHIP_RESET_HAD_BOR_BITS) ? "brown-out (supply dipped)"
            : (chip & POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS) ? "reset button"
            : (chip & POWMAN_CHIP_RESET_HAD_POR_BITS) ? "power on" : "power on or reset");
#else
        snprintf(reason, sizeof(reason), "power on or reset");
#endif
    }

    watchdog_hw->scratch[0] = (RESET_MAGIC << 20) | RESET_STAGE_BOOT;
    watchdog_hw->scratch[1] = watchdog_hw->scratch[2] = watchdog_hw->scratch[3] = 0;
}

char const *reset_info_reason(void)
{
    return reason;
}

void reset_info_status(char *out, size_t size)
{
    uint32_t seconds = to_ms_since_boot(get_absolute_time()) / 1000u;

    snprintf(out, size, "%lu:%02lu:%02lu since: %s", (unsigned long)(seconds / 3600u),
        (unsigned long)((seconds / 60u) % 60u), (unsigned long)(seconds % 60u), reason);
}
