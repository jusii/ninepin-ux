/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the settings page's http server: plain http/1.1 on port 80, lwip raw tcp,
 * one request per connection.
 *
 *   GET  /            status and the settings form (web_page.c)
 *   POST /settings    the form; handed to the main loop (wifi_page_submit())
 *   POST /update      a firmware file, streamed to ota_receive() (ota.h)
 *   GET  /login       the log-in form; POST /login opens a session
 *   POST /password    a new page password, or none asked for
 *   POST /logout      ends the session
 *   POST /profile-save, /profile-load, /profile-delete
 *                     the settings form's pad profile buttons (profiles.h)
 *
 * in join mode every request needs the page password, unless it is switched
 * off: a session cookie from the log-in form (web_auth.h), or http basic
 * auth (any user name) for scripts. posts carry a per-boot token from the
 * page, so another site open in the same browser cannot post to the board.
 *
 * everything runs in the cyw43 background context.
 */

#ifndef _HTTP_SERVER_H
#define _HTTP_SERVER_H

#include <stdbool.h>

/**
 * @brief Listen on port 80 (all interfaces). Idempotent. Lwip lock held.
 *
 * @return true if listening
 */
bool http_server_start(void);

/**
 * @brief Hand an upload's held-back data on once the update side has room for
 *        it. Main loop (takes the lwip lock itself).
 */
void http_server_task(void);

/**
 * @brief The token the page's forms carry (a per-boot random value).
 */
char const *http_server_token(void);

#endif // _HTTP_SERVER_H
