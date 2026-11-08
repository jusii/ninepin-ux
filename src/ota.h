/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * firmware updates into the A/B partition slots (HAS_OTA: every rev7 build).
 * every rev7 image is try-before-you-buy, so every rev7 build needs ota_task()
 * to accept itself; the upload side is only used where there is wi-fi.
 *
 * the upload: the http server (http_server.c) streams the file of a POST
 * /update to ota_receive(), chunk by chunk, as it arrives; nothing buffers the
 * whole file. a multipart form upload is unwrapped first, so the data is the
 * .uf2 file itself; any other body is passed on as it is. the server holds a
 * chunk back (and the tcp window shut) until ota_space() says it fits.
 *
 * ota_space(), ota_receive() and ota_finish() run in the cyw43 background
 * context (a low-priority interrupt on core0, holding the lwip lock), or in
 * the main loop under that lock: they never block, never write flash (that is
 * ota_task()'s job) and never call lwip. without HAS_OTA, weak defaults in
 * http_server.c refuse every update.
 */

#ifndef _OTA_H
#define _OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "config.h"

/**
 * @return how many bytes ota_receive() takes now
 */
size_t ota_space(void);

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

#ifdef HAS_OTA

/**
 * @brief Accept a firmware on trial after 10 s of clean main loop; write
 *        uploaded blocks to the free slot; time out an unconfirmed install.
 *        Main loop.
 */
void ota_task(void);

/**
 * @return true while this firmware runs on trial (a flash update boot of a
 *         try-before-you-buy image) and has not accepted itself yet
 */
bool ota_trial(void);

/**
 * @brief A BOOT button event: installs an update waiting for one.
 *
 * @return true if the event was taken for that
 */
bool ota_button(void);

/**
 * @return what the update side is doing, for the console and the page
 */
char const *ota_status(void);

/**
 * @return true if the previous firmware can be booted again
 */
bool ota_previous_available(void);

/**
 * @brief Boot the previous firmware (its first sector put back). Main loop.
 *
 * @return false with the reason in message; on success the board restarts
 */
bool ota_boot_previous(char *message, size_t size);

#else

static inline void ota_task(void) {}
static inline bool ota_trial(void) { return false; }
static inline bool ota_button(void) { return false; }
static inline char const *ota_status(void) { return "not available"; }
static inline bool ota_previous_available(void) { return false; }
static inline bool ota_boot_previous(char *message, size_t size) { (void)message; (void)size; return false; }

#endif // HAS_OTA

#endif // _OTA_H
