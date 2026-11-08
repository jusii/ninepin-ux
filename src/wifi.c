/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * wi-fi on the rm2; see wifi.h. the main loop starts and stops the access
 * point or the station as the settings say, and watches them, never waiting
 * on the radio: joining is asynchronous and polled here. every call into lwip
 * from here holds the lwip lock; the cyw43 driver takes it for itself.
 */

#include "wifi.h"

#ifdef HAS_WIFI

#include <stdio.h>
#include <string.h>

#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include "lwip/apps/mdns.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"

#include "apply.h"
#include "dhcp_server.h"
#include "http_server.h"
#include "radio.h"
#include "settings_names.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/run_mode.h"
#include "web_page.h"

#define WIFI_POLL_MS            1000    // link and client checks
#define WIFI_RETRY_MS           15000   // a failed join tries again after this
#define WIFI_PASSWORD_RETRY_MS  10000
#define WIFI_PAGE_PASSWORD_LEN  10

// no 0/o, 1/l/i: read off a small oled and typed on a phone
static char const password_alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";

typedef enum {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_NO_RADIO,
    WIFI_STATE_AP,
    WIFI_STATE_AP_IDLE,         // the access point switched itself off
    WIFI_STATE_NO_SSID,         // join mode, no network set
    WIFI_STATE_JOINING,
    WIFI_STATE_JOINED,
    WIFI_STATE_JOIN_FAILED,     // waiting to try again
} wifi_state_t;

static char const *const state_names[] = {
    "off", "no radio", "access point up", "access point off (idle)", "no network set",
    "joining", "joined", "join failed, retrying",
};

static char name[14];                   // "amigahid-xxxx"
static wifi_state_t state;
static uint8_t running;                 // enum settings_wifi actually set up
static char joined_ssid[SETTINGS_WIFI_SSID_LEN];
static char joined_password[SETTINGS_WIFI_PASSWORD_LEN];
static bool ap_up, sta_up;              // interfaces set up, to take down
static bool mdns_ready;

static uint32_t last_poll;
static uint32_t join_attempt;
static int last_link = 99;              // cyw43 link status last seen
static int reported_link = 99;          // and last printed: a retry loop prints once
static char ip_text[16];

static unsigned ap_clients;
static uint32_t ap_idle_since;
static uint32_t ap_idle_generation;     // settings generation when it went idle

static uint32_t password_attempt;
static bool password_tried;

// the page's save, waiting for the main loop
static settings_t page_pending;
static volatile bool page_pending_ready;
static char page_result[64] = "-";

// what the page shows; written under the lwip lock, which the page's
// callbacks run under
static char status_text[64] = "off";

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static struct netif *sta_netif(void)
{
    return &cyw43_state.netif[CYW43_ITF_STA];
}

static struct netif *ap_netif(void)
{
    return &cyw43_state.netif[CYW43_ITF_AP];
}

//--------------------------------------------------------------------+
// what the oled and the page show
//--------------------------------------------------------------------+

static void show(void)
{
    char oled[22];
    char line[sizeof(status_text)];

    switch (state) {
        case WIFI_STATE_OFF:
            oled[0] = 0;
            snprintf(line, sizeof(line), "off");
            break;
        case WIFI_STATE_NO_RADIO:
            snprintf(oled, sizeof(oled), "wifi: no radio");
            snprintf(line, sizeof(line), "no radio");
            break;
        case WIFI_STATE_AP:
            snprintf(oled, sizeof(oled), "wifi ap %s", name);
            snprintf(line, sizeof(line), "access point %s, %u client%s", name, ap_clients, (ap_clients == 1) ? "" : "s");
            break;
        case WIFI_STATE_AP_IDLE:
            snprintf(oled, sizeof(oled), "wifi ap off: idle");
            snprintf(line, sizeof(line), "access point off after 15 min idle");
            break;
        case WIFI_STATE_NO_SSID:
            snprintf(oled, sizeof(oled), "wifi: no network set");
            snprintf(line, sizeof(line), "join mode, no network set");
            break;
        case WIFI_STATE_JOINING:
            snprintf(oled, sizeof(oled), "wifi joining");
            snprintf(line, sizeof(line), "joining %.32s", joined_ssid);
            break;
        case WIFI_STATE_JOINED:
            snprintf(oled, sizeof(oled), "wifi %s", ip_text);
            snprintf(line, sizeof(line), "joined %.32s, %s", joined_ssid, ip_text);
            break;
        case WIFI_STATE_JOIN_FAILED:
        default:
            snprintf(oled, sizeof(oled), "wifi: join failed");
            snprintf(line, sizeof(line), "could not join %.32s, retrying", joined_ssid);
            break;
    }

    dbgcons_wifi(oled);
    cyw43_arch_lwip_begin();
    memcpy(status_text, line, sizeof(status_text));
    cyw43_arch_lwip_end();
}

