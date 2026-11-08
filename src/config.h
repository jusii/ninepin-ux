/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * hid-pico configuration
 *
 * PLEASE NOTE ALL PIN DESIGNATIONS ARE GPIO PIN NUMBERS - NOT PHYSICAL PIN NUMBERS
 */

#ifndef _CONFIG_H
#define _CONFIG_H

#if defined(BOARD_HIDPICO_REV2)
#  define HAS_SCREEN
#  define HAS_KEYBOARD
#  define HAS_PORT1
#  define HAS_PORT2

#  define I2C_PORT      i2c0
#  define I2C_PIN_SDA   4
#  define I2C_PIN_SCL   5
#  define I2C_IRQN      23

#  define KBD_AMIGA_RST 10
#  define KBD_AMIGA_DAT 11
#  define KBD_AMIGA_CLK 12

#  define QM1_AMIGA_HQ  7
#  define QM1_AMIGA_VQ  6
#  define QM1_AMIGA_H   9
#  define QM1_AMIGA_V   8
#  define QM1_AMIGA_B1  22
#  define QM1_AMIGA_B2  26
#  define QM1_AMIGA_B3  27

#  ifdef PORT2_ROLE_KEYBOARD
#    error PORT2_ROLE=KEYBOARD pin mapping is not defined for REV2 yet
#  endif
#elif defined(BOARD_HIDPICO_REV4)
#  define HAS_SCREEN
#  define HAS_KEYBOARD
#  define HAS_PORT1
#  define HAS_PORT2

#  define I2C_PORT      i2c1
#  define I2C_PIN_SDA   2
#  define I2C_PIN_SCL   3
#  define I2C_IRQN      24 // this is tied to the i2c port being used so get the right one!

#  define KBD_AMIGA_RST 4
#  define KBD_AMIGA_DAT 5
#  define KBD_AMIGA_CLK 6

#  define QM1_AMIGA_HQ  7
#  define QM1_AMIGA_VQ  8
#  define QM1_AMIGA_H   9
#  define QM1_AMIGA_V   10
#  define QM1_AMIGA_B1  11
#  define QM1_AMIGA_B2  12
#  define QM1_AMIGA_B3  13

#  ifdef PORT2_ROLE_KEYBOARD
// controller port 2 is repurposed as a real amiga keyboard input. these are
// the existing port 2 'h' and 'v' gpios (kicad nets: lcont2 h / lcont2 v),
// reusing their bss138 level shifters. wired to a real keyboard, not the amiga.
#    define HAS_KBD_IN
#    define KBD_REAL_CLK 26 // port 2 'h' line -> real keyboard kclk (pico input)
#    define KBD_REAL_DAT 27 // port 2 'v' line -> real keyboard kdat (bidirectional)
#  endif
#elif defined(BOARD_HIDPICO_REV6)
#  define HAS_SCREEN
#  define HAS_KEYBOARD
#  define HAS_PORT1
#  define HAS_PORT2

#  define I2C_PORT      i2c1
#  define I2C_PIN_SDA   2
#  define I2C_PIN_SCL   3

#  define KBD_AMIGA_RST 4
#  define KBD_AMIGA_DAT 5
#  define KBD_AMIGA_CLK 6

#  define QM1_AMIGA_HQ  7
#  define QM1_AMIGA_VQ  8
#  define QM1_AMIGA_H   9
#  define QM1_AMIGA_V   10
#  define QM1_AMIGA_B1  11
#  define QM1_AMIGA_B2  12
#  define QM1_AMIGA_B3  13

// rev6 has a dedicated real amiga keyboard input on its own level shifters,
// so controller port 2 is left free for (future) joystick emulation.
#  define HAS_KBD_IN
#  define KBD_REAL_CLK  14 // real keyboard kclk (pico input)
#  define KBD_REAL_DAT  15 // real keyboard kdat (bidirectional)

#  define HAS_STATUS_LEDS
#  define LED_STATUS1   16
#  define LED_STATUS2   17

