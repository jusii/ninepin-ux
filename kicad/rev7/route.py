#!/usr/bin/env python3
"""rev7 routing as code. Run after `make_board.py --force`. Coordinates mm, y down.
Block 1: RP2350 core - fanned combs on all four sides, decoupling, flash, VREG, crystal, the RM2
lines, the port-2 band and the In2 hops that take the north-side signals through the lane wall."""
import os, sys, pcbnew
from pcbnew import VECTOR2I, FromMM as mm
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import placement as PL
PCB = os.path.join(HERE, "amigahid-pico-rev7.kicad_pcb")
b = pcbnew.LoadBoard(PCB)
L = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu, "I2": pcbnew.In2_Cu}
CX, CY = PL.CX, PL.CY

def P(x, y): return VECTOR2I(mm(x), mm(y))
def net(name):
    n = b.FindNet(name)
    if n is None: raise SystemExit("no net " + name)
    return n
_pads = {}
def reload_pads():
    _pads.clear()
    for fp in b.GetFootprints():
        for pd in fp.Pads():
            _pads[(fp.GetReference(), pd.GetNumber())] = (pd.GetPosition().x / 1e6, pd.GetPosition().y / 1e6, pd.GetNetname())
reload_pads()
def pad(ref, num): x, y, n = _pads[(ref, str(num))]; return (x, y)
def padnet(ref, num): return _pads[(ref, str(num))][2]
def track(netname, pts, layer="F", w=0.2):
    n = net(netname)
    for a, c in zip(pts, pts[1:]):
        if abs(a[0] - c[0]) < 1e-6 and abs(a[1] - c[1]) < 1e-6: continue
        t = pcbnew.PCB_TRACK(b); t.SetStart(P(*a)); t.SetEnd(P(*c)); t.SetWidth(mm(w)); t.SetLayer(L[layer]); t.SetNet(n); b.Add(t)
POWER = {"/AMIGA_5V", "/AMIGA_5V_FUSED", "/AMIGA_IO5V", "/SYS5V", "/AUX_5V", "/USBA_5V", "/KBIN_5V", "/cont1_5v", "/cont2_5v"}
def via(netname, x, y, size=None, drill=0.3):
    if size is None: size = 0.6 if netname in POWER else 0.5                   # Power-net vias 0.6/0.3, signals 0.5/0.3
    v = pcbnew.PCB_VIA(b); v.SetPosition(P(x, y)); v.SetWidth(mm(size)); v.SetDrill(mm(drill)); v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); v.SetNet(net(netname)); b.Add(v)
def oct(a, c, first="diag"):
    dx, dy = c[0] - a[0], c[1] - a[1]; d = min(abs(dx), abs(dy)); sx = 1 if dx > 0 else -1; sy = 1 if dy > 0 else -1
    if d < 1e-6: return [a, c]
    m = (a[0] + sx * d, a[1] + sy * d) if first == "diag" else (c[0] - sx * d, c[1] - sy * d)
    return [a, m, c]
def route(netname, a, c, first="diag", layer="F", w=0.2): track(netname, oct(a, c, first), layer, w)
def gnd_stub(p, dx=0.0, dy=0.0):
    """GND pad -> short track -> via to the plane."""
    q = (p[0] + dx, p[1] + dy); track("GND", [p, q]); via("GND", *q)
def jumper(n, pts):
    """In2 hop through the 3V3 plane, via at both ends."""
    track(n, pts, "I2"); via(n, *pts[0]); via(n, *pts[-1])
def save(tag):
    b.Save(PCB); print("routed %s:" % tag, len(list(b.GetTracks())), "track items")

# ---------------- MCU combs ----------------
# pin k (offset from the side centre in pads) goes straight for s = S0 + 0.31*(7-|k|), then
# diagonally outward by (pitch-0.4)*|k| so the tracks end on a comb of the given pitch, then
# straight to `end` (or to a per-pin stop). The 0.31 stagger keeps parallel diagonals 0.5 apart.
S0 = 0.5
def comb(side, pitch, end, stop=None, skip=()):
    stop = stop or {}; ends = {}
    fp = b.FindFootprintByReference("U1")
    for pd in fp.Pads():
        num = pd.GetNumber(); x, y, n = _pads[("U1", num)]
        if not num or num == "61" or not n or num in skip: continue
        if side == "N" and not (y < CY - 3): continue
        if side == "S" and not (y > CY + 3): continue
        if side == "E" and not (x > CX + 3): continue
        if side == "W" and not (x < CX - 3): continue
        vert = side in "NS"
        k = round(((x - CX) if vert else (y - CY)) / 0.4); sgn = 1 if k > 0 else -1
        s = S0 + 0.31 * (7 - abs(k)); dlt = (pitch - 0.4) * abs(k)
        dirn = -1 if side in ("N", "W") else 1
        if vert:
            y0 = y + dirn * (0.4 + s); pts = [(x, y), (x, y0)]
            if k: pts.append((x + sgn * dlt, y0 + dirn * dlt))
            xe = x + sgn * dlt; ye = stop.get(num, end); pts.append((xe, ye)); ends[num] = (xe, ye)
        else:
            x0 = x + dirn * (0.4 + s); pts = [(x, y), (x0, y)]
            if k: pts.append((x0 + dirn * dlt, y + sgn * dlt))
            ye = y + sgn * dlt; xe = stop.get(num, end); pts.append((xe, ye)); ends[num] = (xe, ye)
        track(n, pts)
    return ends

N = comb("N", 1.0, CY - 9.2, stop={"11": 28.3, "6": 28.3, "5": 28.3, "4": 28.3, "3": 28.3, "2": 28.3, "1": CY - 8.55})
E = comb("E", 1.2, CX + 10.5, stop={"60": pad("U8", 1)[0], "59": pad("U8", 2)[0], "58": pad("U8", 3)[0],
                                     "57": CX + 9.2, "56": CX + 9.2, "55": CX + 9.2},
         skip=("46", "47", "48", "49", "50", "51", "52", "53", "54"))              # regulator, USB and 3V3 pins are hand-fanned below
W = comb("W", 1.0, CX - 10.2, stop={"20": CX - 7.3, "23": CX - 7.22, "22": pad("R54", 1)[0], "26": pad("R53", 1)[0],
                                     "30": CX - 8.55, "21": pad("Y1", 1)[0], "27": CX - 8.7, "28": CX - 8.7, "29": CX - 8.7})
# south comb at 0.6 mm pitch so it ends by y 43.3 and leaves room for the port-1 clamp rows; the vias
# on its columns sit on a 45-degree line (31..34, feeding the band) or alternate 43.3 / 43.9
S = comb("S", 0.6, 43.3, stop={"39": 43.21, "35": 44.5, "37": 45.5, "38": 44.0, "40": 43.9, "41": 43.3, "42": 43.9, "43": 45.65,
                               "31": 43.3, "32": 43.9, "33": 44.5, "34": 45.1})
via("/+3V3", *S["38"]); via("/dvdd", *S["39"])

for x in (CX - 0.565, CX + 0.565):                                             # EP vias between the paste windows
    for y in (CY - 0.565, CY + 0.565): via("GND", x, y)

# ---- north: IOVDD 1 / DVDD 6 / IOVDD 11 to the planes on their columns; ws_drv/exp_int/i2c (2..5) and the DVDD
# column hop through the lane wall on In2 (the F.Cu lanes to U6 and the B.Cu lanes to U7 both run here)
for num in ("1", "6", "11"):
    x, ye = N[num]; via(padnet("U1", num), x, ye)
for num in ("2", "3", "4", "5"):
    x, ye = N[num]; jumper(padnet("U1", num), [(x, ye), (x, 19.5)])      # end between F1's pads
n6 = N["6"]
# ---- east: QSPI ----
# BOOT: qspi_ss leaves the flash pad on F.Cu, hops through the wall on In2 at x 45.4 and runs east on B.Cu
p1 = pad("U8", 1); r1 = pad("R52", 1); r2 = pad("R52", 2); sw = pad("SW1", 1)
track("/qspi_ss", [p1, (r1[0], p1[1] - (r1[0] - p1[0])), r1])                       # flash CS -> the 1k at the flash: no stub on the CS line
track("/qspi_ss_sw", [r2, (47.6, r2[1]), (48.2, r2[1] + 0.6)]); jumper("/qspi_ss_sw", [(48.2, r2[1] + 0.6), (48.2, 23.4)])
track("/qspi_ss_sw", [(48.2, 23.4), (48.5, 23.7), (76.7, 23.7), (77.0, 23.4), (77.0, 18.7), (77.4, 18.3), (90.1, 18.3), (90.5, 17.9), (90.5, 12.9), (91.0, 12.4),
                      (sw[0] - 0.5, 12.4), (sw[0], 12.9), sw], "B")               # B.Cu all the way to the BOOT switch
for num, netn, pnum, xv in (("57", "/qspi_sd0", 5, 52.6), ("56", "/qspi_sclk", 6, 53.6), ("55", "/qspi_sd3", 7, 54.4)):
    e = E[num]; via(netn, *e); pp = pad("U8", pnum)
    track(netn, [e, (xv, e[1]), (xv, pp[1])], "B"); via(netn, xv, pp[1]); track(netn, [(xv, pp[1]), pp])
