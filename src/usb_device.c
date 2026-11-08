/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * usb device role on the native phy: cdc debug console and hid passthrough.
 */

#include "config.h"

#ifdef HAS_USB_DEVICE

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pico/critical_section.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "tusb.h"

#include "input_bridge_bt.h"
#include "usb_device.h"
#include "util/output.h"

//--------------------------------------------------------------------+
// cdc console: an sdk stdio driver writing into a ring buffer which the main
// loop drains into the cdc fifo. the driver may be entered from interrupt
// context (bluetooth status printing), where tinyusb must not be called, hence
// the buffer in between.
//--------------------------------------------------------------------+

// enough for the boot banner and a few screens of hotplug chatter
#define CONSOLE_BUFSIZE 4096

static char console_buf[CONSOLE_BUFSIZE];
static uint32_t console_head, console_tail; // head: next write, tail: next read
static critical_section_t console_lock;

static void console_out_chars(char const *buf, int len)
{
    critical_section_enter_blocking(&console_lock);

    for (int i = 0; i < len; i++) {
        uint32_t next = (console_head + 1) % CONSOLE_BUFSIZE;

        // full: drop the rest of this write rather than tear older lines
        if (next == console_tail)
            break;

        console_buf[console_head] = buf[i];
        console_head = next;
    }

    critical_section_exit(&console_lock);
}