#  ifdef PORT2_ROLE_KEYBOARD
#    error PORT2_ROLE=KEYBOARD is not applicable to REV6 (keyboard input is dedicated)
#  endif
#elif defined(BOARD_HIDPICO_REV7)
// chip-down rp2350a + rm2 (sdk board header: boards/amigahid_rev7.h). this block
// is the single source of truth for the application pins; it mirrors the GPIO
// dict in kicad/rev7/design.py, so a gpio moved on the schematic is a one-line
// change here. the rm2 wireless pins (GPIO23 wl_on, 24 wl_d, 25 wl_cs, 29
// wl_clk) belong to the sdk and are set in the board header, not here.
#  define HAS_SCREEN
#  define HAS_KEYBOARD
#  define HAS_PORT1
#  define HAS_PORT2

#  define I2C_PORT      i2c0
#  define I2C_PIN_SDA   0       // i2c_sda (4.7k to 3v3 on the board)
#  define I2C_PIN_SCL   1       // i2c_scl
// the tca9554 sharing this bus is a 400 kHz (fast mode) part, so the bus runs at
// that rather than the 1 MHz the display alone would take; a fast-mode device is
// not specified to ignore fast-mode-plus traffic addressed to someone else
#  define I2C_BAUD      400000

#  define KBD_AMIGA_RST 4       // kbreset_lo
#  define KBD_AMIGA_DAT 5       // kbdata_lo
#  define KBD_AMIGA_CLK 6       // kbclock_lo

#  define QM1_AMIGA_B3  7       // cont1_b3_lo (GPIO7-13 follow the bus-switch pin order, see design.py)
#  define QM1_AMIGA_B2  8       // cont1_b2_lo
#  define QM1_AMIGA_V   9       // cont1_v_lo
#  define QM1_AMIGA_B1  10      // cont1_b1_lo
#  define QM1_AMIGA_H   11      // cont1_h_lo
#  define QM1_AMIGA_VQ  12      // cont1_vq_lo
#  define QM1_AMIGA_HQ  13      // cont1_hq_lo

// dedicated real amiga keyboard input, as on rev6
#  define HAS_KBD_IN
#  define KBD_REAL_CLK  14      // kbin_clk_lo, real keyboard kclk (input)
#  define KBD_REAL_DAT  15      // kbin_dat_lo, real keyboard kdat (bidirectional)

// controller port 2. note the order differs from port 1. both ports are
// driven by de9.c: mouse, joystick, cd32 pad (amiga), joystick or 1351 (c64)
#  define HAS_DE9_OUTPUTS
#  define QM2_AMIGA_V   16      // cont2_v_lo (GPIO16-22: the ADC pins are not 5 V tolerant)
#  define QM2_AMIGA_B1  17      // cont2_b1_lo
#  define QM2_AMIGA_H   18      // cont2_h_lo
#  define QM2_AMIGA_VQ  19      // cont2_vq_lo
#  define QM2_AMIGA_HQ  20      // cont2_hq_lo
#  define QM2_AMIGA_B2  21      // cont2_b2_lo
#  define QM2_AMIGA_B3  22      // cont2_b3_lo

// one ws2812b-2020 replaces rev6's two plain leds; status_led.c paints the
// rev6 led 1 / led 2 states onto its green and blue channels
#  define HAS_STATUS_LEDS
#  define HAS_STATUS_WS2812
#  define LED_STATUS_WS2812 2   // ws_drv: gate of Q1 (2n7002 inverter, 1k pull-up to LED_VDD) -> led din
#  define LED_STATUS_WS2812_INVERT  // the inverter means the gpio output must be inverted

// no usb mux any more (2026-10-15): the rp2350's native phy is on the usb-c
// connector (device port: flashing, debug cdc, and the hid-device mode for a
// host such as the commodore 64 ultimate), and the usb-a host port is driven by
// pio-usb on GPIO26 (D+) / GPIO27 (D-) through 27R and a usblc6. the pio-usb
// host must not take pio0, which keyboard_recv owns.
#  define HAS_PIO_USB_HOST
#  define PIO_USB_DP_PIN    26  // pio_dp (D- is always DP+1 for pio-usb)
#  define PIO_USB_PIO       2   // pio2, all of it: pio0 = keyboard_recv; the ws2812 skips this pio (status_led.c), cyw43 finds it full
// pio-usb bit-bangs full speed and needs clk_sys at a multiple of 12 MHz; the
// rp2350's 150 MHz default is not one. main() sets this before anything derives
// a divider from clk_sys (every pio, i2c and adc divider in this tree is computed
// at init from clock_get_hz(), none is hard-coded)
#  define PIO_USB_SYS_CLOCK_KHZ 120000

