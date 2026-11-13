/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * main entry point for ninepin ux.
 */

#include "config.h"

// these reside within the tinyusb sdk and are not part of this project source
#include "tusb.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "pio_usb.h"

#include "display/disp_ssd.h"
#include "platform/amiga/keyboard_serial_io.h"
#include "platform/amiga/keyboard_recv.h"
#include "platform/amiga/quad_mouse.h"
#include "util/amiga_sense.h"
#include "util/board_pins.h"
#include "util/debug_cons.h"
#include "util/io_expander.h"
#include "util/output.h"
#include "util/run_mode.h"
#include "util/console_cmd.h"
#include "apply.h"
#include "menu.h"
#include "platform/amiga/de9.h"
#include "pad.h"
#include "profiles.h"
#include "util/reset_info.h"
#include "settings.h"
#include "util/status_led.h"
#include "util/user_button.h"
#include "ota.h"

#include "tusb_config.h"

#include "usb_device.h"

#ifdef ENABLE_BLUETOOTH_HID
#include "bt_hid.h"
#endif
#include "wifi.h"
#include "util/reset_info.h"

// defined within usb_hid.c
extern void hid_app_task(void);

/**
 * hand tinyusb the pio-usb root port: D+ on PIO_USB_DP_PIN (D- on the next
 * gpio), the whole of PIO_USB_PIO for tx, rx and eop detection, and a free dma
 * channel. must run before tusb_init() on the host rhport.
 */
static void pio_usb_host_configure(void)
{
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    int dma_ch;

    pio_cfg.pin_dp = PIO_USB_DP_PIN;
    pio_cfg.pinout = PIO_USB_PINOUT_DPDM;
    pio_cfg.pio_tx_num = PIO_USB_PIO;
    pio_cfg.pio_rx_num = PIO_USB_PIO;

    // the library claims fixed state machines (tx 0, rx 1, eop 2 from the
    // default config) on a pio nobody else has touched, but a fixed dma channel
    // could collide with the display's, so find a free one and let the library
    // claim it for itself (it panics on one already claimed)
    dma_ch = dma_claim_unused_channel(true);
    dma_channel_unclaim((uint)dma_ch);
    pio_cfg.tx_ch = (uint8_t)dma_ch;

    tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
}

/**
 * follow the amiga's power: greet it when it comes on, stop talking to it (and
 * forget its state) when it goes off.
 */
static void amiga_power_task(void)
{
    if (!amiga_sense_task())
        return;

    if (amiga_sense_is_on())
        amiga_power_on();
    else
        amiga_power_off();

    dbgcons_amiga_power(amiga_sense_is_on());
}

#ifdef USB_DEVICE_BRINGUP_SERVICE
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

/*
 * BRING-UP ONLY: catch whatever stops the cpu during init. a hard fault records
 * where it hit in the watchdog scratch registers (they survive a watchdog
 * reboot) and reboots through the watchdog; a TRAP_WDT_MS watchdog, fed from
 * the bring-up timer irq, covers interrupts stopping without a fault. the next
 * boot shows the report on the oled and stops there.
 *
 * scratch0: [31:20] TRAP_MAGIC, [19] fault, [16:9] boot stage number,
 *           [8:0] exception number active when the fault hit (0 = thread)
 * scratch1: stacked pc   scratch2: stacked lr   scratch3: cfsr
 */
#define TRAP_MAGIC      0xB0Bu
#define TRAP_FAULT_BIT  (1u << 19)
#define TRAP_WDT_MS     300

static volatile uint32_t boot_stage_count;
static uint32_t trap_report[4];
static bool trap_pending;

/**
 * a thread-mode fault returns here instead of rebooting, with interrupts on:
 * the bring-up irq keeps the usb console alive, so whatever panic() printed
 * before it hit its breakpoint still reaches the host.
 */
static void __attribute__((noreturn)) __not_in_flash_func(trap_park)(void)
{
    __asm volatile("cpsie i");
    ahprintf("\n[trap] parked after a thread-mode fault: pc %08lx lr %08lx\n",
        (unsigned long)watchdog_hw->scratch[1], (unsigned long)watchdog_hw->scratch[2]);
    disp_write(0, 3, "PARKED: see console  ");
    for (;;)
        __asm volatile("wfi");
}

