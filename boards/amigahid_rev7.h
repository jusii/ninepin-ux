/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * pico-sdk board header for the rev7 board: a chip-down rp2350a with a 16 MB
 * w25q128 qspi flash, a 12 MHz crystal and a raspberry pi rm2 (cyw43439) wired
 * exactly as on the pico 2 w. selected with -DPICO_BOARD=amigahid_rev7, which
 * the top-level CMakeLists.txt does for you when BOARD_TYPE=BOARD_HIDPICO_REV7.
 *
 * this only describes the chip, flash, clock and radio - everything the sdk
 * itself needs. the application pin map (keyboard, controller ports, status
 * led, usb mux, amiga sense) lives in src/config.h under BOARD_HIDPICO_REV7.
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_AMIGAHID_REV7_H
#define _BOARDS_AMIGAHID_REV7_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)
pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)

// For board detection
#define AMIGAHID_REV7

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- CLOCK ---
// 12 MHz crystal (abm8-272-t3), the sdk default; spelt out for the reader
#ifndef XOSC_HZ
#define XOSC_HZ 12000000u
#endif

// --- UART --- (uart0 on its aux pins; UART_FUNCSEL_NUM picks GPIO_FUNC_UART_AUX)
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 2
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 3
#endif

// --- LED ---
// no PICO_DEFAULT_LED_PIN: the only indicator is the ws2812 on GPIO16, which
// src/util/status_led.c drives itself. it is deliberately not advertised as
// PICO_DEFAULT_WS2812_PIN so that nothing in the sdk or tinyusb's bsp claims it.
// no CYW43_WL_GPIO_LED_PIN: the rm2 has no led on its wl gpios.

// --- I2C ---
// the oled is on i2c1, GPIO2/3 (see src/config.h); the sdk default instance is
// only used by sdk examples and is left at the pico 2 w values
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 1
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 2
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 3
#endif

// --- SPI ---
// no default spi instance; every free gpio is spoken for

// --- FLASH ---
// w25q128jvsiq, 16 MB
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// the core regulator is the rp2350's own switcher; there is no external smps
// and so no PICO_SMPS_MODE_PIN

// --- CYW43 (RM2) ---
// same pin assignment as the pico 2 w. GPIO29 is wl_clk only: unlike the pico
// 2 w there is no vsys divider sharing it, so CYW43_USES_VSYS_PIN and
// PICO_VSYS_PIN are not defined. the rm2's wl gpio0-2 are unconnected, so there
// is no CYW43_WL_GPIO_LED_PIN, CYW43_WL_GPIO_SMPS_PIN or CYW43_WL_GPIO_VBUS_PIN.
#ifndef CYW43_WL_GPIO_COUNT
#define CYW43_WL_GPIO_COUNT 3
#endif

// cyw43 SPI pins can't be changed at runtime
#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 0
#endif

// gpio pin to power up the cyw43 chip (WL_ON and BT_ON both tied to it)
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 23u
#endif

// gpio pin for spi data out to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 24u
#endif

// gpio pin for spi data in from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 24u
#endif

// gpio (irq) pin for the irq line from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 24u
#endif

// gpio pin for the spi clock line to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 29u
#endif

// gpio pin for the spi chip select to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 25u
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
