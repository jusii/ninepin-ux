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
def via(netname, x, y, size=0.5, drill=0.3):
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
                                     "57": CX + 9.2, "56": CX + 9.2, "55": CX + 9.2})
W = comb("W", 1.0, CX - 10.2, stop={"20": CX - 8.0, "23": CX - 7.6, "22": pad("R54", 1)[0], "26": pad("R53", 1)[0],
                                     "30": CX - 8.55, "21": pad("Y1", 1)[0], "27": CX - 8.7, "28": CX - 8.7, "29": CX - 8.7})
# south comb at 0.6 mm pitch so it ends by y 43.3 and leaves room for the port-1 clamp rows; the vias
# on its columns sit on a 45-degree line (31..34, feeding the band) or alternate 43.3 / 43.9
S = comb("S", 0.6, 43.3, stop={"39": 43.21, "35": 44.5, "37": 45.5, "38": 43.6, "40": 43.9, "41": 43.3, "42": 43.9, "43": 45.65,
                               "31": 43.3, "32": 43.9, "33": 44.5, "34": 45.1})
via("/+3V3", *S["38"]); via("/dvdd", *S["39"])

for x in (CX - 0.8, CX + 0.8):
    for y in (CY - 1.1, CY - 0.1): via("GND", x, y)

# ---- north: IOVDD 1 / DVDD 6 / IOVDD 11 to the planes on their columns; uart/i2c (2..5) and the DVDD
# column hop through the lane wall on In2 (the F.Cu lanes to U6 and the B.Cu lanes to U7 both run here)
for num in ("1", "6", "11"):
    x, ye = N[num]; via(padnet("U1", num), x, ye)
for num in ("2", "3", "4", "5"):
    x, ye = N[num]; jumper(padnet("U1", num), [(x, ye), (x, 19.5)])      # end between F1's pads
n6 = N["6"]
# ---- east: QSPI ----
# BOOT: qspi_ss leaves the flash pad on F.Cu, hops through the wall on In2 at x 45.4 and runs east on B.Cu
p1 = pad("U8", 1); r1 = pad("R52", 1); r2 = pad("R52", 2); sw = pad("SW1", 1)
track("/qspi_ss", [p1, (p1[0] + 1.5, p1[1]), (p1[0] + 2.0, p1[1] - 0.5)]); XQ = p1[0] + 2.0
jumper("/qspi_ss", [(XQ, p1[1] - 0.5), (XQ, 24.0)])
track("/qspi_ss", [(XQ, 24.0), (XQ + 0.3, 23.7), (76.7, 23.7), (77.0, 23.4), (77.0, 18.7), (77.4, 18.3), (r1[0] - 0.4, 18.3), (r1[0], 17.9), (r1[0], 15.9)], "B")
via("/qspi_ss", r1[0], 15.9); track("/qspi_ss", [(r1[0], 15.9), r1])
track("/qspi_ss_sw", [r2, (r2[0], 12.3), (r2[0] + 0.4, 11.9), (sw[0] - 0.4, 11.9), (sw[0], 12.3), sw])
for num, netn, pnum, xv in (("57", "/qspi_sd0", 5, 52.6), ("56", "/qspi_sclk", 6, 53.6), ("55", "/qspi_sd3", 7, 54.4)):
    e = E[num]; via(netn, *e); pp = pad("U8", pnum)
    track(netn, [e, (xv, e[1]), (xv, pp[1])], "B"); via(netn, xv, pp[1]); track(netn, [(xv, pp[1]), pp])