p4 = pad("U8", 4); track("GND", [(p4[0] + 0.6, p4[1]), (p4[0] + 1.2, p4[1] + 0.4)]); via("GND", p4[0] + 1.2, p4[1] + 0.4)
p8 = pad("U8", 8); track("/+3V3", [(p8[0] + 0.7, p8[1]), (p8[0] + 1.4, p8[1] - 0.4)]); via("/+3V3", p8[0] + 1.4, p8[1] - 0.4)
c1 = pad("C32", 1); track("/+3V3", [c1, (c1[0], p8[1] - 0.4 - (c1[0] - p8[0] - 1.4)), (p8[0] + 1.4, p8[1] - 0.4)]); gnd_stub(pad("C32", 2), 0, 1.1)
# ---- east: the regulator group right at the pins (RP2350 DS 6.3.8 fig. 26, turned 90 degrees). The port-2 band jogs
# 3 mm north under it (block 1) so the vias can sit at the caps. 50 FB / 49 VIN / 48 LX / 47 PGND / 46 AVDD leave east on
# their own rows; CIN (0402 land with 1.1 mm pad pitch) straddles VIN and PGND with LX between its pads, COUT the same
# across VOUT and PGND, the inductor stands east of COUT (VOUT on top, LX below). CIN and COUT each have their own ground
# via and the F.Cu pour stays out of the group (make_board.py), so the regulator ground meets the plane only there.
# AVDD dives south-east to the RC filter on the y 41.5 row (0.55 mm below the LX pad). Pins 53/54 (3V3) take their plane feed from the VIN node
# through a short link under the chip body, 0.25 mm inside the pad row.
pe = {n: pad("U1", n) for n in ("46", "47", "48", "49", "50")}
c29 = {k: pad("C29", k) for k in (1, 2)}; c38 = {k: pad("C38", k) for k in (1, 2)}; l1 = {k: pad("L1", k) for k in (1, 2)}
c37 = {k: pad("C37", k) for k in (1, 2)}; r59 = {k: pad("R59", k) for k in (1, 2)}
YFB, YVIN, YLX, YPG, YAV = (pe[n][1] for n in ("50", "49", "48", "47", "46"))
track("/dvdd", [pe["50"], (l1[1][0], YFB)]); track("/dvdd", [(c38[1][0], YFB), c38[1]])            # FB along the top into L1 pad 1, sensed at COUT
track("/dvdd", [c38[1], (l1[1][0] - 0.6, c38[1][1])], w=0.4)                                        # COUT -> inductor, the short way
track("/+3V3", [pe["49"], (c29[1][0] - 0.4, YVIN), (c29[1][0] - 0.25, c29[1][1]), c29[1], (39.1, c29[1][1])]); via("/+3V3", 39.1, c29[1][1])   # VIN: CIN, then the plane
track("/vreg_lx", [pe["48"], (37.5, YLX), (37.8, 39.1), (41.0, 39.1), (41.6, 39.7), l1[2]])     # between the cap pads into L1 pad 2
track("GND", [pe["47"], (37.0, YPG), (37.45, c29[2][1]), c29[2], (39.1, c29[2][1]), c38[2], (40.3, c38[2][1] + 0.45), (40.3, 40.5)])
via("GND", 39.1, c29[2][1]); via("GND", 40.3, 40.5)                                              # CIN's and COUT's own ground vias
track("/dvdd", [l1[1], (43.15, l1[1][1]), (43.3, l1[1][1] + 0.15)]); via("/dvdd", 43.3, l1[1][1] + 0.15); track("/dvdd", [(43.3, l1[1][1] + 0.15), (43.3, 41.9)], "B")   # VOUT to the web, via clear of the jogged band and the silk dot
yav = r59[2][1]
track("/vreg_avdd", [pe["46"], (36.9, YAV), (36.9 + (yav - YAV), yav), (c37[1][0], yav), r59[2]])
track("GND", [c37[2], (45.075, c37[2][1]), (45.3, c37[2][1] + 0.225)]); via("GND", 45.3, c37[2][1] + 0.225)   # CFILT ground beside the pad (the pio_dm lane runs below)
track("/+3V3", [r59[1], (48.2, r59[1][1]), (48.4, r59[1][1] + 0.2)]); via("/+3V3", 48.4, r59[1][1] + 0.2)   # RFILT from the plane
# USB pair straight east off pins 52 / 51 to the series resistors (the H7 post forces the dip past x 59)
for num, r, xt in (("52", "R57", 60.2), ("51", "R58", 59.4)):
    px, py = pad("U1", num); q1 = pad(r, 1); track(padnet("U1", num), [(px, py), (xt, py), (xt + (q1[1] - py), q1[1]), q1])
# USB_OTP_VDD + QSPI_IOVDD (53 / 54) bridged at the pads and linked to VIN under the body; C30 right there between the
# QSPI row and the USB pair, its ground via on the far side of the band jog (nothing nearer can take a via)
p53 = pad("U1", "53"); p54 = pad("U1", "54"); c30 = pad("C30", 1); g30 = pad("C30", 2)
track("/+3V3", [p54, (37.1, p54[1]), (37.1, p53[1]), p53]); track("/+3V3", [(37.1, c30[1]), c30])
track("/+3V3", [p54, (35.7, p54[1]), (35.7, YVIN), pe["49"]])
track("GND", [g30, (48.3, g30[1])]); via("GND", 48.3, g30[1])
# ---- west ----
via("/+3V3", CX - 7.4, W["20"][1]); e = W["30"]; p = pad("C26", 1); track("/+3V3", [e, (e[0], p[1]), (p[0] - 0.85, p[1])]); via("/+3V3", p[0] - 0.85, p[1])   # row end onto C26, via west of the pad
via("/dvdd", 26.4, W["23"][1])                                                  # on the column, east of C20
y1 = {n: pad("Y1", n) for n in (1, 2, 3, 4)}
c33 = pad("C33", 1)                                                               # xin: pad 1 -> north to C33 pad 1 (east pad)
track("/xin", [(y1[1][0], y1[1][1] - 0.55), (y1[1][0], c33[1] + 0.5), (y1[1][0] - 0.5, c33[1]), c33])
gnd_stub(pad("C33", 2), -0.9, 0.0)
g = pad("U9", 15); track("GND", [g, (g[0] + 0.55, g[1]), (g[0] + 0.9, g[1] + 0.35)]); via("GND", g[0] + 0.9, g[1] + 0.35)   # the module ground beside the wl_vin run
r2 = pad("R54", 2); yu = y1[4][1] + 1.05; c34 = pad("C34", 1); xw = c34[0]      # xout_r: under the crystal to pad 3 and C34
track("/xout_r", [r2, (r2[0], r2[1] + 0.6), (r2[0] - (yu - r2[1] - 0.6), yu), (xw, yu), (xw, y1[3][1]), y1[3]])
track("/xout_r", [(xw, yu), c34]); gnd_stub(pad("C34", 2), 0, 0.93)                                                   # below the pad, clear of wl_irq
g = y1[2]; track("GND", [g, (g[0], g[1] - 0.35), (g[0] - 0.5, g[1] - 0.85)]); via("GND", g[0] - 0.5, g[1] - 0.85); gnd_stub(y1[4], 1.2, 0.0)
for num in ("24", "25", "18", "19"):                                             # swd and kbin go to B.Cu at the row ends
    e = W[num]; via(padnet("U1", num), e[0] - 0.4, e[1]); track(padnet("U1", num), [e, (e[0] - 0.4, e[1])])
for num in ("27", "28", "29"): via(padnet("U1", num), *W[num])
r2 = pad("R53", 2); via("/run_sw", r2[0], r2[1] + 0.75); track("/run_sw", [r2, (r2[0], r2[1] + 0.75)])
# ---- south ----
e = S["44"]; r1 = pad("R50", 1); r2 = pad("R50", 2); c31 = pad("C31", 1)       # adc RC: column 44 -> R50 (vertical) pad 2 -> C31 pad 1
track("/adc_avdd", [e, (e[0], r2[1] - (r2[0] - e[0])), r2])
track("/adc_avdd", [r2, (r2[0] + 0.3, r2[1] + 0.3), (c31[0] - 0.5, r2[1] + 0.3), (c31[0], c31[1])])
via("/+3V3", r1[0], r1[1] + 1.1); track("/+3V3", [r1, (r1[0], r1[1] + 1.1)]); gnd_stub(pad("C31", 2), 0, 1.3)
e = S["45"]; p1 = pad("C28", 1); track("/+3V3", [e, (e[0], p1[1] - 0.23), (e[0] + 0.23, p1[1]), p1]); via("/+3V3", e[0], 43.0); gnd_stub(pad("C28", 2), 0, 1.0)                       # plane via on the S45 column
# the 0402 caps at the column vias: C21 (DVDD 6), C23 (IOVDD 11), C24 (IOVDD 1) north; C20 (DVDD 23) on its column west;
# C25 (IOVDD 20) and C26 (IOVDD 30) at the W-side vias
track("/dvdd", [n6, (n6[0], pad("C21", 1)[1])]); gnd_stub(pad("C21", 2), 0.52, 0)   # 0.52: the ws_drv In2 hop at x 37 allows no more
track("/+3V3", [N["11"], pad("C23", 1)]); g = pad("C23", 2); track("GND", [g, (g[0], 23.5)]); via("GND", g[0], 23.5)   # ground north of the lanes
track("/+3V3", [N["1"], (N["1"][0], pad("C24", 1)[1])]); gnd_stub(pad("C24", 2), 0, -0.85)
g = pad("C20", 1); track("/dvdd", [(26.4, W["23"][1]), (26.4 - (g[1] - W["23"][1]), g[1]), g]); gnd_stub(pad("C20", 2), -0.62, 0)
p = pad("C25", 1); track("/+3V3", [(CX - 7.4, W["20"][1]), (CX - 7.4 - (p[1] - W["20"][1]), p[1]), p]); gnd_stub(pad("C25", 2), -0.72, 0)
gnd_stub(pad("C26", 2), 0.72, 0)
for num in ("31", "32", "33", "34", "35", "37", "40", "41", "42"):
    via(padnet("U1", num), *S[num])
