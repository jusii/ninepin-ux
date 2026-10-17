/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * main entry point for amigahid-pico.
 */

#include "config.h"

// these reside within the tinyusb sdk and are not part of this project source
#ifndef HAS_USB_DEVICE
#include "bsp/board.h"
#endif
#include "tusb.h"

#ifdef HAS_PIO_USB_HOST
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "pio_usb.h"
#endif

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
#include "util/status_led.h"
#include "util/user_button.h"

#include "tusb_config.h"

#ifdef HAS_USB_DEVICE
#include "usb_device.h"
#endif

#ifdef ENABLE_BLUETOOTH_HID
#include "bt_hid.h"
#endif

// defined within usb_hid.c
extern void hid_app_task(void);

#ifdef HAS_PIO_USB_HOST
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
#endif

#ifdef HAS_AMIGA_SENSE
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
#endif

#if defined(HAS_IO_EXPANDER) || defined(HAS_USER_BUTTON)
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
            case IO_EXPANDER_EVENT_ENC_CW:      ahprintf("[ctl] encoder +1\n"); break;
            case IO_EXPANDER_EVENT_ENC_CCW:     ahprintf("[ctl] encoder -1\n"); break;
            case IO_EXPANDER_EVENT_ENC_PRESS:   ahprintf("[ctl] encoder switch down\n"); break;
            case IO_EXPANDER_EVENT_ENC_RELEASE: ahprintf("[ctl] encoder switch up\n"); break;
            case IO_EXPANDER_EVENT_VBUS_ON:     ahprintf("[ctl] usb-c vbus on\n"); break;
            case IO_EXPANDER_EVENT_VBUS_OFF:    ahprintf("[ctl] usb-c vbus off\n"); break;
            default: break;
        }
    }

    switch (user_button_task()) {
        case USER_BUTTON_EVENT_PRESS:
            ahprintf("[ctl] boot button press\n");
            break;
        case USER_BUTTON_EVENT_LONG_PRESS:
            run_mode_next();
            break;
        default:
            break;
    }
}
#endif

// main entry point
int main(void)
{
#ifdef HAS_PIO_USB_HOST
    // pio-usb needs clk_sys at a multiple of 12 MHz. first thing, so that every
    // divider computed from clock_get_hz(clk_sys) below (i2c, ws2812, keyboard
    // receiver, pio-usb itself) sees the final frequency
    set_sys_clock_khz(PIO_USB_SYS_CLOCK_KHZ, true);
#endif

#ifdef HAS_USB_DEVICE
    // no tinyusb bsp here (its board_init() would set its own clock and pio-usb
    // pins); just route stdout into the cdc console buffer
    usb_device_init();
#else
    // tinyusb board init; led, uart, button, usb
    board_init();
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
#if defined(HAS_USB_DEVICE) || defined(HAS_AMIGA_SENSE)
    dbgcons_mode(run_mode_name(run_mode_get()));
    dbgcons_amiga_power(amiga_sense_is_on());
#endif

    // initialise the usb host stack on the rhport from tusb_config.h, first
    // pointing the usb mux (on boards with one) at the host port
#ifdef HAS_PIO_USB_HOST
    pio_usb_host_configure();
#endif
    board_pins_usb_host();
    const tusb_rhport_init_t rhport_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUH_OPT_HIGH_SPEED ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL,
    };
    tusb_init(BOARD_TUH_RHPORT, &rhport_init);

    // the device role (boards with HAS_USB_DEVICE) is brought up from the main
    // loop once usb-c vbus is seen; see usb_device_task()

    // we're single arch right now, but in future this should hand off to whatever the
    // configured arch is
#ifdef HAS_AMIGA_SENSE
    // claim the lines now, greet the amiga only once it is seen to be on (and
    // again every time it comes back on); the greeting no longer blocks
    amiga_init_io();
    if (amiga_sense_is_on())
        amiga_power_on();
    else
        amiga_power_off();
#else
    amiga_init();
#endif

    // start amiga mouse emulation
    amiga_quad_mouse_init();

#ifdef HAS_KBD_IN
    // start receiving from a physically attached real amiga keyboard
    amiga_keyboard_recv_init();
#endif

#ifdef ENABLE_BLUETOOTH_HID
    bt_hid_init();
#endif

    while (1) {
        // run host mode jobs (hotplug events, packet io callbacks)
        tuh_task();

#ifdef HAS_USB_DEVICE
        // device mode jobs on the usb-c port, which follows its vbus
        usb_device_task(io_expander_vbus_present());
#endif

#ifdef ENABLE_BLUETOOTH_HID
        bt_hid_task();
#endif

#ifdef HAS_KBD_IN
        // drain + inject any keycodes from the real amiga keyboard
        amiga_keyboard_recv_task();
#endif

#ifdef HAS_AMIGA_SENSE
        // greet or forget the amiga as it is switched on and off
        amiga_power_task();
#endif

        // amiga keyboard service routine
        amiga_service();

#if defined(HAS_IO_EXPANDER) || defined(HAS_USER_BUTTON)
        // encoder, vbus sense and the BOOT button
        board_controls_task();
#endif

        // advance the status indicators
        status_led_task();
    }

    return 0;
}
