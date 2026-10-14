# rev7 schematic review (amigahid-pico-UX rev7), 2026-10-14

Reviewed: `kicad/rev7/design.py` (20:52), `make_sch.py`, `floorplan.py`, the exported netlist `scratchpad/rev7.xml` (20:56), `sch/rev7-sch.pdf`, and the rev6 netlist and notes.
Datasheets fetched to `scratchpad/review/ds/`: SN74CBTD3861 (SCDS084G), TS3USB30E (SCDS255G), TLV757P (SBVS322C). The ST USBLC6 download failed. The RP2350 DS and HWDG were already in the scratchpad.
Scripts are in `scratchpad/review/tools/`:
- `pins.py`: symbol pin names against the design.py nets
- `fp.py`: symbol pins against footprint pads
- `cmp.py`: netlist against design.py

Method checks:
- `cmp.py` found **0 differences** between rev7.xml and design.py. Every pin is on the net design.py intends, and no labels merged by accident.
- `fp.py` found that every symbol's pin numbers match its footprint's pad numbers for all 110 parts.
- Caveat: the `.kicad_sch` on disk was rewritten at 21:03, after the netlist and PDF were exported. Re-export before relying on this review for the final file.

## Findings

1. **BLOCKER: L1 (RP2350 core-regulator inductor) is wired the wrong way round for a polarity-marked part.**
   - design.py has L1 pin 1 = `vreg_lx` and pin 2 = `dvdd`.
   - HWDG Fig. 4 (PDF p.7) has L1 pad 1, the polarity dot, on **+1V1** and pad 2 on VREG_LX. HWDG p.6-7 explains that a "wrong way round" inductor upsets the regulator. The text also says the AOTA-B201610S3R3-101-T was made with a dot for exactly this reason.
   - The footprint `Inductor_SMD:L_Murata_DFE201610P` has no pin-1 or polarity mark at all.
   - **Fix:** swap the pins to `{"1": "dvdd", "2": "vreg_lx"}`. Use or derive a footprint that has a pad-1 dot on silk and fab. Check that the AOTA reel dot corresponds to pad 1 in the JLCPCB rotation.

2. **BLOCKER: the 1V1/DVDD rail is one 4.7 uF short. COUT for the core switcher is missing.**
   - The `dvdd` net holds only C20 4.7 uF, C21 100 nF and C22 100 nF. The block note assigns C20 to "DVDD pin 23".
   - Pico 2 W has COUT C19 4u7 **plus** C9 4u7 at pin 23 **plus** 2x100n. HWDG minimal has COUT C7 4.7 uF plus 3x100n.
   - DS p.413 requires COUT 4.7 uF ±20 % (≤250 mΩ ESR) placed at the regulator, and recommends a second 4.7 uF on DVDD pin 23 (reference §2.1/§2.2).
   - With one 4.7 uF, rev7 has either no COUT or nothing on pin 23, and the note says it is the latter.
   - **Fix:** add a COUT of 4.7 uF (0402/0603 X5R) on `dvdd` at the L1/VREG_FB node. Keep C20 at pin 23. Optionally add a third 100 nF so that DVDD 6, 23 and 39 each have one. Use the HWDG Fig. 4 placement.

3. **SHOULD-FIX: the WS2812B-2020 data input does not meet VIH in the worst case.**
   - LED_VDD = SYS5V minus a 1N5819WS drop, which is about 4.1-4.6 V. On USB-C at 5.0-5.25 V, SYS5V is about 4.65-4.9 V, so LED_VDD is about 4.35-4.6 V. VIH = 0.7·VDD comes to **3.05-3.22 V**.
   - The RP2350 guaranteed VOH at IOVDD 3.3 V is **2.62 V min** (DS p.1336, IO DC table). With a near-zero load the typical VOH is about 3.3 V, so the margin is only about 0.1-0.25 V typical and **negative against the spec**.
   - On Amiga power (SYS5V ≈ 4.4 V) it is better: VIH ≈ 2.9 V, but still above the guaranteed 2.62 V.
   - **Fix, either of:**
     - Feed LED_VDD from +3V3 and delete D3. The survey quotes the V6 part at 3.3-5.3 V; check that against the WS2812B-2020-V6 datasheet. Blue brightness drops, which is irrelevant for a status LED.
     - Keep the 5 V supply and add a 74AHCT1G125 or 74HCT1G04-type buffer powered from LED_VDD.

