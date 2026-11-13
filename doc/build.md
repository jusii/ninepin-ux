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
| `NINEPIN_VERSION` | `dev` | the release number, `N.N.N`; CI sets it from the tag |

The library submodules in `firmware/lib/` are pinned apart from the pico-sdk. Another checkout:
`-DPICO_TINYUSB_PATH=...`, `-DPICO_CYW43_DRIVER_PATH=...`, `-DPICO_PIO_USB_PATH=...`.

SWD (J2) works too: openocd with the RP2350 target, see
[Getting started with Raspberry Pi Pico](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf).

### releases

A `fw-v<N.N.N>` tag makes a release: CI builds it with that version number, signs
`ninepin-ux.uf2` and attaches the signature as `ninepin-ux.uf2.sig`. Boards find the latest full
release by themselves when asked (pre-releases are not offered) and install it only if the
signature matches a key in `firmware/src/update_keys.h`.

* The signing key is an ECDSA P-256 private key in the repository secret `RELEASE_SIGNING_KEY`
  (PEM). The firmware carries two public keys: that one and a spare whose private half is kept
  offline, for a release that has to replace the first.
* New keys: `openssl ecparam -name prime256v1 -genkey -noout -out key.pem`, the public half with
  `openssl ec -in key.pem -pubout -out key.pub.pem`, then
  `python3 tools/release/make_update_keys.py main.pub.pem spare.pub.pem`.
* Signing by hand: `openssl dgst -sha256 -sign key.pem -out ninepin-ux.uf2.sig ninepin-ux.uf2`.
* A fork that wants its boards to follow its own releases changes `NET_UPDATE_REPO` in
  `net_update.c` and the keys. A bench server instead of GitHub: `-DNET_UPDATE_HOST=...
  -DNET_UPDATE_PORT=...`.

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
  socket, L1 and the optional encoder (Bourns PEC11R).
* `bom/rev7.1-mouser-full.csv`: every part of the board from Mouser, for a build without JLCPCB
  assembly (LED1 excepted).
* LED1, WS2812B-2020: not at Mouser or Digi-Key; [LCSC C52917434](https://www.lcsc.com/product-detail/C52917434.html)
  or AliExpress. MSL 5: keep it sealed until fitting, or dry it a few hours at about 60 °C.
* Not on any BOM: the OLED module, four nylon M2 x 11 mm posts and their nylon screws (and nuts
  for male-female posts; [board](./board.md#oled)).
* `bom/ibom.html`: interactive placement view, open in a browser.

### case

[`hardware/case/`](../hardware/case/): print `ninepin-ux-case-tray.stl` and
`ninepin-ux-case-cap.stl`. The board drops into the tray; four M3 screws go up through the tray
and the board into M3 heat-set inserts (4.0 mm hole, up to 6 mm long) in the cap. Same case for
rev7 and rev7.1.

Two colours (optional): `ninepin-ux-case-cap-logo.stl` (lid logo, 0.6 mm deep, print the cap lid
down) and `ninepin-ux-case-tray-labels.stl` (wall labels) fill the recesses flush; add them as
parts of the cap and the tray with a second filament. `ninepin-ux-case.step` holds all four
bodies. Sized for a 0.5 mm nozzle or finer. In one colour the logo and labels stay debossed.

### changing the board

The board is generated from Python (`design.py`, `make_sch.py`, `make_board.py`, `route.py`) and
checked by `tools/check_rev7_1_*.py`. Commands: [`hardware/rev7.1/README.md`](../hardware/rev7.1/README.md).
