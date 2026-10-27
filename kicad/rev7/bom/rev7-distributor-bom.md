# rev7 distributor BOM (2026-10-27)

Source `kicad/rev7/fab/amigahid-pico-rev7-bom.csv`; mapping in `tools/rev7_distributor_bom.py`. Parts are picked per line: the JLC part when the distributor stocks it, otherwise a standard-brand equivalent (marked *sub*), otherwise the cheapest in-stock keyword hit (*kw*). Order quantities are rounded to the seller's minimum/multiple and raised to the next price break whenever that lowers the line total (10 resistors at the 10-piece price are cheaper than 6 at the 1-piece price); the quantity shown is what to order. Totals exclude shipping and VAT.

## Mouser (EUR)

| Refs | /board | Part | Mfr | Mouser # | Stock | x2 qty / unit / line | x3 qty / unit / line | x10 qty / unit / line |
|---|---|---|---|---|---|---|---|---|
| C1 | 1 | CL21A226KOQNNNG *sub* | Samsung Electro-Mechanics | 187-CL21A226KOQNNNG | 34451 | 2 / 0.361 / 0.72 | 3 / 0.361 / 1.08 | 10 / 0.218 / 2.18 |
| C2, C6, C10, C14 | 4 | CL21A106KAYQNWE *kw* | Samsung Electro-Mechanics | 187-CL21A106KAYQNWE | 5924 | 10 / 0.108 / 1.08 | 12 / 0.108 / 1.30 | 40 / 0.108 / 4.32 |
| C3, C5, C7, C11, C12, C13, C23, C24, C26, C27, C28, C32, C36 | 13 | C0603C104M5RACTU *kw* | KEMET | 80-C0603C104M5R | 4515057 | 26 / 0.032 / 0.83 | 50 / 0.022 / 1.10 | 130 / 0.018 / 2.34 |
| C4 | 1 | EEE-FKC151XAP | Panasonic Industry | 667-EEE-FKC151XAP | 57 | 2 / 0.748 / 1.50 | 3 / 0.748 / 2.24 | 10 / 0.445 / 4.45 |
| C8, C9, C31 | 3 | CL10B105KB8NQNC *sub* | Samsung Electro-Mechanics | 187-CL10B105KB8NQNC | 9694 | 10 / 0.064 / 0.64 | 10 / 0.064 / 0.64 | 30 / 0.064 / 1.92 |
| C20, C29, C38 | 3 | CL05A475MQ5NRNC *sub* | Samsung Electro-Mechanics | 187-CL05A475MQ5NRNC | 777 | 6 / 0.112 / 0.67 | 10 / 0.069 / 0.69 | 30 / 0.069 / 2.07 |
| C21, C22, C25, C30, C39 | 5 | CL05B104KP5NNNC *sub* | Samsung Electro-Mechanics | 187-CL05B104KP5NNNC | 255863 | 10 / 0.022 / 0.22 | 15 / 0.022 / 0.33 | 50 / 0.015 / 0.75 |
| C33, C34 | 2 | C0603C150J5GAC *kw* | KEMET | 80-C0603C150J5GAC | 90082 | 10 / 0.028 / 0.28 | 10 / 0.028 / 0.28 | 20 / 0.028 / 0.56 |
| C35 | 1 | CL21A475KAQNNNE | Samsung Electro-Mechanics | 187-CL21A475KAQNNNE | 9804 | 2 / 0.138 / 0.28 | 3 / 0.138 / 0.41 | 10 / 0.075 / 0.75 |
| C37 | 1 | CL10A475MO8NNNC *kw* | Samsung Electro-Mechanics | 187-CL10A475MO8NNNC | 21782 | 2 / 0.146 / 0.29 | 3 / 0.146 / 0.44 | 10 / 0.081 / 0.81 |
| D1, D2, D3 | 3 | SX34_R1_00001 *sub* | Panjit | 241-SX34_R1_00001 | 3405 | 10 / 0.138 / 1.38 | 10 / 0.138 / 1.38 | 30 / 0.138 / 4.14 |
| D4, D5, D6 | 3 | 1N5819HW-7-F *sub* | Diodes Incorporated | 621-1N5819HW-F | 394223 | 10 / 0.117 / 1.17 | 10 / 0.117 / 1.17 | 30 / 0.117 / 3.51 |
| D10, D11, D12, D20, D21, D22, D23, D24, D25, D26, D27, D28, D29, D30 | 14 | PESD5V0S1BAF *kw* | Nexperia | 771-PESD5V0S1BAF | 2451 | 28 / 0.155 / 4.34 | 42 / 0.155 / 6.51 | 140 / 0.135 / 18.90 |
| D13, D14, D15, D16, D17, D18, D19 | 7 | PESD5V0S1BL-QYL *sub* | Nexperia | 771-PESD5V0S1BL-QYL | 16116 | 14 / 0.226 / 3.16 | 21 / 0.226 / 4.75 | 100 / 0.111 / 11.10 |
| F1 | 1 | MF-MSMF075-2 | Bourns | 652-MF-MSMF075-2 | 13672 | 2 / 0.275 / 0.55 | 3 / 0.275 / 0.82 | 10 / 0.249 / 2.49 |
| F2, F3 | 2 | MF-MSMF050-2 | Bourns | 652-MF-MSMF050-2 | 22237 | 4 / 0.275 / 1.10 | 6 / 0.275 / 1.65 | 20 / 0.249 / 4.98 |
| FB1 | 1 | MMZ1608Y601CTAH0 *kw* | TDK | 810-MMZ1608Y601CTAH0 | 4338 | 2 / 0.086 / 0.17 | 3 / 0.086 / 0.26 | 10 / 0.040 / 0.40 |
| J1, J8 | 2 | PH1-08-UA *sub* | Adam Tech | 737-PH1-08-UA | 11043 | 4 / 0.112 / 0.45 | 6 / 0.112 / 0.67 | 20 / 0.094 / 1.88 |
| J2 | 1 | PH1-03-UA *sub* | Adam Tech | 737-PH1-03-UA | 34706 | 2 / 0.103 / 0.21 | 3 / 0.103 / 0.31 | 10 / 0.050 / 0.50 |
| J3 | 1 | 61300411821 | Wurth Elektronik | 710-61300411821 | 10313 | 2 / 0.361 / 0.72 | 3 / 0.361 / 1.08 | 10 / 0.361 / 3.61 |
| J4 | 1 | 67643-2910 | Molex | 538-67643-2910 | 4246 | 2 / 1.030 / 2.06 | 3 / 1.030 / 3.09 | 10 / 1.010 / 10.10 |
| J7 | 1 | PH2-06-UA *sub* | Adam Tech | 737-PH2-06-UA | 89052 | 2 / 0.086 / 0.17 | 3 / 0.086 / 0.26 | 10 / 0.071 / 0.71 |
| J10, J12 | 2 | MD-60S | Same Sky | 490-MD-60S | 5707 | 4 / 1.810 / 7.24 | 6 / 1.810 / 10.86 | 20 / 1.540 / 30.80 |
| J13 | 1 | USB4105-GF-A *sub* | GCT | 640-USB4105-GF-A | 397607 | 2 / 0.688 / 1.38 | 3 / 0.688 / 2.06 | 10 / 0.581 / 5.81 |
| L1 | 1 | AOTA-B201610S3R3-101-T *sub* | ABRACON | 815-AB201610S3R3101T | 7680 | 2 / 0.181 / 0.36 | 3 / 0.181 / 0.54 | 10 / 0.152 / 1.52 |
| LED1 | 1 | WS2812B-2020 **n/a** |  | |  | - | - | - |
| P1, P2 | 2 | L717SDE09PA4CH4R *sub* | Amphenol Commercial Products | 523-L717SDE09PA4CH4R | 254 | 4 / 1.710 / 6.84 | 6 / 1.710 / 10.26 | 20 / 1.510 / 30.20 |
| Q1 | 1 | 2N7002NXAKR *sub* | Nexperia | 771-2N7002NXAKR | 565252 | 2 / 0.120 / 0.24 | 3 / 0.120 / 0.36 | 10 / 0.052 / 0.52 |
| Q2, Q3 | 2 | BSS138P,215 *sub* | Nexperia | 771-BSS138P215 | 289090 | 4 / 0.189 / 0.76 | 10 / 0.099 / 0.99 | 20 / 0.099 / 1.98 |
| R41, R42, R68 | 3 | RC0603FR-074K7L | YAGEO | 603-RC0603FR-074K7L | 2388586 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 30 / 0.012 / 0.36 |
| R43, R44 | 2 | RC0603FR-075K1L | YAGEO | 603-RC0603FR-075K1L | 392050 | 10 / 0.015 / 0.15 | 10 / 0.015 / 0.15 | 20 / 0.015 / 0.30 |
| R45, R55 | 2 | RC0603FR-07470RL | YAGEO | 603-RC0603FR-07470RL | 464271 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R46, R47 | 2 | RC0603FR-07100KL | YAGEO | 603-RC0603FR-07100KL | 3933729 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R49, R52, R53, R54, R60 | 5 | WR06X1001FTL *kw* | Walsin | 791-WR06X1001FTL | 384 | 10 / 0.010 / 0.10 | 15 / 0.010 / 0.15 | 50 / 0.010 / 0.50 |
| R50, R59 | 2 | RC0603FR-0733RL | YAGEO | 603-RC0603FR-0733RL | 860380 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R56, R69 | 2 | RC0603FR-0710KL | YAGEO | 603-RC0603FR-0710KL | 2787136 | 10 / 0.015 / 0.15 | 10 / 0.015 / 0.15 | 20 / 0.015 / 0.30 |
| R57, R58 | 2 | RC0603FR-0727RL | YAGEO | 603-RC0603FR-0727RL | 172709 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R61, R62, R63, R64, R65, R66, R67 | 7 | RC0402FR-0710KL | YAGEO | 603-RC0402FR-0710KL | 14724085 | 14 / 0.009 / 0.13 | 21 / 0.009 / 0.19 | 100 / 0.006 / 0.60 |
| R70, R71 | 2 | RC0603FR-0727RL | YAGEO | 603-RC0603FR-0727RL | 172709 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R72, R73 | 2 | RC0402FR-0715KL | YAGEO | 603-RC0402FR-0715KL | 1535328 | 10 / 0.009 / 0.09 | 10 / 0.009 / 0.09 | 20 / 0.009 / 0.18 |
| RN1, RN2, RN4 | 3 | CAY10-103J4LF *sub* | Bourns | 652-CAY10-103J4LF | 105183 | 10 / 0.020 / 0.20 | 10 / 0.020 / 0.20 | 30 / 0.015 / 0.45 |
| RN3 | 1 | CAY16-103J4LF *sub* | Bourns | 652-CAY16-103J4LF | 275668 | 2 / 0.086 / 0.17 | 3 / 0.086 / 0.26 | 10 / 0.031 / 0.31 |
| SW1, SW2 | 2 | PTS645VK58-2 LFS | C&K | 611-PTS645VK582 | 11153 | 4 / 0.353 / 1.41 | 6 / 0.353 / 2.12 | 20 / 0.312 / 6.24 |
| U1 | 1 | SC1509(13)-A4 *sub* | Raspberry Pi | 358-SC150913 | 2646 | 2 / 0.690 / 1.38 | 3 / 0.690 / 2.07 | 10 / 0.688 / 6.88 |
| U2, U4 | 2 | USBLC6-2SC6 | STMicroelectronics | 511-USBLC6-2SC6 | 191855 | 4 / 0.464 / 1.86 | 10 / 0.206 / 2.06 | 20 / 0.206 / 4.12 |
| U3 | 1 | TLV75733PDRVR | Texas Instruments | 595-TLV75733PDRVR | 1104 | 2 / 0.413 / 0.83 | 3 / 0.413 / 1.24 | 10 / 0.290 / 2.90 |
| U6, U7 | 2 | SN74CBTD3861DBQR | Texas Instruments | 595-SN74CBTD3861DBQR | 1912 | 4 / 0.740 / 2.96 | 6 / 0.740 / 4.44 | 20 / 0.529 / 10.58 |
| U8 | 1 | W25Q128JVSIQ TR *sub* | Winbond | 454-W25Q128JVSIQTR | 44 | 2 / 4.950 / 9.90 | 3 / 4.950 / 14.85 | 10 / 4.950 / 49.50 |
| U9 | 1 | SC1169 *sub* | Raspberry Pi | 358-SC1169 | 1499 | 2 / 3.440 / 6.88 | 3 / 3.440 / 10.32 | 10 / 3.440 / 34.40 |
| U10 | 1 | TCA9554PWR | Texas Instruments | 595-TCA9554PWR | 11239 | 2 / 1.500 / 3.00 | 3 / 1.500 / 4.50 | 10 / 1.090 / 10.90 |
| Y1 | 1 | ABM8-272-T3 | ABRACON | 815-ABM8-272-T3 | 10148 | 2 / 0.593 / 1.19 | 3 / 0.593 / 1.78 | 10 / 0.517 / 5.17 |