4. **SHOULD-FIX: F1 is now in the signalling path, and the schematic marks it "not assembled".**
   - AMIGA_5V_FUSED (after F1) is the **only** VCC for U6/U7 and the only rail for all 19 pull-ups.
   - In rev6, an empty F1 only removed one power leg (the pull-ups ran from SYS5V). In rev7, an empty or tripped F1 leaves both bus switches at VCC = 0, so they are open and the board cannot talk to the Amiga at all, even when USB-C powers it.
   - The rev6 bring-up order ("leave F1 empty, run from aux, cable an Amiga later") is therefore broken for rev7.
   - **Fix:** either ship F1 assembled with a chosen value, or split the rails: a small separate fuse or 0R/10R from AMIGA_5V to a `AMIGA_5V_SW` net for the switch VCC, pull-ups and sense. Then F1 only guards the D1 power leg. Update the bring-up doc.

5. **SHOULD-FIX: D1 (SS34) reverse leakage back-feeds AMIGA_5V_FUSED when the board runs on USB-C.**
   - With USB-C present, SYS5V ≈ 4.7 V sits on D1's cathode. The anode node's only loads are R46+R47 (200 k) and the CBTD quiescent current, plus the Amiga's dead rail if one is attached.
   - SS34 leakage is µA at 25 °C and rises steeply with temperature (several mA at 100 °C at the rated VR). On the bench with nothing on J10/J1, AMIGA_5V_FUSED floats to roughly I_R × 200 k. That can be volts.
   - Consequences:
     - `amiga_sense` reports a false "Amiga on".
     - The CBTD sees a partial VCC.
     - "Never back-feeds an Amiga that is off" holds only at leakage level.
   - **Fix, any of:**
     - Choose a low-leakage OR device for D1, such as a PMOS ideal diode or a low-IR Schottky.
     - Add a bleeder of about 10-22 k on the switch/sense rail (this works once finding 4 has split the rails).
     - At minimum, have the firmware treat Amiga-present as ADC2 > ~2.0 V (≥4 V rail) and document the risk.

6. **SHOULD-FIX: the schematic sheet overflows.**
   - In `rev7-sch.pdf` (A0, one page), the blocks are stacked in one column. The USB block is cut off at the bottom border.
   - The **MCU, radio and display/status blocks are not on the sheet at all**: U1, U8, Y1, U9 and LED1 sit outside the frame. The PDF therefore cannot be reviewed or printed for those parts.
   - **Fix:** make `make_sch.py` lay the blocks out in columns, or across several sheets, so that every symbol is inside the frame. Re-export the PDF.

7. **SHOULD-FIX: a DE9-only hookup to a running Amiga still drags the port lines low (the rev6 trap, in a new form).**
   - The DE9 ports take no power (pin 7 NC). If the keyboard cable (the AMIGA_5V source) is not connected, the 14 port pull-ups go to a dead or low AMIGA_5V_FUSED rail. That rail is also loaded by D1 into SYS5V, and the Amiga's own port pull-ups back-feed it through the 10 k resistors.
   - The Amiga then sees stuck directions and buttons. With USB-C absent, the board half-powers through 14 × 10 k.
   - **Fix:** document it on silk or in the guide, the same as rev6. The rail split in finding 4, with switch/pull-up VCC isolated from the D1 power leg, at least stops the board half-powering itself.