void __attribute__((used)) __not_in_flash_func(trap_hardfault_c)(uint32_t *frame)
{
    watchdog_hw->scratch[0] = (TRAP_MAGIC << 20) | TRAP_FAULT_BIT
        | ((boot_stage_count & 0xffu) << 9) | (frame[7] & 0x1ffu);
    watchdog_hw->scratch[1] = frame[6];
    watchdog_hw->scratch[2] = frame[5];
    watchdog_hw->scratch[3] = *(volatile uint32_t *)0xE000ED28u;   // cfsr

    // thread mode (a panic's breakpoint, say): return into trap_park() with
    // interrupts on rather than reboot, so the console can still talk
    if ((frame[7] & 0x1ffu) == 0) {
        frame[6] = (uint32_t)&trap_park & ~1u;
        frame[7] = 0x01000000u;     // thumb, no exception
        return;
    }

    // the watchdog (armed at boot, so psm wdsel is set) reboots the chip
    // without clearing the scratch registers
    watchdog_hw->ctrl |= WATCHDOG_CTRL_TRIGGER_BITS;
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
        "b    trap_hardfault_c \n"
        "1:                 \n"
        "mrs  r0, msp       \n"
        "b    trap_hardfault_c \n");
}

/**
 * first thing in main(): pick up a report left by the previous run.
 */
static void trap_collect(void)
{
    uint32_t s0 = watchdog_hw->scratch[0];

    if ((s0 >> 20) == TRAP_MAGIC && ((s0 & TRAP_FAULT_BIT) || watchdog_caused_reboot())) {
        for (int i = 0; i < 4; i++)
            trap_report[i] = watchdog_hw->scratch[i];
        trap_pending = true;
    }

    watchdog_hw->scratch[0] = 0;
}

/**
 * once the display is up: show a pending report and stop for good (console
 * still served by the bring-up irq), or arm the watchdog for this run.
 */
static void trap_report_or_arm(void)
{
    char line[32];
    uint32_t s0 = trap_report[0];

    if (!trap_pending) {
        watchdog_enable(TRAP_WDT_MS, true);
        return;
    }

    watchdog_disable();

    if (s0 & TRAP_FAULT_BIT) {
        snprintf(line, sizeof(line), "%-21s", "FAULT");
        disp_write(0, 0, line);
        snprintf(line, sizeof(line), "p%08lx l%08lx", (unsigned long)trap_report[1], (unsigned long)trap_report[2]);
        disp_write(0, 1, line);
        snprintf(line, sizeof(line), "cfsr %08lx ex %lu   ", (unsigned long)trap_report[3], (unsigned long)(s0 & 0x1ffu));
        disp_write(0, 2, line);
    } else {
        snprintf(line, sizeof(line), "%-21s", "WATCHDOG");
        disp_write(0, 0, line);
        snprintf(line, sizeof(line), "%-21s", "irqs stopped");
        disp_write(0, 1, line);
        snprintf(line, sizeof(line), "%-21s", "");
        disp_write(0, 2, line);
    }
    snprintf(line, sizeof(line), "stage %-15lu", (unsigned long)((s0 >> 9) & 0xffu));
    disp_write(0, 3, line);

    ahprintf("[trap] %s s0 %08lx pc %08lx lr %08lx cfsr %08lx\n", (s0 & TRAP_FAULT_BIT) ? "fault" : "watchdog",
        (unsigned long)s0, (unsigned long)trap_report[1], (unsigned long)trap_report[2], (unsigned long)trap_report[3]);

    for (;;)
        tight_loop_contents();
}
#endif

#ifdef USB_DEVICE_BRINGUP_SERVICE
/**
 * BRING-UP ONLY: oled breadcrumb "<stage> <ms since boot> <bring-up timer irqs>".
 * the first number says whether the timer counts, the second whether alarm
 * interrupts fire.
 */
static void boot_stage(char const *name)
{
    char buf[32];
    uint32_t ticks = usb_device_bringup_ticks();

    // the stage number, for the trap report if this run dies
    boot_stage_count++;
    watchdog_hw->scratch[0] = (TRAP_MAGIC << 20) | ((boot_stage_count & 0xffu) << 9);

    snprintf(buf, sizeof(buf), "%-9.9s%5lu %5lu", name,
        (unsigned long)(time_us_32() / 1000u) % 100000u, (unsigned long)ticks % 100000u);
    dbgcons_boot_stage(buf);
}
#else
// bring-up breadcrumbs compile away in a normal build
#define boot_stage(name) do { } while (0)
#endif

