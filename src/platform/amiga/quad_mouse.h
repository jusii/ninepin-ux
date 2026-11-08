/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * amiga quadrature mouse interface.
 */

#ifndef _PLATFORM_AMIGA_QUAD_MOUSE_H
#define _PLATFORM_AMIGA_QUAD_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

enum amiga_quad_mouse_buttons { AQM_LEFT, AQM_MIDDLE, AQM_RIGHT };

void amiga_quad_mouse_init();
void amiga_quad_mouse_motion();
void amiga_quad_mouse_button(enum amiga_quad_mouse_buttons button, bool pressed);
void amiga_quad_mouse_set_motion(int16_t in_x, int16_t in_y);
void amiga_quad_mouse_wheel(int8_t wheel);

/**
 * @brief Put the mouse on de9 port 0 (port 1) or 1 (port 2), or -1 for none.
 *        Core1 moves between quadrature steps; this waits until it has.
 *
 * @return false if core1 had not moved within timeout_us
 */
bool amiga_quad_mouse_port(int8_t port, uint32_t timeout_us);

/**
 * @return the port the mouse is on, -1 for none
 */
int8_t amiga_quad_mouse_get_port(void);

/**
 * @brief Send motion and buttons somewhere else (the c64 1351); NULLs restore
 *        the quadrature mouse.
 */
void amiga_quad_mouse_redirect(void (*motion)(int16_t x, int16_t y),
    void (*button)(enum amiga_quad_mouse_buttons button, bool pressed));

#endif
