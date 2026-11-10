/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * plain-text commands on the usb-c cdc console.
 */

#ifndef _UTIL_CONSOLE_CMD_H
#define _UTIL_CONSOLE_CMD_H

#include "config.h"

/**
 * @brief Run a command line typed on the console, if one is waiting.
 *        Main loop only: commands may write flash or reboot.
 */
void console_cmd_task(void);

#endif // _UTIL_CONSOLE_CMD_H
