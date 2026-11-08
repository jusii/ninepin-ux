/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * plain-text commands on the usb-c cdc console, for development and tests:
 * inspect the board, change the mode, reset the settings, reboot. type `help`.
 */

#include "config.h"
#include "util/console_cmd.h"

#ifdef HAS_USB_DEVICE

#include <stdio.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/platform.h"

#include "keyboard_map.h"
#ifdef ENABLE_BLUETOOTH_HID
#include "bt_hid.h"
#endif
#include "menu.h"
#include "pad.h"
#include "platform/amiga/de9.h"
#include "settings.h"
#include "usb_device.h"
#include "util/output.h"
#include "util/run_mode.h"

static void cmd_help(char const *arg);
static void cmd_info(char const *arg);
static void cmd_settings(char const *arg);
static void cmd_mode(char const *arg);
static void cmd_defaults(char const *arg);
static void cmd_reboot(char const *arg);
static void cmd_bootsel(char const *arg);
static void cmd_menu(char const *arg);
static void cmd_log(char const *arg);
static void cmd_bt(char const *arg);
static void cmd_pads(char const *arg);
static void cmd_ports(char const *arg);

typedef struct {
    char const *name;
    void (*run)(char const *arg);
    char const *help;
} console_cmd_t;

static const console_cmd_t commands[] = {
    { "help",     cmd_help,     "this list" },
    { "info",     cmd_info,     "firmware, chip and boot slot" },
    { "settings", cmd_settings, "the settings in effect" },
    { "mode",     cmd_mode,     "mode amiga|c64|usb: switch and remember the mode" },
    { "defaults", cmd_defaults, "store the factory defaults" },
    { "reboot",   cmd_reboot,   "restart the board" },
    { "bootsel",  cmd_bootsel,  "restart into the usb bootloader (BOOTSEL)" },
    { "log",      cmd_log,      "the last 2K of console output, boot messages included" },
    { "bt",       cmd_bt,       "bt [pair|forget]: bluetooth state, bonds; open pairing 2 min; forget all bonds" },
    { "pads",     cmd_pads,     "pads, players and what goes out" },
    { "ports",    cmd_ports,    "what the de9 ports are, and what they send" },
    { "menu",     cmd_menu,     "menu [open|next|prev|inc|dec|enter|leave]: drive the oled menu, show it" },
};

static void cmd_help(char const *arg)
{
    (void)arg;
    for (unsigned i = 0; i < count_of(commands); i++)
        ahprintf("  %-9s %s\n", commands[i].name, commands[i].help);
}

static void cmd_info(char const *arg)
{
    boot_info_t boot;

    (void)arg;
    ahprintf("firmware built %s %s, board %s\n", __DATE__, __TIME__, PICO_BOARD);
#if !PICO_RP2040
    ahprintf("chip rp2350 rev a%u\n", rp2350_chip_version());
    if (rom_get_boot_info(&boot)) {
        // partition: 0 = slot A, 1 = slot B, -1 = no partition table
        ahprintf("boot: type %u, partition %d%s, tbyb/update 0x%02x\n", boot.boot_type, boot.partition,
            (boot.partition == 0) ? " (slot A)" : (boot.partition == 1) ? " (slot B)" : "",
            boot.tbyb_and_update_info);
    } else {
        ahprintf("boot: no boot info\n");
    }
#endif
    ahprintf("settings: %s\n", settings_source());
}

static char const *const mode_names[SETTINGS_MODE_COUNT] = { "amiga", "c64", "usb" };

static void cmd_settings(char const *arg)
{
    settings_t const *s = settings_get();

    (void)arg;
    ahprintf("source %s\n", settings_source());
    ahprintf("mode %s, run mode %s\n", mode_names[s->mode], run_mode_name(run_mode_get()));
    ahprintf("amiga ports %u/%u, c64 ports %u/%u\n", s->amiga_port[0], s->amiga_port[1], s->c64_port[0], s->c64_port[1]);
    ahprintf("bluetooth %s (pairing %u), wifi %u, updates need boot %u\n",
        s->bt_enabled ? "on" : "off", s->bt_pairing, s->wifi_mode, s->ota_needs_boot);
    ahprintf("wheel %u reverse %u, mouse speed %u, display %u, watchdog %u\n",
        s->wheel_enabled, s->wheel_reverse, s->mouse_speed, s->display, s->watchdog);
    ahprintf("pc kbd: preset %u help %u del %u iso %u; a4000: parens %u, ctrl-amiga-amiga %u\n",
        s->kb_preset, s->kb_help, s->kb_delete, s->kb_iso, s->ak_parens, s->ak_cad);
    for (unsigned i = 0; i < SETTINGS_REMAP_SLOTS; i++) {
        if (s->kb_remap[i].source)
            ahprintf("pc remap %u: %s > %s\n", i + 1, keyboard_map_hid_name(s->kb_remap[i].source),
                keyboard_map_amiga_name(s->kb_remap[i].target));
        if (s->ak_remap[i].source)
            ahprintf("a4000 remap %u: %s > %s\n", i + 1, keyboard_map_amiga_name((uint8_t)(s->ak_remap[i].source - 1)),
                keyboard_map_hid_name(s->ak_remap[i].target));
    }
}

