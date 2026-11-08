/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * human interface device handling.
 *
 * if this file looks kind of sketchy, i'm just at the beginning of my
 * understanding of the tinyusb stack. apologies whilst i get to grips with
 * the terminology, or mapping it to what i understand of it.
 */

#include "config.h"

// these reside within the tinyusb sdk and are not part of this project source
#ifndef HAS_USB_DEVICE
#include "bsp/board.h" // the tinyusb bsp is not linked on boards which run their own device role
#endif
#include "tusb.h"

// other includes
#include <stdint.h>
#include <string.h>

#include "input_bridge.h"
#include "platform/amiga/keyboard_serial_io.h"
#include "tusb_config.h"
#include "util/output.h"
#include "util/debug_cons.h"
#include "util/status_led.h"
#include "usb_hid.h"
#include "ds5.h"
#include "hid_gamepad.h"
#include "pad.h"

// maximum number of reports per hid device
#define MAX_REPORT 4

// textual representations of attached devices
const uint8_t hid_protocol_type[] = { AP_H_UNKNOWN, AP_H_KEYBOARD, AP_H_MOUSE };

// pads on usb (pad.h): which decoder a slot's reports go through
typedef enum {
    USB_PAD_NONE = 0,
    USB_PAD_GENERIC,                // hid_gamepad.c, from the report descriptor
    USB_PAD_PS3,                    // sixaxis / dualshock 3: fixed report, start command
    USB_PAD_DS5,                    // dualsense: ds5.c
} usb_pad_kind_t;

typedef struct
{
    bool mounted;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t report_count;
    tuh_hid_report_info_t report_info[MAX_REPORT];

    usb_pad_kind_t pad_kind;
    uint16_t vid, pid;
    hid_gamepad_t gamepad;
    ds5_t ds5;
    bool ps3_start_pending;         // the start command still has to go
    bool leds_pending;              // a dualsense's player leds still have to go
    int8_t player;
} usb_hid_slot_t;

typedef struct
{
    uint8_t dev_addr;
    uint8_t instance;
} usb_keyboard_led_ctx_t;

static usb_hid_slot_t hid_info[CFG_TUH_HID];

static int8_t usb_hid_find_slot(uint8_t dev_addr, uint8_t instance);
static int8_t usb_hid_allocate_slot(uint8_t dev_addr, uint8_t instance);
static void usb_hid_update_status_leds(void);
static void usb_hid_set_keyboard_leds(void *ctx, uint8_t led_report);
static void process_report(uint8_t slot, uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len);
static void handle_event_keyboard(uint8_t slot, uint8_t dev_addr, uint8_t instance,
    hid_keyboard_report_t const *report);
static void handle_event_mouse(uint8_t slot, hid_mouse_report_t const *report);

static void usb_pad_player(uint8_t slot, int8_t player)
{
    if (slot >= CFG_TUH_HID)
        return;
    hid_info[slot].player = player;
    hid_info[slot].leds_pending = hid_info[slot].pad_kind == USB_PAD_DS5;
}

void hid_app_task(void)
{
    // a ps3 pad waits for this feature report before it sends any input; the
    // payload has to outlive the asynchronous control transfer
    static uint8_t ps3_start[] = { 0x42, 0x0c, 0x00, 0x00 };
    static uint8_t ds5_output[DS5_USB_OUTPUT_LEN];

    for (uint8_t slot = 0; slot < CFG_TUH_HID; slot++) {
        usb_hid_slot_t *hid = &hid_info[slot];

        if (!hid->mounted)
            continue;

        if (hid->ps3_start_pending
            && tuh_hid_set_report(hid->dev_addr, hid->instance, 0xf4, HID_REPORT_TYPE_FEATURE,
                ps3_start, sizeof(ps3_start)))
            hid->ps3_start_pending = false;

        if (hid->leds_pending) {
            size_t length = ds5_output_usb(ds5_output, sizeof(ds5_output), hid->player);

            // the report id goes separately; the rest after it
            if (length && tuh_hid_send_report(hid->dev_addr, hid->instance, ds5_output[0],
                    ds5_output + 1, (uint16_t)(length - 1)))
                hid->leds_pending = false;
        }
    }
}

/**
 * a non-boot hid interface: is it a pad, and which decoder does it need?
 */
