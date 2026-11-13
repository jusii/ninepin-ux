/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the settings page's http server; see http_server.h. lwip raw tcp: a small
 * pool of connections, each reading one request (headers, then a body kept for
 * the settings form or streamed for an upload) and sending one response,
 * copied into lwip as the send buffer frees up, before closing.
 */

#include "http_server.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/cyw43_arch.h"
#include "pico/rand.h"

#include "lwip/tcp.h"

#include "kvmd_api.h"
#include "ota.h"
#include "profiles.h"
#include "settings.h"
#include "util/output.h"
#include "web_auth.h"
#include "web_page.h"
#include "wifi.h"
#ifdef ENABLE_BLUETOOTH_HID
#include "bt_hid.h"
#endif

#define HTTP_PORT               80
#define HTTP_CONNECTIONS        4
#define HTTP_REQUEST_MAX        3072    // the request head and a settings form
#define HTTP_SMALL_MAX          2048    // a response head and a short page
#define HTTP_PAGE_MAX           20480   // the settings page
#define HTTP_POLL_INTERVAL      4       // tcp_poll ticks of 0.5 s: every 2 s
#define HTTP_IDLE_POLLS         10      // ~20 s without progress closes
#define HTTP_BOUNDARY_MAX       70      // rfc 2046
#define HTTP_PREAMBLE_MAX       1024    // multipart: up to the file's data
#define HTTP_UPLOAD_MAX         (8u * 1024u * 1024u)

typedef enum {
    CONN_FREE = 0,
    CONN_HEAD,          // reading the request head
    CONN_FORM,          // reading a settings form into the request buffer
    CONN_UPLOAD,        // streaming a body to ota_receive()
    CONN_DRAIN,         // reading a body to drop it, then the response
    CONN_API,           // reading an api request's body (kvmd_api.c)
    CONN_API_WAIT,      // an api answer comes later (a print being typed)
    CONN_SEND,          // sending the response; anything read is dropped
} conn_phase_t;

// what a CONN_FORM body is for
typedef enum {
    FORM_SETTINGS = 0,
    FORM_LOGIN,
    FORM_PASSWORD,
    FORM_LOGOUT,
    FORM_PROFILE_SAVE,  // the settings form, and its pad part as a named profile
    FORM_PROFILE_LOAD,
    FORM_PROFILE_DELETE,
    FORM_BT_PAIR,       // the settings form again, for its token only
} form_kind_t;

typedef struct {
    struct tcp_pcb *pcb;
    conn_phase_t phase;
    form_kind_t form;
    uint8_t idle_polls;

    // an api request (kvmd_api.c): where its parts are in request[]
    char const *api_method, *api_path, *api_query;
    uint32_t wait_id;                       // CONN_API_WAIT: the answer's ticket

    // the request
    char request[HTTP_REQUEST_MAX + 1];
    size_t request_len;
    size_t head_len;
    size_t header_start;                    // past the request line
    size_t content_length;
    size_t body_seen;

    // an upload: multipart framing and the bytes held back from ota_receive()
    // until it is clear they are not the closing boundary
    bool multipart;
    char boundary[HTTP_BOUNDARY_MAX + 5];   // "\r\n--" + boundary
    size_t boundary_len;
    bool in_data;                           // past the part's head
    size_t preamble;
    uint8_t match;                          // of "\r\n\r\n"
    uint8_t tail[HTTP_BOUNDARY_MAX + 16];
    size_t tail_len;
    uint32_t offset;                        // passed on so far
    bool refused;                           // ota_receive() said no
    uint32_t owed;                          // read, not yet given back to the tcp window

    // the response: the head in small[], the body there too or in page[]
    char small[HTTP_SMALL_MAX];
    char const *part[2];
    size_t part_len[2];
    unsigned part_index;
    size_t part_pos;
    int status;                             // for the drained body's response
    char const *pending_title, *pending_text;
    char pending_head[128];                 // extra response header lines
} http_conn_t;

static struct tcp_pcb *listen_pcb;
static http_conn_t conns[HTTP_CONNECTIONS];
static char page[HTTP_PAGE_MAX];
static http_conn_t *page_owner;
static http_conn_t *upload_owner;
static char token[17];
static char text[320];                      // a response's text, built before the page
static uint32_t next_wait_id = 1;

//--------------------------------------------------------------------+
// helpers
//--------------------------------------------------------------------+

char const *http_server_token(void)
{
    return token;
}

static char const *status_text(int status)
{
    switch (status) {
        case 200: return "OK";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 411: return "Length Required";
        case 413: return "Content Too Large";
        case 431: return "Request Header Fields Too Large";
        case 503: return "Service Unavailable";
        default:  return "Error";
    }
}

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/**
 * @return the decoded length, or -1
 */
static int base64_decode(char const *in, size_t in_len, char *out, size_t out_size)
{
    uint32_t bits = 0;
    int nbits = 0;
    size_t n = 0;

    for (size_t i = 0; i < in_len; i++) {
        int v;

        if (in[i] == '=')
            break;
        v = base64_value(in[i]);
        if (v < 0)
            return -1;
        bits = (bits << 6) | (uint32_t)v;
        nbits += 6;
        if (nbits >= 8) {
            nbits -= 8;
            if (n + 1 >= out_size)
                return -1;
            out[n++] = (char)((bits >> nbits) & 0xffu);
        }
    }
    out[n] = 0;
    return (int)n;
}

/**
 * http basic auth: any user name, the page password
 */
static bool authorised(char const *value, size_t length)
{
    char decoded[96];
    char const *colon;

    if (length < 6 || strncasecmp(value, "Basic ", 6) != 0)
        return false;
    value += 6;
    length -= 6;
    while (length && *value == ' ') {
        value++;
        length--;
    }
    if (base64_decode(value, length, decoded, sizeof(decoded)) < 0)
        return false;
    colon = strchr(decoded, ':');
    if (!colon)
        return false;
    colon++;
    return web_auth_password(colon, strlen(colon));
}

