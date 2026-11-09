/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * bluetooth hid host integration for pico w builds.
 */

#ifndef _BT_HID_H
#define _BT_HID_H

#include <stdbool.h>
#include <stdint.h>

void bt_hid_init(void);

/**
 * @brief Main loop: hands queued input on, and follows the bluetooth settings
 *        (radio on/off, pairing policy) and the requests below.
 */
void bt_hid_task(void);

/**
 * @brief Allow new pairings for two minutes, also under "paired only", and
 *        look for a classic pad right away. Main loop.
 */
void bt_hid_pair_window(void);

// why searching is paused; it stays paused while any reason holds
#define BT_HID_PAUSE_UPLOAD     0x01    // a firmware upload over wi-fi
#define BT_HID_PAUSE_AP         0x02    // the wi-fi access point is up

/**
 * @brief Stop (true) or resume (false) searching for new devices, classic
 *        inquiry and le scanning, for one reason, keeping the links that are
 *        up: wi-fi shares the radio and loses frames while bluetooth searches.
 *        A pairing window searches regardless. Main loop.
 */
void bt_hid_pause_scanning(uint8_t reason, bool pause);

/**
 * @brief Drop every connection, forget all classic and le bonds, and bring
 *        bluetooth back up (if it is on). Main loop.
 */
void bt_hid_forget_all(void);

/**
 * @return the bluetooth state as the oled shows it ("bt c1 le:ready"), or why
 *         there is none
 */
char const *bt_hid_status(void);

/**
 * @brief Print the state, pairing policy, bonds and connections for the
 *        console. Main loop.
 */
void bt_hid_describe(void);

#endif // _BT_HID_H
