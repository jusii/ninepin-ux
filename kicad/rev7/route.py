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
L = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu, "I2": pcbnew.In2_Cu}
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
        if not num or num == "61" or not n or (side == "S" and num == "43"): continue
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

N = comb("N", 1.0, CY - 9.2, stop={"11": 28.3, "6": 28.3, "5": 28.3, "4": 28.3, "3": 28.3, "2": 28.5, "1": CY - 8.55})
E = comb("E", 1.2, CX + 10.5, stop={"60": pad("U8", 1)[0], "59": pad("U8", 2)[0], "58": pad("U8", 3)[0],
                                     "57": CX + 9.2, "56": CX + 9.2, "55": CX + 9.2})
W = comb("W", 1.0, CX - 10.2, stop={"20": CX - 8.0, "23": CX - 7.6, "22": pad("R54", 1)[0], "26": pad("R53", 1)[0],
                                     "30": CX - 8.55, "21": pad("Y1", 1)[0], "27": CX - 8.7, "28": CX - 8.7, "29": CX - 8.7})
S = comb("S", 1.0, CY + 9.0, stop={"38": CY + 7.2, "39": pad("C20", 1)[1], "45": CY + 8.85})

for x in (CX - 0.8, CX + 0.8):
    for y in (CY - 1.1, CY - 0.1): via("GND", x, y)

# ---- north: IOVDD 1 / DVDD 6 / IOVDD 11 go to the planes through vias on their columns; the
# caps are parked north of the lanes with their own vias. i2c/uart/3V3(11) columns also drop
# to B.Cu here (they continue north on the bottom).
for num in ("1", "6", "11", "2", "3", "4", "5"):
    x, ye = N[num]; via(padnet("U1", num), x, ye)
for cap, pwr in (("C23", "/+3V3"), ("C24", "/+3V3")):
    p1 = pad(cap, 1); p2 = pad(cap, 2)
    via(pwr, p1[0], p1[1] - 1.3); track(pwr, [p1, (p1[0], p1[1] - 1.3)]); gnd_stub(p2, 0, -1.3)
gnd_stub(pad("C21", 2), 0, -1.3)                   # C21 (dvdd) is fed on F.Cu from the In2 jumper north of the lanes
# ---- east: QSPI ----
# BOOT: qspi_ss leaves the flash pad on B.Cu, runs east at y 28.6 under the switches to R52 by SW1
p1 = pad("U8", 1); r1 = pad("R52", 1); r2 = pad("R52", 2); sw = pad("SW1", 1)
track("/qspi_ss", [(p1[0] + 0.7, p1[1]), (p1[0] + 1.4, p1[1] - 0.4)]); via("/qspi_ss", p1[0] + 1.4, p1[1] - 0.4)
xq = p1[0] + 1.4; track("/qspi_ss", [(xq, p1[1] - 0.4), (xq, 24.0)], "I2"); via("/qspi_ss", xq, 24.0)
track("/qspi_ss", [(xq, 24.0), (xq + 0.3, 23.7), (76.7, 23.7), (77.0, 23.4), (77.0, 18.7), (77.4, 18.3), (r1[0] - 0.4, 18.3), (r1[0], 17.9), (r1[0], 15.0)], "B"); via("/qspi_ss", r1[0], 15.0); track("/qspi_ss", [(r1[0], 15.0), r1])
track("/qspi_ss_sw", [r2, (r2[0], 11.7), (r2[0] + 0.4, 11.3), (sw[0] - 0.4, 11.3), (sw[0], 11.7), sw])
for num, netn, pnum, xv in (("57", "/qspi_sd0", 5, 52.6), ("56", "/qspi_sclk", 6, 53.6), ("55", "/qspi_sd3", 7, 54.4)):
    e = E[num]; via(netn, *e); pp = pad("U8", pnum)
    track(netn, [e, (xv, e[1]), (xv, pp[1])], "B"); via(netn, xv, pp[1]); track(netn, [(xv, pp[1]), pp])
