add_executable(amigahid-pico
  input_bridge.c
  main.c
  usb_hid.c
)

# rev7: usb device on the native phy (usb-c), host on pio-usb (usb-a)
if (BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7")
  target_sources(amigahid-pico PRIVATE
    usb_descriptors.c
    usb_device.c
  )
endif ()

if (ENABLE_BLUETOOTH_HID)
  target_sources(amigahid-pico PRIVATE
    bt_hid.c
  )
endif ()

add_subdirectory(platform)
add_subdirectory(util)
add_subdirectory(display)

# make the src subdirectory availabe in the include path so tinyusb can find tusb_config.h
target_include_directories(amigahid-pico PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(amigahid-pico PUBLIC
  pico_stdlib
  pico_util
  pico_multicore
  hardware_dma
  hardware_gpio
  hardware_i2c
  hardware_pio
  tinyusb_host
)

if (BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7")
  # no uart on rev7: the debug console is a cdc interface on the usb-c device
  # port (usb_device.c), and main() does its own bring-up instead of the tinyusb
  # bsp's board_init(), which would force clk_sys to 156 MHz and expect a
  # PICO_DEFAULT_PIO_USB_DP_PIN from the board header
  pico_enable_stdio_uart(amigahid-pico 0)
  target_link_libraries(amigahid-pico PUBLIC
    tinyusb_device
    tinyusb_pico_pio_usb
    hardware_adc
    pico_unique_id
  )
else ()
  pico_enable_stdio_uart(amigahid-pico 1)
  target_link_libraries(amigahid-pico PUBLIC
    tinyusb_board
  )
endif ()

if (ENABLE_BLUETOOTH_HID)
  target_compile_options(amigahid-pico PRIVATE
    -Wno-error=maybe-uninitialized
  )
  target_link_libraries(amigahid-pico PUBLIC
    pico_cyw43_arch_none
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_cyw43
  )
endif ()

if (ENABLE_BLUETOOTH_HID AND COMMAND suppress_btstack_warnings)
  suppress_btstack_warnings()
endif ()

add_compile_definitions(PICO_USE_MALLOC_MUTEX=0)

pico_add_extra_outputs(amigahid-pico)