/**
 * show the usb-c device port's state on the oled whenever it changes: whether
 * the expander that senses vbus answered, vbus itself, and the host's view.
 */
static void usb_c_status_task(void)
{
    static char const *shown_state = NULL;
    static uint32_t shown_attempts = 0;
    char const *state;
    uint32_t attempts = io_expander_attempts();

    if (!io_expander_present())
        state = "no iox";
    else if (!io_expander_vbus_present())
        state = "no vbus";
    else if (!usb_device_mounted())
        state = "no host";
    else if (tud_suspended())
        state = "suspend";
    else
        state = "mounted";

    if ((state == shown_state) && (attempts == shown_attempts))
        return;

    shown_state = state;
    shown_attempts = attempts;
    dbgcons_usb_c(state, attempts);
}

/**
 * the board's own controls: encoder and switch on the expander, and the BOOT
 * button. the oled menu will hang off these; for now the button long-press
 * cycles the output mode and everything else is just logged.
 */
static void board_controls_task(void)
{
    io_expander_event_t iox_event;

    io_expander_task();

    while ((iox_event = io_expander_poll_event()) != IO_EXPANDER_EVENT_NONE) {
        switch (iox_event) {
            case IO_EXPANDER_EVENT_ENC_CW:      menu_encoder_turn(+1); break;
            case IO_EXPANDER_EVENT_ENC_CCW:     menu_encoder_turn(-1); break;
            case IO_EXPANDER_EVENT_ENC_PRESS:   menu_encoder_switch(true); break;
            case IO_EXPANDER_EVENT_ENC_RELEASE: menu_encoder_switch(false); break;
            case IO_EXPANDER_EVENT_VBUS_ON:     ahprintf("[ctl] usb-c vbus on\n"); break;
            case IO_EXPANDER_EVENT_VBUS_OFF:    ahprintf("[ctl] usb-c vbus off\n"); break;
            default: break;
        }
    }

    // the menu takes the button whilst open, and a double-press to open it;
    // otherwise a 1 s hold steps to the next mode
    user_button_event_t button = user_button_task();

    // an update waiting for BOOT takes the press first
    if ((button != USER_BUTTON_EVENT_NONE) && !ota_button() && !menu_button(button)) {
        if (button == USER_BUTTON_EVENT_LONG_PRESS)
            run_mode_next();
    }
}

// main entry point
int main(void)
{
#ifdef USB_DEVICE_BRINGUP_SERVICE
    trap_collect();
#else
    reset_info_init();
#endif

    // the stored settings, before anything reads one (a flash read only)
    settings_init();
    profiles_init();

    // pio-usb needs clk_sys at a multiple of 12 MHz. first thing, so that every
    // divider computed from clock_get_hz(clk_sys) below (i2c, ws2812, keyboard
    // receiver, pio-usb itself) sees the final frequency
    set_sys_clock_khz(PIO_USB_SYS_CLOCK_KHZ, true);

    // no tinyusb bsp here (its board_init() would set its own clock and pio-usb
    // pins); just route stdout into the cdc console buffer
    usb_device_init();
#ifdef USB_DEVICE_BRINGUP_SERVICE
    usb_device_bringup_service_start();
#endif

    // board-level pads before any driver claims them: pull-ups on lines with
    // no external pull-up, usb mux held on the device port (boards that have them)
    board_pins_init();

    // status indicators come up first so a board with no display still shows
    // signs of life whilst the rest of the stack initialises
    status_led_init();

    // initialise the i2c controller and send the init sequence to the display
    disp_ssd_init();

    // the expander shares the display's i2c controller, so it comes second
    io_expander_init();
    amiga_sense_init();
    user_button_init();

    // say hello, trevor ("hello, trevor")
    dbgcons_init();
    apply_report_watchdog_reboot();
#ifndef USB_DEVICE_BRINGUP_SERVICE
    ahprintf("[boot] started by: %s\n", reset_info_reason());
#endif
    dbgcons_mode(run_mode_name(run_mode_get()));
    dbgcons_amiga_power(amiga_sense_is_on());

#ifdef USB_DEVICE_BRINGUP_SERVICE
    trap_report_or_arm();
#endif
    boot_stage("init");

    // initialise the usb host stack on the rhport from tusb_config.h
    boot_stage("usb host");
    pio_usb_host_configure();
    const tusb_rhport_init_t rhport_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUH_OPT_HIGH_SPEED ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
    };
    boot_stage("tuh init");
    tusb_init(BOARD_TUH_RHPORT, &rhport_init);

    // the device role is brought up from the main
    // loop once usb-c vbus is seen; see usb_device_task()

    // we're single arch right now, but in future this should hand off to whatever the
    // configured arch is
    boot_stage("amiga io");
    // claim the lines now, greet the amiga only once it is seen to be on (and
    // again every time it comes back on); the greeting no longer blocks
    amiga_init_io();
    if (amiga_sense_is_on())
        amiga_power_on();
    else
        amiga_power_off();

    boot_stage("mouse");
    // start amiga mouse emulation
    amiga_quad_mouse_init();

    // the de9 ports: mouse, joysticks, cd32, c64; claims pio1 before bluetooth
    de9_init();

    boot_stage("kbd in");
    // start receiving from a physically attached real amiga keyboard
    amiga_keyboard_recv_init();