p4 = pad("U8", 4); track("GND", [(p4[0] + 0.6, p4[1]), (p4[0] + 1.2, p4[1] + 0.4)]); via("GND", p4[0] + 1.2, p4[1] + 0.4)
p8 = pad("U8", 8); track("/+3V3", [(p8[0] + 0.7, p8[1]), (p8[0] + 1.4, p8[1] - 0.4)]); via("/+3V3", p8[0] + 1.4, p8[1] - 0.4)
c1 = pad("C32", 1); via("/+3V3", c1[0], c1[1] - 1.3); track("/+3V3", [c1, (c1[0], c1[1] - 1.3)]); gnd_stub(pad("C32", 2), 0, -1.3)
c30 = pad("C30", 1); route("/+3V3", E["53"], c30, "diag"); route("/+3V3", E["54"], c30, "diag"); via("/+3V3", CX + 9.4, E["53"][1]); gnd_stub(pad("C30", 2), 1.2, 0)
for num, r in (("52", "R57"), ("51", "R58")):          # straight F.Cu pair from the comb to the series resistors by the mux
    e = E[num]; p1 = pad(r, 1); track(padnet("U1", num), [e, (60.2, e[1]), (60.2 + (p1[1] - e[1]), p1[1]), p1])
# pin 50: no via here (the band runs under it); C22 is fed from C38 on F.Cu
route("/+3V3", E["49"], pad("C29", 1), "diag"); via("/+3V3", CX + 9.4, E["49"][1]); gnd_stub(pad("C29", 2), 1.2, 0)
route("/vreg_lx", E["48"], pad("L1", 2), "diag")
via("GND", *E["47"])
r2 = pad("R59", 2); route("/vreg_avdd", E["46"], r2, "diag"); c37 = pad("C37", 1); track("/vreg_avdd", [r2, (r2[0], c37[1]), c37])
r1 = pad("R59", 1); via("/+3V3", r1[0] + 1.2, r1[1]); track("/+3V3", [r1, (r1[0] + 1.2, r1[1])]); g = pad("C37", 2); track("GND", [g, (g[0] - 0.9, g[1] + 0.9)]); via("GND", g[0] - 0.9, g[1] + 0.9)
l1 = pad("L1", 1); c38 = pad("C38", 1); track("/dvdd", [l1, c38]); via("/dvdd", c38[0] + 0.1, c38[1] + 1.2); track("/dvdd", [c38, (c38[0] + 0.1, c38[1] + 1.2)]); gnd_stub(pad("C38", 2), 0, -1.2)
# ---- west ----
via("/+3V3", CX - 7.7, W["20"][1]); e = W["30"]; track("/+3V3", [e, (e[0], e[1] + 0.5)]); via("/+3V3", e[0], e[1] + 0.5)
for cap in ("C25", "C26", "C27"):               # parked 3V3 caps west of the lanes (pad 1 down): 3V3 via below, GND above
    p1 = pad(cap, 1); p2 = pad(cap, 2); via("/+3V3", p1[0], p1[1] + 1.3); track("/+3V3", [p1, (p1[0], p1[1] + 1.3)]); gnd_stub(p2, 0, -1.3)
via("/dvdd", *W["23"])
c22 = pad("C22", 1); track("/dvdd", [E["50"], (c22[0] - 0.3, E["50"][1]), c22])
g = pad("C22", 2); track("GND", [g, (g[0] - 0.28, g[1] + 0.28), (g[0] - 0.28, 41.4)]); via("GND", g[0] - 0.28, 41.4)
c38 = pad("C38", 1); track("/dvdd", [c38, (c22[0] + 0.3, c38[1] - (c22[0] + 0.3 - c38[0])), (c22[0] + 0.3, c22[1] + 0.3), c22])
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
e = S["39"]; via("/dvdd", e[0], CY + 7.1); gnd_stub(pad("C20", 2), -1.0, 0)
e = S["44"]; r1 = pad("R50", 1); r2 = pad("R50", 2); c31 = pad("C31", 1)
ya = r2[1] - 0.8
track("/adc_avdd", [e, (e[0], ya - 1.2), (e[0] - 1.2, ya), (c31[0] + 0.5, ya), (c31[0], ya + 0.5), c31]); track("/adc_avdd", [r2, (r2[0], ya + 0.4), (r2[0] - 0.4, ya)])
via("/+3V3", r1[0], r1[1] + 0.9); track("/+3V3", [r1, (r1[0], r1[1] + 0.9)]); gnd_stub(pad("C31", 2), 0, 0.9)
e = S["45"]; p1 = pad("C28", 1); track("/+3V3", [e, (p1[0] - 0.45, e[1])]); via("/+3V3", p1[0] + 0.8, p1[1]); track("/+3V3", [p1, (p1[0] + 0.8, p1[1])]); gnd_stub(pad("C28", 2), -0.8, 1.1)
for num in ("31", "32", "33", "34", "35", "37", "40", "41", "42"):
    via(padnet("U1", num), *S[num])
