# rev7.1 parts

JLCPCB places the SMT parts (`../fab/`). Everything else is fitted by hand:

* `rev7.1-mouser-hand-10.csv`: Mouser BOM import for **10 boards**: both DE9s, both mini-DINs,
  USB-A, BOOT / RESET switches, the A500, SWD and encoder headers, the OLED socket and **L1**
  (Abracon AOTA-B201610S3R3-101-T; pad 1 / silk dot = the AOTA dot). No spares.
* **LED1, WS2812B-2020**: not at Mouser or Digi-Key. [LCSC C52917434](https://www.lcsc.com/product-detail/C52917434.html)
  or AliExpress; order a few extra. MSL 5: keep it sealed until fitting, or dry it a few hours at
  about 60 °C.
* `ibom.html`: interactive placement view, open in a browser.

Not on any BOM: the 0.96" SSD1306 OLED module (26 x 26 mm, holes 22.2 x 22.2 mm, header GND VCC
SCL SDA), four nylon M2 posts 11 mm with nylon screws and nuts, the optional EC11 encoder.
