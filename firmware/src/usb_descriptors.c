/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb device descriptors for the native (usb-c) port on boards with
 * a composite of one cdc acm (debug console) and three hid
 * interfaces - a boot keyboard, a boot mouse and a gamepad. separate hid
 * interfaces rather than one with report ids, so that boot-protocol-only hosts
 * (bios setups, small machines such as the commodore 64 ultimate) find a plain
 * keyboard and mouse where they expect them.
 */

#include "config.h"

#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#include "usb_device.h"

//--------------------------------------------------------------------+
// device descriptor
//--------------------------------------------------------------------+

static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    // interface association descriptor (cdc spans two interfaces)
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_DEVICE_VID,
    .idProduct          = USB_DEVICE_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// hid report descriptors, one per hid instance
//--------------------------------------------------------------------+

static uint8_t const desc_hid_keyboard[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
static uint8_t const desc_hid_mouse[] = { TUD_HID_REPORT_DESC_MOUSE() };
static uint8_t const desc_hid_gamepad[] = { TUD_HID_REPORT_DESC_GAMEPAD() };
// absolute pointer for network control (kvmd_api.c): x / y 0..32767
static uint8_t const desc_hid_abs[] = { TUD_HID_REPORT_DESC_ABSMOUSE() };

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    switch (instance) {
        case USB_DEVICE_HID_KEYBOARD:   return desc_hid_keyboard;
        case USB_DEVICE_HID_MOUSE:      return desc_hid_mouse;
        case USB_DEVICE_HID_ABS:        return desc_hid_abs;
        default:                        return desc_hid_gamepad;
    }
}

//--------------------------------------------------------------------+
// configuration descriptor
//--------------------------------------------------------------------+

enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_HID_KEYBOARD,
    ITF_NUM_HID_MOUSE,
    ITF_NUM_HID_GAMEPAD,
    ITF_NUM_HID_GAMEPAD2,
    ITF_NUM_HID_ABS,
    ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF     0x81
#define EPNUM_CDC_OUT       0x02
#define EPNUM_CDC_IN        0x82
#define EPNUM_HID_KEYBOARD  0x83
#define EPNUM_HID_MOUSE     0x84
#define EPNUM_HID_GAMEPAD   0x85
#define EPNUM_HID_GAMEPAD2  0x86
#define EPNUM_HID_ABS       0x87

// 1 ms polling for all of them; the hosts this talks to are not short of bandwidth
#define HID_POLL_MS         1

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + (5 * TUD_HID_DESC_LEN))

static uint8_t const desc_configuration[] = {
    // config number, interface count, string index, total length, attribute, power in ma.
    // the board is self-powered from the amiga or the usb-a side as often as not,
    // but may run from usb-c alone, so ask for what it can draw there
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 500),

    // interface number, string index, ep notification address and size, ep data address (out, in) and size
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

    // interface number, string index, protocol, report descriptor length, ep in address, size and polling interval
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_KEYBOARD, 5, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_keyboard),
        EPNUM_HID_KEYBOARD, CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_MOUSE, 6, HID_ITF_PROTOCOL_MOUSE, sizeof(desc_hid_mouse),
        EPNUM_HID_MOUSE, CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_GAMEPAD, 7, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_gamepad),
        EPNUM_HID_GAMEPAD, CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
    // player 2: a second, separate controller to the host
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_GAMEPAD2, 8, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_gamepad),
        EPNUM_HID_GAMEPAD2, CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_ABS, 9, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_abs),
        EPNUM_HID_ABS, CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
};

_Static_assert(sizeof(desc_configuration) == CONFIG_TOTAL_LEN, "configuration descriptor length");

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// string descriptors
//--------------------------------------------------------------------+

static char const *const string_desc_arr[] = {
    NULL,                       // 0: language (handled below)
    "Ninepin",                  // 1: manufacturer
    "Ninepin UX",               // 2: product
    NULL,                       // 3: serial, from the flash unique id
    "Ninepin debug console",    // 4: cdc
    "Ninepin keyboard",         // 5: hid keyboard
    "Ninepin mouse",            // 6: hid mouse
    "Ninepin gamepad 1",        // 7: hid gamepad, player 1
    "Ninepin gamepad 2",        // 8: hid gamepad, player 2
    "Ninepin pointer",          // 9: hid absolute pointer (network control)
};

#define USB_STRING_MAX_CHARS 32

static uint16_t desc_str[USB_STRING_MAX_CHARS + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    char const *str;
    size_t chr_count;

    (void)langid;

    if (index == 0) {
        desc_str[1] = 0x0409; // english (us)
        chr_count = 1;
    } else {
        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        if (index == 3) {
            pico_get_unique_board_id_string(serial, sizeof(serial));
            str = serial;
        } else {
            str = string_desc_arr[index];
        }

        chr_count = strlen(str);
        if (chr_count > USB_STRING_MAX_CHARS)
            chr_count = USB_STRING_MAX_CHARS;

        // ascii to utf-16
        for (size_t i = 0; i < chr_count; i++)
            desc_str[1 + i] = (uint16_t)str[i];
    }

    // first word: length (bytes, including itself) and descriptor type
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return desc_str;
}

