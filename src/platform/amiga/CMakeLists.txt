pico_generate_pio_header(amigahid-pico ${CMAKE_CURRENT_LIST_DIR}/keyboard.pio)
# real amiga keyboard input; compiled out by HAS_KBD_IN on boards without one
pico_generate_pio_header(amigahid-pico ${CMAKE_CURRENT_LIST_DIR}/keyboard_recv.pio)

# the de9 ports: cd32 pads (amiga) and the 1351 mouse (c64); compiled out by
# HAS_DE9_OUTPUTS on boards without both ports wired for output
pico_generate_pio_header(amigahid-pico ${CMAKE_CURRENT_LIST_DIR}/cd32.pio)
pico_generate_pio_header(amigahid-pico ${CMAKE_CURRENT_LIST_DIR}/c64_pot.pio)

target_sources(amigahid-pico PRIVATE cd32.c de9.c keyboard_serial_io.c keyboard_recv.c quad_mouse.c)