# C28 and the S-side 3V3: nothing else here
# wl_d: column 36 continues south on F.Cu, west under the clamp rows at y 49.4, up into RM2 pad 5;
# a branch at y 43.1 feeds R55/R56 pad 2. wl_clk: column 43 the same way at y 49.85 to pad 3.
# wl_d / wl_clk: columns 36 / 43 continue south on F.Cu and run west at y 45.65 / 46.05 (between the south-comb
# vias and the port-1 clamp rows) into the RM2's south pads; wl_d also feeds R55/R56 pad 2 on the way
e = S["36"]; u5 = pad("U9", 5); r55 = pad("R55", 2); r56 = pad("R56", 2); XB = r55[0] + 0.5
track("/wl_d", [e, (e[0], 45.25), (e[0] - 0.4, 45.65), (u5[0] + 0.8, 45.65), (u5[0] + 0.4, 45.25), (u5[0] + 0.4, u5[1] + 0.4), u5])
track("/wl_d", [(XB + 0.4, 45.65), (XB, 45.25), (XB, r55[1] + 0.5), r55, r56])
e = S["43"]; u3 = pad("U9", 3)
track("/wl_clk", [e, (e[0] - 0.4, 46.05), (u3[0] + 0.4, 46.05), (u3[0], 45.65), (u3[0], u3[1] + 0.5)])
# wl_dout: R55 pad 1 -> F.Cu to a via at (17.9, 44.6) -> B.Cu under the wl_cs column -> via -> RM2 pad 6
r = pad("R55", 1); u = pad("U9", 6)                                              # wl_dout: F.Cu at y 45.2 under the clamp rows into pad 6
track("/wl_dout", [r, (r[0], r[1] + 0.4), (r[0] - (45.2 - r[1] - 0.4), 45.2), (u[0] + 0.5, 45.2), (u[0], 44.7), u])
# wl_irq: R56 pad 1 -> between C34's pads into RM2 pad 10
r = pad("R56", 1); u = pad("U9", 10)
track("/wl_irq", [r, (r[0], 39.0), (r[0] - 0.4, 38.6), (u[0] + 0.5, 38.6), u])
# wl_on: B.Cu lane 45.7 west, north at x 18.5, via, F.Cu into pads 12/13. wl_cs: B.Cu lane 46.4 west, up at
# x 17.3 to a via, then the F.Cu column x 17.3 north into pad 9
e = S["35"]; u12 = pad("U9", 12); u13 = pad("U9", 13)
track("/wl_on", [e, (e[0], 45.3), (e[0] - 0.4, 45.7), (18.8, 45.7), (18.4, 45.3), (18.4, 35.5)], "B"); via("/wl_on", 18.4, 35.5)
track("/wl_on", [(18.4, 35.5), u12]); track("/wl_on", [(17.9, 35.5), (17.9, u13[1] + 0.4), (17.5, u13[1]), u13])
e = S["37"]; u9c = pad("U9", 9)
XC = 17.7                                                                         # wl_cs: B.Cu column up to a via by pad 9
track("/wl_cs", [e, (e[0], 46.0), (e[0] - 0.4, 46.4), (XC + 0.4, 46.4), (XC, 46.0), (XC, 39.4)], "B"); via("/wl_cs", XC, 39.4)
track("/wl_cs", [(XC, 39.4), (XC, u9c[1] - 0.4), (XC - 0.4, u9c[1]), u9c])
# port 2 (W 27-29 and S 31-34) joins the B.Cu band under the chip: 7 lanes at 0.45 mm, y 38.2..40.9, east to x 74
BAND = [38.2 + 0.45 * i for i in range(7)]
def jog(yl, i):                                                                  # 3 mm north under the regulator group (x 34.8..47.6)
    x0 = 34.8 + 0.186 * i; x1 = 43.5 + 0.186 * (6 - i)
    return [(x0, yl), (x0 + 3.0, yl - 3.0), (x1, yl - 3.0), (x1 + 3.0, yl)]
for i, num in enumerate(("27", "28", "29")):
    e = W[num]; yl = BAND[i]; n = padnet("U1", num)
    track(n, [e, (e[0] + (e[1] - yl), yl)] + jog(yl, i) + [(74.0, yl)], "B")
for j, num in enumerate(("31", "32", "33", "34")):                              # vias on a 45-degree line; north first, then a 1.5 mm diagonal
    e = S[num]; yl = BAND[3 + j]; n = padnet("U1", num)
    track(n, [e, (e[0], yl + 1.5), (e[0] + 1.5, yl)] + jog(yl, 3 + j) + [(74.0, yl)], "B")
save("block 1")

# ================= block 2: U6 at the west edge - MCU lanes, port 1 down the west edge, keyboard trio =================
reload_pads()
U6B = {n: pad("U6", n) for n in range(13, 25)}                    # B row (north, y 19.4): pin 13 east .. 24 west
LANE = {13: 18.0, 14: 17.6, 15: 17.2, 16: 16.8, 17: 16.4, 18: 16.0, 19: 15.6, 20: 15.2, 21: 14.8, 22: 14.4}   # lanes nearer the row end further east
def tvs(netname):
    for fp in b.GetFootprints():
        if fp.GetValue().startswith("PESD5V0S1") and padnet(fp.GetReference(), 1) == netname: return fp.GetReference()
    raise SystemExit("no clamp on " + netname)
# N columns 34..26 turn west at y 14.4..17.2 and drop into the B pads from above
for u1, u6 in (("7", 22), ("8", 21), ("9", 20), ("10", 19), ("12", 18), ("13", 17), ("14", 16), ("15", 15)):
    x, ytop = N[u1]; yl = LANE[u6]; n = padnet("U1", u1); px, py = U6B[u6]
    assert n == padnet("U6", u6), (u1, u6, n, padnet("U6", u6))
    track(n, [(x, ytop), (x, yl + 0.4), (x - 0.4, yl), (px + 0.2, yl), (px, yl + 0.2), (px, py)])
# W rows 16/17 (GPIO12/13) climb at x 22.4 / 21.8 and take the two southern lanes
for u1, u6, xv in (("16", 14, 22.4), ("17", 13, 21.8)):
    e = W[u1]; yl = LANE[u6]; n = padnet("U1", u1); px, py = U6B[u6]
    assert n == padnet("U6", u6), (u1, u6)
    pts = [e] if xv > e[0] - 0.45 else [e, (xv + 0.4, e[1])]
    track(n, pts + [(xv, e[1] - 0.4 if xv < e[0] - 0.45 else e[1] - (e[0] - xv)), (xv, yl + 0.4), (xv - 0.4, yl), (px + 0.2, yl), (px, yl + 0.2), (px, py)])
g = U6B[23]; track("GND", [g, (g[0], 20.6), (g[0] + 0.4, 21.0), (12.2, 21.0), (12.6, 21.4), (12.6, 21.6)]); via("GND", 12.6, 21.6)   # under the body
gnd_stub(pad("U6", 12), 0.7, 0.0)
# port 1: in-pad vias on A1..A7 (staggered), B.Cu lanes y 25.6..28.3 west to the edge bus (x 5.6..9.2), south
# along the west edge, then east into the P1 pins: hq..v from the north (lanes 48.2..50.0), b3/b2 from under
# the housing (55.5 / 55.95)
P1X = {n: pad("P1", n) for n in range(1, 10)}
PIN = {"/cont1_v": 1, "/cont1_h": 2, "/cont1_vq": 3, "/cont1_hq": 4, "/cont1_b3": 5, "/cont1_b1": 6, "/cont1_b2": 9}
END = {"/cont1_hq": 48.8, "/cont1_vq": 49.2, "/cont1_h": 49.6, "/cont1_b1": 50.0, "/cont1_v": 50.4, "/cont1_b2": 55.5, "/cont1_b3": 55.95}
for i, a in enumerate(range(5, 12)):                                # A4..A10 = U6 pins 5..11, west to east
    n = padnet("U6", a); px, py = pad("U6", a); vy = 23.4 if i % 2 == 0 else 22.6     # vias under the body, north of the pads (not in them)
    yl = 25.6 + 0.45 * i; xv = 5.6 + 0.6 * i; ye = END[n]; pp = P1X[PIN[n]]
    via(n, px, vy); track(n, [(px, py), (px, vy)])
    pts = [(px, vy), (px, yl - 0.4), (px - 0.4, yl), (xv + 0.4, yl), (xv, yl + 0.4)]
    if ye < 52:   # north lanes: down the edge, east, then south into the pin
        pts += [(xv, ye - 0.4), (xv + 0.4, ye), (pp[0] - 0.4, ye), (pp[0], ye + 0.4), pp]
    else:         # south lanes: under the housing, east, then north into the pin
        pts += [(xv, ye - 0.4), (xv + 0.4, ye), (pp[0] - 0.4, ye), (pp[0], ye - 0.4), pp]
    track(n, pts, "B")
# clamps: stubs on F.Cu from the pins up into the clamp rows (b2 via a B.Cu stub to the east of the rows)
for n, p in PIN.items():
    t = tvs(n); t1 = pad(t, 1); pp = P1X[p]
    track(n, [pp, t1])
# clamp grounds: row 1 vias beside the pads (h and vq share one), row 2 pads run out past the row-1 clamps
GV = {"/cont1_v": 17.1, "/cont1_h": 22.8, "/cont1_vq": 22.8, "/cont1_hq": 25.1, "/cont1_b3": 30.9, "/cont1_b1": 16.9, "/cont1_b2": 31.5}
seen = set()
for n in PIN:
    g = pad(tvs(n), 2); gx = GV[n]
    if n == "/cont1_b2": track("GND", [g, (gx - 0.35, g[1]), (gx - 0.1, g[1] + 0.25)]); via("GND", gx - 0.1, g[1] + 0.25); continue   # below the wl_cs lane
    track("GND", [g, (gx, g[1])])
    if (gx, g[1]) not in seen: via("GND", gx, g[1]); seen.add((gx, g[1]))
# pull-up arrays under the A row: short stubs from the pads, commons on an F.Cu bus at y 27.9
for rn in ("RN1", "RN2"):
    for k in range(1, 5):
        n = padnet(rn, k)
        if n == "": continue
        e = pad(rn, k); a = [p for p in range(5, 12) if padnet("U6", p) == n][0]; pa = pad("U6", a)
        d = e[0] - pa[0]; track(n, [(pa[0], pa[1] + 0.5), e] if abs(d) < 0.01 else [(pa[0], pa[1] + 0.5), (pa[0], 25.3), (e[0], 25.3 + abs(d)), e])   # straight, or out of the pad, jog, full run down
c13 = pad("C13", 1); v24 = U6B[24]
YF = 28.55                                                                     # commons bus under the arrays
track("/AMIGA_IO5V", [(8.1, YF), (max(pad("RN2", k)[0] for k in range(5, 9)), YF)], w=0.4)
for rn in ("RN1", "RN2"):
    for k in range(5, 9):
        e = pad(rn, k); track("/AMIGA_IO5V", [e, (e[0], YF)], w=0.3)
track("/AMIGA_IO5V", [c13, (c13[0], 20.6), (c13[0] + 1.2, 19.4), v24], w=0.4)
track("/AMIGA_IO5V", [c13, (c13[0] - 0.9, c13[1] + 0.9), (c13[0] - 0.9, YF - 0.9), (c13[0], YF), (8.1, YF)], w=0.4)
g = pad("C13", 2); track("GND", [g, (g[0], g[1] + 0.75)]); via("GND", g[0], g[1] + 0.75)   # U6's own vias now sit under the body
# keyboard trio: in-pad vias on A8..A10, B.Cu north under U6, east on lanes 11.3 / 11.8 / 12.3 to J10, RN3, then
# through the ws_drv/exp_int/i2c wall on F.Cu (lanes 10.8 / 11.8 / 12.3) to J1 and the clamps
# kbclock: B.Cu lane 11.3 to a via at J10 pin 5, then F.Cu: up into pin 5, and east at y 10.3 past RN3 pin 3 to J1.
# kbdata: B.Cu lane 11.8 to J1's wall via; B.Cu stub up into J10 pin 1 (THT); RN3 tap from pin 1 on F.Cu.
# kbreset: B.Cu lane 12.3; RN3 tap via beside the array; F.Cu through the wall at 12.3.
for n, a, yl, xe in (("kbreset", 2, 11.3, 34.1), ("kbdata", 3, 11.8, 34.8), ("kbclock", 4, 12.3, 30.0)):
    px, py = pad("U6", a); vy = 22.6 if a != 3 else 23.4; net_ = "/" + n
    via(net_, px, vy); track(net_, [(px, py), (px, vy)]); track(net_, [(px, vy), (px, yl + 0.4), (px + 0.4, yl), (xe, yl)], "B")