static void set_state(wifi_state_t new_state)
{
    state = new_state;
    show();
}

char const *wifi_status(void)
{
    return status_text;
}

char const *wifi_page_result(void)
{
    return page_result;
}

char const *wifi_name(void)
{
    return name;
}

bool wifi_page_needs_password(void)
{
    return (running == SETTINGS_WIFI_JOIN) || (settings_get()->wifi_mode == SETTINGS_WIFI_JOIN);
}

//--------------------------------------------------------------------+
// the page password
//--------------------------------------------------------------------+

bool wifi_new_page_password(void)
{
    settings_t settings = *settings_get();
    unsigned const alphabet = sizeof(password_alphabet) - 1;
    // the largest multiple of the alphabet below 2^32: no bias
    uint32_t const limit = UINT32_MAX - (UINT32_MAX % alphabet);

    memset(settings.page_password, 0, sizeof(settings.page_password));
    for (unsigned i = 0; i < WIFI_PAGE_PASSWORD_LEN; i++) {
        uint32_t r;

        do {
            r = get_rand_32();
        } while (r >= limit);
        settings.page_password[i] = password_alphabet[r % alphabet];
    }

    if (!settings_save(&settings)) {
        ahprintf("[wifi] could not store a new page password\n");
        return false;
    }
    ahprintf("[wifi] new page password stored\n");
    return true;
}

//--------------------------------------------------------------------+
// access point
//--------------------------------------------------------------------+

static void ap_start(void)
{
    uint32_t started = now_ms();
    bool dhcp_ok, http_ok;

    cyw43_arch_enable_ap_mode(name, NULL, CYW43_AUTH_OPEN);
    if (!(cyw43_state.itf_state & (1u << CYW43_ITF_AP))) {
        ahprintf("[wifi] the access point did not start\n");
        running = SETTINGS_WIFI_AP;
        set_state(WIFI_STATE_NO_RADIO);
        return;
    }

    ap_up = true;
    cyw43_arch_lwip_begin();
    dhcp_ok = dhcp_server_start(ap_netif());
    http_ok = http_server_start();
    cyw43_arch_lwip_end();

    running = SETTINGS_WIFI_AP;
    ap_clients = 0;
    ap_idle_since = now_ms();
    ahprintf("[wifi] access point %s (open) at 192.168.4.1, up in %lu ms%s%s\n", name,
        (unsigned long)(now_ms() - started), dhcp_ok ? "" : ", no dhcp server", http_ok ? "" : ", no http server");
    set_state(WIFI_STATE_AP);
}

static void ap_stop(void)
{
    cyw43_arch_lwip_begin();
    dhcp_server_stop();
    cyw43_arch_lwip_end();
    cyw43_arch_disable_ap_mode();
    ap_clients = 0;
    ap_up = false;
}