p4 = pad("U8", 4); track("GND", [(p4[0] + 0.6, p4[1]), (p4[0] + 1.2, p4[1] + 0.4)]); via("GND", p4[0] + 1.2, p4[1] + 0.4)
p8 = pad("U8", 8); track("/+3V3", [(p8[0] + 0.7, p8[1]), (p8[0] + 1.4, p8[1] - 0.4)]); via("/+3V3", p8[0] + 1.4, p8[1] - 0.4)
c1 = pad("C32", 1); via("/+3V3", c1[0], c1[1] - 1.3); track("/+3V3", [c1, (c1[0], c1[1] - 1.3)]); gnd_stub(pad("C32", 2), 0, -1.3)
c30 = pad("C30", 1); route("/+3V3", E["53"], c30, "diag"); route("/+3V3", E["54"], c30, "diag"); via("/+3V3", CX + 9.4, E["53"][1]); gnd_stub(pad("C30", 2), 1.2, 0)
for num, r, xt in (("52", "R57", 60.2), ("51", "R58", 59.4)):                  # usb pair: staggered turns clear of C5
    e = E[num]; q1 = pad(r, 1); track(padnet("U1", num), [e, (xt, e[1]), (xt + (q1[1] - e[1]), q1[1]), q1])
route("/+3V3", E["49"], pad("C29", 1), "diag"); via("/+3V3", CX + 9.4, E["49"][1]); gnd_stub(pad("C29", 2), 1.2, 0)
route("/vreg_lx", E["48"], pad("L1", 2), "diag")
via("GND", *E["47"])
r2 = pad("R59", 2); route("/vreg_avdd", E["46"], r2, "straight"); c37 = pad("C37", 1)
track("/vreg_avdd", [r2, (r2[0], 46.0), (r2[0] - 0.6, 46.6), c37])
r1 = pad("R59", 1); via("/+3V3", r1[0], r1[1] + 1.1); track("/+3V3", [r1, (r1[0], r1[1] + 1.1)])
gnd_stub(pad("C37", 2), 0, -0.9)
l1 = pad("L1", 1); c38 = pad("C38", 1); track("/dvdd", [l1, c38]); via("/dvdd", c38[0] + 0.1, c38[1] + 1.2); track("/dvdd", [c38, (c38[0] + 0.1, c38[1] + 1.2)]); gnd_stub(pad("C38", 2), 0, -1.2)
c22 = pad("C22", 1); track("/dvdd", [E["50"], (c22[0] - 0.3, E["50"][1]), c22])
g = pad("C22", 2); track("GND", [g, (g[0], 41.7), (g[0] + 0.5, 42.2)]); via("GND", g[0] + 0.5, 42.2)
track("/dvdd", [c38, (c22[0] + 0.3, c38[1] - (c22[0] + 0.3 - c38[0])), (c22[0] + 0.3, c22[1] + 0.3), c22])
# ---- west ----
via("/+3V3", CX - 7.7, W["20"][1]); e = W["30"]; track("/+3V3", [e, (e[0], e[1] + 0.5)]); via("/+3V3", e[0], e[1] + 0.5)
via("/dvdd", *W["23"])
y1 = {n: pad("Y1", n) for n in (1, 2, 3, 4)}
c33 = pad("C33", 1)                                                               # xin: pad 1 -> north to C33 pad 1 (east pad)
track("/xin", [(y1[1][0], y1[1][1] - 0.55), (y1[1][0], c33[1] + 0.5), (y1[1][0] - 0.5, c33[1]), c33])
gnd_stub(pad("C33", 2), -0.9, 0.0)
r2 = pad("R54", 2); yu = y1[4][1] + 1.05; c34 = pad("C34", 1); xw = c34[0]      # xout_r: under the crystal to pad 3 and C34
track("/xout_r", [r2, (r2[0], r2[1] + 0.6), (r2[0] - (yu - r2[1] - 0.6), yu), (xw, yu), (xw, y1[3][1]), y1[3]])
track("/xout_r", [(xw, yu), c34]); g = pad("C34", 2); track("GND", [g, (g[0] + 0.23, g[1] + 0.23), (19.3, g[1] + 0.23)]); via("GND", 19.3, g[1] + 0.23)
g = y1[2]; track("GND", [g, (g[0], g[1] - 0.35), (g[0] - 0.5, g[1] - 0.85)]); via("GND", g[0] - 0.5, g[1] - 0.85); gnd_stub(y1[4], 1.2, 0.0)
for num in ("24", "25", "18", "19"):                                             # swd and kbin go to B.Cu at the row ends
    e = W[num]; via(padnet("U1", num), e[0] - 0.4, e[1]); track(padnet("U1", num), [e, (e[0] - 0.4, e[1])])