/**
 * @return the value of header name in the request head, and its length
 */
static char const *header_value(http_conn_t const *c, char const *name, size_t *length)
{
    size_t name_len = strlen(name);
    char const *line = c->request + c->header_start;

    // the head ends in "\r\n" and a nul where its blank line was
    while (*line) {
        char const *end = strstr(line, "\r\n");

        if (!end || end == line)
            break;
        if (((size_t)(end - line) > name_len) && strncasecmp(line, name, name_len) == 0 && line[name_len] == ':') {
            char const *value = line + name_len + 1;

            while (value < end && (*value == ' ' || *value == '\t'))
                value++;
            *length = (size_t)(end - value);
            return value;
        }
        line = end + 2;
    }
    return NULL;
}

/**
 * @return the value of name in a query string or form ("a=1&b=2"), not decoded
 */
static bool query_value(char const *query, char const *name, char *out, size_t size)
{
    size_t name_len = strlen(name);

    while (query && *query) {
        char const *end = strchr(query, '&');
        size_t len = end ? (size_t)(end - query) : strlen(query);

        if ((len > name_len) && strncmp(query, name, name_len) == 0 && query[name_len] == '=') {
            size_t value_len = len - name_len - 1;

            if (value_len >= size)
                return false;
            memcpy(out, query + name_len + 1, value_len);
            out[value_len] = 0;
            return true;
        }
        query = end ? end + 1 : NULL;
    }
    return false;
}

//--------------------------------------------------------------------+
// connections
//--------------------------------------------------------------------+

static void conn_release(http_conn_t *c)
{
    if (page_owner == c)
        page_owner = NULL;
    if (upload_owner == c)
        upload_owner = NULL;
    c->owed = 0;
    c->pcb = NULL;
    c->phase = CONN_FREE;
}

static void conn_detach(http_conn_t *c)
{
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_sent(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_poll(c->pcb, NULL, 0);
}

/**
 * @return false if the pcb is gone (aborted): do not touch it again
 */
static bool conn_close(http_conn_t *c)
{
    struct tcp_pcb *pcb = c->pcb;

    // a close with bytes read but not given back to the window is a reset
    while (c->owed) {
        u16_t step = (c->owed > 0xffffu) ? 0xffffu : (u16_t)c->owed;

        tcp_recved(pcb, step);
        c->owed -= step;
    }

    conn_detach(c);
    conn_release(c);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
        return false;
    }
    return true;
}

/**
 * queue as much of the response as lwip takes; close once it is all queued
 *
 * @return false if the connection was aborted
 */
static bool conn_send(http_conn_t *c)
{
    while (c->part_index < 2) {
        char const *data = c->part[c->part_index];
        size_t length = c->part_len[c->part_index];
        size_t room, n;
        err_t err;

        if (!data || c->part_pos >= length) {
            c->part_index++;
            c->part_pos = 0;
            continue;
        }

        room = tcp_sndbuf(c->pcb);
        if (!room || (tcp_sndqueuelen(c->pcb) >= TCP_SND_QUEUELEN - 1))
            break;

        n = length - c->part_pos;
        if (n > room)
            n = room;
        err = tcp_write(c->pcb, data + c->part_pos, (u16_t)n, TCP_WRITE_FLAG_COPY);
        if (err == ERR_MEM)
            break;      // the sent callback brings us back
        if (err != ERR_OK) {
            struct tcp_pcb *pcb = c->pcb;

            conn_detach(c);
            conn_release(c);
            tcp_abort(pcb);
            return false;
        }
        c->part_pos += n;
    }

    tcp_output(c->pcb);

    if (c->part_index >= 2) {
        // everything is copied into lwip: the page buffer is free again, and
        // the close goes out after the data
        return conn_close(c);
    }
    return true;
}

/**
 * respond: a head, then body (in small[] or page[], already filled in)
 */
static bool respond_typed(http_conn_t *c, int status, char const *type, char const *body, size_t body_len,
    char const *extra_head)
{
    // the head goes after a body in small[], or at its start for page[]
    char *head = (body == c->small) ? c->small + body_len : c->small;
    size_t room = (body == c->small) ? sizeof(c->small) - body_len : sizeof(c->small);
    int n;

    n = snprintf(head, room,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status, status_text(status), type, (unsigned)body_len, extra_head ? extra_head : "");
    if (n < 0 || (size_t)n >= room)
        n = 0;

    c->part[0] = head;
    c->part_len[0] = (size_t)n;
    c->part[1] = body;
    c->part_len[1] = body_len;
    c->part_index = 0;
    c->part_pos = 0;
    c->phase = CONN_SEND;
    return conn_send(c);
}

static bool respond_with(http_conn_t *c, int status, char const *body, size_t body_len, char const *extra_head)
{
    return respond_typed(c, status, "text/html; charset=utf-8", body, body_len, extra_head);
}

/**
 * a short page in small[]: leave room for the head after it
 */
static bool respond_message(http_conn_t *c, int status, char const *title, char const *message, char const *extra_head)
{
    size_t body_len = web_page_message(c->small, sizeof(c->small) - 320, title, message,
        extra_head && strstr(extra_head, "Refresh:"));

    return respond_with(c, status, c->small, body_len, extra_head);
}

static bool respond_page(http_conn_t *c, char const *note)
{
    size_t length;

    if (page_owner && page_owner != c)
        return respond_message(c, 503, "Busy", "Another page is on its way out; reload in a moment.", "Retry-After: 1\r\n");

    page_owner = c;
    length = web_page_render(page, sizeof(page), note);
    if (!length) {
        page_owner = NULL;
        return respond_message(c, 503, "Too big", "The page did not fit its buffer.", NULL);
    }
    return respond_with(c, 200, page, length, NULL);
}

