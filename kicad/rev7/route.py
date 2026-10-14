#!/usr/bin/env python3
"""rev7 routing as code. Run after `make_board.py --force`. Coordinates mm, y down.
Phase 1: RP2350 core — fanned combs on all four sides, decoupling, flash, VREG, USB
resistors, crystal, SWD/RUN, the RM2 data/clock/select lines, port-2 lines under the MCU."""
import os, sys, pcbnew
from pcbnew import VECTOR2I, FromMM as mm
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import placement as PL
PCB = os.path.join(HERE, "amigahid-pico-rev7.kicad_pcb")
b = pcbnew.LoadBoard(PCB)
L = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu}
CX, CY = PL.CX, PL.CY

def P(x, y): return VECTOR2I(mm(x), mm(y))
def net(name):
    n = b.FindNet(name)
    if n is None: raise SystemExit("no net " + name)
    return n
_pads = {}
for fp in b.GetFootprints():
    for pd in fp.Pads():
        _pads[(fp.GetReference(), pd.GetNumber())] = (pd.GetPosition().x / 1e6, pd.GetPosition().y / 1e6, pd.GetNetname())
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

# ---------------- MCU combs ----------------
# pin k (offset from the side centre in pads) goes straight for s = S0 + 0.31*(7-|k|), then
# diagonally outward by (pitch-0.4)*|k| so the tracks end on a comb of the given pitch, then
# straight to `end` (or to a per-pin stop). The 0.31 stagger keeps parallel diagonals 0.5 apart.
S0 = 0.5
def comb(side, pitch, end, stop=None):
    stop = stop or {}; ends = {}
    fp = b.FindFootprintByReference("U1")
    for pd in fp.Pads():
        num = pd.GetNumber(); x, y, n = _pads[("U1", num)]
        if not num or num == "61" or not n: continue
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

N = comb("N", 1.0, CY - 9.2)
E = comb("E", 1.2, CX + 10.5, stop={"60": pad("U8", 1)[0], "59": pad("U8", 2)[0], "58": pad("U8", 3)[0],
                                     "57": CX + 9.2, "56": CX + 9.2, "55": CX + 9.2})
W = comb("W", 1.0, CX - 10.2, stop={"20": CX - 8.0, "23": CX - 7.6, "22": pad("R54", 1)[0], "26": pad("R53", 1)[0],
                                     "30": CX - 8.55, "21": pad("Y1", 1)[0], "27": CX - 8.7, "28": CX - 8.7, "29": CX - 8.7})
S = comb("S", 1.0, CY + 9.0, stop={"38": CY + 7.2, "39": pad("C20", 1)[1], "45": CY + 8.85})

for x in (CX - 0.8, CX + 0.8):
    for y in (CY - 1.1, CY - 0.1): via("GND", x, y)

# ---- north: IOVDD 1 / DVDD 6 / IOVDD 11 caps at the column tops ----
for num, cap, pwr in (("1", "C23", "/+3V3"), ("6", "C21", "/dvdd"), ("11", "C24", "/+3V3")):
    x, ytop = N[num]; p1 = pad(cap, 1); p2 = pad(cap, 2)
    track(pwr, [(x, ytop), p1]); gnd_stub(p2, 0, -1.5); via(pwr, x, ytop + 0.6)
# ---- east: QSPI ----
p1 = pad("U8", 1); route("/qspi_ss", (p1[0], p1[1] - 0.3), (p1[0], 27.0), "straight"); route("/qspi_ss", (p1[0], 27.0), pad("R52", 1), "diag")
for num, netn, pnum, xv in (("57", "/qspi_sd0", 5, 52.6), ("56", "/qspi_sclk", 6, 53.6), ("55", "/qspi_sd3", 7, 54.4)):
    e = E[num]; via(netn, *e); pp = pad("U8", pnum)
    track(netn, [e, (xv, e[1]), (xv, pp[1])], "B"); via(netn, xv, pp[1]); track(netn, [(xv, pp[1]), pp])