static void usb_pad_mount(uint8_t slot, uint8_t dev_addr, uint8_t const *desc_report, uint16_t desc_len)
{
    // playstation pads number cross 2, circle 3, square 1, triangle 4
    static uint8_t const playstation[PAD_LOGICAL_BUTTONS] = { 2, 3, 1, 4, 5, 6, 10 };
    usb_hid_slot_t *hid = &hid_info[slot];
    char const *name = "usb pad";

    hid->pad_kind = USB_PAD_NONE;
    hid->player = -1;
    if (!tuh_vid_pid_get(dev_addr, &hid->vid, &hid->pid))
        hid->vid = hid->pid = 0;

    if (ds5_matches(hid->vid, hid->pid)) {
        memset(&hid->ds5, 0, sizeof(hid->ds5));
        hid->pad_kind = USB_PAD_DS5;
        name = "DualSense";
    } else if (hid_gamepad_parse(&hid->gamepad, desc_report, desc_len)) {
        hid->pad_kind = USB_PAD_GENERIC;
        if (hid->vid == 0x054c) {
            memcpy(hid->gamepad.defaults, playstation, sizeof(playstation));
            name = "PlayStation pad";
        }
        // a ps3 pad (and controllers posing as one) by its exact report layout
        if ((hid->vid == 0x054c) && (hid->pid == 0x0268) && hid->gamepad.report_ids
            && (hid->gamepad.report_count == 1) && (hid->gamepad.reports[0].id == 1)
            && (hid->gamepad.reports[0].bits == 48 * 8)) {
            hid->pad_kind = USB_PAD_PS3;
            hid->ps3_start_pending = true;
            name = "PS3 pad";
        }
    }

    if (hid->pad_kind == USB_PAD_NONE)
        return;

    dbgcons_plug(AP_H_CONTROLLER);
    pads_connected(slot, ((uint32_t)hid->vid << 16) | hid->pid, name, usb_pad_player);
}

/**
 * a ps3 report (id 1): d-pad and buttons in fixed bits, sticks after them.
 * the own button numbers follow the pad's hid descriptor: select 1, l3 2,
 * r3 3, start 4, d-pad 5-8, l2 9, r2 10, l1 11, r1 12, triangle 13, circle 14,
 * cross 15, square 16.
 */
static bool usb_pad_ps3(uint8_t const *report, uint16_t len, pad_input_t *input)
{
    static uint8_t const defaults[PAD_LOGICAL_BUTTONS] = { 15, 14, 16, 13, 11, 12, 4 };

    if ((len < 49) || (report[0] != 1))
        return false;

    memset(input, 0, sizeof(*input));
    input->buttons = (uint16_t)(report[2] | (report[3] << 8));
    input->home = (report[4] & 0x01) != 0;
    if (report[2] & 0x10) input->dpad |= PAD_UP;
    if (report[2] & 0x20) input->dpad |= PAD_RIGHT;
    if (report[2] & 0x40) input->dpad |= PAD_DOWN;
    if (report[2] & 0x80) input->dpad |= PAD_LEFT;
    input->hat = PAD_HAT_CENTRED;
    input->lx = (int8_t)((int)report[6] - 128 < -127 ? -127 : (int)report[6] - 128);
    input->ly = (int8_t)((int)report[7] - 128 < -127 ? -127 : (int)report[7] - 128);
    input->rx = (int8_t)((int)report[8] - 128 < -127 ? -127 : (int)report[8] - 128);
    input->ry = (int8_t)((int)report[9] - 128 < -127 ? -127 : (int)report[9] - 128);
    memcpy(input->defaults, defaults, sizeof(defaults));
    return true;
}

/**
 * @return true if the slot is a pad and the report went to it
 */
static bool usb_pad_report(uint8_t slot, uint8_t const *report, uint16_t len)
{
    usb_hid_slot_t *hid = &hid_info[slot];
    pad_input_t input;
    bool ok = false;

    switch (hid->pad_kind) {
        case USB_PAD_DS5:
            ok = ds5_decode_usb(&hid->ds5, report, len, &input);
            break;
        case USB_PAD_PS3:
            ok = usb_pad_ps3(report, len, &input);
            break;
        case USB_PAD_GENERIC:
            ok = hid_gamepad_decode(&hid->gamepad, report, len, &input);
            // a playstation pad's ps button is its 13th
            if (ok && (hid->vid == 0x054c))
                input.home = (input.buttons & (1u << 12)) != 0;
            break;
        default:
            return false;
    }

    if (ok)
        pads_report(slot, &input);
    return true;
}

void usb_hid_sync_keyboard_leds(void)
{
    uint8_t led_report = amiga_caps_lock() ? KEYBOARD_LED_CAPSLOCK : 0;

    for (uint8_t slot = 0; slot < CFG_TUH_HID; slot++) {
        if (!hid_info[slot].mounted)
            continue;

        if (tuh_hid_interface_protocol(hid_info[slot].dev_addr, hid_info[slot].instance) !=
            HID_ITF_PROTOCOL_KEYBOARD) {
            continue;
        }

        tuh_hid_set_report(hid_info[slot].dev_addr, hid_info[slot].instance, 0,
            HID_REPORT_TYPE_OUTPUT, &led_report, 1);
    }
}