#ifdef ENABLE_BLUETOOTH_HID
    bt_hid_init();
#endif
    // wi-fi on the same radio (rev7), off unless the settings say otherwise
    wifi_init();

    // the mode the settings remember, now that everything a switch touches is up
    run_mode_restore();
    menu_init();

    // mouse speed, display, watchdog: the stored settings take effect
    apply_settings();
    pads_init();

    boot_stage("loop");

    // bring-up breadcrumbs through the first pass of the loop only
    bool first_pass = true;
#define LOOP_STAGE(name) do { if (first_pass) boot_stage(name); } while (0)
// where the loop is, should the watchdog have to restart the board
#ifdef USB_DEVICE_BRINGUP_SERVICE
#define AT(stage)
#else
#define AT(stage) reset_info_stage(stage)
#endif

    while (1) {
        LOOP_STAGE("L tuh");
        // run host mode jobs (hotplug events, packet io callbacks)
        AT(RESET_STAGE_USB_HOST);
        tuh_task();

        LOOP_STAGE("L usbd");
        // device mode jobs on the usb-c port, which follows its vbus
        AT(RESET_STAGE_USB_DEVICE);
#ifndef USB_DEVICE_BRINGUP_SERVICE
        usb_device_task(io_expander_vbus_present());
#endif
        usb_c_status_task();

        // commands typed on the usb-c console
        AT(RESET_STAGE_CONSOLE);
        console_cmd_task();

        // the settings menu: a held help key opens it
        AT(RESET_STAGE_MENU);
        menu_task();

        // pads: usb pad start-up and leds, players, outputs
        AT(RESET_STAGE_PADS);
        hid_app_task();
        pads_task();
        de9_task();

        // the watchdog, if the settings ask for one
        AT(RESET_STAGE_APPLY);
        apply_task();

#ifdef ENABLE_BLUETOOTH_HID
        AT(RESET_STAGE_BLUETOOTH);
        bt_hid_task();
#endif
        // wi-fi: follow the settings, store a save from the page
        AT(RESET_STAGE_WIFI);
        wifi_task();

        // firmware updates: accept this one after a clean start, write an
        // uploaded one to the free slot
        AT(RESET_STAGE_OTA);
        ota_task();

        LOOP_STAGE("L kbd");
        // drain + inject any keycodes from the real amiga keyboard
        AT(RESET_STAGE_AMIGA);
        amiga_keyboard_recv_task();

        LOOP_STAGE("L power");
        // greet or forget the amiga as it is switched on and off
        amiga_power_task();

        LOOP_STAGE("L kbd svc");
        // amiga keyboard service routine
        amiga_service();

        LOOP_STAGE("L ctl");
        // encoder, vbus sense and the BOOT button
        AT(RESET_STAGE_CONTROLS);
        board_controls_task();

        LOOP_STAGE("L led");
        // advance the status indicators
        status_led_task();

        if (first_pass) {
            boot_stage("ok");
            first_pass = false;
        }
    }

    return 0;
}