for num in ("27", "28", "29"): via(padnet("U1", num), *W[num])
r2 = pad("R53", 2); via("/run_sw", r2[0] + 0.2, r2[1])
# ---- south ----
e = S["44"]; r1 = pad("R50", 1); r2 = pad("R50", 2); c31 = pad("C31", 1)       # adc RC: column 44 -> R50 (vertical) pad 2 -> C31 pad 1
track("/adc_avdd", [e, (e[0], r2[1] - (r2[0] - e[0])), r2])
track("/adc_avdd", [r2, (r2[0] + 0.3, r2[1] + 0.3), (c31[0] - 0.5, r2[1] + 0.3), (c31[0], c31[1])])
via("/+3V3", r1[0], r1[1] + 1.1); track("/+3V3", [r1, (r1[0], r1[1] + 1.1)]); gnd_stub(pad("C31", 2), 0, 1.3)
e = S["45"]; p1 = pad("C28", 1); track("/+3V3", [e, (e[0], p1[1] - 0.23), (e[0] + 0.23, p1[1]), p1]); via("/+3V3", p1[0] - 0.9, p1[1]); gnd_stub(pad("C28", 2), 0, 1.0)
c20 = pad("C20", 1); c21 = pad("C21", 1)                                        # DVDD caps C20 / C21 by the W side, fed from the B.Cu web
via("/dvdd", c20[0], c20[1] + 1.5); track("/dvdd", [c20, (c20[0], c20[1] + 1.5)]); gnd_stub(pad("C20", 2), 0, 1.3)
track("/dvdd", [(26.6, 29.4), (23.6, 29.4), (23.2, 29.0), (23.2, c20[1] + 1.5 + 0.33), (c20[0], c20[1] + 1.5)], "B")
track("/dvdd", [c20, (c20[0], c21[1] + 1.3), (c21[0], c21[1] + 1.1), c21]); gnd_stub(pad("C21", 2), 0, -1.2)
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
for i, num in enumerate(("27", "28", "29")):
    e = W[num]; yl = BAND[i]; n = padnet("U1", num)
    track(n, [e, (e[0] + (e[1] - yl), yl), (74.0, yl)], "B")
for j, num in enumerate(("31", "32", "33", "34")):                              # vias on a 45-degree line; north first, then a 1.5 mm diagonal
    e = S[num]; yl = BAND[3 + j]; n = padnet("U1", num)
    track(n, [e, (e[0], yl + 1.5), (e[0] + 1.5, yl), (74.0, yl)], "B")
save("block 1")

# ================= block 2: U6 at the west edge - MCU lanes, port 1 down the west edge, keyboard trio =================
reload_pads()
U6B = {n: pad("U6", n) for n in range(13, 25)}                    # B row (north, y 19.4): pin 13 east .. 24 west
LANE = {13: 18.0, 14: 17.6, 15: 17.2, 16: 16.8, 17: 16.4, 18: 16.0, 19: 15.6, 20: 15.2, 21: 14.8, 22: 14.4}   # lanes nearer the row end further east
def tvs(netname):
    for fp in b.GetFootprints():
        if fp.GetValue() in ("PESD5V0S1BA", "PESD5V0S1UL") and padnet(fp.GetReference(), 1) == netname: return fp.GetReference()
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
    n = padnet("U6", a); px, py = pad("U6", a); vy = 24.3 if i % 2 == 0 else 25.0
    yl = 25.6 + 0.45 * i; xv = 5.6 + 0.6 * i; ye = END[n]; pp = P1X[PIN[n]]
    via(n, px, vy)
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
    g = pad(tvs(n), 2); gx = GV[n]; track("GND", [g, (gx, g[1])])
    if (gx, g[1]) not in seen: via("GND", gx, g[1]); seen.add((gx, g[1]))
