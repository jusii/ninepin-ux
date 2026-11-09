#pragma once
extern int fake_lwip_locked;
static inline void cyw43_arch_lwip_begin(void) { fake_lwip_locked++; }
static inline void cyw43_arch_lwip_end(void) { fake_lwip_locked--; }