p43 = pad("U1", 43); u3 = pad("U9", 3); y0 = p43[1] + 0.4 + S0 + 0.31 * 2
track("/wl_clk", [p43, (p43[0], y0), (p43[0] + (43.0 - y0), 43.0)]); via("/wl_clk", p43[0] + (43.0 - y0), 43.0)
xv = p43[0] + (43.0 - y0)
track("/wl_clk", [(xv, 43.0), (xv, 43.3), (xv - 1.2, 44.5), (33.85, 44.5), (33.5, 44.85), (33.5, 47.0), (33.1, 47.4), (u3[0] + 0.4, 47.4), (u3[0], 47.0), (u3[0], 45.3)], "B"); via("/wl_clk", u3[0], 45.3); track("/wl_clk", [(u3[0], 45.3), u3])
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
# port 2 (W 27-29 and S 31-34) joins the B.Cu band under the chip: 7 lanes at 0.45 mm, y 38.2..40.9,
# east to x 74 (phase 2b carries it on to U7)
BAND = [38.2 + 0.45 * i for i in range(7)]
for i, num in enumerate(("27", "28", "29")):                 # W rows: via at the row end, diagonal NE up to the lane
    e = W[num]; yl = BAND[i]; n = padnet("U1", num)
    track(n, [e, (e[0] + (e[1] - yl), yl), (74.0, yl)], "B")
for j, num in enumerate(("31", "32", "33", "34")):           # S columns: diagonal NE from the column end
    e = S[num]; yl = BAND[3 + j]; n = padnet("U1", num)
    track(n, [e, (e[0] + (e[1] - yl), yl), (74.0, yl)], "B")

b.Save(PCB)
print("routed phase 1:", len(list(b.GetTracks())), "track items")

# ====================== phase 2a: U6 (keyboard + port 1 hq..b1) ======================
b = pcbnew.LoadBoard(PCB)
_pads.clear()
for fp in b.GetFootprints():
    for pd in fp.Pads():
        _pads[(fp.GetReference(), pd.GetNumber())] = (pd.GetPosition().x / 1e6, pd.GetPosition().y / 1e6, pd.GetNetname())
# MCU lanes: north comb columns turn east at y 24.6..27.4 (west column = top lane) and drop
# into U6's south row; the U6 pad order was chosen in design.py so this is planar.
LANES = [("15", "13"), ("14", "14"), ("13", "15"), ("12", "16"), ("10", "17"), ("9", "18"), ("8", "19"), ("7", "20")]   # (U1 pin, U6 pin)
for i, (u1, u6) in enumerate(LANES):
    x, ytop = N[u1]; yl = 24.6 + 0.4 * i; n = padnet("U1", u1); px, py = pad("U6", u6)
    track(n, [(x, ytop), (x, yl + 0.4), (x + 0.4, yl), (px - 0.4, yl), (px, yl - 0.4), (px, py)])
# kbin_lo and port-1 b2/b3_lo: west-side vias -> B.Cu east bus at y 28.6..30.3 -> vias by the pads
for u1, y_lane, dst, vy in (("16", 25.0, ("U7", "22"), 19.5), ("17", 25.4, ("U7", "21"), 20.4),
                             ("18", 26.2, ("U6", "21"), 26.2), ("19", 26.8, ("U6", "22"), 26.8)):
    e = W[u1]; vx0 = e[0] - 0.4; n = padnet("U1", u1); px, py = pad(*dst); vx = px
    pts = [(vx0, e[1]), (vx0 + (e[1] - y_lane), y_lane)]
    if dst[0] == "U6": pts += [(vx, y_lane)]; via(n, vx, y_lane); track(n, [(vx, y_lane), (vx, py)])
    else: pts += [(vx, y_lane), (vx, vy)]; via(n, vx, vy); track(n, [(vx, vy), (px, py)])
    track(n, pts, "B")
