# rev7 amigahid-pico-UX: JLCPCB / LCSC parts survey

Snapshot taken 2026-10-14 from JLCPCB's parts-search JSON API
(`jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList/v2`), which returns
the same data as jlcpcb.com/parts.

- **Lib** is JLCPCB's library type: `basic` (no per-part loading fee) or `ext` (extended; JLCPCB
  charges a one-time loading fee for each unique extended part per order).
- **Stock** is JLCPCB/LCSC stock at the time of the query.
- **@10** is the unit price in USD at the qty-10 price break.
- **KiCad footprint** names are from the KiCad library installed on this machine
  (`/usr/share/kicad/footprints`) or from the project's `kicad/rev7/rev7.pretty`.
- LCSC codes are copied from the API output. None were typed in from memory.
- `C99xxxxxxx` "JLCPCB Assembly" placeholder entries were ignored.

## Table

| # | Function | LCSC | MPN (maker) | Package | Lib | Stock | @10 USD | KiCad footprint | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 1 | MCU | **C42411118** | RP2350A (Raspberry Pi) | QFN-60-EP 7x7 | ext | 6,339 | 1.204 | `Package_DFN_QFN:QFN-60-1EP_7x7mm_P0.4mm_EP3.4x3.4mm` | JLCPCB's internal name is "SC1509(13)-A4", which suggests the A4 stepping (not verified on the reel). |
| 1a | RP2350 core-VREG inductor (required by RP2350) | **C42411119** | AOTA-B201610S3R3-101-T (Abracon) | 0806 / 2016 | ext | **70** | 0.315 | none in stock KiCad under this name; draw from the RP2350 HW guide, or `Inductor_SMD:L_Murata_DFE201610P` as a 2016 base | This is the part the RP2350 hardware design guide calls for. Only 70 in stock, which caps a run at 70 boards. |
| 2 | 16 MB QSPI flash | **C97521** | W25Q128JVSIQ (Winbond) | SOIC-8 208 mil (5.3x5.3 body) | **basic** | 40,345 | 2.225 | `Package_SO:SOIC-8_5.3x5.3mm_P1.27mm` | **Recommended.** Basic part, no loading fee. |
| 2b | alt | C113767 | W25Q128JVSIQ (Winbond) | SOIC-8 208 mil | ext | 6,066 | 2.170 | same | Second LCSC listing of the same part. |
| 2c | alt | C2928891 | GD25Q128ESIGR (GigaDevice) | SOP-8 208 mil | ext | 15,468 | 1.344 | same | Not tested with RP2350 here. GD25Q is a common RP2040/RP2350 boot flash, but check QE-bit handling in boot2 or bootrom. |
| 2d | alt (WSON) | C190862 | W25Q128JVPIQ | WSON-8-EP 6x5 | ext | 20,306 | 3.056 | KiCad WSON-8 6x5 (not checked) | |
| 2e | alt (WSON) | C2456297 | W25Q128JVEIQ | WSON-8-EP 8x6 | ext | 17,920 | 3.503 | | |
| — | | — | W25Q128 in SOIC-8 150 mil (3.9x4.9) | — | — | — | — | — | No 128 Mbit Winbond in 150 mil was found. 128 Mbit comes in 208 mil or WSON. |
| 3 | 5 V to 3V3 bus switch x2 | **C44508** | SN74CBTD3861DBQR (TI) | SSOP-24 150 mil (QSOP), 0.635 pitch | ext | **103** | 0.842 | `Package_SO:SSOP-24_3.9x8.7mm_P0.635mm` (= `QSOP-24_3.9x8.7mm_P0.635mm`) | Two per board, so about 51 boards. Low stock. |
| 3b | alt pkg | C1880246 | SN74CBTD3861DBR (TI) | SSOP-24 208 mil, 0.65 pitch | ext | 24 | 0.540 | `Package_SO:SSOP-24_5.3x8.2mm_P0.65mm` | Different land pattern. |
| 3c | alt pkg | C2674494 | SN74CBTD3861DGVR | TVSOP-24 4.4 mm, 0.4 pitch | ext | 3 | 1.248 | `TSSOP-24_4.4x5mm_P0.4mm` | Effectively unstocked. |
| 3d | alt pkg | C2655473 / C1547190 | SN74CBTD3861PWR / PW (TSSOP-24) | TSSOP-24 0.65 | ext | **0** | — | `Package_SO:TSSOP-24_4.4x7.8mm_P0.65mm` | Unstocked. |
| 3e | alt pkg | C2674522 / C1547166 / C7633 | SN74CBTD3861DWR / DW / DWRG4 (SOIC-24W) | SOIC-24W | ext | **0** | — | `Package_SO:SOIC-24W_7.5x15.4mm_P1.27mm` | Unstocked. |
| 3f | 2nd source | C551308 | CBTD3861PW,118 (Nexperia) | TSSOP-24 | ext | **0** | — | TSSOP-24 0.65 | Unstocked. |
| 3g | 2nd source | C551307 | CBTD3861BQ,118 (Nexperia) | DHVQFN-24 3.5x5.5 | ext | **0** | — | | Unstocked. No Diodes Inc CBTD3861 equivalent was found on LCSC. |
| 4 | USB2 2:1 mux | **C130085** | TS3USB221RSER (TI) | UQFN-10 1.5x2, 0.5 pitch | ext | 65,246 | 0.306 | `Package_DFN_QFN:Texas_UQFN-10_1.5x2mm_P0.5mm` | **Recommended.** S=L selects channel 1 (D to 1D±), /OE=L enables. Add an S pull-down and an /OE pull-down so the power-on channel is defined. VCC 2.3-3.6 V. |
| 4b | alt | C128396 | TS3USB221ARSER (TI) | UQFN-10 1.5x2 | ext | 86,348 | 0.241 | same | 'A' version, 900 MHz BW. Same land. |
| 4c | alt | C129313 | TS3USB221ERSER (TI) | UQFN-10-EP 1.5x2 | ext | 6,965 | 0.373 | same plus EP (check) | |
| 4d | alt | C324071 | TS3USB221DRCR (TI) | VSON-10-EP 3x3 | ext | 1,039 | 0.731 | `Package_SON:VSON-10-1EP_3x3mm_P0.5mm_EP1.65x2.4mm` (check EP) | Larger and easier to inspect. |
| — | | — | TS3USB221 "DGS" (VSSOP-10) | — | — | — | — | — | No such orderable TS3USB221 package was found. For VSSOP-10/MSOP-10 use TS3USB30EDGSR or FSUSB42MUX below. |
| 4e | alt | C131998 | TS3USB30EDGSR (TI) | VSSOP-10 0.5 | ext | 2,186 | 0.622 | `Package_SO:MSOP-10_3x3mm_P0.5mm` | VCC 3.0-4.3 V. |
| 4f | alt | C73880 | TS3USB30ERSWR (TI) | UQFN-10 1.4x1.8 | ext | 18,640 | 0.444 | `Package_DFN_QFN:Texas_RSW0010A_UQFN-10_1.4x1.8mm_P0.4mm` | |
| 4g | alt | C11355 | FSUSB42MUX (onsemi) | MSOP-10 | ext | 19,102 | 0.311 | `MSOP-10_3x3mm_P0.5mm` | |
| 4h | alt | C11145 | FSUSB42UMX (onsemi) | UFQFN-10 1.4x1.8 | ext | 20,030 | 0.339 | `UQFN-10_1.4x1.8mm_P0.4mm` | |
| 4i | alt | C11143 | FSUSB30MUX (onsemi) | MSOP-10 | ext | 219 | 0.720 | MSOP-10 | Low stock. |
| 4j | alt | C154899 | PI3USB102EZLEX (Diodes) | TQFN-10 1.3x1.6 | ext | 6,244 | 0.282 | `UQFN-10_1.3x1.8mm_P0.4mm` is NOT the same. Needs a custom 1.3x1.6 land. | |
| 4k | alt | C150097 | CH442E (WCH) | MSOP-10 | ext | 29,130 | 0.217 | MSOP-10 | 4-5.5 V supply. Cheap. |
| 5 | 10k x4 isolated array 0603 | **C29718** | 4D03WGJ0103T5E (Uniroyal) | 0603x4 convex | **basic** | 1,989,692 | 0.0075 | `Resistor_SMD:R_Array_Convex_4x0603` | **Recommended.** Basic part. |
| 5b | alt | C107374 | YC164-JR-0710KL (Yageo) | 0603x4 | ext | 102,333 | 0.023 | same | |
| 5c | 0402x4 | C25725 | 4D02WGJ0103TCE (Uniroyal) | 0402x4 | ext (preferred) | 120,455 | 0.0055 | `Resistor_SMD:R_Array_Convex_4x0402` | Flagged "preferred". |
| 5d | 0402x4 | C326872 | YC124-JR-0710KL (Yageo) | 0402x4 | ext | 206,003 | 0.0144 | same | |
| 6 | 12 MHz crystal | **C20625731** | ABM8-272-T3 (Abracon) | SMD3225-4P | ext | 6,649 | 0.511 | `Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm` | The Pico 2 / RP2350 HW-guide part: 10 pF load, 50 Ω ESR. Drop-in for the reference design. |
| 6b | alt | C50430 | X322512MMB4SI (YXC) | SMD3225-4P | ext | 14,280 | 0.106 | same | 10 pF load, ±10 ppm, 80 Ω ESR. Closest cheap match to the ABM8-272 load. Check ESR against the RP2350 XOSC guidance. |
| 6c | basic alt | C9002 | X322512MSB4SI (YXC) | SMD3225-4P | **basic** | 125,939 | 0.095 | same | Basic part, but **20 pF load**. Needs bigger load caps (about 33 pF) and the startup margin needs re-checking. |
| 7 | RGB LED 2020 | **C52917434** | WS2812B-2020-V6 (Worldsemi) | SMD2020-4P | ext | 941,463 | 0.091 | `LED_SMD:LED_WS2812B-2020_PLCC4_2.0x2.0mm` | **Stocked.** Datasheet range 3.3-5.3 V. |
| 7b | | C965555 | WS2812B-2020 (Worldsemi, old) | 2.2x2 | ext | 1 | — | | Unstocked. Use the V6. |
| 7c | alt 2020 | C5349955 | XL-2020RGBC-2812B (XINGLIGHT) | 2020 | ext | 111,663 | 0.101 | same 2020 footprint (check pin order) | |
| 7d | reverse mount | C5149201 | SK6812MINI-E (OPSCO) | 3.2x2.8 reverse-mount | ext | 157,589 | 0.080 | `LED_SMD:LED_SK6812MINI-E_3.2x2.8mm_P1.5mm_ReverseMount` | **Stocked**, but it is reverse-mount: the body sits in a board cutout and the light comes out the **opposite** side from the solder side. With all parts on top it shines **down through the board**. Only use it if that is what you want. |
| 7e | 5050 | C2761795 | WS2812B-B/T (Worldsemi) | SMD5050-4P | ext | 60,468 | 0.107 | `LED_SMD:LED_WS2812B_PLCC4_5.0x5.0mm_P3.2mm` | Stocked. |
| 7f | 5050 | C2874885 | WS2812B-V5/W | SMD5050-4P | ext | 53,741 | 0.110 | same | |
| 7g | 5050 | C2843785 | XL-5050RGBC-2812B | SMD5050-4P | ext | 670,523 | 0.055 | same (check pin order) | |
| 8a | 6x6 tact, H 9.5, SMD | **C455119** | TS-1002S-09526C (XUNPU) | SMD-4P 6x6 | ext | 12,662 | 0.055 | `Button_Switch_SMD:SW_SPST_PTS645Sx43SMTR92`-style 6x6 gull-wing (check pad spacing against the datasheet) | |
| 8a' | 6x6 tact, H 9.5, THT | C589907 | 1-1825910-4 (TE) | THT 6x6 | ext | 31,089 | 0.167 | `Button_Switch_THT:SW_PUSH_6mm_H9.5mm` | |
| 8a'' | | C127479 | SKHHBSA010 (Alps) | THT 6x6 | ext | 6,497 | 0.089 | `SW_PUSH_6mm_H9.5mm` | |
| 8b | 6x6 tact, H 13, SMD | **C2837539** | KH-6X6X13H-STM (Kinghelm) | SMD-4P 6x6 | ext | 6,563 | 0.042 | 6x6 SMD gull-wing (check) | |
| 8b' | | C455126 | TS-1002S-13026C (XUNPU) | SMD-4P 6x6 | ext | 8,280 | 0.063 | same | |
| 8b'' | 6x6 tact, H 13, THT | C2837523 | KH-6X6X13H-TJ (Kinghelm) | THT 6x6 | ext | 10,408 | 0.022 | `Button_Switch_THT:SW_PUSH_6mm_H13mm` | |
| 8b''' | | C513492 | 2-1825910-7 (TE) | THT 6x6 | ext | 20,397 | 0.128 | `SW_PUSH_6mm_H13mm` | |
| 8c | side-actuated, SMD 4x3 | **C720477** | TS-1088-AR02016 (XUNPU) | SMD 4x3, side push | **basic** | 671,309 | 0.053 | Not in stock KiCad. Needs a custom footprint from the XUNPU drawing or the EasyEDA library. | **Only basic side-actuated switch found.** |
| 8c' | side-actuated, THT 6x6 R/A | C557599 | TS665ZJ (SHOU HAN) | THT R/A 6.2x7.5 | ext | 74,822 | 0.036 | Probably close to `SW_Tactile_SPST_Angled_PTS645Vx31-2LFS`; **not checked** | |
| 8c'' | C&K PTS645V (R/A) | C19346875 etc. | PTS645VH15/31/39, VL58 | THT R/A | ext | **0** | — | `SW_Tactile_SPST_Angled_PTS645Vx*` | Unstocked. |
| 8d | (TS-1187A) | C318884 | TS-1187A-B-A-B (XKB) | SMD 5.1x5.1, H 1.5 | **basic** | 320,946 | 0.021 | `Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A` | Basic, but this is a 5.1 mm **top**-actuated low-profile switch, not 6x6 and not side-actuated. |
| — | basic 6x6 | — | — | — | — | — | — | — | **No basic 6x6 tactile switch exists** (a JLCPCB query filtered to `basic` returned only TS-1187A and TS-1088). Every tall 6x6 is extended. |
| 9a | mini-DIN6 PS/2 F R/A | **C6089346** | MD-60S (CUI / Same Sky) | THT | ext | **50** | 2.168 | `rev7:MiniDIN6_PS2_Socket_CUI_MD-60S` (project) | Exact part for the existing footprint. Only 50 in stock. |
| 9a' | alt | C77848 | DS1093-01-PN60 (CONNFLY) | THT | ext | 2,060 | 0.348 | **Does not fit the MD-60S footprint as-is.** Same 6.80 / 2.60 pin grid, 2.50 row pitch and 1.1 holes, but it adds **two Ø2.30 side locating pegs 13.70 apart** plus a centre peg. Needs its own footprint, and the row-to-face distance must be checked against the drawing. | |
| 9a'' | alt | C2926814 | HYC03-MDC603-128B (HOAUC) | THT | ext | 721 | 0.359 | **Different grid**: outer 6.5 / inner 3.0, rows 8.5 / 11.3, Ø2.5 side pegs 13.7 apart. Not compatible. | |
| 9b | USB-A F R/A THT | **C564920** | 676432910 (Molex 67643) | THT | ext | 2,099 | 0.739 | `rev7:USB_A_Molex_67643_Horizontal` / `Connector_USB:USB_A_Molex_67643_Horizontal` | Exact Molex part. |
| 9b' | | C293351 | 676432911 (Molex) | THT | ext | 437 | 1.060 | same | |
| 9b'' | cheap | C456018 | AF 90 WJDG (SHOU HAN) | THT | ext | 77,853 | 0.044 | Probably a 67643-style land; **not checked** | |
| 9c | USB-C 16P | **C165948** | TYPE-C-31-M-12 (Korean Hroparts) | SMD + 4 shell slots | ext | 386,092 | 0.186 | `Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12` | Listed as **extended**, not basic, today. Do **not** reuse the USB4105 footprint for it (see notes). |
| 9c' | USB-C 16P | C5184243 | USB4105-GF-A-120 (GCT) | SMD + shell stakes | ext | 1,982 | 0.721 | `Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal` | The suffix appears to be shell-stake length (1.20 mm). Best GCT choice for a 1.6 mm board. |
| 9c'' | | C3025063 | USB4105-GF-A-060 (GCT) | | ext | 1,051 | 1.201 | same | 0.60 mm stakes. |
| 9c''' | | C3020560 | USB4105-GF-A (GCT) | | ext | **0** | — | same | Unstocked. |
| 9d | 1x4 2.54 F socket, 8.5 mm | **C2897367** | PM254-1-04-Z-8.5 (HCTL) | THT | ext | 13,358 | 0.116 | `Connector_PinSocket_2.54mm:PinSocket_1x04_P2.54mm_Vertical` | Standard LCSC height is **8.5 mm**. No 8.4 mm part was found. |
| 9d' | | C22373890 | HC-PM254-8.5H-1x4PZ-02A (Hong Cheng) | THT | ext | 5,103 | 0.067 | same | |
| 9e | 1x3 header | **C2937625** | PZ254V-11-03P (XFCN) | THT | ext | 947,349 | 0.020 | `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical` | |
| 9f | 1x8 header | **C492407** | PZ254V-11-08P (XFCN) | THT | ext | 332,162 | 0.048 | `Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical` | |
| 10 | DE9 M R/A .318, 4-40 hex standoff, boardlock | **C305940** | **5747840-6** (TE AMPLIMITE HD-20) | THT | ext | **371** | 3.561 | `rev7:DSUB-9_Pins_Horizontal_P2.77x2.84mm_EdgePinOffset7.70mm_Housed_MountingHolesOffset9.12mm` | **Recommended (stocked at JLCPCB).** "Detail B": 4-40 **female screwlock (hex standoff)**, Ø3.84 bore, projects 5.97 mm ahead of the flange, plus forked boardlocks. TE land: 24.99 mm hole pitch, Ø3.17 holes, 1.42 mm behind the 5-pin row. |
| 10b | DE9 M R/A .318, 4-40 **flush threaded insert**, boardlock | C305939 | 5747840-4 (TE) | THT | ext | **8** | 3.253 | same | "Detail A" flush insert, 30 µin Au. Digi-Key A32092-ND showed 533 in stock at $3.141 (qty 10). |
| 10c | as 10b, gold flash | C591957 | 5747840-3 | THT | ext | 0 | — | same | |
| 10d | as 10a, gold flash | C592271 | 5747840-5 | THT | ext | 33 | 2.708 | same | Same hardware as -6. |
| 10e | no hardware (Ø3.18 hole) | C5164218 | 5747840-2 | THT | ext | 8 | 4.677 | same | |
| 10f | NorComp, flush insert + boardlock | C17458381 | 182-009-113R531 | THT | ext | 0 | — | same | Digi-Key 182-09ME-ND showed 10,968 at $2.41 (the qty break was not visible). |
| 10g | NorComp, hex standoff + boardlock | C3140936 | 182-009-113R561 | THT | ext | 0 | — | same | Digi-Key NOR1354-ND showed 5,097 at $2.97. |
| 10h | NorComp, hex standoff, **no** boardlock | C17629708 | 182-009-113R481 | THT | ext | 0 | — | same (boardlock holes become plain holes) | |
| 10i | NorComp, flush insert, **no** boardlock | C17243730 | 182-009-113R451 | THT | ext | 0 | — | same | |
| 10j | Kycon .318 with hex jackscrews, no boardlock | C17189671 | K22X-E9P-NJ (Kycon) | THT | ext | 0 | — | same (Kycon land: Ø3.05 holes, 24.99 pitch) | -NJ15 / -NJ30 are only plating variants. |
| 10x | (not .318) | C3013503 | L717SDE09P1ACH4F (Amphenol) | THT | ext | 307 | 2.184 | **does not match** | The drawing is the "European footprint": 2.54 mm row pitch, holes on a different line. **Do not use.** |
| 11a | 5 V OR-ing schottky | **C8678** | SS34 (MDD) | SMA | **basic** | 4,050,045 | 0.035 | `Diode_SMD:D_SMA` | **Recommended.** Basic part. 3 A, 40 V, 0.55 V. |
| 11a' | smaller | C41029 | DSK34 (MDD) | SOD-123FL | ext | 221,382 | 0.039 | `Diode_SMD:D_SOD-123F` | 3 A, 40 V. |
| 11a'' | smaller 2 A | C84630 | DSS24 (Starsea) | SOD-123F | ext | 22,648 | 0.039 | `D_SOD-123F` | |
| 11a''' | | C50645 | SS24 (MDD) | SMA | ext | 774,321 | 0.026 | `D_SMA` | |
| 11b | USB ESD | **C7519** | USBLC6-2SC6 (ST) | SOT-23-6 | ext | 34,305 | 0.177 | `Package_TO_SOT_SMD:SOT-23-6` | |
| 11b' | | C2687116 | USBLC6-2SC6 (UMW) | SOT-23-6 | ext | 59,904 | 0.048 | same | |
| 11c | line ESD | **C19224** | PESD5V0S1BA,115 (Nexperia) | SOD-323 | ext | 21,084 | 0.085 | `Diode_SMD:D_SOD-323` | |
| 11c' | | C2827694 | PESD5V0S1BA (TECH PUBLIC) | SOD-323 | ext | 535,348 | 0.036 | same | |
| 12a | Pico-style 3V3 | C3187669 | RT6150B-33GQW (Richtek) | WDFN-10 2.5x2.5 | ext | **0** | — | `Package_SON:WSON-10-1EP_2.5x2.5mm_P0.5mm_EP1.2x2mm` (check) | **Unstocked.** Note: this is a **buck-boost**, not a buck. |
| 12a' | adjustable version | C147972 | RT6150BGQW | WSON-10-EP 2.5x2.5 | ext | 1,251 | 0.762 | same | Needs a feedback divider. |
| 12a'' | its inductor | C2041346 | ASPI-0630LR-3R3M-T15 (Abracon) | 6.7x7.2 | ext | 4,020 | 0.400 | `Inductor_SMD:L_Abracon_ASPI-0630LR` | Oversized for this duty. Any 3.3 µH, ≥1.5 A part works, e.g. C47327202 APS0630M3R3A, 66,340 in stock, $0.114. |
| 12b | **LDO (recommended)** | **C485517** | TLV75733PDBVR (TI) | SOT-23-5 | ext | 81,770 | 0.200 | `Package_TO_SOT_SMD:SOT-23-5` | 1 A, dropout about 0.26 V at 400 mA. Vin max 5.5 V (6 V abs max). |
| 12b' | LDO | C51118 | AP2112K-3.3TRG1 (Diodes) | SOT-23-5 | ext | 12,920 | 0.172 | same | 600 mA, Vin max 6 V. |
| 12b'' | LDO | C82942 | ME6211C33M5G-N (Microne) | SOT-23-5 | ext | 178,093 | 0.061 | same | 500 mA. Little margin with RM2 radio peaks. |
| 12b''' | basic LDO | C6186 | AMS1117-3.3 | SOT-223 | **basic** | 1,165,289 | 0.208 | `SOT-223-3_TabPin2` | Basic, but 1.1 V dropout and 5 mA Iq. Marginal at 4.6 V in after the schottky. |
| — | basic LDO | C5446 | XC6206P332MR-G | SOT-23-3 | basic | 407,400 | 0.143 | | 200 mA only. Too small. |

