/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * see net_update.h.
 *
 * one https request at a time, in lwip's raw (altcp) api, from the cyw43
 * background context:
 *
 *   check     GET <repo>/releases/latest/download/ninepin-ux.uf2
 *             github answers with a redirect to .../download/<tag>/...: the
 *             tag is the version, and nothing else is fetched
 *   install   GET .../download/<tag>/ninepin-ux.uf2.sig, following the
 *             redirect to the asset server; then the .uf2 the same way, its
 *             body hashed and passed to ota_receive() as it comes
 *
 * the tcp window is given back only as far as the update side has room, and
 * not at all whilst it wants to write flash (ota_hold_window()), as for an
 * upload from the page (http_server.c).
 */

#include "config.h"
#include "net_update.h"

#ifdef HAS_WIFI

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/altcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/tcp.h"

#include "mbedtls/ecdsa.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"

#include "build_stamp.h"
#include "ota.h"
#include "update_keys.h"
#include "util/output.h"
#include "wifi.h"

// where the releases are. a test build points these at a server of its own
#ifndef NET_UPDATE_HOST
#define NET_UPDATE_HOST         "github.com"
#endif
#ifndef NET_UPDATE_PORT
#define NET_UPDATE_PORT         443
#endif
#ifndef NET_UPDATE_REPO
#define NET_UPDATE_REPO         "/jusii/ninepin-ux"
#endif
// a board that needs another firmware asks for another file
#define NET_UPDATE_ASSET        "ninepin-ux.uf2"

#define NU_HOST_MAX             80
#define NU_PATH_MAX             2048    // the asset server's addresses are long
#define NU_REDIRECTS            4
#define NU_SIGNATURE_MAX        80      // der: two 32-byte integers and their wrapping
#define NU_TIMEOUT_MS           20000   // without a byte either way
#define NU_TAG_MAX              24

typedef enum { JOB_NONE = 0, JOB_CHECK, JOB_SIGNATURE, JOB_IMAGE } job_t;
typedef enum { AT_IDLE = 0, AT_RESOLVE, AT_CONNECT, AT_HEAD, AT_BODY } stage_t;

static volatile bool want_check, want_install;
static volatile job_t job;
static stage_t stage;

static struct altcp_tls_config *tls;
static struct altcp_pcb *pcb;
static char host[NU_HOST_MAX];
static uint16_t port;
static char path[NU_PATH_MAX];
static unsigned redirects;
static uint32_t active_ms;          // the last byte either way

// the response head, a line at a time
static char line[NU_PATH_MAX + 16];
static size_t line_len;
static int http_status;
static bool have_location;
static uint32_t content_length;
static bool have_length;

// the body
static uint32_t body_seen;
static uint32_t owed;               // image bytes taken, their window not yet given back
static uint8_t signature[NU_SIGNATURE_MAX];
static size_t signature_len;
static mbedtls_sha256_context hash;

// the release a download handed to the update side, until that has installed
// it or let it go; and, in ram the restart leaves alone, the one being
// restarted into: if another firmware then finds it there, it fell back
static char handed_over[NU_TAG_MAX];
#define ATTEMPT_MAGIC 0x4e557064u

typedef struct {
    uint32_t magic;
    char version[NU_TAG_MAX];
} attempt_t;

static attempt_t __uninitialized_ram(update_attempt);

static char latest[NU_TAG_MAX];     // "0.9.1"
static char latest_tag[NU_TAG_MAX]; // "fw-v0.9.1"
static char status[72] = "not checked";

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

// mbed tls's millisecond clock (MBEDTLS_PLATFORM_MS_TIME_ALT)
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)to_ms_since_boot(get_absolute_time());
}

//--------------------------------------------------------------------+
// versions
//--------------------------------------------------------------------+

/**
 * "0.9.1" -> its three numbers; false if the text is not such a version
 */
static bool version_parse(char const *text, unsigned long part[3])
{
    char *end;

    for (unsigned i = 0; i < 3; i++) {
        if ((*text < '0') || (*text > '9'))
            return false;
        part[i] = strtoul(text, &end, 10);
        if ((part[i] > 9999) || (*end != ((i < 2) ? '.' : 0)))
            return false;
        text = end + 1;
    }
    return true;
}

int net_update_newer(void)
{
    unsigned long theirs[3], ours[3];

    if (!version_parse(latest, theirs))
        return 0;
    if (!version_parse(ahp_version, ours))
        return -1;      // a build that is no release
    for (unsigned i = 0; i < 3; i++)
        if (theirs[i] != ours[i])
            return (theirs[i] > ours[i]) ? 1 : -1;
    return 0;
}