# pull-up arrays under the A row: short stubs from the pads, commons on an F.Cu bus at y 27.9
for rn in ("RN1", "RN2"):
    for k in range(1, 5):
        n = padnet(rn, k)
        if n == "": continue
        e = pad(rn, k); a = [p for p in range(5, 12) if padnet("U6", p) == n][0]; pa = pad("U6", a)
        d = e[0] - pa[0]; track(n, [(pa[0], pa[1] + 0.75), (pa[0], e[1] - 0.6 - abs(d)), (e[0], e[1] - 0.6), e])
c13 = pad("C13", 1); v24 = U6B[24]
track("/AMIGA_5V_FUSED", [(8.1, 27.9), (16.6, 27.9)], w=0.4)
for rn in ("RN1", "RN2"):
    for k in range(5, 9):
        e = pad(rn, k); track("/AMIGA_5V_FUSED", [e, (e[0], 27.9)])
track("/AMIGA_5V_FUSED", [c13, (c13[0], 20.6), (c13[0] + 1.2, 19.4), v24], w=0.4)
track("/AMIGA_5V_FUSED", [c13, (c13[0] - 0.9, c13[1] + 0.9), (c13[0] - 0.9, 27.0), (c13[0], 27.9), (8.1, 27.9)], w=0.4)
g = pad("C13", 2); track("GND", [g, (12.1, g[1])]); via("GND", 12.1, g[1])
# keyboard trio: in-pad vias on A8..A10, B.Cu north under U6, east on lanes 11.3 / 11.8 / 12.3 to J10, RN3, then
# through the uart/i2c wall on F.Cu (lanes 10.8 / 11.8 / 12.3) to J1 and the clamps
# kbclock: B.Cu lane 11.3 to a via at J10 pin 5, then F.Cu: up into pin 5, and east at y 10.3 past RN3 pin 3 to J1.
# kbdata: B.Cu lane 11.8 to J1's wall via; B.Cu stub up into J10 pin 1 (THT); RN3 tap from pin 1 on F.Cu.
# kbreset: B.Cu lane 12.3; RN3 tap via beside the array; F.Cu through the wall at 12.3.
for n, a, yl, xe in (("kbreset", 2, 11.3, 34.3), ("kbdata", 3, 11.8, 34.8), ("kbclock", 4, 12.3, 30.0)):
    px, py = pad("U6", a); vy = 25.0 if a != 3 else 24.3; net_ = "/" + n
    via(net_, px, vy); track(net_, [(px, vy), (px, yl + 0.4), (px + 0.4, yl), (xe, yl)], "B")
rn3 = {padnet("RN3", k): pad("RN3", k) for k in range(1, 4)}
j10_5 = pad("J10", 5); j10_1 = pad("J10", 1); j1 = {k: pad("J1", k) for k in ("1", "2", "3")}
# kbclock: via at J10 pin 5 (F.Cu up into the pin); via at x 30.0, F.Cu lane y 10.3 past RN3 pin 3 to J1 pin 1
via("/kbclock", 26.6, 12.3); track("/kbclock", [(26.6, 12.3), j10_5])
e = rn3["/kbclock"]; via("/kbclock", 30.0, 12.3)
track("/kbclock", [(30.0, 12.3), (30.0, 10.7), (30.4, 10.3), (e[0], 10.3), e]); track("/kbclock", [(e[0], 10.3), (j1["1"][0], 10.3), j1["1"]])
# kbdata: via at J10 pin 1 (F.Cu up into the pin), via on the lane by RN3 pin 2, wall via at 34.8 -> F.Cu lane 11.8 to J1 pin 2
via("/kbdata", 28.7, 11.8); track("/kbdata", [(28.7, 11.8), j10_1])
e = rn3["/kbdata"]; via("/kbdata", e[0], 11.8); track("/kbdata", [(e[0], 11.8), e])
via("/kbdata", 34.8, 11.8); track("/kbdata", [(34.8, 11.8), (j1["2"][0], 11.8), j1["2"]])
# kbreset: wall via at 34.3 feeds RN3 pin 1 to the west and the F.Cu lane y 12.3 to the east
e = rn3["/kbreset"]; via("/kbreset", 34.3, 11.3); track("/kbreset", [(34.3, 11.3), (e[0] + 0.5, 11.3), (e[0] + 0.4, e[1]), e])
track("/kbreset", [(34.3, 11.3), (34.3, 12.2), (34.4, 12.3), (j1["3"][0], 12.3), j1["3"]])
for n, j in (("kbclock", "1"), ("kbdata", "2"), ("kbreset", "3")):                     # clamp stubs off the J1 legs
    pj = j1[j]; t = tvs("/" + n); t1 = pad(t, 1)
    if t1[0] < pj[0]: track("/" + n, [(pj[0], t1[1] + 0.6), (t1[0] + 0.4, t1[1]), t1])
    else:             track("/" + n, [(pj[0], t1[1] + (t1[0] - pj[0])), t1])
    g = pad(t, 2); track("GND", [g, (g[0], g[1] - 0.95)]); via("GND", g[0], g[1] - 0.95)
