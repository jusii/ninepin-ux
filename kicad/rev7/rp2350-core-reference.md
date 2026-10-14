# RP2350A core circuit reference (for amigahid-pico rev7)

Compiled 2026-10-14 from the sources below. Every item names its source and page. Where a source says nothing,
this file says so under "Not found in the sources" (section 6). Nothing in that section has been filled in from memory.

## Sources and page convention

| Tag | Document | Location |
|---|---|---|
| **P2W-SCH** | Raspberry Pi Pico 2 W schematic, RPI-PICO2W Rev 2, 19 Jul 2024, 2 sheets (A3) | `scratchpad/rm2/p2w/pico-2-w-schematic.pdf` |
| **P2W-DS** | Raspberry Pi Pico 2 W Datasheet (24 pp) | `scratchpad/rm2/p2w/pico-2-w-datasheet.pdf` |
| **HWDG** | Hardware design with RP2350, Release 3, build 20/08/2026 (25 pp) | `scratchpad/hwdg/hardware-design-with-rp2350.pdf` (fetched) |
| **DS** | RP2350 Datasheet (1371 pp). Fetched as an extra source because HWDG points to it for the regulator layout rules. | `scratchpad/rp2350ds/rp2350-datasheet.pdf` (fetched) |
| **RM2** | Raspberry Pi Radio Module 2 datasheet | `scratchpad/rm2/ds/rm2-ds.pdf` |

**Page numbers are PDF page indices** ("p.N"). In P2W-DS, HWDG and DS, the number printed in the page footer is
one lower: PDF p.14 has "13" in its footer. P2W-SCH cites are "sheet 1/2".
The Pico 2 (non-W) schematic was **not** fetched: `datasheets.raspberrypi.com/pico/pico-2-schematic.pdf` returned
HTTP 404. It wasn't needed, because the Pico 2 W schematic contains the full RP2350 core, and DS p.411 reproduces the Pico 2
regulator section.

---

## 1. RP2350A QFN-60 pin table, with Pico 2 W nets

The pin names and numbers come from DS Fig. 152 (p.1331) and Tables 1674-1679 (p.1333-1336). The net column comes
from P2W-SCH sheet 1, U1 = RP2350A0A2.