8. **SHOULD-FIX: the CBTD3861 "clamps at ~3.3 V" note is inaccurate. The clamp is only a typical curve.**
   - The datasheet gives no tabulated Von or Vclamp. The electrical table just says "VOH: See Figure 2" (p.3). Figure 2 (p.5), typical at 100 µA:

     | VCC | 25 °C | 85 °C | 0 °C |
     |---|---|---|---|
     | 4.5 V | 2.5 V | 2.75 V | 2.5 V |
     | 5.0 V | 3.0 V | 3.25 V | 2.95 V |
     | 5.5 V | 3.5 V | 3.75 V | 3.42 V |

   - The datasheet also warns (p.2): "In applications with fast edge rates … the output may have little or no level-shifting effect."
   - Expected VCC is AMIGA_5V minus the F1 drop, about 4.6-4.9 V. The switch therefore passes about 2.6-2.9 V, and the RP2350 internal pull-up (32-86 k, DS p.1337) finishes the rise to 3.3 V. That works: VIH is 2.0 V.
   - Recommended VCC is **4.5 V min** (p.2). An Amiga rail at 4.75 V less the polyfuse drop under the board's full load sits close to that.
   - GPIO26 and GPIO27 (`cont2_h_lo`, `cont2_v_lo`) are **not FT**. Their VIH max is IOVDD + 0.3 = 3.6 V (DS p.1336). Hot with a 5.25-5.5 V Amiga rail, VOH reaches 3.45-3.75 V. Those pins are board outputs in practice, so the risk is small.
   - **Fix:**
     - Correct the note to "B side passes about VCC − 2 V (typical, 2.6-3.0 V), the internal pull-up completes to 3.3 V".
     - Keep the F1 drop small, or use the rail split in finding 4, so that VCC stays ≥4.5 V.
     - Optionally move the two Amiga-facing nets off GPIO26/27 onto FT pins. The cost is a firmware pin-map change, so it may be better to accept and document.

9. **SHOULD-FIX: USB-C sink bulk capacitance and inrush.**
   - AUX_5V feeds C1 220 uF + C2 10 uF on SYS5V through D2, and a diode does not limit inrush. USB 2.0 / Type-C sinks are limited to 10 uF plus a 50 µC inrush charge, and 220 uF at 5 V is 1.1 mC.
   - The same 220 uF hot-plugs onto the Amiga keyboard-port 5 V through F1 and D1, which can glitch a running Amiga.
   - rev6 had the same arrangement, so this is not a regression.
   - **Fix:** reduce C1, or add soft-start: a load switch such as TPS2553 or AP22652 on SYS5V, or an RC-gated PMOS. At minimum, document "do not hot-plug the keyboard cable into a running Amiga".

10. **SHOULD-FIX: no LCSC/MPN on several assembled parts, and stock limits.**
    - F2 and F3 (1812 polyfuses), FB1, C1 and C4 (no voltage rating given), SW1/SW2 (PTS645Vx58: the survey shows the C&K part unstocked, and the TS665ZJ alternative was not checked against this land), and all the plain R/C parts lack sourcing data.
    - SN74CBTD3861DBQR has 103 in stock, so about 51 boards at two per board. L1 (AOTA) has **70** in stock.
    - **Fix:** fill in LCSC codes and capacitor voltage ratings. Reserve stock for the CBTD and L1 before layout fixes the DBQ land.

11. **NIT: USB series resistance through the mux.**
    - R57/R58 (27 R) sit between U1 and the mux. The TS3USB30E adds RON 6 Ω typ / 10 Ω max (p.5).
    - Full-speed driver impedance (28-44 Ω including the series resistor) moves toward the top of the window.
    - FS is tolerant of this, and the mux is transparent at FS and LS (1400 MHz BW, Cio(on) 7.5 pF).
    - **Fix:** optionally use 22 R. Keep the resistors close to U1 as HWDG asks.

