/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * network keyboard and mouse, a kvmd api subset; see kvmd_api.h.
 *
 * requests are validated in the network context and become events on a
 * queue; the main loop applies them through the input bridge at the pace the
 * output takes them: one keyboard change per delivered report (usb-c) or per
 * drained amiga queue, so fast clients never lose a key.
 */

#include "kvmd_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "hardware/sync.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "build_stamp.h"
#include "input_bridge_bt.h"
#include "kvmd_keys.h"
#include "menu.h"
#include "platform/amiga/keyboard_serial_io.h"
#include "platform/amiga/quad_mouse.h"
#include "settings.h"
#include "usb_device.h"
#include "util/debug_cons.h"
#include "util/output.h"
#include "util/run_mode.h"
#include "wifi.h"

#define KVMD_QUEUE              64
#define KVMD_PRINT_MAX          2048    // bytes of text one print may carry
#define KVMD_PRINT_LIMIT        1024    // characters, kvmd's default limit
#define KVMD_HOLD_RELEASE_MS    60000u  // held input released without requests
#define KVMD_ACTIVE_MS          10000u  // "net ctl" on the oled after input
#define KVMD_TOKENS             4
#define KVMD_LOCKOUT_SLOTS      8
#define KVMD_LOCKOUT_FAILS      5
#define KVMD_LOCKOUT_MS         30000u
#define KVMD_IDLE_CHECKS        2       // output idle seen this many passes in a row
#define KVMD_SLOW_DELAY_MS      20
#define KVMD_BUTTON_MS          50      // shortest press or release of a mouse button
// the amiga acknowledges every key code but drops events when they come faster
// than amigaos takes them (measured on an a4000 with a busy rtg screen: full
// speed lost keys and key-ups, 30 ms a character none in six lines)
#define KVMD_AMIGA_CHAR_MS      30
#define KVMD_AMIGA_KEY_MS       15

typedef enum {
    EV_KEY = 1,         // a: usage, b: down
    EV_BUTTON,          // a: button bit, b: down
    EV_REL,             // a: dx, b: dy
    EV_WHEEL,           // a: vertical
    EV_ABS,             // a: x, b: y (-32768..32767)
    EV_PRINT,           // the print buffer
    EV_RESET,
} ev_type_t;

typedef struct {
    uint8_t type;
    int32_t a, b;
} kvmd_event_t;

// network context writes head, the main loop tail: one producer, one consumer
static kvmd_event_t queue[KVMD_QUEUE];
static volatile uint32_t q_head, q_tail;

// one print at a time
static char print_text[KVMD_PRINT_MAX + 1];
static volatile bool print_busy;
static size_t print_len, print_pos;
static char print_keymap[8];
static uint32_t print_delay_ms;
static uint32_t print_wait_id;
static unsigned print_skipped;

static volatile uint32_t last_request_ms;
static volatile uint32_t last_input_ms;
static char tokens[KVMD_TOKENS][17];
static char mouse_output[12] = "usb";

typedef struct {
    uint32_t ip;
    uint8_t fails;
    uint32_t until;
} lockout_t;
static lockout_t lockouts[KVMD_LOCKOUT_SLOTS];

// main loop state: what the network side holds
static uint8_t held_mod;
static uint8_t held_keys[6];
static uint8_t held_buttons;
static bool shown_active;

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

//--------------------------------------------------------------------+
// json
//--------------------------------------------------------------------+

static size_t json_ok(char *out, size_t size, char const *result)
{
    int n = snprintf(out, size, "{\"ok\": true, \"result\": %s}", result ? result : "{}");

    return (n > 0 && (size_t)n < size) ? (size_t)n : 0;
}

size_t kvmd_api_error(char *out, size_t size, char const *error, char const *message)
{
    int n = snprintf(out, size, "{\"ok\": false, \"result\": {\"error\": \"%s\", \"error_msg\": \"%s\"}}",
        error, message);

    return (n > 0 && (size_t)n < size) ? (size_t)n : 0;
}

static void reply(kvmd_response_t *resp, int status, char const *result)
{
    resp->status = status;
    resp->len = json_ok(resp->json, resp->size, result);
}

static void fail(kvmd_response_t *resp, int status, char const *error, char const *message)
{
    resp->status = status;
    resp->len = kvmd_api_error(resp->json, resp->size, error, message);
}

//--------------------------------------------------------------------+
// parameters
//--------------------------------------------------------------------+