## Notes and what could not be verified

1. **RP2350A (C42411118)** is stocked, 6,339 units. Two other things the rev7 BOM must include:
   - **RP2350 VREG inductor.** The core switching regulator needs the 3.3 µH AOTA-B201610S3R3-101-T,
     which the HW design guide specifies by part number and orientation. LCSC has **only 70**. Either
     order more through JLCPCB global sourcing or consignment, or accept a 2016 3.3 µH substitute
     (C411344 CMLW201610S3R3MST 1,644 pcs, C167673 FNR201610S3R3MT 1,396 pcs). Substitutes go against
     Raspberry Pi's guidance, so they would be a deliberate deviation.
   - **Pico 2 flash.** I did not confirm which part Pico 2 uses. Any W25Q128JV boots on RP2350.
2. **Flash package.** W25Q128JVSIQ is SOIC-8 **208 mil**: 5.3 mm body, about 7.9 mm across the leads.
   It is not the 150 mil 3.9x4.9 body. There is no 128 Mbit Winbond in 150 mil on LCSC. WSON options
   are 6x5 (JVPIQ) and 8x6 (JVEIQ). C97521 is a **basic** part, which avoids a loading fee.
3. **SN74CBTD3861DBQR.** Only 103 in stock, and the board needs two, so about 51 boards. Every other
   package and the Nexperia second source are at 0 or near 0. Stock should be locked in (JLCPCB
   "pre-order / reserve") before committing the land. DBQ is the 150 mil QSOP land at 0.635 mm pitch.
   It is not interchangeable with DBR (208 mil, 0.65 mm).