p4 = pad("U8", 4); track("GND", [(p4[0] + 0.6, p4[1]), (p4[0] + 1.2, p4[1] + 0.4)]); via("GND", p4[0] + 1.2, p4[1] + 0.4)
p8 = pad("U8", 8); c1 = pad("C32", 1); track("/+3V3", [p8, c1]); via("/+3V3", p8[0] + 1.4, p8[1]); track("/+3V3", [p8, (p8[0] + 1.4, p8[1])]); gnd_stub(pad("C32", 2), 0, -1.2)
c30 = pad("C30", 1); route("/+3V3", E["53"], c30, "diag"); route("/+3V3", E["54"], c30, "diag"); via("/+3V3", CX + 9.6, E["53"][1]); gnd_stub(pad("C30", 2), 1.2, 0)
route("/usb_dp", E["52"], pad("R57", 1), "diag"); route("/usb_dm", E["51"], pad("R58", 1), "diag")
via("/dvdd", *E["50"])
route("/+3V3", E["49"], pad("C29", 1), "diag"); via("/+3V3", CX + 9.6, E["49"][1]); gnd_stub(pad("C29", 2), 1.2, 0)
route("/vreg_lx", E["48"], pad("L1", 2), "diag")
via("GND", *E["47"])
r2 = pad("R59", 2); route("/vreg_avdd", E["46"], r2, "diag"); c37 = pad("C37", 1); track("/vreg_avdd", [r2, (r2[0], c37[1]), c37])
r1 = pad("R59", 1); via("/+3V3", r1[0] + 1.2, r1[1]); track("/+3V3", [r1, (r1[0] + 1.2, r1[1])]); gnd_stub(pad("C37", 2), 1.2, 0)
l1 = pad("L1", 1); c38 = pad("C38", 1); track("/dvdd", [l1, c38]); via("/dvdd", c38[0] + 0.3, c38[1] + 1.2); track("/dvdd", [c38, (c38[0] + 0.3, c38[1] + 1.2)]); gnd_stub(pad("C38", 2), 1.2, 0)
# ---- west ----
via("/+3V3", CX - 7.7, W["20"][1]); via("/+3V3", *W["30"])
for cap in ("C25", "C26", "C27"):               # parked 3V3 caps north of the west comb
    p1 = pad(cap, 1); p2 = pad(cap, 2); via("/+3V3", p1[0], p1[1] + 1.2); track("/+3V3", [p1, (p1[0], p1[1] + 1.2)]); gnd_stub(p2, 0, -1.2)
via("/dvdd", *W["23"])
c22 = pad("C22", 1); track("/dvdd", [E["50"], (c22[0] - 0.3, E["50"][1]), c22]); gnd_stub(pad("C22", 2), 1.2, 0)
y1 = {n: pad("Y1", n) for n in (1, 2, 3, 4)}
# xin: the row runs straight into pad 1; C33 above-left via a short link over pad 2
c33 = pad("C33", 1); track("/xin", [(y1[1][0], y1[1][1] - 0.55), (y1[1][0], c33[1] + 0.55), (c33[0] + 0.05, c33[1])])
gnd_stub(pad("C33", 2), -1.3, 0)
# xout_r: R54 pad 2 -> under the crystal (y = pad bottom + 0.75) -> up into pad 3 -> on to C34
r2 = pad("R54", 2); yu = y1[4][1] + 1.05; c34 = pad("C34", 1); xw = c34[0]
track("/xout_r", [r2, (r2[0], r2[1] + 0.6), (r2[0] - (yu - r2[1] - 0.6), yu), (xw, yu), (xw, y1[3][1]), y1[3]])
track("/xout_r", [(xw, yu), c34]); g = pad("C34", 2); track("GND", [g, (g[0], g[1] + 0.5), (g[0] + 0.55, g[1] + 1.05)]); via("GND", g[0] + 0.55, g[1] + 1.05)
gnd_stub(y1[2], 0.0, -0.85); gnd_stub(y1[4], 1.2, 0.0)
for num in ("24", "25", "16", "17", "18", "19"):
    e = W[num]; via(padnet("U1", num), e[0] - 0.4, e[1]); track(padnet("U1", num), [e, (e[0] - 0.4, e[1])])
