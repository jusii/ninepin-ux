/**
 * this file is part of amigahid-pico, (c) 2021 just nine <nine@aphlor.org>
 * please locate the full source at https://github.com/borb/amigahid-pico
 *
 * released under the terms of the Eclipse Public License 2.0 (EPL-2.0).
 * please find the complete license text at https://spdx.org/licenses/EPL-2.0
 *
 * cd32 pad output on the de9 ports (boards with both ports wired for output, HAS_DE9_OUTPUTS).
 * ported from stefan reinauer's cd32.c; pio1 and its state machines 0-3 are
 * claimed by de9.c.
 */

#ifndef _PLATFORM_AMIGA_CD32_H
#define _PLATFORM_AMIGA_CD32_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#ifdef HAS_DE9_OUTPUTS

void cd32_load(void);       // put the programs into pio1
void cd32_unload(void);     // and take them out again (disables both ports)
void cd32_enable(unsigned port, bool enabled);
void cd32_update(unsigned port, uint16_t state);

#else

static inline void cd32_load(void) {}
static inline void cd32_unload(void) {}
static inline void cd32_enable(unsigned port, bool enabled) { (void)port; (void)enabled; }
static inline void cd32_update(unsigned port, uint16_t state) { (void)port; (void)state; }

#endif

#endif // _PLATFORM_AMIGA_CD32_H
