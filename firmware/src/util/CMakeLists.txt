target_sources(amigahid-pico PRIVATE board_pins.c debug_cons.c output.c status_led.c)
target_sources(amigahid-pico PRIVATE amiga_sense.c console_cmd.c io_expander.c run_mode.c user_button.c)
# the single ws2812 status led
pico_generate_pio_header(amigahid-pico ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)
