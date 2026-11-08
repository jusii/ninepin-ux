/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * putting the general settings into effect: mouse speed, display, watchdog and
 * the de9 ports. at boot and after every save; settings read where they are
 * used (keyboard maps, pads, bluetooth) need nothing here.
 */

#ifndef _APPLY_H
#define _APPLY_H

/**
 * @brief Put the stored settings into effect. Main loop.
 */
void apply_settings(void);

/**
 * @brief Keep the watchdog fed. Every main loop pass.
 */
void apply_task(void);

/**
 * @brief Report a restart by the watchdog (call once, early at boot).
 */
void apply_report_watchdog_reboot(void);

#endif // _APPLY_H