rn3 = {padnet("RN3", k): pad("RN3", k) for k in range(1, 4)}
j10_5 = pad("J10", 5); j10_1 = pad("J10", 1); j1 = {k: pad("J1", k) for k in ("1", "2", "3")}
# kbclock: via at J10 pin 5 (F.Cu up into the pin); via at x 30.0, F.Cu lane y 10.3 past RN3 pin 3 to J1 pin 1
via("/kbclock", 26.6, 12.3); track("/kbclock", [(26.6, 12.3), j10_5])
e = rn3["/kbclock"]; via("/kbclock", 30.0, 12.3)
track("/kbclock", [(30.0, 12.3), (30.0, 10.7), (30.4, 10.3), (e[0], 10.3), e]); track("/kbclock", [(e[0], 10.3), (j1["1"][0], 10.3), j1["1"]])
# kbdata: via at J10 pin 1 (F.Cu up into the pin), via on the lane by RN3 pin 2, wall via at 34.8 -> F.Cu lane 11.8 to J1 pin 2
via("/kbdata", 28.7, 11.8); track("/kbdata", [(28.7, 11.8), j10_1])
e = rn3["/kbdata"]; via("/kbdata", e[0], 11.95); track("/kbdata", [(e[0], 11.95), e])
via("/kbdata", 34.8, 11.8); track("/kbdata", [(34.8, 11.8), (j1["2"][0], 11.8), j1["2"]])
# kbreset: wall via at 34.1, then the F.Cu lane y 12.3 east to J1 (no board pull-up: RN3 element 1 is a spare)
via("/kbreset", 34.1, 11.3)
track("/kbreset", [(34.1, 11.3), (34.1, 12.3), (j1["3"][0], 12.3), j1["3"]])
for n, j in (("kbclock", "1"), ("kbdata", "2"), ("kbreset", "3")):                     # clamp stubs off the J1 legs
    pj = j1[j]; t = tvs("/" + n); t1 = pad(t, 1)
    if t1[0] < pj[0]: track("/" + n, [(pj[0], t1[1] + 0.6), (t1[0] + 0.4, t1[1]), t1])
    else:             track("/" + n, [(pj[0], t1[1] + (t1[0] - pj[0])), t1])
    g = pad(t, 2); track("GND", [g, (g[0], g[1] - 0.95)]); via("GND", g[0], g[1] - 0.95)
# FUSED to U6 and RN3: F1 pad 2 west along y 13.9 (north of the lanes), down x 9.4 into pin 24
f1 = pad("F1", 2)
via("/AMIGA_IO5V", 34.7, 14.6, 0.6, 0.3); track("/AMIGA_IO5V", [(34.7, 14.6), (34.7, 14.5), (34.1, 13.9), (9.4, 13.9), (9.0, 14.3), (9.0, v24[1] - (v24[0] - 9.0)), v24], w=0.4)   # I/O rail west branch, up B.Cu from D4's via (block 3a)
for k in range(5, 9):
    e = pad("RN3", k); track("/AMIGA_IO5V", [e, (e[0], 13.9)], w=0.3)
# SWD: W rows 24/25 -> B.Cu north-west at x 20.3 / 20.8 -> In2 through the lane band and the keyboard lanes -> J2
for u1, xv, yt, yv, j2 in (("24", 20.4, 18.6, 10.6, "1"), ("25", 19.9, 19.3, 9.6, "3")):
    e = W[u1]; n = padnet("U1", u1); v0 = (e[0] - 0.4, e[1]); pj = pad("J2", j2)
    track(n, [v0, (xv, v0[1] - (v0[0] - xv)), (xv, yt)], "B"); jumper(n, [(xv, yt), (xv, yv)])
    if j2 == "3": track(n, [(xv, yv), (xv, pj[1] + (xv - pj[0])), pj])
    else:         track(n, [(xv, yv), (xv - 0.4, yv + 0.4), (pj[0] + 0.5, yv + 0.4), (pj[0], yv - 0.1), pj])
# wl_vin: C36 by U6 links to the filter at the RM2 on B.Cu; FB1 / C35 / the two module pads on F.Cu
c36 = pad("C36", 1); fb2 = pad("FB1", 2); fb1 = pad("FB1", 1); c35 = pad("C35", 1); u14 = pad("U9", 14); u16 = pad("U9", 16)
XV = c36[0] - 0.5
via("/wl_vin", XV, c36[1] + 0.95); track("/wl_vin", [c36, (XV, c36[1] + 0.95)]); gnd_stub(pad("C36", 2), 0, -0.95)
track("/wl_vin", [(XV, c36[1] + 0.95), (XV, 28.75)], "B"); via("/wl_vin", XV, 28.75)
track("/wl_vin", [(XV, 28.75), fb2])
track("/wl_vin", [fb2, (fb2[0] + 0.6, fb2[1]), (c35[0] - 0.55, fb2[1] + (c35[0] - 0.55 - fb2[0] - 0.6)), (c35[0] - 0.55, c35[1]), c35])
track("/wl_vin", [fb2, (17.6, fb2[1]), (17.3, fb2[1] + 0.3), (17.3, u14[1]), u14])
track("/wl_vin", [(17.3, 31.6), (u16[0] + 0.5, 31.6), (u16[0], 31.1), u16])                                  # south of pin 15
via("/+3V3", 17.8, fb1[1]); track("/+3V3", [fb1, (17.8, fb1[1])])                    # west of the bead, between it and the wl_vin riser
g = pad("C35", 2); track("GND", [g, (g[0], g[1] - 0.45), (g[0] + 0.4, g[1] - 0.85), (21.2, g[1] - 0.85)]); via("GND", 21.2, g[1] - 0.85)
save("block 2")

# ================= block 3a: north-east - ws_drv/exp_int/i2c, keyboard in, J1/J8/J10/J12, the 5 V rails, LDO, parked caps =================
reload_pads()
# ws_drv / exp_int / i2c: the In2 hops end at y 19.5 between F1's pads; B.Cu jogs north-west onto four verticals that climb
# west of J8 and turn east onto lanes y 10.1 / 10.6 / 11.1 / 11.6 (rx, tx, scl, sda) to J7 and the OLED socket
WALL = {"5": (35.5, 10.1), "4": (35.97, 10.6), "3": (36.43, 11.1), "2": (36.9, 11.6)}
for num, (xv, yl) in WALL.items():
    x = N[num][0]; n = padnet("U1", num)
    track(n, [(x, 19.5), (xv, 19.5 - (x - xv)), (xv, yl + 0.4), (xv + 0.4, yl)], "B")
j3 = {k: pad("J3", k) for k in ("1", "2", "3", "4")}
track("/ws_drv", [(36.37, 10.6), (91.9, 10.6)], "B")                           # GPIO2: status LED drive, east along the north
track("/exp_int", [(35.9, 10.1), (88.3, 10.1)], "B"); via("/exp_int", 88.3, 10.1)   # GPIO3: expander interrupt
track("/i2c_sda", [(37.3, 11.6), (j3["4"][0] - 0.4, 11.6), (j3["4"][0], 12.0), j3["4"]], "B")
track("/i2c_scl", [(36.83, 11.1), (74.25, 11.1)], "B"); via("/i2c_scl", 74.25, 11.1)
track("/i2c_scl", [(74.25, 11.1), (74.25, 11.6), (j3["3"][0] - 0.4, 12.9), j3["3"]])
r41 = pad("R41", 2); r42 = pad("R42", 2)                                         # pull-ups by the socket
track("/i2c_scl", [j3["3"], (j3["3"][0], 14.4), (r42[0] + 1.6, r42[1]), r42]); track("/i2c_sda", [j3["4"], (j3["4"][0], r41[1] - 0.36), r41])
for r in ("R41", "R42"):
    p = pad(r, 1)
    if r == "R41": via("/+3V3", p[0] + 1.0, p[1] + 0.42); track("/+3V3", [p, (p[0] + 0.58, p[1]), (p[0] + 1.0, p[1] + 0.42)])   # clear of the SYS5V lane
    else:          via("/+3V3", p[0], p[1] + 1.0); track("/+3V3", [p, (p[0], p[1] + 1.0)])