# keyboard-out trio: J1 pins on B.Cu east at y 15.6/16.0/16.4 to vias north of U6 A4..A6
for j1, y_lane, u6, t, gdy in (("3", 15.2, "4", "D12", -1.3), ("2", 16.2, "5", "D13", 1.3), ("1", 17.2, "6", "D14", 1.3)):
    n = padnet("J1", j1); jx, jy = pad("J1", j1); px, py = pad("U6", u6); tp = pad(t, 1)
    track(n, [(jx, jy), (jx, y_lane - 0.4), (jx + 0.4, y_lane), (px, y_lane)]); via(n, px, y_lane); track(n, [(px, y_lane), (px, py)])
    gnd_stub(pad(t, 2), 0, gdy)          # the lane passes through the clamp's pad 1
# keyboard-in: J12 pins east over the J12 body, down between the J3 pins, into A1/A2
jd, jc = pad("J12", 1), pad("J12", 5); a1, a2 = pad("U6", 2), pad("U6", 3)
d10 = pad("D10", 1); xv = 76.8
track("/kbin_dat", [jd, (jd[0], 7.6), (jd[0] + 2.4, 10.0), (xv, 10.0), (xv, d10[1] - (xv - d10[0])), d10, (d10[0], d10[1] + 0.45), (d10[0] - (17.0 - d10[1] - 0.45), 17.0), (a1[0], 17.0), a1]); gnd_stub(pad("D10", 2), 1.2, 0)
track("/kbin_clk", [jc, (jc[0], 10.0), (jc[0] + 0.4, 10.4), (74.25 - 0.4, 10.4), (74.25, 10.8), (74.25, 14.3), (a2[0], 14.3 + (74.25 - a2[0])), a2])
d11 = pad("D11", 1); track("/kbin_clk", [jc, (jc[0], d11[1] - 0.65), d11]); gnd_stub(pad("D11", 2), -1.2, 0)
# U6 ground pins and VCC cap
gnd_stub(pad("U6", 12), -0.9, -0.3); gnd_stub(pad("U6", 23), 0.0, 0.8)
v24 = pad("U6", 24); c13 = pad("C13", 1); track("/AMIGA_5V_FUSED", [v24, (v24[0], v24[1] + 0.7), (c13[0], v24[1] + 0.7 + (c13[0] - v24[0])), c13], w=0.3); gnd_stub(pad("C13", 2), 0, 1.2)
b.Save(PCB)
print("routed phase 2a:", len(list(b.GetTracks())), "track items")

# ====================== phase 2b: band to U7, jumpers, south corridor, USB mux ======================
b = pcbnew.LoadBoard(PCB)
_pads.clear()
for fp in b.GetFootprints():
    for pd in fp.Pads():
        _pads[(fp.GetReference(), pd.GetNumber())] = (pd.GetPosition().x / 1e6, pd.GetPosition().y / 1e6, pd.GetNetname())
# --- band: at x 74 the seven lanes fan north-east into verticals under U7 and pop up north of its B row
U7B = {padnet("U7", n)[1:]: n for n in ("14", "15", "16", "17", "18", "19", "20")}
ORDER = [padnet("U1", p)[1:] for p in ("27", "28", "29", "31", "32", "33", "34")]   # lane 0 (top) .. 6, as laid in phase 1
XS = sorted(pad("U7", p)[0] for p in U7B.values())           # 80.05 .. 83.86
for i, n in enumerate(ORDER):
    yl = BAND[i]; xv = XS[i]; px, py = pad("U7", U7B[n])
    assert abs(px - xv) < 1e-6, (n, px, xv)
    vy = 19.5 if i % 2 == 0 else 20.4
    T = 74.0 + 0.2 * i; h = T + yl - xv                       # NE diagonals x+y = const, 0.65 apart, end on the verticals
    track("/" + n, [(74.0, yl), (T, yl), (xv, h), (xv, vy)], "B"); via("/" + n, xv, vy); track("/" + n, [(xv, vy), (px, py)])
