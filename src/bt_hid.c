/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * bluetooth hid host integration for pico w builds.
 *
 * the radio following the settings live, the pairing policy (automatic, or
 * paired devices only with a two-minute pair window), forgetting all bonds,
 * classic pad discovery by inquiry, classic and le pads through the generic
 * hid parser and the dualshock 4 decoder, and the per-slot coalescing event
 * queue are ported from stefan reinauer's bt_hid.c (reinauer/amigahid-pico,
 * features branch, EPL-2.0). pads go to this tree's player manager (pad.h)
 * instead of his input bridge; the dualsense over bluetooth is this tree's.
 *
 * everything below except the public functions at the end runs in the
 * btstack context: the cyw43 background interrupt (the threadsafe-background
 * async context of pico_cyw43_arch_none, or of its lwip variant on rev7, where
 * wi-fi shares it). input is posted to a queue and handed on from
 * bt_hid_task() in the main loop; the main loop only calls into btstack holding
 * the async context lock.
 */

#include "bt_hid.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "btstack_config.h"
#include "btstack.h"
#include "ble/le_device_db.h"
#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"

#include "bt_ds4.h"
#include "ds5.h"
#include "hid_gamepad.h"
#include "input_bridge_bt.h"
#include "pad.h"
#include "radio.h"
#include "settings.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/status_led.h"

#define BT_HID_QUEUE_DEPTH 64
// shared by both classic connections; a ds4's or dualsense's descriptor alone
// is several hundred bytes
#define BT_CLASSIC_DESCRIPTOR_STORAGE_SIZE 2048
#define BT_LE_DESCRIPTOR_STORAGE_SIZE 1024
#define BT_HID_LOCAL_NAME "AmigaHID Pico"

// the input slots bluetooth owns; the real keyboard slot after them is not ours
#define BT_HID_SLOTS (INPUT_BRIDGE_BT_SLOT_END - INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE)

#define BT_PAIR_WINDOW_MS       120000u
#define BT_PAD_SETUP_TIMEOUT_MS 1000    // a feature report request unanswered this long: carry on

#define HID_USAGE_DESKTOP_X     0x30
#define HID_USAGE_DESKTOP_Y     0x31
#define HID_USAGE_DESKTOP_WHEEL 0x38

typedef enum
{
    BT_HID_QUEUE_DISCONNECT = 0,
    BT_HID_QUEUE_KEYBOARD,
    BT_HID_QUEUE_MOUSE,
    BT_HID_QUEUE_PAD_CONNECT,
    BT_HID_QUEUE_PAD,
} bt_hid_queue_type_t;

typedef enum
{
    BT_PAD_GENERIC = 0,             // hid_gamepad.c, from the report descriptor
    BT_PAD_DS4,                     // bt_ds4.c
    BT_PAD_DS5,                     // ds5.c
} bt_pad_kind_t;

static char const *const bt_pad_names[] = { "BT pad", "DS4", "DualSense" };

typedef struct
{
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} bt_hid_keyboard_report_t;

typedef struct
{
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
    int8_t pan;
} bt_hid_mouse_report_t;

typedef struct
{
    uint32_t identity;
    uint8_t kind;                   // bt_pad_kind_t
} bt_hid_pad_connect_t;

typedef struct
{
    uint8_t slot;
    bt_hid_queue_type_t type;
    union
    {
        bt_hid_keyboard_report_t keyboard;
        bt_hid_mouse_report_t mouse;
        bt_hid_pad_connect_t connect;
        pad_input_t pad;
    };
} bt_hid_queue_entry_t;

/**
 * a slot's events whilst the queue is full: the latest state of each kind,
 * and every disconnect and connect, so the main loop still ends up right.
 */
typedef struct
{
    bool disconnect;
    bool connect_pending;
    bool keyboard_pending;
    bool mouse_pending;
    bool pad_pending;
    bt_hid_pad_connect_t connect;
    bt_hid_keyboard_report_t keyboard;
    bt_hid_mouse_report_t mouse;
    pad_input_t pad;
} bt_hid_overflow_t;

typedef struct
{
    bool in_use;
    uint16_t hid_cid;
    hid_protocol_mode_t protocol_mode;
    bd_addr_t address;
    bool descriptor_ready;
    bool gamepad_device;            // a controller (decoded or not): no keyboard / mouse decoding
    bool has_gamepad;               // a pad handed to the player manager
    bt_pad_kind_t pad_kind;
    bool sony_layout;               // a generic pad laid out like the playstation pads
    hid_gamepad_t gamepad;
    bt_ds4_t ds4;
    ds5_t ds5;

    // ds4 / dualsense: a feature report request switches them to full reports
    btstack_timer_source_t setup_timer;
    uint8_t setup_attempts;
    bool setup_requested;
    bool setup_done;                // answered, or given up on

    // dualsense: the player leds, sent from the main loop once setup is done
    volatile bool leds_pending;
    int8_t player;
    uint8_t output_sequence;
    uint8_t output[DS5_BT_OUTPUT_LEN];
} bt_classic_connection_t;

typedef enum
{
    BT_CLASSIC_IDLE,
    BT_CLASSIC_SCANNING,
    BT_CLASSIC_CONNECTING,
} bt_classic_state_t;

typedef enum
{
    BT_LE_STATE_OFF = 0,
    BT_LE_STATE_SCANNING,
    BT_LE_STATE_CONNECTING,
    BT_LE_STATE_ENCRYPTING,
    BT_LE_STATE_SERVICE_QUERY,
    BT_LE_STATE_CHARACTERISTIC_QUERY,
    BT_LE_STATE_ENABLE_KEYBOARD,
    BT_LE_STATE_ENABLE_MOUSE,
    BT_LE_STATE_ENABLE_GAMEPAD,
    BT_LE_STATE_READY,
    BT_LE_STATE_DISCONNECTING,
} bt_le_state_t;

static queue_t bt_hid_queue;
static bool bt_hid_queue_ready;
static bool bt_hid_ready;
static bool bt_hid_init_failed;

// the radio as the settings want it (main loop), and the controller as
// btstack last reported it
static volatile bool bt_radio_on;
static volatile bool bt_controller_off = true;

// main-loop requests, carried out by bt_hid_settings_task()
static volatile bool bt_pair_request, bt_forget_request;
static bool bt_forgetting;
static volatile bool bt_pair_window;
static uint32_t bt_pair_deadline;

static volatile int8_t bt_discoverable = -1;    // what gap was last told; -1 unknown

static bool bt_resolving;
static bd_addr_t bt_resolve_addr;
static uint8_t bt_resolve_type;

static bool bt_hid_queue_resync;
static bt_hid_overflow_t bt_hid_overflow[BT_HID_SLOTS];

static bt_classic_connection_t bt_classic_connections[INPUT_BRIDGE_BT_CLASSIC_SLOTS];
static uint8_t bt_classic_descriptor_storage[BT_CLASSIC_DESCRIPTOR_STORAGE_SIZE];
// btstack dropped HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT (pico-sdk 2.3.1 pin);
// asking for REPORT now runs the SDP/descriptor path and the device's actual mode comes
// back in the SET_PROTOCOL response, which bt_classic_connections[].protocol_mode tracks.
static const hid_protocol_mode_t bt_classic_report_mode = HID_PROTOCOL_MODE_REPORT;
static bt_classic_state_t bt_classic_state;
static volatile bool bt_classic_working;
static bool bt_classic_candidate;
static bd_addr_t bt_classic_candidate_addr;
static uint16_t bt_classic_outgoing_cid;
static btstack_timer_source_t bt_classic_scan_timer;

static btstack_packet_callback_registration_t bt_classic_hci_event_callback;
static btstack_packet_callback_registration_t bt_le_hci_event_callback;
static btstack_packet_callback_registration_t bt_le_sm_event_callback;

static bt_le_state_t bt_le_state = BT_LE_STATE_OFF;

// searching for new devices paused (bt_hid_pause_scanning()): classic inquiry
// and le scanning hold the shared radio most of the time, and wi-fi loses
// frames meanwhile - a firmware upload stalls, and the access point never
// answers a client's association request. links already up stay; a pairing
// window (BT pair) searches regardless
static volatile uint8_t bt_scan_pause_reasons;   // BT_HID_PAUSE_* bits
static bool bt_scan_paused;

static bool bt_scan_pause_effective(void);
static bd_addr_t bt_le_addr;
static bd_addr_type_t bt_le_addr_type;
static hci_con_handle_t bt_le_connection_handle = HCI_CON_HANDLE_INVALID;
static gatt_client_service_t bt_le_hid_service;
static gatt_client_characteristic_t bt_le_protocol_mode_characteristic;
static gatt_client_characteristic_t bt_le_boot_keyboard_characteristic;
static gatt_client_characteristic_t bt_le_boot_mouse_characteristic;
static gatt_client_notification_t bt_le_keyboard_notifications;
static gatt_client_notification_t bt_le_mouse_notifications;
static bool bt_le_has_protocol_mode;
static bool bt_le_has_boot_keyboard;
static bool bt_le_has_boot_mouse;
static uint16_t bt_le_hids_cid;
static uint8_t bt_le_descriptor_storage[BT_LE_DESCRIPTOR_STORAGE_SIZE];
static hid_gamepad_t bt_le_gamepads[MAX_NUM_HID_SERVICES];
static pad_input_t bt_le_pad_inputs[MAX_NUM_HID_SERVICES];
static bool bt_le_pad_valid[MAX_NUM_HID_SERVICES];
static bool bt_le_has_gamepad;

static void bt_classic_schedule_inquiry(uint32_t delay_ms);

static bool bt_pairing_allowed(void)
{
    return bt_radio_on
        && ((settings_get()->bt_pairing != SETTINGS_BT_PAIRING_PAIRED_ONLY) || bt_pair_window);
}

static bool bt_classic_known(bd_addr_t address)
{
    link_key_t key;
    link_key_type_t type;

    return gap_get_link_key_for_bd_addr(address, key, &type);
}