static int8_t usb_hid_find_slot(uint8_t dev_addr, uint8_t instance)
{
    for (uint8_t slot = 0; slot < CFG_TUH_HID; slot++) {
        if (hid_info[slot].mounted && (hid_info[slot].dev_addr == dev_addr) && (hid_info[slot].instance == instance))
            return (int8_t)slot;
    }

    return -1;
}

static int8_t usb_hid_allocate_slot(uint8_t dev_addr, uint8_t instance)
{
    int8_t slot = usb_hid_find_slot(dev_addr, instance);

    if (slot >= 0)
        return slot;

    for (uint8_t i = 0; i < CFG_TUH_HID; i++) {
        if (!hid_info[i].mounted) {
            hid_info[i].mounted = true;
            hid_info[i].dev_addr = dev_addr;
            hid_info[i].instance = instance;
            hid_info[i].report_count = 0;
            input_bridge_reset(i);
            return (int8_t)i;
        }
    }

    return -1;
}

/**
 * Tell the status indicators whether usb still has anything mounted.
 */
static void usb_hid_update_status_leds(void)
{
    bool present = false;

    for (uint8_t slot = 0; slot < CFG_TUH_HID; slot++) {
        if (hid_info[slot].mounted) {
            present = true;
            break;
        }
    }

    status_led_source(STATUS_LED_SOURCE_USB, present);
}

static void usb_hid_set_keyboard_leds(void *ctx, uint8_t led_report)
{
    usb_keyboard_led_ctx_t *led_ctx = (usb_keyboard_led_ctx_t *)ctx;

    tuh_hid_set_report(led_ctx->dev_addr, led_ctx->instance, 0, HID_REPORT_TYPE_OUTPUT, &led_report, 1);
}

// callback functions; methods below suffixed with "_cb" are called by tinyusb
// when processing certain hid events.

/**
 * HID connection callback
 *
 * @param dev_addr    Address of connected device
 * @param instance    Instance of connected device
 * @param desc_report Report descriptor
 * @param desc_len    Length of descriptor
 */
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    uint8_t hid_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    int8_t slot = usb_hid_allocate_slot(dev_addr, instance);
    bool receive_ok;

    dbgcons_plug(hid_protocol_type[hid_protocol]);
    usb_hid_update_status_leds();

    if (slot < 0)
        return;

    // this part doesn't entirely make sense to me; hid devices come in two modes, boot protocol and report;
    // as i understand it, boot proto is intended for simplistic software such as bios which don't want to
    // implement a full stack. so if we're not in boot proto mode, display... something?
    // this might be number of interfaces on a device (think wireless kbd+mouse receiver). maybe. speculation.
    if (hid_protocol == HID_ITF_PROTOCOL_NONE) {
        hid_info[slot].report_count = tuh_hid_parse_report_descriptor(hid_info[slot].report_info, MAX_REPORT, desc_report, desc_len);
        // ahprintf("[PLUG] %02x report(s)\n", hid_info[instance].report_count);
        usb_pad_mount((uint8_t)slot, dev_addr, desc_report, desc_len);
    } else {
        hid_info[slot].pad_kind = USB_PAD_NONE;
    }

    receive_ok = tuh_hid_receive_report(dev_addr, instance);
    dbgcons_hid_status(dev_addr, instance, hid_protocol, receive_ok, hid_info[slot].report_count, true);

    if (!receive_ok) {
        // ahprintf("[PLUG] warning! report request failed; delayed initialisation?\n");
    }
}

/**
 * HID disconnection callback
 *
 * @param dev_addr    Address of disconnected device
 * @param instance    Instance of disconnected device
 */
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    uint8_t hid_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    int8_t slot = usb_hid_find_slot(dev_addr, instance);

    dbgcons_unplug(hid_protocol_type[hid_protocol]);
    dbgcons_hid_status(dev_addr, instance, hid_protocol, true, slot >= 0 ? hid_info[slot].report_count : 0, false);

    if (slot >= 0) {
        if (hid_info[slot].pad_kind != USB_PAD_NONE) {
            pads_disconnected((uint8_t)slot);
            dbgcons_unplug(AP_H_CONTROLLER);
        }
        hid_info[slot].pad_kind = USB_PAD_NONE;
        hid_info[slot].ps3_start_pending = false;
        hid_info[slot].leds_pending = false;
        input_bridge_disconnect((uint8_t)slot);
        hid_info[slot].mounted = false;
        hid_info[slot].report_count = 0;
    }

    usb_hid_update_status_leds();
}

