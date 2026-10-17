# rev7 distributor BOM (2026-10-17)

Source `kicad/rev7/fab/amigahid-pico-rev7-bom.csv`; mapping in `tools/rev7_distributor_bom.py`. Parts are picked per line: the JLC part when the distributor stocks it, otherwise a standard-brand equivalent (marked *sub*), otherwise the cheapest in-stock keyword hit (*kw*). Order quantities are rounded to the seller's minimum/multiple and raised to the next price break whenever that lowers the line total (10 resistors at the 10-piece price are cheaper than 6 at the 1-piece price); the quantity shown is what to order. Totals exclude shipping and VAT.

## Mouser (EUR)

| Refs | /board | Part | Mfr | Mouser # | Stock | x2 qty / unit / line | x10 qty / unit / line |
|---|---|---|---|---|---|---|---|
| C1 | 1 | MLAST21GBB5226MTNC12 *kw* | TAIYO YUDEN | 963-MLAST21GBB5226MT | 8458 | 2 / 1.240 / 2.48 | 10 / 0.753 / 7.53 |
| C2, C6, C10, C14 | 4 | CL21A106KAYQNWE *kw* | Samsung Electro-Mechanics | 187-CL21A106KAYQNWE | 5907 | 10 / 0.108 / 1.08 | 40 / 0.108 / 4.32 |
| C3, C5, C7, C11, C12, C13, C23, C24, C26, C27, C28, C32, C36 | 13 | C0603C104M5RACTU *kw* | KEMET | 80-C0603C104M5R | 4535775 | 26 / 0.032 / 0.83 | 130 / 0.018 / 2.34 |
| C4 | 1 | EEE-FKC151XAP | Panasonic Industry | 667-EEE-FKC151XAP | 145 | 2 / 0.705 / 1.41 | 10 / 0.445 / 4.45 |
| C8, C9, C31 | 3 | CL10B105KB8NQNC *sub* | Samsung Electro-Mechanics | 187-CL10B105KB8NQNC | 9709 | 10 / 0.064 / 0.64 | 30 / 0.064 / 1.92 |
| C20, C29, C38 | 3 | CL05A475MQ5NRNC *sub* | Samsung Electro-Mechanics | 187-CL05A475MQ5NRNC | 2172 | 6 / 0.112 / 0.67 | 30 / 0.069 / 2.07 |
| C21, C22, C25, C30, C39 | 5 | CL05B104KP5NNNC *sub* | Samsung Electro-Mechanics | 187-CL05B104KP5NNNC | 480102 | 10 / 0.022 / 0.22 | 50 / 0.015 / 0.75 |
| C33, C34 | 2 | C0603C150J5GAC *kw* | KEMET | 80-C0603C150J5GAC | 90412 | 10 / 0.028 / 0.28 | 20 / 0.028 / 0.56 |
| C35 | 1 | CL21A475KAQNNNE | Samsung Electro-Mechanics | 187-CL21A475KAQNNNE | 9804 | 2 / 0.138 / 0.28 | 10 / 0.075 / 0.75 |
| C37 | 1 | CL10A475MO8NNNC *kw* | Samsung Electro-Mechanics | 187-CL10A475MO8NNNC | 21240 | 2 / 0.146 / 0.29 | 10 / 0.081 / 0.81 |
| D1, D2, D3 | 3 | SX34_R1_00001 *sub* | Panjit | 241-SX34_R1_00001 | 3405 | 10 / 0.138 / 1.38 | 30 / 0.138 / 4.14 |
| D4, D5, D6 | 3 | 1N5819HW-7-F *sub* | Diodes Incorporated | 621-1N5819HW-F | 416991 | 10 / 0.117 / 1.17 | 30 / 0.117 / 3.51 |
| D10, D11, D12, D20, D21, D22, D23, D24, D25, D26, D27, D28, D29, D30 | 14 | PESD5V0S1BAF *kw* | Nexperia | 771-PESD5V0S1BAF | 26187 | 28 / 0.155 / 4.34 | 140 / 0.135 / 18.90 |
| D13, D14, D15, D16, D17, D18, D19 | 7 | PESD5V0S1BL-QYL *sub* | Nexperia | 771-PESD5V0S1BL-QYL | 16149 | 14 / 0.225 / 3.15 | 100 / 0.143 / 14.30 |
| F1 | 1 | MF-MSMF075-2 | Bourns | 652-MF-MSMF075-2 | 14794 | 2 / 0.275 / 0.55 | 10 / 0.249 / 2.49 |
| F2, F3 | 2 | MF-MSMF050-2 | Bourns | 652-MF-MSMF050-2 | 24322 | 4 / 0.275 / 1.10 | 20 / 0.249 / 4.98 |
| FB1 | 1 | MMZ1608Y601CTAH0 *kw* | TDK | 810-MMZ1608Y601CTAH0 | 4382 | 2 / 0.086 / 0.17 | 10 / 0.040 / 0.40 |
| J1, J8 | 2 | PH1-08-UA *sub* | Adam Tech | 737-PH1-08-UA | 11130 | 4 / 0.112 / 0.45 | 20 / 0.090 / 1.80 |
| J2 | 1 | PH1-03-UA *sub* | Adam Tech | 737-PH1-03-UA | 35458 | 2 / 0.103 / 0.21 | 10 / 0.050 / 0.50 |
| J3 | 1 | 61300411821 | Wurth Elektronik | 710-61300411821 | 10309 | 2 / 0.361 / 0.72 | 10 / 0.361 / 3.61 |
| J4 | 1 | 67643-2910 | Molex | 538-67643-2910 | 4269 | 2 / 1.370 / 2.74 | 10 / 1.140 / 11.40 |
| J7 | 1 | PH2-06-UA *sub* | Adam Tech | 737-PH2-06-UA | 42529 | 2 / 0.086 / 0.17 | 10 / 0.071 / 0.71 |
| J10, J12 | 2 | MD-60S | Same Sky | 490-MD-60S | 4956 | 4 / 1.810 / 7.24 | 20 / 1.540 / 30.80 |
| J13 | 1 | USB4105-GF-A *sub* | GCT | 640-USB4105-GF-A | 403469 | 2 / 0.688 / 1.38 | 10 / 0.581 / 5.81 |
| L1 | 1 | AOTA-B201610S3R3-101-T *sub* | ABRACON | 815-AB201610S3R3101T | 9264 | 2 / 0.181 / 0.36 | 10 / 0.152 / 1.52 |
| LED1 | 1 | WS2812B-2020 **n/a** |  | |  | - | - |
| P1, P2 | 2 | L717SDE09PA4CH4R *sub* | Amphenol Commercial Products | 523-L717SDE09PA4CH4R | 260 | 4 / 1.540 / 6.16 | 20 / 1.370 / 27.40 |
| Q1 | 1 | 2N7002NXAKR *sub* | Nexperia | 771-2N7002NXAKR | 588569 | 2 / 0.120 / 0.24 | 10 / 0.052 / 0.52 |
| Q2, Q3 | 2 | BSS138P,215 *sub* | Nexperia | 771-BSS138P215 | 6803 | 4 / 0.189 / 0.76 | 20 / 0.099 / 1.98 |
| R41, R42, R68 | 3 | RC0603FR-074K7L | YAGEO | 603-RC0603FR-074K7L | 2600253 | 10 / 0.012 / 0.12 | 30 / 0.012 / 0.36 |
| R43, R44 | 2 | RC0603FR-075K1L | YAGEO | 603-RC0603FR-075K1L | 404541 | 10 / 0.015 / 0.15 | 20 / 0.015 / 0.30 |
| R45, R55 | 2 | RC0603FR-07470RL | YAGEO | 603-RC0603FR-07470RL | 518779 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R46, R47 | 2 | RC0603FR-07100KL | YAGEO | 603-RC0603FR-07100KL | 4392461 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R49, R52, R53, R54, R60 | 5 | WR06X1001FTL *kw* | Walsin | 791-WR06X1001FTL | 919 | 10 / 0.010 / 0.10 | 50 / 0.010 / 0.50 |
| R50, R59 | 2 | RC0603FR-0733RL | YAGEO | 603-RC0603FR-0733RL | 598865 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R56, R69 | 2 | RC0603FR-0710KL | YAGEO | 603-RC0603FR-0710KL | 2920832 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R57, R58 | 2 | RC0603FR-0727RL | YAGEO | 603-RC0603FR-0727RL | 43983 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R61, R62, R63, R64, R65, R66, R67 | 7 | RC0402FR-0710KL | YAGEO | 603-RC0402FR-0710KL | 17287018 | 14 / 0.009 / 0.13 | 100 / 0.006 / 0.60 |
| R70, R71 | 2 | RC0603FR-0727RL | YAGEO | 603-RC0603FR-0727RL | 43983 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R72, R73 | 2 | RC0402FR-0715KL | YAGEO | 603-RC0402FR-0715KL | 1573354 | 10 / 0.009 / 0.09 | 20 / 0.009 / 0.18 |
| RN1, RN2, RN4 | 3 | CAY10-103J4LF *sub* | Bourns | 652-CAY10-103J4LF | 105183 | 10 / 0.020 / 0.20 | 30 / 0.015 / 0.45 |
| RN3 | 1 | CAY16-103J4LF *sub* | Bourns | 652-CAY16-103J4LF | 275984 | 2 / 0.086 / 0.17 | 10 / 0.031 / 0.31 |
| SW1, SW2 | 2 | PTS645VK39-2 LFS *sub* | C&K | 611-PTS645VK392 | 23803 | 4 / 0.361 / 1.44 | 20 / 0.312 / 6.24 |
| U1 | 1 | SC1509(13)-A4 *sub* | Raspberry Pi | 358-SC150913 | 2644 | 2 / 0.690 / 1.38 | 10 / 0.688 / 6.88 |
| U2, U4 | 2 | USBLC6-2SC6 | STMicroelectronics | 511-USBLC6-2SC6 | 216153 | 4 / 0.507 / 2.03 | 20 / 0.247 / 4.94 |
| U3 | 1 | TLV75733PDRVR | Texas Instruments | 595-TLV75733PDRVR | 3734 | 2 / 0.439 / 0.88 | 10 / 0.303 / 3.03 |
| U6, U7 | 2 | SN74CBTD3861DBQR | Texas Instruments | 595-SN74CBTD3861DBQR | 1912 | 4 / 0.740 / 2.96 | 20 / 0.529 / 10.58 |
| U8 | 1 | W25Q128JVSIQ TR *sub* | Winbond | 454-W25Q128JVSIQTR | 42 | 2 / 4.950 / 9.90 | 10 / 4.950 / 49.50 |
| U9 | 1 | SC1169 *sub* | Raspberry Pi | 358-SC1169 | 1611 | 2 / 3.440 / 6.88 | 10 / 3.440 / 34.40 |
| U10 | 1 | TCA9554PWR | Texas Instruments | 595-TCA9554PWR | 13593 | 2 / 1.500 / 3.00 | 10 / 1.090 / 10.90 |
| Y1 | 1 | ABM8-272-T3 | ABRACON | 815-ABM8-272-T3 | 10366 | 2 / 0.611 / 1.22 | 10 / 0.530 / 5.30 |