/**
 * a body follows that we do not want: read it, drop it, then answer (an
 * answer before the body is through would end in a reset, and the browser
 * would show that instead)
 */
static bool respond_after_body(http_conn_t *c, int status, char const *title, char const *message,
    char const *extra_head)
{
    // too much to sit through: answer now, and close on whatever follows
    if ((c->body_seen >= c->content_length) || (c->content_length > HTTP_UPLOAD_MAX))
        return respond_message(c, status, title, message, extra_head);

    c->status = status;
    c->pending_title = title;
    c->pending_text = message;
    snprintf(c->pending_head, sizeof(c->pending_head), "%s", extra_head ? extra_head : "");
    c->phase = CONN_DRAIN;
    return true;
}

//--------------------------------------------------------------------+
// the settings form
//--------------------------------------------------------------------+

#define SESSION_COOKIE "Set-Cookie: auth_token=%s; Path=/; HttpOnly; SameSite=Strict\r\n"

static bool respond_login(http_conn_t *c, int status, char const *note)
{
    size_t length = web_page_login(c->small, sizeof(c->small) - 320, note);

    return respond_with(c, status, c->small, length, NULL);
}

static bool token_given(char const *body)
{
    char given[sizeof(token)];

    return web_page_field(body, "t", given, sizeof(given)) && strcmp(given, token) == 0;
}

/**
 * the log-in form: the right password opens a session and leads to the page
 */
static bool handle_login(http_conn_t *c, char const *body)
{
    char given[SETTINGS_PAGE_PASSWORD_LEN + 8];
    char head[128];
    uint32_t ip = ip_addr_get_ip4_u32(&c->pcb->remote_ip);

    if (web_auth_locked_out(ip))
        return respond_login(c, 403, "Too many wrong passwords: try again in 30 s.");

    if (!web_page_field(body, "password", given, sizeof(given)) || !web_auth_password(given, strlen(given))) {
        web_auth_result(ip, false);
        ahprintf("[web] wrong page password from %s\n", ipaddr_ntoa(&c->pcb->remote_ip));
        return respond_login(c, 403, "That is not the page password.");
    }

    web_auth_result(ip, true);
    snprintf(head, sizeof(head), "Location: /\r\n" SESSION_COOKIE, web_auth_session_open());
    return respond_message(c, 303, "Logged in", "The settings are this way.", head);
}

static bool handle_logout(http_conn_t *c, char const *body)
{
    size_t length;
    char const *cookie = header_value(c, "Cookie", &length);

    if (!token_given(body))
        return respond_message(c, 400, "Not logged out", "The page is out of date: reload it and try again.", NULL);
    if (cookie)
        web_auth_session_close(cookie, length);
    return respond_message(c, 303, "Logged out", "The settings ask for the page password again.",
        "Location: /\r\n");
}

/**
 * the page's "pair" button: what "BT pair (2 min)" does in the board's menu
 */
static bool handle_bt_pair(http_conn_t *c, char const *body)
{
    if (!token_given(body))
        return respond_message(c, 400, "Not opened", "The page is out of date: reload it and try again.", NULL);
#ifdef ENABLE_BLUETOOTH_HID
    if (!settings_get()->bt_enabled)
        return respond_message(c, 409, "Not opened", "Bluetooth is off.", NULL);
    bt_hid_pair_window();
    return respond_message(c, 200, "Pairing open", "The board searches for new Bluetooth devices for 2 minutes: "
        "put the device in its pairing mode now. Nothing on the form was saved.", NULL);
#else
    return respond_message(c, 409, "Not opened", "This firmware has no Bluetooth.", NULL);
#endif
}

/**
 * the page password form: a new password, or none asked for
 */
static bool handle_password(http_conn_t *c, char const *body)
{
    char fresh[SETTINGS_PAGE_PASSWORD_LEN + 8] = "", again[SETTINGS_PAGE_PASSWORD_LEN + 8] = "", open[4] = "";
    char head[128] = "";
    char const *fault;
    bool is_open;

    if (!token_given(body))
        return respond_message(c, 400, "Not changed", "The page is out of date: reload it and try again.", NULL);

    if (!web_page_field(body, "open", open, sizeof(open)) || (strcmp(open, "0") != 0 && strcmp(open, "1") != 0))
        return respond_message(c, 400, "Not changed", "Say whether join mode asks for the password.", NULL);
    is_open = open[0] == '1';

    // a password too long for its buffer is a fault as well
    if ((strstr(body, "new=") && !web_page_field(body, "new", fresh, sizeof(fresh)))
        || (strstr(body, "again=") && !web_page_field(body, "again", again, sizeof(again))))
        return respond_message(c, 400, "Not changed", web_auth_password_fault(""), NULL);

    if (fresh[0] || again[0]) {
        if (strcmp(fresh, again) != 0)
            return respond_message(c, 400, "Not changed", "The two passwords are not the same.", NULL);
        fault = web_auth_password_fault(fresh);
        if (fault)
            return respond_message(c, 400, "Not changed", fault, NULL);
    } else if (is_open == (settings_get()->page_open != 0)) {
        return respond_message(c, 200, "Nothing to change", "The page password stays as it was.", NULL);
    }

    if (!wifi_page_password_submit(fresh, is_open))
        return respond_message(c, 503, "Not changed", "The previous change is still being stored; try again.", NULL);

    // a new password ends every session but this browser's
    if (fresh[0]) {
        web_auth_sessions_clear();
        snprintf(head, sizeof(head), SESSION_COOKIE, web_auth_session_open());
    }
    ahprintf("[web] page password from the page\n");

    if (is_open)
        snprintf(text, sizeof(text), "The board stores that now: join mode no longer asks for the page password%s. "
            "Firmware updates wait for a BOOT press only if the Board setting says so.", fresh[0] ? " (the new one is kept for when it does)" : "");
    else
        snprintf(text, sizeof(text), "The board stores that now: join mode asks for the %spage password.",
            fresh[0] ? "new " : "");
    return respond_message(c, 200, "Changed", text, head[0] ? head : NULL);
}