/**
 * a pad's identity for the player manager: the same pad coming back keeps its
 * player (fnv-1a over the address)
 */
static uint32_t bt_identity(uint8_t const *address)
{
    uint32_t hash = 2166136261u;

    for (unsigned i = 0; i < sizeof(bd_addr_t); i++) {
        hash ^= address[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint8_t bt_classic_connected_count(void)
{
    uint8_t count = 0;

    for (uint8_t slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++) {
        if (bt_classic_connections[slot].in_use)
            count++;
    }

    return count;
}

static unsigned bt_pad_count(void)
{
    unsigned pads = bt_le_has_gamepad ? 1 : 0;

    for (unsigned slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++)
        pads += bt_classic_connections[slot].in_use && bt_classic_connections[slot].has_gamepad;
    return pads;
}

static char const *bt_le_state_name(bt_le_state_t state)
{
    switch (state) {
        case BT_LE_STATE_OFF:
            return "off";

        case BT_LE_STATE_SCANNING:
            return "scan";

        case BT_LE_STATE_CONNECTING:
            return "conn";

        case BT_LE_STATE_ENCRYPTING:
            return "pair";

        case BT_LE_STATE_SERVICE_QUERY:
            return "svc";

        case BT_LE_STATE_CHARACTERISTIC_QUERY:
            return "char";

        case BT_LE_STATE_ENABLE_KEYBOARD:
            return "kbd";

        case BT_LE_STATE_ENABLE_MOUSE:
            return "mouse";

        case BT_LE_STATE_ENABLE_GAMEPAD:
            return "pad";

        case BT_LE_STATE_READY:
            return "ready";

        case BT_LE_STATE_DISCONNECTING:
            return "disc";
    }

    return "?";
}

// the last status line, for the console's bt command
static char bt_status_text[32] = "not started";

char const *bt_hid_status(void)
{
    return bt_status_text;
}

static void bt_hid_update_status(void)
{
    char linebuf[32] = "";
    bool usable = false;

    if (!bt_radio_on) {
        snprintf(linebuf, sizeof(linebuf), "bt off");
    } else {
        unsigned pads = bt_pad_count();
        char const *phase = (bt_classic_state == BT_CLASSIC_SCANNING) ? ":scan"
            : (bt_classic_state == BT_CLASSIC_CONNECTING) ? ":conn" : "";

        // the whole line within the oled's 21 columns
        if (pads)
            snprintf(linebuf, sizeof(linebuf), "bt c%u le:%s j%u",
                bt_classic_connected_count(), bt_le_state_name(bt_le_state), pads);
        else
            snprintf(linebuf, sizeof(linebuf), "bt c%u%s le:%s",
                bt_classic_connected_count(), phase, bt_le_state_name(bt_le_state));

        // a link half way through pairing or service discovery cannot deliver
        // keystrokes yet, so only count devices which are actually reporting
        usable = (bt_classic_connected_count() > 0) || (bt_le_state == BT_LE_STATE_READY);
    }

    dbgcons_bt_status(linebuf);
    snprintf(bt_status_text, sizeof(bt_status_text), "%s", linebuf);
    status_led_source(STATUS_LED_SOURCE_BT, usable);
}

static void bt_le_set_state(bt_le_state_t state)
{
    bt_le_state = state;
    bt_hid_update_status();
}

/**
 * Put a pairing prompt on display; the status leds follow it, so every prompt
 * and every dismissal goes through this pair of helpers.
 */
static void bt_hid_passkey_prompt(char const *message)
{
    dbgcons_bt_passkey(message);
    status_led_pairing(true);
}

static void bt_hid_passkey_done(void)
{
    dbgcons_bt_passkey_clear();
    status_led_pairing(false);
}

static void bt_hid_show_passkey(char const *label, uint32_t passkey)
{
    char linebuf[32] = "";

    snprintf(linebuf, sizeof(linebuf), "bt %s %06lu", label, (unsigned long)passkey);
    bt_hid_passkey_prompt(linebuf);
}

static inline uint8_t bt_classic_input_slot(uint8_t classic_slot)
{
    return (uint8_t)(INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE + classic_slot);
}

static inline uint8_t bt_le_input_slot(void)
{
    return INPUT_BRIDGE_BT_LE_SLOT_BASE;
}

static int8_t bt_hid_clamp_i8(int32_t value)
{
    if (value > INT8_MAX)
        return INT8_MAX;
    if (value < INT8_MIN)
        return INT8_MIN;

    return (int8_t)value;
}

//--------------------------------------------------------------------+
// the event queue: btstack context in, main loop out
//--------------------------------------------------------------------+

static void bt_hid_enqueue(bt_hid_queue_entry_t const *entry)
{
    bt_hid_overflow_t *pending;
    uint32_t interrupts;

    if ((entry->slot < INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE) || (entry->slot >= INPUT_BRIDGE_BT_SLOT_END))
        return;
    if (!bt_radio_on && (entry->type != BT_HID_QUEUE_DISCONNECT))
        return;

    // the cyw43 background callbacks and bt_hid_task() both run on core0;
    // keep the fifo and the overflow snapshots consistent against the former
    interrupts = save_and_disable_interrupts();

    if (!bt_hid_queue_resync && queue_try_add(&bt_hid_queue, entry)) {
        restore_interrupts(interrupts);
        return;
    }

    // full: from now on everything goes to the per-slot snapshots until the
    // main loop has drained them, so nothing overtakes what is in the fifo.
    // intermediate states may be coalesced, but the final key / button state,
    // the motion and every disconnect and connect are kept.
    bt_hid_queue_resync = true;
    pending = &bt_hid_overflow[entry->slot - INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE];

    switch (entry->type) {
        case BT_HID_QUEUE_DISCONNECT:
            memset(pending, 0, sizeof(*pending));
            pending->disconnect = true;
            break;

        case BT_HID_QUEUE_PAD_CONNECT:
            pending->connect = entry->connect;
            pending->connect_pending = true;
            break;

        case BT_HID_QUEUE_KEYBOARD:
            pending->keyboard = entry->keyboard;
            pending->keyboard_pending = true;
            break;

        case BT_HID_QUEUE_MOUSE: {
            bt_hid_mouse_report_t mouse = entry->mouse;

            if (pending->mouse_pending) {
                mouse.x = bt_hid_clamp_i8((int32_t)mouse.x + pending->mouse.x);
                mouse.y = bt_hid_clamp_i8((int32_t)mouse.y + pending->mouse.y);
                mouse.wheel = bt_hid_clamp_i8((int32_t)mouse.wheel + pending->mouse.wheel);
            }
            pending->mouse = mouse;
            pending->mouse_pending = true;
            break;
        }

        case BT_HID_QUEUE_PAD: {
            pad_input_t pad = entry->pad;

            if (pending->pad_pending) {
                pad.touch_dx = bt_hid_clamp_i8((int32_t)pad.touch_dx + pending->pad.touch_dx);
                pad.touch_dy = bt_hid_clamp_i8((int32_t)pad.touch_dy + pending->pad.touch_dy);
            }
            pending->pad = pad;
            pending->pad_pending = true;
            break;
        }
    }

    restore_interrupts(interrupts);
}

static bool bt_hid_dequeue(bt_hid_queue_entry_t *entry)
{
    uint32_t interrupts = save_and_disable_interrupts();

    if (queue_try_remove(&bt_hid_queue, entry)) {
        restore_interrupts(interrupts);
        return true;
    }

    // the fifo is drained; now the snapshots. a disconnect goes before a
    // connect, which goes before any report from that new connection
    for (uint8_t index = 0; bt_hid_queue_resync && (index < BT_HID_SLOTS); index++) {
        bt_hid_overflow_t *pending = &bt_hid_overflow[index];

        entry->slot = (uint8_t)(INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE + index);
        if (pending->disconnect) {
            pending->disconnect = false;
            entry->type = BT_HID_QUEUE_DISCONNECT;
        } else if (pending->connect_pending) {
            pending->connect_pending = false;
            entry->type = BT_HID_QUEUE_PAD_CONNECT;
            entry->connect = pending->connect;
        } else if (pending->keyboard_pending) {
            pending->keyboard_pending = false;
            entry->type = BT_HID_QUEUE_KEYBOARD;
            entry->keyboard = pending->keyboard;
        } else if (pending->mouse_pending) {
            pending->mouse_pending = false;
            entry->type = BT_HID_QUEUE_MOUSE;
            entry->mouse = pending->mouse;
        } else if (pending->pad_pending) {
            pending->pad_pending = false;
            entry->type = BT_HID_QUEUE_PAD;
            entry->pad = pending->pad;
        } else {
            continue;
        }
        restore_interrupts(interrupts);
        return true;
    }

    bt_hid_queue_resync = false;
    restore_interrupts(interrupts);
    return false;
}

static void bt_hid_enqueue_disconnect(uint8_t slot)
{
    bt_hid_queue_entry_t entry = {
        .slot = slot,
        .type = BT_HID_QUEUE_DISCONNECT,
    };

    bt_hid_enqueue(&entry);
}

static void bt_hid_enqueue_keyboard(uint8_t slot, bt_hid_keyboard_report_t const *report)
{
    bt_hid_queue_entry_t entry = {
        .slot = slot,
        .type = BT_HID_QUEUE_KEYBOARD,
    };

    entry.keyboard = *report;
    bt_hid_enqueue(&entry);
}

static void bt_hid_enqueue_mouse(uint8_t slot, bt_hid_mouse_report_t const *report)
{
    bt_hid_queue_entry_t entry = {
        .slot = slot,
        .type = BT_HID_QUEUE_MOUSE,
    };

    entry.mouse = *report;
    bt_hid_enqueue(&entry);
}

static void bt_hid_enqueue_pad_connect(uint8_t slot, uint8_t const *address, bt_pad_kind_t kind)
{
    bt_hid_queue_entry_t entry = {
        .slot = slot,
        .type = BT_HID_QUEUE_PAD_CONNECT,
    };

    entry.connect.identity = bt_identity(address);
    entry.connect.kind = (uint8_t)kind;
    bt_hid_enqueue(&entry);
}

static void bt_hid_enqueue_pad(uint8_t slot, pad_input_t const *input)
{
    bt_hid_queue_entry_t entry = {
        .slot = slot,
        .type = BT_HID_QUEUE_PAD,
    };

    entry.pad = *input;
    bt_hid_enqueue(&entry);
}

static void bt_hid_append_keycode(bt_hid_keyboard_report_t *report, uint8_t keycode)
{
    for (uint8_t pos = 0; pos < 6; pos++) {
        if (report->keycode[pos] == keycode)
            return;
        if (report->keycode[pos] == 0) {
            report->keycode[pos] = keycode;
            return;
        }
    }
}

//--------------------------------------------------------------------+
// classic
//--------------------------------------------------------------------+

static int8_t bt_classic_find_slot(uint16_t hid_cid)
{
    for (uint8_t slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++) {
        if (bt_classic_connections[slot].in_use && (bt_classic_connections[slot].hid_cid == hid_cid))
            return (int8_t)slot;
    }

    return -1;
}

static int8_t bt_classic_find_free_slot(void)
{
    for (uint8_t slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++)
        if (!bt_classic_connections[slot].in_use)
            return (int8_t)slot;

    return -1;
}

/**
 * look for a classic pad: only with a free slot and no classic pad connected
 */
static bool bt_classic_wants_inquiry(void)
{
    if (!bt_radio_on || !bt_classic_working || bt_scan_paused || (bt_classic_find_free_slot() < 0))
        return false;
    for (unsigned slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++)
        if (bt_classic_connections[slot].in_use && bt_classic_connections[slot].has_gamepad)
            return false;
    return true;
}

static void bt_classic_inquiry_timer(btstack_timer_source_t *timer)
{
    UNUSED(timer);

    if (!bt_classic_wants_inquiry() || (bt_classic_state != BT_CLASSIC_IDLE))
        return;

    // short searches with a quiet interval between them
    if (gap_inquiry_start(3) == ERROR_CODE_SUCCESS) {
        bt_classic_state = BT_CLASSIC_SCANNING;
        bt_hid_update_status();
    } else {
        bt_classic_schedule_inquiry(5000);
    }
}

static void bt_classic_schedule_inquiry(uint32_t delay_ms)
{
    btstack_run_loop_remove_timer(&bt_classic_scan_timer);
    if (!bt_classic_wants_inquiry() || (bt_classic_state != BT_CLASSIC_IDLE))
        return;
    bt_classic_scan_timer.process = &bt_classic_inquiry_timer;
    btstack_run_loop_set_timer(&bt_classic_scan_timer, delay_ms);
    btstack_run_loop_add_timer(&bt_classic_scan_timer);
}

static void bt_classic_stop_inquiry(void)
{
    btstack_run_loop_remove_timer(&bt_classic_scan_timer);
    bt_classic_candidate = false;
    if (bt_classic_state == BT_CLASSIC_SCANNING) {
        bt_classic_state = BT_CLASSIC_IDLE;
        gap_inquiry_stop();
    }
}

static void bt_classic_inquiry_result(uint8_t const *packet, uint16_t size)
{
    uint32_t cod;
    unsigned minor;
    bd_addr_t address;

    if ((size < 27) || (bt_classic_state != BT_CLASSIC_SCANNING) || bt_classic_candidate
        || !bt_classic_wants_inquiry())
        return;

    // peripheral, joystick or gamepad only: nearby keyboards, mice, phones and
    // consoles are not claimed just because they are discoverable
    cod = gap_event_inquiry_result_get_class_of_device(packet);
    minor = (cod >> 2) & 0x0f;
    if (((cod & 0x1f00) != 0x0500) || ((minor != 1) && (minor != 2)))
        return;

    gap_event_inquiry_result_get_bd_addr(packet, address);
    if (!bt_pairing_allowed() && !bt_classic_known(address))
        return;
    for (unsigned slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++)
        if (bt_classic_connections[slot].in_use
            && !memcmp(address, bt_classic_connections[slot].address, sizeof(bd_addr_t)))
            return;

    memcpy(bt_classic_candidate_addr, address, sizeof(bd_addr_t));
    bt_classic_candidate = true;
    // connect only once the inquiry has been cancelled
    gap_inquiry_stop();
}

static void bt_classic_inquiry_complete(void)
{
    bt_classic_state = BT_CLASSIC_IDLE;
    if (bt_classic_candidate && bt_classic_wants_inquiry()) {
        uint8_t result;

        bt_classic_candidate = false;
        bt_classic_state = BT_CLASSIC_CONNECTING;
        result = hid_host_connect(bt_classic_candidate_addr, HID_PROTOCOL_MODE_REPORT, &bt_classic_outgoing_cid);
        if (result != ERROR_CODE_SUCCESS) {
            ahprintf("[bt] classic pad connect failed: 0x%02x\n", result);
            bt_classic_outgoing_cid = 0;
            bt_classic_state = BT_CLASSIC_IDLE;
        }
    } else {
        bt_classic_candidate = false;
    }
    bt_classic_schedule_inquiry(5000);
    bt_hid_update_status();
}

/**
 * a ds4 or dualsense: ask for a feature report, which switches it from its
 * short report to the full one with the touchpad. deferred until btstack has
 * finished its own sdp / set_protocol work on the new connection.
 */
static void bt_classic_pad_setup(btstack_timer_source_t *timer)
{
    bt_classic_connection_t *connection = timer->context;
    uint8_t result;

    if (!connection->in_use || connection->setup_done)
        return;

    if (connection->setup_requested) {
        ahprintf("[bt] %s: no answer to the feature report request\n", bt_pad_names[connection->pad_kind]);
        connection->setup_done = true;
        return;
    }

    // ds4: feature 0x02, as stefan reinauer's bt_ds4 setup asks for; dualsense
    // (and a pad that may be one): its calibration feature 0x05, as the linux
    // hid-playstation driver reads it
    result = hid_host_send_get_report(connection->hid_cid, HID_REPORT_TYPE_FEATURE,
        (connection->pad_kind == BT_PAD_DS4) ? 0x02 : 0x05);
    if (result == ERROR_CODE_SUCCESS) {
        connection->setup_requested = true;
        btstack_run_loop_set_timer(timer, BT_PAD_SETUP_TIMEOUT_MS);
        btstack_run_loop_add_timer(timer);
    } else if (++connection->setup_attempts < 5) {
        btstack_run_loop_set_timer(timer, 200);
        btstack_run_loop_add_timer(timer);
    } else {
        ahprintf("[bt] %s: feature report request failed: 0x%02x\n", bt_pad_names[connection->pad_kind], result);
        connection->setup_done = true;
    }
}

static void bt_classic_start_setup(bt_classic_connection_t *connection)
{
    connection->setup_attempts = 0;
    connection->setup_requested = false;
    connection->setup_done = false;
    connection->setup_timer.process = &bt_classic_pad_setup;
    connection->setup_timer.context = connection;
    btstack_run_loop_remove_timer(&connection->setup_timer);
    btstack_run_loop_set_timer(&connection->setup_timer, 100);
    btstack_run_loop_add_timer(&connection->setup_timer);
}

/**
 * does the descriptor declare an input or output report with this id? a walk
 * over the short items, following the report id (a global item).
 */
static bool bt_descriptor_has_report(uint8_t const *descriptor, uint16_t length, uint8_t id)
{
    uint8_t current = 0;
    uint16_t pos = 0;

    while (pos < length) {
        uint8_t prefix = descriptor[pos];
        uint8_t size = prefix & 0x03;

        if (prefix == 0xfe) {
            // long item: size byte, tag byte, data
            if ((uint32_t)pos + 2 >= length)
                break;
            pos = (uint16_t)(pos + 3 + descriptor[pos + 1]);
            continue;
        }

        if (size == 3)
            size = 4;
        if ((uint32_t)pos + 1 + size > length)
            break;

        switch (prefix & 0xfc) {
            case 0x84:                      // report id
                if (size)
                    current = descriptor[pos + 1];
                break;

            case 0x80:                      // input
            case 0x90:                      // output
                if (current == id)
                    return true;
                break;

            default:
                break;
        }
        pos = (uint16_t)(pos + 1 + size);
    }

    return false;
}

/**
 * the playstation pads' descriptors open with a game pad collection and its
 * report 0x01
 */
static bool bt_sony_layout(uint8_t const *descriptor, uint16_t length)
{
    static const uint8_t gamepad[] = { 0x05, 0x01, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x01 };

    return descriptor && (length > sizeof(gamepad)) && !memcmp(descriptor, gamepad, sizeof(gamepad));
}

/**
 * a dualsense: laid out like that, and declaring the full bluetooth report
 * 0x31, which the ds4's descriptor does not. both pads call themselves
 * "Wireless Controller", so the name cannot tell them apart.
 */
static bool bt_ds5_matches_descriptor(uint8_t const *descriptor, uint16_t length)
{
    return bt_sony_layout(descriptor, length) && bt_descriptor_has_report(descriptor, length, 0x31);
}

static void bt_classic_descriptor_ready(uint8_t slot)
{
    // playstation pads number cross 2, circle 3, square 1, triangle 4
    static uint8_t const playstation[PAD_LOGICAL_BUTTONS] = { 2, 3, 1, 4, 5, 6, 10 };
    bt_classic_connection_t *connection = &bt_classic_connections[slot];
    uint8_t const *descriptor = hid_descriptor_storage_get_descriptor_data(connection->hid_cid);
    uint16_t length = hid_descriptor_storage_get_descriptor_len(connection->hid_cid);

    if (!descriptor || !length || connection->descriptor_ready)
        return;
    connection->descriptor_ready = true;

    if (bt_ds4_matches_descriptor(descriptor, length))
        connection->pad_kind = BT_PAD_DS4;
    else if (bt_ds5_matches_descriptor(descriptor, length))
        connection->pad_kind = BT_PAD_DS5;
    else
        connection->pad_kind = BT_PAD_GENERIC;

    connection->has_gamepad = (connection->pad_kind != BT_PAD_GENERIC)
        || hid_gamepad_parse(&connection->gamepad, descriptor, length);

    // a playstation-like pad neither decoder knows (a dualsense whose
    // descriptor hides its 0x31, say): its buttons in the playstation order,
    // and asked for the dualsense's feature report like a dualsense, so a
    // real one switches to its full report and is then taken for one
    if ((connection->pad_kind == BT_PAD_GENERIC) && connection->has_gamepad && bt_sony_layout(descriptor, length)) {
        connection->sony_layout = true;
        memcpy(connection->gamepad.defaults, playstation, sizeof(playstation));
    }

    // a controller layout the parser cannot use still must not drive the mouse
    if ((length >= 6) && (descriptor[0] == 0x05) && (descriptor[1] == 0x01) && (descriptor[2] == 0x09)
        && ((descriptor[3] == 0x04) || (descriptor[3] == 0x05)) && (descriptor[4] == 0xa1) && (descriptor[5] == 0x01))
        connection->gamepad_device = true;
    connection->gamepad_device |= connection->has_gamepad;

    if (connection->has_gamepad) {
        bt_classic_stop_inquiry();
        bt_hid_enqueue_pad_connect(bt_classic_input_slot(slot), connection->address, connection->pad_kind);
        if ((connection->pad_kind != BT_PAD_GENERIC) || connection->sony_layout)
            bt_classic_start_setup(connection);
    }

    ahprintf("[bt] classic descriptor %u bytes, %s%s\n", length,
        connection->has_gamepad ? bt_pad_names[connection->pad_kind]
            : connection->gamepad_device ? "controller, not decoded" : "keyboard / mouse",
        connection->sony_layout ? " (playstation layout)" : "");
    bt_hid_update_status();
}

static void bt_classic_disconnect_slot(uint8_t slot)
{
    if (!bt_classic_connections[slot].in_use)
        return;

    bt_hid_enqueue_disconnect(bt_classic_input_slot(slot));
    btstack_run_loop_remove_timer(&bt_classic_connections[slot].setup_timer);
    memset(&bt_classic_connections[slot], 0, sizeof(bt_classic_connections[slot]));
    bt_classic_schedule_inquiry(1000);
    bt_hid_update_status();
}

/**
 * a controller we took for something else turns out to be a ds4 or a
 * dualsense by its full report: hand it to the player manager again as one
 * (its player is kept for it across the disconnect)
 */
static void bt_classic_become(uint8_t slot, bt_pad_kind_t kind)
{
    bt_classic_connection_t *connection = &bt_classic_connections[slot];
    uint8_t input_slot = bt_classic_input_slot(slot);

    ahprintf("[bt] classic %u: full report, a %s\n", slot, bt_pad_names[kind]);
    if (connection->has_gamepad)
        bt_hid_enqueue_disconnect(input_slot);

    btstack_run_loop_remove_timer(&connection->setup_timer);
    memset(&connection->ds4, 0, sizeof(connection->ds4));
    memset(&connection->ds5, 0, sizeof(connection->ds5));
    connection->pad_kind = kind;
    connection->sony_layout = false;
    connection->has_gamepad = true;
    connection->gamepad_device = true;
    // it already sends its full report: nothing to ask for
    connection->setup_requested = true;
    connection->setup_done = true;
    bt_classic_stop_inquiry();
    bt_hid_enqueue_pad_connect(input_slot, connection->address, kind);
    bt_hid_update_status();
}

static void bt_classic_pad_report(uint8_t slot, uint8_t const *report, uint16_t report_len)
{
    bt_classic_connection_t *connection = &bt_classic_connections[slot];
    pad_input_t input;
    bool decoded;

    switch (connection->pad_kind) {
        case BT_PAD_DS4:
            decoded = bt_ds4_decode(&connection->ds4, report, report_len, &input);
            break;

        case BT_PAD_DS5:
            decoded = ds5_decode_bt(&connection->ds5, report, report_len, &input);
            break;

        default:
            decoded = hid_gamepad_decode(&connection->gamepad, report, report_len, &input);
            // a playstation pad's ps button is its 13th
            if (decoded && connection->sony_layout)
                input.home = (input.buttons & (1u << 12)) != 0;
            break;
    }

    if (decoded)
        bt_hid_enqueue_pad(bt_classic_input_slot(slot), &input);
}

static void bt_classic_parse_report(uint8_t slot, uint8_t const *report, uint16_t report_len)
{
    bt_classic_connection_t *connection = &bt_classic_connections[slot];
    bt_hid_keyboard_report_t keyboard = { 0, 0, {0} };
    bt_hid_mouse_report_t mouse = { 0 };
    btstack_hid_parser_t parser;
    const uint8_t *descriptor;
    uint16_t descriptor_len;
    bool saw_keyboard = false;
    bool saw_mouse = false;

    if (!bt_radio_on)
        return;

    if ((report == NULL) || (report_len < 2) || (report[0] != 0xa1))
        return;

    report++;
    report_len--;

    if ((connection->protocol_mode != HID_PROTOCOL_MODE_BOOT) && !connection->descriptor_ready)
        return;

    // a ds4 or dualsense the descriptor did not give away shows itself by its
    // full report (crc checked)
    if (connection->gamepad_device && (connection->pad_kind == BT_PAD_GENERIC) && (report_len >= 78)) {
        pad_input_t input;

        if (report[0] == 0x31) {
            ds5_t probe;

            memset(&probe, 0, sizeof(probe));
            if (ds5_decode_bt(&probe, report, report_len, &input))
                bt_classic_become(slot, BT_PAD_DS5);
        } else if (report[0] == 0x11) {
            bt_ds4_t probe;

            memset(&probe, 0, sizeof(probe));
            if (bt_ds4_decode(&probe, report, report_len, &input))
                bt_classic_become(slot, BT_PAD_DS4);
        }
    }

    if (connection->has_gamepad) {
        bt_classic_pad_report(slot, report, report_len);
        return;
    }
    if (connection->gamepad_device)
        return;

    if (connection->protocol_mode == HID_PROTOCOL_MODE_BOOT) {
        descriptor = btstack_hid_get_boot_descriptor_data();
        descriptor_len = btstack_hid_get_boot_descriptor_len();
    } else {
        descriptor = hid_descriptor_storage_get_descriptor_data(connection->hid_cid);
        descriptor_len = hid_descriptor_storage_get_descriptor_len(connection->hid_cid);

        if ((descriptor == NULL) || (descriptor_len == 0))
            return;
    }

    btstack_hid_parser_init(&parser, descriptor, descriptor_len, HID_REPORT_TYPE_INPUT, report, report_len);

    while (btstack_hid_parser_has_more(&parser)) {
        uint16_t usage_page;
        uint16_t usage;
        int32_t value;

        btstack_hid_parser_get_field(&parser, &usage_page, &usage, &value);

        switch (usage_page) {
            case HID_USAGE_PAGE_KEYBOARD:
                saw_keyboard = true;

                if ((usage >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL) && (usage <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI)) {
                    if (value)
                        keyboard.modifier |= (uint8_t)(1u << (usage - HID_USAGE_KEY_KEYBOARD_LEFTCONTROL));
                    break;
                }

                if ((usage != HID_USAGE_KEY_RESERVED) && (usage < 0x100))
                    bt_hid_append_keycode(&keyboard, (uint8_t)usage);
                break;

            case HID_USAGE_PAGE_BUTTON:
                if ((usage >= 1) && (usage <= 8)) {
                    saw_mouse = true;
                    if (value)
                        mouse.buttons |= (uint8_t)(1u << (usage - 1u));
                }
                break;

            case HID_USAGE_PAGE_DESKTOP:
                switch (usage) {
                    case HID_USAGE_DESKTOP_X:
                        saw_mouse = true;
                        mouse.x = bt_hid_clamp_i8(value);
                        break;

                    case HID_USAGE_DESKTOP_Y:
                        saw_mouse = true;
                        mouse.y = bt_hid_clamp_i8(value);
                        break;

                    case HID_USAGE_DESKTOP_WHEEL:
                        saw_mouse = true;
                        mouse.wheel = bt_hid_clamp_i8(value);
                        break;

                    default:
                        break;
                }
                break;

            default:
                break;
        }
    }

    if (saw_keyboard)
        bt_hid_enqueue_keyboard(bt_classic_input_slot(slot), &keyboard);

    if (saw_mouse)
        bt_hid_enqueue_mouse(bt_classic_input_slot(slot), &mouse);
}

static void bt_classic_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);

    if ((packet_type != HCI_EVENT_PACKET) || (size < 2))
        return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (size < 3)
                break;
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                bt_controller_off = false;
                bt_classic_working = true;
                bt_discoverable = (int8_t)bt_pairing_allowed();
                gap_discoverable_control((uint8_t)bt_discoverable);
                bt_classic_schedule_inquiry(1000);
                bt_hid_update_status();
            } else if (btstack_event_state_get_state(packet) == HCI_STATE_OFF) {
                bt_controller_off = true;
                bt_classic_working = false;
                bt_discoverable = -1;
                bt_classic_stop_inquiry();
                bt_classic_state = BT_CLASSIC_IDLE;
                bt_classic_outgoing_cid = 0;
                for (uint8_t slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++)
                    bt_classic_disconnect_slot(slot);
                bt_hid_passkey_done();
                bt_hid_update_status();
            }
            break;

        case GAP_EVENT_INQUIRY_RESULT:
            bt_classic_inquiry_result(packet, size);
            break;

        case GAP_EVENT_INQUIRY_COMPLETE:
            if (bt_classic_state == BT_CLASSIC_SCANNING)
                bt_classic_inquiry_complete();
            break;

        case HCI_EVENT_PIN_CODE_REQUEST: {
            bd_addr_t event_addr;

            if (size < 8)
                break;
            hci_event_pin_code_request_get_bd_addr(packet, event_addr);
            if (bt_pairing_allowed()) {
                bt_hid_passkey_prompt("bt pin 0000");
                gap_pin_code_response(event_addr, "0000");
            } else {
                gap_pin_code_negative(event_addr);
            }
            break;
        }

        case HCI_EVENT_USER_CONFIRMATION_REQUEST: {
            bd_addr_t event_addr;

            if (size < 12)
                break;
            hci_event_user_confirmation_request_get_bd_addr(packet, event_addr);
            if (bt_pairing_allowed() || bt_classic_known(event_addr)) {
                bt_hid_show_passkey("conf",
                    hci_event_user_confirmation_request_get_numeric_value(packet));
                gap_ssp_confirmation_response(event_addr);
            } else {
                gap_ssp_confirmation_negative(event_addr);
            }
            break;
        }

        // the prompts above are put up by a request event, but nothing
        // guarantees an hid connection follows one: an ssp attempt from a phone
        // is auto-accepted and then simply never opens a channel. dismiss the
        // prompt whenever a pairing attempt reaches any conclusion at all, or a
        // stray one would sit on the display forever - and its blink would keep
        // the activity led hostage with it.
        case HCI_EVENT_SIMPLE_PAIRING_COMPLETE:
        case HCI_EVENT_AUTHENTICATION_COMPLETE:
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            bt_hid_passkey_done();
            break;

        case HCI_EVENT_HID_META:
            if (size < 3)
                break;
            switch (hci_event_hid_meta_get_subevent_code(packet)) {
                case HID_SUBEVENT_INCOMING_CONNECTION: {
                    uint16_t hid_cid;
                    bd_addr_t incoming;

                    if (size < 14)
                        break;
                    hid_cid = hid_subevent_incoming_connection_get_hid_cid(packet);
                    if (hid_subevent_incoming_connection_get_status(packet) != ERROR_CODE_SUCCESS)
                        break;
                    hid_subevent_incoming_connection_get_address(packet, incoming);
                    if (bt_radio_on && (bt_pairing_allowed() || bt_classic_known(incoming))
                        && (bt_classic_find_free_slot() >= 0)) {
                        bt_classic_stop_inquiry();
                        hid_host_accept_connection(hid_cid, bt_classic_report_mode);
                    } else {
                        hid_host_decline_connection(hid_cid);
                    }
                    break;
                }

                case HID_SUBEVENT_CONNECTION_OPENED: {
                    uint8_t status;
                    uint16_t hid_cid;
                    bool outgoing;
                    int8_t slot;

                    if (size < 15)
                        break;
                    status = hid_subevent_connection_opened_get_status(packet);
                    hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                    outgoing = (hid_cid == bt_classic_outgoing_cid);
                    if (outgoing) {
                        bt_classic_outgoing_cid = 0;
                        bt_classic_state = BT_CLASSIC_IDLE;
                    }

                    if (status != ERROR_CODE_SUCCESS) {
                        ahprintf("[bt] classic hid connect failed: 0x%02x\n", status);
                        bt_hid_passkey_done();
                        slot = bt_classic_find_slot(hid_cid);
                        if (slot >= 0)
                            bt_classic_disconnect_slot((uint8_t)slot);
                        bt_classic_schedule_inquiry(5000);
                        bt_hid_update_status();
                        break;
                    }

                    if (bt_classic_find_slot(hid_cid) >= 0)
                        break;
                    slot = bt_classic_find_free_slot();
                    if (slot < 0) {
                        hid_host_disconnect(hid_cid);
                        break;
                    }

                    memset(&bt_classic_connections[slot], 0, sizeof(bt_classic_connections[slot]));
                    bt_classic_connections[slot].in_use = true;
                    bt_classic_connections[slot].hid_cid = hid_cid;
                    bt_classic_connections[slot].protocol_mode = HID_PROTOCOL_MODE_REPORT;
                    // we only call out to what the inquiry took for a pad
                    bt_classic_connections[slot].gamepad_device = outgoing;
                    bt_classic_connections[slot].player = -1;
                    hid_subevent_connection_opened_get_bd_addr(packet, bt_classic_connections[slot].address);
                    bt_classic_schedule_inquiry(5000);
                    bt_hid_passkey_done();
                    bt_hid_update_status();
                    break;
                }

                case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
                    int8_t slot;

                    if (size < 6)
                        break;
                    slot = bt_classic_find_slot(hid_subevent_descriptor_available_get_hid_cid(packet));
                    if ((slot >= 0) && (hid_subevent_descriptor_available_get_status(packet) == ERROR_CODE_SUCCESS))
                        bt_classic_descriptor_ready((uint8_t)slot);
                    break;
                }

                case HID_SUBEVENT_SET_PROTOCOL_RESPONSE: {
                    int8_t slot;

                    if (size < 7)
                        break;
                    slot = bt_classic_find_slot(hid_subevent_set_protocol_response_get_hid_cid(packet));
                    if ((slot >= 0)
                        && (hid_subevent_set_protocol_response_get_handshake_status(packet) == HID_HANDSHAKE_PARAM_TYPE_SUCCESSFUL)) {
                        bt_classic_connections[slot].protocol_mode =
                            (hid_protocol_mode_t)hid_subevent_set_protocol_response_get_protocol_mode(packet);
                    }
                    break;
                }

                case HID_SUBEVENT_GET_REPORT_RESPONSE: {
                    bt_classic_connection_t *connection;
                    int8_t slot;

                    if (size < 8)
                        break;
                    slot = bt_classic_find_slot(hid_subevent_get_report_response_get_hid_cid(packet));
                    if (slot < 0)
                        break;
                    connection = &bt_classic_connections[slot];
                    if (!connection->setup_requested || connection->setup_done)
                        break;

                    // the pad's answer, whatever it is, ends the setup
                    btstack_run_loop_remove_timer(&connection->setup_timer);
                    connection->setup_done = true;
                    ahprintf("[bt] %s: feature report answered (0x%02x)\n", bt_pad_names[connection->pad_kind],
                        hid_subevent_get_report_response_get_handshake_status(packet));
                    break;
                }

                case HID_SUBEVENT_REPORT: {
                    int8_t slot;

                    if ((size < 7) || (hid_subevent_report_get_report_len(packet) > size - 7))
                        break;
                    slot = bt_classic_find_slot(hid_subevent_report_get_hid_cid(packet));
                    if (slot >= 0)
                        bt_classic_parse_report((uint8_t)slot, hid_subevent_report_get_report(packet),
                            hid_subevent_report_get_report_len(packet));
                    break;
                }

                case HID_SUBEVENT_CONNECTION_CLOSED: {
                    uint16_t cid;
                    int8_t slot;

                    if (size < 5)
                        break;
                    cid = hid_subevent_connection_closed_get_hid_cid(packet);
                    if (cid == bt_classic_outgoing_cid) {
                        bt_classic_outgoing_cid = 0;
                        bt_classic_state = BT_CLASSIC_IDLE;
                        bt_classic_schedule_inquiry(5000);
                    }
                    slot = bt_classic_find_slot(cid);

                    bt_hid_passkey_done();

                    if (slot >= 0)
                        bt_classic_disconnect_slot((uint8_t)slot);
                    break;
                }

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

//--------------------------------------------------------------------+
// le
//--------------------------------------------------------------------+

static void bt_le_clear_characteristics(void)
{
    memset(&bt_le_hid_service, 0, sizeof(bt_le_hid_service));
    memset(&bt_le_protocol_mode_characteristic, 0, sizeof(bt_le_protocol_mode_characteristic));
    memset(&bt_le_boot_keyboard_characteristic, 0, sizeof(bt_le_boot_keyboard_characteristic));
    memset(&bt_le_boot_mouse_characteristic, 0, sizeof(bt_le_boot_mouse_characteristic));
    memset(&bt_le_keyboard_notifications, 0, sizeof(bt_le_keyboard_notifications));
    memset(&bt_le_mouse_notifications, 0, sizeof(bt_le_mouse_notifications));
    bt_le_has_protocol_mode = false;
    bt_le_has_boot_keyboard = false;
    bt_le_has_boot_mouse = false;
    bt_le_hids_cid = 0;
    memset(bt_le_gamepads, 0, sizeof(bt_le_gamepads));
    memset(bt_le_pad_inputs, 0, sizeof(bt_le_pad_inputs));
    memset(bt_le_pad_valid, 0, sizeof(bt_le_pad_valid));
    bt_le_has_gamepad = false;
}

static bool bt_le_adv_event_contains_hid_service(uint8_t const *packet)
{
    uint8_t ad_len = gap_event_advertising_report_get_data_length(packet);
    uint8_t const *ad_data = gap_event_advertising_report_get_data(packet);

    return ad_data_contains_uuid16(ad_len, ad_data, ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE);
}

static void bt_le_start_scan(void)
{
    if (!bt_hid_ready || !bt_radio_on || !bt_classic_working || bt_scan_paused
        || (bt_le_connection_handle != HCI_CON_HANDLE_INVALID))
        return;

    bt_le_clear_characteristics();
    bt_hid_passkey_done();
    bt_le_set_state(BT_LE_STATE_SCANNING);
    // active: some le pads put their hid service uuid in the scan response
    gap_set_scan_parameters(1, 48, 48);
    gap_start_scan();
}

static void bt_le_restart_scan(void)
{
    bt_le_connection_handle = HCI_CON_HANDLE_INVALID;
    bt_hid_passkey_done();
    bt_le_set_state(BT_LE_STATE_OFF);
    bt_le_start_scan();
}

static void bt_le_disconnect_and_restart(void)
{
    if (bt_le_connection_handle != HCI_CON_HANDLE_INVALID) {
        bt_le_set_state(BT_LE_STATE_DISCONNECTING);
        gap_disconnect(bt_le_connection_handle);
    } else {
        bt_le_restart_scan();
    }
}

static void bt_le_gamepad_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    // hids uses HCI_EVENT_PACKET for its connection events and
    // HCI_EVENT_GATTSERVICE_META for notifications: look at the event itself
    UNUSED(packet_type);
    UNUSED(channel);

    if ((size < 5) || (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) || !bt_le_hids_cid)
        return;
    if (little_endian_read_16(packet, 3) != bt_le_hids_cid)
        return;

    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            uint8_t status;
            uint8_t services;

            if ((size < 8) || (bt_le_state != BT_LE_STATE_ENABLE_GAMEPAD))
                break;
            status = gattservice_subevent_hid_service_connected_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                ahprintf("[btle] pad discovery failed: 0x%02x\n", status);
                bt_le_disconnect_and_restart();
                break;
            }

            services = gattservice_subevent_hid_service_connected_get_num_instances(packet);
            for (unsigned i = 0; (i < services) && (i < MAX_NUM_HID_SERVICES); i++) {
                uint8_t const *descriptor = hids_host_descriptor_storage_get_descriptor_data(bt_le_hids_cid, (uint8_t)i);
                uint16_t length = hids_host_descriptor_storage_get_descriptor_len(bt_le_hids_cid, (uint8_t)i);

                if (hid_gamepad_parse(&bt_le_gamepads[i], descriptor, length))
                    bt_le_has_gamepad = true;
            }
            if (!bt_le_has_gamepad) {
                ahprintf("[btle] no usable pad report map\n");
                bt_le_disconnect_and_restart();
                break;
            }

            bt_hid_enqueue_pad_connect(bt_le_input_slot(), bt_le_addr, BT_PAD_GENERIC);
            bt_hid_passkey_done();
            bt_le_set_state(BT_LE_STATE_READY);
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            uint8_t service;
            uint8_t const *report;
            uint16_t length;
            hid_gamepad_t *pad;
            pad_input_t input;

            if ((size < 10) || (bt_le_state != BT_LE_STATE_READY) || !bt_le_has_gamepad)
                break;
            service = gattservice_subevent_hid_report_get_service_index(packet);
            if ((service >= MAX_NUM_HID_SERVICES) || !bt_le_gamepads[service].field_count)
                break;
            report = gattservice_subevent_hid_report_get_report(packet);
            length = gattservice_subevent_hid_report_get_report_len(packet);
            if ((length == 0) || (length > size - 9u))
                break;

            pad = &bt_le_gamepads[service];
            // hids always puts the report reference id first, even for id zero
            if (!pad->report_ids) {
                if (*report != 0)
                    break;
                report++;
                length--;
            }
            if (!hid_gamepad_decode(pad, report, length, &bt_le_pad_inputs[service]))
                break;
            bt_le_pad_valid[service] = true;

            // a pad with more than one hid service: the analog side from the
            // one that just reported, buttons and d-pad from all of them
            input = bt_le_pad_inputs[service];
            for (unsigned i = 0; i < MAX_NUM_HID_SERVICES; i++) {
                if ((i == service) || !bt_le_pad_valid[i])
                    continue;
                input.buttons |= bt_le_pad_inputs[i].buttons;
                input.dpad |= bt_le_pad_inputs[i].dpad;
            }
            bt_hid_enqueue_pad(bt_le_input_slot(), &input);
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
            // the hci disconnection handler releases the input and scans again
            bt_le_has_gamepad = false;
            break;

        default:
            break;
    }
}