# (the THT pins of J3 / J7 reach the planes directly)
# keyboard in: F.Cu columns x 68.6 / 69.2 from the mini-DIN pins down to U7 A1/A2 (y 28.5 / 28.95), B.Cu lanes
# y 15.3 / 15.75 west into J8 pins 1/2; clamps D20/D21 and the array RN4 hang off those
j12 = {k: pad("J12", k) for k in ("1", "3", "4", "5", "SH")}; j8 = {k: pad("J8", k) for k in ("1", "2", "3", "4", "6", "7", "8")}
q2 = {k: pad("Q2", k) for k in (1, 2, 3)}; q3 = {k: pad("Q3", k) for k in (1, 2, 3)}; d20 = pad("D20", 1); d21 = pad("D21", 1)
track("/kbin_clk", [j12["5"], (68.6, q3[3][1] - 0.4), (69.0, q3[3][1]), q3[3]])                       # into Q3's drain
track("/kbin_clk", [(68.6, d20[1]), d20]); gnd_stub(pad("D20", 2), -1.2, 0)
track("/kbin_dat", [j12["1"], (j12["1"][0], 9.8), (69.2, 11.3), (69.2, 22.4), (69.6, 22.8), q2[3]])                        # down past D21 into Q2's drain
# sources to the GPIO lanes (B.Cu 25.0 / 25.6, which end here), gates to the 3V3 plane
track("/kbin_dat_lo", [q2[2], (q2[2][0], 25.1)]); via("/kbin_dat_lo", q2[2][0], 25.1)
track("/kbin_clk_lo", [q3[2], (q3[2][0], 25.6)]); via("/kbin_clk_lo", q3[2][0], 25.6)
track("/+3V3", [q2[1], (70.3, q2[1][1])]); via("/+3V3", 70.3, q2[1][1])
track("/+3V3", [q3[1], (74.45, q3[1][1])]); via("/+3V3", 74.45, q3[1][1])
track("/kbin_dat", [(69.2, d21[1]), d21]); gnd_stub(pad("D21", 2), 0, -0.95)
via("/kbin_clk", 68.6, 16.0); via("/kbin_dat", 69.2, 15.3)                      # dat (ends further east at J8) takes the northern lane
track("/kbin_clk", [(68.6, 16.0), (j8["1"][0] + 0.4, 16.0), (j8["1"][0], 15.6), j8["1"]], "B")
track("/kbin_dat", [(69.2, 15.3), (j8["2"][0] + 0.4, 15.3), (j8["2"][0], 14.9), j8["2"]], "B")
# RN4 (kbin pull-ups) sits on the kbin B.Cu lanes west of the H5 post: vias up from the lanes into pins 3 / 4, KBIN_5V from the y 7.5 lane
rn4 = {padnet("RN4", k): pad("RN4", k) for k in (3, 4)}
e = rn4["/kbin_dat"]; via("/kbin_dat", e[0] - 0.7, 15.3); track("/kbin_dat", [(e[0] - 0.7, 15.3), (e[0] - 0.7, e[1]), e])   # pin 3 from the west
e = rn4["/kbin_clk"]; via("/kbin_clk", e[0], 16.0); track("/kbin_clk", [(e[0], 16.0), e])                            # pin 4 from below
q = pad("RN4", 8); track("/KBIN_5V", [pad("RN4", 5), q], w=0.3); via("/KBIN_5V", 59.0, 7.5, 0.6, 0.3)
track("/KBIN_5V", [(59.0, 7.5), (59.0, q[1])], w=0.3)                                                     # down the free column between D30 and R45
# kbreset into J8 pin 3 from the F.Cu lane; KBIN_5V: J12 pin 4 -> B.Cu y 7.5 west -> via -> F.Cu into J8 pin 4; F3 / C6 / C7 on the east
track("/kbreset", [(j8["3"][0], 12.3), j8["3"]])
f3 = {k: pad("F3", k) for k in ("1", "2")}; c6 = pad("C6", 1); c7 = pad("C7", 1)
track("/KBIN_5V", [j12["4"], (j12["4"][0], 7.1), (j12["4"][0] + 0.4, 7.5), (83.3, 7.5)], "B", w=0.3)
via("/KBIN_5V", 83.3, 7.5); track("/KBIN_5V", [(83.3, 7.5), (83.3, 7.3), (82.85, 6.85), f3["2"]], w=0.4)
track("/KBIN_5V", [(83.3, 7.5), (83.3, 8.1), (79.6, 8.1), (79.3, 8.4), (79.3, 11.3), (79.6, 11.6), c6], w=0.3)   # between the fuse pads, down its west side
track("/KBIN_5V", [(79.3, 11.3), (79.3, c7[1] - 0.3), (79.6, c7[1]), c7], w=0.3)
g6 = pad("C6", 2); g7 = pad("C7", 2); track("GND", [g6, (g6[0], g7[1])])   # C6 grounds through C7's via (the I2C lanes own the B.Cu above)
gnd_stub(pad("C7", 2), 0, 0.8)
track("/KBIN_5V", [(j12["4"][0], 7.1), (j12["4"][0] - 0.4, 7.5), (46.6, 7.5)], "B", w=0.3); via("/KBIN_5V", 46.6, 7.5)
track("/KBIN_5V", [(46.6, 7.5), (46.6, 13.02), j8["4"]], w=0.4)
# AMIGA_5V: J10 pin 4 along the north edge to J1 pin 4, then down the J8 gap at x 49.43 and west at y 17.5 into F1 pad 1
j10 = {k: pad("J10", k) for k in ("1", "3", "4", "5", "SH")}; j1 = {k: pad("J1", k) for k in ("4", "6", "7", "8")}; f1 = {k: pad("F1", k) for k in ("1", "2")}
track("/AMIGA_5V", [j10["4"], (j10["4"][0], 4.5), (j10["4"][0] + 1.2, 3.3), (j1["4"][0] - 0.8, 3.3), (j1["4"][0], 4.1), j1["4"]], w=0.6)
track("/AMIGA_5V", [j1["4"], (j1["4"][0], 6.62), (48.3, 7.9), (49.43, 7.9), (49.43, 12.6)], w=0.6)
track("/AMIGA_5V", [(49.43, 12.6), (49.43, 15.4)], w=0.3)                                      # neck between J8 pins 5 and 6
track("/AMIGA_5V", [(49.43, 15.4), (49.43, 17.1), (49.03, 17.5), (f1["1"][0] + 0.3, 17.5), f1["1"]], w=0.6)
# The I/O rail (2026-10-16): F1 pad 2 feeds D4 (west of F1); D4's cathode goes north to the west branch and, through a via
# beside it, down B.Cu to the y 24.4 bus east (R46 tap, C14 / U7 VCC, the port-2 pull-ups). The fused keyboard 5 V itself
# reaches D1 on B.Cu under the sense row (y 23.5) and up to a short F.Cu lane at y 22.65 that also feeds R60.
r46 = pad("R46", 1); r60 = pad("R60", 1); d1 = {k: pad("D1", k) for k in ("1", "2")}; c14 = pad("C14", 1); u7v = pad("U7", 24)
d4a = pad("D4", 2); d4k = pad("D4", 1)
track("/AMIGA_5V", [(f1["1"][0] - 1.5, d4a[1]), d4a], w=0.6)                                              # F1 pad 1 (before the fuse) -> D4 anode
track("/AMIGA_IO5V", [d4k, (d4k[0], 20.6), (34.7, 20.3)], w=0.6); via("/AMIGA_IO5V", 34.7, 20.3, 0.6, 0.3)   # between D4's pads, west of the In2 hops and the exp_int riser
track("/AMIGA_IO5V", [(34.7, 20.3), (34.7, 14.6)], "B", w=0.6)                                           # north to the west branch (block 2)
track("/AMIGA_IO5V", [(34.7, 20.3), (34.7, 24.4), (69.5, 24.4)], "B", w=0.6); track("/AMIGA_IO5V", [(69.5, 24.4), (73.5, 24.4)], "B", w=0.5)   # 0.5 under Q2 so its source via fits
track("/AMIGA_IO5V", [(73.5, 24.4), (75.9, 24.4)], "B", w=0.6); via("/AMIGA_IO5V", 75.9, 24.4, 0.6, 0.3)
via("/AMIGA_IO5V", 40.65, 24.4, 0.6, 0.3); track("/AMIGA_IO5V", [(40.65, 24.4), (40.65, r46[1] + 1.05), r46], w=0.5)   # R46 (sense) from the bus
track("/AMIGA_5V_FUSED", [(39.9, f1["2"][1]), (39.9, 23.55)], w=0.6); via("/AMIGA_5V_FUSED", 39.9, 23.55, 0.6, 0.3)   # F1 pad 2 -> via just below the pad -> B.Cu riser -> the D1 lane
track("/AMIGA_5V_FUSED", [(39.9, 23.55), (39.9, 19.65), (40.6, 18.95), (41.5, 18.95)], "B", w=0.6); via("/AMIGA_5V_FUSED", 40.6, 18.95, 0.6, 0.3); via("/AMIGA_5V_FUSED", 41.5, 18.95, 0.6, 0.3)   # two vias in parallel on the main supply
YA = 18.95                                                                      # D1 anode lane north of the sense row
track("/AMIGA_5V_FUSED", [(40.6, YA), (d1["2"][0] - 1.1, YA), (d1["2"][0], YA + 1.1), d1["2"]], w=0.6)
track("/AMIGA_5V_FUSED", [(r60[0], YA), r60], w=0.3)
track("/AMIGA_IO5V", [(75.9, 24.4), (75.9, 23.2), (76.4, 22.7), (76.7, 22.4), u7v], w=0.4); track("/AMIGA_IO5V", [(75.9, 23.2), (75.9, 20.4), (c14[0] + 0.05, c14[1] + 0.35), c14], w=0.4)

gnd_stub(pad("C14", 2), 1.0, 0); gnd_stub(pad("U7", 23), 0, 0.95)
# SYS5V: D1 pad 1 -> C2 / C8 / U3 inputs along y 21.6; R45 pad 2 down x 61.83 to y 19 and west into U3; C1 and C3;
# via (61.83, 16.4) -> B.Cu y 16.4 east -> via under D2's anode; D2 -> y 16 east to the trunk via and F3
c2 = pad("C2", 1); c8 = pad("C8", 1); u3 = {k: pad("U3", k) for k in ("1", "3", "4", "6", "7")}; r45 = pad("R45", 2); c1 = pad("C1", 1); c3 = pad("C3", 1); d2 = pad("D2", 1)
XS = 61.0                                                                        # SYS5V column west of the OLED post
YB5 = 22.55
track("/SYS5V", [(d1["1"][0] + 1.25, YB5 - 1.0), (d1["1"][0] + 1.75, YB5 - 0.5), (d1["1"][0] + 2.25, YB5), (59.5, YB5)], w=0.6)
track("/SYS5V", [(u3["6"][0] - 0.1, YB5), (u3["6"][0] - 0.1, u3["6"][1])], w=0.4)
track("/SYS5V", [c2, (c2[0], YB5)], w=0.4); track("/SYS5V", [c8, (c8[0], YB5)], w=0.4)
track("/SYS5V", [r45, (r45[0] - 0.4, r45[1] + 0.4), (XS, r45[1] + 0.4 + (r45[0] - 0.4 - XS)), (XS, 16.8)], w=0.4); track("/SYS5V", [(XS, 16.8), (XS, 18.9), (59.05, 18.9), (59.05, YB5)], w=0.5); track("/SYS5V", [(u3["4"][0] - 0.1, 18.9), (u3["4"][0] - 0.1, u3["4"][1])], w=0.4)   # x 59.05 joins the two U3 inputs west of the chip
via("/SYS5V", XS, 16.8, 0.6, 0.3)
track("/SYS5V", [(XS, 16.8), (78.95, 16.8), (79.6, 16.15)], "B", w=0.6); via("/SYS5V", 79.6, 16.15, 0.6, 0.3); track("/SYS5V", [(79.6, 16.15), (79.6, d2[1])], w=0.6)   # via north of the pad
track("/SYS5V", [(59.5, YB5), (60.0, YB5 + 0.5), (c1[0] - 0.75, YB5 + 0.5), (c1[0], YB5 + 1.25), c1], w=0.4)
track("/SYS5V", [(d1["1"][0] - 1.25, 21.6), (c3[0] + 0.17, 22.05), (c3[0] + 0.17, c3[1] - 0.6), c3], w=0.4)
gnd_stub(pad("C3", 2), 0.9, 0.9); gnd_stub(pad("C1", 2), 0, 1.7)
gnd_stub(c2 and pad("C2", 2), 0, -1.0); gnd_stub(pad("C8", 2), 0, -1.0)
# LDO U3: output pin 1 -> C9 / C10 (shared 3V3 via between them); grounds
c9 = pad("C9", 1); c10 = pad("C10", 1)
track("/+3V3", [u3["1"], (c9[0] - 0.45, u3["1"][1]), c9, c10], w=0.4)
via("/+3V3", c10[0] + 1.05, c10[1]); track("/+3V3", [c10, (c10[0] + 1.05, c10[1])], w=0.4)                      # three vias carry the LDO output into the plane
via("/+3V3", u3["1"][0], u3["1"][1] + 0.9); track("/+3V3", [u3["1"], (u3["1"][0], u3["1"][1] + 0.9)], w=0.4)
gnd_stub(pad("C9", 2), 0, -1.1); gnd_stub(pad("C10", 2), 0, -1.0)
g = u3["3"]; track("GND", [g, (g[0] + 0.4, g[1] - 0.4)]); via("GND", g[0] + 0.4, g[1] - 0.4)   # clear of C9's pad
via("GND", u3["7"][0], u3["7"][1] - 0.4); via("GND", u3["7"][0], u3["7"][1] + 0.4)                     # two in the thermal pad
# D2 anode on to the trunk and F3 (east corner block takes it from (92.4, 16.0))
track("/SYS5V", [d2, (d2[0], 16.9), (d2[0] + 0.9, 16.0), (92.7, 16.0)], w=0.5); via("/SYS5V", 92.7, 16.0, 0.6, 0.3)
track("/SYS5V", [(83.7, 16.0), (83.7, f3["1"][1] + 0.7), f3["1"]], w=0.4)                 # keyboard fuse from the trunk lane
# parked 3V3 caps under the OLED (two rows), vias north / south of the rows
for ref, dy in (("C27", -1.3),):                                               # plane bulk, stays under the OLED
    p1 = pad(ref, 1); p2 = pad(ref, 2); via("/+3V3", p1[0], p1[1] + dy); track("/+3V3", [p1, (p1[0], p1[1] + dy)]); gnd_stub(p2, 0, dy)