/**
 * load or delete the profile the form's select names; the rest of the form is
 * not looked at
 */
static bool handle_profile(http_conn_t *c, char const *body, bool load)
{
    char value[8];
    char *end;
    unsigned long number;
    profile_t const *profile;

    if (!token_given(body))
        return respond_message(c, 400, "Not done", "The page is out of date: reload it and try again.", NULL);

    number = web_page_field(body, "profile", value, sizeof(value)) ? strtoul(value, &end, 10) : 0;
    profile = (number && !*end) ? profiles_get((unsigned)number - 1u) : NULL;
    if (!profile)
        return respond_message(c, 400, "Not done", "There is no such pad profile.", NULL);

    snprintf(text, sizeof(text), load ? "The board loads the pad profile %s into the settings now."
        : "The board deletes the pad profile %s now; the settings stay as they are.", profile->name);
    if (!wifi_page_profile_submit(load ? WIFI_PROFILE_LOAD : WIFI_PROFILE_DELETE, NULL, NULL, (unsigned)number - 1u))
        return respond_message(c, 503, "Not done", "The previous save is still being stored; try again.", NULL);

    ahprintf("[web] pad profile %s: %s\n", profile->name, load ? "load" : "delete");
    return respond_message(c, 200, load ? "Loaded" : "Deleted", text, "Refresh: 2; url=/\r\n");
}

static bool handle_form(http_conn_t *c)
{
    static settings_t submitted;    // lwip callbacks do not nest
    char *body = c->request + c->head_len;
    char error[96] = "";
    settings_t const *current = settings_get();
    char const *new_name;
    char profile_name[PROFILE_NAME_LEN + 4] = "";
    char profile_for[4] = "";
    unsigned machine = PROFILE_FOR_ANY;
    bool wifi_changed, renamed;

    body[c->content_length] = 0;

    switch (c->form) {
        case FORM_LOGIN:    return handle_login(c, body);
        case FORM_LOGOUT:   return handle_logout(c, body);
        case FORM_PASSWORD: return handle_password(c, body);
        case FORM_BT_PAIR:  return handle_bt_pair(c, body);
        case FORM_PROFILE_LOAD:     return handle_profile(c, body, true);
        case FORM_PROFILE_DELETE:   return handle_profile(c, body, false);
        default:            break;
    }

    // the profile's name, before the parser takes the body apart
    if (c->form == FORM_PROFILE_SAVE) {
        if (!web_page_field(body, "profile_name", profile_name, sizeof(profile_name)))
            profile_name[0] = 0;
        if (!profiles_name_ok(profile_name))
            return respond_message(c, 400, "Not saved", "A profile's name has 1 to 12 letters, digits, "
                "spaces and - _ . +", NULL);
        if (web_page_field(body, "profile_for", profile_for, sizeof(profile_for)) && (profile_for[0] >= '0')
            && (profile_for[0] < '0' + PROFILE_FOR_COUNT) && !profile_for[1])
            machine = (unsigned)(profile_for[0] - '0');
        else
            return respond_message(c, 400, "Not saved", "Say which machine the profile is for.", NULL);
        if ((profiles_find(profile_name) < 0) && (profiles_count() >= PROFILES_MAX))
            return respond_message(c, 400, "Not saved", "Every pad profile slot is in use: delete one, "
                "or save over one by its name.", NULL);
    }

    submitted = *current;

    if (!web_page_parse(body, &submitted, error, sizeof(error)))
        return respond_message(c, 400, "Not saved", error, NULL);

    if (!wifi_page_profile_submit((c->form == FORM_PROFILE_SAVE) ? WIFI_PROFILE_SAVE : WIFI_PROFILE_NONE,
            &submitted, profile_name, machine))
        return respond_message(c, 503, "Not saved", "The previous save is still being stored; try again.", NULL);

    renamed = strcmp(submitted.device_name, current->device_name) != 0;
    new_name = submitted.device_name[0] ? submitted.device_name : wifi_default_name();
    wifi_changed = (submitted.wifi_mode != current->wifi_mode)
        || strcmp(submitted.wifi_ssid, current->wifi_ssid) != 0
        || strcmp(submitted.wifi_password, current->wifi_password) != 0
        || (renamed && submitted.wifi_mode != SETTINGS_WIFI_OFF);

    ahprintf("[web] settings from the page\n");

    if (wifi_changed && submitted.wifi_mode == SETTINGS_WIFI_JOIN)
        snprintf(text, sizeof(text), "The board stores the settings and joins \"%s\" now; this connection ends. "
            "Find it again at http://%s.local/%s.",
            submitted.wifi_ssid, new_name,
            submitted.page_open ? "" : " and log in with the page password (board menu: Show page password)");
    else if (wifi_changed && submitted.wifi_mode == SETTINGS_WIFI_OFF)
        snprintf(text, sizeof(text), "The board stores the settings and switches Wi-Fi off now.");
    else if (wifi_changed)
        snprintf(text, sizeof(text), "The board stores the settings and restarts its access point %s now; "
            "join it again and open http://192.168.4.1/.", new_name);
    else if (c->form == FORM_PROFILE_SAVE)
        snprintf(text, sizeof(text), "The board stores and applies the settings now, and keeps their pad part "
            "as the profile %s.", profile_name);
    else
        snprintf(text, sizeof(text), "The board stores and applies the settings now.");

    // a plain save goes straight back to the settings, which show it at once
    if (!wifi_changed && (c->form == FORM_SETTINGS))
        return respond_message(c, 303, "Saved", text, "Location: /?saved\r\n");
    return respond_message(c, 200, "Saved", text, wifi_changed ? NULL : "Refresh: 2; url=/\r\n");
}