4. **TS3USB221.** RSE (UQFN-10 1.5x2) is well stocked. TI makes no "DGS/VSSOP-10" TS3USB221. Power-on
   state is set only by the S and /OE pins, so give each a pull resistor.
5. **USB-C footprint fit.** The KiCad HRO TYPE-C-31-M-12 and GCT USB4105 footprints have the same
   peg holes (±2.89 mm, Ø0.65) and front shell slots (±4.32). They differ in three places:
   - signal/power pad row: y = −4.045 (HRO) vs −3.68 (GCT)
   - pad length: 1.45 vs 1.15 mm
   - VBUS/GND pad x: ±3.25/±2.45 vs ±3.20/±2.40

   The rear shell slot is 1.2 mm long on the HRO footprint and 1.4 mm on the GCT one. The HRO part
   would probably still solder on the GCT land, but use `USB_C_Receptacle_HRO_TYPE-C-31-M-12` for the
   HRO part. TYPE-C-31-M-12 is **extended** in today's data, not basic.
6. **Mini-DIN.** The only exact fit for the project MD-60S footprint is the CUI part (50 in stock).
   The CONNFLY DS1093-01-PN60 has the same pin grid but needs extra peg holes. I read this from the
   LCSC drawing and have **not** checked it against a physical part. The HOAUC part has a different
   grid.
