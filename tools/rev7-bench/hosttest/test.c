// host test for http_server.c + web_page.c with a fake lwip tcp layer
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_sdk.h"
#include "lwip/tcp.h"
#include "settings.h"
#include "settings_names.h"
#include "http_server.h"
#include "kvmd_api.h"
#include "pad.h"
#include "profiles.h"
#include "flash_map.h"
#include "wifi.h"
#include "util/run_mode.h"

// ---------------- stubs added since the test was written
int fake_lwip_locked;
uint64_t fake_now_us = 1000000;
char const ahp_build_stamp[] = "test";
bool ota_trial(void) { return false; }
char const *ota_status(void) { return "idle"; }
bool rom_get_boot_info(boot_info_t *b) { b->partition = 1; b->tbyb_and_update_info = 0; b->boot_type = 0; return true; }
void pbuf_cat(struct pbuf *h, struct pbuf *t) { (void)h; (void)t; }

// ---------------- fake lwip
ip_addr_t ip_addr_any_fake;
static struct tcp_pcb listener;
static int pcb_count;
struct tcp_pcb *tcp_new_ip_type(u8_t type) { (void)type; return &listener; }
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *ip, u16_t port) { (void)pcb; (void)ip; (void)port; return ERR_OK; }
struct tcp_pcb *tcp_listen_with_backlog(struct tcp_pcb *pcb, u8_t b) { (void)b; return pcb; }
void tcp_accept(struct tcp_pcb *pcb, tcp_accept_fn fn) { pcb->accept = fn; }
void tcp_arg(struct tcp_pcb *pcb, void *arg) { pcb->arg = arg; }
void tcp_recv(struct tcp_pcb *pcb, tcp_recv_fn fn) { pcb->recv = fn; }
void tcp_sent(struct tcp_pcb *pcb, tcp_sent_fn fn) { pcb->sent = fn; }
void tcp_err(struct tcp_pcb *pcb, tcp_err_fn fn) { pcb->errf = fn; }
void tcp_poll(struct tcp_pcb *pcb, tcp_poll_fn fn, u8_t i) { (void)i; pcb->poll = fn; }
void tcp_setprio(struct tcp_pcb *pcb, u8_t p) { (void)pcb; (void)p; }
err_t tcp_write(struct tcp_pcb *pcb, const void *d, u16_t len, u8_t f)
{
    assert(f & TCP_WRITE_FLAG_COPY);
    assert(!pcb->closed && !pcb->aborted);
    if (len > pcb->sndbuf) return ERR_MEM;
    memcpy(pcb->out + pcb->out_len, d, len);
    pcb->out_len += len;
    pcb->sndbuf -= len;
    return ERR_OK;
}
err_t tcp_output(struct tcp_pcb *pcb) { (void)pcb; return ERR_OK; }
err_t tcp_close(struct tcp_pcb *pcb) { pcb->closed = 1; return ERR_OK; }
void tcp_abort(struct tcp_pcb *pcb) { pcb->aborted = 1; }
void tcp_recved(struct tcp_pcb *pcb, u16_t len) { pcb->recved += len; }
u8_t pbuf_free(struct pbuf *p) { (void)p; return 1; }
char *ipaddr_ntoa(const ip_addr_t *a) { (void)a; return "192.168.4.16"; }
uint32_t get_rand_32(void) { static uint32_t n = 0x12345678u; return n += 0x9e3779b9u; }

// ---------------- stubs of the firmware around it
static settings_t current;
void settings_defaults(settings_t *s) { memset(s, 0, sizeof(*s)); s->ota_needs_boot = 1; s->bt_enabled = 1; s->watchdog = 2; }
settings_t const *settings_get(void) { return &current; }
bool settings_valid(settings_t const *s) { return s->mode < SETTINGS_MODE_COUNT && s->wifi_mode < SETTINGS_WIFI_COUNT; }
void ahprintf(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("    log: "); vprintf(fmt, a); va_end(a); }
static int join_mode;
bool wifi_page_needs_password(void) { return join_mode && !current.page_open; }
char const *wifi_name(void) { return "ninepin-64f6"; }
char const *wifi_default_name(void) { return "ninepin-64f6"; }
bool settings_device_name_ok(char const *n)
{
    size_t l = strlen(n);
    if (l > 15) return false;
    for (size_t i = 0; i < l; i++)
        if (!((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= '0' && n[i] <= '9') || (n[i] == '-' && i && i + 1 < l))) return false;
    return true;
}
static char new_password[32]; static int new_open = -1, password_submits;
bool wifi_page_password_submit(char const *p, bool open)
{
    snprintf(new_password, sizeof(new_password), "%s", p ? p : ""); new_open = open; password_submits++;
    // as the main loop would
    if (new_password[0]) strcpy(current.page_password, new_password);
    current.page_open = open;
    return true;
}