static void cmd_mode(char const *arg)
{
    for (unsigned mode = 0; mode < RUN_MODE_COUNT; mode++) {
        if (strcmp(arg, run_mode_name((run_mode_t)mode)) == 0) {
            run_mode_set((run_mode_t)mode);
            ahprintf("mode %s, stored %s\n", run_mode_name(run_mode_get()), mode_names[settings_get()->mode]);
            return;
        }
    }
    ahprintf("mode amiga|c64|usb\n");
}

static void cmd_defaults(char const *arg)
{
    settings_t settings;

    (void)arg;
    settings_defaults(&settings);
    ahprintf(settings_save(&settings) ? "defaults stored\n" : "could not store the defaults\n");
}

static void cmd_reboot(char const *arg)
{
    (void)arg;
    ahprintf("rebooting\n");
    sleep_ms(50);   // let the line reach the host
    watchdog_reboot(0, 0, 0);
    for (;;)
        tight_loop_contents();
}

static void cmd_bootsel(char const *arg)
{
    (void)arg;
    ahprintf("rebooting into BOOTSEL\n");
    sleep_ms(50);
    reset_usb_boot(0, 0);
}

static void cmd_menu(char const *arg)
{
    static const struct { char const *name; menu_input_t input; } inputs[] = {
        { "open", MENU_IN_OPEN }, { "next", MENU_IN_NEXT }, { "prev", MENU_IN_PREV },
        { "inc", MENU_IN_INC }, { "dec", MENU_IN_DEC }, { "enter", MENU_IN_ENTER },
        { "leave", MENU_IN_LEAVE },
    };
    char lines[MENU_LINES][MENU_COLS + 1];

    if (*arg) {
        unsigned i;

        for (i = 0; i < count_of(inputs); i++) {
            if (strcmp(arg, inputs[i].name) == 0) {
                menu_input(inputs[i].input);
                break;
            }
        }
        if (i == count_of(inputs)) {
            ahprintf("menu [open|next|prev|inc|dec|enter|leave]\n");
            return;
        }
    }

    if (!menu_is_open()) {
        ahprintf("menu closed\n");
        return;
    }

    menu_lines(lines);
    for (unsigned i = 0; i < MENU_LINES; i++)
        ahprintf("|%s|\n", lines[i]);
}

static void cmd_log(char const *arg)
{
    static char copy[2048];
    size_t n;

    (void)arg;
    n = usb_device_console_history(copy, sizeof(copy));
    ahprintf("---- last %u bytes ----\n", (unsigned)n);
    // straight to the cdc buffer in pieces; ahprintf would also re-log it
    for (size_t i = 0; i < n; i += 128) {
        char piece[129];
        size_t len = ((n - i) < 128) ? (n - i) : 128;

        memcpy(piece, copy + i, len);
        piece[len] = 0;
        ahprintf("%s", piece);
    }
    ahprintf("\n---- end ----\n");
}

static void cmd_bt(char const *arg)
{
#ifdef ENABLE_BLUETOOTH_HID
    if (strcmp(arg, "pair") == 0) {
        bt_hid_pair_window();
        ahprintf("pairing requested\n");
    } else if (strcmp(arg, "forget") == 0) {
        bt_hid_forget_all();
        ahprintf("forgetting all bonds\n");
    } else if (*arg) {
        ahprintf("bt [pair|forget]\n");
    } else {
        bt_hid_describe();
    }
#else
    (void)arg;
    ahprintf("bluetooth: not in this build\n");
#endif
}

static void cmd_pads(char const *arg)
{
    (void)arg;
    pads_describe();
}

static void cmd_ports(char const *arg)
{
    (void)arg;
    de9_describe();
}

void console_cmd_task(void)
{
    char line[USB_DEVICE_CONSOLE_LINE_MAX];
    char *arg;

    if (!usb_device_console_line(line, sizeof(line)))
        return;

    // "name rest-of-line"
    arg = strchr(line, ' ');
    if (arg) {
        *arg++ = 0;
        while (*arg == ' ')
            arg++;
    } else {
        arg = line + strlen(line);
    }

    ahprintf("> %s%s%s\n", line, *arg ? " " : "", arg);

    for (unsigned i = 0; i < count_of(commands); i++) {
        if (strcmp(line, commands[i].name) == 0) {
            commands[i].run(arg);
            return;
        }
    }
    ahprintf("unknown command; try help\n");
}

#endif // HAS_USB_DEVICE