//--------------------------------------------------------------------+
// uploads
//--------------------------------------------------------------------+

static void upload_pass(http_conn_t *c, uint8_t const *data, size_t len)
{
    if (!len || c->refused)
        return;
    if (!ota_receive(c->offset, data, len)) {
        c->refused = true;
        ahprintf("[web] update refused at byte %lu\n", (unsigned long)c->offset);
        return;
    }
    c->offset += (uint32_t)len;
}

/**
 * hold back the last bytes: they may be the closing boundary
 */
static void upload_data(http_conn_t *c, uint8_t const *data, size_t len)
{
    size_t keep = c->boundary_len + 4;      // "\r\n--boundary" "--\r\n"
    size_t total = c->tail_len + len;

    if (!c->multipart) {
        upload_pass(c, data, len);
        return;
    }

    if (total > keep) {
        size_t emit = total - keep;
        size_t from_tail = (emit < c->tail_len) ? emit : c->tail_len;

        if (from_tail) {
            upload_pass(c, c->tail, from_tail);
            memmove(c->tail, c->tail + from_tail, c->tail_len - from_tail);
            c->tail_len -= from_tail;
            emit -= from_tail;
        }
        if (emit) {
            upload_pass(c, data, emit);
            data += emit;
            len -= emit;
        }
    }
    memcpy(c->tail + c->tail_len, data, len);
    c->tail_len += len;
}

/**
 * multipart: skip the first boundary line and the part's head
 *
 * @return bytes of data used up, the rest is file data
 */
static size_t upload_skip_head(http_conn_t *c, uint8_t const *data, size_t len)
{
    static char const blank[] = "\r\n\r\n";
    size_t i;

    for (i = 0; i < len && !c->in_data; i++) {
        char ch = (char)data[i];

        if (ch == blank[c->match])
            c->match++;
        else
            c->match = (ch == '\r') ? 1 : 0;
        if (c->match == 4)
            c->in_data = true;
        c->preamble++;
    }
    return i;
}

static bool upload_feed(http_conn_t *c, uint8_t const *data, size_t len)
{
    if (c->multipart && !c->in_data) {
        size_t used = upload_skip_head(c, data, len);

        data += used;
        len -= used;
        if (!c->in_data && c->preamble > HTTP_PREAMBLE_MAX) {
            c->refused = true;
            c->in_data = true;  // drop the rest
            ahprintf("[web] update: no file part found\n");
        }
    }
    if (c->in_data || !c->multipart)
        upload_data(c, data, len);
    return true;
}

static bool upload_finish(http_conn_t *c)
{
    bool complete = true;
    bool taken;
    char message[160];

    upload_owner = NULL;

    if (c->multipart) {
        uint8_t *found = NULL;

        // the closing delimiter, "\r\n--boundary", is somewhere in the tail
        for (size_t i = 0; c->in_data && i + c->boundary_len <= c->tail_len; i++) {
            if (memcmp(c->tail + i, c->boundary, c->boundary_len) == 0) {
                found = c->tail + i;
                break;
            }
        }
        if (found)
            upload_pass(c, c->tail, (size_t)(found - c->tail));
        else
            complete = false;
    }

    if (!complete && !c->refused) {
        ahprintf("[web] update: the upload ended without its closing boundary\n");
        return respond_message(c, 400, "Update failed", "The upload ended early (no closing boundary). Nothing was installed.",
            NULL);
    }
    if (!c->offset && !c->refused)
        return respond_message(c, 400, "Update failed", "The upload was empty.", NULL);

    message[0] = 0;
    taken = ota_finish(message, sizeof(message));
    ahprintf("[web] update of %lu bytes %s: %s\n", (unsigned long)c->offset, taken ? "taken" : "not taken", message);
    snprintf(text, sizeof(text), "%s", message[0] ? message : (taken ? "Update taken." : "Update not taken."));
    return respond_message(c, taken ? 200 : 400, taken ? "Update received" : "Update not installed", text, NULL);
}

static bool upload_begin(http_conn_t *c, char const *query)
{
    size_t length;
    char const *type = header_value(c, "Content-Type", &length);
    char given[sizeof(token)];

    if (!query || !query_value(query, "t", given, sizeof(given)) || strcmp(given, token) != 0)
        return respond_after_body(c, 403, "Not taken", "The page is out of date: reload it and try again.", NULL);
    if (upload_owner)
        return respond_after_body(c, 409, "Busy", "Another update is on its way in.", NULL);
    if (c->content_length > HTTP_UPLOAD_MAX)
        return respond_message(c, 413, "Too big", "That file is larger than any firmware for this board.", NULL);

    c->multipart = false;
    if (type && length >= 19 && strncasecmp(type, "multipart/form-data", 19) == 0) {
        char const *b = NULL;
        size_t blen = 0;

        for (size_t i = 19; i + 9 <= length; i++) {
            if (strncasecmp(type + i, "boundary=", 9) == 0) {
                b = type + i + 9;
                blen = length - i - 9;
                break;
            }
        }
        if (b && blen && *b == '"') {
            char const *q;

            b++;
            q = memchr(b, '"', blen - 1);
            blen = q ? (size_t)(q - b) : 0;
        } else if (b) {
            char const *semi = memchr(b, ';', blen);

            if (semi)
                blen = (size_t)(semi - b);
        }
        if (!b || !blen || blen > HTTP_BOUNDARY_MAX)
            return respond_after_body(c, 400, "Not taken", "The upload's multipart boundary is missing or too long.",
                NULL);

        memcpy(c->boundary, "\r\n--", 4);
        memcpy(c->boundary + 4, b, blen);
        c->boundary_len = blen + 4;
        c->boundary[c->boundary_len] = 0;
        c->multipart = true;
    }

    // the file's data starts after the first blank line: the boundary line
    // and the part's head come before it
    c->in_data = false;
    c->preamble = 0;
    c->match = 0;
    c->tail_len = 0;
    c->offset = 0;
    c->refused = false;
    upload_owner = c;
    c->phase = CONN_UPLOAD;
    ahprintf("[web] update: %u bytes coming%s\n", (unsigned)c->content_length, c->multipart ? " (form upload)" : "");
    return true;
}

