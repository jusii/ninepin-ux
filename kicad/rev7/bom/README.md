# rev7 parts

* `rev7-mouser-bom-1.csv`, `rev7-digikey-bom-1.csv`: BOM-import files for one board. Multiply
  the quantities for more.
* `ibom.html`: interactive assembly view, open in a browser.
* JLCPCB assembly BOM and CPL: `../fab/`.

Not stocked at Mouser or Digi-Key:

* **LED1, WS2812B-2020** (status LED): [LCSC C52917434](https://www.lcsc.com/product-detail/C52917434.html),
  or AliExpress, search "WS2812B-2020".
* At pricing time Digi-Key also had no stock of D10-D30 (PESD5V0S1BA) and J4 (Molex 67643-2910).

Not on the board BOM:

* **OLED:** 0.96" SSD1306 128x64 I2C module, 26 x 26 mm, holes 22.2 x 22.2 mm, e.g.
  [AliExpress 1005011855265244](https://www.aliexpress.com/item/1005011855265244.html) (the
  drawing on that page is wrong). Header order GND VCC SCL SDA.
* Four nylon M2 posts, 11 mm, with nylon screws and nuts.
* Encoder (optional): EC11 with push switch.
