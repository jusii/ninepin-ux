add_executable(ninepin-ux
  apply.c
  ds5.c
  hid_gamepad.c
  input_bridge.c
  keyboard_map.c
  main.c
  ota.c
  menu.c
  pads.c
  profiles.c
  settings.c
  settings_names.c
  usb_descriptors.c
  usb_device.c
  usb_hid.c
)

if (ENABLE_BLUETOOTH_HID)
  target_sources(ninepin-ux PRIVATE
    bt_ds4.c
    bt_hid.c
    radio.c
  )
  # wi-fi and the settings page on the same radio (HAS_WIFI, config.h)
  target_sources(ninepin-ux PRIVATE
    dhcp_server.c
    http_server.c
    kvmd_api.c
    kvmd_keys.c
    web_auth.c
    web_page.c
    wifi.c
  )
endif ()

# the build time, stamped on every build (build_stamp.h)
set(AHP_STAMP_C ${CMAKE_CURRENT_BINARY_DIR}/build_stamp.c)
add_custom_target(ninepin-ux-build-stamp
  COMMAND ${CMAKE_COMMAND} -DOUT=${AHP_STAMP_C} -P ${CMAKE_CURRENT_LIST_DIR}/build_stamp.cmake
  BYPRODUCTS ${AHP_STAMP_C}
  VERBATIM)
target_sources(ninepin-ux PRIVATE ${AHP_STAMP_C})
add_dependencies(ninepin-ux ninepin-ux-build-stamp)

add_subdirectory(platform)
add_subdirectory(util)
add_subdirectory(display)

# make the src subdirectory availabe in the include path so tinyusb can find tusb_config.h
target_include_directories(ninepin-ux PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ninepin-ux PUBLIC
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

# no uart: the debug console is a cdc interface on the usb-c device port
# (usb_device.c), and main() does its own bring-up instead of the tinyusb bsp's
# board_init(), which would force clk_sys to 156 MHz and expect a
# PICO_DEFAULT_PIO_USB_DP_PIN from the board header
pico_enable_stdio_uart(ninepin-ux 0)
target_link_libraries(ninepin-ux PUBLIC
  tinyusb_device
  tinyusb_pico_pio_usb
  hardware_adc
  pico_unique_id
)

if (ENABLE_BLUETOOTH_HID)
  target_compile_options(ninepin-ux PRIVATE
    -Wno-error=maybe-uninitialized
  )
  target_link_libraries(ninepin-ux PUBLIC
    pico_btstack_ble
    pico_btstack_classic
    pico_btstack_cyw43
  )
  # lwip (lwipopts.h) in the same background context as btstack
  target_link_libraries(ninepin-ux PUBLIC
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mdns
    pico_rand
  )
endif ()

if (ENABLE_BLUETOOTH_HID AND COMMAND suppress_btstack_warnings)
  suppress_btstack_warnings()
endif ()

add_compile_definitions(PICO_USE_MALLOC_MUTEX=0)

pico_add_extra_outputs(ninepin-ux)

# the flash is split into A/B partition slots for over-the-air updates
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
  COMMENT "Creating the partition table uf2"
  VERBATIM)
add_custom_target(ninepin-ux-partition-table ALL DEPENDS ${AHP_PT_UF2})

# every image is try-before-you-buy: a flash update boot runs it under
# the bootrom's 16.7 s watchdog and it stays only once it accepts itself
# (ota.c, after ten seconds of clean main loop); otherwise the previous
# firmware boots again
target_compile_definitions(ninepin-ux PRIVATE PICO_CRT0_IMAGE_TYPE_TBYB=1)

# the smaller slot (B) bounds the image
add_custom_command(TARGET ninepin-ux POST_BUILD
  COMMAND ${CMAKE_COMMAND} -DIMAGE=$<TARGET_FILE_DIR:ninepin-ux>/ninepin-ux.bin
    -DLIMIT_KB=4084 -P ${CMAKE_CURRENT_LIST_DIR}/check_image_size.cmake
  VERBATIM)
