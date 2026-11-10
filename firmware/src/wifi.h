/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * wi-fi on the rm2 (boards with HAS_WIFI): the settings page over plain http.
 *
 *   off            the default; the wlan side of the radio stays idle
 *   access point   open network "amigahid-xxxx" (the last four hex digits of
 *                  the board id), the board at 192.168.4.1 handing out
 *                  addresses (dhcp_server.c), no page password. it switches
 *                  itself off after 15 minutes without a client, and comes back
 *                  with the next settings save or the next boot
 *   join network   station on wifi_ssid / wifi_password, address by dhcp,
 *                  answering as amigahid-xxxx.local (mdns); every request needs
 *                  the page password (http basic auth, any user name)
 *
 * the mode follows the settings live. everything network-side runs in the
 * cyw43 background context; the page hands a save to the main loop
 * (wifi_task()), which stores and applies it.
 */

#ifndef _WIFI_H
#define _WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"
#include "settings.h"

#ifdef HAS_WIFI

#define WIFI_AP_IDLE_MS     (15u * 60u * 1000u)

/**
 * @brief Name the board, start the radio if the settings want wi-fi. Once,
 *        after bt_hid_init(), with the settings loaded.
 */
void wifi_init(void);

/**
 * @brief Main loop: follow the settings, watch the link and the access
 *        point's clients, store a save from the page.
 */
void wifi_task(void);

/**
 * @brief Make a new page password, store it. Main loop.
 *
 * @return true if stored
 */
bool wifi_new_page_password(void);

/**
 * @return "amigahid-xxxx": the access point's name, the mdns host name
 */
char const *wifi_name(void);

/**
 * @return the state in a line ("access point amigahid-1a2b, 1 client",
 *         "joined home, 192.168.1.23"), for the page. Any context.
 */
char const *wifi_status(void);

/**
 * @return what became of the last save from the page ("stored 12 s ago",
 *         "not stored: ..."), for the page. Any context.
 */
char const *wifi_page_result(void);

/**
 * @brief The state for the console's wifi command. Main loop.
 */
void wifi_describe(void);

/**
 * @return true if requests need the page password (join mode)
 */
bool wifi_page_needs_password(void);

/**
 * @brief Hand settings from the page to the main loop, which validates,
 *        stores and applies them. Cyw43 background context.
 *
 * @return false if a previous save is still waiting
 */
bool wifi_page_submit(settings_t const *settings);

#else

static inline void wifi_init(void) {}
static inline void wifi_task(void) {}

#endif // HAS_WIFI

#endif // _WIFI_H
