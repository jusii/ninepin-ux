/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * putting the general settings into effect; see apply.h. the watchdog setting
 * follows stefan reinauer's runtime_menu.c (reinauer/amigahid-pico, features
 * branch): it watches the main loop only, not the mouse core.
 */

#include "apply.h"
#include "config.h"

#include "hardware/watchdog.h"

#include "platform/amiga/de9.h"
#include "platform/amiga/quad_mouse.h"
#include "settings.h"
#include "util/debug_cons.h"
#include "util/output.h"

static uint32_t watchdog_ms;    // 0: off

void apply_settings(void)
{
    static uint16_t const step_us[SETTINGS_MOUSE_SPEED_COUNT] = { 300, 200, 150, 100 };
    static uint32_t const timeout_ms[SETTINGS_WATCHDOG_COUNT] = { 0, 2000, 5000 };
    settings_t const *s = settings_get();
    uint32_t wanted = timeout_ms[s->watchdog % SETTINGS_WATCHDOG_COUNT];

    amiga_quad_mouse_set_interval(step_us[s->mouse_speed % SETTINGS_MOUSE_SPEED_COUNT]);
    dbgcons_display_mode(s->display);

    // nothing the main loop does blocks for anywhere near 2 s (the longest:
    // the amiga reset pulse, 0.5 s, and a flash write)
    if (wanted != watchdog_ms) {
        if (wanted)
            watchdog_enable(wanted, true);
        else
            watchdog_disable();
        watchdog_ms = wanted;
        ahprintf("[wdt] %s\n", wanted ? (wanted == 2000 ? "2 s" : "5 s") : "off");
    }

    de9_apply();
}

void apply_task(void)
{
    if (watchdog_ms)
        watchdog_update();
}

void apply_report_watchdog_reboot(void)
{
    if (watchdog_enable_caused_reboot())
        ahprintf("[wdt] the watchdog restarted the board: the main loop had stopped\n");
}
