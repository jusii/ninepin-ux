#!/usr/bin/env python3
"""Generate the rev7 floorplan board from floorplan.py: outline, mounting holes, connectors,
OLED socket + posts, RM2 keep-out and placement guides. Re-runnable: it REPLACES the whole
board file, so only run it while the board is still a floorplan (before step 5 routing).
Usage: python3 make_board.py   (from kicad/rev7, pcbnew from the system KiCad 10)."""
import os, sys, pcbnew
from pcbnew import VECTOR2I, FromMM as mm
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import floorplan as F

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "amigahid-pico-rev7.kicad_pcb")
SYSFP = "/usr/share/kicad/footprints"

b = pcbnew.BOARD()
ds = b.GetDesignSettings()
ds.m_CopperEdgeClearance = mm(0.3)

def P(x, y): return VECTOR2I(mm(x), mm(y))

def line(x0, y0, x1, y1, layer, w=0.1):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(P(x0, y0)); s.SetEnd(P(x1, y1)); s.SetLayer(layer); s.SetWidth(mm(w)); b.Add(s)

def arc(cx, cy, r, a0, a1, layer, w=0.1):
    import math
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_ARC)
    am = (a0 + a1) / 2
    s.SetArcGeometry(P(cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0))),
                     P(cx + r * math.cos(math.radians(am)), cy + r * math.sin(math.radians(am))),
                     P(cx + r * math.cos(math.radians(a1)), cy + r * math.sin(math.radians(a1))))
    s.SetLayer(layer); s.SetWidth(mm(w)); b.Add(s)

def rect(x0, y0, x1, y1, layer, w=0.1):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_RECTANGLE)
    s.SetStart(P(x0, y0)); s.SetEnd(P(x1, y1)); s.SetLayer(layer); s.SetWidth(mm(w)); b.Add(s)

def text(t, x, y, layer, size=1.0, rot=0):
    s = pcbnew.PCB_TEXT(b); s.SetText(t); s.SetPosition(P(x, y)); s.SetLayer(layer)
    s.SetTextSize(VECTOR2I(mm(size), mm(size))); s.SetTextThickness(mm(size * 0.15))
    s.SetTextAngleDegrees(rot); b.Add(s)

# outline: rounded rectangle
W, H, R = F.W, F.H, F.CORNER_R
E = pcbnew.Edge_Cuts
line(R, 0, W - R, 0, E); line(W, R, W, H - R, E); line(W - R, H, R, H, E); line(0, H - R, 0, R, E)
arc(W - R, R, R, 270, 360, E); arc(W - R, H - R, R, 0, 90, E); arc(R, H - R, R, 90, 180, E); arc(R, R, R, 180, 270, E)

def place(ref, val, lib, name, x, y, rot, note=""):
    path = os.path.join(HERE, lib + ".pretty") if lib == "rev7" else os.path.join(SYSFP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    assert fp, (lib, name)
    fp.SetReference(ref); fp.SetValue(val)
    fp.SetPosition(P(x, y)); fp.SetOrientationDegrees(rot)
    b.Add(fp)
    return fp

for h in F.HOLES:
    place("H%d" % (F.HOLES.index(h) + 1), "M3", "MountingHole", "MountingHole_3.2mm_M3", h[0], h[1], 0)
for i, h in enumerate(F.OLED_HOLES):
    place("H%d" % (5 + i), "M2 OLED post", "MountingHole", "MountingHole_2.2mm_M2", h[0], h[1], 0)
for c in F.CONNECTORS:
    place(*c)
place(*F.J3)

# guides on User.Comments / User.Drawings
C, D = pcbnew.Cmts_User, pcbnew.Dwgs_User
ox, oy = F.OLED_X0, F.OLED_Y0
rect(ox, oy, ox + F.OLED_PCB[0], oy + F.OLED_PCB[1], C)
rect(ox + 0.4, F.OLED_GLASS_Y0, ox + 0.4 + F.OLED_GLASS[0], F.OLED_GLASS_Y0 + F.OLED_GLASS[1], C)
text("OLED 0.96in on 11 mm posts", ox + F.OLED_PCB[0] / 2, oy + F.OLED_PCB[1] + 1.5, C, 0.8)
place("U2", "RM2", "rev7", "RaspberryPi_RM2", F.RM2_CENTRE[0], F.RM2_CENTRE[1], F.RM2_ROT, "radio")
kx0, ky0, kx1, ky1 = F.RM2_KEEPOUT
rect(kx0, ky0, kx1, ky1, C)
text("RM2 antenna keep-out, no metal, all layers", 3.0, ky0 - 1.2, C, 0.7)
# the on-board part as a real rule area
z = pcbnew.ZONE(b)
z.SetIsRuleArea(True); z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True)
z.SetDoNotAllowPads(True); z.SetDoNotAllowZoneFills(True); z.SetDoNotAllowFootprints(True)
z.SetZoneName("rm2_antenna_keepout")
ls = pcbnew.LSET(); ls.addLayer(pcbnew.F_Cu); ls.addLayer(pcbnew.B_Cu); z.SetLayerSet(ls)
ol = z.Outline(); ol.NewOutline()
for (x, y) in ((max(kx0, 0), ky0), (kx1, ky0), (kx1, ky1), (max(kx0, 0), ky1)):
    ol.Append(mm(x), mm(y))
b.Add(z)
for name, (x0, y0, x1, y1) in F.ZONES.items():
    rect(x0, y0, x1, y1, D); text(name, (x0 + x1) / 2, (y0 + y1) / 2, D, 0.8)
# case wall references: inner wall and outer wall as dashed guides
g = F.GAP; o = F.OVH
rect(-g, -g, W + g, H + g, D); rect(-o, -o, W + o, H + o, D)
text("case inner wall", W / 2, -g - 0.8, D, 0.7); text("case outer wall", W / 2, -o - 0.8, D, 0.7)
text("amigahid-pico-UX rev7 floorplan", W / 2, H + o + 2.5, D, 1.2)

b.Save(OUT)
print("saved", OUT)
# report connector extents vs board edge
for fp in b.GetFootprints():
    bb = fp.GetBoundingBox(False, False)
    x0, y0, x1, y1 = [v / 1e6 for v in (bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom())]
    pads = [p for p in fp.Pads()]
    if pads:
        pb = [p.GetBoundingBox() for p in pads]
        px0 = min(q.GetLeft() for q in pb) / 1e6; px1 = max(q.GetRight() for q in pb) / 1e6
        py0 = min(q.GetTop() for q in pb) / 1e6; py1 = max(q.GetBottom() for q in pb) / 1e6
    else:
        px0 = px1 = py0 = py1 = float('nan')
    print(f"{fp.GetReference():5} body x {x0:7.2f}..{x1:7.2f} y {y0:7.2f}..{y1:7.2f} | pads x {px0:6.2f}..{px1:6.2f} y {py0:6.2f}..{py1:6.2f}")