static void bt_le_ready(void)
{
    uint8_t boot_protocol_mode = 0;

    if (bt_le_has_protocol_mode) {
        gatt_client_write_value_of_characteristic_without_response(bt_le_connection_handle,
            bt_le_protocol_mode_characteristic.value_handle, 1, &boot_protocol_mode);
    }

    bt_hid_passkey_done();
    bt_le_set_state(BT_LE_STATE_READY);
}

static void bt_le_keyboard_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    bt_hid_keyboard_report_t report = { 0, 0, {0} };

    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    if (hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION)
        return;

    if (gatt_event_notification_get_value_length(packet) > sizeof(report))
        memcpy(&report, gatt_event_notification_get_value(packet), sizeof(report));
    else
        memcpy(&report, gatt_event_notification_get_value(packet), gatt_event_notification_get_value_length(packet));

    bt_hid_enqueue_keyboard(bt_le_input_slot(), &report);
}

static void bt_le_mouse_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    bt_hid_mouse_report_t report = { 0 };
    uint8_t const *value;
    uint16_t value_len;

    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    if (hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION)
        return;

    value = gatt_event_notification_get_value(packet);
    value_len = gatt_event_notification_get_value_length(packet);

    if (value_len > 0)
        report.buttons = value[0];
    if (value_len > 1)
        report.x = (int8_t)value[1];
    if (value_len > 2)
        report.y = (int8_t)value[2];
    if (value_len > 3)
        report.wheel = (int8_t)value[3];

    bt_hid_enqueue_mouse(bt_le_input_slot(), &report);
}

