/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * the two de9 controller ports (boards with HAS_DE9_OUTPUTS): what each port is
 * in the current mode (settings: amiga_port[], c64_port[]), and the outputs for
 * it - the quadrature mouse (quad_mouse.c, either port), joysticks, cd32 pads
 * (cd32.c) and the commodore 1351 mouse.
 *
 *   amiga mode   mouse / joystick / cd32 pad / off per port
 *   c64 mode     joystick / 1351 / off per port
 *   usb mode     both quiet
 *
 * player 1 drives port 2 (the usual game port on both machines), player 2
 * port 1, when that port is a joystick or cd32 pad.
 *
 * the c64 joystick's extra fire buttons are on the pot lines, which carry a
 * 10k pull-up on this board: a released line reads as pressed, so "not
 * pressed" holds the line low and "pressed" lets it go. the 1351 emulation
 * holds a pot line low through the sid's discharge and beyond by the position,
 * then lets the pull-up charge it (c64_pot.pio). both are untested on a c64.
 */

#ifndef _PLATFORM_AMIGA_DE9_H
#define _PLATFORM_AMIGA_DE9_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#ifdef HAS_DE9_OUTPUTS

/**
 * @brief Claim pio1 (all of it: cd32 or 1351) and set the ports up for the
 *        current mode. After amiga_quad_mouse_init(), before anything else
 *        takes a pio state machine (bluetooth).
 */
void de9_init(void);

/**
 * @brief The mode or the port settings changed: set the ports up again.
 */
void de9_apply(void);

/**
 * @brief Autofire. Main loop.
 */
void de9_task(void);

/**
 * @brief Describe the ports for the console.
 */
void de9_describe(void);

/**
 * @return true if a port is a 1351 in the given stored settings (the mouse then
 *         goes there in c64 mode rather than to usb)
 */
bool de9_c64_has_1351(void);

#else

static inline void de9_init(void) {}
static inline void de9_apply(void) {}
static inline void de9_task(void) {}
static inline void de9_describe(void) {}
static inline bool de9_c64_has_1351(void) { return false; }

#endif

#endif // _PLATFORM_AMIGA_DE9_H