// the network-control api is not under test here: answer every call 404
void kvmd_api_handle(kvmd_request_t const *req, kvmd_response_t *resp)
{
    (void)req;
    resp->status = 404;
    resp->len = 0;
}

// ota.c is not linked into this test
size_t ota_space(void) { return SIZE_MAX; }
bool ota_hold_window(void) { return false; }
char const *wifi_status(void) { return "access point ninepin-64f6, 1 client"; }
char const *wifi_page_result(void) { return "-"; }
static settings_t submitted; static int submits;
bool wifi_page_submit(settings_t const *s) { submitted = *s; submits++; return true; }

// a fake flash for profiles.c, and the main loop's part of a profile save, load, delete
uint8_t fake_flash[FAKE_FLASH_SIZE];
void flash_range_erase(uint32_t off, size_t len) { assert(off % 4096 == 0 && len % 4096 == 0); memset(fake_flash + off, 0xff, len); }
void flash_range_program(uint32_t off, uint8_t const *data, size_t len)
{
    assert(off % 256 == 0 && len % 256 == 0);
    for (size_t i = 0; i < len; i++) fake_flash[off + i] &= data[i];
}
int flash_safe_execute(void (*fn)(void *), void *param, uint32_t t) { (void)t; fn(param); return PICO_OK; }
static int profile_ops;
bool wifi_page_profile_submit(wifi_profile_op_t op, settings_t const *s, char const *name, unsigned slot)
{
    profile_ops++;
    if (op == WIFI_PROFILE_NONE) return wifi_page_submit(s);
    if (op == WIFI_PROFILE_SAVE) {
        profile_t p;
        int at;
        profiles_from_settings(&p, name, (uint8_t)slot, s);
        at = profiles_put(&p);
        assert(at >= 0);
        current = *s;
        current.pad_profile = (uint8_t)(at + 1);
    } else if (op == WIFI_PROFILE_LOAD) {
        profiles_to_settings(profiles_get(slot), &current);
        current.pad_profile = (uint8_t)(slot + 1);
    } else {
        assert(profiles_remove(slot));
        if (current.pad_profile == slot + 1) current.pad_profile = 0;
    }
    return true;
}
static pad_player_t players[2];
pad_player_t const *pads_player(uint8_t p) { return &players[p]; }
char const *bt_hid_status(void) { return "bt c0:scan le:scan"; }
void reset_info_status(char *out, size_t size) { snprintf(out, size, "0:00:07 since: power on"); }
static int pair_windows;
void bt_hid_pair_window(void) { pair_windows++; }
run_mode_t run_mode_get(void) { return RUN_MODE_AMIGA; }
char const *run_mode_name(run_mode_t m) { (void)m; return "amiga"; }
char const *keyboard_map_amiga_name(uint8_t a) { (void)a; return "A<x>"; }
char const *keyboard_map_hid_name(uint8_t h) { (void)h; return "Caps & Lock"; }

// ---------------- ota hooks under test
static uint8_t *received; static size_t received_len; static int finishes; static int refuse_at = -1;
bool ota_receive(uint32_t offset, uint8_t const *data, size_t len)
{
    if (refuse_at >= 0 && offset >= (uint32_t)refuse_at) return false;
    assert(offset == received_len);
    memcpy(received + offset, data, len);
    received_len += len;
    return true;
}
bool ota_finish(char *message, size_t size) { finishes++; snprintf(message, size, "got %zu bytes", received_len); return refuse_at < 0; }

// ---------------- driving
static struct tcp_pcb *conn_new(void)
{
    struct tcp_pcb *p = calloc(1, sizeof(*p));
    p->sndbuf = 2920;
    pcb_count++;
    assert(listener.accept(NULL, p, ERR_OK) == ERR_OK);
    return p;
}

static void deliver(struct tcp_pcb *p, void const *data, size_t len, size_t chunk)
{
    size_t off = 0;
    while (off < len && !p->closed && !p->aborted) {
        size_t n = (len - off < chunk) ? len - off : chunk;
        struct pbuf b = { NULL, (void *)((char const *)data + off), (u16_t)n, (u16_t)n };
        err_t e = p->recv(p->arg, p, &b, ERR_OK);
        assert(e == ERR_OK || e == ERR_ABRT);
        off += n;
        // let the "network" drain the send buffer
        while (p->sent && !p->closed && !p->aborted && p->sndbuf < 2920) { p->sndbuf = 2920; p->sent(p->arg, p, 0); }
    }
    while (p->sent && !p->closed && !p->aborted && p->sndbuf < 2920) { p->sndbuf = 2920; p->sent(p->arg, p, 0); }
}