12. **NIT: there is no USB-C VBUS sense to a GPIO.**
    - Firmware cannot tell whether a PC is attached on USB-C, so it cannot choose device or host role automatically. Today the role is chosen only by usb_sel, so BOOTSEL works.
    - **Fix:** only if automatic role switching is wanted, add a divider from AUX_5V to a GPIO. No free GPIO is left apart from UART0/1.

13. **NIT: LDO thermal headroom.**
    - TLV75733PDBV has RθJA **231 °C/W** (JEDEC; 100.8 on the TI EVM), per p.6. Dropout is 425 mV max at 1 A (p.7).
    - At 4.7 V in and about 150 mA average, the rise is about 50 °C. That is fine, but continuous Wi-Fi TX at about 300 mA would mean +100 °C.
    - **Fix:** give the GND pin a copper pour. Alternatively use the DYD/DRV variant, or accept the risk, since the use is BT HID only.

14. **NIT: the RM2 VIN droop.**
    - FB1 (BLM18AG601SN1, DCR up to about 0.38 Ω) at 271 mA TX drops about 0.1 V, so VIN ≈ 3.2 V. That is the RM2's "RF performance degrades below 3.2 V" point (RM2 DS p.12).
    - It does not matter for BT.
    - **Fix:** optionally use a lower-DCR bead or 0R.

15. **NIT: ESD gaps.** USB-C CC1/CC2, AMIGA_5V at J10/J1, and the J7 UART / J2 SWD headers have no clamps. USB-C VBUS is clamped only via U4 pin 5. **Fix:** optionally add a SOD-323 TVS on AMIGA_5V and CC lines, using the same PESD5V0S1BA.

16. **NIT: PWR_FLAGs on `dvdd`, `vreg_avdd`, `adc_avdd`, `wl_vin` and `LED_VDD` hide ERC "no driver" checks.** Those rails are driven by passives or the on-chip regulator, so ERC = 0 does not prove they are fed. This is acceptable, but it is the reason findings 1 and 2 needed a manual check.

## Assessment of dropping the 3.3 V-side pull-ups

This is acceptable.
- **Amiga on:** the switch is closed, and the 10 k to 5 V drives B up to the clamp. The internal pull-up then completes the rise to 3.3 V.
- **Boot and reset:** GPIOs default to pull-down (RPD 35-189 k). That only sinks about 30-80 µA through the switch, so the A side stays at about 4.4-5 V and the Amiga sees every line released. There are no spurious resets (kbreset), key events or button presses.
- **Amiga off:** the switch is open and the B side floats until the firmware enables pull-ups. Nothing on the Amiga side is affected.
- Nothing depends on an external pull-up. The RP2350 A2 erratum E9 (input latch with pull-down) does not apply, because the design uses pull-ups. The chosen reel ("A4" in the JLC name) is reported to fix E9 anyway; this is not verified.

## GPIO changes vs rev6

Every Amiga-facing net keeps its rev6 GPIO:
- GPIO4-6: `kbreset_lo`, `kbdata_lo`, `kbclock_lo`
- GPIO7-13: port 1
- GPIO14/15: `kbin`
- GPIO18-22 and 26/27: port 2

GPIO0-3 (UART, I2C) are also unchanged.

Changed or new:
- **GPIO16**: `led1_drv` → `ws_drv` (one WS2812 replaces two discrete LEDs; firmware change)
- **GPIO17**: `led2_drv` → `usb_sel` (new USB mux select; low = USB-C)
- **GPIO28**: `gp28` spare header → `amiga_sense` (ADC2)
- GPIO23/24/25/29: radio `wl_on`, `wl_d`, `wl_cs`, `wl_clk`. These are the same numbers that were module-internal on the Pico W / Pico 2 W, so the stock pico-sdk cyw43 pin defaults apply.