**Totals** (lines with a stocked part): 2 boards 71.84 EUR (35.92/board), 10 boards 296.18 EUR (29.62/board)

Not available at Mouser:

- LED1 (WS2812B-2020, JLC C52917434): tried WS2812B-2020-V6, WS2812B-2020; buy at LCSC / AliExpress (Worldsemi WS2812B-2020)

## Digikey (EUR)

| Refs | /board | Part | Mfr | Digikey # | Stock | x2 qty / unit / line | x10 qty / unit / line |
|---|---|---|---|---|---|---|---|
| C1 | 1 | CL21A226MAQNNNE **n/a** | Samsung Electro-Mechanics | | 0 | - | - |
| C2, C6, C10, C14 | 4 | GRM21BR61E106MA73K *kw* | Murata Electronics | 490-GRM21BR61E106MA73KCT-ND | 9370 | 10 / 0.083 / 0.83 | 50 / 0.059 / 2.95 |
| C3, C5, C7, C11, C12, C13, C23, C24, C26, C27, C28, C32, C36 | 13 | CC0603KRX7R9BB104 | YAGEO | 311-1344-1-ND | 4456541 | 26 / 0.070 / 1.82 | 130 / 0.042 / 5.46 |
| C4 | 1 | 865080345010 *sub* | Würth Elektronik | 732-8420-1-ND | 7647 | 2 / 0.280 / 0.56 | 10 / 0.280 / 2.80 |
| C8, C9, C31 | 3 | UMK107BJ105KA-T *kw* | Taiyo Yuden | 587-2400-1-ND | 184907 | 10 / 0.072 / 0.72 | 30 / 0.072 / 2.16 |
| C20, C29, C38 | 3 | 0402X475M100CT *kw* | Walsin Technology Corporation | 1292-0402X475M100CT-ND | 272 | 10 / 0.068 / 0.68 | 30 / 0.068 / 2.04 |
| C21, C22, C25, C30, C39 | 5 | CL05B104KP5NNNC *sub* | Samsung Electro-Mechanics | 1276-1002-1-ND | 25954963 | 10 / 0.022 / 0.22 | 50 / 0.014 / 0.72 |
| C33, C34 | 2 | 0603N150J500CT *kw* | Walsin Technology Corporation | 1292-1487-1-ND | 36000 | 10 / 0.020 / 0.20 | 20 / 0.020 / 0.40 |
| C35 | 1 | TMK212ABJ475KG-T *kw* | Taiyo Yuden | 587-5478-1-ND | 29648 | 2 / 0.140 / 0.28 | 10 / 0.079 / 0.79 |
| C37 | 1 | CL10A475MO8NNNC *kw* | Samsung Electro-Mechanics | 1276-1904-1-ND | 8686 | 2 / 0.150 / 0.30 | 10 / 0.083 / 0.83 |
| D1, D2, D3 | 3 | SX34_R1_00001 *sub* | Panjit International Inc. | 3757-SX34_R1_00001CT-ND | 3503 | 6 / 0.320 / 1.92 | 30 / 0.195 / 5.85 |
| D4, D5, D6 | 3 | 1N5819HW-7-F *sub* | Diodes Incorporated | 1N5819HW-FDICT-ND | 488459 | 6 / 0.270 / 1.62 | 30 / 0.137 / 4.12 |
| D10, D11, D12, D20, D21, D22, D23, D24, D25, D26, D27, D28, D29, D30 | 14 | PESD5V0S1BA-QX *sub* | Nexperia USA Inc. | 1727-PESD5V0S1BA-QXCT-ND | 4200 | 28 / 0.227 / 6.36 | 140 / 0.143 / 19.96 |
| D13, D14, D15, D16, D17, D18, D19 | 7 | PESD5V0S1BL,315 **n/a** | Nexperia USA Inc. | | 0 | - | - |
| F1 | 1 | MF-MSMF075-2 | Bourns Inc. | MF-MSMF075-2CT-ND | 71059 | 2 / 0.330 / 0.66 | 10 / 0.255 / 2.55 |
| F2, F3 | 2 | MF-MSMF050-2 | Bourns Inc. | MF-MSMF050-2CT-ND | 59484 | 4 / 0.330 / 1.32 | 20 / 0.255 / 5.10 |
| FB1 | 1 | PE-0603FB601ST *kw* | Pulse Electronics | 553-2384-1-ND | 41976 | 2 / 0.090 / 0.18 | 10 / 0.030 / 0.30 |
| J1, J8 | 2 | PH1-08-UA *sub* | Adam Tech | 2057-PH1-08-UA-ND | 19791 | 4 / 0.110 / 0.44 | 20 / 0.096 / 1.92 |
| J2 | 1 | PH1-03-UA *sub* | Adam Tech | 2057-PH1-03-UA-ND | 118346 | 2 / 0.090 / 0.18 | 10 / 0.042 / 0.42 |
| J3 | 1 | PPTC041LFBN-RC *sub* | Sullins Connector Solutions | S7002-ND | 42993 | 2 / 0.370 / 0.74 | 10 / 0.311 / 3.11 |
| J4 | 1 | 0676432910 **n/a** | Molex | | 0 | - | - |
| J7 | 1 | PH2-06-UA *sub* | Adam Tech | 2057-PH2-06-UA-ND | 53441 | 2 / 0.090 / 0.18 | 10 / 0.071 / 0.71 |
| J10, J12 | 2 | MD-60S | Same Sky (Formerly CUI Devices) | CP-2460-ND | 2757 | 4 / 1.840 / 7.36 | 20 / 1.565 / 31.30 |
| J13 | 1 | USB4105-GF-A-120 | GCT | 2073-USB4105-GF-A-120CT-ND | 43301 | 2 / 0.700 / 1.40 | 10 / 0.594 / 5.94 |
| L1 | 1 | AOTA-B201610S3R3-101-T *sub* | Abracon LLC | 535-AOTA-B201610S3R3-101-TCT-ND | 16341 | 2 / 0.180 / 0.36 | 10 / 0.155 / 1.55 |
| LED1 | 1 | WS2812B-2020 **n/a** |  | |  | - | - |
| P1, P2 | 2 | L717SDE09PA4CH4R *sub* | Amphenol ICC (Commercial Products) | L717SDE09PA4CH4R-ND | 23 | 4 / 1.690 / 6.76 | 20 / 1.435 / 28.70 |
| Q1 | 1 | 2N7002NXAKR *sub* | Nexperia USA Inc. | 1727-8643-1-ND | 533234 | 2 / 0.120 / 0.24 | 10 / 0.076 / 0.76 |
| Q2, Q3 | 2 | BSS138P,215 *sub* | Nexperia USA Inc. | 1727-1142-1-ND | 662793 | 4 / 0.180 / 0.72 | 20 / 0.111 / 2.22 |
| R41, R42, R68 | 3 | WR06X4701FTL *kw* | Walsin Technology Corporation | 1292-WR06X4701FTLCT-ND | 32711 | 10 / 0.014 / 0.14 | 30 / 0.010 / 0.30 |
| R43, R44 | 2 | RT0603FRE075K1L *kw* | YAGEO | YAG5966CT-ND | 7938 | 10 / 0.011 / 0.11 | 20 / 0.011 / 0.22 |
| R45, R55 | 2 | RMCF0603FT470R *kw* | Stackpole Electronics Inc | RMCF0603FT470RCT-ND | 587153 | 10 / 0.018 / 0.18 | 25 / 0.013 / 0.33 |
| R46, R47 | 2 | RT0603FRE13100KL *kw* | YAGEO | 13-RT0603FRE13100KLCT-ND | 86346 | 10 / 0.011 / 0.11 | 20 / 0.011 / 0.22 |
| R49, R52, R53, R54, R60 | 5 | RNCP0603FTD1K00 *kw* | Stackpole Electronics Inc | RNCP0603FTD1K00CT-ND | 181118 | 10 / 0.011 / 0.11 | 50 / 0.009 / 0.47 |
| R50, R59 | 2 | RT0603FRE0733KL *kw* | YAGEO | YAG5959CT-ND | 23339 | 10 / 0.011 / 0.11 | 20 / 0.011 / 0.22 |
| R56, R69 | 2 | RNCP0603FTD10K0 *kw* | Stackpole Electronics Inc | RNCP0603FTD10K0CT-ND | 511319 | 10 / 0.011 / 0.11 | 20 / 0.011 / 0.22 |
| R57, R58 | 2 | RT0603FRE0727KL *kw* | YAGEO | YAG5952CT-ND | 3811 | 10 / 0.011 / 0.11 | 20 / 0.011 / 0.22 |
| R61, R62, R63, R64, R65, R66, R67 | 7 | CR0402-FX-1002GLF *kw* | Bourns Inc. | CR0402-FX-1002GLFCT-ND | 4643469 | 14 / 0.013 / 0.18 | 70 / 0.007 / 0.52 |
| R70, R71 | 2 | RT0603FRE0727KL *kw* | YAGEO | YAG5952CT-ND | 3811 | 10 / 0.011 / 0.11 | 20 / 0.011 / 0.22 |
| R72, R73 | 2 | WR04X1502FTL *kw* | Walsin Technology Corporation | 1292-WR04X1502FTLCT-ND | 281880 | 10 / 0.011 / 0.11 | 25 / 0.008 / 0.19 |
| RN1, RN2, RN4 | 3 | CAY10-103J4LF *sub* | Bourns Inc. | CAY10-103J4LFCT-ND | 267602 | 10 / 0.020 / 0.20 | 30 / 0.017 / 0.50 |
| RN3 | 1 | CAY16-103J4LF *sub* | Bourns Inc. | CAY16-103J4LFCT-ND | 264147 | 2 / 0.090 / 0.18 | 10 / 0.032 / 0.32 |
| SW1, SW2 | 2 | PTS645VK39-2 LFS *sub* | C&K | CKN10053-ND | 8999 | 4 / 0.370 / 1.48 | 20 / 0.319 / 6.38 |
| U1 | 1 | SC1509(13) *sub* | Raspberry Pi | 2648-SC1509(13)CT-ND | 1909 | 2 / 1.400 / 2.80 | 10 / 1.179 / 11.79 |
| U2, U4 | 2 | USBLC6-2SC6 | STMicroelectronics | 497-5235-1-ND | 100956 | 4 / 0.470 / 1.88 | 20 / 0.289 / 5.78 |
| U3 | 1 | TLV75733PDRVR | Texas Instruments | 296-49281-1-ND | 1445 | 2 / 0.420 / 0.84 | 10 / 0.296 / 2.96 |
| U6, U7 | 2 | SN74CBTD3861DBQR | Texas Instruments | 296-6483-1-ND | 4517 | 4 / 0.760 / 3.04 | 20 / 0.540 / 10.80 |
| U8 | 1 | W25Q128JVSIQ TR *sub* | Winbond Electronics | 256-W25Q128JVSIQTRCT-ND | 44381 | 2 / 4.780 / 9.56 | 10 / 4.454 / 44.54 |
| U9 | 1 | SC1169 *sub* | Raspberry Pi | 2648-SC1169CT-ND | 1856 | 2 / 4.030 / 8.06 | 10 / 4.030 / 40.30 |
| U10 | 1 | TCA9554PWR | Texas Instruments | 296-30135-1-ND | 17733 | 2 / 1.500 / 3.00 | 10 / 1.102 / 11.02 |
| Y1 | 1 | ABM8-272-T3 | Abracon LLC | 535-ABM8-272-T3CT-ND | 27483 | 2 / 0.610 / 1.22 | 10 / 0.528 / 5.28 |

**Totals** (lines with a stocked part): 2 boards 69.69 EUR (34.84/board), 10 boards 275.46 EUR (27.55/board)

Not available at Digikey:

- C1 (22uF, JLC C45783): tried CL21A226MAQNNNE, GRM21BR61E226ME44L, CL21A226MAYNNNE, C2012X5R1E226M125AB, TMK212ABJ226MG-T, C2012X5R1E226M125AC; stock 0 / on order
- D13,D14,D15,D16,D17,D18,D19 (PESD5V0S1BL, JLC C84374): tried PESD5V0S1BL,315, PESD5V0S1BL-QYL, PESD5V0S1BL; buy at Mouser (Nexperia PESD5V0S1BL; Digi-Key lists only a 10000-piece reel)
- J4 (USB-A HOST, JLC C564920): tried 67643-2910, 676432910, 0676432910; stock 0 / on order
- LED1 (WS2812B-2020, JLC C52917434): tried WS2812B-2020-V6, WS2812B-2020; buy at LCSC / AliExpress (Worldsemi WS2812B-2020)