static void check_response(struct tcp_pcb *p, int status, char const *must)
{
    char head[32];
    char *body;
    long cl;
    p->out[p->out_len] = 0;
    snprintf(head, sizeof(head), "HTTP/1.1 %d ", status);
    if (strncmp(p->out, head, strlen(head)) != 0) { printf("FAIL want %d got: %.300s\n", status, p->out); exit(1); }
    body = strstr(p->out, "\r\n\r\n");
    assert(body);
    body += 4;
    cl = strtol(strstr(p->out, "Content-Length: ") + 16, NULL, 10);
    if ((size_t)cl != p->out_len - (size_t)(body - p->out)) { printf("FAIL content-length %ld vs %zu\n", cl, p->out_len - (size_t)(body - p->out)); exit(1); }
    if (must && !strstr(p->out, must)) { printf("FAIL missing '%s' in: %.2000s\n", must, p->out); exit(1); }
    assert(p->closed);
    printf("  ok %d (%zu bytes)%s%s\n", status, p->out_len, must ? ", has " : "", must ? must : "");
}

static char last_out[70000];
static void request(char const *req, size_t chunk, int status, char const *must)
{
    struct tcp_pcb *p = conn_new();
    deliver(p, req, strlen(req), chunk);
    check_response(p, status, must);
    memcpy(last_out, p->out, p->out_len + 1);
}

static void post(char const *path, char const *extra_head, char const *body, int status, char const *must)
{
    char r[4096];
    snprintf(r, sizeof(r), "POST %s HTTP/1.1\r\n%sContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\n\r\n%s",
        path, extra_head ? extra_head : "", strlen(body), body);
    request(r, 11, status, must);
}

// the session cookie of the last response, as a request header line
static char *cookie_line(void)
{
    static char line[128];
    char *c = strstr(last_out, "Set-Cookie: auth_token=");
    assert(c);
    snprintf(line, sizeof(line), "Cookie: a=b; auth_token=%.16s; z=1\r\n", c + 23);
    return line;
}

