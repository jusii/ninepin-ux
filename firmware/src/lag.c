/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * how long a pad's report takes through the board. see lag.h.
 *
 * samples are taken in the main loop. the page reads them from interrupt
 * context, and the usb interrupt leaves the time a report was collected, so
 * the report is made from a copy taken with interrupts off.
 */

#include "lag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "util/reset_info.h"

#ifndef LAG_HOST_TEST
#include "pico/stdlib.h"
#include "hardware/sync.h"

uint32_t lag_now(void)
{
    return time_us_32();
}

static uint32_t now_s(void)
{
    return (uint32_t)(time_us_64() / 1000000u);
}
#else
uint32_t lag_test_s(void);
static uint32_t now_s(void) { return lag_test_s(); }
static uint32_t save_and_disable_interrupts(void) { return 0; }
static void restore_interrupts(uint32_t state) { (void)state; }
#endif

// half-octave buckets: upper bounds in microseconds, the last bucket is open
static uint32_t const bounds[] = {
    31, 44, 62, 88, 125, 177, 250, 354, 500, 707, 1000, 1414, 2000, 2828, 4000, 5657, 8000, 11314, 16000,
    22627, 32000, 45255, 64000,
};
#define BUCKETS     (sizeof(bounds) / sizeof(bounds[0]) + 1)

// the columns shown: under 0.25, 0.5, 1, 2, 4, 8, 16, 32 ms, and the rest
static uint8_t const column_end[] = { 7, 9, 11, 13, 15, 17, 19, 21, BUCKETS };
#define COLUMNS     (sizeof(column_end))

#define SLOW_US     4000u
#define SLOW_MAX    8
#define PATHS       (LAG_FROMS * LAG_TOS)
#define STAGES      RESET_STAGE_COUNT
#define BY_HOST     0xfe    // the wait was for the usb host's poll
#define BY_NOTHING  0xff

typedef struct {
    uint32_t count;
    uint32_t min_us;
    uint32_t worst_us;
    uint32_t bucket[BUCKETS];
} path_t;

typedef struct {
    uint32_t at_s;
    uint32_t lag_us;
    uint32_t by_us;
    uint8_t path;
    uint8_t by;
} slow_t;

typedef struct {
    path_t path[PATHS];
    slow_t slow[SLOW_MAX];
    uint32_t slows;             // ever; the newest is slow[(slows - 1) % SLOW_MAX]
    uint32_t passes;
    uint64_t passes_us;
    uint32_t pass_worst_us;
    uint32_t pass_worst_by_us;
    uint8_t pass_worst_by;
    uint32_t fast, fast_corrected;
} stats_t;

typedef struct {
    bool on;
    uint8_t from;
    uint32_t at_us;
} stamp_t;

static stats_t stats;
static stamp_t origin;
static volatile bool fast_wanted = true;

// usb out, per player: changed and waiting, handed to the stack, collected
static stamp_t usb_pending[2], usb_flight[2];
static volatile bool usb_done[2];
static volatile uint32_t usb_done_at[2];

// the main loop's stages: when each last ran and for how long
static uint32_t stage_end[STAGES], stage_us[STAGES];
static uint8_t stage_now = BY_NOTHING;
static uint32_t stage_since, pass_since;
static uint32_t pass_top_us;
static uint8_t pass_top = BY_NOTHING;
static bool pass_on;

static uint32_t overlap(uint32_t begin, uint32_t end, uint32_t from, uint32_t to)
{
    // all within a few seconds of each other, on a wrapping clock
    int32_t a = (int32_t)(begin - from), b = (int32_t)(end - from), w = (int32_t)(to - from);

    if (a < 0)
        a = 0;
    if (b > w)
        b = w;
    return (b > a) ? (uint32_t)(b - a) : 0;
}

// the stage that took most of the time between two moments
static uint8_t blame(uint32_t from, uint32_t to, uint32_t *us)
{
    uint8_t by = BY_NOTHING;
    uint32_t most = 0;

    for (uint8_t i = 0; i < STAGES; i++) {
        uint32_t part;

        if (i == stage_now)
            part = overlap(stage_since, lag_now(), from, to);
        else if (stage_us[i])
            part = overlap(stage_end[i] - stage_us[i], stage_end[i], from, to);
        else
            continue;
        if (part > most) {
            most = part;
            by = i;
        }
    }
    *us = most;
    return by;
}

