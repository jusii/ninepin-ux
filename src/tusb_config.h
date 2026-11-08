/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb stack configuration; this file is included from the project source
 * by the tinyusb sdk so must be available within the include path
 *
 * one section per role:
 *   common  - mcu, os, memory placement
 *   host    - every board. rev2-rev6 run it on the native phy (rhport 0); rev7
 *             (HAS_PIO_USB_HOST) runs it on pio-usb as rhport 1, because its
 *             native phy is wired to the usb-c connector instead
 *   device  - rev7 only (HAS_USB_DEVICE): the native phy on rhport 0, as a
 *             composite cdc (debug console) + hid (keyboard, mouse, gamepad)
 */

#ifndef _TUSB_CONFIG_H
#define _TUSB_CONFIG_H

#include "config.h"

//--------------------------------------------------------------------
// common
//--------------------------------------------------------------------

// should be defined by the pico board.h
#ifndef CFG_TUSB_MCU
#   error CFG_TUSB_MCU is missing; please check your configuration (ported to another board?)
#endif

#ifndef CFG_TUSB_OS
#   define CFG_TUSB_OS OPT_OS_NONE
#endif

#ifndef CFG_TUSB_MEM_SECTION
#   define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#   define CFG_TUSB_MEM_ALIGN __attribute__ ((aligned(4)))
#endif

//--------------------------------------------------------------------
// host
//--------------------------------------------------------------------

#ifdef HAS_PIO_USB_HOST
// the usb-a port is bit-banged by pico-pio-usb; tinyusb numbers that root port 1
#   define CFG_TUH_RPI_PIO_USB 1
#   ifndef BOARD_TUH_RHPORT
#       define BOARD_TUH_RHPORT 1
#   endif
#else
// the native phy is the host port
#   ifndef BOARD_TUH_RHPORT
#       define BOARD_TUH_RHPORT 0
#   endif
#endif

// maximum speed permitted (which on a pico is usb 1.1)
#ifndef BOARD_TUH_MAX_SPEED
#   define BOARD_TUH_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#endif

// enable the host stack (if not explicitly enabled)
#ifndef CFG_TUH_ENABLED
#   define CFG_TUH_ENABLED 1
#endif

// max speed from board
#define CFG_TUH_MAX_SPEED BOARD_TUH_MAX_SPEED

// usb descriptor buffer size
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// supported devices & interfaces; tusb examples use a convention of multi-purpose
// constants where >0 may also mean multiple endpoints on a single device
#ifdef HAS_PIO_USB_HOST
// pico-pio-usb keeps a fixed table of PIO_USB_DEVICE_CNT (4) devices; one hub
// plus four devices behind it is what tinyusb's own pio-usb examples run
#   define CFG_TUH_HUB 1
#else
#   define CFG_TUH_HUB 8 // permit usb hubs (pico has a single port)
#endif
#define CFG_TUH_CDC 0 // no serial
#define CFG_TUH_HID 8 // keyboard/mouse/joystick; 4 endpoints maximum per device
#define CFG_TUH_MSC 0 // no mass storage
#define CFG_TUH_VENDOR 0 // not sure what this is but no anyway

// max device support (excluding hub)
#ifdef HAS_PIO_USB_HOST
#   define CFG_TUH_DEVICE_MAX 4
#else
#   define CFG_TUH_DEVICE_MAX 16
#endif

// hid event buffer sizes
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

//--------------------------------------------------------------------
// device
//--------------------------------------------------------------------

#ifdef HAS_USB_DEVICE
#   ifndef BOARD_TUD_RHPORT
#       define BOARD_TUD_RHPORT 0
#   endif
#   ifndef BOARD_TUD_MAX_SPEED
#       define BOARD_TUD_MAX_SPEED OPT_MODE_DEFAULT_SPEED
#   endif

#   define CFG_TUD_ENABLED 1
#   define CFG_TUD_MAX_SPEED BOARD_TUD_MAX_SPEED
#   define CFG_TUD_ENDPOINT0_SIZE 64

// one cdc (debug console) and three hid interfaces (boot keyboard, boot mouse,
// gamepad), in that order; usb_device.h names the hid instances
#   define CFG_TUD_CDC 1
#   define CFG_TUD_HID 4
#   define CFG_TUD_MSC 0
#   define CFG_TUD_MIDI 0
#   define CFG_TUD_VENDOR 0

#   define CFG_TUD_CDC_RX_BUFSIZE 64
#   define CFG_TUD_CDC_TX_BUFSIZE 256
#   define CFG_TUD_CDC_EP_BUFSIZE 64

// big enough for the largest report (gamepad, 11 bytes)
#   define CFG_TUD_HID_EP_BUFSIZE 16
#else
#   define CFG_TUD_ENABLED 0
#endif

#endif // _TUSB_CONFIG_H