char const *net_update_latest(void)
{
    return latest;
}

char const *net_update_status(void)
{
    return status;
}

bool net_update_busy(void)
{
    return want_check || want_install || (job != JOB_NONE);
}

bool net_update_checking(void)
{
    return want_check || (job == JOB_CHECK);
}

//--------------------------------------------------------------------+
// the signature
//--------------------------------------------------------------------+

static bool signature_matches(uint8_t const digest[32], uint8_t const *key)
{
    mbedtls_ecdsa_context ecdsa;
    bool ok;

    mbedtls_ecdsa_init(&ecdsa);
    ok = (mbedtls_ecp_group_load(&ecdsa.MBEDTLS_PRIVATE(grp), MBEDTLS_ECP_DP_SECP256R1) == 0)
        && (mbedtls_ecp_point_read_binary(&ecdsa.MBEDTLS_PRIVATE(grp), &ecdsa.MBEDTLS_PRIVATE(Q), key,
            UPDATE_KEY_BYTES) == 0)
        && (mbedtls_ecp_check_pubkey(&ecdsa.MBEDTLS_PRIVATE(grp), &ecdsa.MBEDTLS_PRIVATE(Q)) == 0)
        && (mbedtls_ecdsa_read_signature(&ecdsa, digest, 32, signature, signature_len) == 0);
    mbedtls_ecdsa_free(&ecdsa);
    return ok;
}

static bool signature_ok(uint8_t const digest[32])
{
    for (unsigned i = 0; i < UPDATE_KEY_COUNT; i++)
        if (signature_matches(digest, update_keys[i]))
            return true;
    return false;
}

//--------------------------------------------------------------------+
// one request
//--------------------------------------------------------------------+

static void fetch(void);

// a connection given up from inside one of its own callbacks is aborted, and
// the callback then has to say so to lwip (which must not touch it again)
static struct altcp_pcb *in_callback;
static bool aborted_in_callback;

static void drop(void)
{
    if (pcb) {
        altcp_arg(pcb, NULL);
        altcp_recv(pcb, NULL);
        altcp_sent(pcb, NULL);
        altcp_err(pcb, NULL);
        altcp_poll(pcb, NULL, 0);
        if (pcb == in_callback) {
            altcp_abort(pcb);
            aborted_in_callback = true;
        } else if (altcp_close(pcb) != ERR_OK) {
            altcp_abort(pcb);
        }
        pcb = NULL;
    }
    stage = AT_IDLE;
}

static void callback_enter(struct altcp_pcb *conn)
{
    in_callback = conn;
    aborted_in_callback = false;
}

static err_t callback_leave(void)
{
    in_callback = NULL;
    return aborted_in_callback ? ERR_ABRT : ERR_OK;
}

static void done(void)
{
    drop();
    if (tls) {
        altcp_tls_free_config(tls);
        tls = NULL;
    }
    if (job == JOB_IMAGE)
        mbedtls_sha256_free(&hash);
    job = JOB_NONE;
    owed = 0;
}

static void fail(char const *why)
{
    if ((job == JOB_IMAGE) && body_seen)
        ota_abandon("the download failed");
    snprintf(status, sizeof(status), "failed: %s", why);
    ahprintf("[net] update %s\n", status);
    done();
}

/**
 * "https://host[:port]/path" into host, port and path
 */
static bool take_url(char const *url)
{
    char const *slash, *colon;
    size_t length;

    if (strncmp(url, "https://", 8) != 0)
        return false;
    url += 8;
    slash = strchr(url, '/');
    if (!slash)
        return false;
    colon = memchr(url, ':', (size_t)(slash - url));
    length = (size_t)((colon ? colon : slash) - url);
    if (!length || (length >= sizeof(host)) || (strlen(slash) >= sizeof(path)))
        return false;
    port = colon ? (uint16_t)strtoul(colon + 1, NULL, 10) : 443;
    // the path first: it may be read out of the buffer the host goes into
    memmove(path, slash, strlen(slash) + 1);
    memmove(host, url, length);
    host[length] = 0;
    return port != 0;
}

/**
 * the image is in: its signature, then the update side takes over
 */
static void image_complete(void)
{
    uint8_t digest[32];
    char message[96];

    mbedtls_sha256_finish(&hash, digest);
    if (!signature_ok(digest)) {
        ota_abandon("the signature does not match");
        snprintf(status, sizeof(status), "failed: the signature does not match");
        ahprintf("[net] update %s\n", status);
        done();
        return;
    }

    if (ota_finish(message, sizeof(message))) {
        snprintf(status, sizeof(status), "%s downloaded, signature good", latest);
        snprintf(handed_over, sizeof(handed_over), "%s", latest);
    }
    else
        snprintf(status, sizeof(status), "failed: %.60s", message);
    ahprintf("[net] update: %s\n", status);
    done();
}