static void sample(uint8_t from, uint8_t to, uint32_t begin, uint32_t end)
{
    uint8_t index = (uint8_t)(from * LAG_TOS + to);
    path_t *path = &stats.path[index];
    uint32_t us = end - begin;
    unsigned bucket = 0;

    // a stamp from the future or the far past is no measurement
    if (us > 10000000u)
        return;

    while ((bucket < BUCKETS - 1) && (us >= bounds[bucket]))
        bucket++;
    path->bucket[bucket]++;
    if (!path->count || (us < path->min_us))
        path->min_us = us;
    if (us > path->worst_us)
        path->worst_us = us;
    path->count++;

    if (us >= SLOW_US) {
        slow_t *slow = &stats.slow[stats.slows % SLOW_MAX];

        slow->at_s = now_s();
        slow->lag_us = us;
        slow->path = index;
        slow->by = blame(begin, end, &slow->by_us);
        // the board was not busy: the host had not come for it yet
        if ((to == LAG_TO_USB) && (slow->by_us * 2 < us))
            slow->by = BY_HOST;
        stats.slows++;
    }
}

void lag_origin(uint8_t from, uint32_t at_us)
{
    origin.on = true;
    origin.from = from;
    origin.at_us = at_us;
}

void lag_origin_end(void)
{
    origin.on = false;
}

void lag_out_de9(void)
{
    if (origin.on)
        sample(origin.from, LAG_TO_DE9, origin.at_us, lag_now());
}

void lag_out_de9_at(uint32_t at_us)
{
    if (origin.on)
        sample(origin.from, LAG_TO_DE9, origin.at_us, at_us);
}

bool lag_fast_on(void)
{
    return fast_wanted;
}

void lag_fast_set(bool on)
{
    fast_wanted = on;
}

void lag_fast(bool corrected)
{
    stats.fast++;
    if (corrected)
        stats.fast_corrected++;
}

void lag_out_usb(uint8_t player)
{
    // several changes before the host comes: timed from the oldest
    if ((player < 2) && origin.on && !usb_pending[player].on)
        usb_pending[player] = origin;
}

void lag_usb_sending(uint8_t player)
{
    if (player >= 2)
        return;
    usb_done[player] = false;
    usb_flight[player] = usb_pending[player];
    usb_pending[player].on = false;
}

void lag_usb_unsent(uint8_t player)
{
    if (player >= 2)
        return;
    usb_pending[player] = usb_flight[player];
    usb_flight[player].on = false;
}

void lag_usb_collected(uint8_t player)
{
    if (player >= 2)
        return;
    usb_done_at[player] = lag_now();
    usb_done[player] = true;
}

void lag_usb_idle(void)
{
    for (unsigned player = 0; player < 2; player++) {
        usb_pending[player].on = false;
        usb_flight[player].on = false;
        usb_done[player] = false;
    }
}

static void usb_take(void)
{
    for (unsigned player = 0; player < 2; player++) {
        if (!usb_done[player])
            continue;
        usb_done[player] = false;
        if (usb_flight[player].on)
            sample(usb_flight[player].from, LAG_TO_USB, usb_flight[player].at_us, usb_done_at[player]);
        usb_flight[player].on = false;
    }
}

static void stage_close(uint32_t now)
{
    if (stage_now >= STAGES)
        return;
    stage_us[stage_now] = now - stage_since;
    stage_end[stage_now] = now;
    if (stage_us[stage_now] > pass_top_us) {
        pass_top_us = stage_us[stage_now];
        pass_top = stage_now;
    }
    // a stage of no length is still one that ran
    if (!stage_us[stage_now])
        stage_us[stage_now] = 1;
}

void lag_stage(uint8_t stage)
{
    uint32_t now = lag_now();

    stage_close(now);
    if (!pass_on) {
        pass_on = true;
        pass_since = now;
    }
    stage_now = (stage < STAGES) ? stage : BY_NOTHING;
    stage_since = now;
}

void lag_pass_end(void)
{
    uint32_t now = lag_now();
    uint32_t us;

    stage_close(now);
    stage_now = BY_NOTHING;
    usb_take();
    if (!pass_on)
        return;
    pass_on = false;

    us = now - pass_since;
    stats.passes++;
    stats.passes_us += us;
    if (us > stats.pass_worst_us) {
        stats.pass_worst_us = us;
        stats.pass_worst_by = pass_top;
        stats.pass_worst_by_us = pass_top_us;
    }
    pass_top = BY_NOTHING;
    pass_top_us = 0;
}

void lag_reset(void)
{
    uint32_t interrupts = save_and_disable_interrupts();

    memset(&stats, 0, sizeof(stats));
    restore_interrupts(interrupts);
}

static uint32_t median_us(path_t const *path)
{
    uint32_t want = (path->count + 1) / 2, seen = 0;

    for (unsigned i = 0; i < BUCKETS; i++) {
        uint32_t low, high, us;

        if (seen + path->bucket[i] < want) {
            seen += path->bucket[i];
            continue;
        }
        // somewhere in this bucket: as far in as its share says
        low = i ? bounds[i - 1] : 0;
        high = (i < BUCKETS - 1) ? bounds[i] : path->worst_us;
        if (low < path->min_us)
            low = path->min_us;
        if (high > path->worst_us)
            high = path->worst_us;
        if (high < low)
            high = low;
        us = low + (uint32_t)(((uint64_t)(high - low) * (want - seen)) / path->bucket[i]);
        return us;
    }
    return path->worst_us;
}