//--------------------------------------------------------------------+
// the kvmd-compatible api (kvmd_api.c)
//--------------------------------------------------------------------+

static char const *api_header(void *ctx, char const *name, size_t *length)
{
    return header_value((http_conn_t const *)ctx, name, length);
}

static bool api_answer(http_conn_t *c, int status, size_t length, char const *extra_head)
{
    return respond_typed(c, status, "application/json", c->small, length, extra_head);
}

/**
 * the request (and its body) is in: hand it to the api
 */
static bool api_dispatch(http_conn_t *c)
{
    kvmd_request_t req;
    kvmd_response_t resp;
    char *body = c->request + c->head_len;
    size_t length;
    char const *auth = header_value(c, "Authorization", &length);

    body[c->content_length] = 0;
    memset(&req, 0, sizeof(req));
    req.method = c->api_method;
    req.path = c->api_path;
    req.query = c->api_query;
    req.body = body;
    req.body_len = c->content_length;
    req.basic_ok = auth && authorised(auth, length);
    req.header = api_header;
    req.ctx = c;
    req.remote_ip = ip_addr_get_ip4_u32(&c->pcb->remote_ip);
    req.wait_id = next_wait_id++;

    memset(&resp, 0, sizeof(resp));
    resp.json = c->small;
    resp.size = sizeof(c->small) - 320;     // the head goes after the body
    kvmd_api_handle(&req, &resp);

    if (resp.deferred) {
        c->wait_id = req.wait_id;
        c->phase = CONN_API_WAIT;
        return true;
    }
    return api_answer(c, resp.status, resp.len, resp.extra_head[0] ? resp.extra_head : NULL);
}

static bool api_begin(http_conn_t *c, char const *method, char const *path, char const *query)
{
    c->api_method = method;
    c->api_path = path;
    c->api_query = query;
    if (c->content_length > HTTP_REQUEST_MAX - c->head_len)
        return respond_after_body(c, 413, "Too big", "That request body is too large.", NULL);
    if (!c->content_length)
        return api_dispatch(c);
    c->phase = CONN_API;
    return true;
}

void http_server_api_complete(uint32_t wait_id, int status, char const *json)
{
    cyw43_arch_lwip_begin();
    for (unsigned i = 0; i < HTTP_CONNECTIONS; i++) {
        http_conn_t *c = &conns[i];
        size_t n;

        if (c->phase != CONN_API_WAIT || c->wait_id != wait_id)
            continue;
        n = strlen(json);
        if (n > sizeof(c->small) - 320)
            n = sizeof(c->small) - 320;
        memcpy(c->small, json, n);
        api_answer(c, status, n, NULL);
        break;
    }
    cyw43_arch_lwip_end();
}

//--------------------------------------------------------------------+
// requests
//--------------------------------------------------------------------+

/**
 * @return true if the request carries the page password or a session
 */
static bool request_authorised(http_conn_t const *c)
{
    size_t length;
    char const *value = header_value(c, "Authorization", &length);

    if (value && authorised(value, length))
        return true;
    value = header_value(c, "Cookie", &length);
    return value && web_auth_session(value, length);
}

/**
 * a form's body follows: keep it in the request buffer
 */
static bool form_begin(http_conn_t *c, form_kind_t form)
{
    if (c->content_length > HTTP_REQUEST_MAX - c->head_len)
        return respond_after_body(c, 413, "Too big", "That form is larger than the settings.", NULL);
    c->form = form;
    c->phase = CONN_FORM;
    return true;
}

/**
 * the head is in: route the request
 */
