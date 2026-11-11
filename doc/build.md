# Building Ninepin UX

Ready-made firmware: [Releases](https://github.com/jusii/ninepin-ux/releases). Flashing it:
[user guide](./user-guide.md#first-install).

## firmware

Needs cmake, make, `arm-none-eabi-gcc` and git (Debian/Ubuntu: `cmake gcc-arm-none-eabi`;
macOS: Homebrew; Windows: WSL). The pico-sdk builds its own `picotool`.

```shell
git clone --recursive https://github.com/jusii/ninepin-ux.git
cd ninepin-ux
cmake -S firmware -B build
cmake --build build
```

Output in `build/src/`: `ninepin-ux.uf2` (the firmware) and `partition-table.uf2` (first install
only). An existing clone needs `git submodule update --init --recursive`.

### options

| cmake option | default | what |
|---|---|---|
| `ENABLE_BLUETOOTH_HID` | `ON` | Bluetooth and Wi-Fi on the RM2; `OFF` builds without the radio stack |
| `DISPLAY_CONTROLLER` | `DISPLAY_SSD1306` | `DISPLAY_SH1106` for 1.3" OLED modules |
| `CMAKE_BUILD_TYPE` | `Release` | `Debug` for SWD debugging with openocd / gdb |
| `DEBUG_KEYBOARD`, `DEBUG_MOUSE`, `DEBUG_HID_STATUS` | `OFF` | per-event debug output on the console |
| `AHP_BRINGUP` | `OFF` | board bring-up: early USB console, boot-stage breadcrumbs, fault trap |

The library submodules in `firmware/lib/` are pinned apart from the pico-sdk. Another checkout:
`-DPICO_TINYUSB_PATH=...`, `-DPICO_CYW43_DRIVER_PATH=...`, `-DPICO_PIO_USB_PATH=...`.

SWD (J2) works too: openocd with the RP2350 target, see
[Getting started with Raspberry Pi Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf).

## board

Everything for an order is in [`hardware/rev7.1/`](../hardware/rev7.1/). Read the
[errata](./errata.md) first.

### PCB and assembly (JLCPCB)

* Upload `fab/ninepin-ux-rev7.1-gerbers.zip`: 4 layers, 1.6 mm, 100 x 62 mm. Tick "remove order
  number" (the back silk carries the board text instead).
* Assembly, pick one pair:
  * SMT only: `fab/ninepin-ux-rev7.1-bom.csv` and `-cpl.csv`. Economic PCBA (green solder mask);
    the connectors, switches, headers, L1 and LED1 are then fitted by hand (below).
  * Fully assembled: `fab/ninepin-ux-rev7.1-full-bom.csv` and `-full-cpl.csv`. Standard PCBA with
    THT assembly; nothing left to fit by hand. Not ordered this way yet: check the connectors in
    the preview too.
* **Check every polarised part in JLC's placement preview** before ordering (diodes, C4, ICs,
  transistors, Y1). The rotation offsets in `fab/make_fab.py` come from the rev7 preview. JLC's
  model for U9 (RM2) is smaller than the module and sits off-centre in the preview; pin 1 is the
  castellation at the silk triangle.

### hand-fitted parts

* `bom/rev7.1-mouser-hand.csv`: Mouser BOM import for **one board**; set the quantity in the cart.
  Both DE9s, both mini-DINs, USB-A, BOOT / RESET, the A500, SWD and encoder headers, the OLED
  socket and L1.
* LED1, WS2812B-2020: not at Mouser or Digi-Key; [LCSC C52917434](https://www.lcsc.com/product-detail/C52917434.html)
  or AliExpress. MSL 5: keep it sealed until fitting, or dry it a few hours at about 60 °C.
* Not on any BOM: the OLED module ([board](./board.md#oled)), four nylon M2 posts 11 mm with
  nylon screws and nuts, the optional EC11 encoder.
* `bom/ibom.html`: interactive placement view, open in a browser.

### case

[`hardware/case/`](../hardware/case/): print `ninepin-ux-case-tray.stl` and
`ninepin-ux-case-cap.stl`. The board drops into the tray; four M3 screws go up through the tray
and the board into M3 heat-set inserts (4.0 mm hole, up to 6 mm long) in the cap. Same case for
rev7 and rev7.1.

### changing the board

The board is generated from Python (`design.py`, `make_sch.py`, `make_board.py`, `route.py`) and
checked by `tools/check_rev7_1_*.py`. Commands: [`hardware/rev7.1/README.md`](../hardware/rev7.1/README.md).
