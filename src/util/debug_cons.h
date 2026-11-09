/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * debug console routines.
 */

#ifndef _PLATFORM_COMMON_DEBUG_CONS_H
#define _PLATFORM_COMMON_DEBUG_CONS_H

#include <stdbool.h>
#include <stdint.h>

enum debug_plug_types { AP_H_UNKNOWN, AP_H_KEYBOARD, AP_H_MOUSE, AP_H_CONTROLLER };

#define ESC         "\033"

#define VT_ED_EOS   ESC "[0J"
#define VT_ED_BOS   ESC "[1J"
#define VT_ED_CLS   ESC "[2J"
#define VT_EL_EOL   ESC "[0K"
#define VT_EL_BOL   ESC "[1K"
#define VT_EL_LIN   ESC "[2K"

#define VT_CUP_HOM  ESC "[H"
#define VT_CUP_POS  ESC "[%d;%dH"

void dbgcons_init();

void dbgcons_print_counters();

void dbgcons_plug(enum debug_plug_types devtype);

void dbgcons_unplug(enum debug_plug_types devtype);

void dbgcons_amiga_key(uint8_t incode, uint8_t outcode, char *updown);

void dbgcons_hid_status(uint8_t dev_addr, uint8_t instance, uint8_t hid_protocol, bool receive_ok, uint8_t report_count, bool mounted);

void dbgcons_mouse_report(int16_t x, int16_t y, uint8_t buttons);

void dbgcons_mouse_wheel(int8_t wheel);

void dbgcons_tankmouse_status(uint16_t queued, uint16_t requests, uint16_t responses);

/**
 * board status line (boards with HAS_USB_DEVICE / HAS_AMIGA_SENSE; no-ops
 * elsewhere): where input goes, and whether the amiga is powered.
 */
void dbgcons_mode(char const *mode);

void dbgcons_amiga_power(bool on);

/**
 * usb-c device port state on oled line 1 (HAS_USB_DEVICE boards without
 * bluetooth; a no-op elsewhere), with the number of attempts the i/o expander
 * that senses its vbus took to answer.
 */
void dbgcons_usb_c(char const *state, uint32_t iox_attempts);

/**
 * bring-up breadcrumb (AHP_BRINGUP builds): the text on oled line 3 and the
 * console; blocks ~40 ms so the frame is on the panel before the caller moves on.
 */
void dbgcons_boot_stage(char const *stage);

void dbgcons_bt_status(char const *status);

/**
 * @brief The display setting: 0 status, 1 hid diagnostics, 2 mouse
 *        diagnostics, 3 off (the panel blanked; the menu still shows).
 */
void dbgcons_display_mode(uint8_t mode);

void dbgcons_bt_passkey(char const *message);

void dbgcons_bt_passkey_clear(void);

/**
 * @brief The wi-fi state for oled line 3 ("wifi ap amigahid-1a2b",
 *        "wifi 192.168.1.23"), shown whilst no bluetooth prompt is up; NULL
 *        or "" clears it. Main loop.
 */
void dbgcons_wifi(char const *text);

/**
 * @brief A firmware update's state for oled line 3 ("BOOT: install (25s)"),
 *        over the wi-fi state, under a bluetooth prompt; NULL or "" clears
 *        it. Main loop.
 */
void dbgcons_ota(char const *text);

/**
 * @brief "net ctl active" on oled line 3 while network control is in use
 *        (under an update's text and a bluetooth prompt). Main loop.
 */
void dbgcons_net(bool active);

#endif // _PLATFORM_COMMON_DEBUG_CONS_H