7. **DE9 hardware decoding** (from the TE 5747840 customer drawing rev D and the NorComp 182-YYY-113RYY1
   drawing rev 12, both from LCSC):
   - TE: -2 = no hardware (Ø3.18 hole); -3/-4 = **4-40 flush threaded insert** + boardlock;
     -5/-6 = **4-40 female screwlock (hex standoff)** + boardlock. Gold flash vs 30 µin applies within
     each pair.
   - NorComp R-codes: 16/43/55 = Ø.120 hole; 17/45/53 = 4-40 flush insert; 18/48/56 = 4-40 hex
     standoff female screwlock. Within each set the three codes are inside-ground-strap / plain metal
     shell / fork boardlock (55/53/56 = boardlock). The final digit is plating: 1 = gold flash,
     2 = 30 µin, 7 = selective.
   - So the exact ordering codes for **".318, male, R/A, 4-40"** are:
     - hex standoff + boardlock: **TE 5747840-6** (LCSC C305940, stocked) or NorComp 182-009-113R561
     - flush insert + boardlock: **TE 5747840-4** or NorComp 182-009-113R531
     - no boardlock: NorComp 182-009-113R481 (hex) / R451 (insert), or Kycon K22X-E9P-NJ (hex)
   - **Case wall in front of the flange:**
     - **Flush inserts (-3/-4, R531/R451):** the insert is flush with the flange face and its body
       stays within the flange and bracket. Nothing protrudes forward. A wall of thickness t in front
       of the flange takes t off the jackscrew's thread engagement. With a typical about 3 mm Amiga
       jackscrew, a wall over about 1.5 mm leaves too little thread.
     - **Hex standoffs (-5/-6, R561/R481, Kycon -NJ):** the standoff projects about 6 mm ahead of the
       flange (TE 5.97 mm, Kycon 6.1 mm). The wall needs clearance holes for the hex, about 4.8 mm
       across flats, so about 5.6 mm across corners, plus clearance. The standoff then carries the
       thread out through the wall.
     - Neither style puts a nut or boss below the flange's bottom edge on the board side. The rear
       nut or rivet sits inside the bracket eyelet above the PCB.
   - This is read from the drawings, not measured on parts.
   - Amphenol L717SDE09P1ACH4F (stocked) uses the **European** footprint, not .318. Do not use it.