| Pin | Name | Pico 2 W net / connection (P2W-SCH sheet 1) |
|---|---|---|
| 1 | IOVDD | 3V3; C4 100n |
| 2 | GPIO0 | GPIO0 to header J0-1 |
| 3 | GPIO1 | GPIO1 to header |
| 4 | GPIO2 | GPIO2 to header |
| 5 | GPIO3 | GPIO3 to header |
| 6 | DVDD | 1V1; C11 100n |
| 7 | GPIO4 | header |
| 8 | GPIO5 | header |
| 9 | GPIO6 | header |
| 10 | GPIO7 | header |
| 11 | IOVDD | 3V3; C5 100n |
| 12 | GPIO8 | header |
| 13 | GPIO9 | header |
| 14 | GPIO10 | header |
| 15 | GPIO11 | header |
| 16 | GPIO12 | header |
| 17 | GPIO13 | header |
| 18 | GPIO14 | header |
| 19 | GPIO15 | header |
| 20 | IOVDD | 3V3; C8 100n |
| 21 | XIN | XIN to X1 pin 1, C16 15p to GND |
| 22 | XOUT | XOUT, then R14 1k, then net "XR" to X1 pin 3, C17 15p to GND |
| 23 | DVDD | 1V1; **C9 4u7** (the DVDD pin furthest from the regulator) |
| 24 | SWCLK | net SC, R19 100R, SWCLK, J2-1 (debug header, marked NF) |
| 25 | SWDIO | net SD, R18 100R, SWDIO, J2-3 (J2-2 = GND) |
| 26 | RUN | RUN to header J0-30. No external pull-up or cap. |
| 27 | GPIO16 | header |
| 28 | GPIO17 | header |
| 29 | GPIO18 | header |
| 30 | IOVDD | 3V3; C14 100n |
| 31 | GPIO19 | header |
| 32 | GPIO20 | header |
| 33 | GPIO21 | header |
| 34 | GPIO22 | header |
| 35 | GPIO23 | **WL_ON** (radio power-on, internal) |
| 36 | GPIO24 | **WL_D** (radio gSPI data/IRQ, internal) |
| 37 | GPIO25 | **WL_CS** (radio CS; also the gate of Q1 VSYS-sense FET) |
| 38 | IOVDD | 3V3; shares C6 100n and C7 100n with pin 45 (same 3V3 trace) |
| 39 | DVDD | 1V1; C12 100n |
| 40 | GPIO26_ADC0 | header |
| 41 | GPIO27_ADC1 | header |
| 42 | GPIO28_ADC2 | header |
| 43 | GPIO29_ADC3 | **WL_CLK** (radio gSPI clock; also VSYS/3 sense through Q1) |
| 44 | ADC_AVDD | filtered 3V3: 3V3, R7 200R, **ADC_VREF** (header J0-35), R9 1R, ADC_AVDD; C13 4u7 to GND |
| 45 | IOVDD | 3V3; C6 and C7 100n (shared with pin 38) |
| 46 | VREG_AVDD | 3V3, then R17 27R, then VREG_AVDD; C23 4u7 to GND |
| 47 | VREG_PGND | GND (high-current return of C19/C22) |
| 48 | VREG_LX | L2 3u3, then 1V1 |
| 49 | VREG_VIN | 3V3; C22 4u7 (CIN) |
| 50 | VREG_FB | 1V1, sensed at the C19 (COUT) node |
| 51 | USB_DM | R13 27R, USB_DM, J1-2 (micro-USB) |
| 52 | USB_DP | R12 27R, USB_DP, J1-3 |
| 53 | USB_OTP_VDD | 3V3; shares C10 100n with pin 54 |
| 54 | QSPI_IOVDD | 3V3; C10 100n |
| 55 | QSPI_SD3 | U3 pin 7 (/HOLD/IO3) |
| 56 | QSPI_SCLK | U3 pin 6 (CLK) |
| 57 | QSPI_SD0 | U3 pin 5 (DI/IO0) |
| 58 | QSPI_SD2 | U3 pin 3 (/WP/IO2) |
| 59 | QSPI_SD1 | U3 pin 2 (DO/IO1) |
| 60 | QSPI_SS | U3 pin 1 (/CS); also R11 1k to the BOOTSEL switch SW1 (TP6) |
| 61 (EP) | GND | GND, central exposed pad. This is the **only** ground connection of the die (DS p.18, p.1336). |

Notes:
- **TESTEN does not exist on RP2350.** No pin of that name appears in the QFN-60 pinout (DS p.1331) or the pin tables (DS
  p.1333-1336), and the string "TESTEN" doesn't occur anywhere in the 1371-page datasheet. (RP2040 had it; do not
  carry it over.)
- Supply-pin summary (DS Table 1679, p.1335): DVDD 6, 23, 39; ADC_AVDD 44; VREG_AVDD 46; VREG_PGND 47; VREG_LX 48;
  VREG_VIN 49; VREG_FB 50 (p.1336); USB_OTP_VDD 53; QSPI_IOVDD 54. **Discrepancy:** Table 1679 lists IOVDD as
  "11, 20, 30, 38, 45, 54" but the pinout figure (p.1331), HWDG Appendix A and P2W-SCH all have **pin 1 = IOVDD** and
  pin 54 = QSPI_IOVDD. The figure and both schematics agree, so trust them: IOVDD = 1, 11, 20, 30, 38, 45.
- Reset states (DS p.1333-1335): GPIO0-29 pull-down. QSPI_SS, SD2 and SD3 pull-up; SCLK, SD0 and SD1 pull-down. RUN, SWCLK
  and SWDIO pull-up. GPIO0-25, RUN, SWCLK and SWDIO are "FT" (5 V tolerant when IOVDD = 3.3 V, DS p.1332). GPIO26-29 are
  ADC pins and are **not** FT: they have a diode to IOVDD (P2W-DS p.14).
- USB_DP/USB_DM "27 Ω series resistor required for USB operation"; the internal pull-ups/pull-downs are built in (DS p.1333,
  p.1335).

---

## 2. Passives around the chip (Pico 2 W, P2W-SCH sheet 1, unless noted)

