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

// controller port 2. nothing drives it yet (PORT2_ROLE=JOYSTICK is a no-op);
// the pins are named so the pull-ups below cover them and a future driver has
// nothing to look up. note the order differs from port 1.
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
#  define LED_STATUS_WS2812 26  // ws_drv: gate of Q1 (2n7002 inverter, 1k pull-up to LED_VDD) -> led din
#  define LED_STATUS_WS2812_INVERT  // the inverter means the gpio output must be inverted

// ts3usb30e 2:1 mux in front of the rp2350's only usb phy. low (r48 pull-down,
// so also the state through reset and bootsel) = usb-c, the device port used
// for flashing; high = usb-a, the host port the firmware runs on.
#  define HAS_USB_MUX
#  define USB_MUX_SEL       27  // usb_sel
#  define USB_MUX_SEL_HOST  1

// amiga-on sense: 100k/100k divider (+100n) from the amiga's fused 5v, so about
// 2.5v with the amiga on and 0v with it off. ADC2.
// TODO: nothing reads it yet. board_pins_init() parks the pad as an analogue
// input (no pulls, digital input off) so a future reader gets an unloaded
// divider; the reader itself (hardware_adc, adc_select_input(2)) and what to do
// with the result - e.g. hold off amiga_init()'s initpower until the amiga is
// up - are still to be written.
#  define HAS_AMIGA_SENSE
#  define AMIGA_SENSE_PIN   28  // amiga_sense
#  define AMIGA_SENSE_ADC   2

// rev7 dropped rev6's 3v3-side pull-ups: with the amiga off the cbtd3861 bus
// switches open and the rp2350 side floats, so every _lo line needs the internal
// pull-up. board_pins_init() enables these before any driver touches the pins.
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