static void bt_le_gatt_client_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    gatt_client_characteristic_t characteristic;

    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    switch (bt_le_state) {
        case BT_LE_STATE_SERVICE_QUERY:
            switch (hci_event_packet_get_type(packet)) {
                case GATT_EVENT_SERVICE_QUERY_RESULT:
                    gatt_event_service_query_result_get_service(packet, &bt_le_hid_service);
                    break;

                case GATT_EVENT_QUERY_COMPLETE:
                    if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS) {
                        bt_le_disconnect_and_restart();
                        break;
                    }

                    bt_le_set_state(BT_LE_STATE_CHARACTERISTIC_QUERY);
                    gatt_client_discover_characteristics_for_service(&bt_le_gatt_client_handler,
                        bt_le_connection_handle, &bt_le_hid_service);
                    break;

                default:
                    break;
            }
            break;

        case BT_LE_STATE_CHARACTERISTIC_QUERY:
            switch (hci_event_packet_get_type(packet)) {
                case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
                    gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);

                    switch (characteristic.uuid16) {
                        case ORG_BLUETOOTH_CHARACTERISTIC_PROTOCOL_MODE:
                            bt_le_protocol_mode_characteristic = characteristic;
                            bt_le_has_protocol_mode = true;
                            break;

                        case ORG_BLUETOOTH_CHARACTERISTIC_BOOT_KEYBOARD_INPUT_REPORT:
                            bt_le_boot_keyboard_characteristic = characteristic;
                            bt_le_has_boot_keyboard = true;
                            break;

                        case ORG_BLUETOOTH_CHARACTERISTIC_BOOT_MOUSE_INPUT_REPORT:
                            bt_le_boot_mouse_characteristic = characteristic;
                            bt_le_has_boot_mouse = true;
                            break;

                        default:
                            break;
                    }
                    break;

                case GATT_EVENT_QUERY_COMPLETE:
                    if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS) {
                        bt_le_disconnect_and_restart();
                        break;
                    }

                    if (bt_le_has_boot_keyboard) {
                        bt_le_set_state(BT_LE_STATE_ENABLE_KEYBOARD);
                        gatt_client_write_client_characteristic_configuration(&bt_le_gatt_client_handler,
                            bt_le_connection_handle, &bt_le_boot_keyboard_characteristic,
                            GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
                    } else if (bt_le_has_boot_mouse) {
                        bt_le_set_state(BT_LE_STATE_ENABLE_MOUSE);
                        gatt_client_write_client_characteristic_configuration(&bt_le_gatt_client_handler,
                            bt_le_connection_handle, &bt_le_boot_mouse_characteristic,
                            GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
                    } else {
                        uint8_t status;

                        // pads (stadia and the like) have no boot reports: let
                        // the hids client find the report maps, report
                        // references and their notifications
                        bt_le_set_state(BT_LE_STATE_ENABLE_GAMEPAD);
                        status = hids_host_connect(bt_le_connection_handle, &bt_le_gamepad_handler,
                            HID_PROTOCOL_MODE_REPORT, &bt_le_hids_cid);
                        if (status != ERROR_CODE_SUCCESS) {
                            ahprintf("[btle] pad discovery could not start: 0x%02x\n", status);
                            bt_le_disconnect_and_restart();
                        }
                    }
                    break;

                default:
                    break;
            }
            break;

        case BT_LE_STATE_ENABLE_KEYBOARD:
            if (hci_event_packet_get_type(packet) != GATT_EVENT_QUERY_COMPLETE)
                break;

            if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS) {
                bt_le_disconnect_and_restart();
                break;
            }

            gatt_client_listen_for_characteristic_value_updates(&bt_le_keyboard_notifications,
                &bt_le_keyboard_notification_handler, bt_le_connection_handle, &bt_le_boot_keyboard_characteristic);

            if (bt_le_has_boot_mouse) {
                bt_le_set_state(BT_LE_STATE_ENABLE_MOUSE);
                gatt_client_write_client_characteristic_configuration(&bt_le_gatt_client_handler,
                    bt_le_connection_handle, &bt_le_boot_mouse_characteristic,
                    GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
            } else {
                bt_le_ready();
            }
            break;

        case BT_LE_STATE_ENABLE_MOUSE:
            if (hci_event_packet_get_type(packet) != GATT_EVENT_QUERY_COMPLETE)
                break;

            if (gatt_event_query_complete_get_att_status(packet) != ATT_ERROR_SUCCESS) {
                bt_le_disconnect_and_restart();
                break;
            }

            gatt_client_listen_for_characteristic_value_updates(&bt_le_mouse_notifications,
                &bt_le_mouse_notification_handler, bt_le_connection_handle, &bt_le_boot_mouse_characteristic);
            bt_le_ready();
            break;

        default:
            break;
    }
}