int main(void)
{
    char req[4096];
    char token[17];

    settings_defaults(&current);
    strcpy(current.page_password, "d37qtsp979");
    strcpy(current.wifi_ssid, "a<b>\"c");
    strcpy(players[0].name, "DualSense");
    players[0].active = true;
    players[0].input.buttons = (1u << 4) | (1u << 6);
    current.kb_remap[0].source = 4; current.kb_remap[0].target = 5;

    assert(http_server_start());
    strcpy(token, http_server_token());
    printf("token %s\n", token);

    printf("GET / (ap mode), various chunkings\n");
    request("GET / HTTP/1.1\r\nHost: x\r\n\r\n", 1, 200, "<select name=wifi_mode>");
    request("GET / HTTP/1.1\r\nHost: x\r\n\r\n", 7, 200, "a&lt;b&gt;&quot;c");
    request("GET /?x=1 HTTP/1.1\r\nHost: x\r\n\r\n", 1000, 200, "DualSense, holding button 5 7");
    request("GET /favicon.ico HTTP/1.1\r\n\r\n", 1000, 404, NULL);
    request("PUT / HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc", 1000, 405, NULL);
    request("garbage\r\n\r\n", 1000, 400, NULL);
    {
        // print the page once to look at
        struct tcp_pcb *p = conn_new();
        char const *g = "GET / HTTP/1.1\r\n\r\n";
        FILE *f;
        deliver(p, g, strlen(g), 100);
        f = fopen("page.html", "w");
        fwrite(strstr(p->out, "\r\n\r\n") + 4, 1, p->out_len - (size_t)(strstr(p->out, "\r\n\r\n") + 4 - p->out), f);
        fclose(f);
    }

    printf("join mode: log-in form, sessions, basic auth\n");
    join_mode = 1;
    request("GET / HTTP/1.1\r\n\r\n", 1000, 303, "Location: /login");
    assert(!strstr(last_out, "WWW-Authenticate"));
    request("GET /login HTTP/1.1\r\n\r\n", 1000, 200, "autocomplete=current-password");
    // "x:d37qtsp979" -> eDpkMzdxdHNwOTc5
    request("GET / HTTP/1.1\r\nAuthorization: Basic eDpkMzdxdHNwOTc5\r\n\r\n", 1000, 200, "<h1>ninepin-64f6</h1>");
    // ":d37qtsp979" -> OmQzN3F0c3A5Nzk=
    request("GET / HTTP/1.1\r\nauthorization: basic OmQzN3F0c3A5Nzk=\r\n\r\n", 5, 200, NULL);
    // "x:d37qtsp97" -> eDpkMzdxdHNwOTc=
    request("GET / HTTP/1.1\r\nAuthorization: Basic eDpkMzdxdHNwOTc=\r\n\r\n", 1000, 303, NULL);
    // a post body behind a 401 is read first
    request("POST /settings HTTP/1.1\r\nContent-Length: 5\r\n\r\nabcde", 2, 401, NULL);
    request("GET /favicon.ico HTTP/1.1\r\n\r\n", 1000, 401, NULL);
    {
        char *cookie, r[512], body[256];

        post("/login", NULL, "username=ninepin-64f6&password=wrong", 403, "not the page password");
        post("/login", NULL, "username=ninepin-64f6", 403, "not the page password");
        post("/login", NULL, "username=ninepin-64f6&password=d37qtsp979", 303, "Location: /\r\n");
        cookie = cookie_line();
        assert(strstr(last_out, "HttpOnly; SameSite=Strict"));
        snprintf(r, sizeof(r), "GET / HTTP/1.1\r\n%s\r\n", cookie);
        request(r, 9, 200, "action=/logout");
        request("GET / HTTP/1.1\r\nCookie: auth_token=0000000000000000\r\n\r\n", 1000, 303, NULL);
        request("GET / HTTP/1.1\r\nCookie: auth_token=\r\n\r\n", 1000, 303, NULL);
        // logged in already: /login leads to the page
        snprintf(r, sizeof(r), "GET /login HTTP/1.1\r\n%s\r\n", cookie);
        request(r, 1000, 303, "Location: /\r\n");

        // the password form
        snprintf(body, sizeof(body), "t=%s&new=short&again=short&open=0", token);
        post("/password", cookie, body, 400, "8 to 16");
        snprintf(body, sizeof(body), "t=%s&new=longenough1&again=longenough2&open=0", token);
        post("/password", cookie, body, 400, "not the same");
        snprintf(body, sizeof(body), "t=%s&new=has+space+in&again=has+space+in&open=0", token);
        post("/password", cookie, body, 400, "without spaces");
        snprintf(body, sizeof(body), "t=%s&new=12345678901234567&again=12345678901234567&open=0", token);
        post("/password", cookie, body, 400, "8 to 16");
        snprintf(body, sizeof(body), "t=nope&new=longenough1&again=longenough1&open=0");
        post("/password", cookie, body, 400, "out of date");
        snprintf(body, sizeof(body), "t=%s&new=&again=&open=0", token);
        post("/password", cookie, body, 200, "Nothing to change");
        assert(password_submits == 0);
        snprintf(body, sizeof(body), "t=%s&new=longenough1&again=longenough1&open=0", token);
        post("/password", NULL, body, 401, NULL);
        assert(password_submits == 0);
        post("/password", cookie, body, 200, "Set-Cookie: auth_token=");
        assert(password_submits == 1 && !strcmp(new_password, "longenough1") && new_open == 0);
        // the old session is gone, the new one and the new password work
        snprintf(r, sizeof(r), "GET / HTTP/1.1\r\n%s\r\n", cookie);
        cookie = cookie_line();
        request(r, 1000, 303, NULL);
        snprintf(r, sizeof(r), "GET / HTTP/1.1\r\n%s\r\n", cookie);
        request(r, 1000, 200, NULL);
        post("/login", NULL, "password=d37qtsp979", 403, NULL);
        post("/login", NULL, "password=longenough1", 303, NULL);

        // the pair button: needs the log-in and the page's token
        snprintf(body, sizeof(body), "t=%s&mode=0", token);
        post("/bt-pair", NULL, body, 401, NULL);
        post("/bt-pair", cookie, "t=bad", 400, NULL);
        assert(pair_windows == 0);
        post("/bt-pair", cookie, body, 200, "Pairing open");
        assert(pair_windows == 1);
        snprintf(r, sizeof(r), "GET / HTTP/1.1\r\n%s\r\n", cookie);
        request(r, 1000, 200, "formaction=/bt-pair");

        // log out
        snprintf(body, sizeof(body), "t=%s", token);
        post("/logout", cookie, "t=bad", 400, NULL);
        post("/logout", cookie, body, 303, "Location: /\r\n");
        request(r, 1000, 303, "Location: /login");

        // no password asked for: everything is open, and back
        post("/login", NULL, "password=longenough1", 303, NULL);
        cookie = cookie_line();
        snprintf(body, sizeof(body), "t=%s&new=&again=&open=1", token);
        post("/password", cookie, body, 200, "no longer asks");
        assert(password_submits == 2 && new_open == 1 && !strcmp(current.page_password, "longenough1"));
        request("GET / HTTP/1.1\r\n\r\n", 1000, 200, "<option value=1 selected>no password");
        assert(!strstr(last_out, "action=/logout"));
        request("GET /login HTTP/1.1\r\n\r\n", 1000, 303, "Location: /\r\n");
        snprintf(body, sizeof(body), "t=%s&open=0", token);
        post("/password", NULL, body, 200, "asks for the page password");
        request("GET / HTTP/1.1\r\n\r\n", 1000, 303, NULL);

        // five wrong passwords lock the address out
        for (int i = 0; i < 5; i++)
            post("/login", NULL, "password=wrongwrong", 403, "not the page password");
        post("/login", NULL, "password=longenough1", 403, "Too many wrong");
        fake_now_us += 31000000ull;
        post("/login", NULL, "password=longenough1", 303, NULL);
    }
    strcpy(current.page_password, "d37qtsp979");
    join_mode = 0;

    printf("POST /settings\n");
    {
        char body[512];
        snprintf(body, sizeof(body), "t=%s&mode=2&amiga_port1=3&wifi_mode=2&wifi_ssid=My+Net%%21&wifi_password=secret%%26123&kb_help=4&ota_needs_boot=0", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 13, 200, "joins");
        assert(submits == 1);
        assert(submitted.mode == 2 && submitted.amiga_port[0] == 3 && submitted.wifi_mode == 2 && submitted.kb_help == 4);
        assert(strcmp(submitted.wifi_ssid, "My Net!") == 0);
        assert(strcmp(submitted.wifi_password, "secret&123") == 0);
        assert(submitted.ota_needs_boot == 1);
        assert(strcmp(submitted.page_password, "d37qtsp979") == 0);

        // pad combos: the form carries them whole; a partial form leaves them be
        current.pad_combo[5].button = 9; current.pad_combo[5].key = 0x2c;
        snprintf(body, sizeof(body), "t=%s&mode=1", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 200, NULL);
        assert(submitted.pad_combo[5].button == 9 && submitted.pad_combo[5].key == 0x2c);
        snprintf(body, sizeof(body), "t=%s&combos=1&c1b=6&c1d=1&c1f=1&c1k=&c2b=7&c2k=+Space&c3b=0&c3u=1&c3k=f1&c4b=16&c4k=0x64&c5b=1&c5k=Q", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 17, 200, NULL);
        assert(submitted.pad_combo[0].button == 6 && submitted.pad_combo[0].outputs == (2 | 16) && submitted.pad_combo[0].key == 0);
        assert(submitted.pad_combo[1].button == 7 && submitted.pad_combo[1].outputs == 0 && submitted.pad_combo[1].key == 0x2c);
        assert(submitted.pad_combo[2].button == 0 && submitted.pad_combo[2].outputs == 0 && submitted.pad_combo[2].key == 0);
        assert(submitted.pad_combo[3].button == 16 && submitted.pad_combo[3].key == 0x64);
        assert(submitted.pad_combo[4].button == 1 && submitted.pad_combo[4].key == 0x14);
        assert(submitted.pad_combo[5].button == 0 && submitted.pad_combo[5].key == 0);
        snprintf(body, sizeof(body), "t=%s&combos=1&c1b=17", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "no such pad button");
        snprintf(body, sizeof(body), "t=%s&combos=1&c1b=1&c1k=spce", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "no such key");
        // the page shows them
        current.pad_combo[0] = submitted.pad_combo[0]; current.pad_combo[1] = submitted.pad_combo[1];
        request("GET / HTTP/1.1\r\n\r\n", 1000, 200, "<option value=6 selected>6 R1");
        assert(strstr(last_out, "name=c1d value=1 checked") && strstr(last_out, "name=c1f value=1 checked"));
        assert(!strstr(last_out, "name=c1u value=1 checked") && strstr(last_out, "name=c2k size=7 maxlength=9 list=keys value=\"space\""));
        memset(current.pad_combo, 0, sizeof(current.pad_combo));

        // pad profiles: save the form's pad part under a name, load, delete
        memset(fake_flash, 0xff, sizeof(fake_flash));
        {
            // a record as the 16-slot firmware wrote it is carried over, for any machine
            uint8_t old[16 + 16 * 33 + 4]; uint32_t head[4] = { 0x50484141u, 1, 16 * 33, 7 }, crc = UINT32_MAX;
            profile_t fresh;
            memset(old, 0, sizeof(old)); memcpy(old, head, 16);
            strcpy((char *)old + 16 + 2 * 33, "Old one"); old[16 + 2 * 33 + 13] = 1; old[16 + 2 * 33 + 15] = 6; old[16 + 2 * 33 + 16] = 0x12;
            for (size_t k = 0; k < sizeof(old) - 4; k++) { crc ^= old[k]; for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0); }
            crc = ~crc; memcpy(old + sizeof(old) - 4, &crc, 4);
            memcpy(fake_flash + FLASH_MAP_PROFILES_OFFSET + 4096, old, sizeof(old));
            profiles_init();
            assert(profiles_count() == 1 && profiles_find("Old one") == 2 && profiles_get(2)->machine == PROFILE_FOR_ANY);
            assert(profiles_get(2)->pad_triangle == 1 && profiles_get(2)->combo[0].button == 6 && profiles_get(2)->combo[0].outputs == 0x12);
            profiles_from_settings(&fresh, "New one", PROFILE_FOR_C64, &current);
            assert(profiles_put(&fresh) == 0);
            profiles_init();
            assert(profiles_count() == 2 && profiles_find("Old one") == 2 && profiles_get(0)->machine == PROFILE_FOR_C64);
            // offered by machine: the c64 one not in amiga mode, both in c64 and usb mode
            assert(!profiles_shown(profiles_get(0), SETTINGS_MODE_AMIGA) && profiles_shown(profiles_get(0), SETTINGS_MODE_C64)
                && profiles_shown(profiles_get(0), SETTINGS_MODE_USB) && profiles_shown(profiles_get(2), SETTINGS_MODE_AMIGA));
            assert(profiles_count_shown(SETTINGS_MODE_AMIGA) == 1 && profiles_count_shown(SETTINGS_MODE_C64) == 2);
            {
                // each mode has its own: c64 with "New one", over to the amiga (none: the combos go), and back
                settings_t was = current, next;
                was.mode = SETTINGS_MODE_C64; was.pad_profile = 1; was.pad_combo[0].button = 3; memset(was.pad_profile_mode, 0, 3);
                next = was; next.mode = SETTINGS_MODE_AMIGA; profiles_follow_mode(&next, &was);
                assert(next.pad_profile == 0 && next.pad_combo[0].button == 0 && next.pad_profile_mode[SETTINGS_MODE_C64] == 1);
                was = next; next.mode = SETTINGS_MODE_C64; profiles_follow_mode(&next, &was);
                assert(next.pad_profile == 1 && next.pad_profile_mode[SETTINGS_MODE_AMIGA] == 0);
                // no profiles in play: a change of mode leaves the combos alone
                was = current; was.pad_profile = 0; was.pad_combo[0].button = 3; memset(was.pad_profile_mode, 0, 3);
                next = was; next.mode = (uint8_t)!was.mode; profiles_follow_mode(&next, &was);
                assert(next.pad_combo[0].button == 3 && next.pad_profile == 0);
                // the same mode: just noted
                was = current; next = was; next.pad_profile = 3; profiles_follow_mode(&next, &was);
                assert(next.pad_profile_mode[was.mode] == 3);
            }
            memset(fake_flash, 0xff, sizeof(fake_flash));
        }
        profiles_init();
        assert(profiles_count() == 0);
        request("GET / HTTP/1.1\r\n\r\n", 1000, 200, "Profile: none");
        assert(!strstr(last_out, "formaction=/profile-load") && strstr(last_out, "formaction=/profile-save"));
        snprintf(body, sizeof(body), "t=%s&pad_triangle=1&pad_directions=2&combos=1&c1b=6&c1d=1&c1f=1&c2b=7&c2k=space&profile_for=0&profile_name=Blue+Max", token);
        post("/profile-save", NULL, body, 200, "as the profile Blue Max");
        assert(profiles_count() == 1 && current.pad_profile == 1 && current.pad_triangle == 1 && current.pad_combo[1].key == 0x2c);
        snprintf(body, sizeof(body), "t=%s&pad_triangle=0&pad_directions=0&combos=1&c1b=5&c1u=1&profile_for=0&profile_name=jumper", token);
        post("/profile-save", NULL, body, 200, "as the profile jumper");
        assert(profiles_count() == 2 && current.pad_profile == 2 && current.pad_combo[0].button == 5 && current.pad_combo[1].button == 0);
        request("GET / HTTP/1.1\r\n\r\n", 1000, 200, "Profile: <b>jumper</b><label>");
        assert(strstr(last_out, "<option value=1>Blue Max<option value=2 selected>jumper</select>"));
        // the same name again: saved over, not a third
        snprintf(body, sizeof(body), "t=%s&combos=1&c1b=5&c1u=1&c1f=1&profile_for=0&profile_name=jumper", token);
        post("/profile-save", NULL, body, 200, NULL);
        assert(profiles_count() == 2 && profiles_get(1)->combo[0].outputs == (1 | 16));
        // edited without saving the profile: the page says so
        current.pad_combo[0].outputs = 1;
        request("GET / HTTP/1.1\r\n\r\n", 1000, 200, "<b>jumper</b> (changed since)");
        // load the other one
        snprintf(body, sizeof(body), "t=%s&mode=2&profile=1&profile_for=0&profile_name=ignored", token);
        post("/profile-load", NULL, body, 200, "loads the pad profile Blue Max");
        assert(current.pad_profile == 1 && current.pad_triangle == 1 && current.pad_directions == 2 && current.mode == 0);
        assert(current.pad_combo[0].button == 6 && current.pad_combo[0].outputs == (2 | 16) && current.pad_combo[1].key == 0x2c);
        // they survive a restart, in the flash
        profiles_init();
        assert(profiles_count() == 2 && !strcmp(profiles_get(0)->name, "Blue Max") && profiles_find("jumper") == 1);
        // refusals
        snprintf(body, sizeof(body), "t=%s&profile_for=0&profile_name=", token);
        post("/profile-save", NULL, body, 400, "1 to 12");
        snprintf(body, sizeof(body), "t=%s&profile_for=0&profile_name=thirteen-char", token);
        post("/profile-save", NULL, body, 400, "1 to 12");
        snprintf(body, sizeof(body), "t=%s&profile_for=0&profile_name=a%%3Cb", token);
        post("/profile-save", NULL, body, 400, "1 to 12");
        snprintf(body, sizeof(body), "t=%s&profile_name=nomachine", token);
        post("/profile-save", NULL, body, 400, "which machine");
        snprintf(body, sizeof(body), "t=%s&profile_for=3&profile_name=nomachine", token);
        post("/profile-save", NULL, body, 400, "which machine");
        snprintf(body, sizeof(body), "t=bad&profile_for=0&profile_name=x");
        post("/profile-save", NULL, body, 400, "out of date");
        snprintf(body, sizeof(body), "t=%s&profile=7", token);
        post("/profile-load", NULL, body, 400, "no such pad profile");
        snprintf(body, sizeof(body), "t=%s&profile=x", token);
        post("/profile-delete", NULL, body, 400, "no such pad profile");
        snprintf(body, sizeof(body), "t=bad&profile=1");
        post("/profile-delete", NULL, body, 400, "out of date");
        // delete the loaded one
        snprintf(body, sizeof(body), "t=%s&profile=1", token);
        post("/profile-delete", NULL, body, 200, "deletes the pad profile Blue Max");
        assert(profiles_count() == 1 && current.pad_profile == 0 && current.pad_combo[0].button == 6);
        // thirty-two fit, the next is refused, saving over one still goes
        for (int i = 0; i < 31; i++) {
            snprintf(body, sizeof(body), "t=%s&profile_for=0&profile_name=game+%d", token, i);
            post("/profile-save", NULL, body, 200, NULL);
        }
        assert(profiles_count() == 32);
        snprintf(body, sizeof(body), "t=%s&profile_for=0&profile_name=one+too+many", token);
        post("/profile-save", NULL, body, 400, "slot is in use");
        snprintf(body, sizeof(body), "t=%s&profile_for=0&profile_name=game+3", token);
        post("/profile-save", NULL, body, 200, NULL);
        request("GET / HTTP/1.1\r\n\r\n", 1000, 200, "<b>game 3</b>");
        printf("  page with 32 profiles: %zu bytes\n", strlen(last_out));
        profiles_init();
        assert(profiles_count() == 32);
        memset(current.pad_combo, 0, sizeof(current.pad_combo));
        current.pad_profile = 0; current.pad_triangle = 0; current.pad_directions = 0;

        // the device name
        snprintf(body, sizeof(body), "t=%s&wifi_mode=1&device_name=Amiga-4000", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 200, "access point amiga-4000");
        assert(!strcmp(submitted.device_name, "amiga-4000"));
        snprintf(body, sizeof(body), "t=%s&device_name=-bad", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "device name");
        snprintf(body, sizeof(body), "t=%s&device_name=a+b", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "device name");
        snprintf(body, sizeof(body), "t=%s&device_name=sixteen-chars-xx", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "device name");
        submits = 1;
        profile_ops = 0;

        snprintf(body, sizeof(body), "t=%s&mode=9", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "no such value");
        snprintf(body, sizeof(body), "t=bad&mode=1");
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "out of date");
        snprintf(body, sizeof(body), "t=%s&wifi_password=short", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        request(req, 1000, 400, "8 to 63");
        snprintf(body, sizeof(body), "t=%s&wifi_open=1&wifi_password=ignoredxx", token);
        snprintf(req, sizeof(req), "POST /settings HTTP/1.1\r\nContent-Length: %zu\r\n\r\n%s", strlen(body), body);
        strcpy(current.wifi_password, "oldpassword");
        request(req, 1000, 200, NULL);
        assert(submitted.wifi_password[0] == 0);
        current.wifi_password[0] = 0;
        assert(submits == 2);
    }

    printf("POST /update multipart, 2.5 MB\n");
    {
        size_t file_len = 2500000 + 17;
        uint8_t *file = malloc(file_len);
        char const *boundary = "----geckoformboundary1234567890abcdef";
        char head[512], tailstr[128];
        size_t hl, tl, total;
        uint8_t *all;
        size_t chunks[] = { 1460, 536, 1, 7, 4000 };
        for (size_t i = 0; i < file_len; i++) file[i] = (uint8_t)(rand() & 0xff);
        // put a decoy near the end: "\r\n--" without the full boundary
        memcpy(file + file_len - 10, "\r\n--abc", 7);
        received = malloc(file_len + 100);
        hl = (size_t)snprintf(head, sizeof(head), "--%s\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.uf2\"\r\nContent-Type: application/octet-stream\r\n\r\n", boundary);
        tl = (size_t)snprintf(tailstr, sizeof(tailstr), "\r\n--%s--\r\n", boundary);
        total = hl + file_len + tl;
        for (size_t k = 0; k < sizeof(chunks) / sizeof(chunks[0]); k++) {
            struct tcp_pcb *p = conn_new();
            size_t rl = (size_t)snprintf(req, sizeof(req), "POST /update?t=%s HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=%s\r\nContent-Length: %zu\r\n\r\n", token, boundary, total);
            all = malloc(rl + total);
            memcpy(all, req, rl); memcpy(all + rl, head, hl); memcpy(all + rl + hl, file, file_len); memcpy(all + rl + hl + file_len, tailstr, tl);
            received_len = 0; finishes = 0;
            deliver(p, all, rl + total, chunks[k]);
            check_response(p, 200, "got 2500017 bytes");
            assert(received_len == file_len && memcmp(received, file, file_len) == 0);
            assert(finishes == 1);
            assert(p->recved == rl + total);
            free(all);
        }
        // quoted boundary, no trailing crlf
        {
            struct tcp_pcb *p = conn_new();
            size_t rl;
            tl = (size_t)snprintf(tailstr, sizeof(tailstr), "\r\n--%s--", boundary);
            total = hl + 1000 + tl;
            rl = (size_t)snprintf(req, sizeof(req), "POST /update?t=%s HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=\"%s\"\r\nContent-Length: %zu\r\n\r\n", token, boundary, total);
            all = malloc(rl + total);
            memcpy(all, req, rl); memcpy(all + rl, head, hl); memcpy(all + rl + hl, file, 1000); memcpy(all + rl + hl + 1000, tailstr, tl);
            received_len = 0; finishes = 0;
            deliver(p, all, rl + total, 300);
            check_response(p, 200, "got 1000 bytes");
            assert(received_len == 1000 && memcmp(received, file, 1000) == 0);
            free(all);
        }
        // raw body
        {
            struct tcp_pcb *p = conn_new();
            size_t rl = (size_t)snprintf(req, sizeof(req), "POST /update?t=%s HTTP/1.1\r\nContent-Type: application/octet-stream\r\nContent-Length: %zu\r\n\r\n", token, file_len);
            all = malloc(rl + file_len);
            memcpy(all, req, rl); memcpy(all + rl, file, file_len);
            received_len = 0; finishes = 0;
            deliver(p, all, rl + file_len, 1460);
            check_response(p, 200, "got 2500017 bytes");
            assert(received_len == file_len && memcmp(received, file, file_len) == 0);
            free(all);
        }
        // refused part way: body drained, then the reason
        {
            struct tcp_pcb *p = conn_new();
            size_t rl = (size_t)snprintf(req, sizeof(req), "POST /update?t=%s HTTP/1.1\r\nContent-Length: %zu\r\n\r\n", token, file_len);
            all = malloc(rl + file_len);
            memcpy(all, req, rl); memcpy(all + rl, file, file_len);
            received_len = 0; finishes = 0; refuse_at = 10000;
            deliver(p, all, rl + file_len, 1460);
            check_response(p, 400, "Update not installed");
            assert(finishes == 1 && p->recved == rl + file_len);
            refuse_at = -1;
            free(all);
        }
        // bad token: drained, 403
        {
            struct tcp_pcb *p = conn_new();
            size_t rl = (size_t)snprintf(req, sizeof(req), "POST /update?t=nope HTTP/1.1\r\nContent-Length: %zu\r\n\r\n", (size_t)100000);
            all = calloc(1, rl + 100000);
            memcpy(all, req, rl);
            deliver(p, all, rl + 100000, 1460);
            check_response(p, 403, NULL);
            free(all);
        }
        // missing closing boundary
        {
            struct tcp_pcb *p = conn_new();
            size_t rl;
            total = hl + 1000;
            rl = (size_t)snprintf(req, sizeof(req), "POST /update?t=%s HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=%s\r\nContent-Length: %zu\r\n\r\n", token, boundary, total);
            all = malloc(rl + total);
            memcpy(all, req, rl); memcpy(all + rl, head, hl); memcpy(all + rl + hl, file, 1000);
            finishes = 0; received_len = 0;
            deliver(p, all, rl + total, 512);
            check_response(p, 400, "ended early");
            assert(finishes == 0);
            free(all);
        }
        // no length
        request("POST /update HTTP/1.1\r\n\r\n", 1000, 411, NULL);
    }
    printf("all passed\n");
    return 0;
}
