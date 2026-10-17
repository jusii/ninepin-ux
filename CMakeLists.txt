cmake_minimum_required(VERSION 3.13..3.27)

set(ENV{PICO_SDK_PATH} "${CMAKE_SOURCE_DIR}/pico-sdk/")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# pin external dependencies independently of the versions bundled with the SDK
if (NOT PICO_TINYUSB_PATH AND NOT DEFINED ENV{PICO_TINYUSB_PATH})
	set(PICO_TINYUSB_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb")
endif ()
if (NOT PICO_CYW43_DRIVER_PATH AND NOT DEFINED ENV{PICO_CYW43_DRIVER_PATH})
	set(PICO_CYW43_DRIVER_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/cyw43-driver")
endif ()

# amigahid-pico build target; see src/config.h BOARD_* defines; default to rev4
if (NOT BOARD_TYPE)
	set(BOARD_TYPE BOARD_HIDPICO_REV4)
endif ()

# which chip/pico board version is being used; unfortunately this can't be driven by config.h so needs to be set here.
# rev7 is chip-down (rp2350a + rm2) rather than a pico module, so it brings its own sdk board header from boards/
# (guarded: the sdk caches PICO_BOARD_HEADER_DIRS, so a bare append would grow on every reconfigure)
if (NOT "${CMAKE_CURRENT_LIST_DIR}/boards" IN_LIST PICO_BOARD_HEADER_DIRS)
	list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/boards")
endif ()
if (NOT PICO_BOARD)
	if (BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7")
		set(PICO_BOARD amigahid_rev7)
	else ()
		set(PICO_BOARD pico)
	endif ()
endif ()
if ((BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7") AND NOT (PICO_BOARD STREQUAL "amigahid_rev7"))
	message(FATAL_ERROR "BOARD_HIDPICO_REV7 is chip-down and needs PICO_BOARD=amigahid_rev7 (got '${PICO_BOARD}')")
endif ()
if ((PICO_BOARD STREQUAL "amigahid_rev7") AND NOT (BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7"))
	message(FATAL_ERROR "PICO_BOARD=amigahid_rev7 is only valid with BOARD_TYPE=BOARD_HIDPICO_REV7 (got '${BOARD_TYPE}')")
endif ()
if (NOT PICO_PLATFORM)
	# the pico 2 family and rev7 are rp2350; everything else defaults to rp2040
	if (PICO_BOARD STREQUAL "pico2" OR PICO_BOARD STREQUAL "pico2_w" OR PICO_BOARD STREQUAL "amigahid_rev7")
		set(PICO_PLATFORM rp2350-arm-s)
	else ()
		set(PICO_PLATFORM rp2040)
	endif ()
endif ()

# DISPLAY_SSD1306 (0.96" panels, default) or
# DISPLAY_SH1106 (1.3" panels; page-addressed, 132-wide ram with a 2px column offset)
if (NOT DISPLAY_CONTROLLER)
	set(DISPLAY_CONTROLLER DISPLAY_SSD1306)
endif ()
if (NOT (DISPLAY_CONTROLLER STREQUAL "DISPLAY_SSD1306" OR DISPLAY_CONTROLLER STREQUAL "DISPLAY_SH1106"))
	message(FATAL_ERROR "DISPLAY_CONTROLLER must be DISPLAY_SSD1306 or DISPLAY_SH1106 (got '${DISPLAY_CONTROLLER}')")
endif ()

# use sdk from git submodule
include(pico-sdk/pico_sdk_init.cmake)

project(amigahid-pico C CXX ASM)

add_compile_options(-Wall -Werror)

# configuration options

# uncomment for debug slurry
add_compile_definitions(DEBUG_MESSAGES=1)

# enable per-report mouse debug only when explicitly requested
option(DEBUG_MOUSE "Enable per-report mouse debug output" OFF)
if (DEBUG_MOUSE)
	add_compile_definitions(DEBUG_MOUSE=1)
endif ()

# enable per-key keyboard debug only when explicitly requested
option(DEBUG_KEYBOARD "Enable per-key keyboard debug output" OFF)
if (DEBUG_KEYBOARD)
	add_compile_definitions(DEBUG_KEYBOARD=1)
endif ()

# enable HID mount/status debug only when explicitly requested
option(DEBUG_HID_STATUS "Enable HID status debug output" OFF)
if (DEBUG_HID_STATUS)
	add_compile_definitions(DEBUG_HID_STATUS=1)
endif ()

# real amiga keyboard input timings (milliseconds), for bench tuning; see
# src/platform/amiga/keyboard_recv.c for the defaults and what they mean
if (KBDIN_RESET_HOLD_MS)
	add_compile_definitions(KBDIN_RESET_HOLD_MS=${KBDIN_RESET_HOLD_MS})
endif ()
if (KBDIN_RESET_HOLD_MAX_MS)
	add_compile_definitions(KBDIN_RESET_HOLD_MAX_MS=${KBDIN_RESET_HOLD_MAX_MS})
endif ()

# enable bluetooth hid host support when building for pico w
option(ENABLE_BLUETOOTH_HID "Enable Bluetooth HID host support on Pico W" OFF)
if (ENABLE_BLUETOOTH_HID)
	add_compile_definitions(ENABLE_BLUETOOTH_HID=1)
endif ()

# set the board revision (changes which pins are mapped to which ports)
add_compile_definitions(${BOARD_TYPE})

# set the display controller type (ssd1306 vs sh1106)
add_compile_definitions(${DISPLAY_CONTROLLER})

# controller port 2 role:
#   JOYSTICK - default; port 2 reserved for (future) joystick emulation, no-op today
#   KEYBOARD - repurpose port 2 as a real amiga keyboard input (rx via pio)
# in KEYBOARD mode port 2 is wired to a real amiga keyboard, NOT to the amiga.
if (NOT PORT2_ROLE)
	set(PORT2_ROLE JOYSTICK)
endif ()
if (NOT (PORT2_ROLE STREQUAL "JOYSTICK" OR PORT2_ROLE STREQUAL "KEYBOARD"))
	message(FATAL_ERROR "PORT2_ROLE must be JOYSTICK or KEYBOARD (got '${PORT2_ROLE}')")
endif ()
if ((BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV6" OR BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7") AND PORT2_ROLE STREQUAL "KEYBOARD")
	message(FATAL_ERROR "REV6/REV7 have dedicated keyboard input; PORT2_ROLE=KEYBOARD is a REV4 option")
endif ()
add_compile_definitions(PORT2_ROLE_${PORT2_ROLE}=1)
message(STATUS "Controller port 2 role: ${PORT2_ROLE}")

# debugging for tinyusb - be warned that it can cause timing issues causing things to break
# add_compile_definitions(CFG_TUSB_DEBUG=2)

# rev7 drives its usb-a host port with pico-pio-usb (vendored at lib/pico_pio_usb).
# tinyusb's rp2040 family cmake builds the tinyusb_pico_pio_usb library from
# PICO_PIO_USB_PATH when the sdk brings tinyusb in, so this must be set before
# pico_sdk_init(); the other boards never see it and keep the native host port.
if (BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7" AND NOT PICO_PIO_USB_PATH)
	set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico_pio_usb")
endif ()

pico_sdk_init()

if (BOARD_TYPE STREQUAL "BOARD_HIDPICO_REV7" AND NOT TARGET tinyusb_pico_pio_usb)
	message(FATAL_ERROR "BOARD_HIDPICO_REV7 needs pico-pio-usb at ${PICO_PIO_USB_PATH}; try 'git submodule update --init lib/pico_pio_usb'")
endif ()

if (ENABLE_BLUETOOTH_HID AND NOT PICO_CYW43_SUPPORTED)
	message(FATAL_ERROR "ENABLE_BLUETOOTH_HID requires a CYW43 board such as pico_w")
endif ()

add_subdirectory(src)
