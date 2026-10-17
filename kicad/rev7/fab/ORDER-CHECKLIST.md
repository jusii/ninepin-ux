# amigahid-pico-UX rev7 — JLCPCB order checklist (PCB + SMT/THT assembly)

Package: `amigahid-pico-rev7-gerbers.zip`, `amigahid-pico-rev7-bom.csv`, `amigahid-pico-rev7-cpl.csv` — all three are
written by `make_fab.py` from the board file at the commit named in `fab-manifest.md`. Never hand-edit them; re-run the
script after any board change (it restores the project file it has to touch).

Board gates at that commit: ERC 0; DRC `--severity-all --schematic-parity --refill-zones` 0 violations / 0 unconnected;
`tools/check_rev7_pcb.py` 40/40; `tools/check_rev7_netlist.py` 490/490; `tools/check_rev7_style.py` S1–S3; two
independent full audits (routing/power, placement/assembly, parts/BOM) adjudicated — see the vault Changelog 2026-10-16.

## 0. Before paying

- **OLED module measured** (socket height 8.4 vs 8.5 mm, glass position) — needed by the case, not by the board.
- **Board count decided.** Thin JLC stock on 2026-10-16: DE9 C305939 8 (6 sellable → 3 boards), mini-DIN C6089346 50,
  CBTD3861 C44508 103, RM2 C49451547 114, L1 C17701150 plenty. Reserve the extended parts in the JLC parts cart first.
- If the DE9 line is short: **untick P1/P2 in the BOM step** and hand-solder the Kycon K22X-E9P-N you already have
  (identical land, flush 4-40 inserts). Everything else is JLC-placed, THT included.

## 1. PCB order form (values that are not the JLC default are marked ⚠)

| Field | Value | Why |
|---|---|---|
| Layers | **4** ⚠ | F.Cu / In1 GND / In2 +3V3 / B.Cu; the job file says so |
| Dimensions | 100 × 62 mm | outline centreline |
| Thickness | 1.6 mm | |
| Layer stackup | JLC04161H-7628 (default) | design rules assume the standard 4-layer prepreg |
| Impedance control | No | full-speed USB only |
| Surface finish | **ENIG** ⚠ | 0.4 mm QFN-60, WSON-6, 0402s and the RM2 castellations: flat pads for the stencil |
| Outer copper | 1 oz | rules drawn to 1 oz (0.15 mm min) |
| Inner copper | 0.5 oz (default) | planes only |
| Via covering | **Tented** | all vias tented; no via-in-pad process needed (the only vias under pads are the QFN/WSON ground slugs) |
| Min via hole / diameter | 0.3 / 0.5 mm | matches the design rules |
| Board outline tolerance | ±0.2 mm (regular) | |
| Delivery format | **Panel by JLCPCB** ⚠ | assembly needs edge rails: **rails on the north and south (100 mm) edges only**, 5 mm, **mouse-bites** (not V-cut: the pours sit 0.3 mm from the edge). West edge is the RM2 antenna (0.3 mm from the edge), east edge is the USB-C overhang — neither may carry a rail or a bite. |
| Castellated holes | No | the RM2's castellations are on the module, not the board |
| Remove order number | **Specify a location** ⚠ | `JLCJLCJLCJLC` text is on the back silk at (50, 40) |
| Confirm production file | Yes | look at their rendered silk/mask once more |

## 2. Assembly form

| Field | Value | Why |
|---|---|---|
| Assembly side | **Top** | every part is top-side |
| Process | **Standard** (not Economic) ⚠ | QFN-60 0.4 mm, WSON-6, THT connectors |
| Tooling holes / fiducials | **Added by JLC on the rails** | the board carries none (its edges are all used) |
| BOM | `amigahid-pico-rev7-bom.csv` | columns Comment / Designator / Footprint / LCSC Part # |
| CPL | `amigahid-pico-rev7-cpl.csv` | Mid X/Y in mm from the south-west corner (Y up); rotations are KiCad's |
| Parts to confirm in their preview | **D1–D6** (SMA / SOD-123 cathode bars), **C4** (electrolytic stripe), **U1** (pin 1 at the NW corner), U2, U3 (WSON pin 1), U6/U7/U8/U10, **U9** (RM2 pin 1), **Q1–Q3**, **L1** (dot = pad 1 = DVDD), **LED1** | JLC's rotation convention differs from KiCad's for most polarised packages; their preview shows the part body, fix any that look rotated there |
| THT parts placed by JLC | J10, J12 (mini-DIN C6089346), P1, P2 (DE9 C305939 if in stock), J4 (USB-A C564920), SW1, SW2 (C285519), J1, J2, J3, J7, J8 (headers/socket) | all in the BOM; untick any you want to hand-solder |
| DNP | JP1 (solder jumper), H1–H8 (holes) | already absent from the BOM and CPL |

## 3. After the boards arrive

- Clip pin 5 of J1 and J8 flush (the A500 keyboard plug's key position; the "KEY" silk marks it).
- OLED on its 8.4–8.5 mm socket, posts H5–H8 **nylon only** (copper passes 0.2 mm from the holes).
- Inspect the two pad-edge vias at R47.2 and C25.1 under a loupe (accepted trade-off; weak joints would show there first).
- First power: current-limited bench supply into USB-C (no Amiga, no F1 drama — F1 only matters on Amiga power), expect
  ~0.1 A idle. BOOTSEL + USB-C enumerates as RP2350 boot; flash the rev7 firmware.
- Then the Amiga: keyboard cable first (powers SYS5V through F1/D1), then the DE9s. With only the DE9s connected the
  ports still work (AMIGA_IO5V from pin 7) but the board needs USB-C power.

## 4. Known trade-offs on this revision (reviewed and kept 2026-10-17)

AVDD filter 8.7 mm from pin 46; C30 ground 9.6 mm; IOVDD pin 38 without its own cap; RM2 ground pins 1/21 through the
pour (first suspect if Wi-Fi/BT range is short); keyboard clamps at the A500 header; ~195 µF behind the USB-C diode.
Details in the vault `Rev7 Plan.md`.