static void bt_le_connect_candidate(uint8_t type, bd_addr_t address)
{
    if (!bt_radio_on || (bt_le_state != BT_LE_STATE_SCANNING))
        return;

    gap_stop_scan();
    memcpy(bt_le_addr, address, sizeof(bd_addr_t));
    bt_le_addr_type = (bd_addr_type_t)type;
    bt_le_set_state(BT_LE_STATE_CONNECTING);
    if (gap_connect(bt_le_addr, bt_le_addr_type) != ERROR_CODE_SUCCESS)
        bt_le_restart_scan();
}

static void bt_le_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            // the classic handler, registered first, has seen this already
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                bt_le_start_scan();
            } else if (btstack_event_state_get_state(packet) == HCI_STATE_OFF) {
                bt_resolving = false;
                bt_hid_enqueue_disconnect(bt_le_input_slot());
                bt_le_clear_characteristics();
                bt_le_connection_handle = HCI_CON_HANDLE_INVALID;
                bt_le_set_state(BT_LE_STATE_OFF);
            }
            break;

        case GAP_EVENT_ADVERTISING_REPORT:
            if ((bt_le_state != BT_LE_STATE_SCANNING) || bt_resolving)
                break;
            if (!bt_le_adv_event_contains_hid_service(packet))
                break;

            gap_event_advertising_report_get_address(packet, bt_resolve_addr);
            bt_resolve_type = gap_event_advertising_report_get_address_type(packet);
            if (bt_pairing_allowed()) {
                bt_le_connect_candidate(bt_resolve_type, bt_resolve_addr);
            } else {
                // paired only: a known device by its public identity or a
                // private address resolved against the stored irks
                bt_resolving = true;
                if (sm_address_resolution_lookup(bt_resolve_type, bt_resolve_addr))
                    bt_resolving = false;
            }
            break;

        case HCI_EVENT_META_GAP:
            if (hci_event_gap_meta_get_subevent_code(packet) != GAP_SUBEVENT_LE_CONNECTION_COMPLETE)
                break;

            if (bt_le_state != BT_LE_STATE_CONNECTING)
                break;

            if (gap_subevent_le_connection_complete_get_status(packet) != ERROR_CODE_SUCCESS) {
                bt_le_restart_scan();
                break;
            }

            bt_le_connection_handle = gap_subevent_le_connection_complete_get_connection_handle(packet);
            bt_le_set_state(BT_LE_STATE_ENCRYPTING);
            sm_request_pairing(bt_le_connection_handle);
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE:
            if (hci_event_disconnection_complete_get_connection_handle(packet) != bt_le_connection_handle)
                break;

            bt_hid_enqueue_disconnect(bt_le_input_slot());
            bt_le_clear_characteristics();
            bt_le_restart_scan();
            break;

        default:
            break;
    }
}

