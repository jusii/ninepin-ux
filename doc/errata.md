# Errata

## rev7.1

Not built yet. If U11 holds the board in reset or it still starts in BOOTSEL: remove U11 and fit
the rev7 fix below.

## rev7

* **Cold plug-in may start in BOOTSEL.** RUN has no delay, so the chip can start before the flash
  is ready. Fix: solder **1 µF** and **820 kΩ** in parallel across **SW2 (RESET) pins 1-2**, on
  the back. Any small SMD (0805/1206) or through-hole part fits across the switch pins. RUN reads
  about 3.2 V while running. Fixed in rev7.1 by U11.
* **LED1 (WS2812B-2020) is not stocked at Mouser or Digi-Key:**
  [LCSC C52917434](https://www.lcsc.com/product-detail/C52917434.html), or AliExpress.

## both

* **OLED module listings show wrong dimensions.** The board takes a 26 x 26 mm module with holes
  22.2 x 22.2 mm; see [board](./board.md#oled). Nylon posts and screws only.
* **Check U7's pins after assembly** (SN74CBTD3861, under the OLED). An open pin there kills a
  single joystick direction and is hard to reach once the OLED is fitted.