### 2.1 Decoupling per rail

| Rail / pins | Pico 2 W parts | Notes |
|---|---|---|
| IOVDD 3V3: pins 1, 11, 20, 30, 38+45 | C4, C5, C8, C14 = 100n each; pins 38/45 share **C6 + C7** 100n | all 100n 0603 6.3 V X5R 10 % |
| QSPI_IOVDD 54 + USB_OTP_VDD 53 | **C10 100n** (one cap, shared) | HWDG p.9 does the same thing: "pins 53 and 54 … share a single capacitor" (lack of room beside the regulator) |
| ADC_AVDD 44 | C13 4u7 (0402, X5R, 20 %) after R7 200R + R9 1R | see 2.4 |
| DVDD 1V1: pins 6, 39 | C11, C12 = 100n | DS p.401: "the two DVDD pins closest to the regulator should be decoupled with a 100nF capacitor" |
| DVDD 1V1: pin 23 | **C9 4u7** (0402, X5R) | DS p.401: "The DVDD pin furthest from the regulator should be decoupled with a 4.7μF capacitor". DS p.413 also says the second 4.7 µF goes on DVDD pin 23. |
| VREG_VIN 49 (CIN) | C22 4u7 0402 6.3 V X5R 20 % | |
| VREG output / 1V1 (COUT) | C19 4u7 0402 6.3 V X5R 20 % | |
| VREG_AVDD 46 (CFILT) | C23 4u7 0402 6.3 V X5R 20 %, RFILT = R17 **27R** | the minimal design and DS use **33R** (see 3) |
| Flash VCC (U3 pin 8) | C15 100n + C18 4u7 | |

Rail totals at the chip (Pico 2 W): **3V3** = 7 × 100n (C4, C5, C6, C7, C8, C10, C14) + 4u7 CIN (C22) + 4u7 behind 27R
(C23) + 4u7 behind 201R (C13). **1V1** = 2 × 100n (C11, C12) + 4u7 (C9 at pin 23) + 4u7 COUT (C19).
DS per-pin minimums: USB_OTP_VDD 100 nF (p.402); ADC_AVDD 100 nF (p.402); VREG_VIN 4.7 µF close to the pin (p.402).

### 2.2 Internal core regulator (1V1 switcher)

From P2W-SCH sheet 1. The schematic carries this note: "VREG SMPS components/layout critical … Please use recommended components and PCB layout as specified in documentation."

| Ref | Value | Nets |
|---|---|---|
| L2 | 3u3. The Pico 2 W schematic gives **no part number**. | VREG_LX (48) to 1V1 |
| C19 | 4u7 0402 (COUT) | 1V1 to PGND/GND |
| C22 | 4u7 0402 (CIN) | 3V3 / VREG_VIN to PGND/GND |
| R17 | 27R 0603 1 % | 3V3 to VREG_AVDD |
| C23 | 4u7 0402 | VREG_AVDD to GND |
| VREG_FB (50) | — | tied to 1V1 at COUT |
| VREG_VIN (49) | — | **tied to 3V3** (the same 3V3 rail as IOVDD) |

Inductor specification, from DS 6.3.8.2 (p.413): "LX must be fully shielded, 3.3μH ±20% and with a maximum DC resistance of
250mΩ. Saturation current should be at least 1.5A. The inductor must be marked for polarity … we recommend the
**AOTA-B201610S3R3-101-T**."
- CIN ≥ 4.7 µF, max parasitic resistance 50 mΩ. COUT 4.7 µF ±20 %, max 250 mΩ ESR, max 6 nH ESL (DS p.413).
- AOTA-B201610S3R3-101-T is 0806 / 2016-metric, 2.00 × 1.60 × 1.00 mm max (DS p.415 Fig. 29; HWDG p.8). It is polarity-marked
  and supplied on the reel all aligned the same way (HWDG p.8; DS p.414).
- The HWDG minimal schematic annotates L1 as "3.3u 2.1A" (HWDG p.21). That is the only current figure given for the part itself.
- DS p.414 says "generally the higher saturation current the better" and asks for an inductor with low inductance
  roll-off with current.

### 2.3 1.1 V DVDD caps, IOVDD caps

