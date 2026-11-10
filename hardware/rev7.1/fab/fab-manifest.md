# amigahid-pico-rev7.1 fab package

Cut from `board-rev7` @ `6a102e0` (working tree clean), `kicad-cli` 10.0.7, outline 100.00 x 62.00 mm (centreline), 4 layers.

`amigahid-pico-rev7.1-gerbers.zip` sha256 3df4eb1d98caf25d, 14 files: amigahid-pico-rev7.1-B_Cu.gbl, amigahid-pico-rev7.1-B_Mask.gbs, amigahid-pico-rev7.1-B_Paste.gbp, amigahid-pico-rev7.1-B_Silkscreen.gbo, amigahid-pico-rev7.1-Edge_Cuts.gm1, amigahid-pico-rev7.1-F_Cu.gtl, amigahid-pico-rev7.1-F_Mask.gts, amigahid-pico-rev7.1-F_Paste.gtp, amigahid-pico-rev7.1-F_Silkscreen.gto, amigahid-pico-rev7.1-In1_Cu.g1, amigahid-pico-rev7.1-In2_Cu.g2, amigahid-pico-rev7.1-NPTH.drl, amigahid-pico-rev7.1-PTH.drl, amigahid-pico-rev7.1-job.gbrjob

## Drill table

amigahid-pico-rev7.1-NPTH.drl: 10 hits
- 0.650 mm x 2
- 2.200 mm x 4
- 3.200 mm x 4

amigahid-pico-rev7.1-PTH.drl: 332 hits
- 0.300 mm x 249
- 0.600 mm x 4
- 0.950 mm x 4
- 0.990 mm x 4
- 1.000 mm x 29
- 1.100 mm x 30
- 1.300 mm x 4
- 2.300 mm x 2
- 2.500 mm x 2
- 3.200 mm x 4

## BOM: 41 lines (one per LCSC part), 116 placements; CPL rows 116

- not placed / not in the BOM: H1, H2, H3, H4, H5, H6, H7, H8, JP1 (DNP and the mounting holes)
- without an LCSC code: none
- hand-fitted, not in this BOM/CPL (SMT-only recipe): J1, J2, J3, J4, J7, J8, J10, J12, L1, LED1, P1, P2, SW1, SW2
- CPL coordinates are the pad-centre of each part in the gerber frame (KiCad X, Y negated: the board spans Y -62..0); rotations are KiCad's plus the JLC_ROT offsets (PinHeader_ -90, PinSocket_ -90, SSOP-24_ -90, SOIC-8_ -90, SOT-23-6 -90, SOT-23 +180) - check JLC's placement preview for every polarised part before ordering (D1-D6, C4, U1-U4, U6-U11, Q1-Q3, Y1)