/**
 * the response's body has ended (its length is through, or the server closed)
 */
static void body_complete(void)
{
    if (have_length && (body_seen != content_length)) {
        fail("the download was cut short");
        return;
    }

    if (job == JOB_SIGNATURE) {
        if (!signature_len) {
            fail("the release has no signature");
            return;
        }
        // now the image itself
        drop();
        job = JOB_IMAGE;
        redirects = 0;
        snprintf(host, sizeof(host), "%s", NET_UPDATE_HOST);
        port = NET_UPDATE_PORT;
        snprintf(path, sizeof(path), NET_UPDATE_REPO "/releases/download/%s/" NET_UPDATE_ASSET, latest_tag);
        mbedtls_sha256_init(&hash);
        mbedtls_sha256_starts(&hash, 0);
        snprintf(status, sizeof(status), "downloading %s", latest);
        fetch();
        return;
    }

    if (job == JOB_IMAGE) {
        image_complete();
        return;
    }

    fail("no release found");
}

/**
 * give image bytes back to the window, as far as the update side has room for
 * what may then arrive (http_server.c credit(), which see)
 */
static void credit(void)
{
    uint32_t give = owed, room;

    if (!give || !pcb || (job != JOB_IMAGE))
        return;
    if (ota_hold_window())
        return;

    room = (uint32_t)ota_space();
    if (room + owed <= TCP_WND)
        return;
    if (room + owed - TCP_WND < give)
        give = room + owed - TCP_WND;

    while (give) {
        u16_t step = (give > 0xffffu) ? 0xffffu : (u16_t)give;

        altcp_recved(pcb, step);
        owed -= step;
        give -= step;
    }
}

/**
 * a piece of the body. false if the request is over (failed or complete)
 */
static bool body(uint8_t const *data, size_t length)
{
    if (have_length && (length > content_length - body_seen))
        length = content_length - body_seen;

    if (job == JOB_SIGNATURE) {
        if (length > sizeof(signature) - signature_len) {
            fail("the signature is too long");
            return false;
        }
        memcpy(signature + signature_len, data, length);
        signature_len += length;
        body_seen += (uint32_t)length;
        altcp_recved(pcb, (u16_t)length);
    } else if (job == JOB_IMAGE) {
        if (!length)
            return true;
        if (!ota_receive(body_seen, data, length)) {
            char message[96];

            // the update side says why
            ota_finish(message, sizeof(message));
            snprintf(status, sizeof(status), "failed: %.60s", message);
            ahprintf("[net] update %s\n", status);
            done();
            return false;
        }
        mbedtls_sha256_update(&hash, data, length);
        body_seen += (uint32_t)length;
        owed += (uint32_t)length;
        credit();
    } else {
        altcp_recved(pcb, (u16_t)length);
    }

    if (have_length && (body_seen == content_length)) {
        body_complete();
        return false;
    }
    return true;
}

/**
 * the head is through: what the server said
 */
static bool head_complete(void)
{
    bool redirect = (http_status == 301) || (http_status == 302) || (http_status == 303) || (http_status == 307);

    if ((job == JOB_CHECK) && redirect && have_location) {
        // .../releases/download/<tag>/ninepin-ux.uf2
        char const *tag = strstr(path, "/releases/download/");
        char const *end = tag ? strchr(tag + 19, '/') : NULL;
        size_t length = end ? (size_t)(end - (tag + 19)) : 0;

        if (!length || (length >= sizeof(latest_tag)) || (strncmp(tag + 19, "fw-v", 4) != 0)) {
            fail("no release found");
            return false;
        }
        memcpy(latest_tag, tag + 19, length);
        latest_tag[length] = 0;
        snprintf(latest, sizeof(latest), "%s", latest_tag + 4);
        if (net_update_newer() > 0)
            snprintf(status, sizeof(status), "%s is available", latest);
        else if (net_update_newer() == 0)
            snprintf(status, sizeof(status), "up to date (%s)", latest);
        else
            snprintf(status, sizeof(status), "latest release is %s", latest);
        ahprintf("[net] update: %s\n", status);
        done();
        return false;
    }

    if (redirect && have_location) {
        if (++redirects > NU_REDIRECTS) {
            fail("too many redirects");
            return false;
        }
        drop();
        fetch();
        return false;
    }

    if (http_status != 200) {
        static char why[32];

        snprintf(why, sizeof(why), (http_status == 404) ? "no release found" : "the server said %d", http_status);
        fail(why);
        return false;
    }
    if (job == JOB_CHECK) {
        fail("no release found");
        return false;
    }
    if ((job == JOB_IMAGE) && have_length && (content_length > 8u * 1024u * 1024u)) {
        fail("the file is too big");
        return false;
    }

    stage = AT_BODY;
    body_seen = 0;
    return true;
}

