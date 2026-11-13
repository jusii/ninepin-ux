// host test: http_server.c + ota.c together, with a fake lwip (tcp window
// like the real one), fake flash and a fake bootrom. uploads the real built
// rev7 uf2 and checks slot A in the fake flash against the built .bin.
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_sdk.h"
#include "boot/picoboot_constants.h"
#include "lwip/tcp.h"
#include "http_server.h"
#include "kvmd_api.h"
#include "ota.h"
#include "pad.h"
#include "settings.h"
#include "util/run_mode.h"
#include "wifi.h"

#define WND TCP_WND

// ---------------- fake sdk
uint8_t fake_flash[FAKE_FLASH_SIZE];
uint64_t fake_now_us;
int fake_lwip_locked;
static int flash_ops, buys, reboots, flash_while_open;
static struct tcp_pcb *uploading;
static uint32_t reboot_flags, reboot_addr;
static int boot_partition = 1, trial_pending;

void flash_range_erase(uint32_t off, size_t len)
{
    assert(off % FLASH_SECTOR_SIZE == 0 && len % FLASH_SECTOR_SIZE == 0 && off + len <= FAKE_FLASH_SIZE);
    memset(fake_flash + off, 0xff, len);
    fake_now_us += 45000;
}
void flash_range_program(uint32_t off, uint8_t const *data, size_t len)
{
    assert(off % FLASH_PAGE_SIZE == 0 && len % FLASH_PAGE_SIZE == 0 && off + len <= FAKE_FLASH_SIZE);
    for (size_t i = 0; i < len; i++)
        fake_flash[off + i] &= data[i];
    fake_now_us += 400 * (len / 256);
}
int flash_safe_execute(void (*fn)(void *), void *param, uint32_t t)
{
    (void)t;
    assert(!fake_lwip_locked);      // never from network context
    flash_ops++;
    if (uploading && !uploading->closed && !uploading->aborted && uploading->rcv_ann_wnd)
        flash_while_open++;
    fn(param);
    return PICO_OK;
}
bool rom_get_boot_info(boot_info_t *b) { b->partition = (int8_t)boot_partition; b->tbyb_and_update_info = trial_pending; b->boot_type = 0; return true; }
int rom_get_partition_table_info(uint32_t *out, uint32_t words, uint32_t flags)
{
    uint32_t slot = flags >> 24, first, last;
    assert(words >= 2);
    if (slot == 0) { first = 2; last = 1023; }
    else if (slot == 1) { first = 1024; last = 2044; }
    else return -1;
    out[0] = flags & 0xffffu;
    out[1] = first | (last << 13);
    return 2;
}
int rom_explicit_buy(uint8_t *b, uint32_t s) { (void)b; (void)s; buys++; return 0; }
int rom_reboot(uint32_t flags, uint32_t d, uint32_t p0, uint32_t p1) { (void)d; (void)p1; reboots++; reboot_flags = flags; reboot_addr = p0; return 0; }

char const ahp_build_stamp[] = "test";
bool ota_trial(void);

// ---------------- fake lwip
ip_addr_t ip_addr_any_fake;
static struct tcp_pcb listener;
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
    (void)f;
    if (len > pcb->sndbuf) return ERR_MEM;
    memcpy(pcb->out + pcb->out_len, d, len);
    pcb->out_len += len;
    pcb->sndbuf -= len;
    return ERR_OK;
}
err_t tcp_output(struct tcp_pcb *pcb) { (void)pcb; return ERR_OK; }
err_t tcp_close(struct tcp_pcb *pcb) { pcb->closed = 1; return ERR_OK; }
void tcp_abort(struct tcp_pcb *pcb) { pcb->aborted = 1; }
void tcp_recved(struct tcp_pcb *pcb, u16_t len) { pcb->recved += len; pcb->rcv_ann_wnd += len; assert(pcb->rcv_ann_wnd <= TCP_WND); }
static int pbufs_live;
u8_t pbuf_free(struct pbuf *p)
{
    while (p) { struct pbuf *n = p->next; free(p->payload); free(p); pbufs_live--; p = n; }
    return 1;
}
void pbuf_cat(struct pbuf *h, struct pbuf *t)
{
    struct pbuf *p;
    for (p = h; p->next; p = p->next) p->tot_len += t->tot_len;
    p->tot_len += t->tot_len;
    p->next = t;
}
char *ipaddr_ntoa(const ip_addr_t *a) { (void)a; return "192.168.4.16"; }
uint32_t get_rand_32(void) { return 0x12345678u; }

