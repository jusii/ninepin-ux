# Raspberry Pi Radio Module 2 (RM2, RMC20452T): data for drawing a footprint and symbol

## Sources

| Tag | Document | Where it is |
|---|---|---|
| **PB** | RM2 Product Brief, published Sept 2025, 7 pp. | `rm2/brief/brief.pdf` (copied from the webfetch result) |
| **DS** | *Radio Module 2 Datasheet*, RP-008943-DS-7, Release 7, built 20/02/2026, 17 pp. Fetched from `https://rpltd.co/rm2-datasheet`, which redirects to pip-assets.raspberrypi.com | `rm2/ds/rm2-ds.pdf` |
| **SCH** | Raspberry Pi Pico 2 W schematic, 2 pp. A3, (c) 2024 | `rm2/p2w/pico-2-w-schematic.pdf` |
| **P2W** | Raspberry Pi Pico 2 W datasheet, 24 pp. | `rm2/p2w/pico-2-w-datasheet.pdf` |

Page numbers are written as **PDF page / printed page**. In the DS the printed page number is the PDF page minus 1.

The PB has no land pattern. Its only drawing (PB p.5) gives 14.5 x 16.5 x 2.55 and is marked "should not be used for producing production data". The land pattern below comes from **DS Figure 6 (PDF p.10 / printed p.9)**.

I tried to fetch the Pico 2 W design-files zip (`datasheets.raspberrypi.com/picow/pico-2-w-design-files.zip`) and got HTTP 404. The KiCad sources were therefore **not** checked. Only the schematic PDF was used.

---

## 1. Mechanical data and land pattern

### Outline
- **W x L x H = 14.5 x 16.5 x 2.55 mm** (W along the bottom pad row, L along the side pad columns). Sources: DS p.8/7 Fig. 4, DS p.9/8 text, PB p.5. All three call the dimensions "approximate".
- PCB material is FR4 with an ENIG finish (DS p.8/7). The RF shield can sits over the lower part of the module. The printed inverted-F antenna is in the **top 5 mm strip** (DS p.8/7 Fig. 3 photo and Fig. 4).

### Pads
- **21 pads at 1.5 mm pitch.** Each pad is a castellation, a plated half-hole on the module edge (DS p.8/7: "21-pin castellated package consisting of 1.5 mm pitch gold contacts ... plated half-holes").
- Pad groups: 7 down the left edge (pins 1-7), 7 along the bottom edge (pins 8-14), 7 up the right edge (pins 15-21). The top edge, where the antenna is, has no pads.
- **No bottom-only pad and no exposed ground pad** appears in any figure or text. DS Fig. 6 shows only the 21 edge pads. However, neither document includes a photo or drawing of the module underside, so the absence of underside pads is **not explicitly confirmed**.
- **Pin 1** is the top pad of the left edge, the one nearest the antenna. Numbering runs counter-clockwise when viewed from the top (DS p.10/9 text, DS p.11/10 Fig. 7).

### Land pad size (DS Fig. 6, p.10/9)
- Side pads: **2.0 mm** perpendicular to the edge x **1.0 mm** along the edge. Each pad sits **0.5 mm outside** the module outline and **1.5 mm inside** it.
- Bottom pads: **1.0 mm** along the edge x **2.0 mm** perpendicular to it. Same 0.5 mm out / 1.5 mm in.
- Labelled dimensions in Fig. 6: 1.0 (pad width), 2.0 (pad length), 0.5 (overhang beyond the outline, given for both side and bottom pads), 1.5 (pitch), 2.75 (module left edge to the pin 8 centre), 1.8 (module bottom edge to the pin 15 centre, the lowest right-hand pad), 14.5, 16.5, 35.5, 18.5, 30.0.
- Fig. 6 does **not** dimension the vertical position of the side pads directly. I derived it from 1.8 + 6 x 1.5. I also measured the 300 dpi render with a pixel script: scale 32.0 px/mm, cross-checked against both 14.5 and 16.5 to within 0.1 %. The measurement agrees with every labelled value to within +/-0.05 mm, and the two sides and the bottom row are symmetric.

### Pad centres
The origin is the **module outline centre**, using KiCad axes (+X right, +Y down) and a top view. The antenna edge is at **y = -8.25**, the bottom pad edge at y = +8.25, and the outline spans x = +/-7.25.