static stdio_driver_t console_driver = {
    .out_chars = console_out_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

/**
 * move whatever fits from the ring buffer into the cdc fifo; only whilst a
 * terminal is listening (dtr), so boot output waits for somebody to read it.
 */
static void console_drain(void)
{
    uint8_t chunk[64];
    uint32_t avail, count = 0;

    // nothing is read from the console; keep the rx fifo from backing up
    if (tud_cdc_available())
        tud_cdc_read_flush();

    if (!tud_cdc_connected())
        return;

    avail = tud_cdc_write_available();
    if (avail > sizeof(chunk))
        avail = sizeof(chunk);

    critical_section_enter_blocking(&console_lock);
    while ((count < avail) && (console_tail != console_head)) {
        chunk[count++] = (uint8_t)console_buf[console_tail];
        console_tail = (console_tail + 1) % CONSOLE_BUFSIZE;
    }
    critical_section_exit(&console_lock);

    if (count) {
        tud_cdc_write(chunk, count);
        tud_cdc_write_flush();
    }
}

//--------------------------------------------------------------------+
// hid passthrough state. the endpoint may still be busy with the previous
// report when a new one arrives, so the latest state is kept here and sent from
// the task when the endpoint is free: keys and buttons are levels (the newest
// report supersedes), mouse motion is accumulated so none is lost.
//--------------------------------------------------------------------+

static hid_keyboard_report_t slot_keyboard[INPUT_BRIDGE_MAX_SLOTS];
static uint8_t slot_buttons[INPUT_BRIDGE_MAX_SLOTS];
static bool keyboard_dirty;
static bool mouse_buttons_dirty;
static int32_t mouse_dx, mouse_dy, mouse_wheel, mouse_pan;

static bool stack_up;
static bool connected;

// the host's caps lock, from the led output report it sends the keyboard
// interface: -1 until the host has said, else 0 / 1
static int8_t host_caps_lock = -1;

// a caps lock tap in flight: the press goes out in one report and the release in
// the next, since the slot state above keeps only the newest level and would
// fold a press and its release into nothing
typedef enum { CAPS_TAP_NONE = 0, CAPS_TAP_PRESS, CAPS_TAP_RELEASE } caps_tap_t;
static caps_tap_t caps_tap;

// hid usage table, keyboard page: "keyboard errorrollover"
#define HID_USAGE_KEYBOARD_ERROR_ROLLOVER 0x01

static void hid_wake_host(void)
{
    // a suspended host which allowed remote wakeup gets woken by input
    if (tud_suspended())
        tud_remote_wakeup();
}

static void hid_merge_keyboard(hid_keyboard_report_t *out)
{
    uint8_t keys = 0;

    memset(out, 0, sizeof(*out));

    for (uint8_t slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++) {
        out->modifier |= slot_keyboard[slot].modifier;

        for (uint8_t pos = 0; pos < 6; pos++) {
            uint8_t code = slot_keyboard[slot].keycode[pos];
            bool dup = false;

            if (code == 0)
                continue;

            for (uint8_t k = 0; k < keys; k++)
                if (out->keycode[k] == code)
                    dup = true;

            if (dup)
                continue;

            // more than six keys down across all keyboards: boot protocol's
            // answer is phantom state (all keys = errorrollover)
            if (keys == 6) {
                memset(out->keycode, HID_USAGE_KEYBOARD_ERROR_ROLLOVER, sizeof(out->keycode));
                return;
            }

            out->keycode[keys++] = code;
        }
    }
}

static uint8_t hid_merge_buttons(void)
{
    uint8_t buttons = 0;

    for (uint8_t slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++)
        buttons |= slot_buttons[slot];

    return buttons;
}

static int8_t hid_take_axis(int32_t *accum)
{
    int32_t step = *accum;

    if (step > 127)
        step = 127;
    if (step < -127)
        step = -127;

    *accum -= step;
    return (int8_t)step;
}

static void hid_flush(void)
{
    if (!tud_mounted()) {
        // nobody to send to: levels stay (and go out on mount), motion is stale,
        // and a tap or the host's led state means nothing to the next host
        mouse_dx = mouse_dy = mouse_wheel = mouse_pan = 0;
        caps_tap = CAPS_TAP_NONE;
        host_caps_lock = -1;
        return;
    }

    if ((caps_tap != CAPS_TAP_NONE) && tud_hid_n_ready(USB_DEVICE_HID_KEYBOARD)) {
        hid_keyboard_report_t merged;

        hid_merge_keyboard(&merged);

        if (caps_tap == CAPS_TAP_PRESS) {
            // into the first free position; with six keys already down the
            // last one gives way for this one report
            uint8_t pos = 0;

            while ((pos < 5) && (merged.keycode[pos] != 0))
                pos++;
            merged.keycode[pos] = HID_KEY_CAPS_LOCK;

            if (tud_hid_n_keyboard_report(USB_DEVICE_HID_KEYBOARD, 0, merged.modifier, merged.keycode))
                caps_tap = CAPS_TAP_RELEASE;
        } else if (tud_hid_n_keyboard_report(USB_DEVICE_HID_KEYBOARD, 0, merged.modifier, merged.keycode)) {
            // the release report carries the current state, so it also
            // delivers anything that changed whilst the tap was in flight
            caps_tap = CAPS_TAP_NONE;
            keyboard_dirty = false;
        }
    }

    if (keyboard_dirty && (caps_tap == CAPS_TAP_NONE) && tud_hid_n_ready(USB_DEVICE_HID_KEYBOARD)) {
        hid_keyboard_report_t merged;

        hid_merge_keyboard(&merged);
        if (tud_hid_n_keyboard_report(USB_DEVICE_HID_KEYBOARD, 0, merged.modifier, merged.keycode))
            keyboard_dirty = false;
    }

    if ((mouse_buttons_dirty || mouse_dx || mouse_dy || mouse_wheel || mouse_pan)
        && tud_hid_n_ready(USB_DEVICE_HID_MOUSE)) {
        int32_t dx = mouse_dx, dy = mouse_dy, wheel = mouse_wheel, pan = mouse_pan;
        int8_t x = hid_take_axis(&dx), y = hid_take_axis(&dy);
        int8_t v = hid_take_axis(&wheel), h = hid_take_axis(&pan);

        // only consume the motion once the report is actually queued
        if (tud_hid_n_mouse_report(USB_DEVICE_HID_MOUSE, 0, hid_merge_buttons(), x, y, v, h)) {
            mouse_dx = dx;
            mouse_dy = dy;
            mouse_wheel = wheel;
            mouse_pan = pan;
            mouse_buttons_dirty = false;
        }
    }
}

//--------------------------------------------------------------------+
// public api
//--------------------------------------------------------------------+

void usb_device_init(void)
{
    critical_section_init(&console_lock);
    stdio_set_driver_enabled(&console_driver, true);
}

void usb_device_task(bool vbus_present)
{
    if (!stack_up) {
        // dcd_init() presents the pull-up at once, so the stack is not even
        // initialised until there is a powered port to present it to
        if (!vbus_present)
            return;

        tusb_rhport_init_t const dev_init = {
            .role = TUSB_ROLE_DEVICE,
            .speed = TUSB_SPEED_AUTO,
        };

        if (!tusb_init(BOARD_TUD_RHPORT, &dev_init)) {
            ahprintf("[usbd] device stack failed to start\n");
            return;
        }

        stack_up = true;
        connected = true;
        ahprintf("[usbd] usb-c vbus present, device stack up\n");
    } else if (vbus_present && !connected) {
        tud_connect();
        connected = true;
        ahprintf("[usbd] usb-c vbus present, connecting\n");
    } else if (!vbus_present && connected) {
        tud_disconnect();
        connected = false;
        ahprintf("[usbd] usb-c vbus gone, disconnected\n");
    }

    tud_task();

    if (!connected)
        return;

    console_drain();
    hid_flush();
}

#ifdef USB_DEVICE_BRINGUP_SERVICE
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/watchdog.h"
#include "pico/time.h"

// above the default 0x80, so the console is served even from under a stuck
// default-priority handler
#define BRINGUP_IRQ_PRIORITY 0x40

static alarm_pool_t *bringup_pool;
static repeating_timer_t bringup_timer;

static volatile uint32_t bringup_ticks;

static bool bringup_service_cb(repeating_timer_t *rt)
{
    (void)rt;
    bringup_ticks++;
    watchdog_update();
    usb_device_task(true);
    return true;
}

uint32_t usb_device_bringup_ticks(void)
{
    return bringup_ticks;
}

void usb_device_bringup_service_start(void)
{
    // stack up from thread context first
    usb_device_task(true);
    irq_set_priority(USBCTRL_IRQ, BRINGUP_IRQ_PRIORITY);

    bringup_pool = alarm_pool_create_with_unused_hardware_alarm(2);
    irq_set_priority(hardware_alarm_get_irq_num(alarm_pool_hardware_alarm_num(bringup_pool)),
        BRINGUP_IRQ_PRIORITY);
    alarm_pool_add_repeating_timer_ms(bringup_pool, 1, bringup_service_cb, NULL, &bringup_timer);

    ahprintf("[usbd] bring-up service: console served from a timer irq\n");
}
#endif

bool usb_device_mounted(void)
{
    return stack_up && tud_mounted();
}

void usb_device_keyboard(uint8_t slot, hid_keyboard_report_t const *report)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    slot_keyboard[slot] = *report;
    keyboard_dirty = true;
    hid_wake_host();
}