See 2.1. Note that on both boards the 1V1 net is generated on-chip and only routed back to DVDD pins 6, 23 and 39 and to
VREG_FB. Nothing else hangs on it.

### 2.4 ADC_AVDD filter and ADC_VREF

- P2W-SCH sheet 1: 3V3, **R7 200R** (0402 1 %), node **ADC_VREF** (header pin 35), **R9 1R** (0603), node ADC_AVDD (pin 44), with **C13 4u7** to GND.
- P2W-DS p.14: "R-C filter (201 Ω into 2.2 μF)". The schematic shows **C13 = 4u7**, so the datasheet text and the
  schematic disagree on the capacitor. The schematic is Rev 2. The ADC draws about 150 µA, which gives about a 30 mV offset across 200 Ω. An
  external 3.0 V shunt (LM4040) can go from ADC_VREF to GND. R9 1R keeps the shunt stable and keeps filtering even
  when ADC_VREF is shorted to 3V3 (P2W-DS p.14). R7 can be removed to supply ADC_VREF separately (P2W-DS p.15).
- DS p.402: ADC_AVDD 1.8-3.3 V; performance is compromised below 2.97 V; 100 nF close to the pin.

### 2.5 USB_OTP_VDD

On 3V3, sharing C10 100n with QSPI_IOVDD (P2W-SCH sheet 1; HWDG p.9). DS p.402: it "must always be provided, even in
applications where the USB PHY is never used", decoupled with 100 nF.

### 2.6 Crystal

| Ref | Value | Nets |
|---|---|---|
| X1 | **Abracon ABM8-272-T3**, 12 MHz, 4-pad 3.2 × 2.5 mm (pads 2 and 4 to GND) | pad 1 = XIN, pad 3 = XR |
| C16 | 15p 0603 25 V C0G 5 % | XIN to GND |
| C17 | 15p 0603 25 V C0G 5 % | XR to GND |
| R14 | **1k** 0603 1 % | XOUT (pin 22) to XR (series resistor on the **XOUT** side) |

The crystal specification is in DS Table 629 (p.508): 12.000 MHz fundamental AT cut, ±30 ppm tolerance, ±30 ppm stability, ESR max 50 Ω, C0 max 3 pF,
CL 10 pF, drive level 10-200 µW, −40 to +85 °C. HWDG p.14 explains the circuit: 15 pF + 15 pF gives 7.5 pF in series, plus about 3 pF of assumed parasitics,
so about 10.5 pF load. The 1 kΩ resistor prevents over-driving the crystal at IOVDD = 3.3 V, and the values are tuned for 3.3 V IOVDD only. HWDG p.15 says
to use this exact crystal ("Pico 2 has been specifically tuned for the ABM8-272-T3").

### 2.7 RUN

- Pico 2 W: RUN (pin 26) goes straight to header pin 30. There is **no external pull-up, cap or button** (P2W-SCH sheet 1). The on-chip pull-up
  to 3.3 V is "about ~50 kΩ" (P2W-DS p.9). DS p.1335 lists RUN reset state = Pull-Up.
- HWDG minimal: RUN, then SW2 (434133025816 tactile), then **R4 1k** (0402), then GND. There is still no external pull-up (HWDG p.21 Appendix A;
  buttons discussed on p.19).

### 2.8 BOOTSEL / QSPI_SS

- Pico 2 W: QSPI_SS (pin 60) goes directly to flash /CS (U3 pin 1). **R11 1k** (0603) runs from QSPI_SS to SW1 (TP-1221U-K9K5325, pins 1 and 2
  tied together with test point TP6), and SW1 pins 3 and 4 go to GND. There is **no QSPI_SS pull-up** on the Pico 2 W (P2W-SCH sheet 1).
- HWDG minimal (HWDG p.11-12, schematic p.21): QSPI_SS, then **R10 0R**, then FLASH_SS, then U3 /CS. **R1 10k** from FLASH_SS to 3V3 is
  **DNF**: it is "unnecessary" with the W25Q128JVS but kept as a footprint for other flashes. **R6 1k** runs from QSPI_SS to net USB_BOOT, then SW1
  (434133025816), then GND. R9 0R (DNF) is an option for GPIO0 (XIP_CS1n) to drive FLASH_SS on RP2354 parts. The guide says:
  "resistors R1 and R6 (R9 and R10 also), should be placed close to the flash chip" (p.12), and "It is important to include
  resistor R6, as this allows the QSPI_SS pin to safely over-drive the applied pull-down when the RP2350 starts booting"
  (p.11).

