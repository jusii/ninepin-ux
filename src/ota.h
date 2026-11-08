/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * firmware updates from the settings page (boards with HAS_WIFI). the http
 * server (http_server.c) streams the file of a POST /update to these, chunk by
 * chunk, as it arrives; nothing buffers the whole file. a multipart form
 * upload is unwrapped first, so the data is the .uf2 file itself; any other
 * body is passed on as it is.
 *
 * both run in the cyw43 background context (a low-priority interrupt on core0,
 * holding the lwip lock): they must not block for long, must not write flash
 * themselves (that is the main loop's job), and must not call lwip.
 *
 * the weak defaults below (http_server.c) refuse every update; the ota code
 * replaces them.
 */

#ifndef _OTA_H
#define _OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief The next piece of an upload. Offset 0 starts a new upload: forget
 *        anything from an earlier one, which may have been cut short (a
 *        dropped connection ends an upload without ota_finish()). One upload
 *        at a time.
 *
 * @return false to refuse the update: the server passes no more of this
 *         upload on, reads the rest of the body and drops it, then calls
 *         ota_finish() for the reason
 */
bool ota_receive(uint32_t offset, uint8_t const *data, size_t len);

/**
 * @brief The body has ended. Called once per upload that reached
 *        ota_receive(), also after it refused; not called if the connection
 *        dropped or a multipart body ended without its closing boundary.
 *
 * @param message   one line for the page: what happened, what comes next
 * @return true if the update was taken
 */
bool ota_finish(char *message, size_t size);

#endif // _OTA_H