static void ap_poll(uint32_t now)
{
    uint8_t macs[6 * 8];
    int count = 8;

    cyw43_wifi_ap_get_stas(&cyw43_state, &count, macs);
    if (count < 0)
        count = 0;

    if ((unsigned)count != ap_clients) {
        ahprintf("[wifi] access point: %d client%s\n", count, (count == 1) ? "" : "s");
        ap_clients = (unsigned)count;
        show();
    }

    if (ap_clients) {
        ap_idle_since = now;
        return;
    }

    if ((uint32_t)(now - ap_idle_since) >= WIFI_AP_IDLE_MS) {
        ap_stop();
        running = SETTINGS_WIFI_OFF;
        ap_idle_generation = settings_generation();
        ahprintf("[wifi] access point off: no client for 15 min. it comes back with the next settings save "
            "or restart\n");
        set_state(WIFI_STATE_AP_IDLE);
    }
}

//--------------------------------------------------------------------+
// joining a network
//--------------------------------------------------------------------+

static char const *link_name(int link)
{
    switch (link) {
        case CYW43_LINK_DOWN:       return "link down";
        case CYW43_LINK_JOIN:       return "associated, waiting for an address";
        case CYW43_LINK_NOIP:       return "associated, no address";
        case CYW43_LINK_UP:         return "up";
        case CYW43_LINK_FAIL:       return "failed";
        case CYW43_LINK_NONET:      return "network not found";
        case CYW43_LINK_BADAUTH:    return "wrong password";
        default:                    return "?";
    }
}

static void join_attempt_now(void)
{
    char const *password = joined_password[0] ? joined_password : NULL;
    int err;

    join_attempt = now_ms();
    err = cyw43_arch_wifi_connect_async(joined_ssid, password,
        password ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);
    if (err) {
        ahprintf("[wifi] join \"%s\" did not start (%d)\n", joined_ssid, err);
        set_state(WIFI_STATE_JOIN_FAILED);
        return;
    }
    set_state(WIFI_STATE_JOINING);
}

static void sta_start(settings_t const *s)
{
    running = SETTINGS_WIFI_JOIN;
    memcpy(joined_ssid, s->wifi_ssid, sizeof(joined_ssid));
    memcpy(joined_password, s->wifi_password, sizeof(joined_password));

    if (!joined_ssid[0]) {
        ahprintf("[wifi] join mode, but no network is set: set one on the page in access point mode\n");
        set_state(WIFI_STATE_NO_SSID);
        return;
    }

    cyw43_arch_enable_sta_mode();
    if (!(cyw43_state.itf_state & (1u << CYW43_ITF_STA))) {
        ahprintf("[wifi] station mode did not start\n");
        set_state(WIFI_STATE_NO_RADIO);
        return;
    }
    sta_up = true;

    // the name for the router's dhcp and for mdns; the netif keeps the pointer
    cyw43_arch_lwip_begin();
    netif_set_hostname(sta_netif(), name);
    if (!mdns_ready) {
        mdns_resp_init();
        mdns_ready = true;
    }
    if (mdns_resp_add_netif(sta_netif(), name) == ERR_OK)
        mdns_resp_add_service(sta_netif(), name, "_http", DNSSD_PROTO_TCP, 80, NULL, NULL);
    http_server_start();
    cyw43_arch_lwip_end();

    ahprintf("[wifi] joining \"%s\" as %s\n", joined_ssid, name);
    ip_text[0] = 0;
    last_link = reported_link = 99;
    join_attempt_now();
}

static void sta_stop(void)
{
    cyw43_arch_lwip_begin();
    mdns_resp_remove_netif(sta_netif());
    cyw43_arch_lwip_end();
    cyw43_arch_disable_sta_mode();
    ip_text[0] = 0;
    sta_up = false;
}