static void bt_le_sm_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    bool connect_to_service = false;

    UNUSED(channel);

    if (packet_type != HCI_EVENT_PACKET)
        return;

    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED:
        case SM_EVENT_IDENTITY_RESOLVING_FAILED: {
            bd_addr_t address;

            if ((size < 11) || !bt_resolving)
                break;
            // the succeeded and failed events share their leading fields
            sm_event_identity_resolving_failed_get_address(packet, address);
            if ((sm_event_identity_resolving_failed_get_addr_type(packet) != bt_resolve_type)
                || memcmp(address, bt_resolve_addr, sizeof(address)))
                break;
            bt_resolving = false;
            if (hci_event_packet_get_type(packet) == SM_EVENT_IDENTITY_RESOLVING_SUCCEEDED)
                bt_le_connect_candidate(bt_resolve_type, bt_resolve_addr);
            break;
        }

        case SM_EVENT_JUST_WORKS_REQUEST:
            if (bt_pairing_allowed())
                sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            else
                sm_bonding_decline(sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            if (bt_pairing_allowed()) {
                ahprintf("[btle] confirm %lu\n", (unsigned long)sm_event_numeric_comparison_request_get_passkey(packet));
                bt_hid_show_passkey("conf",
                    sm_event_numeric_comparison_request_get_passkey(packet));
                sm_numeric_comparison_confirm(sm_event_numeric_comparison_request_get_handle(packet));
            } else {
                sm_bonding_decline(sm_event_numeric_comparison_request_get_handle(packet));
            }
            break;

        case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
            ahprintf("[btle] passkey %lu\n", (unsigned long)sm_event_passkey_display_number_get_passkey(packet));
            bt_hid_show_passkey("key",
                sm_event_passkey_display_number_get_passkey(packet));
            break;

        case SM_EVENT_PAIRING_COMPLETE:
            if ((sm_event_pairing_complete_get_handle(packet) != bt_le_connection_handle)
                || (bt_le_state != BT_LE_STATE_ENCRYPTING))
                break;
            if (sm_event_pairing_complete_get_status(packet) == ERROR_CODE_SUCCESS)
                connect_to_service = true;
            else
                bt_le_disconnect_and_restart();
            break;

        case SM_EVENT_REENCRYPTION_COMPLETE:
            if ((sm_event_reencryption_complete_get_handle(packet) != bt_le_connection_handle)
                || (bt_le_state != BT_LE_STATE_ENCRYPTING))
                break;
            if (sm_event_reencryption_complete_get_status(packet) == ERROR_CODE_SUCCESS)
                connect_to_service = true;
            else
                bt_le_disconnect_and_restart();
            break;

        default:
            break;
    }

    if (!connect_to_service || (bt_le_connection_handle == HCI_CON_HANDLE_INVALID))
        return;

    bt_le_set_state(BT_LE_STATE_SERVICE_QUERY);
    gatt_client_discover_primary_services_by_uuid16(&bt_le_gatt_client_handler, bt_le_connection_handle,
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE);
}

