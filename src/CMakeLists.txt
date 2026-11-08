add_executable(amigahid-pico
  apply.c
  ds5.c
  hid_gamepad.c
  input_bridge.c
  keyboard_map.c
  main.c
  menu.c
  pads.c
  settings.c
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
    bt_ds4.c
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
  hardware_flash
  hardware_gpio
  hardware_i2c
  hardware_pio
  pico_flash
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

if (BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7")
  # rev7 flash is split into A/B partition slots for over-the-air updates
  # (partition_table_rev7.json): A at 8K-4M, B at 4M up to 12K short of 8M; the
  # top 20K of the 16M flash stays unpartitioned for settings, the btstack
  # pairing bank and the RP2350-E10 block. partition-table.uf2 is a one-time
  # install: drop it onto the BOOTSEL drive before the first firmware uf2.
  # after that, every firmware uf2 lands in the free slot and the bootrom
  # boots it straight away (a "flash update" boot).
  set(AHP_PT_JSON ${CMAKE_CURRENT_LIST_DIR}/partition_table_rev7.json)
  set(AHP_PT_UF2 ${CMAKE_CURRENT_BINARY_DIR}/partition-table.uf2)
  add_custom_command(OUTPUT ${AHP_PT_UF2}
    COMMAND picotool partition create --quiet ${AHP_PT_JSON} ${AHP_PT_UF2}
    DEPENDS ${AHP_PT_JSON}
    COMMENT "Creating the rev7 partition table uf2"
    VERBATIM)
  add_custom_target(amigahid-pico-partition-table ALL DEPENDS ${AHP_PT_UF2})

  # the smaller slot (B) bounds the image
  add_custom_command(TARGET amigahid-pico POST_BUILD
    COMMAND ${CMAKE_COMMAND} -DIMAGE=$<TARGET_FILE_DIR:amigahid-pico>/amigahid-pico.bin
      -DLIMIT_KB=4084 -P ${CMAKE_CURRENT_LIST_DIR}/check_image_size.cmake
    VERBATIM)
endif ()