static bool handle_head(http_conn_t *c)
{
    char *method = c->request;
    char *target, *version, *query;
    char const *value;
    size_t length;
    bool has_length = false;
    bool is_get, is_post, login;

    // "METHOD /path?query HTTP/1.1"
    target = strchr(method, ' ');
    if (!target)
        return respond_message(c, 400, "Bad request", "That was not http.", NULL);
    *target++ = 0;
    version = strchr(target, ' ');
    if (!version || strncmp(version + 1, "HTTP/1.", 7) != 0)
        return respond_message(c, 400, "Bad request", "That was not http/1.x.", NULL);
    *version = 0;
    query = strchr(target, '?');
    if (query)
        *query++ = 0;

    if (strncmp(target, "/api/hid/events/", 16) != 0)
        ahprintf("[web] %s %s from %s\n", method, target, ipaddr_ntoa(&c->pcb->remote_ip));

    c->content_length = 0;
    value = header_value(c, "Content-Length", &length);
    if (value) {
        size_t number = 0;

        if (length == 0 || length > 9)
            return respond_message(c, 400, "Bad request", "That length is not a number.", NULL);
        for (size_t i = 0; i < length; i++) {
            if (value[i] < '0' || value[i] > '9')
                return respond_message(c, 400, "Bad request", "That length is not a number.", NULL);
            number = number * 10u + (size_t)(value[i] - '0');
        }
        c->content_length = number;
        has_length = true;
    } else if (header_value(c, "Transfer-Encoding", &length)) {
        return respond_message(c, 411, "Length required", "Send the body with a Content-Length.", NULL);
    }

    // the api does its own authentication (kvmd's headers, basic, a cookie)
    if (!strncmp(target, "/api/", 5) || !strcmp(target, "/api"))
        return api_begin(c, method, target, query);

    is_get = strcmp(method, "GET") == 0;
    is_post = strcmp(method, "POST") == 0;
    login = strcmp(target, "/login") == 0;

    if (wifi_page_needs_password() && !request_authorised(c)) {
        if (login && is_get)
            return respond_login(c, 200, NULL);
        if (login && is_post)
            return form_begin(c, FORM_LOGIN);
        if (is_get && strcmp(target, "/") == 0)
            return respond_message(c, 303, "Log in", "The settings ask for the page password.", "Location: /login\r\n");
        return respond_after_body(c, 401, "Log in first",
            "The settings ask for the page password: log in, then try again.", NULL);
    }

    // nothing to log in to
    if (login)
        return respond_after_body(c, 303, "Logged in", "The settings are this way.", "Location: /\r\n");

    if (is_get) {
        if (strcmp(target, "/") == 0)
            return respond_page(c, (query && !strcmp(query, "saved")) ? "Saved." : NULL);
        return respond_after_body(c, 404, "Not found", "There is only the settings page here.", NULL);
    }

    if (!is_post)
        return respond_after_body(c, 405, "Not allowed", "GET or POST only.", NULL);

    if (strcmp(target, "/settings") == 0)
        return form_begin(c, FORM_SETTINGS);
    if (strcmp(target, "/password") == 0)
        return form_begin(c, FORM_PASSWORD);
    if (strcmp(target, "/logout") == 0)
        return form_begin(c, FORM_LOGOUT);
    if (strcmp(target, "/profile-save") == 0)
        return form_begin(c, FORM_PROFILE_SAVE);
    if (strcmp(target, "/profile-load") == 0)
        return form_begin(c, FORM_PROFILE_LOAD);
    if (strcmp(target, "/profile-delete") == 0)
        return form_begin(c, FORM_PROFILE_DELETE);
    if (strcmp(target, "/bt-pair") == 0)
        return form_begin(c, FORM_BT_PAIR);

    if (strcmp(target, "/update") == 0) {
        if (!has_length)
            return respond_message(c, 411, "Length required", "Send the file with a Content-Length.", NULL);
        return upload_begin(c, query);
    }

    return respond_after_body(c, 404, "Not found", "There is only the settings page here.", NULL);
}

static bool in_body(http_conn_t const *c)
{
    return (c->phase == CONN_FORM) || (c->phase == CONN_UPLOAD) || (c->phase == CONN_DRAIN)
        || (c->phase == CONN_API);
}

/**
 * the body bytes so far, by phase
 *
 * @return false if the connection is gone
 */
static bool feed_body(http_conn_t *c, uint8_t const *data, size_t len)
{
    size_t want = c->content_length - c->body_seen;

    if (len > want)
        len = want;     // anything past the body is ignored
    c->body_seen += len;

    switch (c->phase) {
        case CONN_API:
            if (len)
                memmove(c->request + c->request_len, data, len);
            c->request_len += len;
            if (c->body_seen >= c->content_length)
                return api_dispatch(c);
            return true;

        case CONN_FORM:
            // the head's leftovers are already in place: memmove
            if (len)
                memmove(c->request + c->request_len, data, len);
            c->request_len += len;
            if (c->body_seen >= c->content_length)
                return handle_form(c);
            return true;

        case CONN_UPLOAD:
            upload_feed(c, data, len);
            if (c->body_seen >= c->content_length)
                return upload_finish(c);
            return true;

        case CONN_DRAIN:
            if (c->body_seen >= c->content_length)
                return respond_message(c, c->status, c->pending_title, c->pending_text,
                    c->pending_head[0] ? c->pending_head : NULL);
            return true;

        default:
            return true;
    }
}

/**
 * @return false if the connection is gone
 */
static bool feed(http_conn_t *c, uint8_t const *data, size_t len)
{
    size_t room, take, extra;
    char *end;

    if (c->phase != CONN_HEAD)
        return in_body(c) ? feed_body(c, data, len) : true;    // CONN_SEND: dropped

    room = HTTP_REQUEST_MAX - c->request_len;
    take = (len < room) ? len : room;
    memcpy(c->request + c->request_len, data, take);
    c->request_len += take;
    c->request[c->request_len] = 0;
    data += take;
    len -= take;

    end = strstr(c->request, "\r\n\r\n");
    if (!end) {
        if (c->request_len >= HTTP_REQUEST_MAX)
            return respond_message(c, 431, "Too big", "That request head is too large.", NULL);
        return true;
    }

    // the head's strings end at its blank line; bytes after it are body
    c->head_len = (size_t)(end - c->request) + 4;
    c->header_start = (size_t)(strstr(c->request, "\r\n") - c->request) + 2;
    extra = c->request_len - c->head_len;
    c->request_len = c->head_len;
    c->request[c->head_len - 2] = 0;
    c->body_seen = 0;

    if (!handle_head(c))
        return false;
    if (!in_body(c))
        return true;

    if (extra || !c->content_length) {
        if (!feed_body(c, (uint8_t const *)c->request + c->head_len, extra))
            return false;
    }
    if (len && in_body(c))
        return feed_body(c, data, len);
    return true;
}

//--------------------------------------------------------------------+
// lwip callbacks
//--------------------------------------------------------------------+

/**
 * give read bytes back to the tcp window. an upload is copied out of lwip's
 * buffers at once (holding on to them starves the radio driver of receive
 * buffers, and then it drops everything, retransmissions too); it goes no
 * faster than the flash takes it because the window only reopens as far as
 * the update side has room for whatever the sender may then send
 */