static bool param(char const *query, char const *name, char *out, size_t size)
{
    size_t name_len = strlen(name);

    while (query && *query) {
        char const *end = strchr(query, '&');
        size_t len = end ? (size_t)(end - query) : strlen(query);

        if ((len > name_len) && !strncmp(query, name, name_len) && query[name_len] == '=') {
            size_t o = 0;

            for (size_t i = name_len + 1; i < len && o + 1 < size; i++) {
                char ch = query[i];

                if (ch == '+') {
                    ch = ' ';
                } else if (ch == '%' && i + 2 < len) {
                    char hex[3] = { query[i + 1], query[i + 2], 0 };

                    ch = (char)strtol(hex, NULL, 16);
                    i += 2;
                }
                out[o++] = ch;
            }
            out[o] = 0;
            return true;
        }
        query = end ? end + 1 : NULL;
    }
    return false;
}

/**
 * @return 1 true, 0 false, -1 missing or not a boolean
 */
static int param_bool(char const *query, char const *name)
{
    char v[8];

    if (!param(query, name, v, sizeof(v)))
        return -1;
    if (!strcasecmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes"))
        return 1;
    if (!strcasecmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "no"))
        return 0;
    return -1;
}

static bool param_int(char const *query, char const *name, long *value)
{
    char v[16], *end;

    if (!param(query, name, v, sizeof(v)) || !v[0])
        return false;
    *value = strtol(v, &end, 10);
    return *end == 0;
}