| Pad | x (mm) | y (mm) | Pad size x x y (mm) |
|---|---|---|---|
| 1 | -6.75 | -2.55 | 2.0 x 1.0 |
| 2 | -6.75 | -1.05 | 2.0 x 1.0 |
| 3 | -6.75 | 0.45 | 2.0 x 1.0 |
| 4 | -6.75 | 1.95 | 2.0 x 1.0 |
| 5 | -6.75 | 3.45 | 2.0 x 1.0 |
| 6 | -6.75 | 4.95 | 2.0 x 1.0 |
| 7 | -6.75 | 6.45 | 2.0 x 1.0 |
| 8 | -4.50 | 7.75 | 1.0 x 2.0 |
| 9 | -3.00 | 7.75 | 1.0 x 2.0 |
| 10 | -1.50 | 7.75 | 1.0 x 2.0 |
| 11 | 0.00 | 7.75 | 1.0 x 2.0 |
| 12 | 1.50 | 7.75 | 1.0 x 2.0 |
| 13 | 3.00 | 7.75 | 1.0 x 2.0 |
| 14 | 4.50 | 7.75 | 1.0 x 2.0 |
| 15 | 6.75 | 6.45 | 2.0 x 1.0 |
| 16 | 6.75 | 4.95 | 2.0 x 1.0 |
| 17 | 6.75 | 3.45 | 2.0 x 1.0 |
| 18 | 6.75 | 1.95 | 2.0 x 1.0 |
| 19 | 6.75 | 0.45 | 2.0 x 1.0 |
| 20 | 6.75 | -1.05 | 2.0 x 1.0 |
| 21 | 6.75 | -2.55 | 2.0 x 1.0 |

Each pad's extent is centre +/- half its size:
- Side pads span x = 5.75 to 7.75 (left side mirrored).
- Bottom pads span y = 6.75 to 8.75.
- The pad envelope is therefore 15.5 x 11.8 mm: x +/-7.75, y from -3.05 to +8.75.

The corner gap between pin 7 (x up to -5.75, y up to 6.95) and pin 8 (x from -5.0, y from 6.75) is 0.75 mm in x. The pads do not collide.

If you prefer an origin at the pin 1 centre, subtract (-6.75, -2.55) from every coordinate.

---

## 2. Antenna keep-out (DS Fig. 6, p.10/9; DS p.9/8 and p.12/11 text)

- **Antenna position:** on the top short edge, the one opposite the bottom pad row and above pins 1 and 21.
- **Keep-out size: 35.5 mm wide x 18.5 mm deep.** It is centred on the module, so it extends **10.5 mm beyond each side** of the module.
- **Keep-out depth relative to the module:** it starts **5.0 mm inside** the module, measured from the antenna edge, and reaches **13.5 mm past** the antenna edge. 30.0 - 18.5 = 11.5 mm from the bottom edge, and 16.5 - 11.5 = 5.0 mm. The 30.0 dimension is the overall height from the bottom pad edge of the module to the far keep-out edge.
- **Keep-out rectangle in the origin above:** x = -17.75 to +17.75, y = -21.75 to -3.25.
- The keep-out ends 0.2 mm above the top edge of pins 1 and 21 (y = -3.05). That figure is derived, not dimensioned.
- **Layers:** DS p.9/8 says the keep-out is "the minimum region around the pins that must remain free of any metal". DS p.12/11 says "The area around the antenna must be clear of grounding to prevent shielding and de-tuning of the antenna." Neither names specific layers. "Any metal" implies all copper layers, and in practice also no components, mounting hardware or metal enclosure in that area. **No explicit per-layer statement was found.**
- **Board-edge rule: NOT FOUND.** Neither document states a required distance from, or overhang past, the carrier edge. The keep-out is drawn as an area of the carrier, not as an overhang. The rule implied is to keep 35.5 x 18.5 mm free of metal. If the carrier board is cut away inside that rectangle, that also satisfies "free of metal", but this is not stated.
- DS p.7/6 refers antenna radiation patterns to PIP (pip.raspberrypi.com, category 602 "antenna-patterns"), which needs a login. Not fetched.
- The P2W guidance for the Pico 2 W's different (Abracon) antenna is "antenna in free space ... grounded metal to the sides can improve bandwidth" (P2W p.18/17). It is not RM2-specific.
- Ground (DS p.12/11): "RM2 requires a good ground plane. Connect the ground of RM2 to the ground of the host system. The better the ground connections ... the better the RF performance."

---

## 3. Pin table (DS Table 2, PDF p.11 / printed p.10)

The "Default connection" column is the Pico W default from the datasheet. The CYW43439 signal column is my inference from matching the RM2 pin descriptions and resistor values against the Pico 2 W schematic (section 4). It is **not** stated in the DS.