8. **Land check for the .318 DE9.** The TE land has Ø3.17 boardlock holes 24.99 mm apart, 1.42 mm
   behind the 5-pin row, rows 2.84 mm apart. That matches the rev7 footprint
   (`...EdgePinOffset7.70mm_..._MountingHolesOffset9.12mm`, 9.12 − 7.70 = 1.42).
9. **3V3 regulator recommendation: TLV75733PDBVR (C485517).** The board runs from 5 V only, about
   4.6 V after an SS34, at under 400 mA. A 1 A LDO with about 0.3 V dropout gives more than 1 V of
   headroom and removes the inductor and the buck-boost layout. Dissipation is about 1.3 V × 0.4 A ≈
   0.5 W worst case in SOT-23-5. That is OK at room temperature but should be checked thermally. If the
   average is about 150 mA, this concern goes away.

   TLV757P, AP2112K and ME6211 are commonly pin-compatible in SOT-23-5 (1 IN, 2 GND, 3 EN, 4 NC,
   5 OUT), so one land can take any of them. **Check each datasheet** before relying on that.

   The RT6150B-33 fixed version is unstocked. The Pico 1 used a 3.3 µH Murata LQH2HPN3R3MJR
   (C1330752), which is also unstocked. Pico 2 W's inductor part was **not verified**.