static long clamp(long v, long lo, long hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

//--------------------------------------------------------------------+
// authentication
//--------------------------------------------------------------------+

static bool same(char const *a, size_t a_len, char const *b)
{
    size_t b_len = strlen(b);
    uint8_t diff = 0;

    for (size_t i = 0; i < b_len; i++)
        diff |= (uint8_t)(b[i] ^ ((i < a_len) ? a[i] : 0));
    return !diff && (a_len == b_len);
}

static lockout_t *lockout_for(uint32_t ip, bool create)
{
    lockout_t *spare = NULL;

    for (unsigned i = 0; i < KVMD_LOCKOUT_SLOTS; i++) {
        if (lockouts[i].ip == ip)
            return &lockouts[i];
        if (!spare && !lockouts[i].fails && (int32_t)(now_ms() - lockouts[i].until) >= 0)
            spare = &lockouts[i];
    }
    if (create && spare) {
        memset(spare, 0, sizeof(*spare));
        spare->ip = ip;
        return spare;
    }
    return NULL;
}

static bool locked_out(uint32_t ip)
{
    lockout_t *l = lockout_for(ip, false);

    return l && l->until && (int32_t)(now_ms() - l->until) < 0;
}

static void auth_result(uint32_t ip, bool ok)
{
    lockout_t *l = lockout_for(ip, !ok);

    if (!l)
        return;
    if (ok) {
        l->fails = 0;
        return;
    }
    if (++l->fails >= KVMD_LOCKOUT_FAILS) {
        l->fails = 0;
        l->until = now_ms() + KVMD_LOCKOUT_MS;
        ahprintf("[api] too many wrong passwords from %lu.%lu.%lu.%lu: locked out for 30 s\n",
            (unsigned long)(ip & 0xff), (unsigned long)((ip >> 8) & 0xff),
            (unsigned long)((ip >> 16) & 0xff), (unsigned long)(ip >> 24));
    }
}

static bool cookie_token(kvmd_request_t const *req)
{
    size_t len;
    char const *cookie = req->header(req->ctx, "Cookie", &len);

    while (cookie && len) {
        char const *p = memchr(cookie, '=', len);
        char const *end = memchr(cookie, ';', len);
        size_t vlen;

        if (!p)
            break;
        if (!end)
            end = cookie + len;
        while (*cookie == ' ')
            cookie++;
        vlen = (size_t)(end - p - 1);
        if ((size_t)(p - cookie) == 10 && !strncmp(cookie, "auth_token", 10))
            for (unsigned i = 0; i < KVMD_TOKENS; i++)
                if (tokens[i][0] && same(p + 1, vlen, tokens[i]))
                    return true;
        if (end >= cookie + len)
            break;
        len -= (size_t)(end + 1 - cookie);
        cookie = end + 1;
    }
    return false;
}

/**
 * @return 1 authenticated, 0 credentials missing, -1 wrong
 */
static int authenticate(kvmd_request_t const *req)
{
    char const *password = settings_get()->page_password;
    size_t len;
    char const *given = req->header(req->ctx, "X-KVMD-Passwd", &len);

    if (!*password)
        return -1;
    if (req->basic_ok || cookie_token(req))
        return 1;
    if (given)
        return same(given, len, password) ? 1 : -1;
    if (req->header(req->ctx, "Authorization", &len))
        return -1;
    return 0;
}

//--------------------------------------------------------------------+
// the queue (network side)
//--------------------------------------------------------------------+

static bool push(uint8_t type, int32_t a, int32_t b)
{
    uint32_t next = (q_head + 1) % KVMD_QUEUE;

    if (next == q_tail)
        return false;
    queue[q_head].type = type;
    queue[q_head].a = a;
    queue[q_head].b = b;
    __dmb();
    q_head = next;
    return true;
}

static size_t queue_free(void)
{
    return KVMD_QUEUE - 1 - ((q_head + KVMD_QUEUE - q_tail) % KVMD_QUEUE);
}

static bool mouse_absolute(void)
{
    // the de9 ports only know relative motion
    return run_mode_mouse_usb(run_mode_get()) && strcmp(mouse_output, "usb_rel") != 0;
}

//--------------------------------------------------------------------+
// requests
//--------------------------------------------------------------------+

static void hid_state(kvmd_response_t *resp)
{
    char result[512];

    snprintf(result, sizeof(result),
        "{\"online\": true, \"busy\": %s, \"connected\": true, "
        "\"keyboard\": {\"online\": true, \"leds\": {\"caps\": %s, \"scroll\": false, \"num\": false}, "
        "\"outputs\": {\"available\": [], \"active\": \"\"}}, "
        "\"mouse\": {\"online\": true, \"absolute\": %s, "
        "\"outputs\": {\"available\": [\"usb\", \"usb_rel\"], \"active\": \"%s\"}}, "
        "\"jiggler\": {\"enabled\": false, \"active\": false, \"interval\": 0}}",
        print_busy ? "true" : "false", amiga_caps_lock() ? "true" : "false",
        mouse_absolute() ? "true" : "false", mouse_output);
    reply(resp, 200, result);
}

static void info(kvmd_response_t *resp)
{
    char result[400];

    snprintf(result, sizeof(result),
        "{\"meta\": {\"server\": {\"host\": \"%s\"}, \"kvm\": {}}, "
        "\"system\": {\"kvmd\": {\"version\": \"amigahid-pico %s\"}, "
        "\"platform\": {\"type\": \"amigahid-pico\", \"base\": \"rp2350\"}}, \"hw\": {}, \"extras\": {}}",
        wifi_name(), ahp_build_stamp);
    reply(resp, 200, result);
}

static void keymaps(kvmd_response_t *resp)
{
    char result[200];
    size_t n;
    char const *const *names = kvmd_keymaps();

    n = (size_t)snprintf(result, sizeof(result), "{\"keymaps\": {\"default\": \"%s\", \"available\": [", names[0]);
    for (unsigned i = 0; names[i] && n < sizeof(result); i++)
        n += (size_t)snprintf(result + n, sizeof(result) - n, "%s\"%s\"", i ? ", " : "", names[i]);
    if (n < sizeof(result))
        snprintf(result + n, sizeof(result) - n, "]}}");
    reply(resp, 200, result);
}

static void login(kvmd_request_t const *req, kvmd_response_t *resp)
{
    char given[64];
    char const *password = settings_get()->page_password;
    unsigned slot = 0;

    if (!param(req->body, "passwd", given, sizeof(given)) || !*password || !same(given, strlen(given), password)) {
        auth_result(req->remote_ip, false);
        fail(resp, 403, "ForbiddenError", "wrong credentials");
        return;
    }
    auth_result(req->remote_ip, true);
    for (unsigned i = 0; i < KVMD_TOKENS; i++)
        if (!tokens[i][0]) {
            slot = i;
            break;
        }
    if (tokens[slot][0])
        slot = get_rand_32() % KVMD_TOKENS;
    snprintf(tokens[slot], sizeof(tokens[slot]), "%08lx%08lx", (unsigned long)get_rand_32(),
        (unsigned long)get_rand_32());
    snprintf(resp->extra_head, sizeof(resp->extra_head),
        "Set-Cookie: auth_token=%s; Path=/; HttpOnly; SameSite=Strict\r\n", tokens[slot]);
    reply(resp, 200, NULL);
}

static void logout(kvmd_request_t const *req, kvmd_response_t *resp)
{
    size_t len;
    char const *cookie = req->header(req->ctx, "Cookie", &len);

    for (unsigned i = 0; i < KVMD_TOKENS; i++) {
        size_t t = strlen(tokens[i]);
        char const *p;

        if (!t || !cookie)
            continue;
        p = strstr(cookie, tokens[i]);
        if (p && (size_t)(p - cookie) + t <= len)
            tokens[i][0] = 0;
    }
    reply(resp, 200, NULL);
}

static bool send_key(char const *name, int state, bool finish, kvmd_response_t *resp)
{
    uint8_t usage = kvmd_key_usage(name);

    if (!usage) {
        fail(resp, 400, "ValidatorError", "unknown key name");
        return false;
    }
    if (queue_free() < 2) {
        fail(resp, 503, "BusyError", "the input queue is full; slow down");
        return false;
    }
    if (state != 0)
        push(EV_KEY, usage, 1);
    // released at once: state false, a missing state, or finish on a non-modifier
    if (state <= 0 || (finish && usage < 0xe0))
        push(EV_KEY, usage, 0);
    return true;
}

static void events(kvmd_request_t const *req, char const *what, kvmd_response_t *resp)
{
    char name[24];
    long x, y;

    if (!strcmp(what, "send_key")) {
        if (!param(req->query, "key", name, sizeof(name))) {
            fail(resp, 400, "ValidatorError", "key is required");
            return;
        }
        if (send_key(name, param_bool(req->query, "state"), param_bool(req->query, "finish") == 1, resp))
            reply(resp, 200, NULL);
        return;
    }

    if (!strcmp(what, "send_shortcut")) {
        char keys[160];
        uint8_t usages[8];
        unsigned count = 0;
        char *save = NULL;

        if (!param(req->query, "keys", keys, sizeof(keys))) {
            fail(resp, 400, "ValidatorError", "keys is required");
            return;
        }
        for (char *k = strtok_r(keys, ",", &save); k && count < 8; k = strtok_r(NULL, ",", &save)) {
            usages[count] = kvmd_key_usage(k);
            if (!usages[count]) {
                fail(resp, 400, "ValidatorError", "unknown key name");
                return;
            }
            count++;
        }
        if (queue_free() < 2 * count) {
            fail(resp, 503, "BusyError", "the input queue is full; slow down");
            return;
        }
        for (unsigned i = 0; i < count; i++)
            push(EV_KEY, usages[i], 1);
        for (unsigned i = count; i-- > 0;)
            push(EV_KEY, usages[i], 0);
        reply(resp, 200, NULL);
        return;
    }

    if (!strcmp(what, "send_mouse_button")) {
        static char const *const names[] = { "left", "right", "middle", "up", "down" };
        int state = param_bool(req->query, "state");
        unsigned bit = 0;

        if (param(req->query, "button", name, sizeof(name)))
            for (unsigned i = 0; i < 5; i++)
                if (!strcmp(name, names[i]))
                    bit = 1u << i;
        if (!bit) {
            fail(resp, 400, "ValidatorError", "button is left, right, middle, up or down");
            return;
        }
        if (queue_free() < 2) {
            fail(resp, 503, "BusyError", "the input queue is full; slow down");
            return;
        }
        if (state != 0)
            push(EV_BUTTON, (int32_t)bit, 1);
        if (state <= 0)
            push(EV_BUTTON, (int32_t)bit, 0);
        reply(resp, 200, NULL);
        return;
    }

    if (!strcmp(what, "send_mouse_move") || !strcmp(what, "send_mouse_relative")
        || !strcmp(what, "send_mouse_wheel")) {
        bool move = !strcmp(what, "send_mouse_move");
        bool ok = move ? (param_int(req->query, "to_x", &x) && param_int(req->query, "to_y", &y))
                       : (param_int(req->query, "delta_x", &x) && param_int(req->query, "delta_y", &y));

        if (!ok) {
            fail(resp, 400, "ValidatorError", move ? "to_x and to_y are required" : "delta_x and delta_y are required");
            return;
        }
        if (!queue_free()) {
            fail(resp, 503, "BusyError", "the input queue is full; slow down");
            return;
        }
        if (move)
            push(EV_ABS, clamp(x, -32768, 32767), clamp(y, -32768, 32767));
        else if (!strcmp(what, "send_mouse_relative"))
            push(EV_REL, clamp(x, -127, 127), clamp(y, -127, 127));
        else
            push(EV_WHEEL, clamp(y, -127, 127), 0);
        reply(resp, 200, NULL);
        return;
    }

    fail(resp, 404, "NotFoundError", "no such event");
}

static void print(kvmd_request_t const *req, kvmd_response_t *resp)
{
    char keymap[8] = "";
    long limit = KVMD_PRINT_LIMIT, delay_ms = 0;
    char v[12];
    size_t len = req->body_len, chars = 0, i = 0;
    uint32_t cp;

    if (param(req->query, "keymap", keymap, sizeof(keymap)) && !kvmd_keymap_known(keymap)) {
        fail(resp, 400, "ValidatorError", "unknown keymap");
        return;
    }
    if (!keymap[0])
        snprintf(keymap, sizeof(keymap), "%s", kvmd_keymaps()[0]);
    param_int(req->query, "limit", &limit);
    if (param_bool(req->query, "slow") == 1)
        delay_ms = KVMD_SLOW_DELAY_MS;
    if (param(req->query, "delay", v, sizeof(v)))
        delay_ms = clamp((long)(strtof(v, NULL) * 1000.0f), 0, 5000);
    if (print_busy || !queue_free()) {
        fail(resp, 503, "BusyError", "a print is still being typed");
        return;
    }
    if (len > KVMD_PRINT_MAX) {
        fail(resp, 413, "ValidatorError", "the text is too long");
        return;
    }
    // limit counts characters (0: none)
    while (i < len) {
        size_t n = kvmd_utf8_next(req->body + i, len - i, &cp);

        if (limit > 0 && (long)chars >= limit)
            break;
        i += n;
        chars++;
    }
    memcpy(print_text, req->body, i);
    print_text[i] = 0;
    print_len = i;
    print_pos = 0;
    print_skipped = 0;
    snprintf(print_keymap, sizeof(print_keymap), "%s", keymap);
    print_delay_ms = (uint32_t)delay_ms;
    print_wait_id = req->wait_id;
    print_busy = true;
    push(EV_PRINT, 0, 0);
    resp->deferred = true;
}

void kvmd_api_handle(kvmd_request_t const *req, kvmd_response_t *resp)
{
    settings_t const *s = settings_get();
    char const *path = req->path;
    bool post = !strcmp(req->method, "POST");
    int auth;

    resp->status = 500;
    last_request_ms = now_ms();

    if (s->wifi_mode == SETTINGS_WIFI_AP) {
        fail(resp, 403, "ForbiddenError", "network control is not available in access point mode");
        return;
    }
    if (!s->net_control) {
        fail(resp, 503, "UnavailableError", "network control is off on the board");
        return;
    }
    if (locked_out(req->remote_ip)) {
        fail(resp, 403, "ForbiddenError", "too many wrong passwords; try again in 30 s");
        return;
    }

    if (!strcmp(path, "/api/auth/login")) {
        if (!post) {
            fail(resp, 405, "MethodError", "use POST");
            return;
        }
        login(req, resp);
        return;
    }

    auth = authenticate(req);
    if (auth != 1) {
        if (auth < 0)
            auth_result(req->remote_ip, false);
        fail(resp, auth < 0 ? 403 : 401, auth < 0 ? "ForbiddenError" : "UnauthorizedError",
            auth < 0 ? "wrong credentials" : "credentials required");
        return;
    }
    auth_result(req->remote_ip, true);

    if (!strcmp(path, "/api/auth/check")) { reply(resp, 200, NULL); return; }
    if (!strcmp(path, "/api/auth/logout")) { logout(req, resp); return; }
    if (!strcmp(path, "/api/info")) { info(resp); return; }
    if (!strcmp(path, "/api/hid")) { hid_state(resp); return; }
    if (!strcmp(path, "/api/hid/keymaps")) { keymaps(resp); return; }
    if (!strncmp(path, "/api/streamer", 13)) {
        fail(resp, 503, "UnavailableError", "this device has no video; use a capture device for the screen");
        return;
    }

    if (strncmp(path, "/api/hid/", 9) != 0) {
        fail(resp, 404, "NotFoundError", "not provided by this device");
        return;
    }
    if (!post) {
        fail(resp, 405, "MethodError", "use POST");
        return;
    }
    path += 9;

    if (!strcmp(path, "set_connected") || !strcmp(path, "set_params")) {
        char out[12];

        if (param(req->query, "mouse_output", out, sizeof(out))) {
            if (strcmp(out, "usb") && strcmp(out, "usb_rel") && strcmp(out, "usb_win98") && strcmp(out, "ps2")
                && strcmp(out, "disabled")) {
                fail(resp, 400, "ValidatorError", "unknown mouse output");
                return;
            }
            snprintf(mouse_output, sizeof(mouse_output), "%s", out);
        }
        reply(resp, 200, NULL);
        return;
    }
    if (!strcmp(path, "reset")) {
        push(EV_RESET, 0, 0);
        reply(resp, 200, NULL);
        return;
    }

    if (menu_is_open()) {
        fail(resp, 503, "BusyError", "the board's menu is open");
        return;
    }
    last_input_ms = now_ms();

    if (!strcmp(path, "print")) {
        print(req, resp);
        return;
    }
    if (!strncmp(path, "events/", 7)) {
        events(req, path + 7, resp);
        return;
    }
    fail(resp, 404, "NotFoundError", "no such hid call");
}

//--------------------------------------------------------------------+
// main loop: events to the outputs
//--------------------------------------------------------------------+

typedef enum {
    STEP_NONE = 0,
    STEP_PRINT_PRESS,       // print: the next character's modifiers (or key)
    STEP_PRINT_KEY,         // its key, with the modifiers held
    STEP_PRINT_RELEASE,     // the key up
    STEP_PRINT_MOD_UP,      // the modifiers up
    STEP_PRINT_SPACE,       // after a dead key
    STEP_PRINT_SPACE_UP,
    STEP_PRINT_GAP,
    STEP_HOME,              // absolute on de9: pushed into the corner, moving to the target
} step_t;

static step_t step;
static uint32_t button_hold_until;      // a button change stays at least this long
static uint32_t key_gap_until;          // the next key event to the amiga not before
static uint32_t mouse_idle_since;       // mouse_settled()
static bool mouse_was_idle;
static uint32_t gap_until;
static unsigned idle_seen;
static int32_t home_x, home_y;          // the target, in counts from the corner
static kvmd_stroke_t stroke;
static uint8_t stroke_mod;

static bool keyboard_to_amiga(void)
{
    return !run_mode_keyboard_usb(run_mode_get());
}

static bool keyboard_idle(void)
{
    if (!keyboard_to_amiga())
        return usb_device_keyboard_idle();
    return amiga_keyboard_idle() && (int32_t)(now_ms() - key_gap_until) >= 0;
}

/**
 * @return true once the output has been idle on KVMD_IDLE_CHECKS passes
 */
static bool settled(bool idle)
{
    if (!idle) {
        idle_seen = 0;
        return false;
    }
    return ++idle_seen >= KVMD_IDLE_CHECKS;
}

static void keyboard_out(uint8_t extra_mod, uint8_t extra_key)
{
    uint8_t keys[6];
    unsigned n = 0;

    memset(keys, 0, sizeof(keys));
    for (unsigned i = 0; i < 6; i++)
        if (held_keys[i])
            keys[n++] = held_keys[i];
    if (extra_key && n < 6)
        keys[n] = extra_key;
    input_bridge_handle_keyboard_boot(INPUT_BRIDGE_NET_SLOT, (uint8_t)(held_mod | extra_mod), keys);
    idle_seen = 0;
}

static void mouse_out(int32_t dx, int32_t dy, int32_t wheel)
{
    do {
        int8_t x = (int8_t)clamp(dx, -127, 127), y = (int8_t)clamp(dy, -127, 127);
        int8_t w = (int8_t)clamp(wheel, -127, 127);

        input_bridge_handle_mouse_boot(INPUT_BRIDGE_NET_SLOT, held_buttons, x, y, w);
        dx -= x;
        dy -= y;
        wheel -= w;
    } while (dx || dy || wheel);
}

static void key_event(uint8_t usage, bool down)
{
    if (usage >= 0xe0) {
        uint8_t bit = (uint8_t)(1u << (usage - 0xe0));

        held_mod = down ? (uint8_t)(held_mod | bit) : (uint8_t)(held_mod & ~bit);
    } else {
        int at = -1;

        for (int i = 0; i < 6; i++)
            if (held_keys[i] == usage)
                at = i;
        if (down && at < 0) {
            for (int i = 0; i < 6; i++)
                if (!held_keys[i]) {
                    held_keys[i] = usage;
                    break;
                }
        } else if (!down && at >= 0) {
            held_keys[at] = 0;
        }
    }
    keyboard_out(0, 0);
    // single key events to the amiga keep a gap (print paces per character)
    if (keyboard_to_amiga())
        key_gap_until = now_ms() + KVMD_AMIGA_KEY_MS;
}

static void release_all(void)
{
    held_mod = 0;
    memset(held_keys, 0, sizeof(held_keys));
    held_buttons = 0;
    keyboard_out(0, 0);
    mouse_out(0, 0, 0);
}

static void print_finish(int status, char const *error)
{
    char json[160];

    if (error)
        kvmd_api_error(json, sizeof(json), "HidError", error);
    else
        snprintf(json, sizeof(json), "{\"ok\": true, \"result\": {\"skipped\": %u}}", print_skipped);
    step = STEP_NONE;
    print_busy = false;
    http_server_api_complete(print_wait_id, status, json);
}

static void abs_move(int32_t x, int32_t y)
{
    static uint16_t const screens[SETTINGS_NET_SCREEN_COUNT][2] = {
        { 640, 256 }, { 640, 512 }, { 640, 200 }, { 640, 400 },
        { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 1920, 1080 },
    };
    static uint16_t const scales[SETTINGS_NET_SCALE_COUNT] = { 50, 75, 100, 150, 200 };
    settings_t const *s = settings_get();
    uint16_t w = screens[s->net_screen % SETTINGS_NET_SCREEN_COUNT][0];
    uint16_t h = screens[s->net_screen % SETTINGS_NET_SCREEN_COUNT][1];
    uint32_t scale = scales[s->net_scale % SETTINGS_NET_SCALE_COUNT];

    if (run_mode_mouse_usb(run_mode_get()) && mouse_absolute()) {
        usb_device_mouse_abs(held_buttons, (uint16_t)((x + 32768) / 2), (uint16_t)((y + 32768) / 2));
        return;
    }

    // de9: into the top-left corner first, then the target distance
    home_x = (int32_t)(((int64_t)(x + 32768) * w / 65536) * scale / 100);
    home_y = (int32_t)(((int64_t)(y + 32768) * h / 65536) * scale / 100);
    mouse_out(-(int32_t)(w * scale / 50 + 64), -(int32_t)(h * scale / 50 + 64), 0);
    idle_seen = 0;
    mouse_was_idle = false;
    step = STEP_HOME;
}

/**
 * the de9 mouse has put out every queued step, and stayed so for 30 ms (core1
 * picks the queue up between steps; a quick look can fall in that gap, and a
 * move sent then would merge into the push into the corner)
 */
static bool mouse_settled(void)
{
    bool idle = run_mode_mouse_usb(run_mode_get()) || amiga_quad_mouse_idle();

    if (!idle) {
        mouse_was_idle = false;
        return false;
    }
    if (!mouse_was_idle) {
        mouse_was_idle = true;
        mouse_idle_since = now_ms();
    }
    return (uint32_t)(now_ms() - mouse_idle_since) >= 30u;
}

static uint32_t print_gap_ms(void)
{
    uint32_t floor_ms = keyboard_to_amiga() ? KVMD_AMIGA_CHAR_MS : 0;

    return (print_delay_ms > floor_ms) ? print_delay_ms : floor_ms;
}

static void print_step(void)
{
    uint32_t cp;
    size_t n;

    switch (step) {
        case STEP_PRINT_PRESS:
            if (!settled(keyboard_idle()))
                return;
            for (;;) {
                n = kvmd_utf8_next(print_text + print_pos, print_len - print_pos, &cp);
                if (!n) {
                    print_finish(200, NULL);
                    return;
                }
                print_pos += n;
                if (kvmd_keymap_stroke(print_keymap, cp, &stroke))
                    break;
                print_skipped++;
            }
            // modifiers in a report of their own first: the amiga side sends a
            // report's keys before its modifiers, which would type "h" for "H"
            stroke_mod = (uint8_t)(((stroke.flags & KVMD_STROKE_SHIFT) ? 0x02 : 0)
                | ((stroke.flags & KVMD_STROKE_ALTGR) ? 0x40 : 0));
            if (stroke_mod) {
                keyboard_out(stroke_mod, 0);
                step = STEP_PRINT_KEY;
            } else {
                keyboard_out(0, stroke.usage);
                step = STEP_PRINT_RELEASE;
            }
            return;

        case STEP_PRINT_KEY:
            if (!settled(keyboard_idle()))
                return;
            keyboard_out(stroke_mod, stroke.usage);
            step = STEP_PRINT_RELEASE;
            return;

        case STEP_PRINT_RELEASE:
            if (!settled(keyboard_idle()))
                return;
            keyboard_out(stroke_mod, 0);
            step = STEP_PRINT_MOD_UP;
            return;

        case STEP_PRINT_MOD_UP:
            if (stroke_mod) {
                if (!settled(keyboard_idle()))
                    return;
                keyboard_out(0, 0);
            }
            step = (stroke.flags & KVMD_STROKE_DEAD) ? STEP_PRINT_SPACE : STEP_PRINT_GAP;
            gap_until = now_ms() + print_gap_ms();
            return;

        case STEP_PRINT_SPACE:
            if (!settled(keyboard_idle()))
                return;
            keyboard_out(0, 0x2c);
            step = STEP_PRINT_SPACE_UP;
            return;

        case STEP_PRINT_SPACE_UP:
            if (!settled(keyboard_idle()))
                return;
            keyboard_out(0, 0);
            step = STEP_PRINT_GAP;
            gap_until = now_ms() + print_gap_ms();
            return;

        case STEP_PRINT_GAP:
            if ((int32_t)(now_ms() - gap_until) < 0)
                return;
            step = STEP_PRINT_PRESS;
            return;

        default:
            return;
    }
}

void kvmd_api_task(void)
{
    settings_t const *s = settings_get();
    bool allowed = s->net_control && (s->wifi_mode != SETTINGS_WIFI_AP);
    bool active;

    // switched off, or the menu opened: let go of everything
    if (!allowed || menu_is_open()) {
        bool held = held_mod || held_buttons || held_keys[0] || held_keys[1] || held_keys[2]
            || held_keys[3] || held_keys[4] || held_keys[5];

        q_tail = q_head;
        if (print_busy)
            print_finish(503, "network control stopped");
        step = STEP_NONE;
        if (held)
            release_all();
        return;
    }

    // a client that went away does not keep keys down
    if ((held_mod || held_buttons || held_keys[0]) && (uint32_t)(now_ms() - last_request_ms) >= KVMD_HOLD_RELEASE_MS) {
        ahprintf("[api] no requests for 60 s: releasing held keys and buttons\n");
        release_all();
    }

    active = (uint32_t)(now_ms() - last_input_ms) < KVMD_ACTIVE_MS && last_input_ms;
    if (active != shown_active) {
        shown_active = active;
        dbgcons_net(active);
    }

    if (step == STEP_HOME) {
        if (!mouse_settled())
            return;
        mouse_out(home_x, home_y, 0);
        step = STEP_NONE;
        return;
    }
    if (step != STEP_NONE) {
        print_step();
        return;
    }

    while (q_tail != q_head) {
        kvmd_event_t ev = queue[q_tail];

        // one keyboard change per delivered report
        if ((ev.type == EV_KEY || ev.type == EV_PRINT) && !settled(keyboard_idle()))
            return;
        // a click must last: the amiga reads its buttons once a frame, and a
        // press and release in one usb report would merge into nothing
        if (ev.type == EV_BUTTON && (int32_t)(now_ms() - button_hold_until) < 0)
            return;

        q_tail = (q_tail + 1) % KVMD_QUEUE;
        switch (ev.type) {
            case EV_KEY:
                key_event((uint8_t)ev.a, ev.b != 0);
                return;
            case EV_BUTTON:
                held_buttons = ev.b ? (uint8_t)(held_buttons | ev.a) : (uint8_t)(held_buttons & ~ev.a);
                mouse_out(0, 0, 0);
                button_hold_until = now_ms() + KVMD_BUTTON_MS;
                return;
            case EV_REL:
                mouse_out(ev.a, ev.b, 0);
                break;
            case EV_WHEEL:
                mouse_out(0, 0, ev.a);
                break;
            case EV_ABS:
                abs_move(ev.a, ev.b);
                if (step == STEP_HOME)
                    return;
                break;
            case EV_PRINT:
                step = STEP_PRINT_PRESS;
                idle_seen = KVMD_IDLE_CHECKS;
                print_step();
                return;
            case EV_RESET:
                if (print_busy)
                    print_finish(503, "reset");
                release_all();
                break;
            default:
                break;
        }
    }
}