typedef struct {
    char *out;
    size_t size, length;
} text_t;

static void put(text_t *t, char const *format, ...)
{
    va_list args;
    int n;

    if (t->length + 1 >= t->size)
        return;
    va_start(args, format);
    n = vsnprintf(t->out + t->length, t->size - t->length, format, args);
    va_end(args);
    if (n < 0)
        return;
    t->length += (size_t)n;
    if (t->length >= t->size)
        t->length = t->size - 1;
}

// microseconds as milliseconds, two decimals
static char const *ms(uint32_t us, char *buffer, size_t size)
{
    snprintf(buffer, size, "%lu.%02lu", (unsigned long)(us / 1000u), (unsigned long)((us % 1000u) / 10u));
    return buffer;
}

static char const *by_name(uint8_t by)
{
    if (by == BY_HOST)
        return "waiting for the usb host";
    return (by < STAGES) ? reset_info_stage_name((reset_stage_t)by) : "?";
}

size_t lag_report(char *out, size_t size)
{
    static char const *const names[PATHS] = {
        "bluetooth pad > de9", "bluetooth pad > usb", "usb pad > de9", "usb pad > usb",
    };
    static stats_t copy;        // the callers do not nest
    text_t t = { out, size, 0 };
    char a[16], b[16], c[16];
    uint32_t interrupts, total = 0;

    if (!out || !size)
        return 0;
    out[0] = '\0';

    interrupts = save_and_disable_interrupts();
    copy = stats;
    restore_interrupts(interrupts);

    for (unsigned i = 0; i < PATHS; i++)
        total += copy.path[i].count;

    put(&t, "pad report in to output out, milliseconds\n");
    if (!total)
        put(&t, "nothing yet: no pad has changed an output\n");
    else
        put(&t, "%-20s %7s %7s %7s %7s\n", "", "changes", "least", "median", "worst");
    for (unsigned i = 0; i < PATHS; i++) {
        path_t const *path = &copy.path[i];

        if (!path->count)
            continue;
        put(&t, "%-20s %7lu %7s %7s %7s\n", names[i], (unsigned long)path->count,
            ms(path->min_us, a, sizeof(a)), ms(median_us(path), b, sizeof(b)), ms(path->worst_us, c, sizeof(c)));
    }

    if (total) {
        put(&t, "how many took under\n%-20s %6s %6s %6s %6s %6s %6s %6s %6s %6s\n", "",
            "0.25", "0.5", "1", "2", "4", "8", "16", "32", "more");
        for (unsigned i = 0; i < PATHS; i++) {
            path_t const *path = &copy.path[i];
            unsigned bucket = 0;

            if (!path->count)
                continue;
            put(&t, "%-20s", names[i]);
            for (unsigned column = 0; column < COLUMNS; column++) {
                uint32_t sum = 0;

                for (; bucket < column_end[column]; bucket++)
                    sum += path->bucket[bucket];
                put(&t, " %6lu", (unsigned long)sum);
            }
            put(&t, "\n");
        }
    }

    put(&t, "fast path %s: %lu reports straight to the de9 lines, %lu put right afterwards\n",
        fast_wanted ? "on" : "off", (unsigned long)copy.fast, (unsigned long)copy.fast_corrected);

    if (copy.passes) {
        put(&t, "main loop: a pass takes %s on average, the slowest %s",
            ms((uint32_t)(copy.passes_us / copy.passes), a, sizeof(a)), ms(copy.pass_worst_us, b, sizeof(b)));
        if (copy.pass_worst_by < STAGES)
            put(&t, " (%s %s)", by_name(copy.pass_worst_by), ms(copy.pass_worst_by_us, c, sizeof(c)));
        put(&t, "\n");
    }

    if (copy.slows) {
        uint32_t shown = (copy.slows < SLOW_MAX) ? copy.slows : SLOW_MAX;
        uint32_t now = now_s();

        put(&t, "over %lu ms: %lu, the newest first\n", (unsigned long)(SLOW_US / 1000u), (unsigned long)copy.slows);
        for (uint32_t i = 0; i < shown; i++) {
            slow_t const *slow = &copy.slow[(copy.slows - 1 - i) % SLOW_MAX];

            put(&t, "  %5lu s ago  %-20s %7s", (unsigned long)(now - slow->at_s), names[slow->path],
                ms(slow->lag_us, a, sizeof(a)));
            if (slow->by == BY_HOST)
                put(&t, "  %s", by_name(slow->by));
            else if (slow->by != BY_NOTHING)
                put(&t, "  %s %s", by_name(slow->by), ms(slow->by_us, b, sizeof(b)));
            put(&t, "\n");
        }
    }

    return t.length;
}
