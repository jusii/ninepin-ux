#pragma once
#include <stdint.h>
typedef struct { uint8_t modifier, reserved, keycode[6]; } hid_keyboard_report_t;
typedef struct { uint8_t buttons; int8_t x, y, wheel, pan; } hid_mouse_report_t;
#define HID_KEY_CONTROL_LEFT 0xE0
#define HID_KEY_GUI_RIGHT 0xE7