//--------------------------------------------------------------------+
// main loop
//--------------------------------------------------------------------+

static uint32_t bt_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/**
 * the player manager gave a pad its player (main loop): a dualsense shows it
 * on its leds, sent by bt_classic_leds_task()
 */
static void bt_pad_player(uint8_t slot, int8_t player)
{
    async_context_t *context;
    bt_classic_connection_t *connection;

    if ((slot < INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE)
        || (slot >= INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE + INPUT_BRIDGE_BT_CLASSIC_SLOTS) || !bt_hid_ready)
        return;

    context = cyw43_arch_async_context();
    async_context_acquire_lock_blocking(context);
    connection = &bt_classic_connections[slot - INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE];
    if (connection->in_use && (connection->pad_kind == BT_PAD_DS5)) {
        connection->player = player;
        connection->leds_pending = true;
    }
    async_context_release_lock(context);
}

/**
 * dualsense leds, once its setup is through (an output report sent whilst
 * btstack still waits for the feature report answer would cut that short)
 */
static void bt_classic_leds_task(void)
{
    async_context_t *context;
    bool any = false;

    for (unsigned slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++)
        any |= bt_classic_connections[slot].leds_pending;
    if (!any || !bt_radio_on)
        return;

    context = cyw43_arch_async_context();
    async_context_acquire_lock_blocking(context);
    for (unsigned slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++) {
        bt_classic_connection_t *connection = &bt_classic_connections[slot];
        size_t length;

        if (!connection->in_use || !connection->leds_pending || !connection->setup_done
            || (connection->pad_kind != BT_PAD_DS5))
            continue;

        // the report id goes separately; btstack keeps the pointer until sent
        length = ds5_output_bt(connection->output, sizeof(connection->output), connection->player,
            connection->output_sequence++);
        if (length && (hid_host_send_report(connection->hid_cid, connection->output[0], connection->output + 1,
                (uint16_t)(length - 1)) == ERROR_CODE_SUCCESS))
            connection->leds_pending = false;
    }
    async_context_release_lock(context);
}

void bt_hid_init(void)
{
    if (bt_hid_ready || bt_hid_init_failed)
        return;

    if (!settings_get()->bt_enabled) {
        dbgcons_bt_status("bt off");
        snprintf(bt_status_text, sizeof(bt_status_text), "off in the settings");
        return;
    }

    dbgcons_bt_status("bt init");

    for (uint8_t slot = INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE; slot < INPUT_BRIDGE_BT_SLOT_END; slot++)
        input_bridge_reset(slot);

    if (!bt_hid_queue_ready) {
        queue_init(&bt_hid_queue, sizeof(bt_hid_queue_entry_t), BT_HID_QUEUE_DEPTH);
        bt_hid_queue_ready = true;
    }

    // the radio may already be up for wi-fi
    if (!radio_init()) {
        ahprintf("[bt] cyw43 init failed\n");
        dbgcons_bt_status("bt init fail");
        snprintf(bt_status_text, sizeof(bt_status_text), "cyw43 init failed (no radio?)");
        bt_hid_init_failed = true;
        return;
    }

    l2cap_init();
    sm_init();
    // keep pairing with older hid devices after btstack's defaults changed
    sm_set_secure_connections_only_mode(false);
    sm_set_encryption_key_size_range(7, 16);
    gap_set_required_encryption_key_size(7);
    sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);
    gatt_client_init();
    hids_host_init(bt_le_descriptor_storage, sizeof(bt_le_descriptor_storage));

    hid_host_init(bt_classic_descriptor_storage, sizeof(bt_classic_descriptor_storage));
    hid_host_register_packet_handler(&bt_classic_packet_handler);

    gap_set_local_name(BT_HID_LOCAL_NAME);
    hci_set_inquiry_mode(INQUIRY_MODE_RSSI_AND_EIR);
    // discoverable follows the pairing policy once the controller is up
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE | LM_LINK_POLICY_ENABLE_ROLE_SWITCH);
    hci_set_master_slave_policy(HCI_ROLE_MASTER);
    // confirmations are answered here, under the pairing policy
    gap_ssp_set_auto_accept(0);

    bt_classic_hci_event_callback.callback = &bt_classic_packet_handler;
    hci_add_event_handler(&bt_classic_hci_event_callback);

    bt_le_hci_event_callback.callback = &bt_le_packet_handler;
    hci_add_event_handler(&bt_le_hci_event_callback);

    bt_le_sm_event_callback.callback = &bt_le_sm_packet_handler;
    sm_add_event_handler(&bt_le_sm_event_callback);

    bt_le_clear_characteristics();
    bt_hid_ready = true;
    bt_radio_on = true;
    bt_controller_off = false;
    bt_hid_update_status();

    hci_power_control(HCI_POWER_ON);
}

static bool bt_scan_pause_effective(void)
{
    return bt_scan_pause_reasons && !bt_pair_window;
}