### 2.9 QSPI flash wiring

| RP2350A pin | Signal | Pico 2 W U3 (W25Q32RVXHJQ, 4 MB) | HWDG U3 (**W25Q128JVSIQ**, 16 MB, SOIC-8) |
|---|---|---|---|
| 60 | QSPI_SS | pin 1 /CS (direct) | pin 1 /CS via R10 0R (FLASH_SS) |
| 56 | QSPI_SCLK | pin 6 CLK | pin 6 CLK |
| 57 | QSPI_SD0 | pin 5 DI/IO0 | pin 5 DI/IO0 |
| 59 | QSPI_SD1 | pin 2 DO/IO1 | pin 2 DO/IO1 |
| 58 | QSPI_SD2 | pin 3 /WP/IO2 | pin 3 /WP/IO2 |
| 55 | QSPI_SD3 | pin 7 /HOLD/IO3 | pin 7 /HOLD/IO3 |
| — | VCC | pin 8, 3V3; C15 100n + C18 4u7 | pin 8, 3V3; C2 100n (0402 X7R) |
| — | GND | pin 4 (pin 9 thermal pad NC) | pin 4 |

No series resistors are fitted on SCLK or SD0-3 on either board. HWDG p.11: "the QSPI pins of RP2350 should be wired directly to
the flash, using short connections". W25Q128 (128 Mbit / 16 MB) "is the largest memory size that RP2350 can support"
(HWDG p.11). The flash command requirements (03h read, 02h, 05h, 06h, 20h, and the XIP-exit sequence) are on HWDG p.12-13.

### 2.10 USB D+/D−

- Pico 2 W: **R12 27R** (USB_DP) and **R13 27R** (USB_DM), 0603 1 %, between the chip and the connector (P2W-SCH sheet 1). TP1 is GND,
  TP2 is USB_DM and TP3 is USB_DP, all at the connector (P2W-DS p.9).
- HWDG: **R7/R8 27R** 0402, with the note "Make sure R7 and R8 are close to RP2350" (p.21; text on p.16).

### 2.11 SWD

- Pico 2 W: **100R series** resistors, R19 on SWCLK and R18 on SWDIO, run to the 3-pin J2 (1 SWCLK, 2 GND, 3 SWDIO), which is marked NF (P2W-SCH sheet 1).
  The internal pull-ups are nominally 60 kΩ (P2W-DS p.19).
- HWDG: no series resistors. J4 is a JST SM03B-SRSS-TB right-angle connector with pinout 1 SWCLK, 2 GND, 3 SWD (Raspberry Pi Debug Probe pinout) (p.21, p.18).

### 2.12 TESTEN

Not applicable: RP2350 has no TESTEN pin (see section 1).

---

## 3. HWDG "Minimal design" (RP2350A, Appendix A, HWDG p.20-21) vs Pico 2 W