static void credit(http_conn_t *c)
{
    uint32_t give = c->owed;

    if (!give || !c->pcb)
        return;

    if ((c->phase == CONN_UPLOAD) && !c->refused) {
        // the update side is about to write flash: nothing more until then
        if (ota_hold_window())
            return;

        // the window after giving x back is TCP_WND - owed + x; that much may
        // arrive, plus the multipart tail held in the connection
        uint32_t room = (uint32_t)ota_space();
        uint32_t need = TCP_WND + sizeof(c->tail);

        if (room + c->owed <= need)
            return;
        if (room + c->owed - need < give)
            give = room + c->owed - need;
    }

    while (give) {
        u16_t step = (give > 0xffffu) ? 0xffffu : (u16_t)give;

        tcp_recved(c->pcb, step);
        c->owed -= step;
        give -= step;
    }
}

/**
 * a chunk read: copied on (to the update side, the form buffer, or nowhere)
 * and freed straight away
 *
 * @return false if the connection is gone
 */
static bool take(http_conn_t *c, struct pbuf *p)
{
    c->idle_polls = 0;
    // a close with data not yet given back would be a reset: everything read
    // is owed until credit() hands it back
    c->owed += p->tot_len;

    for (struct pbuf *q = p; q; q = q->next) {
        if (!feed(c, q->payload, q->len)) {
            pbuf_free(p);
            return false;
        }
        if (c->phase == CONN_FREE)
            break;      // closed cleanly: the pcb is no longer ours
    }

    pbuf_free(p);
    if (c->phase != CONN_FREE)
        credit(c);
    return true;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    http_conn_t *c = arg;

    if (!c) {
        if (p) {
            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (!p) {
        // the client is done (or gone) before we were
        if (c->phase == CONN_UPLOAD)
            ahprintf("[web] update: the connection closed after %lu bytes\n", (unsigned long)c->offset);
        if (c->phase == CONN_SEND)
            return ERR_OK;      // the response still goes out, then we close
        return conn_close(c) ? ERR_OK : ERR_ABRT;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    return take(c, p) ? ERR_OK : ERR_ABRT;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    http_conn_t *c = arg;

    (void)pcb;
    (void)len;
    if (!c)
        return ERR_OK;
    c->idle_polls = 0;
    if (c->phase == CONN_SEND)
        return conn_send(c) ? ERR_OK : ERR_ABRT;
    return ERR_OK;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb)
{
    http_conn_t *c = arg;

    if (!c) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    // a print being typed answers when it is done (kvmd_api.c ends it)
    if (c->phase == CONN_API_WAIT)
        c->idle_polls = 0;

    if (++c->idle_polls >= HTTP_IDLE_POLLS) {
        if (c->phase == CONN_UPLOAD)
            ahprintf("[web] update: stalled after %lu bytes, closing\n", (unsigned long)c->offset);
        conn_detach(c);
        conn_release(c);
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    if (c->phase == CONN_SEND)
        return conn_send(c) ? ERR_OK : ERR_ABRT;
    return ERR_OK;
}

static void on_error(void *arg, err_t err)
{
    http_conn_t *c = arg;

    (void)err;
    // the pcb is already freed
    if (c) {
        if (c->phase == CONN_UPLOAD)
            ahprintf("[web] update: the connection broke after %lu bytes\n", (unsigned long)c->offset);
        conn_release(c);
    }
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    http_conn_t *c = NULL;

    (void)arg;
    if (err != ERR_OK || !pcb)
        return ERR_VAL;

    for (unsigned i = 0; i < HTTP_CONNECTIONS; i++) {
        if (conns[i].phase == CONN_FREE) {
            c = &conns[i];
            break;
        }
    }
    if (!c) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    c->pcb = pcb;
    c->phase = CONN_HEAD;
    c->idle_polls = 0;
    c->request_len = 0;
    c->head_len = 0;
    c->content_length = 0;
    c->body_seen = 0;
    c->request[0] = 0;

    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_error);
    tcp_poll(pcb, on_poll, HTTP_POLL_INTERVAL);
    tcp_setprio(pcb, TCP_PRIO_MIN);
    return ERR_OK;
}

bool http_server_upload_quiet(void)
{
    bool quiet = true;

    cyw43_arch_lwip_begin();
    for (unsigned i = 0; i < HTTP_CONNECTIONS; i++) {
        http_conn_t const *c = &conns[i];

        // the sender may still send up to the window it was last told of
        if ((c->phase == CONN_UPLOAD) && !c->refused && c->pcb && c->pcb->rcv_ann_wnd)
            quiet = false;
    }
    cyw43_arch_lwip_end();
    return quiet;
}

void http_server_task(void)
{
    bool owing = false;

    for (unsigned i = 0; i < HTTP_CONNECTIONS; i++)
        if (conns[i].owed)
            owing = true;
    if (!owing)
        return;

    cyw43_arch_lwip_begin();
    for (unsigned i = 0; i < HTTP_CONNECTIONS; i++)
        if (conns[i].phase != CONN_FREE)
            credit(&conns[i]);
    cyw43_arch_lwip_end();
}

bool http_server_start(void)
{
    struct tcp_pcb *pcb;

    if (listen_pcb)
        return true;

    if (!token[0])
        snprintf(token, sizeof(token), "%08lx%08lx", (unsigned long)get_rand_32(), (unsigned long)get_rand_32());

    pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb)
        return false;
    if (tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) != ERR_OK) {
        tcp_abort(pcb);
        return false;
    }
    listen_pcb = tcp_listen_with_backlog(pcb, 2);
    if (!listen_pcb) {
        tcp_abort(pcb);
        return false;
    }
    tcp_accept(listen_pcb, on_accept);
    return true;
}