static void sta_poll(uint32_t now)
{
    int link;

    if (state == WIFI_STATE_NO_SSID)
        return;

    cyw43_arch_lwip_begin();
    link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link == CYW43_LINK_UP)
        ip4addr_ntoa_r(netif_ip4_addr(sta_netif()), ip_text, sizeof(ip_text));
    cyw43_arch_lwip_end();

    // the address, a failure, or losing the link; a failure repeating on
    // every retry only once
    if ((link != reported_link) && ((link == CYW43_LINK_UP) || (link < 0) || (state == WIFI_STATE_JOINED))) {
        ahprintf("[wifi] \"%s\": %s%s%s\n", joined_ssid, link_name(link), (link == CYW43_LINK_UP) ? ", " : "",
            (link == CYW43_LINK_UP) ? ip_text : "");
        reported_link = link;
    }
    last_link = link;

    if (link == CYW43_LINK_UP) {
        if (state != WIFI_STATE_JOINED) {
            ahprintf("[wifi] page at http://%s/ and http://%s.local/\n", ip_text, name);
            set_state(WIFI_STATE_JOINED);
        }
        return;
    }

    if ((link == CYW43_LINK_JOIN) || (link == CYW43_LINK_NOIP)) {
        if (state != WIFI_STATE_JOINING)
            set_state(WIFI_STATE_JOINING);
        return;
    }

    // down or failed: try again a little later
    if (state != WIFI_STATE_JOIN_FAILED)
        set_state(WIFI_STATE_JOIN_FAILED);
    if ((uint32_t)(now - join_attempt) >= WIFI_RETRY_MS) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        join_attempt_now();
    }
}

//--------------------------------------------------------------------+
// following the settings
//--------------------------------------------------------------------+

static void stop_running(void)
{
    if (ap_up)
        ap_stop();
    if (sta_up)
        sta_stop();
    running = SETTINGS_WIFI_OFF;
}

static void start(settings_t const *s)
{
    if (s->wifi_mode == SETTINGS_WIFI_OFF) {
        set_state(WIFI_STATE_OFF);
        return;
    }

    // the radio may be up for bluetooth already; if not, it comes up now
    if (!radio_init()) {
        running = s->wifi_mode;
        if (state != WIFI_STATE_NO_RADIO)
            ahprintf("[wifi] no radio\n");
        set_state(WIFI_STATE_NO_RADIO);
        return;
    }

    if (s->wifi_mode == SETTINGS_WIFI_AP)
        ap_start();
    else
        sta_start(s);
}

static void follow_settings(void)
{
    settings_t const *s = settings_get();
    bool change = false;

    if (state == WIFI_STATE_NO_RADIO && running == s->wifi_mode)
        return;

    if (state == WIFI_STATE_AP_IDLE) {
        // a save brings it back, as does a change of mode
        change = (s->wifi_mode != SETTINGS_WIFI_AP) || (settings_generation() != ap_idle_generation);
        if (change && s->wifi_mode == SETTINGS_WIFI_AP)
            ahprintf("[wifi] settings saved: access point back on\n");
    } else if (s->wifi_mode != running) {
        change = true;
    } else if (running == SETTINGS_WIFI_JOIN) {
        change = strcmp(s->wifi_ssid, joined_ssid) != 0 || strcmp(s->wifi_password, joined_password) != 0;
    }

    if (!change)
        return;

    if (running != SETTINGS_WIFI_OFF || s->wifi_mode == SETTINGS_WIFI_OFF)
        ahprintf("[wifi] %s\n", (s->wifi_mode == SETTINGS_WIFI_OFF) ? "off" : "restarting for the new settings");
    stop_running();
    start(s);
}

//--------------------------------------------------------------------+
// a save from the page
//--------------------------------------------------------------------+

bool wifi_page_submit(settings_t const *settings)
{
    if (page_pending_ready)
        return false;
    page_pending = *settings;
    __dmb();
    page_pending_ready = true;
    return true;
}

