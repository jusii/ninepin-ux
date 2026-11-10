# Ninepin UX rev7.1

rev7 plus **U11, a TLV803S reset supervisor on RUN**. Same firmware, outline, connectors, case
(`../case/`) and hand-fitted parts as rev7 (tag `hw/ux-rev7`). Ordering and assembly:
[doc/build.md](../../doc/build.md).

## changes from rev7

* **U11 TLV803SDBZR (LCSC C132016) + C40 100 nF**, south of the OLED. U11 holds RUN low until 3V3
  has been above 2.93 V for 200 ms, and again whenever 3V3 sags below that: no more cold-plug
  BOOTSEL. Open drain, wired-OR with SW2 on `run_sw`. New copper is `route.py` block 5 only.
* R70/R71 use C25190, the same 27R as R57/R58.
* L1 is the Abracon AOTA-B201610S3R3-101-T the land was drawn for.
* `fab/make_fab.py`: JLC rotation offsets from the rev7 order preview; SMT-only recipe.

If U11 misbehaves: remove it and fit 1 µF + 1 MΩ across SW2 pins 1-2 on the back.

## regenerate and check

From the repository root. KiCad 10 with its Python `pcbnew`; FreeCAD for the case.

```shell
cd hardware/rev7.1 && python3 make_sch.py && python3 make_board.py --force && python3 route.py && cd ../..
P=hardware/rev7.1/ninepin-ux-rev7.1
kicad-cli sch erc $P.kicad_sch --exit-code-violations
kicad-cli pcb drc $P.kicad_pcb --schematic-parity --severity-all --refill-zones --save-board --exit-code-violations
git checkout -- $P.kicad_pro $P.kicad_prl     # kicad-cli rewrites them
python3 tools/check_rev7_1_pcb.py && python3 tools/check_rev7_1_netlist.py && python3 tools/check_rev7_1_style.py
python3 hardware/rev7.1/fab/make_fab.py       # --full adds the hand-fitted parts for a full-assembly order
python3 tools/check_rev7_1_fab.py             # the fab package matches the board
python3 hardware/case/run_case.py             # case STLs and fit report
```

`bom/ibom.html` comes from the InteractiveHtmlBom plugin
(`generate_interactive_bom.py --no-browser --dest-dir bom --name-format ibom --highlight-pin1 all
--checkboxes Sourced,Placed --bom-view top-bottom --layer-view F --show-fields Value,Footprint
--blacklist 'H*' ninepin-ux-rev7.1.kicad_pcb`).