// ---------------- the firmware around it
static settings_t current;
settings_t const *settings_get(void) { return &current; }
bool settings_valid(settings_t const *s) { (void)s; return true; }
static int quiet;
void ahprintf(const char *fmt, ...) { va_list a; if (quiet) return; va_start(a, fmt); printf("    log: "); vprintf(fmt, a); va_end(a); }
static char oled_line[64];
void dbgcons_ota(char const *t) { snprintf(oled_line, sizeof(oled_line), "%s", t ? t : ""); }
void apply_watchdog(void) {}
void bt_hid_pause_scanning(bool p) { (void)p; }
bool wifi_page_needs_password(void) { return false; }
char const *wifi_name(void) { return "ninepin-64f6"; }
char const *wifi_default_name(void) { return "ninepin-64f6"; }
bool settings_device_name_ok(char const *n) { (void)n; return true; }
bool wifi_page_password_submit(char const *p, bool open) { (void)p; (void)open; return true; }

// the network-control api is not under test here: answer every call 404
void kvmd_api_handle(kvmd_request_t const *req, kvmd_response_t *resp)
{
    (void)req;
    resp->status = 404;
    resp->len = 0;
}
char const *wifi_status(void) { return "ap"; }
char const *wifi_page_result(void) { return "-"; }
bool wifi_page_submit(settings_t const *s) { (void)s; return true; }
bool wifi_page_profile_submit(wifi_profile_op_t op, settings_t const *s, char const *n, unsigned slot) { (void)op; (void)s; (void)n; (void)slot; return true; }
static pad_player_t players[2];
pad_player_t const *pads_player(uint8_t p) { return &players[p]; }
char const *bt_hid_status(void) { return "bt"; }
void reset_info_status(char *out, size_t size) { snprintf(out, size, "0:00:07 since: power on"); }
void bt_hid_pair_window(void) { }
run_mode_t run_mode_get(void) { return RUN_MODE_AMIGA; }
char const *run_mode_name(run_mode_t m) { (void)m; return "amiga"; }
char const *keyboard_map_amiga_name(uint8_t a) { (void)a; return "A"; }
char const *keyboard_map_hid_name(uint8_t h) { (void)h; return "H"; }

static uint32_t crc32(uint8_t const *d, size_t n)
{
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int b = 0; b < 8; b++) c = (c >> 1) ^ ((c & 1u) ? 0xedb88320u : 0); }
    return ~c;
}

// ---------------- driving
static uint8_t *load(char const *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *d;
    assert(f);
    fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    d = malloc(*len);
    assert(fread(d, 1, *len, f) == *len);
    fclose(f);
    return d;
}

static void main_loop_pass(void)
{
    fake_now_us += 1000;
    ota_task();
    http_server_task();
    assert(!fake_lwip_locked);
}

static int holds;
static unsigned drain_every;      // 0: the main loop runs only while the sender is blocked

/**
 * send like a tcp peer: never more than WND unacknowledged (recved)
 */
static void upload(struct tcp_pcb *p, uint8_t const *data, size_t len, size_t chunk, size_t stop_at)
{
    size_t off = 0;
    unsigned spins = 0;

    uploading = p;
    while (off < len && off < stop_at && !p->closed && !p->aborted) {
        size_t n = (len - off < chunk) ? len - off : chunk;
        if (n > p->rcv_ann_wnd && p->rcv_ann_wnd > 0)
            n = p->rcv_ann_wnd;
        if (off + n - p->recved > WND || p->rcv_ann_wnd == 0) {
            main_loop_pass();
            assert(++spins < 100000);
            continue;
        }
        struct pbuf *b = calloc(1, sizeof(*b));
        b->payload = malloc(n);
        memcpy(b->payload, data + off, n);
        b->len = b->tot_len = (u16_t)n;
        assert(p->rcv_ann_wnd >= n);
        p->rcv_ann_wnd -= (u32_t)n;
        pbufs_live++;
        size_t before = p->recved;
        err_t e = p->recv(p->arg, p, b, ERR_OK);
        assert(e == ERR_OK || e == ERR_ABRT);
        if (p->recved == before) holds++;
        off += n;
        while (p->sent && !p->closed && !p->aborted && p->sndbuf < 2920) { p->sndbuf = 2920; p->sent(p->arg, p, 0); }
        if (drain_every && (off / chunk) % drain_every == 0)
            main_loop_pass();
    }
    // a sender that stops part way leaves its window open: what the update side
    // then still flushes is not the case flash_while_open counts (where in the
    // image the stop falls decides whether a sector is waiting)
    if (off < len)
        uploading = NULL;
    for (int i = 0; i < 2000 && !p->closed && !p->aborted; i++) {
        main_loop_pass();
        while (p->sent && !p->closed && !p->aborted && p->sndbuf < 2920) { p->sndbuf = 2920; p->sent(p->arg, p, 0); }
    }
}

static struct tcp_pcb *conn_new(void)
{
    struct tcp_pcb *p = calloc(1, sizeof(*p));
    p->sndbuf = 2920;
    p->rcv_ann_wnd = TCP_WND;
    assert(listener.accept(NULL, p, ERR_OK) == ERR_OK);
    return p;
}