static void page_save_task(void)
{
    settings_t settings;
    uint32_t interrupts;
    bool mode_changed;
    char result[sizeof(page_result)];

    if (!page_pending_ready)
        return;

    interrupts = save_and_disable_interrupts();
    settings = page_pending;
    page_pending_ready = false;
    restore_interrupts(interrupts);

    // the page never sets these; keep what the board has
    settings.ota_needs_boot = settings_get()->ota_needs_boot;
    memcpy(settings.page_password, settings_get()->page_password, sizeof(settings.page_password));

    mode_changed = settings.mode != settings_get()->mode;

    if (!settings_valid(&settings)) {
        snprintf(result, sizeof(result), "not stored: the settings were not valid");
    } else if (!settings_save(&settings)) {
        snprintf(result, sizeof(result), "not stored: the flash write failed");
    } else {
        snprintf(result, sizeof(result), "stored at %lu s after boot", (unsigned long)(now_ms() / 1000u));
        // as the menu does: the mode last, then everything else
        if (mode_changed)
            run_mode_set((run_mode_t)settings.mode);
        apply_settings();
    }
    ahprintf("[web] settings %s\n", result);

    cyw43_arch_lwip_begin();
    memcpy(page_result, result, sizeof(page_result));
    cyw43_arch_lwip_end();
}

//--------------------------------------------------------------------+
// main loop
//--------------------------------------------------------------------+

void wifi_init(void)
{
    char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    size_t length;

    pico_get_unique_board_id_string(id, sizeof(id));
    length = strlen(id);
    snprintf(name, sizeof(name), "amigahid-%.4s", (length >= 4) ? id + length - 4 : id);
    for (char *c = name; *c; c++)
        if (*c >= 'A' && *c <= 'Z')
            *c = (char)(*c - 'A' + 'a');

    web_page_init();
    state = WIFI_STATE_OFF;
    running = SETTINGS_WIFI_OFF;
    follow_settings();
}

void wifi_task(void)
{
    uint32_t now = now_ms();

    page_save_task();
    // an upload waiting for the flash to catch up
    http_server_task();

    // a page password exists from the first boot on (and again after the
    // factory defaults cleared it)
    if (!settings_get()->page_password[0]
        && (!password_tried || (uint32_t)(now - password_attempt) >= WIFI_PASSWORD_RETRY_MS)) {
        password_tried = true;
        password_attempt = now;
        wifi_new_page_password();
    }

    follow_settings();

    if ((uint32_t)(now - last_poll) < WIFI_POLL_MS)
        return;
    last_poll = now;

    if (state == WIFI_STATE_AP)
        ap_poll(now);
    else if (running == SETTINGS_WIFI_JOIN && state != WIFI_STATE_NO_RADIO)
        sta_poll(now);
}

void wifi_describe(void)
{
    settings_t const *s = settings_get();
    uint32_t now = now_ms();

    ahprintf("wifi: setting %s, state %s\n", (s->wifi_mode < SETTINGS_WIFI_COUNT)
        ? settings_names_wifi.names[s->wifi_mode] : "?", state_names[state]);
    ahprintf("name %s, page %s\n", name, wifi_page_needs_password() ? "needs the page password" : "open");

    if (state == WIFI_STATE_AP) {
        unsigned leases;

        cyw43_arch_lwip_begin();
        leases = dhcp_server_leases();
        cyw43_arch_lwip_end();
        ahprintf("access point \"%s\" (open), 192.168.4.1, clients %u, dhcp leases %u\n", name, ap_clients, leases);
        if (ap_clients)
            ahprintf("idle timer: a client is on\n");
        else
            ahprintf("idle timer: %lu s without a client, off at %lu s\n",
                (unsigned long)((now - ap_idle_since) / 1000u), (unsigned long)(WIFI_AP_IDLE_MS / 1000u));
    }

    ahprintf("join network: ssid \"%s\", password %s\n", s->wifi_ssid, s->wifi_password[0] ? "set" : "none (open)");
    if (running == SETTINGS_WIFI_JOIN && state != WIFI_STATE_NO_SSID && state != WIFI_STATE_NO_RADIO)
        ahprintf("link: %s%s%s\n", link_name(last_link), ip_text[0] ? ", ip " : "", ip_text);

    ahprintf("page password: %s\n", s->page_password[0] ? s->page_password : "(none yet)");
    ahprintf("last save from the page: %s\n", page_result);
}

#endif // HAS_WIFI
