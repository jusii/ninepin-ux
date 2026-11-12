# rev7.1 parts

With the SMT-only JLCPCB order (`../fab/`), everything else is fitted by hand. A fully assembled
order (`../fab/*-full-*.csv`) needs none of this.

* `rev7.1-mouser-hand.csv`: Mouser BOM import for **one board** (set the quantity in the cart):
  both DE9s, both mini-DINs, USB-A, BOOT / RESET switches, the A500, SWD and encoder headers, the
  OLED socket and **L1** (Abracon AOTA-B201610S3R3-101-T; pad 1 / silk dot = the AOTA dot).
* **LED1, WS2812B-2020**: not at Mouser or Digi-Key. [LCSC C52917434](https://www.lcsc.com/product-detail/C52917434.html)
  or AliExpress; order a few extra. MSL 5: keep it sealed until fitting, or dry it a few hours at
  about 60 °C.
* `ibom.html`: interactive placement view, open in a browser.

Not on any BOM: the 0.96" SSD1306 OLED module (26 x 26 mm, holes 22.2 x 22.2 mm, header GND VCC
SCL SDA), four nylon M2 x 11 mm posts with nylon M2 x 5 screws (female-female, or male-female with
the thread cut to 5 mm and a nut: see [board](../../../doc/board.md#oled)), the optional EC11
encoder.