void bt_hid_pause_scanning(uint8_t reason, bool pause)
{
    if (pause)
        bt_scan_pause_reasons |= reason;
    else
        bt_scan_pause_reasons &= (uint8_t)~reason;
}

void bt_hid_pair_window(void)
{
    bt_pair_request = true;
}

void bt_hid_forget_all(void)
{
    bt_forget_request = true;
}

static void bt_hid_forget_bonds(void)
{
    unsigned le = 0;

    gap_delete_all_link_keys();
    for (int index = 0; index < le_device_db_max_count(); index++) {
        int type;
        bd_addr_t address;

        le_device_db_info(index, &type, address, NULL);
        if (type != BD_ADDR_TYPE_UNKNOWN) {
            le_device_db_remove(index);
            le++;
        }
    }
    ahprintf("[bt] all bonds forgotten (classic link keys, %u le)\n", le);
}

/**
 * the settings and the requests: radio on / off, forgetting, the pair window,
 * whether to be discoverable. stefan reinauer's bt_hid_settings_task().
 */
static void bt_hid_settings_task(void)
{
    bool enabled = settings_get()->bt_enabled != 0;
    async_context_t *context;
    uint32_t now = bt_now_ms();
    bool expired;
    bool work;

    if (!bt_hid_ready) {
        if (enabled)
            bt_hid_init();
        if (bt_pair_request || bt_forget_request) {
            ahprintf("[bt] bluetooth is not running\n");
            bt_pair_request = bt_forget_request = false;
        }
        return;
    }

    // most passes have nothing to do: do not take the lock for those
    expired = bt_pair_window && ((int32_t)(now - bt_pair_deadline) >= 0);
    work = bt_forget_request || bt_forgetting || bt_pair_request || expired
        || (bt_radio_on && !enabled) || (!bt_radio_on && enabled && bt_controller_off)
        || (bt_classic_working && (bt_discoverable != (int8_t)bt_pairing_allowed()))
        || (bt_scan_pause_effective() != bt_scan_paused);
    if (!work)
        return;

    context = cyw43_arch_async_context();
    async_context_acquire_lock_blocking(context);

    if (bt_forget_request) {
        bt_forget_request = false;
        bt_forgetting = true;
    }

    if (bt_radio_on && (!enabled || bt_forgetting)) {
        bt_hid_queue_entry_t stale;

        ahprintf("[bt] radio off%s\n", bt_forgetting ? " to forget the bonds" : "");
        bt_radio_on = false;
        bt_pair_window = false;
        bt_classic_stop_inquiry();
        gap_stop_scan();
        // a quick off / on must not replay input queued before it
        while (bt_hid_dequeue(&stale)) { }
        hci_power_control(HCI_POWER_OFF);
        // let go of everything now, not when the links have finished closing
        for (uint8_t slot = INPUT_BRIDGE_BT_CLASSIC_SLOT_BASE; slot < INPUT_BRIDGE_BT_SLOT_END; slot++)
            bt_hid_enqueue_disconnect(slot);
        bt_hid_update_status();
    }

    if (bt_controller_off && bt_forgetting) {
        bt_hid_forget_bonds();
        bt_forgetting = false;
    }

    if (enabled && !bt_radio_on && bt_controller_off && !bt_forgetting) {
        ahprintf("[bt] radio on\n");
        bt_radio_on = true;
        bt_controller_off = false;
        bt_hid_update_status();
        hci_power_control(HCI_POWER_ON);
    }

    if (bt_scan_pause_effective() && !bt_scan_paused) {
        bt_scan_paused = true;
        bt_classic_stop_inquiry();
        if (bt_le_state == BT_LE_STATE_SCANNING) {
            gap_stop_scan();
            bt_le_set_state(BT_LE_STATE_OFF);
        }
        ahprintf("[bt] searching paused\n");
    } else if (!bt_scan_pause_effective() && bt_scan_paused) {
        bt_scan_paused = false;
        bt_classic_schedule_inquiry(1000);
        if (bt_le_state == BT_LE_STATE_OFF)
            bt_le_start_scan();
        ahprintf("[bt] searching again\n");
    }

    if (bt_pair_request) {
        bt_pair_request = false;
        if (bt_radio_on) {
            bt_pair_window = true;
            bt_pair_deadline = now + BT_PAIR_WINDOW_MS;
            ahprintf("[bt] pairing open for 2 min\n");
            bt_classic_schedule_inquiry(1);
        } else {
            ahprintf("[bt] radio is off, no pairing\n");
        }
    }

    if (bt_pair_window && ((int32_t)(now - bt_pair_deadline) >= 0)) {
        bt_pair_window = false;
        ahprintf("[bt] pairing closed\n");
    }

    if (bt_classic_working && (bt_discoverable != (int8_t)bt_pairing_allowed())) {
        bt_discoverable = (int8_t)bt_pairing_allowed();
        gap_discoverable_control((uint8_t)bt_discoverable);
    }

    async_context_release_lock(context);
}

void bt_hid_task(void)
{
    bt_hid_queue_entry_t entry;

    bt_hid_settings_task();

    if (!bt_hid_ready)
        return;

    while (bt_hid_dequeue(&entry)) {
        if (!bt_radio_on && (entry.type != BT_HID_QUEUE_DISCONNECT))
            continue;

        switch (entry.type) {
            case BT_HID_QUEUE_DISCONNECT:
                input_bridge_disconnect(entry.slot);
                pads_disconnected(entry.slot);
                break;

            case BT_HID_QUEUE_KEYBOARD:
                input_bridge_handle_keyboard_boot(entry.slot, entry.keyboard.modifier, entry.keyboard.keycode);
                break;

            case BT_HID_QUEUE_MOUSE:
                input_bridge_handle_mouse_boot(entry.slot, entry.mouse.buttons, entry.mouse.x, entry.mouse.y,
                    entry.mouse.wheel);
                break;

            case BT_HID_QUEUE_PAD_CONNECT:
                pads_connected(entry.slot, entry.connect.identity,
                    bt_pad_names[(entry.connect.kind <= BT_PAD_DS5) ? entry.connect.kind : BT_PAD_GENERIC],
                    bt_pad_player);
                break;

            case BT_HID_QUEUE_PAD:
                pads_report(entry.slot, &entry.pad);
                break;
        }
    }

    bt_classic_leds_task();
}

void bt_hid_describe(void)
{
    settings_t const *s = settings_get();
    bool paired_only = s->bt_pairing == SETTINGS_BT_PAIRING_PAIRED_ONLY;
    unsigned classic_bonds = 0, le_bonds = 0, pads = 0;
    uint32_t window_left = 0;
    struct {
        bool in_use, has_gamepad, gamepad_device;
        bt_pad_kind_t kind;
        int8_t player;
    } classic[INPUT_BRIDGE_BT_CLASSIC_SLOTS];
    bt_le_state_t le_state;
    bool le_pad, le_keyboard, le_mouse;
    async_context_t *context;

    ahprintf("bluetooth: %s\n", bt_status_text);
    if (!bt_hid_ready) {
        ahprintf("not running%s\n", s->bt_enabled ? "" : " (off in the settings)");
        return;
    }

    // a snapshot under the lock, printed after it
    context = cyw43_arch_async_context();
    async_context_acquire_lock_blocking(context);
    {
        btstack_link_key_iterator_t it;

        if (gap_link_key_iterator_init(&it)) {
            bd_addr_t address;
            link_key_t key;
            link_key_type_t type;

            while (gap_link_key_iterator_get_next(&it, address, key, &type))
                classic_bonds++;
            gap_link_key_iterator_done(&it);
        }
    }
    for (int index = 0; index < le_device_db_max_count(); index++) {
        int type;
        bd_addr_t address;

        le_device_db_info(index, &type, address, NULL);
        if (type != BD_ADDR_TYPE_UNKNOWN)
            le_bonds++;
    }
    for (unsigned slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++) {
        classic[slot].in_use = bt_classic_connections[slot].in_use;
        classic[slot].has_gamepad = bt_classic_connections[slot].has_gamepad;
        classic[slot].gamepad_device = bt_classic_connections[slot].gamepad_device;
        classic[slot].kind = bt_classic_connections[slot].pad_kind;
        classic[slot].player = bt_classic_connections[slot].player;
    }
    le_state = bt_le_state;
    le_pad = bt_le_has_gamepad;
    le_keyboard = bt_le_has_boot_keyboard;
    le_mouse = bt_le_has_boot_mouse;
    if (bt_pair_window)
        window_left = (uint32_t)(bt_pair_deadline - bt_now_ms()) / 1000u;
    async_context_release_lock(context);

    ahprintf("radio %s, pairing %s", bt_radio_on ? "on" : bt_controller_off ? "off" : "stopping",
        paired_only ? "paired devices only" : "automatic");
    if (bt_pair_window)
        ahprintf(" (pair window open, %lu s left)", (unsigned long)window_left);
    ahprintf("\nbonds: classic %u, le %u\n", classic_bonds, le_bonds);

    for (unsigned slot = 0; slot < INPUT_BRIDGE_BT_CLASSIC_SLOTS; slot++) {
        if (!classic[slot].in_use) {
            ahprintf("classic %u: -\n", slot);
        } else if (classic[slot].has_gamepad) {
            pads++;
            if ((classic[slot].kind == BT_PAD_DS5) && (classic[slot].player >= 0))
                ahprintf("classic %u: %s, leds show player %d\n", slot, bt_pad_names[classic[slot].kind],
                    classic[slot].player + 1);
            else
                ahprintf("classic %u: %s\n", slot, bt_pad_names[classic[slot].kind]);
        } else {
            ahprintf("classic %u: %s\n", slot,
                classic[slot].gamepad_device ? "controller, not decoded" : "keyboard / mouse");
        }
    }
    ahprintf("le: %s%s%s%s\n", bt_le_state_name(le_state), le_pad ? ", BT pad" : "",
        le_keyboard ? ", keyboard" : "", le_mouse ? ", mouse" : "");
    ahprintf("pads %u (players: see pads)\n", pads + (le_pad ? 1u : 0u));
}
