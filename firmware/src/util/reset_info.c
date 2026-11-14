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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"
#if !PICO_RP2040
#include "hardware/structs/powman.h"
#endif

#define RESET_MAGIC     0x9A1u
#define RESET_FAULT_BIT (1u << 19)

static char reason[72] = "?";

// more than the scratch registers hold, in ram the start-up code leaves alone:
// a panic's message, and what on the stack looked like a return address
#define NOTE_MAGIC      0x4e6f7465u
#define NOTE_TRAIL      10

typedef struct {
    uint32_t magic;
    uint32_t check;                 // ~magic once the note is whole
    uint32_t trail[NOTE_TRAIL];
    char text[64];
} crash_note_t;

static crash_note_t __uninitialized_ram(crash_note);
static char detail[sizeof(((crash_note_t *)0)->text) + NOTE_TRAIL * 9 + 16] = "";

extern char __flash_binary_start, __flash_binary_end;

static void __not_in_flash_func(note_begin)(void)
{
    if ((crash_note.magic == NOTE_MAGIC) && (crash_note.check == ~NOTE_MAGIC))
        return;     // a panic's message is already in: keep it
    crash_note.magic = NOTE_MAGIC;
    crash_note.check = 0;
    for (unsigned i = 0; i < NOTE_TRAIL; i++)
        crash_note.trail[i] = 0;
    crash_note.text[0] = 0;
}

static char const *const stage_names[RESET_STAGE_COUNT] = {
    "start-up", "usb host", "usb device", "console", "menu", "pads", "apply", "bluetooth", "wifi", "updates",
    "amiga", "controls",
};

// the main loop's pulse, for whoever can still look when it has stopped (the
// page is served from the radio's interrupt context)
static volatile uint32_t loop_passes, loop_seen_passes, loop_seen_ms;

void reset_info_stage(reset_stage_t stage)
{
    watchdog_hw->scratch[0] = (RESET_MAGIC << 20) | (uint32_t)stage;
    if (stage == RESET_STAGE_USB_HOST)
        loop_passes++;
}

bool reset_info_stalled(char *out, size_t size)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    unsigned stage = watchdog_hw->scratch[0] & 0xffu;

    if (loop_passes != loop_seen_passes) {
        loop_seen_passes = loop_passes;
        loop_seen_ms = now;
        return false;
    }
    if ((uint32_t)(now - loop_seen_ms) < 1000u)
        return false;
    snprintf(out, size, "stopped for %lu s in %s (watchdog %s, %lu ms left)", (unsigned long)((now - loop_seen_ms) / 1000u),
        (stage < RESET_STAGE_COUNT) ? stage_names[stage] : "?",
        (watchdog_hw->ctrl & WATCHDOG_CTRL_ENABLE_BITS) ? "on" : "off",
        (unsigned long)((watchdog_hw->ctrl & WATCHDOG_CTRL_TIME_BITS) / 1000u));
    return true;
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

    // what on the stack points into the firmware with the thumb bit set: the
    // callers, more or less
    note_begin();
    for (unsigned i = 8, found = 0; (i < 200) && (found < NOTE_TRAIL); i++) {
        uint32_t word;

        if ((uintptr_t)&frame[i] >= 0x20082000u)
            break;
        word = frame[i];
        if ((word & 1u) && (word >= (uintptr_t)&__flash_binary_start) && (word < (uintptr_t)&__flash_binary_end))
            crash_note.trail[found++] = word;
    }
    crash_note.check = ~NOTE_MAGIC;

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

/**
 * panic(): keep the message, then stop the way the sdk does (a breakpoint,
 * which with no debugger is a hard fault: the handler above does the rest)
 */
void __attribute__((noreturn)) reset_info_panic(const char *fmt, ...)
{
    va_list args;

    crash_note.magic = NOTE_MAGIC;
    crash_note.check = 0;
    memset(crash_note.trail, 0, sizeof(crash_note.trail));
    va_start(args, fmt);
    vsnprintf(crash_note.text, sizeof(crash_note.text), fmt ? fmt : "panic", args);
    va_end(args);
    crash_note.check = ~NOTE_MAGIC;

    for (;;)
        __asm volatile("bkpt #0");
}

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
    } else if (watchdog_hw->reason) {
        // through the watchdog, not by its timeout: a command, an update
        // (watchdog_caused_reboot() says no after an update's kind of boot)
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

    // the note belongs to a fault only; anything else left it from earlier
    if (ours && (s0 & RESET_FAULT_BIT) && (crash_note.magic == NOTE_MAGIC) && (crash_note.check == ~NOTE_MAGIC)) {
        size_t at;

        crash_note.text[sizeof(crash_note.text) - 1] = 0;
        for (char *c = crash_note.text; *c; c++)
            if ((*c < 0x20) || (*c > 0x7e))
                *c = ' ';
        at = (size_t)snprintf(detail, sizeof(detail), "%s%s", crash_note.text[0] ? "panic: " : "", crash_note.text);
        for (unsigned i = 0; (i < NOTE_TRAIL) && crash_note.trail[i] && (at < sizeof(detail) - 10); i++)
            at += (size_t)snprintf(detail + at, sizeof(detail) - at, " %08lx", (unsigned long)crash_note.trail[i]);
    }
    crash_note.magic = 0;

    watchdog_hw->scratch[0] = (RESET_MAGIC << 20) | RESET_STAGE_BOOT;
    watchdog_hw->scratch[1] = watchdog_hw->scratch[2] = watchdog_hw->scratch[3] = 0;
}

char const *reset_info_reason(void)
{
    return reason;
}

char const *reset_info_detail(void)
{
    return detail;
}

void reset_info_status(char *out, size_t size)
{
    uint32_t seconds = to_ms_since_boot(get_absolute_time()) / 1000u;

    snprintf(out, size, "%lu:%02lu:%02lu since: %s", (unsigned long)(seconds / 3600u),
        (unsigned long)((seconds / 60u) % 60u), (unsigned long)(seconds % 60u), reason);
}