| Item | HWDG minimal (p.8-21) | Pico 2 W (P2W-SCH) |
|---|---|---|
| 3V3 source | NCP1117ST33T3G LDO (up to 1 A) from micro-USB VBUS; C1 10u input, C5 10u output (0805 X5R 16 V) (p.8-9) | RT6154AGQW buck-boost from VSYS (section 4) |
| VREG_VIN | +3V3 (same rail as IOVDD) | 3V3 |
| VREG_AVDD filter | **R3 33R** + C9 4.7u (p.8) | R17 **27R** + C23 4u7 |
| CIN / COUT | C6 4.7u / C7 4.7u (0402 X5R 6.3 V) | C22 / C19 4u7 |
| Inductor | L1 **AOTA-B201610S3R3-101-T**, 3.3 µH, "2.1A", 0806 (p.8, p.21) | L2 3u3 (no part number on the schematic) |
| 3V3 decoupling | **7 × 100n** (C12-C18, 0402 X7R 25 V), one per 3V3 pin group, with **53+54 sharing one** (p.9, p.21). Plus C19 10u bulk on +3V3 at the I/O headers. | 7 × 100n (two on pins 38/45, one shared by 53/54) |
| ADC_AVDD | **tied directly to +3V3 with IOVDD pin 45, no RC filter** (p.21) | 200R + 1R + 4u7 filter; ADC_VREF brought out |
| 1V1 decoupling | **3 × 100n** (C8, C10, C11) on DVDD 6/23/39. There is **no 4.7 µF on pin 23**. | 2 × 100n + **4u7 on pin 23** (C9). DS p.401 and p.413 recommend the 4.7 µF on pin 23, so the Pico 2 W follows the datasheet and the minimal design does not. |
| Flash | W25Q128JVSIQ, C2 100n only, R10 0R in /CS, R1 10k DNF pull-up, R9 0R DNF from GPIO0 | W25Q32RV, C15 100n + C18 4u7, /CS direct |
| BOOTSEL | R6 1k + SW1 to GND | R11 1k + SW1 to GND |
| RUN | SW2 + R4 1k to GND | none (header only) |
| Crystal | ABM8-272-T3, C3/C4 15p **0402 C0G 50 V**, R2 1k on XOUT | same values (0603 25 V) |
| USB series | R7/R8 27R 0402 "close to RP2350" | R12/R13 27R 0603 |
| SWD | JST SM03B-SRSS-TB, direct | 100R series, 3-pin header (NF) |
| Board | 2-layer, 1 mm thick, all parts on top, 0402 passives (p.5, p.16) | — |

**Can VREG_VIN be 3.3 V?** Yes. DS 6.3.7 (p.408): "VREG_VIN must be in the range 2.7 V to 5.5 V, and VREG_AVDD must be in
the range 3.135 V to 3.63 V. If VREG_VIN is limited to the range 3.135 V to 3.63 V, a single combined supply can be used for
both VREG_VIN and VREG_AVDD … Take care to minimise noise on VREG_AVDD." Both reference designs do this from 3V3.
Sequencing (DS p.402): VREG_VIN and VREG_AVDD "should be powered up together". The other supplies can come up in any order.
Running from one 3.3 V supply is explicitly acceptable. DS p.402 also notes that VREG_AVDD powers the POR/brown-out
blocks, so it must always be powered.

**Inductor spec recap:** 3.3 µH ±20 %, fully shielded, DCR ≤ 250 mΩ, Isat ≥ 1.5 A, polarity-marked, 2016-metric
(AOTA-B201610S3R3-101-T recommended) (DS p.413-415; HWDG p.8). HWDG p.8: if you don't use their layout, "you do so at your
own risk".

---

## 4. Pico 2 W wireless pins, power tree, ADC_VREF

### 4.1 Wireless interface (P2W-DS p.8, p.18-19; P2W-SCH sheets 1-2)

| RP2350 GPIO (pin) | Net | Role |
|---|---|---|
| GPIO23 (35) | WL_ON | output: radio power-on. Drives both WL_REG_ON and BT_REG_ON of the CYW43439 (sheet 2). |
| GPIO24 (36) | WL_D | bidirectional gSPI data **and** IRQ, all on one pin |
| GPIO25 (37) | WL_CS | output: gSPI CS. "When high also enables GPIO29 ADC pin to read VSYS" (gate of Q1). |
| GPIO29 (43) | WL_CLK | gSPI clock. It doubles as ADC3, which reads VSYS/3 when no SPI transfer is in progress. |

The shared data line is on P2W-SCH sheet 2, at CYW43439 U4A:
- SDIO_CMD (L6) and **SDIO_DATA2 (L7)** connect **directly** to WL_D.
- **SDIO_DATA0 (K6)** goes **through R22 470R** (0603 1 %) to WL_D.
- **SDIO_DATA1 (H7)** goes **through R23 10K** (0603 1 %) to WL_D.
- SDIO_CLK (M7) goes to WL_CLK. SDIO_DATA3 (J7) goes to WL_CS. BT_DEV_WAKE (B1) goes to GND.
- P2W-DS p.18: "The Infineon CYW43439 DIN/DOUT and IRQ all share one pin on the RP2350. Only when an SPI transaction isn't in
  progress is it suitable to check for IRQs. The interface typically runs at 33 MHz."