# keyboard LED lines: J1 pins 7/8 down into J8 pins 7/8 with the clamps on the way; JP1 / R45 balance network
d29 = pad("D29", 1); d30 = pad("D30", 1); jp1 = {k: pad("JP1", k) for k in ("1", "2")}; r45b = pad("R45", 1)
track("/kb_pwrled", [j1["7"], (j1["7"][0], j8["7"][1] - 2.25 - (j1["7"][0] - j8["7"][0])), (j8["7"][0], j8["7"][1] - 2.25), j8["7"]])
track("/kb_pwrled", [(j1["7"][0], d29[1]), (d29[0] - 0.23, d29[1])]); gnd_stub(pad("D29", 2), 0, -0.85)
track("/kb_pwrled", [j1["7"], (j1["7"][0], 3.9), (j1["7"][0] + 0.6, 3.3), (jp1["1"][0] - 0.6, 3.3), (jp1["1"][0], 3.9), jp1["1"]])
track("/kb_drvled", [j1["8"], (j1["8"][0], j8["8"][1] - 2.25 - (j1["8"][0] - j8["8"][0])), (j8["8"][0], j8["8"][1] - 2.25), j8["8"]])
track("/kb_drvled", [(j1["8"][0], d30[1]), (d30[0] - 0.23, d30[1])]); gnd_stub(pad("D30", 2), 0, -0.85)
track("/kb_pwrled_bal", [jp1["2"], (jp1["2"][0], r45b[1] - (jp1["2"][0] - r45b[0])), r45b])
save("block 3a")

# ================= block 3b-1: centre links - dvdd web, amiga_sense, kbin_lo lanes =================
reload_pads()
# dvdd: W23 via -> north beside the 3V3 via -> y 29.4 (joins C20's stub) -> pin-6 via; S39 via -> y 41.4 / 42.6 -> C38 via
track("/dvdd", [(26.4, 36.8), (26.4, 30.0), (27.0, 29.4), (33.9, 29.4), (35.0, 28.3)], "B")
track("/dvdd", [S["39"], (33.6, 42.2), (33.9, 41.9), (43.3, 41.9)], "B")           # S39 via -> the VOUT via, just south of the band
c22 = pad("C22", 1); track("/dvdd", [S["39"], (S["39"][0], c22[1] - (c22[0] - S["39"][0])), c22]); gnd_stub(pad("C22", 2), 0, 0.8)   # DVDD 39: 0402 beside its via
# amiga_sense: S42 via -> In2 north-east past the qspi vias -> via north of U8 -> F.Cu up to the divider (R46 pad 2, R47, C11)
track("/amiga_sense", [(35.4, 43.9), (35.4, 42.3), (36.0, 41.7), (36.0, 35.5), (36.8, 34.7), (40.6, 34.7), (40.6, 29.5), (42.6, 27.5), (43.3, 27.5)], "I2"); via("/amiga_sense", 43.3, 27.5)
r46b = pad("R46", 2); r47 = pad("R47", 1); c11 = pad("C11", 1)
track("/amiga_sense", [(43.3, 27.5), r47])
track("/amiga_sense", [r46b, (r46b[0], 22.2), (r46b[0] + 0.3, 22.5), (c11[0] - 0.3, 22.5), (c11[0], 22.2), c11])
# kbin_lo: W18 / W19 vias north on x 22.4 / 22.0, east on y 25.6 / 25.0 under the FUSED lane, vias under U7's body, stubs to pins 22 / 21
u7 = {n: pad("U7", n) for n in range(13, 25)}
track("/kbin_clk_lo", [(22.4, 31.8), (22.4, 26.0), (22.8, 25.6), (69.8, 25.6), (70.0, 25.8), (pad("Q3", 2)[0] - 0.2, 25.8), (pad("Q3", 2)[0], 25.6)], "B")   # jogs south past Q2's via, ends at Q3's source via
track("/kbin_dat_lo", [(22.4, 32.8), (21.8, 32.2), (21.8, 25.6), (22.4, 25.0), (pad("Q2", 2)[0], 25.0), (pad("Q2", 2)[0], 25.1)], "B")   # ends at Q2's source via
# the sense divider grounds share one F.Cu run with vias at both ends (the top pour cannot reach them)
track("GND", [pad("R47", 2), (pad("C11", 2)[0], pad("R47", 2)[1])]); gnd_stub(pad("R47", 2), -0.65, 0); gnd_stub(pad("R60", 2), 0, 0.88)   # R60's ground pad is south now (fused pad on the D1 lane); via below it, clear of the SYS5V feed
# north and south groups join on In2 from the W23 via to the south link's corner
track("/dvdd", [(26.4, 36.8), (26.4, 38.4), (29.9, 41.9), (33.6, 41.9), (33.6, S["39"][1])], "I2")                 # ends on the S39 via
save("block 3b-1")

# ================= block 3b-2: port 2 - band into U7's B row, the Amiga-side fan, clamps, pull-ups, FUSED bus =================
reload_pads()
u7 = {n: pad("U7", n) for n in range(1, 25)}
# the seven band lanes turn north at their pin's x (north lane first, so the turns nest) and end on vias under the body
for i, (num, pin) in enumerate((("27", 20), ("28", 19), ("29", 18), ("31", 17), ("32", 16), ("33", 15), ("34", 14))):
    n = padnet("U1", num); yl = BAND[i]; xp = u7[pin][0]; yv = 24.0 if i % 2 == 0 else 24.8
    track(n, [(74.0, yl), (xp - 0.45, yl), (xp, yl - 0.45), (xp, yv)], "B"); via(n, xp, yv); track(n, [(xp, yv), u7[pin]])
gnd_stub(u7[12], 0, 1.0)
# A row -> south to y 30.2 -> parallel 45-degree runs south-west -> straight down through clamp pad 1, pull-up pad 1 and the DE9 pin
YS = 30.2
for pin, tvs, r, p2 in (("4", "D22", "R61", "1"), ("5", "D23", "R62", "6"), ("6", "D24", "R63", "2"), ("7", "D25", "R64", "3"),
                        ("8", "D26", "R65", "4"), ("9", "D27", "R66", "9"), ("10", "D28", "R67", "5")):
    n = padnet("U7", pin); px, py = u7[int(pin)]; t1 = pad(tvs, 1); r1 = pad(r, 1); pp = pad("P2", p2); x = t1[0]
    assert abs(r1[0] - x) < 0.02 and abs(pp[0] - x) < 0.02, (tvs, r, p2, t1, r1, pp)
    track(n, [(px, py), (px, YS), (x, YS + px - x), t1])
    if r == "R61":   track(n, [t1, (x, 45.6), (x - 0.4, 46.0), (x - 0.4, 47.7), (x, 48.1), r1, pp])     # v jogs west round R62's FUSED pad
    elif r == "R65": track(n, [t1, r1, (x, 47.3), (x - 0.4, 47.7), (x - 0.4, 49.3), (x, 49.7), pp])     # hq jogs west round R66's FUSED pad
    else:            track(n, [t1, r1, pp])
for tvs, dy in (("D22", -1.0), ("D25", -1.0), ("D28", -1.0), ("D24", 1.2), ("D26", 1.2), ("D23", 1.1), ("D27", 1.1)): gnd_stub(pad(tvs, 2), 0, dy)
# pull-ups: FUSED pads to vias on a B.Cu bus at y 47.5 between the two rows; the bus is fed from the east on F.Cu
# (x 87.6 up the east side, y 20.9 west to the FUSED column by C14)
FU = "/AMIGA_IO5V"; YB = 47.5
for r in ("R61", "R62", "R65", "R67"): q = pad(r, 2); track(FU, [q, (q[0], YB)], w=0.3); via(FU, q[0], YB)
q = pad("R63", 2); track(FU, [q, (q[0], 47.15), (75.2, YB)], w=0.3); q = pad("R64", 2); track(FU, [q, (q[0], 47.88), (75.2, YB)], w=0.3); via(FU, 75.2, YB)
q = pad("R66", 2); track(FU, [q, (q[0], 47.87), (80.1, YB)], w=0.3); via(FU, 80.1, YB)
track(FU, [(pad("R61", 2)[0], YB), (87.6, YB)], "B", w=0.4); via(FU, 87.6, YB, 0.6, 0.3)
track(FU, [(87.6, YB), (87.6, 21.3), (87.2, 20.9), (75.9, 20.9)], w=0.4)
save("block 3b-2")

