// host test for the pad combos in pads.c
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "fake_sdk.h"
#include "tusb.h"
#include "pad.h"
#include "settings.h"
#include "menu.h"
#include "input_bridge.h"
#include "input_bridge_bt.h"
#include "util/run_mode.h"
uint64_t fake_now_us = 1000000;
static settings_t s;
settings_t const *settings_get(void) { return &s; }
void ahprintf(const char *f, ...) { (void)f; }
static bool menu_open;
bool menu_is_open(void) { return menu_open; }
void menu_input(menu_input_t i) { (void)i; }
run_mode_t run_mode_get(void) { return RUN_MODE_AMIGA; }
bool run_mode_pads_usb(run_mode_t m) { (void)m; return false; }
void input_bridge_handle_mouse(uint8_t slot, hid_mouse_report_t const *r) { (void)slot; (void)r; }
static hid_keyboard_report_t kb; static int kb_reports; static uint8_t kb_slot;
void input_bridge_handle_keyboard(uint8_t slot, hid_keyboard_report_t const *r, input_bridge_keyboard_sink_t const *k) { (void)k; kb = *r; kb_reports++; kb_slot = slot; }
static uint16_t de9[2];
void pads_output_de9(uint8_t p, uint16_t st) { de9[p] = st; }
int main(void)
{
    pad_input_t in;
    uint8_t d[7] = { 2, 3, 1, 4, 5, 6, 10 };   // a dualsense: cross circle square triangle l1 r1 options
    pads_init();
    pads_connected(0, 1, "pad", NULL);
    memset(&in, 0, sizeof(in)); in.hat = PAD_HAT_CENTRED; memcpy(in.defaults, d, 7);

    // no combos: cross fires, r1 is the cd32 r
    in.buttons = 1u << 1; pads_report(0, &in); assert(de9[0] == PAD_FIRE);
    in.buttons = 1u << 5; pads_report(0, &in); assert(de9[0] == PAD_R);
    assert(kb_reports == 0);

    // r1 -> down + fire (blue max's bomb); l2 -> space; l1 -> up + lshift
    s.pad_combo[0] = (settings_combo_t){ 6, PAD_DOWN | PAD_FIRE, 0 };
    s.pad_combo[1] = (settings_combo_t){ 7, 0, 0x2c };
    s.pad_combo[2] = (settings_combo_t){ 5, PAD_UP, 0xe1 };
    in.buttons = 1u << 5; pads_report(0, &in); assert(de9[0] == (PAD_DOWN | PAD_FIRE));
    // the stick held up and right: down wins on its axis, right stays
    in.ly = -127; in.lx = 127; pads_report(0, &in); assert(de9[0] == (PAD_DOWN | PAD_RIGHT | PAD_FIRE));
    in.buttons = 0; pads_report(0, &in); assert(de9[0] == (PAD_UP | PAD_RIGHT));
    in.lx = in.ly = 0;
    // two combos pulling opposite ways cancel
    in.buttons = (1u << 5) | (1u << 4); pads_report(0, &in); assert(de9[0] == PAD_FIRE);
    assert(kb_reports == 1 && kb.modifier == 0x02 && kb.keycode[0] == 0 && kb_slot == INPUT_BRIDGE_PAD_KEY_SLOT);
    // the key
    in.buttons = 1u << 6; pads_report(0, &in); assert(de9[0] == 0 && kb.keycode[0] == 0x2c && kb.modifier == 0 && kb_reports == 2);
    pads_report(0, &in); pads_task(); assert(kb_reports == 2);
    // the menu opening lets go of it; closed again, the pad counts once it was at rest
    menu_open = true; pads_task(); assert(kb.keycode[0] == 0 && kb_reports == 3);
    menu_open = false; pads_release_all(); pads_task(); assert(kb_reports == 3);
    pads_report(0, &in); assert(kb_reports == 3 && de9[0] == 0);
    in.buttons = 0; pads_report(0, &in);
    in.buttons = 1u << 6; pads_report(0, &in); assert(kb.keycode[0] == 0x2c && kb_reports == 4);
    // unplugged with the key down
    pads_disconnected(0); assert(kb.keycode[0] == 0 && kb_reports == 5);
    printf("pads: all passed\n");
    return 0;
}
