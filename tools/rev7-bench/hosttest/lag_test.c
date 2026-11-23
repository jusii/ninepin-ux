// host test of lag.c: the arithmetic, the merge rule, the blame and the text
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "lag.h"
#include "util/reset_info.h"

static uint32_t now;
uint32_t lag_now(void) { return now; }
uint32_t lag_test_s(void) { return now / 1000000u; }
char const *reset_info_stage_name(reset_stage_t stage)
{
    static char const *const names[RESET_STAGE_COUNT] = {
        "start-up", "usb host", "usb device", "console", "menu", "pads", "apply", "bluetooth", "wifi", "updates",
        "amiga", "controls",
    };
    return (stage < RESET_STAGE_COUNT) ? names[stage] : "?";
}

static char text[LAG_REPORT_MAX];

static void has(char const *want)
{
    lag_report(text, sizeof(text));
    if (!strstr(text, want)) {
        fprintf(stderr, "missing: %s\nin:\n%s", want, text);
        assert(0);
    }
}

static void hasnt(char const *want)
{
    lag_report(text, sizeof(text));
    assert(!strstr(text, want));
}

// a bluetooth report that waited `us` and changed the de9 pins
static void bt_de9(uint32_t us)
{
    lag_origin(LAG_FROM_BT, now);
    now += us;
    lag_out_de9();
    lag_origin_end();
}

int main(void)
{
    // nothing yet
    now = 5000000;
    has("nothing yet");

    // outputs changed by the board itself (no report being worked on) do not count
    lag_out_de9();
    lag_out_usb(0);
    lag_usb_sending(0);
    lag_usb_collected(0);
    lag_pass_end();
    has("nothing yet");

    // 100 quick ones and one slow: least, median, worst, the columns
    for (int i = 0; i < 100; i++)
        bt_de9(200);
    bt_de9(100);
    bt_de9(3000);
    // the median is read off half-octave buckets: near, not exact
    has("bluetooth pad > de9      102    0.10    0.2");
    has("    3.00\n");
    has("bluetooth pad > de9     101      0      0      0      1      0      0      0      0\n");
    hasnt("usb pad");
    hasnt("over 4 ms");

    // the median is the middle one, not the middle of the range
    lag_reset();
    for (int i = 0; i < 10; i++)
        bt_de9(300);
    for (int i = 0; i < 11; i++)
        bt_de9(1500);
    lag_report(text, sizeof(text));
    {
        unsigned n, a, b, c, d, e, f;
        char const *row = strstr(text, "bluetooth pad > de9");
        assert(row && sscanf(row, "bluetooth pad > de9 %u %u.%u %u.%u %u.%u", &n, &a, &b, &c, &d, &e, &f) == 7);
        assert(n == 21 && a == 0 && b == 30 && e == 1 && f == 50);
        assert(c == 1 && d >= 41 && d <= 50);       // in the 1.41 .. 2 ms bucket, capped by the worst
    }

    // a slow one names the stage that held it up
    lag_reset();
    lag_stage(RESET_STAGE_USB_HOST);
    now += 100;
    {
        uint32_t arrived = now;         // the report lands as the wifi stage begins

        lag_stage(RESET_STAGE_WIFI);
        now += 12800;
        lag_stage(RESET_STAGE_OTA);
        now += 300;
        lag_stage(RESET_STAGE_BLUETOOTH);
        now += 100;
        lag_origin(LAG_FROM_BT, arrived);
        lag_out_de9();
        lag_origin_end();
    }
    lag_stage(RESET_STAGE_CONTROLS);
    now += 50;
    lag_pass_end();
    has("over 4 ms: 1, the newest first");
    has("0 s ago  bluetooth pad > de9    13.20  wifi 12.80");
    has("the slowest 13.35 (wifi 12.80)");
    now += 7000000;
    has("7 s ago  bluetooth pad > de9");

    // the stage still running is blamed too
    lag_reset();
    lag_stage(RESET_STAGE_BLUETOOTH);
    {
        uint32_t arrived = now;

        now += 6000;
        lag_origin(LAG_FROM_BT, arrived);
        lag_out_de9();
        lag_origin_end();
    }
    lag_pass_end();
    has("bluetooth pad > de9     6.00  bluetooth 6.00");

    // usb out: timed to the host collecting it, the oldest change counts
    lag_reset();
    lag_origin(LAG_FROM_USB, now);
    lag_out_usb(1);
    lag_origin_end();
    now += 400;
    lag_origin(LAG_FROM_USB, now);
    lag_out_usb(1);                     // a second change before the host came
    lag_origin_end();
    now += 100;
    lag_usb_sending(1);
    now += 300;
    lag_usb_collected(1);
    now += 5000;                        // the loop comes round late: not counted
    lag_pass_end();
    has("usb pad > usb              1    0.80    0.80    0.80");

    // refused by the stack: it waits on
    lag_reset();
    lag_origin(LAG_FROM_BT, now);
    lag_out_usb(0);
    lag_origin_end();
    lag_usb_sending(0);
    lag_usb_unsent(0);
    now += 1000;
    lag_usb_sending(0);
    now += 500;
    lag_usb_collected(0);
    lag_pass_end();
    has("bluetooth pad > usb        1    1.50    1.50    1.50");

    // a host that polls slowly is not the board being busy
    lag_reset();
    lag_origin(LAG_FROM_BT, now);
    lag_out_usb(0);
    lag_origin_end();
    lag_usb_sending(0);
    now += 8000;
    lag_usb_collected(0);
    lag_pass_end();
    has("bluetooth pad > usb     8.00  waiting for the usb host");

    // no host: what waits is forgotten, not counted when one arrives
    lag_reset();
    lag_origin(LAG_FROM_BT, now);
    lag_out_usb(0);
    lag_origin_end();
    lag_usb_idle();
    now += 3000000;
    lag_usb_sending(0);
    lag_usb_collected(0);
    lag_pass_end();
    hasnt("bluetooth pad > usb");

    // the slow list keeps the last eight, the count all of them
    lag_reset();
    for (int i = 0; i < 11; i++)
        bt_de9(5000 + (uint32_t)i * 1000);
    has("over 4 ms: 11, the newest first");
    has("15.00");
    hasnt("  7.00");
    has("  8.00");
    assert(strstr(text, "   15.00") < strstr(text, "    8.00"));

    // the clock wrapping in the middle of a wait
    lag_reset();
    now = 0xffffff00u;
    bt_de9(1000);
    has("bluetooth pad > de9        1    1.00    1.00    1.00");

    // a text too long for the buffer is cut, not overrun
    {
        char small[40];
        size_t n = lag_report(small, sizeof(small));
        assert(n == sizeof(small) - 1 && small[n] == '\0');
    }

    printf("lag: all pass\n");
    return 0;
}