# FUSED to U6 and RN3: F1 pad 2 west along y 13.9 (north of the lanes), down x 9.4 into pin 24
f1 = pad("F1", 2)
track("/AMIGA_5V_FUSED", [f1, (f1[0], 17.0), (f1[0] - 0.8, 16.2), (36.6, 16.2), (36.6, 14.3), (36.2, 13.9), (9.4, 13.9), (9.0, 14.3), (9.0, 18.9), (v24[0], 18.9 + (v24[0] - 9.0)), v24], w=0.4)
for k in range(5, 9):
    e = pad("RN3", k); track("/AMIGA_5V_FUSED", [e, (e[0], 13.9)])
# SWD: W rows 24/25 -> B.Cu north-west at x 20.3 / 20.8 -> In2 through the lane band and the keyboard lanes -> J2
for u1, xv, yt, yv, j2 in (("24", 20.4, 18.6, 10.6, "1"), ("25", 19.9, 19.1, 9.6, "3")):
    e = W[u1]; n = padnet("U1", u1); v0 = (e[0] - 0.4, e[1]); pj = pad("J2", j2)
    track(n, [v0, (xv, v0[1] - (v0[0] - xv)), (xv, yt)], "B"); jumper(n, [(xv, yt), (xv, yv)])
    if j2 == "3": track(n, [(xv, yv), (xv, pj[1] + (xv - pj[0])), pj])
    else:         track(n, [(xv, yv), (xv - 0.4, yv + 0.4), (pj[0] + 0.5, yv + 0.4), (pj[0], yv - 0.1), pj])
gnd_stub(pad("J2", 2), 0, 1.3)
# wl_vin: C36 by U6 links to the filter at the RM2 on B.Cu; FB1 / C35 / the two module pads on F.Cu
c36 = pad("C36", 1); fb2 = pad("FB1", 2); fb1 = pad("FB1", 1); c35 = pad("C35", 1); u14 = pad("U9", 14); u16 = pad("U9", 16)
XV = c36[0] - 0.5
via("/wl_vin", XV, c36[1] + 0.95); track("/wl_vin", [c36, (XV, c36[1] + 0.95)]); gnd_stub(pad("C36", 2), 0, -0.95)
track("/wl_vin", [(XV, c36[1] + 0.95), (XV, 29.1)], "B"); via("/wl_vin", XV, 29.1)
track("/wl_vin", [(XV, 29.1), (fb2[0] - 0.4, 29.1), fb2])
track("/wl_vin", [fb2, (fb2[0] + 0.6, fb2[1]), (c35[0] - 0.55, fb2[1] + (c35[0] - 0.55 - fb2[0] - 0.6)), (c35[0] - 0.55, c35[1]), c35])
track("/wl_vin", [fb2, (17.6, fb2[1]), (17.3, fb2[1] + 0.3), (17.3, u14[1]), u14])
track("/wl_vin", [(17.3, 29.1), (16.8, 28.6), (u16[0] + 0.5, 28.6), (u16[0], 29.1), u16])
via("/+3V3", 17.9, fb1[1]); track("/+3V3", [fb1, (17.9, fb1[1])])
gnd_stub(pad("C35", 2), 0, -0.9)
save("block 2")
