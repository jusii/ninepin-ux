# amigahid-pico-rev7 fab package

Cut from `board-rev7` @ `fe23226` (working tree clean), `kicad-cli` 10.0.7, outline 100.00 x 62.00 mm (centreline), 4 layers.

`amigahid-pico-rev7-gerbers.zip` sha256 eba6aa86b151cb8f, 14 files: amigahid-pico-rev7-B_Cu.gbl, amigahid-pico-rev7-B_Mask.gbs, amigahid-pico-rev7-B_Paste.gbp, amigahid-pico-rev7-B_Silkscreen.gbo, amigahid-pico-rev7-Edge_Cuts.gm1, amigahid-pico-rev7-F_Cu.gtl, amigahid-pico-rev7-F_Mask.gts, amigahid-pico-rev7-F_Paste.gtp, amigahid-pico-rev7-F_Silkscreen.gto, amigahid-pico-rev7-In1_Cu.g1, amigahid-pico-rev7-In2_Cu.g2, amigahid-pico-rev7-NPTH.drl, amigahid-pico-rev7-PTH.drl, amigahid-pico-rev7-job.gbrjob

## Drill table

amigahid-pico-rev7-NPTH.drl: 10 hits
- 0.650 mm x 2
- 2.200 mm x 4
- 3.200 mm x 4

amigahid-pico-rev7-PTH.drl: 327 hits
- 0.300 mm x 244
- 0.600 mm x 4
- 0.950 mm x 4
- 0.990 mm x 4
- 1.000 mm x 29
- 1.100 mm x 30
- 1.300 mm x 4
- 2.300 mm x 2
- 2.500 mm x 2
- 3.200 mm x 4

## BOM: 56 lines, 128 placements; CPL rows 128

- not placed / not in the BOM: H1, H2, H3, H4, H5, H6, H7, H8, JP1 (DNP and the mounting holes)
- without an LCSC code: none
- hand-solder candidates if JLC stock is short: P1, P2
- CPL Y is measured upward from the south board edge (KiCad origin flipped); rotations are KiCad's - check JLC's placement preview for the polarised parts (D1-D6, C4, U1-U3, U6-U10, Q1-Q3, L1, LED1)
