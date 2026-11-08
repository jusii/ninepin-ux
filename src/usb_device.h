/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb device role on the native phy (boards with HAS_USB_DEVICE).
 *
 * the device runs alongside the host stack (tinyusb dual role: device on the
 * native rhport 0, host on pio-usb rhport 1) and offers:
 *
 *   - a cdc acm debug console. stdout is routed into it through an sdk stdio
 *     driver; output is buffered from boot and drained whilst a terminal holds
 *     dtr, so nothing printed before the cable went in is lost (up to the
 *     buffer size).
 *   - hid keyboard, mouse and gamepad interfaces, fed by the input bridge in
 *     RUN_MODE_USB_DEVICE_PASSTHROUGH (util/run_mode.h).
 *
 * the D+ pull-up is only ever presented whilst the usb-c port has vbus (the
 * caller passes that in from the i/o expander): the device stack is not even
 * initialised until vbus is first seen, since tinyusb's rp2 dcd_init()
 * connects straight away, and it is disconnected again whenever vbus goes.
 * presenting the pull-up to an unpowered port would back-feed the vbus sense
 * through the usblc6.
 *
 * everything here is core0 main loop only, except the stdio hook, which may be
 * called from any context on core0 (the bluetooth background interrupt prints).
 */

#ifndef _USB_DEVICE_H
#define _USB_DEVICE_H

#include "config.h"

#ifdef HAS_USB_DEVICE

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tusb.h"

// hid instance numbers, in interface order (see usb_descriptors.c)
#define USB_DEVICE_HID_KEYBOARD 0
#define USB_DEVICE_HID_MOUSE    1
#define USB_DEVICE_HID_GAMEPAD  2

/**
 * @brief Hook stdout into the cdc console buffer.
 *
 * Call first thing in main(), so boot messages are captured. Does not touch the
 * usb hardware.
 */
void usb_device_init(void);

/**
 * @brief Run the device side: bring the stack up or connect / disconnect it
 *        to follow vbus, run tud_task(), drain the console, send pending hid
 *        reports.
 *
 * @param vbus_present  true whilst the usb-c port has vbus
 */
void usb_device_task(bool vbus_present);

#ifdef USB_DEVICE_BRINGUP_SERVICE
/**
 * @brief BRING-UP ONLY: bring the device stack up now, without waiting for
 *        vbus sense, and run usb_device_task() from a 1 ms high-priority timer
 *        interrupt instead of the main loop, so the cdc console keeps working
 *        while main() is stuck in init. The main loop must not also call
 *        usb_device_task(). Only for a board powered from usb-c alone.
 */
void usb_device_bringup_service_start(void);

/**
 * @return how many times the bring-up timer interrupt has run
 */
uint32_t usb_device_bringup_ticks(void);
#endif

/**
 * @return true whilst a host has the device configured
 */
bool usb_device_mounted(void);

#define USB_DEVICE_CONSOLE_LINE_MAX 64

/**
 * @brief Take the last line typed on the cdc console, if one is complete.
 *        Main loop only (the line may ask for a flash write).
 *
 * @return true and the line (no line ending) in line, else false
 */
bool usb_device_console_line(char *line, size_t size);

/**
 * @return the host's caps lock from the led report it last sent the keyboard
 *         interface: 1 on, 0 off, -1 not known (nothing sent since mount)
 */
int usb_device_host_caps_lock(void);

/**
 * @brief Press and release caps lock once on the host, as its own two reports
 *        so the two are never merged away. Ignored whilst a tap is in flight.
 */
void usb_device_caps_tap(void);

/**
 * @return true from usb_device_caps_tap() until its release has gone out
 */
bool usb_device_caps_tap_pending(void);

/**
 * @brief Pass a keyboard report from an input slot to the usb-c host.
 *
 * Reports from several slots are merged (modifiers or-ed, up to six keys).
 */
void usb_device_keyboard(uint8_t slot, hid_keyboard_report_t const *report);

/**
 * @brief Pass a mouse report from an input slot to the usb-c host. Motion is
 *        accumulated until the endpoint is free; buttons are or-ed across slots.
 */
void usb_device_mouse(uint8_t slot, hid_mouse_report_t const *report);

/**
 * @brief Send a gamepad report. Nothing feeds this yet (the host side does not
 *        parse joysticks); it is here so the interface has a producer to grow.
 */
void usb_device_gamepad(hid_gamepad_report_t const *report);

/**
 * @brief Release whatever one slot holds down on the usb-c host.
 */
void usb_device_release_slot(uint8_t slot);

/**
 * @brief Release every key and button on the usb-c host (leaving passthrough).
 */
void usb_device_release_all(void);

#endif // HAS_USB_DEVICE

#endif // _USB_DEVICE_H