Other differences from rev6:
- **Removed:**
  - The DIN5 J9 and J11; DIN5 Amigas now use a passive adapter (stated intent).
  - The 6-pin UART+SWD header, now split into J2 (SWD) and J7 (UART).
  - The J7 GP28 spare.
  - The 19 low-side 10 k pull-ups and the 19 BSS138s, now replaced by the two CBTD3861s.
- **Re-railed:** the high-side pull-ups moved from SYS5V to AMIGA_5V_FUSED.
- **Pin-for-pin identical to rev6:** P1/P2 DE9, J10/J12 mini-DIN6, J1/J8 A500 headers, J3 OLED, JP1/R45 and the pull-up values.

## Verified correct

- **RP2350A, all 61 pins against DS Fig. 152 and P2W-SCH:**
  - The KiCad RP2350A symbol has pin 1 = IOVDD and pin 54 = QSPI_IOVDD, which agrees with the figure, HWDG and Pico 2 W. The Table 1679 typo has no effect, since both pins are on +3V3.
  - IOVDD 1/11/20/30/38/45, USB_OTP_VDD 53, QSPI_IOVDD 54 and VREG_VIN 49 are on +3V3. VREG_PGND 47 and EP 61 are on GND. DVDD 6/23/39 and VREG_FB 50 are on dvdd.
  - VREG_AVDD has 33 R + 4.7 uF (HWDG values). ADC_AVDD has 33 R + 1 uF.
  - 3V3 decoupling: 7 × 100 nF (6 IOVDD + one shared by 53/54), plus 4.7 uF CIN (C29) and 100 nF at the flash.
- **Clock, reset, debug:**
  - XIN to Y1.1 with 15 pF. XOUT through 1 k to Y1.3 with 15 pF. Y1 pads 2/4 on GND. ABM8-272-T3.
  - RUN goes through 1 k to RESET to GND, with no external pull-up (as in the HWDG minimal design).
  - SWD J2 order is SWCLK/GND/SWDIO, matching the debug probe.
  - BOOTSEL is QSPI_SS through 1 k to SW1 to GND.
- **QSPI, pin by pin:** SS 60→/CS 1, SD1 59→DO 2, SD2 58→/WP 3, SD0 57→DI 5, SCLK 56→6, SD3 55→/HOLD 7. The flash VCC (8) is on 3V3 and GND (4) on GND.
- **USB:** 27 R series on DP/DM.
- **SN74CBTD3861** (DBQ pinout p.1; all packages share it): NC 1, A1-A10 = 2-11, GND 12, B10-B1 = 13-22, OE 23, VCC 24. This matches the KiCad symbol. OE low = A connected to B (p.1 function table), so OE tied to GND is correct, and "control inputs held at VCC or GND" is met.
- **CBTD3861 with VCC = 0 (Amiga off, board on USB-C):** the gate drive is at 0 V, so the switch stays open whether B is pulled high to 3.3 V or not. The I/Os have no clamp to VCC, so there is no back-feed path. The board cannot be powered off while the Amiga is on, because the Amiga powers it through D1 and LDO EN is tied to IN. The FT pins are rated "very little current below 3.63 V with IOVDD = 0" (DS p.1333). See "Could not verify" for Ioff.
- **The 10 k A-side pull-ups with open-collector Amiga keyboard and port lines:** correct. They sit in parallel with the Amiga's own pull-ups, the board only sinks, and kbin gets the pull-ups a real keyboard expects. The sink current through the switch (≤ a few mA) is within the RP2350 drive and the CBTD 128 mA rating.
- **TS3USB30E (DGS):**
  - Pinout (p.3): 1 S, 2 D1+, 3 D2+, 4 D+, 5 GND, 6 D−, 7 D2−, 8 D1−, 9 OE, 10 VCC. This matches the symbol and footprint.
  - The truth table (p.12) gives S = L → D = D1, which is USB-C. With R48 and the GPIO17 reset pull-down, USB-C is selected at power-up and BOOTSEL.
  - OE is active-low, so tying it to GND enables the switch. VCC = 3.3 V is within the 3.0-4.3 V range.
  - The I/O stays below VCC + 0.3 V for full-speed USB. Bandwidth is 1400 MHz, so the mux is transparent to the RP2350's FS/LS device and host traffic.
