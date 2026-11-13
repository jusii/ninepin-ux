/**
 * this file is part of ninepin-ux; released under the terms of the Eclipse
 * Public License 2.0 (EPL-2.0), https://spdx.org/licenses/EPL-2.0
 *
 * why the board last restarted, and how long it has been up. a hard fault and
 * the place the main loop had reached are kept in the watchdog's scratch
 * registers, which survive the restart, and read back at the next start.
 */

#ifndef _RESET_INFO_H
#define _RESET_INFO_H

#include <stddef.h>
#include <stdint.h>

// where the main loop is (main.c), for a watchdog restart to name
typedef enum {
    RESET_STAGE_BOOT = 0,
    RESET_STAGE_USB_HOST,
    RESET_STAGE_USB_DEVICE,
    RESET_STAGE_CONSOLE,
    RESET_STAGE_MENU,
    RESET_STAGE_PADS,
    RESET_STAGE_APPLY,
    RESET_STAGE_BLUETOOTH,
    RESET_STAGE_WIFI,
    RESET_STAGE_OTA,
    RESET_STAGE_AMIGA,
    RESET_STAGE_CONTROLS,
    RESET_STAGE_COUNT
} reset_stage_t;

/**
 * @brief First thing in main(): read what the last run left behind.
 */
void reset_info_init(void);

/**
 * @brief The main loop has reached this stage.
 */
void reset_info_stage(reset_stage_t stage);

/**
 * @return why the board started, e.g. "power on", "watchdog (in wifi)",
 *         "fault pc 1000abcd lr 1000ef01 cfsr 00000400 (in bluetooth)"
 */
char const *reset_info_reason(void);

/**
 * @return after a fault: a panic's message and the return addresses found on
 *         the stack (for addr2line); else ""
 */
char const *reset_info_detail(void);

/**
 * @brief "0:12:34 since power on" for the page and the console.
 */
void reset_info_status(char *out, size_t size);

#endif // _RESET_INFO_H