# ================= block 3b-3: USB - ws_drv lane, USB-A and USB-C pairs, east corner, SYS5V trunk, LED, USB-A power =================
reload_pads()
# PIO-USB pair: GPIO26 (S40) / GPIO27 (S41) on B.Cu lanes 47.7 (dp, round the south of the regulator group) and 44.4 (dm) to the 27R at U2
u2 = {n: pad("U2", n) for n in range(1, 7)}; r70 = {k: pad("R70", k) for k in (1, 2)}; r71 = {k: pad("R71", k) for k in (1, 2)}
# pio_dp: B.Cu lane to x 46.2, an In2 hop under U2 across the USBA_5V feed to J4 (B.Cu) and the pass-throughs (F.Cu), B.Cu again to R70
e = S["40"]; track("/pio_dp", [e, (e[0], 47.3), (e[0] + 0.4, 47.7), (46.2, 47.7)], "B")
jumper("/pio_dp", [(46.2, 47.7), (50.4, 47.7), (51.6, 48.9)]); track("/pio_dp", [(51.6, 48.9), (54.3, 48.9), (55.1, 48.1)], "B"); via("/pio_dp", 55.1, 48.1)
track("/pio_dp", [(55.1, 48.1), (54.85, 47.85), r70[1]]); r72 = {k: pad("R72", k) for k in (1, 2)}; r73 = {k: pad("R73", k) for k in (1, 2)}
track("/pio_dp", [(55.1, 48.1), (55.35, 47.85), r72[1]]); gnd_stub(r72[2], 0.75, 0)                     # 15k host pull-downs, R72 fed from the via
track("/pio_dm", [r71[1], (r71[1][0], r73[1][1] - 0.75 - (r71[1][0] - r73[1][0])), (r73[1][0], r73[1][1] - 0.75), r73[1]]); gnd_stub(r73[2], -0.75, 0)
track("/usba_dp", [r70[2], (r70[2][0], u2[4][1] + (r70[2][0] - u2[4][0])), u2[4]])
e = S["41"]; track("/pio_dm", [e, (e[0], 44.1), (e[0] + 0.3, 44.4), (46.2, 44.4), (46.5, 44.1), (46.5, 43.8)], "B"); via("/pio_dm", 46.5, 43.8)
track("/pio_dm", [(46.5, 43.8), r71[1]]); track("/usba_dm", [r71[2], (r71[2][0], 44.42 - (u2[6][0] - r71[2][0])), (u2[6][0], 44.42), u2[6]])
track("/usba_dp", [u2[4], u2[3]]); track("/usba_dm", [u2[6], u2[1]])             # USBLC6 pass-throughs
p = pad("J4", 3); track("/usba_dp", [u2[3], (u2[3][0], p[1] - (p[0] - u2[3][0])), p])
p = pad("J4", 2); track("/usba_dm", [u2[1], (u2[1][0], p[1] - (p[0] - u2[1][0])), p])
track("GND", [u2[2], (u2[2][0], 48.55), (u2[2][0] + 0.3, 48.85)]); via("GND", u2[2][0] + 0.3, 48.85)   # clear of the VBUS corner
# native pair: the 27R at R57 / R58 -> vias south of the band -> B.Cu lanes 42.1 / 42.6 east -> vias under U4's south pins
r57 = pad("R57", 2); r58 = pad("R58", 2); u4 = {n: pad("U4", n) for n in range(1, 7)}
track("/usbc_dp", [r57, (66.2, r57[1]), (67.5, r57[1] + 1.3), (67.5, 41.9)]); via("/usbc_dp", 67.5, 41.9)
vdp = (u4[4][0], 39.4); track("/usbc_dp", [(67.5, 41.9), (67.7, 42.1), (vdp[0] - 2.7, 42.1), vdp], "B"); via("/usbc_dp", *vdp)
track("/usbc_dp", [vdp, u4[4]]); track("/usbc_dp", [u4[4], u4[3]])
track("/usbc_dm", [r58, (65.65, r58[1]), (66.9, r58[1] + 1.25)]); via("/usbc_dm", 66.9, r58[1] + 1.25)
vdm = (u4[6][0], 39.4); track("/usbc_dm", [(66.9, r58[1] + 1.25), (67.2, 42.6), (vdm[0] - 3.2, 42.6), vdm], "B"); via("/usbc_dm", *vdm)
track("/usbc_dm", [vdm, u4[6]]); track("/usbc_dm", [u4[6], u4[1]])
# ws_drv: GPIO2's lane 10.6 -> In2 hop under the BOOT line -> B.Cu south on x 91.9 -> via -> Q1 gate
q1 = pad("Q1", 1); vw = (92.0, 39.7)
jumper("/ws_drv", [(91.9, 10.6), (91.9, 13.5)]); track("/ws_drv", [(91.9, 13.5), (91.9, 39.6), vw], "B"); via("/ws_drv", *vw)
track("/ws_drv", [vw, (q1[0] - (q1[1] - vw[1]), vw[1]), q1])
# east corner: U4, USB-C receptacle J13, AUX_5V, CC resistors, shield grounds
J = {}
for fp in b.GetFootprints():
    if fp.GetReference() == "J13":
        for pd in fp.Pads(): J.setdefault(pd.GetNumber(), (pd.GetPosition().x / 1e6, pd.GetPosition().y / 1e6))
xw = J["A6"][0] - 0.575; xe = J["A6"][0] + 0.575
g = u4[2]; track("GND", [g, (g[0], 34.85)]); via("GND", g[0], 34.85)
a5 = u4[5]; via("/AUX_5V", a5[0], 37.1); track("/AUX_5V", [a5, (a5[0], 37.1)], w=0.3)
track("/AUX_5V", [(a5[0], 37.1), (a5[0] - 0.7, 37.8), (88.4, 37.8), (87.6, 37.0), (87.6, 29.7), (88.0, 29.3), (89.4, 29.3), (90.0, 28.7), (90.0, 28.2)], "B", w=0.3); via("/AUX_5V", 90.0, 28.2)
track("/AUX_5V", [(90.0, 28.2), (90.4, J["B4"][1]), J["B4"]], w=0.3)
track("/AUX_5V", [J["A4"], (xe, J["A4"][1]), (xe + 0.23, J["A4"][1] - 0.23), (95.6, J["A4"][1] - 0.23), (95.6, J["A9"][1] + 0.23), (xe + 0.23, J["A9"][1] + 0.23), (xe, J["A9"][1]), J["A9"]], w=0.3)
d2b = pad("D2", 2); track("/AUX_5V", [d2b, (91.6, d2b[1]), (92.2, d2b[1] + 0.6), (92.2, 28.0), (92.5, 28.3), (92.8, J["B4"][1])], w=0.5)
track("/usbc_dp", [u4[3], (u4[3][0], 31.3), (u4[3][0] + 0.3, 31.0), (91.6, 31.0), (91.6 + (31.0 - J["B6"][1]), J["B6"][1]), J["B6"]])
track("/usbc_dp", [J["B6"], (xe + 0.3, J["B6"][1]), (xe + 0.3, J["A6"][1]), J["A6"]])
dv = (90.4, 31.7); track("/usbc_dm", [u4[1], (u4[1][0], 34.85)]); via("/usbc_dm", u4[1][0], 34.85)
track("/usbc_dm", [(u4[1][0], 34.85), (dv[0], 34.05), dv], "B"); via("/usbc_dm", *dv)
track("/usbc_dm", [dv, (dv[0] + 0.05, J["B7"][1]), J["B7"]]); track("/usbc_dm", [(xw - 0.3, J["B7"][1]), (xw - 0.3, J["A7"][1]), J["A7"]])
r43 = pad("R43", 1); r43g = pad("R43", 2); r44 = pad("R44", 1); r44g = pad("R44", 2)
track("/usb_cc1", [r43, (r43[0], 32.55), (r43[0] + 0.3, J["A5"][1]), J["A5"]]); track("GND", [r43g, (r43g[0] - 0.4, 33.4), (r43g[0] - 0.75, 33.4)]); via("GND", r43g[0] - 0.75, 33.4)
track("/usb_cc2", [r44, (r44[0] + 0.3, r44[1]), (r44[0] + 0.55, J["B5"][1]), J["B5"]]); track("GND", [r44g, (r44g[0], 30.4)]); via("GND", r44g[0], 30.4)
for a, sh in (("A1", (94.22, 35.32)), ("A12", (94.22, 26.68))):          # B12 / B1 sit on the same spots
    p = J[a]; track("GND", [p, (p[0], p[1] + (0.4 if sh[1] > p[1] else -0.4)), sh])
