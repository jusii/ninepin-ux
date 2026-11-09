# amigahid-pico-UX

USB and Bluetooth keyboards, mice and game controllers on a Commodore Amiga (and C64), with the
real Amiga keyboard passed through alongside them. **Network control:** keyboard and mouse over
Wi-Fi through a PiKVM-compatible API, so scripts and LLM agents can drive the machine. Over USB-C
the Amiga keyboard and everything else also work on a PC or Mac. RP2040 / RP2350 firmware and
carrier boards that plug into the Amiga's keyboard and controller ports.

A fork of [borb/amigahid-pico](https://github.com/borb/amigahid-pico) with its own boards.

![UX rev7](./doc/images/ux-rev7-photo.jpg)

## boards

| board | what | state |
|---|---|---|
| **UX rev7** (`kicad/rev7/`) | RP2350A + RM2 radio chip-down, 4 layers, 100 x 62 mm | prototypes built, firmware in test |
| UX rev 1 (`kicad/rev6/`, branch `board-rev6`) | carrier for a Pico W / Pico 2 W, 2 layers | built, works |
| rev 2-5 (`kicad/`) | upstream boards | frozen |

## features (rev7)

* **Keyboard out** to the Amiga: mini-DIN6 (A4000D); DIN5 machines (A2000, A3000, A4000T) with a
  plain DIN5-to-mini-DIN6 adapter cable; A500 header.
* **Real Amiga keyboard in** (mini-DIN6 or A500 header), merged with USB and Bluetooth input.
  Ctrl-Amiga-Amiga resets the Amiga for as long as it is held.
* **USB host** (USB-A, hubs): keyboards, mice, game controllers.
* **Bluetooth** (RM2 / CYW43439): Classic and LE keyboards, mice and controllers. Pairing
  automatic or paired-only with a 2-minute window; forget all.
* **Game controllers:** PS5 DualSense (USB, Bluetooth), PS4 DualShock 4 (Bluetooth), PS3
  DualShock 3 (USB), generic HID pads. Two players, 30 s seat hold on replug, swap, merge,
  deadzone, d-pad / stick choice, triangle as up or autofire, DualSense touchpad as mouse, PS
  button opens the menu.
* **Three modes**, remembered across power cycles:
  * `amiga`: keyboards to the Amiga, mouse and pads to the DE9 ports.
  * `c64`: keyboard and mouse to USB-C (e.g. C64 Ultimate), pads to the DE9 ports.
  * `usb`: the board is a USB adapter for a PC or Mac: the Amiga keyboard, USB / Bluetooth
    keyboards and mice, and pads as two USB gamepads, all on USB-C.
* **DE9 ports, Amiga:** mouse on either port (TankMouse/Cocolino wheel, speed setting),
  joystick (3 fire buttons), CD32 pad (7 buttons).
* **DE9 ports, C64:** joystick with 2nd/3rd fire on the POT lines, 1351 mouse (untuned).
* **USB-C device:** keyboard, mouse, two gamepads and a serial console. The A4000 keyboard works
  as a USB keyboard: numpad ( ) in US, Nordic/DE, French or keypad style; Ctrl-Amiga-Amiga can
  send Ctrl-Alt-Del.
* **Keyboard settings:** Help / Del / ISO key mapping, Y-Z and AZERTY presets, 8 remaps per
  direction taught by keypress.
* **Menu** on the OLED, driven from a keyboard, the BOOT button, a rotary encoder, a PlayStation
  pad or the console.
* **Wi-Fi:** access point (`amigahid-xxxx`, 192.168.4.1) or joins a network (mDNS
  `amigahid-xxxx.local`, page password). Settings page with every menu setting.
* **Network control:** a subset of the PiKVM `kvmd` HTTP API (keys, text in six keymaps, mouse),
  so PiKVM / GL.iNet KVM clients and MCP servers work with it. Input follows the mode: to the
  Amiga, a C64 Ultimate or a PC. Typing is paced to what the target accepts.
* **Firmware updates:** A/B slots; every image runs on trial and keeps itself only after 10 s of
  clean running, otherwise the previous one boots. Upload from the settings page (BOOT press
  optional), "previous firmware" in the menu. A running board enters BOOTSEL on a 1200 baud
  touch, no button needed.
* Watchdog, settings in flash, WS2812 status LED.

## start here

* [rev7 instructions](./doc/rev7-guide.md): first install, modes, menu map, pads, pairing, Wi-Fi, updates.
* [rev7 board guide](./doc/ux-rev7.md): connectors, headers, power, parts.
* [errata](./doc/errata.md): read before building.
* [UX rev 1 board guide](./doc/ux-rev1.md), [installation](./doc/installation.md) (older boards).
* Fabrication: `kicad/rev7/fab/` (gerbers, JLCPCB BOM/CPL), parts: `kicad/rev7/bom/`,
  case: `kicad/rev7/case/`.

## credits

* **nine (borb)**: the original [amigahid](https://github.com/borb/amigahid) and
  [amigahid-pico](https://github.com/borb/amigahid-pico): the idea, the Amiga keyboard and
  quadrature code, the rev 2-5 boards.
* **Stefan Reinauer**: Bluetooth HID host, mouse-motion rewrite, input bridge, SDK/TinyUSB/CYW43
  update ([#44](https://github.com/borb/amigahid-pico/pull/44),
  [#47](https://github.com/borb/amigahid-pico/pull/47),
  [#48](https://github.com/borb/amigahid-pico/pull/48),
  [#49](https://github.com/borb/amigahid-pico/pull/49)). The settings store, gamepad parser, PS3,
  DualShock 4, CD32 and menu input handling are ported from his
  [reinauer/amigahid-pico](https://github.com/reinauer/amigahid-pico).
* **fboldog**: SH1106 1.3" OLED support ([#46](https://github.com/borb/amigahid-pico/pull/46)).
* **BennehBoy**, **rfuest**: keyboard fixes upstream.
* µgui by Achim Döbler, see the [display readme](./src/display/README.md).

An LLM assistant was used throughout this project, for the board layout, the checker scripts,
the firmware and these docs. Every result was reviewed, built and tested by hand.

## license

Eclipse Public License 2.0 (EPL-2.0), inherited from upstream.

Jusii: boards, keyboard passthrough, rev7 firmware, this fork.