| Pad | Name (DS) | Type (for the symbol) | Default host connection | CYW43439 signal (inferred) |
|---|---|---|---|---|
| 1 | GND | power_in | GND | - |
| 2 | NC | no_connect | - | - |
| 3 | gSPI SCLK | input | GPIO29 | SDIO_CLK |
| 4 | GND | power_in | GND | - |
| 5 | gSPI Data In | input (bidir) | GPIO24 direct | SDIO_CMD |
| 6 | gSPI Data Out | output | GPIO24 via **470 Ω** | SDIO_DATA0 |
| 7 | GND | power_in | GND | - |
| 8 | GPIO0 | bidirectional | (WL_GPIO0, LED on Pico W) | GPIO0 |
| 9 | gSPI CS | input | GPIO25 | SDIO_DATA3 |
| 10 | nIRQ | output | GPIO24 via **10 kΩ** | SDIO_DATA1 |
| 11 | GND | power_in | GND | - |
| 12 | WL_ON (Wi-Fi on) | input | GPIO23 | WL_REG_ON |
| 13 | BT_ON (Bluetooth on) | input | GPIO23 (tied to pin 12) | BT_REG_ON |
| 14 | VDDIO (1.8 V or 3.3 V) | power_in | 3.3 V | WCC_VDDIO / IO supply |
| 15 | GND | power_in | GND | - |
| 16 | VIN (3.0 to 4.8 V) | power_in | 3.3 V | VDDBAT (via the module's own SR/LDO) |
| 17 | GPIO2 | bidirectional | (WL_GPIO2, VBUS sense on Pico W) | GPIO2 |
| 18 | GPIO1 | bidirectional | (WL_GPIO1, SMPS PS on Pico W) | GPIO1 |
| 19 | NC | no_connect | - | - |
| 20 | NC | no_connect | - | - |
| 21 | GND | power_in | GND | - |

On the RM2 the three CYW GPIOs are free for general use. They are controlled over gSPI and work only while the radio is powered and communicating (DS p.7/6).

Electrical data (DS p.12/11):
- VIN must be 3.0 to 4.8 V. Below 3.2 V, RF performance drops.
- VDDIO is 1.8 V +/-5 % or 3.3 V +/-5 %.
- At 3.3 V IO: VIH >= 2.0 V, VIL <= 0.8 V, VOH >= VDDIO - 0.4 V and VOL <= 0.4 V at 2 mA.
- ESD: HBM 1000 V, MM 30 V, CDM 300 V.
- Current (DS p.4/3): TX at MCS7 / 16 dBm is 271 mA, RX is 43 mA, PM1 DTIM1 is 1.19 mA. Budget at least about 300 mA on VIN.

---

## 4. Host connection map: Pico 2 W and RP2350A (SCH p.1 and p.2; DS Table 1 p.6/5)

The **Pico 2 W does not use the RM2 module.** It carries a discrete CYW43439KUBG (U4) with its own antenna. The RM2 contains the same chip, regulator, crystal and antenna internally. Only the host-side parts of the Pico 2 W circuit apply to an RM2 carrier. DS Table 1 confirms the same mapping for the RM2.

| RM2 pad | Net (Pico 2 W) | RP2350A GPIO | RP2350A QFN-60 pin (SCH p.1) | Parts between them |
|---|---|---|---|---|
| 3 SCLK | WL_CLK | GPIO29 (ADC3) | 43 | none. On Pico 2 W the same pin also reads VSYS/3 through a divider that is enabled while WL_CS is high (P2W p.18/17). This is Pico-specific and can be omitted. |
| 5 Data In | WL_D | GPIO24 | 36 | direct |
| 6 Data Out | WL_D | GPIO24 | 36 | **470 Ω 1 %** series (R22) |
| 10 nIRQ | WL_D | GPIO24 | 36 | **10 kΩ 1 %** series (R23) |
| 9 CS | WL_CS | GPIO25 | 37 | direct |
| 12 WL_ON, 13 BT_ON | WL_ON | GPIO23 | 35 | direct. Both REG_ON pins are tied together on the WL_ON net. No pull resistor is visible on the schematic. |
| 14 VDDIO | 3V3 | - | - | On Pico 2 W the CYW IO (WCC_VDDIO) is on VDDBAT, which is 3V3 through FB1 (BLM18AG601SN1, 600 Ω at 100 MHz) with C24 4u7 X5R. C35 100n sits on WCC_VDDIO. |
| 16 VIN | 3V3 | - | - | Same VDDBAT net (FB1 + C24 4u7) |
| 8 / 18 / 17 GPIO0/1/2 | WL_GPIO0/1/2 | - | - | LED (via 470R), RT6154 PS pin, VBUS sense (Pico-specific) |

On the Pico 2 W, three signals share the single host pin GPIO24: Data In (direct), Data Out (through 470 Ω) and nIRQ (through 10 kΩ). The SDK's PIO gSPI driver handles this. **The 470 Ω and 10 kΩ resistors sit on the carrier board, outside the RM2.** DS Table 1 (p.6/5) and Table 2 (p.11/10) both say "via a 470 Ω resistor" and "via a 10 kΩ resistor".

Other notes:
- The Pico 2 W schematic also ties SDIO_DATA2 to the WL_D/CMD net (the gSPI mode strap) and grounds BT_DEV_WAKE. The RM2 does not bring these out, so they are presumably handled inside the module. This is inferred, not stated.
- **Supply arrangement and 3V3 enable:**
  - On the Pico 2 W everything runs from the single 3V3 rail made by the RT6154 buck-boost (U2). 3V3_EN is that regulator's EN pin.
  - The radio has no separate supply switch. Power-up and reset are controlled entirely by WL_ON (GPIO23) driving WL_REG_ON and BT_REG_ON.
  - No 1.8 V rail is used. VDDIO is 3.3 V.
  - For an RM2 carrier: feed pin 16 VIN from 3V3 (or anything from 3.0 to 4.8 V), feed pin 14 VDDIO from the host IO rail (3.3 V for RP2040/RP2350 at 3.3 V IO), and drive pins 12 and 13 together from one GPIO.
- **External decoupling for the RM2: NOT SPECIFIED** in the DS. The DS says only "minimal external components (requiring only SPI, power, and reset signals)" (p.4/3). Mirroring the Pico 2 W gives a reasonable starting point: 4u7 bulk plus 100n at VIN, 100n at VDDIO, and optionally a 600 Ω ferrite on VIN. That is a suggestion, not a documented requirement.
- **GPIO assignment is not fixed:**
  - The DS says the defaults "match the Pico W configuration. You can change the default GPIO assignments at build time" through the `CYW43_DEFAULT_PIN_*` defines (DS p.6/5, p.11/10).
  - The SDK needs `PICO_CYW43_SUPPORTED`.
  - The SPI clock is about 31.25 MHz at a 125 MHz system clock. The PIO divisor may need retuning if the system clock changes (DS p.6/5).
  - The DS also says the RM2 is supported only with Raspberry Pi microcontrollers, through PIO gSPI (DS p.6/5). RP2040 is included.

---

## 5. Assembly and sourcing

### Reflow (DS 2.8, Table 7, PDF p.13-14 / printed p.12-13)
The module is Pb-free and the profile is classification-only.

| Parameter | Value |
|---|---|
| Ts min / max | 150 / 200 °C |
| Ts duration | 60 to 120 s |
| Ramp TL to Tp | 3 °C/s max |
| TL | 217 °C |
| tL | 60 to 150 s |
| Tp = Tc | 260 °C |
| Time within 5 °C of Tc | 30 s |
| Ramp-down | 6 °C/s max |
| 25 °C to peak | 8 min max |

- Measure temperature at the top centre of the module body. For dead-bug reflow, stay within +/-2 °C of Tp.
- **Storage:** -40 to +125 °C at < 60 % RH. **Operating:** -30 to +70 °C at < 85 % RH (DS p.13/12 Table 6).
- **MSL rating: NOT FOUND** in the DS or PB.
- **Packaging:** tape and reel of **960** (DS p.16/15 Table 8).
- **Certification:** modular approval in the EU, UK, USA and Canada. India and Malaysia have module-only certification (DS p.15/14).
- **Price:** RRP $4. Production guaranteed until at least January 2036.

### JLCPCB / LCSC
- **LCSC C49451547**, "Raspberry Pi RMC20452T", RF Modules, SMD 16.5 x 14.5 mm.
- Source: the JLCPCB SMT parts search API, queried 2026-10-14.
- JLCPCB lists it as **Extended** (`componentLibraryType: expand`), with `componentSource: "shop"` (this may mean it is sourced from a third-party shop rather than JLC's own stock; I have not confirmed that). Stock was **114**, price about **$6.53** each at 1-9 and $6.37 at 10 or more, minimum 1.
- The LCSC website itself returned "Access Denied" to the script, so I could not check the LCSC listing page directly.
- Stock changes; re-check before ordering.
