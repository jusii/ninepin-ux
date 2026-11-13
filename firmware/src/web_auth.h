/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * who may use the settings page and the network-control api: the page
 * password, the sessions a login hands out (an auth_token cookie, kvmd's
 * name for it) and the lockout after wrong passwords. shared by http_server.c
 * and kvmd_api.c; everything runs in the cyw43 background context.
 */

#ifndef _WEB_AUTH_H
#define _WEB_AUTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEB_AUTH_TOKEN_LEN      16
#define WEB_AUTH_PASSWORD_MIN   8

/**
 * @return true if given is the page password (compared without an early way out)
 */
bool web_auth_password(char const *given, size_t length);

/**
 * @return NULL if the text can be a page password, else what is wrong with it
 */
char const *web_auth_password_fault(char const *password);

/**
 * @brief Open a session.
 *
 * @return its token (WEB_AUTH_TOKEN_LEN characters); the oldest session goes
 *         when all are taken
 */
char const *web_auth_session_open(void);

/**
 * @return true if a Cookie header's value carries a live session
 */
bool web_auth_session(char const *cookie, size_t length);

/**
 * @brief Close the sessions a Cookie header's value names.
 */
void web_auth_session_close(char const *cookie, size_t length);

/**
 * @brief Close every session (the page password changed).
 */
void web_auth_sessions_clear(void);

/**
 * @return true whilst an address is locked out after wrong passwords
 */
bool web_auth_locked_out(uint32_t ip);

/**
 * @brief Count a right or wrong password from an address (ipv4, network order).
 */
void web_auth_result(uint32_t ip, bool ok);

#endif // _WEB_AUTH_H