# SYS5V trunk: via (92.7, 16) -> B.Cu south on x 92.7 -> branch via at D3 pad 2 -> on to the south edge, west on y 57.6 (between the ws_drv
# and run_sw lanes), via at x 61.6 and an F.Cu riser up to F2
d3s = pad("D3", 2); f2s = pad("F2", 1)
track("/SYS5V", [(92.7, 16.0), (92.7, 57.2), (92.3, 57.6), (62.0, 57.6), (61.6, 57.2), (61.6, 56.9)], "B", w=0.5)
via("/SYS5V", 92.7, d3s[1], 0.6, 0.3); track("/SYS5V", [(92.7, d3s[1]), d3s], w=0.5)
via("/SYS5V", 61.6, 56.9, 0.6, 0.3); track("/SYS5V", [(61.6, 56.9), (61.6, f2s[1])], w=0.5)                  # F.Cu riser between the two shields
# LED corner: D3 cathode -> y 43.4 east -> x 96.5 up -> C12 -> round the east side -> R49 -> LED pin 4; Q1 drain -> LED pin 3 -> R49
d3l = pad("D3", 1); q = {n: pad("Q1", n) for n in (1, 2, 3)}; l = {n: pad("LED1", n) for n in (2, 3, 4)}; r49 = pad("R49", 1); r49b = pad("R49", 2); c12 = pad("C12", 1); c12g = pad("C12", 2)
track("/LED_VDD", [d3l, (d3l[0] + 1.0, d3l[1]), (d3l[0] + 1.7, d3l[1] - 0.7), (d3l[0] + 1.7, 43.4), (96.1, 43.4), (c12[0], 43.4 - (c12[0] - 96.1)), c12])
track("/LED_VDD", [c12, (98.3, c12[1] - 1.0), (98.3, r49[1] + 0.4), (97.9, r49[1]), r49]); track("/LED_VDD", [r49, (l[4][0], r49[1]), l[4]])
track("GND", [c12g, (96.45, c12g[1])]); via("GND", 96.45, c12g[1])
track("/ws_din", [q[3], (q[3][0], l[3][1] + 0.28), l[3]]); track("/ws_din", [l[3], (r49b[0] - 0.5, l[3][1]), (r49b[0] - 0.2, r49b[1]), r49b])
track("GND", [q[2], (93.9, q[2][1] + 0.65)]); via("GND", 93.9, q[2][1] + 0.65)
track("GND", [l[2], (l[2][0], l[2][1] + 0.95)]); via("GND", l[2][0], l[2][1] + 0.95)
# USB-A power: F2 top pad -> via -> B.Cu -> via below C4 -> C4 pad 1; C5 from F2 on F.Cu; U2 pin 5 and J4 pin 1 on F.Cu between U2's rows
f2u = pad("F2", 2); c4 = pad("C4", 1); c4g = pad("C4", 2); c5 = pad("C5", 1); c5g = pad("C5", 2); j4 = pad("J4", 1)
via("/USBA_5V", f2u[0], 45.1, 0.6, 0.3); track("/USBA_5V", [f2u, (f2u[0], 45.1)], w=0.5)
track("/USBA_5V", [(f2u[0], 45.1), (f2u[0] - 0.8, 45.9), (c4[0] + 0.2, 45.9)], "B", w=0.5); via("/USBA_5V", c4[0] + 0.2, 45.9, 0.6, 0.3)
track("/USBA_5V", [(c4[0] + 0.2, 45.9), (c4[0] + 0.2, c4[1])], w=0.5)
track("/USBA_5V", [(f2u[0] + 0.5, f2u[1]), (64.4, f2u[1]), (64.4 + (f2u[1] - c5[1] - 0.2), c5[1] + 0.2), c5], w=0.4)
# U2 VBUS from a via between its rows; J4 pin 1 on B.Cu round the west of U2 (the pass-throughs own the F.Cu between the rows)
track("/USBA_5V", [(c4[0] + 0.2, 45.9), (c4[0] - 0.3, 46.4), (u2[5][0] + 0.2, 46.4), (u2[5][0], 46.6), (u2[5][0], 47.9), (u2[5][0] - 0.5, 48.4), (47.6, 48.4), (47.3, 48.7), (47.3, j4[1] - (47.3 - j4[0])), j4], "B", w=0.5)
via("/USBA_5V", u2[5][0], 46.6, 0.5); track("/USBA_5V", [(u2[5][0], 46.6), u2[5]], w=0.3)   # 0.5: all that fits between U2's rows
track("GND", [c4g, (c4g[0], 45.3)]); via("GND", c4g[0], 45.3); track("GND", [c5g, (c5g[0], c5g[1] + 0.8)]); via("GND", c5g[0], c5g[1] + 0.8)
# run_sw: R53's via -> In2 south-east under the band vias and the S staircase -> via east of the port-1 clamps -> B.Cu
# south on x 32 -> along the south edge on y 59.0 -> north on x 93.3 (between the trunk and SW2's posts) -> SW2 pin 1
rs = pad("R53", 2); v0 = (rs[0], rs[1] + 0.75); sw2 = pad("SW2", 1)
track("/run_sw", [v0, (v0[0], 43.2), (v0[0] + 0.4, 43.6), (27.2, 43.6), (31.2, 47.6), (32.0, 47.6)], "I2"); via("/run_sw", 32.0, 47.6)
track("/run_sw", [(32.0, 47.6), (32.0, 58.6), (32.4, 59.0), (92.9, 59.0), (93.3, 58.6), (93.3, 46.6), (93.6, 46.3), (sw2[0] - 0.3, 46.3), sw2], "B")
# pads the top pour cannot give two spokes keep their explicit copper and no pour connection
for ref, num in (("C34", "2"), ("J13", "A1"), ("J13", "B12"), ("U1", "47"), ("C20", "2")): b.FindFootprintByReference(ref).FindPadByNumber(num).SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_NONE)
save("block 3b-3")

# ================= block 3c: ground - RM2 ground pads, a stitching grid for the pours =================
reload_pads()
for n, dx, dy in ((4, 0, 1.35), (7, 1.0, 0), (11, 1.5, 0)): gnd_stub(pad("U9", n), dx, dy)   # pins 1 / 21 sit on the port-1 bus corridor: pour only
via("GND", 7.2, 24.6)                                                               # pour tie north of the module
import math
def seg_dist(p, a, c):
    ax, ay = a; cx, cy = c; px, py = p; dx, dy = cx - ax, cy - ay; l2 = dx * dx + dy * dy
    t = 0 if l2 == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))
def stitch_ok(x, y):
    if x < 6.5 and 18.0 < y < 56.0: return False                                  # RM2 keep-out and its margin
    for t in b.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA):
            if math.dist((x, y), (t.GetPosition().x / 1e6, t.GetPosition().y / 1e6)) < 1.2: return False
        elif t.GetNetname() != "GND":
            if seg_dist((x, y), (t.GetStart().x / 1e6, t.GetStart().y / 1e6), (t.GetEnd().x / 1e6, t.GetEnd().y / 1e6)) < 0.9: return False
    for fp in b.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            if bb.GetLeft() / 1e6 - 0.7 < x < bb.GetRight() / 1e6 + 0.7 and bb.GetTop() / 1e6 - 0.7 < y < bb.GetBottom() / 1e6 + 0.7: return False
        if fp.GetReference().startswith(("U1", "U9", "H")):                        # not under the MCU, the module or the holes
            bb = fp.GetBoundingBox(False, False)
            if bb.GetLeft() / 1e6 - 1.0 < x < bb.GetRight() / 1e6 + 1.0 and bb.GetTop() / 1e6 - 1.0 < y < bb.GetBottom() / 1e6 + 1.0: return False
    return True
n = 0
for gx in range(4, 98, 8):
    for gy in range(4, 60, 8):
        if stitch_ok(gx, gy): via("GND", gx, gy); n += 1
print("stitching vias:", n)
save("block 3c")

# ================= block 4: I/O expander U10, encoder header J7, VBUS sense =================
reload_pads()
u10 = {n: pad("U10", n) for n in range(1, 17)}; j7 = {k: pad("J7", k) for k in ("1", "2", "3", "4", "5", "6")}
track("/exp_int", [(88.3, 10.1), (88.3, u10[13][1]), u10[13]])
track("/i2c_scl", [(74.25, 11.1), (87.6, 11.1)], "B"); via("/i2c_scl", 87.6, 11.1); track("/i2c_scl", [(87.6, 11.1), (87.6, u10[14][1]), u10[14]])
track("/i2c_sda", [(j3["4"][0] - 0.4, 11.6), (88.0, 11.6), (88.3, 11.9), (88.3, 12.1)], "B"); via("/i2c_sda", 88.3, 12.1); track("/i2c_sda", [(88.3, 12.1), (88.3, u10[15][1]), u10[15]])
via("/+3V3", 88.8, 13.4); track("/+3V3", [u10[16], (u10[16][0], 13.14), (u10[16][0] - 0.26, 13.4), (88.8, 13.4)])
c39 = pad("C39", 1); track("/+3V3", [(88.8, 13.4), (c39[0], 13.4 + (c39[0] - 88.8)), c39]); gnd_stub(pad("C39", 2), 0.72, 0)
track("GND", [u10[3], u10[1], (u10[1][0] + 0.22, 13.1), (96.5, 13.1)]); via("GND", 96.5, 13.1); gnd_stub(u10[8], 1.1, 0)
# the four encoder lines: east of the chip, up the corner, back west along the north edge into the header
for nm, n, p, lane in (("/enc_a", 4, "5", 0.9), ("/enc_b", 5, "3", 1.25), ("/enc_sw", 6, "1", 1.6)):
    xc = 99.1 - 0.35 * (n - 4); y = u10[n][1]; j = j7[p]
    track(nm, [u10[n], (xc - 0.93, y), (xc, y - 0.93), (xc, lane + 0.93), (xc - 0.93, lane), (j[0] + 0.93, lane), (j[0], lane + 0.93), j])
j = j7["2"]; track("/enc_sp", [u10[7], (97.12, u10[7][1]), (98.05, 8.04), (98.05, 7.23), (97.12, 6.3), (j[0] + 0.36, 6.3), j])
gnd_stub(j7["4"], 0, 1.26); via("/+3V3", j7["6"][0], 7.2); track("/+3V3", [j7["6"], (j7["6"][0], 7.2)])
# VBUS sense: divider under the switch, the sense line on In2 up the east strip, in from the right into P4
r68 = {k: pad("R68", k) for k in (1, 2)}; r69 = {k: pad("R69", k) for k in (1, 2)}
track("/AUX_5V", [(92.2, r68[1][1]), r68[1]], w=0.3)
track("/vbus_sense", [r68[2], r69[1]]); via("/vbus_sense", 95.85, 24.9); track("/vbus_sense", [(95.85, r68[2][1]), (95.85, 24.9)])
track("/vbus_sense", [(95.85, 24.9), (94.25, 23.3), (93.75, 23.3), (93.35, 22.9), (93.35, 9.6)], "I2"); via("/vbus_sense", 93.35, 9.6)
track("/vbus_sense", [(93.35, 9.6), (93.35, 9.35), (92.32, u10[9][1]), u10[9]])
gnd_stub(r69[2], -0.7, 0)
# ---- I/O rail OR diodes at the DE9s (2026-10-16): DE9 pin 7 -> B.Cu between the pin rows -> diode -> the rail
d5 = {k: pad("D5", k) for k in (1, 2)}; d6 = {k: pad("D6", k) for k in (1, 2)}; p1_7 = pad("P1", 7); p2_7 = pad("P2", 7)
track("/cont1_5v", [p1_7, (p1_7[0], 55.2), (p1_7[0] - 0.7, 55.9), (17.0, 55.9), (16.3, 55.2), (16.3, 50.5), (15.8, 50.0), (13.6, 50.0), (d5[2][0], 50.0 - (13.6 - d5[2][0])), d5[2]], w=0.4)   # F.Cu round the south of P1's back row, up its west end, along the north edge into D5
track("/AMIGA_IO5V", [d5[1], (11.3, d5[1][1])], w=0.4); via("/AMIGA_IO5V", 11.3, d5[1][1], 0.6, 0.3)
track("/AMIGA_IO5V", [(11.3, d5[1][1]), (11.3, 32.1), (11.7, 31.7), (16.2, 31.7), (16.6, 31.3), (16.6, YF)], "B", w=0.4)   # north under the RM2, east below its north pads, up to the pull-up bus
via("/AMIGA_IO5V", 16.6, YF, 0.6, 0.3); track("/AMIGA_IO5V", [(max(pad("RN2", k)[0] for k in range(5, 9)), YF), (16.6, YF)], w=0.4)
track("/cont2_5v", [p2_7, (p2_7[0], 52.88), (84.9, 52.88), (86.2, 51.58), (86.2, 48.3)], "B", w=0.4); via("/cont2_5v", 86.2, 48.3, 0.6, 0.3)
track("/cont2_5v", [(86.2, 48.3), d6[2]], w=0.4)
track("/AMIGA_IO5V", [d6[1], (87.6, d6[1][1])], w=0.4)                                                            # D6 cathode onto the port-2 feed column
save("block 4")