for num in ("27", "28", "29"): via(padnet("U1", num), *W[num])
r2 = pad("R53", 2); via("/run_sw", r2[0] + 0.2, r2[1])
# ---- south ----
e = S["38"]; via("/+3V3", *e)                      # IOVDD 38: plane via on the column
e = S["39"]; via("/dvdd", e[0], CY + 7.6); gnd_stub(pad("C20", 2), 1.0, 0)
e = S["44"]; r1 = pad("R50", 1); r2 = pad("R50", 2); c31 = pad("C31", 1)
track("/adc_avdd", [e, (e[0], r2[1] - 0.9 - (r2[0] - e[0] - 0.0)), (r2[0], r2[1] - 0.9), (r2[0], r2[1])])
track("/adc_avdd", [(r2[0], r2[1] - 0.9), (c31[0], r2[1] - 0.9), c31])
via("/+3V3", r1[0] - 1.2, r1[1]); track("/+3V3", [r1, (r1[0] - 1.2, r1[1])]); gnd_stub(pad("C31", 2), 0, 1.1)
e = S["45"]; p1 = pad("C28", 1); track("/+3V3", [e, (p1[0] - 0.45, e[1])]); via("/+3V3", p1[0] + 1.4, p1[1]); track("/+3V3", [p1, (p1[0] + 1.4, p1[1])]); gnd_stub(pad("C28", 2), -0.8, 1.1)
for num in ("31", "32", "33", "34", "35", "37", "40", "41", "42", "43"):
    via(padnet("U1", num), *S[num])
# wl_d on F.Cu: comb -> south strip (y 46.45) -> R55/R56 pad 2 and RM2 pad 5
e = S["36"]; r55 = pad("R55", 2); r56 = pad("R56", 2); u5 = pad("U9", 5); ys = 46.7; xv = 22.7
track("/wl_d", [e, (e[0] - (ys - e[1]), ys), (u5[0], ys), (u5[0], u5[1] + 0.5)])
track("/wl_d", [(xv, ys), (xv, r56[1]), r56]); track("/wl_d", [(xv, r55[1]), r55])
r = pad("R55", 1); u = pad("U9", 6); yd = u[1] + 1.0 + 0.35
track("/wl_dout", [r, (r[0], r[1] + 0.3), (r[0] - (yd - r[1] - 0.3), yd), (u[0], yd), u])
r = pad("R56", 1); u = pad("U9", 10); yi = u[1] + 0.1
track("/wl_irq", [r, (r[0], yi + 0.6), (r[0] - 0.6, yi), (u[0] + 1.0, yi), u])
# B.Cu lanes in the south strip: wl_cs (y 46.9, to x 18.3), wl_on (46.3, to 19.2), wl_clk (47.4, to 9.0)
e = S["37"]; u9c = pad("U9", 9); track("/wl_cs", [e, (e[0], 46.9), (17.5, 46.9), (17.5, u9c[1])], "B"); via("/wl_cs", 17.5, u9c[1])
e = S["35"]; u12 = pad("U9", 12); u13 = pad("U9", 13); track("/wl_on", [e, (e[0], 46.3), (18.1, 46.3), (18.1, 36.2)], "B"); via("/wl_on", 18.1, 36.2)
track("/wl_on", [(18.1, 36.2), (17.6, 35.7), (17.4, u12[1]), u12]); track("/wl_on", [(17.6, 35.7), (17.6, u13[1] + 0.3), (17.3, u13[1]), u13])
e = S["43"]; u3 = pad("U9", 3); track("/wl_clk", [e, (e[0], 47.4), (u3[0], 47.4), (u3[0], 45.6)], "B"); via("/wl_clk", u3[0], 45.6); track("/wl_clk", [(u3[0], 45.6), u3])
# port 2 (31-34) on B.Cu: north-east under the chip, then east in a 0.7 mm bundle at y 37.5..39.6
for i, num in enumerate(("31", "32", "33", "34")):
    e = S[num]; yl = CY + 0.7 + 0.7 * i; n = padnet("U1", num)
    track(n, [e, (e[0] + (e[1] - yl), yl), (CX + 12.0, yl)], "B")

b.Save(PCB)
print("routed phase 1:", len(list(b.GetTracks())), "track items")