- **USB-C and USB-A:**
  - USB-C: 5.1 k on each CC (sink). A6/B6 and A7/B7 are tied. VBUS goes to D2.
  - USBLC6 U2 and U4: I/O1 = pins 1/6, I/O2 = pins 3/4, GND = 2, VBUS = 5. The flow-through pairs are on the same line and VBUS is on the right rail.
  - USB-A VBUS is SYS5V through the 500 mA F2, with 150 uF.
- **TLV75733 (DBV):** pinout IN 1, GND 2, EN 3, NC 4, OUT 5. EN tied to IN is allowed (p.15). 1 uF in and out meets the ≥0.47 µF effective requirement (p.3), and the 10 uF extra is within the 200 µF max. The dropout of 425 mV max at 1 A sits well below the ~4.2-4.7 V input.
- **Power path:** the D1/D2 OR orientation is correct. The amiga_sense divider gives 2.5 V at 5 V, within the ADC2 range.
- **RM2:** every pad matches RM2.md Table 2.
  - SCLK 3 → GPIO29. DIN 5 → GPIO24 direct. DOUT 6 → 470 R → GPIO24. nIRQ 10 → 10 k → GPIO24. CS 9 → GPIO25. WL_ON 12 and BT_ON 13 → GPIO23.
  - VDDIO 14 and VIN 16 → FB1 → +3V3, with 4.7 uF + 100 nF.
  - GND is on 1/4/7/11/15/21. NC is on 2/19/20. The CYW GPIO 8/17/18 pads are unused.
- **Amiga connectors:** the DE9 pinout matches the Amiga (1 V, 2 H, 3 VQ, 4 HQ, 5 b3/POTX, 6 b1, 8 GND, 9 b2/POTY). The mini-DIN6 follows the A4000/PS2 positions, and J1/J8 follow the A500 8-pin header. All are identical to rev6.
- **ESD:** a PESD5V0S1BA sits on all 19 Amiga-facing lines and on both LED lines.
- **Value against footprint:** these all agree: QSOP-24 for DBQ, VSSOP/MSOP-10, SOIC-8 208 mil, SOT-23-5, QFN-60 EP 3.4, 3225 crystal, 4×0603 convex array (pin mapping 1-4 / 8-5 correct), and SOD-323(F).

## Could not verify

- **WS2812B-2020-V6 datasheet:** I did not fetch it. Not checked: its VDD range (3.3 V min according to the survey), VIH, and the pin order (taken as 1 DOUT, 2 GND, 3 DIN, 4 VDD from the KiCad symbol).
- **USBLC6-2SC6:** the ST datasheet download failed. The pinout was checked against the KiCad symbol and rev6 only.
- **SS34 (MDD) reverse leakage at 5 V:** I have no datasheet figures. The magnitude in finding 5 comes from typical SS34 datasheets.
- **SN74CBTD3861 behaviour at VCC = 0 (Ioff / partial power-down):** the datasheet does not specify it. My conclusion comes from the device structure, not a guaranteed figure.
- **Whether the AOTA dot lands on footprint pad 1 in the JLC placement rotation**, and the AOTA land pattern against the Murata DFE201610P land.
- **The RP2350 stepping on the C42411118 reel** and whether its E9 fix applies.
- **The PESD5V0S1BA standoff and leakage at 5.25 V.** It was carried over from rev6 unchanged.
- **The current `.kicad_sch`:** it was rewritten after the netlist/PDF export, and I did not re-export it.
- **Amiga-side pull-up values and the POT-line behaviour with 10 k:** I reasoned from Amiga architecture, not a schematic. Behaviour is unchanged from rev6.