/**
 * one line of the head. false if the request is over
 */
static bool head_line(void)
{
    if (!line_len)
        return head_complete();

    if (http_status == 0) {
        // "HTTP/1.1 302 Found"
        char const *space = strchr(line, ' ');

        http_status = space ? atoi(space + 1) : -1;
        if (http_status <= 0) {
            fail("not an http answer");
            return false;
        }
    } else if (strncasecmp(line, "Location:", 9) == 0) {
        char const *url = line + 9;

        while (*url == ' ')
            url++;
        // an address here, or a path on the same server
        if (*url == '/') {
            if (strlen(url) < sizeof(path)) {
                memmove(path, url, strlen(url) + 1);
                have_location = true;
            }
        } else {
            have_location = take_url(url);
        }
    } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
        content_length = (uint32_t)strtoul(line + 15, NULL, 10);
        have_length = true;
    }
    return true;
}

static err_t on_recv(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;

    callback_enter(conn);

    if (!p) {
        // the server closed
        if (stage == AT_BODY)
            body_complete();
        else
            fail("the server closed the connection");
        return callback_leave();
    }

    active_ms = now_ms();

    for (struct pbuf *q = p; q && (pcb == conn); q = q->next) {
        uint8_t const *data = q->payload;
        size_t length = q->len, used = 0;

        while ((used < length) && (pcb == conn) && (stage == AT_HEAD)) {
            char c = (char)data[used++];

            altcp_recved(conn, 1);
            if (c == '\n') {
                if (line_len && (line[line_len - 1] == '\r'))
                    line_len--;
                line[line_len] = 0;
                if (!head_line())
                    break;
                line_len = 0;
            } else if (line_len < sizeof(line) - 1) {
                line[line_len++] = c;
            }
        }
        if ((used < length) && (pcb == conn) && (stage == AT_BODY))
            body(data + used, length - used);
    }

    pbuf_free(p);
    return callback_leave();
}

static void on_err(void *arg, err_t err)
{
    (void)arg;
    (void)err;

    // lwip has freed the connection already
    pcb = NULL;
    if (job != JOB_NONE)
        fail("the connection broke");
}

static err_t on_connected(void *arg, struct altcp_pcb *conn, err_t err)
{
    static char request[NU_PATH_MAX + 200];
    int length;

    (void)arg;

    callback_enter(conn);

    if (err != ERR_OK) {
        fail("no connection");
        return callback_leave();
    }

    length = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: ninepin-ux/%s\r\n"
        "Accept: */*\r\nConnection: close\r\n\r\n", path, host, ahp_version);
    stage = AT_HEAD;
    line_len = 0;
    http_status = 0;
    have_location = false;
    have_length = false;
    content_length = 0;
    body_seen = 0;
    active_ms = now_ms();

    if ((altcp_write(conn, request, (u16_t)length, TCP_WRITE_FLAG_COPY) != ERR_OK) || (altcp_output(conn) != ERR_OK))
        fail("the request did not go out");
    return callback_leave();
}

static void connect_to(ip_addr_t const *address)
{
    pcb = altcp_tls_new(tls, IPADDR_TYPE_V4);
    if (!pcb) {
        fail("out of memory");
        return;
    }
    // the name the server picks its certificate by
    mbedtls_ssl_set_hostname(altcp_tls_context(pcb), host);
    altcp_arg(pcb, NULL);
    altcp_recv(pcb, on_recv);
    altcp_err(pcb, on_err);

    stage = AT_CONNECT;
    active_ms = now_ms();
    if (altcp_connect(pcb, address, port, on_connected) != ERR_OK)
        fail("no connection");
}

static void on_resolved(char const *name, ip_addr_t const *address, void *arg)
{
    (void)name;
    (void)arg;

    if ((job == JOB_NONE) || (stage != AT_RESOLVE))
        return;
    if (!address) {
        fail("no address for the server (no internet?)");
        return;
    }
    connect_to(address);
}

/**
 * host, port, path: go and get it. lwip context
 */
