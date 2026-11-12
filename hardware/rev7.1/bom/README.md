# rev7.1 parts

Mouser BOM imports, each for **one board** (set the quantity in the cart):

* `rev7.1-mouser-hand.csv`: with the SMT-only JLCPCB order (`../fab/`), the parts fitted by hand:
  both DE9s, both mini-DINs, USB-A, BOOT / RESET switches, the A500, SWD and encoder headers, the
  OLED socket, **L1** (Abracon AOTA-B201610S3R3-101-T; pad 1 / silk dot = the AOTA dot) and the
  optional encoder.
* `rev7.1-mouser-full.csv`: every part of the board from Mouser, for building it without JLCPCB
  assembly (RP2350A, RM2, flash and all passives included), plus the optional encoder.
* A fully assembled JLCPCB order (`../fab/*-full-*.csv`) needs neither, only the encoder.

Not at Mouser:

* **LED1, WS2812B-2020**: [LCSC C52917434](https://www.lcsc.com/product-detail/C52917434.html)
  or AliExpress; order a few extra. MSL 5: keep it sealed until fitting, or dry it a few hours at
  about 60 °C.
* The 0.96" SSD1306 OLED module (26 x 26 mm, holes 22.2 x 22.2 mm, header GND VCC SCL SDA), four
  nylon M2 x 11 mm posts with nylon M2 x 5 screws (female-female, or male-female with the thread
  cut to 5 mm and a nut: see [board](../../../doc/board.md#oled)).

The encoder is a Bourns PEC11R (M7 bushing through the case lid, 15 mm shaft, push switch), wired
to J7: see [board](../../../doc/board.md#encoder-optional). Any EC11 with a push switch works
electrically.

`ibom.html`: interactive placement view, open in a browser.
