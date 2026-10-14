target_sources(amigahid-pico PRIVATE board_pins.c debug_cons.c output.c status_led.c)
# single ws2812 status led; only used on boards with HAS_STATUS_WS2812
pico_generate_pio_header(amigahid-pico ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