// the native usb phy is a device port on the usb-c connector: a composite cdc
// (debug console - there is no uart) + hid (keyboard, mouse, gamepad) device.
// its D+ pull-up is only presented while the expander sees usb-c vbus.
#  define HAS_USB_DEVICE
// the settings menu on the oled (menu.c): keyboards, BOOT, encoder, console
#  define HAS_MENU
// wi-fi and the settings page share the rm2 with bluetooth: builds with the
// radio only (wifi.c)
#  ifdef ENABLE_BLUETOOTH_HID
#    define HAS_WIFI
#  endif
// a/b partition slots and try-before-you-buy images (ota.c): every rev7 build
// accepts itself after a clean start; uploads come over wi-fi where there is one
#  define HAS_OTA
#  define USB_DEVICE_VID    0xcafe  // tinyusb's example vid; see the report's open questions
#  define USB_DEVICE_PID    0x4a50  // "JP"; arbitrary within the example space

// the BOOT button (SW1, qspi_ss through 1k) read back at runtime with the
// bootsel trick; see util/user_button.h for the constraints that come with it
#  define HAS_USER_BUTTON

// tca9554 i2c i/o expander at 0x20 on the oled bus: P0 enc_a, P1 enc_b, P2 enc_sw,
// P3 enc_spare (2x3 encoder header J7), P4 = usb-c vbus sense (4.7k/10k divider),
// P5-P7 unused (the tca9554's internal pull-ups hold them). /INT is open drain
// on GPIO3: enable the internal pull-up.
#  define HAS_IO_EXPANDER
#  define IO_EXPANDER_ADDR  0x20
#  define IO_EXPANDER_INT   3   // exp_int
#  define IO_EXPANDER_P_ENC_A   0
#  define IO_EXPANDER_P_ENC_B   1
#  define IO_EXPANDER_P_ENC_SW  2
#  define IO_EXPANDER_P_ENC_SP  3
#  define IO_EXPANDER_P_VBUS    4

// amiga-on sense: 100k/100k divider (+100n) from AMIGA_IO5V, the rail the bus
// switches run on (one schottky below the amiga's 5 v, fed from the keyboard
// connector or either DE9 pin 7): about 2.2-2.4 v with the amiga on, 0 v off.
// ADC2 on GPIO28, which is not 5 v tolerant and sees the rail through 100k only.
// board_pins_init() parks the pad as an analogue input (no pulls, digital input
// off - also the RP2350-E9 precaution); thresholds ~1.5 v on / ~1.2 v off.
#  define HAS_AMIGA_SENSE
#  define AMIGA_SENSE_PIN   28  // amiga_sense
#  define AMIGA_SENSE_ADC   2
#  define AMIGA_SENSE_VREF_MV   3300    // adc_avdd is +3V3 through 33R
#  define AMIGA_SENSE_ON_MV     1500    // rising above this: the amiga is on
#  define AMIGA_SENSE_OFF_MV    1200    // falling below this: the amiga is off

// rev7 dropped rev6's 3v3-side pull-ups: with the amiga off the cbtd3861 bus
// switches open and the rp2350 side floats, so every _lo line needs the internal
// pull-up; the keyboard-in lines sit behind bss138 shifters (Q2/Q3) whose source
// side is likewise pulled up here. board_pins_init() enables these before any
// driver touches the pins.
#  define BOARD_PULLUP_PINS { \
       KBD_AMIGA_RST, KBD_AMIGA_DAT, KBD_AMIGA_CLK, \
       QM1_AMIGA_HQ, QM1_AMIGA_VQ, QM1_AMIGA_H, QM1_AMIGA_V, \
       QM1_AMIGA_B1, QM1_AMIGA_B2, QM1_AMIGA_B3, \
       KBD_REAL_CLK, KBD_REAL_DAT, \
       QM2_AMIGA_HQ, QM2_AMIGA_VQ, QM2_AMIGA_H, QM2_AMIGA_V, \
       QM2_AMIGA_B1, QM2_AMIGA_B2, QM2_AMIGA_B3 }

#  ifdef PORT2_ROLE_KEYBOARD
#    error PORT2_ROLE=KEYBOARD is not applicable to REV7 (keyboard input is dedicated)
#  endif
#else
#  error Board type has not been defined; check cmake command line
#endif

#endif // _CONFIG_H