# --- In2 jumpers across the B.Cu band at x 43 (dvdd inner, amiga_sense outer) and across the lane wall
# (y 24.6..28.6 on both outer layers) for amiga_sense (x 41.5) and the north dvdd group (x 35)
def jumper(n, pts): track(n, pts, "I2"); via(n, *pts[0]); via(n, *pts[-1])
jumper("/dvdd", [(43.1, 37.4), (43.1, 42.2)])
jumper("/amiga_sense", [(43.0, 36.2), (43.6, 36.8), (43.6, 42.8), (43.0, 43.4)])
# dvdd web: south via -> C38 via, and on west along y 44.6 to the S39 via; north via -> x 41.0 -> y 29.4 -> N6 column, W23 row
c38v = (pad("C38", 1)[0] + 0.1, pad("C38", 1)[1] + 1.2); s39 = (S["39"][0], CY + 7.1)
n6 = N["6"]; w23 = W["23"]; c21 = pad("C21", 1)
track("/dvdd", [(43.1, 42.2), (43.5, 42.6), (44.5, 43.6), (45.5, 44.6), c38v], "B")
track("/dvdd", [(43.1, 42.2), (43.0, 42.3), (s39[0] + 0.6, 42.3), s39], "B")
track("/dvdd", [(43.1, 37.4), (41.4, 37.4), (41.0, 37.0), (41.0, 29.8), (40.6, 29.4), (26.6, 29.4), (26.6, w23[1] - 1.2), w23], "B")
track("/dvdd", [(n6[0], 29.4), n6], "B"); track("/dvdd", [n6, (n6[0], 24.0)], "I2"); via("/dvdd", n6[0], 24.0)
track("/dvdd", [(n6[0], 24.0), (n6[0] - 0.4, 23.6), (c21[0] + 0.9, 23.6), (c21[0], 22.7), c21])
# amiga_sense: S42 via -> lane 46.7 -> up x 41 -> jumper; north: x 41.5 on B.Cu to y 27.9, In2 through the lane wall to y 24.0, F.Cu to the divider at y 22.4
e = S["42"]
track("/amiga_sense", [e, (e[0] + 0.9, 46.7), (40.1, 46.7), (41.0, 45.8), (41.0, 44.2), (41.4, 43.8), (42.6, 43.8), (43.0, 43.4)], "B")
track("/amiga_sense", [(43.0, 36.2), (41.9, 35.1), (41.5, 34.7), (41.5, 27.9)], "B"); jumper("/amiga_sense", [(41.5, 27.9), (41.5, 24.0)])
r46 = pad("R46", 2); r47 = pad("R47", 1); c11 = pad("C11", 1)
track("/amiga_sense", [(41.5, 24.0), (41.5, 22.9), (41.0, 22.4), (r46[0], 22.4)])
for p in (r46, r47, c11): track("/amiga_sense", [p, (p[0], 22.4)])
# --- south B.Cu corridor from the S comb: usb_sel (lane 47.2) and ws_drv (47.7) go east under the USB mux;
# the port TVS ground vias sit at y 48.9, below the lanes
e = S["41"]; track("/usb_sel", [e, (e[0] + 1.4, 47.2), (80.0, 47.2), (80.0, 43.0), (79.5, 42.5), (72.17, 42.5)], "B"); via("/usb_sel", 72.17, 42.5)
e = S["40"]; track("/ws_drv", [e, (e[0] + 1.9, 47.7), (83.1, 47.7), (90.6, 40.2), (92.0, 40.2)], "B"); via("/ws_drv", 92.0, 40.2)
# --- USB mux U5 (rot 270): top row 5 4 3 2 1 at y 41.3, bottom row 6 7 8 9 10 at y 45.5
U = {n: pad("U5", n) for n in range(1, 11)}
r57 = pad("R57", 2); r58 = pad("R58", 2); r48 = pad("R48", 1); r48g = pad("R48", 2); c15 = pad("C15", 1); c15g = pad("C15", 2)
track("/usb_dp_m", [r57, (U[4][0] - 0.7, r57[1]), (U[4][0], r57[1] + 0.7), U[4]])                        # pin 4 from the north-west
xd = r58[0] + 0.1; track("/usb_dm_m", [r58, (xd, r58[1]), (xd, U[6][1] - 0.4), (xd + 0.4, U[6][1]), U[6]])   # pin 6 from the west
for n, y in ((5, 42.3), (9, 43.5)): g = U[n]; track("GND", [g, (g[0], y)]); via("GND", g[0], y)         # ground pins: vias under the body
track("/usb_sel", [U[1], r48]); track("/usb_sel", [(72.17, 42.5), (72.17, r48[1] + 0.4), r48])         # sel: R48 pull-down, via below it
gv = (r48g[0] + 0.58, 41.8); track("GND", [r48g, (r48g[0] + 0.4, r48g[1]), (gv[0], r48g[1] + 0.2), gv]); track("GND", [c15g, (c15g[0], 42.3), (gv[0] - 0.1, 41.9), gv]); via("GND", *gv)
track("/+3V3", [U[10], (U[10][0] + 0.75, U[10][1]), (c15[0] - 0.5, c15[1] + 0.5), c15]); via("/+3V3", c15[0] + 0.7, c15[1]); track("/+3V3", [c15, (c15[0] + 0.7, c15[1])])
# usba pair: vias under the mux body, west on B.Cu (dm 42.8, dp 44.4) to U2 by the USB-A socket
u2 = {n: pad("U2", n) for n in range(1, 7)}
p3 = U[3]; v3 = (p3[0], 43.0); track("/usba_dp", [p3, v3]); via("/usba_dp", *v3)
track("/usba_dp", [v3, (p3[0], 43.6), (p3[0] - 0.9, 44.5), (52.9, 44.5), (52.0, 45.4), (52.0, 46.2)], "B"); via("/usba_dp", 52.0, 46.2)
track("/usba_dp", [(52.0, 46.2), (u2[4][0] + 0.7, u2[4][1] + 0.4), (u2[4][0] + 0.3, u2[4][1]), u2[4]]); track("/usba_dp", [u2[3], pad("J4", 3)])
p7 = U[7]; v7 = (p7[0] - 0.6, 43.8); track("/usba_dm", [p7, (p7[0], 44.4), v7]); via("/usba_dm", *v7)
track("/usba_dm", [v7, (v7[0] - 1.0, 42.9), (49.5, 42.9), (49.0, 43.4), (49.0, 44.6)], "B"); via("/usba_dm", 49.0, 44.6)
track("/usba_dm", [(49.0, 44.6), (u2[6][0], 44.65), u2[6]]); track("/usba_dm", [u2[1], pad("J4", 2)])
gnd_stub(u2[2], 0, 1.0)
# usbc pair to U4 (rot 270 by the USB-C): dp north over the sel pin, east at y 40.0 into pin 4 from the west;
# dm south under pins 9/10, up east of C15, north-east, east at y 40.5 into pin 6 from below
u4 = {n: pad("U4", n) for n in range(1, 7)}
track("/usbc_dp", [U[2], (U[2][0], 40.0), (u4[4][0] - 1.7, 40.0), (u4[4][0], 40.0 - 1.7 + 0.1), u4[4]])
xm = c15[0] + 1.3
track("/usbc_dm", [U[8], (U[8][0], 46.5), (U[8][0] + 0.2, 46.7), (xm - 0.8, 46.7), (xm, 45.9), (xm, 43.0), (xm + 2.5, 40.5), (u4[6][0], 40.5), u4[6]])
# U4 (rot 270: top row 3 2 1, bottom 4 5 6): pin 2 ground via north-west, pin 5 (AUX) via under the body to the
# south AUX pads, dp on F.Cu north then east into B6/A6, dm on B.Cu north to a via and east into B7 with the A7 bridge
g = u4[2]; gv = (u4[2][0] - 0.15, 35.0); track("GND", [g, (g[0], 35.4), (gv[0], 35.25), gv]); via("GND", *gv)
a5 = u4[5]; via("/AUX_5V", a5[0], 37.0); track("/AUX_5V", [a5, (a5[0], 37.0)])
track("/AUX_5V", [(a5[0], 37.0), (a5[0] - 0.8, 37.8), (88.4, 37.8), (87.6, 37.0), (87.6, 29.7), (88.0, 29.3), (89.6, 29.3), (90.5, 28.4), (90.5, 28.2)], "B"); via("/AUX_5V", 90.5, 28.2)
J = {}
for fp in b.GetFootprints():
    if fp.GetReference() == "J13":
        for pd in fp.Pads(): J.setdefault(pd.GetNumber(), (pd.GetPosition().x / 1e6, pd.GetPosition().y / 1e6))
