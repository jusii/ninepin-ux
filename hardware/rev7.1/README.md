# UX rev7.1

rev7 with one fix: **U11, a TLV803S reset supervisor on RUN.** Same firmware, outline, connectors, case
(`../rev7/case/`) and hand-fitted parts as rev7. `../rev7/` stays as fabricated.

## What changed from rev7

* **U11 TLV803SDBZR (LCSC C132016) + C40 100 nF** in the free spot south of the OLED. U11 holds RUN low
  until 3V3 has been above 2.93 V for 200 ms, and again whenever 3V3 sags below that. This fixes the rev7
  cold-plug BOOTSEL bug: the boot ROM no longer reads the flash (min 2.7 V) on a half-risen rail.
  Open drain, wired-OR with SW2 on `run_sw`.
* The output reaches SW2 pad 1 through a via south of the USB-C pair, along B.Cu under the AMIGA_IO5V
  column, back up east of it and along F.Cu south of D3. Every rev7 track and via is unchanged
  (`route.py` block 5 is the only new copper).
* R70/R71 use C25190, the same 27R as R57/R58: one BOM line fewer.
* L1 is the Abracon AOTA-B201610S3R3-101-T the land was drawn for (the rev7 substitute became unbuyable).
* `fab/make_fab.py`: JLC rotation offsets read from the order preview (SSOP-24, SOIC-8, SOT-23-6 -90,
  SOT-23 180), and the SMT-only recipe (see below).

**If U11 misbehaves** (board stays in reset, or still lands in BOOTSEL): lift U11 and fit 1 uF + 1 Mohm
across SW2 pins 1-2 on the back, the bodge proven on rev7 board 1.

## Ordering (SMT-only recipe)

* JLCPCB **Economic PCBA**, which needs green mask (4 layers, 1.6 mm, ENIG). `fab/` holds the gerbers, BOM and CPL
  for the SMT parts only. The THT parts, L1 and LED1 are not in it.
* Hand-fitted: `bom/rev7.1-mouser-hand-10.csv` (THT parts + L1, 10 boards); LED1 from LCSC, see `bom/README.md`.
* **Check every polarised part in JLC's placement preview before ordering.** The rotation offsets come
  from the rev7 preview and their direction has not been confirmed on a rev7.1 upload yet.

## Regenerate and check

```
python3 make_sch.py && python3 make_board.py --force && python3 route.py
kicad-cli sch erc amigahid-pico-rev7.1.kicad_sch --exit-code-violations
kicad-cli pcb drc amigahid-pico-rev7.1.kicad_pcb --schematic-parity --severity-all --refill-zones --save-board --exit-code-violations
git checkout -- amigahid-pico-rev7.1.kicad_pro amigahid-pico-rev7.1.kicad_prl
python3 ../../tools/check_rev7_1_pcb.py && python3 ../../tools/check_rev7_1_netlist.py && python3 ../../tools/check_rev7_1_style.py
python3 fab/make_fab.py          # --full puts the hand-fitted parts back for a full-assembly order
```
