/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the settings page's html: the status, a form with every menu setting except
 * the board-only "updates need BOOT" (the remap slots read-only) plus the pad
 * combos, the page password form, the firmware upload form, the log-in page;
 * and reading the settings form back. plain server-rendered html,
 * no javascript. render and parse run in the cyw43 background context.
 */

#ifndef _WEB_PAGE_H
#define _WEB_PAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "settings.h"

/**
 * @brief Note what the page shows but cannot ask for from an interrupt (the
 *        boot slot). Main loop, once.
 */
void web_page_init(void);

/**
 * @brief The status and settings page.
 *
 * @return its length, or 0 if it did not fit
 */
size_t web_page_render(char *out, size_t size);

/**
 * @brief A short page: a title, one paragraph of text (escaped here), a link
 *        back to the settings.
 *
 * @return its length (cut short if it did not fit)
 */
size_t web_page_message(char *out, size_t size, char const *title, char const *text);

/**
 * @brief The log-in page (join mode asks for the page password).
 *
 * @param note  NULL, or a line to show above the form ("Wrong password.")
 * @return its length, or 0 if it did not fit
 */
size_t web_page_login(char *out, size_t size, char const *note);

/**
 * @brief One field of a form (application/x-www-form-urlencoded), decoded.
 *
 * @return false if it is not there or does not fit
 */
bool web_page_field(char const *body, char const *name, char *out, size_t size);

/**
 * @brief Read the settings form (application/x-www-form-urlencoded, decoded in
 *        place) over a copy of the settings in effect. The board-only setting
 *        and the page password are never taken from this form.
 *
 * @param error     why not, for the page
 * @return true if settings holds a valid set
 */
bool web_page_parse(char *body, settings_t *settings, char *error, size_t error_size);

#endif // _WEB_PAGE_H