xw = J["A6"][0] - 0.575; xe = J["A6"][0] + 0.575                   # ends of the 1.15 mm signal pads
track("/AUX_5V", [(90.5, 28.2), (90.9, J["B4"][1]), J["B4"]])
track("/AUX_5V", [J["A4"], (xe, J["A4"][1]), (xe + 0.23, J["A4"][1] - 0.23), (95.6, J["A4"][1] - 0.23), (95.6, J["A9"][1] + 0.23), (xe + 0.23, J["A9"][1] + 0.23), (xe, J["A9"][1]), J["A9"]])
track("/usbc_dp", [u4[3], (u4[3][0], 31.3), (u4[3][0] + 0.3, 31.0), (91.6, 31.0), (92.35, 30.25), J["B6"]]); track("/usbc_dp", [J["B6"], (xe + 0.3, J["B6"][1]), (xe + 0.3, J["A6"][1]), J["A6"]])
dv = (u4[1][0] - 0.8, 31.7)
track("/usbc_dm", [u4[1], (u4[1][0], 35.1)]); via("/usbc_dm", u4[1][0], 35.1)
track("/usbc_dm", [(u4[1][0], 35.1), (u4[1][0] - 0.3, 34.8), (dv[0], 34.3), dv], "B"); via("/usbc_dm", *dv)
track("/usbc_dm", [dv, (dv[0] + 0.05, J["B7"][1]), J["B7"]]); track("/usbc_dm", [(xw - 0.3, J["B7"][1]), (xw - 0.3, J["A7"][1]), J["A7"]])
r43 = pad("R43", 1); track("/usb_cc1", [r43, (r43[0], r43[1] - 0.35), (r43[0] + 0.4, J["A5"][1]), J["A5"]]); g = pad("R43", 2); track("GND", [g, (g[0] - 0.18, g[1] + 0.18), (g[0] - 0.18, 33.9)]); via("GND", g[0] - 0.18, 33.9)
r44 = pad("R44", 1); track("/usb_cc2", [r44, (r44[0] + 0.5, r44[1]), (r44[0] + 0.75, J["B5"][1]), J["B5"]]); g = pad("R44", 2); track("GND", [g, (g[0], 30.3)]); via("GND", g[0], 30.3)
track("/AUX_5V", [(92.2, 18.0), (92.2, 28.0), (92.5, 28.3), (92.8, J["B4"][1])])
track("GND", [J["A1"], (J["A1"][0] + 0.6, J["A1"][1] + 0.6)]); track("GND", [J["B1"], (J["B1"][0] + 0.6, J["B1"][1] - 0.6)])
# LED corner: Q1 drives ws_din; LED_VDD from D3 through R49 and on to C12/LED1
q = {n: pad("Q1", n) for n in (1, 2, 3)}; l = {n: pad("LED1", n) for n in (2, 3, 4)}; r49 = pad("R49", 1); r49b = pad("R49", 2); c12 = pad("C12", 1)
track("/ws_drv", [(92.0, 40.2), (q[1][0] - 0.45, q[1][1])]); track("GND", [q[2], (q[2][0] - 0.35, q[2][1] + 0.35), (92.0, q[2][1] + 0.35)]); via("GND", 92.0, q[2][1] + 0.35)
track("/ws_din", [r49b, (r49b[0] + 0.5, r49b[1]), (r49b[0] + 1.3, 40.9), (91.6, 40.9), (92.3, q[3][1]), q[3]])
track("/ws_din", [q[3], (96.0, q[3][1]), (96.0, l[3][1] + 0.55), (l[3][0] + 0.45, l[3][1]), l[3]])
d3 = pad("D3", 1); track("/LED_VDD", [d3, (d3[0], 43.5), (d3[0] - 0.5, 43.0), (r49[0], 43.0), (r49[0], r49[1] + 0.1)])
track("/LED_VDD", [(d3[0], 43.5), (d3[0] + 0.4, 43.1), (95.9, 43.1), (96.5, 42.5), (96.5, 37.8), (l[4][0] + 0.45, l[4][1]), l[4]]); track("/LED_VDD", [(96.5, c12[1]), c12])
gnd_stub(pad("C12", 2), 0, -0.9); gnd_stub(l[2], 0, 0.95)
# SYS5V east trunk: D2 pad 1 -> F.Cu y 16.0 east -> via at x 96 -> B.Cu south -> y 46.0 west -> via by D3; D3 and F2 on F.Cu
d2 = pad("D2", 1); track("/SYS5V", [d2, (d2[0], 16.7), (d2[0] + 0.7, 16.0), (92.7, 16.0)], w=0.5); via("/SYS5V", 92.7, 16.0, 0.6, 0.3)
track("/SYS5V", [(92.7, 16.0), (92.7, 46.0), (88.8, 46.0)], "B", w=0.4); via("/SYS5V", 88.8, 46.0, 0.6, 0.3)
f3 = pad("F3", 1); track("/SYS5V", [(84.3, 16.0), (84.3, f3[1] + 0.4)], w=0.5)
d3b = pad("D3", 2); f2 = pad("F2", 1); track("/SYS5V", [(88.8, 46.0), d3b], w=0.4); track("/SYS5V", [(88.8, 46.0), (88.5, 45.8), (f2[0] + 0.9, 45.8), (f2[0], 45.1)], w=0.6)
d2b = pad("D2", 2); track("/AUX_5V", [d2b, (91.6, d2b[1]), (92.2, d2b[1] + 0.6), (92.2, 18.0)], w=0.5)
# USBA_5V: F2 pad 2 -> via -> B.Cu y 45.1 west under the mux -> via below C4 -> C4, C5, U2, J4 on F.Cu
f2b = pad("F2", 2); c4 = pad("C4", 1); c5 = pad("C5", 1)
via("/USBA_5V", f2b[0] - 0.9, 45.25, 0.6, 0.3); track("/USBA_5V", [f2b, (f2b[0] - 0.9, 45.25)], w=0.6)
track("/USBA_5V", [(f2b[0] - 0.9, 45.25), (c4[0] + 0.2, 45.25)], "B", w=0.6); via("/USBA_5V", c4[0] + 0.2, 45.25, 0.6, 0.3); track("/USBA_5V", [(c4[0] + 0.2, 45.25), c4], w=0.6)
track("/USBA_5V", [c4, (c4[0], 42.3), (c4[0] + 0.7, 41.6), (c5[0] - 0.4, 41.6), c5], w=0.5)
track("/USBA_5V", [(c4[0], 42.3), (c4[0] - 0.2, 42.1), (50.3, 42.1), (50.0, 42.4), (50.0, u2[5][1] - 0.3), u2[5]], w=0.3)
j4 = pad("J4", 1); track("/USBA_5V", [c4, (c4[0] + 0.7, c4[1] + 0.7), (59.6, c4[1] + 1.4), (59.6, 55.9), (59.1, 56.4), (j4[0] + 0.5, 56.4), (j4[0], 55.9), j4], w=0.5)
g = pad("C4", 2); track("GND", [(g[0] - 1.6, g[1]), (50.6, g[1])]); via("GND", 50.6, g[1])
g = pad("C5", 2); track("GND", [g, (g[0] + 0.3, g[1] + 0.3), (g[0] + 0.6, g[1] + 0.6)]); via("GND", g[0] + 0.6, g[1] + 0.6)
b.Save(PCB)
print("routed phase 2b-1:", len(list(b.GetTracks())), "track items")