/**
 * HID report event has occurred
 *
 * @param dev_addr    Address of device sending report
 * @param instance    Instance of device sending report
 * @param report      Address of report structure
 * @param len         Length of report structure
 */
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    uint8_t const hid_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    int8_t slot = usb_hid_find_slot(dev_addr, instance);

    if (slot < 0) {
        tuh_hid_receive_report(dev_addr, instance);
        return;
    }

    switch (hid_protocol) {
        case HID_ITF_PROTOCOL_KEYBOARD:
            handle_event_keyboard((uint8_t)slot, dev_addr, instance, (hid_keyboard_report_t const *)report);
            break;

        case HID_ITF_PROTOCOL_MOUSE:
            handle_event_mouse((uint8_t)slot, (hid_mouse_report_t const *)report);
            break;

        default:
            // pads first; anything else: read the usage page, as some reports have a classifier as
            // "desktop" for media keys, power, or are just encapsulated.
            if (!usb_pad_report((uint8_t)slot, report, len))
                process_report((uint8_t)slot, dev_addr, instance, report, len);
            break;
    }

    // continue to request to receive report
    tuh_hid_receive_report(dev_addr, instance);
    // if (!tuh_hid_receive_report(dev_addr, instance))
        // ahprintf("[ERROR] unable to receive hid event report\n");
}

/**
 * HID Boot Protocol keeps a six-key buffer of pressed keys. Return true if keycode is "pressed".
 *
 * @param report    Address of hid_keyboard_report_t struct
 * @param keycode   Keycode to look for
 * @return true     Key is currently pressed
 * @return false    Key is not currently pressed
 */
/**
 * Process incoming event and pass off to device-centric handler.
 *
 * @param slot      Source slot for this reporting device
 * @param dev_addr  Address of reporting device
 * @param instance  Instance of reporting device
 * @param report    Address of the report data structure
 * @param len       Size of the report event
 */
static void process_report(uint8_t slot, uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    uint8_t const report_count = hid_info[slot].report_count;
    tuh_hid_report_info_t *report_info_arr = hid_info[slot].report_info;
    tuh_hid_report_info_t *report_info = NULL;

    if (report_count == 1 && report_info_arr[0].report_id == 0) {
        // single report with id of 0 is a single-shot report
        report_info = &report_info_arr[0];
    } else {
        // (possibly, but not mandatory) array of reports
        // @todo i don't understand why, if we're facing an array of reports, why we're only going to process the
        // report containing the identifier at byte 0, but maybe one day that will make sense.
        uint8_t const report_id = report[0];

        // locate the array member matching the report ID we are intending to process (then exit the loop)
        for (uint8_t i = 0; i < report_count; i++) {
            if (report_id == report_info_arr[i].report_id) {
                report_info = &report_info_arr[i];
                break;
            }
        }

        report++;
        len--;
    }

    if (!report_info) {
        // ahprintf("[ERROR] report_info pointer was not set during process_report(), value: $%x, report_count was %d\n", report_info, report_count);
        return;
    }

    // process report event based on usage_page class and usage device
    if (report_info->usage_page == HID_USAGE_PAGE_DESKTOP) {
        switch (report_info->usage) {
            case HID_USAGE_DESKTOP_KEYBOARD:
                // keyboard event; let's hope it appears as a boot proto event or else this will break
                handle_event_keyboard(slot, dev_addr, instance, (hid_keyboard_report_t const *)report);
                break;

            case HID_USAGE_DESKTOP_MOUSE:
                // mouse event
                handle_event_mouse(slot, (hid_mouse_report_t const *)report);
                break;

            default:
                break;
        }
    }
}

/**
 * Handle the mouse event sent to us.
 *
 * @param dev_addr  Device address of report
 * @param instance  Instance number of reporting device
 * @param report    Address of hid_mouse_report_t structure of current mouse event
 */
static void handle_event_mouse(uint8_t slot, hid_mouse_report_t const *report)
{
    input_bridge_handle_mouse(slot, report);
}

/**
 * Handle the keyboard event sent to us.
 *
 * @param slot      Source slot for this reporting device
 * @param dev_addr  Device address of report
 * @param instance  Instance number of reporting device
 * @param report    Address of hid_keyboard_report_t structure of current keyboard event (boot proto?)
 */
static void handle_event_keyboard(uint8_t slot, uint8_t dev_addr, uint8_t instance,
    hid_keyboard_report_t const *report)
{
    usb_keyboard_led_ctx_t led_ctx = { dev_addr, instance };
    input_bridge_keyboard_sink_t sink = { usb_hid_set_keyboard_leds, &led_ctx };

    input_bridge_handle_keyboard(slot, report, &sink);
}
