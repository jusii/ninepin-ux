# ninepin-ux-rev7.1 fab package

Cut from `master` @ `6e9adc1` (plus uncommitted changes; the board sha256 below is the link), `kicad-cli` 10.0.8, outline 100.00 x 62.00 mm (centreline), 4 layers.

Board `ninepin-ux-rev7.1.kicad_pcb` sha256 06bf450d3317594c040091c04365d7e5139e21938fa4359e322b65284b5cf375

`ninepin-ux-rev7.1-gerbers.zip` sha256 3a5cc7818db96a17, 14 files: ninepin-ux-rev7.1-B_Cu.gbl, ninepin-ux-rev7.1-B_Mask.gbs, ninepin-ux-rev7.1-B_Paste.gbp, ninepin-ux-rev7.1-B_Silkscreen.gbo, ninepin-ux-rev7.1-Edge_Cuts.gm1, ninepin-ux-rev7.1-F_Cu.gtl, ninepin-ux-rev7.1-F_Mask.gts, ninepin-ux-rev7.1-F_Paste.gtp, ninepin-ux-rev7.1-F_Silkscreen.gto, ninepin-ux-rev7.1-In1_Cu.g1, ninepin-ux-rev7.1-In2_Cu.g2, ninepin-ux-rev7.1-NPTH.drl, ninepin-ux-rev7.1-PTH.drl, ninepin-ux-rev7.1-job.gbrjob

## Drill table

ninepin-ux-rev7.1-NPTH.drl: 10 hits
- 0.650 mm x 2
- 2.200 mm x 4
- 3.200 mm x 4

ninepin-ux-rev7.1-PTH.drl: 332 hits
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

## BOM and CPL

- `ninepin-ux-rev7.1-bom.csv` / `-cpl.csv` (SMT-only, JLCPCB Economic PCBA): 41 BOM lines (one per LCSC part), 116 placements, CPL rows 116; hand-fitted: J1, J2, J3, J4, J7, J8, J10, J12, L1, LED1, P1, P2, SW1, SW2
- `ninepin-ux-rev7.1-full-bom.csv` / `-cpl.csv` (full assembly, JLCPCB Standard PCBA): 51 BOM lines (one per LCSC part), 130 placements, CPL rows 130; hand-fitted: none
- not placed / in no BOM: H1, H2, H3, H4, H5, H6, H7, H8, JP1 (DNP and the mounting holes)
- without an LCSC code: none
- the full recipe's extra parts (J1, J2, J3, J4, J7, J8, J10, J12, L1, LED1, P1, P2, SW1, SW2) have not been through a JLC placement preview yet
- CPL coordinates are the pad-centre of each THT part and the origin (body centre) of each SMD part, in the gerber frame (KiCad X, Y negated: the board spans Y -62..0); rotations are KiCad's plus the JLC_ROT offsets (PinHeader_ -90, PinSocket_ -90, SSOP-24_ -90, SOIC-8_ -90, SOT-23-6 -90, SOT-23 +180) - check JLC's placement preview for every polarised part before ordering (D1-D6, C4, U1-U4, U6-U11, Q1-Q3, Y1)
