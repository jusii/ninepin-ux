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

#include <stdio.h>
#include <string.h>

#include "config.h"

#include "hardware/watchdog.h"

#include "platform/amiga/de9.h"
#include "platform/amiga/quad_mouse.h"
#include "ota.h"
#include "pad.h"
#include "profiles.h"
#include "settings.h"
#include "util/debug_cons.h"
#include "util/output.h"

// what runs now; unknown at boot, when the bootrom may have one counting (a
// trial boot of a new firmware: 16.7 s to accept itself, see ota.c)
#define WATCHDOG_UNKNOWN    UINT32_MAX
#define WATCHDOG_TRIAL_MS   5000u

static uint32_t watchdog_ms = WATCHDOG_UNKNOWN;     // 0: off

void apply_watchdog(void)
{
    static uint32_t const timeout_ms[SETTINGS_WATCHDOG_COUNT] = { 0, 2000, 5000 };
    uint32_t wanted = timeout_ms[settings_get()->watchdog % SETTINGS_WATCHDOG_COUNT];

    // a firmware on trial keeps a watchdog even with the setting off: if it
    // stops before it has accepted itself, the previous firmware comes back
    if (!wanted && ota_trial())
        wanted = WATCHDOG_TRIAL_MS;

    // nothing the main loop does blocks for anywhere near 2 s (the longest:
    // the amiga reset pulse, 0.5 s, and a flash write)
    if (wanted != watchdog_ms) {
        if (wanted)
            watchdog_enable(wanted, true);
        else
            watchdog_disable();
        if (watchdog_ms != WATCHDOG_UNKNOWN || wanted)
            ahprintf("[wdt] %s%s\n", wanted ? (wanted == 2000 ? "2 s" : "5 s") : "off",
                (wanted && !settings_get()->watchdog) ? " (new firmware on trial)" : "");
        watchdog_ms = wanted;
    }
}

void apply_settings(void)
{
    static uint16_t const step_us[SETTINGS_MOUSE_SPEED_COUNT] = { 300, 200, 150, 100 };
    settings_t const *s = settings_get();

    amiga_quad_mouse_set_interval(step_us[s->mouse_speed % SETTINGS_MOUSE_SPEED_COUNT]);
    dbgcons_display_mode(s->display);
    apply_watchdog();
    de9_apply();
}

/**
 * the pad profiles in turn, "none" (no combos) amongst them; stored at once,
 * like a load from the page
 */
void pads_profile_step(int step)
{
    settings_t s = *settings_get();
    unsigned at = s.pad_profile;
    profile_t const *profile = NULL;
    char text[22];

    if (!profiles_count()) {
        dbgcons_notice("no pad profiles");
        return;
    }

    for (unsigned tries = 0; tries <= PROFILES_MAX; tries++) {
        at = (at + PROFILES_MAX + 1u + ((step > 0) ? 1u : PROFILES_MAX)) % (PROFILES_MAX + 1u);
        profile = at ? profiles_get(at - 1u) : NULL;
        if (!at || profile)
            break;
    }

    if (profile) {
        profiles_to_settings(profile, &s);
    } else {
        at = 0;
        memset(s.pad_combo, 0, sizeof(s.pad_combo));
    }
    s.pad_profile = (uint8_t)at;

    if (!settings_save(&s)) {
        dbgcons_notice("profile: save failed");
        return;
    }
    snprintf(text, sizeof(text), "profile %.12s", profile ? profile->name : "none");
    dbgcons_notice(text);
    ahprintf("[pads] %s\n", text);
}

void apply_task(void)
{
    dbgcons_notice_task();
    if (watchdog_ms && (watchdog_ms != WATCHDOG_UNKNOWN))
        watchdog_update();
}

void apply_report_watchdog_reboot(void)
{
    if (watchdog_enable_caused_reboot())
        ahprintf("[wdt] the watchdog restarted the board: the main loop had stopped\n");
}