void usb_device_mouse(uint8_t slot, hid_mouse_report_t const *report)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    if (slot_buttons[slot] != report->buttons) {
        slot_buttons[slot] = report->buttons;
        mouse_buttons_dirty = true;
    }

    mouse_dx += report->x;
    mouse_dy += report->y;
    mouse_wheel += report->wheel;
    mouse_pan += report->pan;

    hid_wake_host();
}

void usb_device_gamepad(hid_gamepad_report_t const *report)
{
    if (!usb_device_mounted() || !tud_hid_n_ready(USB_DEVICE_HID_GAMEPAD))
        return;

    tud_hid_n_report(USB_DEVICE_HID_GAMEPAD, 0, report, sizeof(*report));
}

int usb_device_host_caps_lock(void)
{
    return host_caps_lock;
}

void usb_device_caps_tap(void)
{
    if (caps_tap != CAPS_TAP_NONE)
        return;

    caps_tap = CAPS_TAP_PRESS;
    hid_wake_host();
}

bool usb_device_caps_tap_pending(void)
{
    return caps_tap != CAPS_TAP_NONE;
}

void usb_device_release_slot(uint8_t slot)
{
    if (slot >= INPUT_BRIDGE_MAX_SLOTS)
        return;

    if (memcmp(&slot_keyboard[slot], &(hid_keyboard_report_t){ 0 }, sizeof(slot_keyboard[slot])) != 0) {
        memset(&slot_keyboard[slot], 0, sizeof(slot_keyboard[slot]));
        keyboard_dirty = true;
    }

    if (slot_buttons[slot]) {
        slot_buttons[slot] = 0;
        mouse_buttons_dirty = true;
    }
}

void usb_device_release_all(void)
{
    for (uint8_t slot = 0; slot < INPUT_BRIDGE_MAX_SLOTS; slot++)
        usb_device_release_slot(slot);

    mouse_dx = mouse_dy = mouse_wheel = mouse_pan = 0;
}

//--------------------------------------------------------------------+
// tinyusb hid device callbacks
//--------------------------------------------------------------------+

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
    uint8_t *buffer, uint16_t reqlen)
{
    // nothing to poll; a stall tells the host to use the interrupt endpoint
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
    uint8_t const *buffer, uint16_t bufsize)
{
    // the host's keyboard leds arrive here. the usb-a keyboards follow the
    // amiga's caps lock, not this host's, so only the caps state is kept: a
    // real amiga keyboard forwarded to this host syncs its latched caps against
    // it (keyboard_recv.c)
    (void)report_id;

    if ((instance == USB_DEVICE_HID_KEYBOARD) && (report_type == HID_REPORT_TYPE_OUTPUT) && (bufsize >= 1))
        host_caps_lock = (buffer[0] & KEYBOARD_LED_CAPSLOCK) ? 1 : 0;
}

#endif // HAS_USB_DEVICE