- The RM2 module needs the same arrangement **externally** (RM2 p.6 Table 1, p.11 Table 2):
  - module pin 3 SCLK to GPIO29
  - pin 5 Data In to GPIO24 directly
  - **pin 6 Data out to GPIO24 through 470 Ω**
  - pin 9 CS to GPIO25
  - **pin 10 nIRQ to GPIO24 through 10 kΩ**
  - pins 12 and 13 (Wi-Fi on, BT on) to GPIO23
  - pin 14 VDDIO to 3.3 V
  - pin 16 Vin (3.0-4.8 V) to 3.3 V
  - RM2 pins 8, 17 and 18 are the radio's own GPIO0, GPIO2 and GPIO1

CYW43 GPIOs on the Pico 2 W (P2W-DS p.8, p.19): WL_GPIO0 is the user LED (R3 470R + green LED D2, sheet 1). WL_GPIO1 drives the RT6154 PS pin
(R8 100K pull-down, TP4). WL_GPIO2 is VBUS sense.

VSYS sense on GPIO29 (P2W-SCH sheet 1): VSYS, then R5 20K, then the Q1 DMG1012T channel (gate = WL_CS), then WL_CLK, with R6 10K to GND. The schematic note explains Q1: "FET stops leakage through ADC3 pin diode into 3V3 net when 3V3 supply is off".

Radio supply on the Pico 2 W (sheet 2): 3V3, then FB1 BLM18AG601SN1 ferrite, then VDDBAT (C24 4u7); WCC_VDDIO = VDDBAT (C35 100n).
These components sit inside the RM2 module and are listed for reference only.

### 4.2 Power tree (P2W-SCH sheet 1; P2W-DS p.9, p.15)

- **VBUS** (micro-USB J1 pin 1) goes through **D1 PMEG6010ELR** Schottky to **VSYS**. The VBUS divider is R10 5K6 / R1 10K to GND, and its midpoint is
  WL_GPIO2 (VBUS sense).
- **VSYS** (1.8-5.5 V, header pin 39) has C1 47u (0805 X5R) and goes to U2 **RT6154AGQW** buck-boost VIN10, VIN11 and VINA. C20 100n sits on VINA.
- U2: L1 **1u** across LX1a/b and LX2a/b. EN (pin 12) = **3V3_EN**, pulled to VSYS by R2 100K (header pin 37). PS/SYNC (13) =
  WL_GPIO1 with R8 100K pull-down (PS=0 PFM default, PS=1 PWM). FB (3) is set by the R21 560K / R20 100K divider. PGOOD (14) is NC.
- **3V3** output (VOUTa/b) has C2 47u + C21 47u (0805). It goes to header pin 36; P2W-DS p.9 recommends keeping the external load under 300 mA.
- 3V3 then feeds all RP2350 3.3 V pins (IOVDD, QSPI_IOVDD, USB_OTP_VDD, VREG_VIN, VREG_AVDD via 27R, ADC_AVDD via 201R),
  the flash, and the radio via FB1.
- **1V1** is generated by the RP2350 internal switcher (2.2) and used only by DVDD.
- Discrepancy: P2W-DS p.15 says the RP2350 has an "on-chip linear regulator (LDO)" for the 1.1 V core. That is stale text.
  The schematic and DS (p.407-415) show a **switching** regulator with an external inductor.

### 4.3 ADC VREF

See 2.4: R7 200R from 3V3 to ADC_VREF, then R9 1R to ADC_AVDD with C13 4u7. ADC_VREF is brought out on header pin 35. The
schematic notes say: "For best performance use external 3.0V shunt ref (e.g. LM4040)" and "For lower offset (at expense of noise)
connect VREF to 3V3 with lower resistance".

---

## 5. Layout notes (as stated in the sources)

**Core regulator** (DS 6.3.8, p.410-415; HWDG p.6-8). This is the most critical part of the layout:
- "This should be placed first on any board design and these guidelines must be strictly followed" (DS p.410). Copy the
  Pico 2 / Minimal layout (DS Fig. 26 p.412; HWDG Fig. 4 p.7).