**Totals** (lines with a stocked part): 2 boards 69.92 EUR (34.96/board), 3 boards 100.83 EUR (33.61/board), 10 boards 288.19 EUR (28.82/board)

Not available at Mouser:

- LED1 (WS2812B-2020, JLC C52917434): tried WS2812B-2020-V6, WS2812B-2020; buy at LCSC / AliExpress (Worldsemi WS2812B-2020)

## Digikey (EUR)

| Refs | /board | Part | Mfr | Digikey # | Stock | x2 qty / unit / line | x3 qty / unit / line | x10 qty / unit / line |
|---|---|---|---|---|---|---|---|---|
| C1 | 1 | CL21A226MAYNNWE *sub* | Samsung Electro-Mechanics | 1276-CL21A226MAYNNWECT-ND | 1023 | 2 / 0.320 / 0.64 | 3 / 0.320 / 0.96 | 10 / 0.191 / 1.91 |
| C2, C6, C10, C14 | 4 | GRM21BR61E106MA73K *kw* | Murata Electronics | 490-GRM21BR61E106MA73KCT-ND | 6709 | 10 / 0.084 / 0.84 | 12 / 0.084 / 1.01 | 50 / 0.060 / 2.98 |
| C3, C5, C7, C11, C12, C13, C23, C24, C26, C27, C28, C32, C36 | 13 | CC0603KRX7R9BB104 | YAGEO | 311-1344-1-ND | 3955819 | 26 / 0.071 / 1.85 | 50 / 0.049 / 2.46 | 130 / 0.042 / 5.51 |
| C4 | 1 | 865080345010 *sub* | Würth Elektronik | 732-8420-1-ND | 7647 | 2 / 0.280 / 0.56 | 3 / 0.280 / 0.84 | 10 / 0.280 / 2.80 |
| C8, C9, C31 | 3 | UMK107BJ105KA-T *kw* | Taiyo Yuden | 587-2400-1-ND | 66781 | 10 / 0.073 / 0.73 | 10 / 0.073 / 0.73 | 30 / 0.073 / 2.19 |
| C20, C29, C38 | 3 | MSASL105BB5475KFNA01 *kw* | Taiyo Yuden | 587-MSASL105BB5475KFNA01CT-ND | 20209 | 10 / 0.066 / 0.66 | 10 / 0.066 / 0.66 | 30 / 0.066 / 1.98 |
| C21, C22, C25, C30, C39 | 5 | CL05B104KP5NNNC *sub* | Samsung Electro-Mechanics | 1276-1002-1-ND | 25779443 | 10 / 0.022 / 0.22 | 15 / 0.022 / 0.33 | 50 / 0.015 / 0.73 |
| C33, C34 | 2 | 0603N150J500CT *kw* | Walsin Technology Corporation | 1292-1487-1-ND | 96000 | 10 / 0.020 / 0.20 | 10 / 0.020 / 0.20 | 20 / 0.020 / 0.40 |
| C35 | 1 | TMK212ABJ475KG-T *kw* | Taiyo Yuden | 587-5478-1-ND | 28480 | 2 / 0.140 / 0.28 | 3 / 0.140 / 0.42 | 10 / 0.080 / 0.80 |
| C37 | 1 | CL10A475KP8NNNC *sub* | Samsung Electro-Mechanics | 1276-1044-1-ND | 502 | 2 / 0.090 / 0.18 | 3 / 0.090 / 0.27 | 10 / 0.039 / 0.39 |
| D1, D2, D3 | 3 | SX34_R1_00001 *sub* | Panjit International Inc. | 3757-SX34_R1_00001CT-ND | 3503 | 6 / 0.320 / 1.92 | 10 / 0.197 / 1.97 | 30 / 0.197 / 5.91 |
| D4, D5, D6 | 3 | 1N5819HW-7-F *sub* | Diodes Incorporated | 1N5819HW-FDICT-ND | 356858 | 6 / 0.280 / 1.68 | 10 / 0.169 / 1.69 | 30 / 0.139 / 4.16 |
| D10, D11, D12, D20, D21, D22, D23, D24, D25, D26, D27, D28, D29, D30 | 14 | PESD5V0S1BA,115 **n/a** | Nexperia USA Inc. | | 0 | - | - | - |
| D13, D14, D15, D16, D17, D18, D19 | 7 | PESD5V0S1BL,315 | Nexperia USA Inc. | 1727-PESD5V0S1BL,315CT-ND | 19700 | 14 / 0.167 / 2.34 | 21 / 0.167 / 3.51 | 70 / 0.119 / 8.36 |
| F1 | 1 | MF-MSMF075-2 | Bourns Inc. | MF-MSMF075-2CT-ND | 64817 | 2 / 0.340 / 0.68 | 3 / 0.340 / 1.02 | 10 / 0.257 / 2.57 |
| F2, F3 | 2 | MF-MSMF050-2 | Bourns Inc. | MF-MSMF050-2CT-ND | 56069 | 4 / 0.340 / 1.36 | 6 / 0.278 / 1.67 | 20 / 0.257 / 5.14 |
| FB1 | 1 | PE-0603FB601ST *kw* | Pulse Electronics | 553-2384-1-ND | 41861 | 2 / 0.090 / 0.18 | 3 / 0.090 / 0.27 | 10 / 0.030 / 0.30 |
| J1, J8 | 2 | PH1-08-UA *sub* | Adam Tech | 2057-PH1-08-UA-ND | 18607 | 4 / 0.120 / 0.48 | 6 / 0.120 / 0.72 | 20 / 0.097 / 1.94 |
| J2 | 1 | PH1-03-UA *sub* | Adam Tech | 2057-PH1-03-UA-ND | 117152 | 2 / 0.090 / 0.18 | 3 / 0.090 / 0.27 | 10 / 0.043 / 0.43 |
| J3 | 1 | PPTC041LFBN-RC *sub* | Sullins Connector Solutions | S7002-ND | 35475 | 2 / 0.370 / 0.74 | 3 / 0.370 / 1.11 | 10 / 0.314 / 3.14 |
| J4 | 1 | 0676432910 **n/a** | Molex | | 0 | - | - | - |
| J7 | 1 | PH2-06-UA *sub* | Adam Tech | 2057-PH2-06-UA-ND | 52327 | 2 / 0.090 / 0.18 | 3 / 0.090 / 0.27 | 10 / 0.072 / 0.72 |
| J10, J12 | 2 | MD-60S | Same Sky (Formerly CUI Devices) | CP-2460-ND | 2745 | 4 / 1.860 / 7.44 | 6 / 1.860 / 11.16 | 20 / 1.582 / 31.64 |
| J13 | 1 | USB4105-GF-A-120 | GCT | 2073-USB4105-GF-A-120CT-ND | 42657 | 2 / 0.710 / 1.42 | 3 / 0.710 / 2.13 | 10 / 0.600 / 6.00 |
| L1 | 1 | AOTA-B201610S3R3-101-T *sub* | Abracon LLC | 535-AOTA-B201610S3R3-101-TCT-ND | 8352 | 2 / 0.190 / 0.38 | 3 / 0.190 / 0.57 | 10 / 0.154 / 1.54 |
| LED1 | 1 | WS2812B-2020 **n/a** |  | |  | - | - | - |
| P1, P2 | 2 | L717SDE09PA4CH4R *sub* | Amphenol ICC (Commercial Products) | L717SDE09PA4CH4R-ND | 23 | 4 / 1.700 / 6.80 | 6 / 1.700 / 10.20 | 20 / 1.451 / 29.02 |
| Q1 | 1 | 2N7002NXAKR *sub* | Nexperia USA Inc. | 1727-8643-1-ND | 523476 | 2 / 0.120 / 0.24 | 3 / 0.120 / 0.36 | 10 / 0.076 / 0.76 |
| Q2, Q3 | 2 | BSS138P,215 *sub* | Nexperia USA Inc. | 1727-1142-1-ND | 646119 | 4 / 0.190 / 0.76 | 10 / 0.112 / 1.12 | 20 / 0.112 / 2.24 |
| R41, R42, R68 | 3 | WR06X4701FTL *kw* | Walsin Technology Corporation | 1292-WR06X4701FTLCT-ND | 152411 | 10 / 0.015 / 0.15 | 10 / 0.015 / 0.15 | 30 / 0.011 / 0.34 |
| R43, R44 | 2 | RT0603FRE075K1L *kw* | YAGEO | YAG5966CT-ND | 7784 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R45, R55 | 2 | RMCF0603FT470R *kw* | Stackpole Electronics Inc | RMCF0603FT470RCT-ND | 544699 | 10 / 0.019 / 0.19 | 10 / 0.019 / 0.19 | 25 / 0.014 / 0.34 |
| R46, R47 | 2 | RT0603FRE07100KL *kw* | YAGEO | YAG2319CT-ND | 51309 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R49, R52, R53, R54, R60 | 5 | RNCP0603FTD1K00 *kw* | Stackpole Electronics Inc | RNCP0603FTD1K00CT-ND | 475307 | 10 / 0.011 / 0.11 | 15 / 0.011 / 0.17 | 50 / 0.009 / 0.47 |
| R50, R59 | 2 | RT0603FRE1033KL *kw* | YAGEO | 13-RT0603FRE1033KLCT-ND | 9978 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R56, R69 | 2 | RNCP0603FTD10K0 *kw* | Stackpole Electronics Inc | RNCP0603FTD10K0CT-ND | 602186 | 10 / 0.011 / 0.11 | 10 / 0.011 / 0.11 | 20 / 0.011 / 0.22 |
| R57, R58 | 2 | RT0603FRE0727KL *kw* | YAGEO | YAG5952CT-ND | 3754 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R61, R62, R63, R64, R65, R66, R67 | 7 | CR0402-FX-1002GLF *kw* | Bourns Inc. | CR0402-FX-1002GLFCT-ND | 4635333 | 14 / 0.013 / 0.18 | 25 / 0.010 / 0.24 | 70 / 0.007 / 0.52 |
| R70, R71 | 2 | RT0603FRE0727KL *kw* | YAGEO | YAG5952CT-ND | 3754 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 20 / 0.012 / 0.24 |
| R72, R73 | 2 | WR04X1502FTL *kw* | Walsin Technology Corporation | 1292-WR04X1502FTLCT-ND | 259545 | 10 / 0.012 / 0.12 | 10 / 0.012 / 0.12 | 25 / 0.008 / 0.20 |
| RN1, RN2, RN4 | 3 | CAY10-103J4LF *sub* | Bourns Inc. | CAY10-103J4LFCT-ND | 237402 | 10 / 0.020 / 0.20 | 10 / 0.020 / 0.20 | 30 / 0.017 / 0.52 |
| RN3 | 1 | CAY16-103J4LF *sub* | Bourns Inc. | CAY16-103J4LFCT-ND | 207502 | 2 / 0.090 / 0.18 | 3 / 0.090 / 0.27 | 10 / 0.032 / 0.32 |
| SW1, SW2 | 2 | PTS645VK58-2 LFS | C&K | CKN10965-ND | 9621 | 4 / 0.370 / 1.48 | 6 / 0.370 / 2.22 | 20 / 0.322 / 6.44 |
| U1 | 1 | SC1509(13) *sub* | Raspberry Pi | 2648-SC1509(13)CT-ND | 1892 | 2 / 1.410 / 2.82 | 3 / 1.410 / 4.23 | 10 / 1.192 / 11.92 |
| U2, U4 | 2 | USBLC6-2SC6 | STMicroelectronics | 497-5235-1-ND | 72254 | 4 / 0.480 / 1.92 | 6 / 0.480 / 2.88 | 20 / 0.292 / 5.84 |
| U3 | 1 | TLV75733PDRVR | Texas Instruments | 296-49281-1-ND | 38 | 2 / 0.430 / 0.86 | 3 / 0.430 / 1.29 | 10 / 0.299 / 2.99 |
| U6, U7 | 2 | SN74CBTD3861DBQR | Texas Instruments | 296-6483-1-ND | 4517 | 4 / 0.760 / 3.04 | 6 / 0.760 / 4.56 | 20 / 0.546 / 10.92 |
| U8 | 1 | W25Q128JVSIQ TR *sub* | Winbond Electronics | 256-W25Q128JVSIQTRCT-ND | 38390 | 2 / 4.830 / 9.66 | 3 / 4.830 / 14.49 | 10 / 4.503 / 45.03 |
| U9 | 1 | SC1169 *sub* | Raspberry Pi | 2648-SC1169CT-ND | 1795 | 2 / 4.070 / 8.14 | 3 / 4.070 / 12.21 | 10 / 4.070 / 40.70 |
| U10 | 1 | TCA9554PWR | Texas Instruments | 296-30135-1-ND | 17591 | 2 / 1.520 / 3.04 | 3 / 1.520 / 4.56 | 10 / 1.114 / 11.14 |
| Y1 | 1 | ABM8-272-T3 | Abracon LLC | 535-ABM8-272-T3CT-ND | 25656 | 2 / 0.610 / 1.22 | 3 / 0.610 / 1.83 | 10 / 0.534 / 5.34 |

**Totals** (lines with a stocked part): 2 boards 66.96 EUR (33.48/board), 3 boards 96.23 EUR (32.08/board), 10 boards 268.00 EUR (26.80/board)

Not available at Digikey:

- D10,D11,D12,D20,D21,D22,D23,D24,D25,D26,D27,D28,D29,D30 (PESD5V0S1BA, JLC C19224): tried PESD5V0S1BA,115, PESD5V0S1BA, PESD5V0S1BA-QX; stock 0 / on order
- J4 (USB-A HOST, JLC C564920): tried 67643-2910, 676432910, 0676432910; stock 0 / on order
- LED1 (WS2812B-2020, JLC C52917434): tried WS2812B-2020-V6, WS2812B-2020; buy at LCSC / AliExpress (Worldsemi WS2812B-2020)