static void fetch(void)
{
    ip_addr_t address;
    err_t err;

    if (!tls)
        tls = altcp_tls_create_config_client(NULL, 0);
    if (!tls) {
        fail("out of memory");
        return;
    }

    stage = AT_RESOLVE;
    active_ms = now_ms();
    err = dns_gethostbyname(host, &address, on_resolved, NULL);
    if (err == ERR_OK)
        connect_to(&address);
    else if (err != ERR_INPROGRESS)
        fail("no address for the server (no internet?)");
}

//--------------------------------------------------------------------+
// public
//--------------------------------------------------------------------+

bool net_update_check(void)
{
    if (net_update_busy())
        return false;
    want_check = true;
    return true;
}

bool net_update_install(void)
{
    if (net_update_busy() || !latest_tag[0])
        return false;
    want_install = true;
    return true;
}

bool net_update_quiet(void)
{
    bool quiet = true;

    cyw43_arch_lwip_begin();
    if ((job == JOB_IMAGE) && pcb && (stage == AT_BODY)) {
        // under the tls layer: what the sender may still send
        struct altcp_pcb *inner = pcb->inner_conn;
        struct tcp_pcb *tcp = inner ? (struct tcp_pcb *)inner->state : NULL;

        if (tcp && (tcp->rcv_ann_wnd >= TCP_MSS))
            quiet = false;
    }
    cyw43_arch_lwip_end();
    return quiet;
}

void net_update_task(void)
{
    static bool looked;

    // once after start: is this the firmware the last download restarted into?
    if (!looked) {
        looked = true;
        if (update_attempt.magic == ATTEMPT_MAGIC) {
            update_attempt.version[sizeof(update_attempt.version) - 1] = 0;
            if (strcmp(update_attempt.version, ahp_version) != 0) {
                snprintf(status, sizeof(status), "%.20s was installed and fell back", update_attempt.version);
                ahprintf("[net] update: %s\n", status);
            }
        }
        update_attempt.magic = 0;
    }

    // a downloaded release on its way in: note it for the firmware that starts next
    if (handed_over[0]) {
        if (ota_installing()) {
            update_attempt.magic = ATTEMPT_MAGIC;
            snprintf(update_attempt.version, sizeof(update_attempt.version), "%s", handed_over);
            handed_over[0] = 0;
        } else if (!ota_busy()) {
            handed_over[0] = 0;     // refused, or no BOOT press
        }
    }

    if (want_check || want_install) {
        bool install = want_install;

        cyw43_arch_lwip_begin();
        want_check = want_install = false;
        if (job != JOB_NONE) {
            // one thing at a time
        } else if (!wifi_joined()) {
            snprintf(status, sizeof(status), "failed: needs Wi-Fi in join mode");
        } else if (install && ota_busy()) {
            snprintf(status, sizeof(status), "failed: another update is in progress");
        } else {
            redirects = 0;
            snprintf(host, sizeof(host), "%s", NET_UPDATE_HOST);
            port = NET_UPDATE_PORT;
            if (install) {
                job = JOB_SIGNATURE;
                signature_len = 0;
                snprintf(path, sizeof(path), NET_UPDATE_REPO "/releases/download/%s/" NET_UPDATE_ASSET ".sig",
                    latest_tag);
                snprintf(status, sizeof(status), "downloading %s", latest);
            } else {
                job = JOB_CHECK;
                snprintf(path, sizeof(path), NET_UPDATE_REPO "/releases/latest/download/" NET_UPDATE_ASSET);
                snprintf(status, sizeof(status), "checking");
            }
            ahprintf("[net] update: %s\n", status);
            fetch();
        }
        cyw43_arch_lwip_end();
    }

    if (job == JOB_NONE)
        return;

    cyw43_arch_lwip_begin();
    if (job != JOB_NONE) {
        credit();
        // the update side holding the window shut is no silence of the server's
        if ((job == JOB_IMAGE) && owed)
            active_ms = now_ms();
        if ((uint32_t)(now_ms() - active_ms) >= NU_TIMEOUT_MS)
            fail("no answer from the server");
    }
    cyw43_arch_lwip_end();
}

#else // !HAS_WIFI

bool net_update_check(void) { return false; }
bool net_update_install(void) { return false; }
bool net_update_busy(void) { return false; }
bool net_update_checking(void) { return false; }
char const *net_update_status(void) { return "no Wi-Fi in this firmware"; }
char const *net_update_latest(void) { return ""; }
int net_update_newer(void) { return 0; }
bool net_update_quiet(void) { return true; }
void net_update_task(void) { }

#endif // HAS_WIFI