- VREG_AVDD is noise-sensitive and must be RC-filtered. CFILT needs its own short GND path to the QFN pad and must not
  share vias with the CIN/COUT high-current GND (DS p.412-413).
- CIN needs its own separate GND via (a low-impedance path) back to the RP2350 GND pad (DS p.412).
- Keep both switching-current loop areas minimal and isolated, connecting to main GND at **one point** through **2 adjacent vias**,
  as short as possible back to the QFN GND pad (DS p.412-413).
- **Do not place any of CIN, L or COUT on the opposite side** of the PCB (DS p.412).
- Cut away copper under the inductor on the top layer and keep copper back from the VREG_LX trace. On boards with 4 or more layers, cut layer 2
  under the LX node (DS p.412-413, Fig. 27).
- Feed VREG_FB from the COUT output and do not route it under LX (DS p.413).
- Place COUT as close to the pins as practical. Add a second 4.7 µF on DVDD pin 23 (bottom edge of the package), away from LX/COUT
  (DS p.413). Note: DS p.413 literally says COUT "must be placed between VREG_VIN and VREG_PGND", which looks like a typo for the
  output node. Follow the figure instead.
- **Inductor orientation matters**: the polarity dot goes on the +1V1 pad (pad 1) in HWDG Fig. 4 (p.7). DS Fig. 28 (p.414) shows the current and
  field direction.
- The bottom layer under the regulator is a ground plane connected to the QFN GND pad (DS p.412).

**Decoupling** (HWDG p.9-10): "it is important to place decoupling close to the power pins … we recommend the use of a 100 nF
capacitor per power pin". The one exception is pins 53/54, which share a cap. Because the minimal board is 2-layer, its caps sit "slightly further away … than is optimal",
which may limit maximum speed (p.10).

**Crystal** (HWDG p.14): keep the traces short, because their parasitic capacitance is part of the about 3 pF assumed in the load calculation:
"Try and keep the layout as short as possible."

**USB** (HWDG p.16): 27 Ω resistors close to the chip. Target **90 Ω differential**. On a 1 mm 2-layer board that means 0.8 mm tracks with a
0.15 mm gap. Keep a solid, uninterrupted ground under the whole pair. For thicker boards, re-engineer the geometry, or accept the risk: "USB FS
can be quite forgiving … probably not going to be compliant".

**QSPI** (HWDG p.11): wire it directly with short connections, for signal integrity and to limit crosstalk. Put the SS resistors close to the flash (p.12).
DS p.1329 (RP2354 context): "minimise the track length connected to the QSPI package pins". It gives no numeric limit.

**Exposed pad**: the GND pad is the only ground connection of the die (DS p.18, p.1336). DS p.412 asks for a bottom-layer ground plane connected to it.

---

## 6. Not found in the sources (do not guess)

- **QSPI routing length limit:** neither HWDG nor DS gives a number. Both say only "short".
- **Crystal keep-out area:** none specified, beyond keeping the traces short (HWDG p.14).
- **Exposed-pad via count, size or pattern:** not specified anywhere. Inspect the downloadable Minimal KiCad design (HWDG p.4 links
  "RP2350A Minimal KiCAD design (ZIP file)") if you need it. That ZIP was not fetched.
- **The AOTA-B201610S3R3-101-T's own DCR and Isat:** not given. Only the requirement (≤250 mΩ, ≥1.5 A, DS p.413) and the
  "2.1A" annotation on the HWDG schematic (p.21) appear.
- **Part number of the Pico 2 W inductor L2:** the schematic shows only "3u3".
- **Crystal spec table in HWDG:** HWDG p.15 says "has the following specifications:" and then prints no table in this
  release. The table is in DS Table 629, p.508 (used in 2.6).
- **USB differential impedance for other stack-ups or board thicknesses:** only the 1 mm 2-layer example is given.
- **Guidance on a USB mux or host-mode VBUS switching:** neither reference design has one. The placement of the 27 Ω resistors relative to
  a mux is not covered (HWDG requires only that they sit "close to RP2350").
- **RUN external pull-up or RC reset:** neither design uses one. No recommended value is given beyond the internal ~50 kΩ.
- **Pico 2 (non-W) schematic:** the URL returned 404. It was not needed, because DS p.411 reproduces its regulator section.