10. **Basic-part summary:** W25Q128JVSIQ C97521, 4D03WGJ0103T5E C29718, X322512MSB4SI C9002 (20 pF load
    caveat), TS-1088-AR02016 C720477 (side), TS-1187A-B-A-B C318884, SS34 C8678, AMS1117-3.3 C6186.
    Everything else is extended.
11. **Not verified:**
    - quantity-10 prices on Digi-Key for the NorComp parts (the page showed one price)
    - Mouser stock (fetch timed out)
    - stock or land of the "AF 90" USB-A clone
    - 6x6 SMD tactile pad spacing against a named KiCad footprint
    - TS665ZJ right-angle land
    - footprints for TS-1088 and PI3USB102E, which are not in the stock KiCad library
    - JLCPCB THT assembly availability for each connector. JLCPCB does offer THT assembly, but each
      THT part should be checked as "assemblable" on its part page.

Sources: JLCPCB parts API (2026-10-14). TE 5747840 customer drawing (LCSC C305939 datasheet). NorComp
182-YYY-113RYY1 drawing (LCSC C17458381). Kycon K22X-E9P-NJ drawing (LCSC C17244334). Amphenol
L717SD---P1ACH4F drawing (LCSC C3013503). CONNFLY DS1093-01 (C77848) and HOAUC HYC03-MDC603-128B
(C2926814) drawings. Digi-Key search page for 5747840-4.
