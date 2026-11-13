/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the page password, sessions and the lockout; see web_auth.h.
 */

#include "web_auth.h"
#include "config.h"

#include <stdio.h>
#include <string.h>

#include "pico/rand.h"
#include "pico/stdlib.h"

#include "settings.h"
#include "util/output.h"

#define AUTH_SESSIONS           4
#define AUTH_LOCKOUT_SLOTS      8
#define AUTH_LOCKOUT_FAILS      5
#define AUTH_LOCKOUT_MS         30000u

typedef struct {
    uint32_t ip;
    uint8_t fails;
    uint32_t until;
} lockout_t;

static char sessions[AUTH_SESSIONS][WEB_AUTH_TOKEN_LEN + 1];
static unsigned next_session;
static lockout_t lockouts[AUTH_LOCKOUT_SLOTS];

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static bool same(char const *a, size_t a_len, char const *b)
{
    size_t b_len = strlen(b);
    uint8_t diff = 0;

    for (size_t i = 0; i < b_len; i++)
        diff |= (uint8_t)(b[i] ^ ((i < a_len) ? a[i] : 0));
    return !diff && (a_len == b_len);
}

bool web_auth_password(char const *given, size_t length)
{
    char const *password = settings_get()->page_password;

    // none yet: wifi_task() makes one
    return *password && same(given, length, password);
}

char const *web_auth_password_fault(char const *password)
{
    size_t length = strlen(password);

    if ((length < WEB_AUTH_PASSWORD_MIN) || (length >= SETTINGS_PAGE_PASSWORD_LEN))
        return "A page password has 8 to 16 characters.";
    for (size_t i = 0; i < length; i++)
        if ((password[i] <= ' ') || (password[i] > '~'))
            return "A page password is made of letters, digits and punctuation, without spaces.";
    return NULL;
}

//--------------------------------------------------------------------+
// sessions
//--------------------------------------------------------------------+

char const *web_auth_session_open(void)
{
    unsigned slot = next_session;

    for (unsigned i = 0; i < AUTH_SESSIONS; i++)
        if (!sessions[i][0]) {
            slot = i;
            break;
        }
    // all taken: they go round, the oldest first
    if (sessions[slot][0])
        next_session = (slot + 1) % AUTH_SESSIONS;

    snprintf(sessions[slot], sizeof(sessions[slot]), "%08lx%08lx", (unsigned long)get_rand_32(),
        (unsigned long)get_rand_32());
    return sessions[slot];
}

/**
 * the session a Cookie header's value names ("a=1; auth_token=...; b=2")
 *
 * @return its slot, or -1
 */
static int session_of(char const *cookie, size_t length)
{
    while (cookie && length) {
        char const *equals, *end;

        while (length && (*cookie == ' ')) {
            cookie++;
            length--;
        }
        end = memchr(cookie, ';', length);
        if (!end)
            end = cookie + length;
        equals = memchr(cookie, '=', (size_t)(end - cookie));
        if (equals && ((size_t)(equals - cookie) == 10) && !strncmp(cookie, "auth_token", 10))
            for (unsigned i = 0; i < AUTH_SESSIONS; i++)
                if (sessions[i][0] && same(equals + 1, (size_t)(end - equals - 1), sessions[i]))
                    return (int)i;
        if (end >= cookie + length)
            break;
        length -= (size_t)(end + 1 - cookie);
        cookie = end + 1;
    }
    return -1;
}

bool web_auth_session(char const *cookie, size_t length)
{
    return session_of(cookie, length) >= 0;
}

void web_auth_session_close(char const *cookie, size_t length)
{
    int slot = session_of(cookie, length);

    if (slot >= 0)
        sessions[slot][0] = 0;
}

void web_auth_sessions_clear(void)
{
    memset(sessions, 0, sizeof(sessions));
}

//--------------------------------------------------------------------+
// the lockout
//--------------------------------------------------------------------+

static lockout_t *lockout_for(uint32_t ip, bool create)
{
    lockout_t *spare = NULL;

    for (unsigned i = 0; i < AUTH_LOCKOUT_SLOTS; i++) {
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

bool web_auth_locked_out(uint32_t ip)
{
    lockout_t *l = lockout_for(ip, false);

    return l && l->until && (int32_t)(now_ms() - l->until) < 0;
}

void web_auth_result(uint32_t ip, bool ok)
{
    lockout_t *l = lockout_for(ip, !ok);

    if (!l)
        return;
    if (ok) {
        l->fails = 0;
        return;
    }
    if (++l->fails >= AUTH_LOCKOUT_FAILS) {
        l->fails = 0;
        l->until = now_ms() + AUTH_LOCKOUT_MS;
        ahprintf("[web] too many wrong passwords from %lu.%lu.%lu.%lu: locked out for 30 s\n",
            (unsigned long)(ip & 0xff), (unsigned long)((ip >> 8) & 0xff),
            (unsigned long)((ip >> 16) & 0xff), (unsigned long)(ip >> 24));
    }
}