static uint8_t *multipart(char const *token, uint8_t const *file, size_t file_len, size_t *out_len)
{
    char const *bnd = "----geckoformboundaryf00d";
    char req[512], head[256], tail[64];
    size_t hl = (size_t)snprintf(head, sizeof(head), "--%s\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"a.uf2\"\r\nContent-Type: application/octet-stream\r\n\r\n", bnd);
    size_t tl = (size_t)snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", bnd);
    size_t total = hl + file_len + tl;
    size_t rl = (size_t)snprintf(req, sizeof(req), "POST /update?t=%s HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=%s\r\nContent-Length: %zu\r\n\r\n", token, bnd, total);
    uint8_t *all = malloc(rl + total);
    memcpy(all, req, rl); memcpy(all + rl, head, hl); memcpy(all + rl + hl, file, file_len); memcpy(all + rl + hl + file_len, tail, tl);
    *out_len = rl + total;
    return all;
}

static void response_has(struct tcp_pcb *p, char const *status, char const *must)
{
    p->out[p->out_len] = 0;
    if (strncmp(p->out, status, strlen(status)) != 0 || (must && !strstr(p->out, must))) {
        printf("FAIL want '%s' / '%s', got: %.600s\n", status, must ? must : "", p->out);
        exit(1);
    }
    printf("  ok: %s%s%s\n", status, must ? " / " : "", must ? must : "");
}

static void run_until(char const *status_prefix, unsigned max_passes)
{
    for (unsigned i = 0; i < max_passes; i++) {
        if (strncmp(ota_status(), status_prefix, strlen(status_prefix)) == 0)
            return;
        main_loop_pass();
    }
    printf("FAIL status '%s', wanted '%s'\n", ota_status(), status_prefix);
    exit(1);
}

