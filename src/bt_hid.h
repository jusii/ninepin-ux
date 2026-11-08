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

void bt_hid_init(void);
void bt_hid_task(void);

/**
 * @return the bluetooth state as the oled shows it ("bt c1 le:ready"), or why
 *         there is none
 */
char const *bt_hid_status(void);

#endif // _BT_HID_H