int main(int argc, char **argv)
{
    size_t uf2_len, bin_len, all_len, pt_len;
    uint8_t *uf2, *bin, *all, *pt;
    char token[17];
    struct tcp_pcb *p;

    setvbuf(stdout, NULL, _IONBF, 0);
    assert(argc == 4 || argc == 5);
    uf2 = load(argv[1], &uf2_len);
    bin = load(argv[2], &bin_len);
    pt = load(argv[3], &pt_len);
    memset(fake_flash, 0xff, sizeof(fake_flash));
    // "slot B": the running firmware, something recognisable
    for (uint32_t i = 0; i < 64 * 1024; i++) fake_flash[4096u * 1024u + i] = (uint8_t)(i * 7 + 3);
    // "slot A": an older image to be overwritten
    memset(fake_flash + 8192, 0x5a, 512 * 1024);

    memset(&current, 0, sizeof(current));
    current.ota_needs_boot = 1;
    assert(http_server_start());
    strcpy(token, http_server_token());

    if (argc == 5) {
        // a second process: the new firmware's first boot, from the fake flash
        // of a finished install (slot A written, slot B's sector saved)
        memcpy(fake_flash + 8192, bin, bin_len);
        memcpy(fake_flash + 8u * 1024u * 1024u, fake_flash + 4096u * 1024u, 4096);
        boot_partition = 1;
        {   // the header the old firmware wrote with it (ota.c's format)
            uint32_t h[5];
            h[0] = 0x4f544142u; h[1] = 1;
            h[2] = crc32(fake_flash + 8u * 1024u * 1024u, 4096);
            h[3] = crc32(fake_flash + 4096u * 1024u + 4096u, 60u * 1024u);
            h[4] = crc32((uint8_t const *)h, 16);
            memcpy(fake_flash + 8u * 1024u * 1024u + 4096u, h, sizeof(h));
        }
    printf("the same, as the new firmware: on trial, buys after 10 s, then 'previous' is slot B\n");
    boot_partition = 0; trial_pending = 1;
    // what the bootrom's buy does: erase the other slot's first sector
    fake_now_us = 0;
    for (int i = 0; i < 50; i++) main_loop_pass();
    assert(ota_trial() && buys == 0);
    fake_now_us = 10 * 1000000ull;
    main_loop_pass();
    assert(buys == 1 && !ota_trial());
    // our fake buy erased nothing: ota.c retires slot B itself
    {
        int blank = 1;
        for (int i = 0; i < 4096; i++) if (fake_flash[4096u * 1024u + i] != 0xff) blank = 0;
        assert(blank);
    }
    assert(ota_previous_available());
    {
        char msg[64];
        assert(!ota_boot_previous(msg, sizeof(msg)));   // fake reboot returns
        assert(reboots == 1 && reboot_addr == XIP_BASE + 4096u * 1024u);
        assert(fake_flash[4096u * 1024u + 5] == (uint8_t)(5 * 7 + 3));
        printf("  previous: slot B restored, restart requested (%s)\n", msg);
    }

        printf("all passed\n");
        return 0;
    }

    printf("not on trial: no buy\n");
    fake_now_us = 11 * 1000000ull;
    main_loop_pass();
    assert(buys == 0 && !ota_trial());

    printf("upload the real uf2 (%zu bytes), 1460-byte segments, BOOT needed\n", uf2_len);
    all = multipart(token, uf2, uf2_len, &all_len);
    p = conn_new();
    upload(p, all, all_len, 1460, SIZE_MAX);
    response_has(p, "HTTP/1.1 200", "press BOOT within 30 s");
    printf("  held back %d times; recved %zu of %zu\n", holds, p->recved, all_len);
    printf("  pbufs live %d\n", pbufs_live);
    assert(holds > 0 && p->recved == all_len && pbufs_live == 0);
    run_until("waiting for BOOT", 100000);
    main_loop_pass();
    assert(strstr(oled_line, "BOOT: install"));
    printf("  oled: %s\n", oled_line);
    // slot A holds exactly the built image
    assert(memcmp(fake_flash + 8192, bin, bin_len) == 0);
    // the rest of what was erased is blank, nothing past the image's last sector touched
    printf("  slot A matches the %zu-byte .bin\n", bin_len);
    assert(ota_button());
    assert(reboots == 1 && reboot_addr == XIP_BASE + 8192 && (reboot_flags & REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE));
    // slot B's first sector saved at 8M with a header
    assert(memcmp(fake_flash + 8u * 1024u * 1024u, fake_flash + 4096u * 1024u, 4096) == 0);
    printf("  restart into slot A requested; slot B's first sector saved\n");
    free(all);

    printf("partition-table.uf2 is refused\n");
    holds = 0;
    all = multipart(token, pt, pt_len, &all_len);
    p = conn_new();
    upload(p, all, all_len, 536, SIZE_MAX);
    response_has(p, "HTTP/1.1 200", NULL);
    run_until("failed: no rp2350 firmware", 1000);
    printf("  status: %s\n", ota_status());
    free(all);

    printf("not a uf2\n");
    {
        uint8_t junk[4096];
        memset(junk, 'x', sizeof(junk));
        all = multipart(token, junk, sizeof(junk), &all_len);
        p = conn_new();
        upload(p, all, all_len, 1460, SIZE_MAX);
        run_until("failed: not a uf2", 1000);
        printf("  status: %s\n", ota_status());
        free(all);
    }

    printf("truncated uf2 (last 10 blocks missing)\n");
    all = multipart(token, uf2, uf2_len - 5120, &all_len);
    p = conn_new();
    upload(p, all, all_len, 1460, SIZE_MAX);
    run_until("failed: the image is incomplete", 100000);
    printf("  status: %s; slot A first sector blank: %d\n", ota_status(), fake_flash[8192] == 0xff);
    assert(fake_flash[8192] == 0xff && fake_flash[8192 + 4095] == 0xff);
    free(all);

    printf("dropped connection, then a new upload after the stall timeout\n");
    all = multipart(token, uf2, uf2_len, &all_len);
    p = conn_new();
    upload(p, all, all_len, 1460, all_len / 3);
    p->errf(p->arg, ERR_ABRT);       // the connection breaks
    {
        struct tcp_pcb *q = conn_new();
        upload(q, all, all_len, 1460, SIZE_MAX);
        response_has(q, "HTTP/1.1 400", "busy");
    }
    fake_now_us += 31 * 1000000ull;
    run_until("failed: the upload stopped", 100);
    p = conn_new();
    upload(p, all, all_len, 1460, SIZE_MAX);
    response_has(p, "HTTP/1.1 200", NULL);
    run_until("waiting for BOOT", 100000);
    printf("  status: %s\n", ota_status());

    printf("BOOT not pressed in 30 s\n");
    fake_now_us += 31 * 1000000ull;
    run_until("failed: BOOT was not pressed", 10);
    assert(fake_flash[8192] == 0xff);
    printf("  status: %s, oled: %s\n", ota_status(), oled_line);

    printf("no BOOT needed: installs by itself after the answer went out\n");
    current.ota_needs_boot = 0;
    p = conn_new();
    upload(p, all, all_len, 4000, SIZE_MAX);
    response_has(p, "HTTP/1.1 200", "installs it");
    {
        int r = reboots;
        run_until("installing", 100000);
        assert(reboots == r);
        fake_now_us += 2 * 1000000ull;
        main_loop_pass();
        assert(reboots == r + 1 && reboot_addr == XIP_BASE + 8192);
    }
    assert(memcmp(fake_flash + 8192, bin, bin_len) == 0);
    printf("  restarted into slot A, image intact\n");
    free(all);

    printf("all passed (%d flash operations, %d with the window open)\n", flash_ops, flash_while_open);
    return 0;
}
