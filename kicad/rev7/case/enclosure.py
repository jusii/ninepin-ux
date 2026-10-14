"""amigahid-pico-UX rev7 enclosure, rough fit-check model. Reads ../floorplan.py.
KiCad x, y_up = -y_kicad, z = 0 at the board underside. Base plate + cap, M3 screws from
below through the base into heat-set inserts in cap pillars on the board's corner holes.
Outer wall is the reference: mini-DIN / USB-A faces flush with it, DE9 flange behind a
thinned wall with a D-cutout and 4-40 jackscrew holes, USB-C face behind a thinned wall,
side-actuated BOOT/RESET through the east wall, OLED window and LED peek hole in the lid.
"""
import FreeCAD, Part, math, os, sys
from FreeCAD import Vector as V

FP = {}
exec(globals().get("FLOORPLAN_SRC") or open(FLOORPLAN_PATH).read(), FP)   # runner injects FLOORPLAN_SRC or FLOORPLAN_PATH
W, H, BOARD_T = FP["W"], FP["H"], FP["BOARD_T"]
GAP, WALL, OVH = FP["GAP"], FP["WALL"], FP["OVH"]
STAND, BASE_T, CEIL, TOP_T = FP["STAND"], FP["BASE_T"], FP["CEIL"], FP["TOP_T"]
DE9_WALL = FP["DE9_WALL"]
HOLES = FP["HOLES"]
INSERT_D, INSERT_L, SCREW_D, PILLAR_D = 4.0, 6.0, 3.4, 6.5
LIP_H, LIP_CLR = 1.2, 0.2

OUT_X0, OUT_X1 = -OVH, W + OVH
OUT_Y0, OUT_Y1 = -(H + OVH), OVH
IN_X0, IN_X1 = -GAP, W + GAP
IN_Y0, IN_Y1 = -(H + GAP), GAP
Z_PLATE_TOP = -STAND
Z_PLATE_BOT = Z_PLATE_TOP - BASE_T
Z_TOP = CEIL + TOP_T

def ky(y): return -y
def box(x0, x1, y0, y1, z0, z1): return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))
def cyl(d, h, x, y, z, axis=V(0, 0, 1)): return Part.makeCylinder(d / 2, h, V(x, y, z), axis)
def rbox(x0, x1, y0, y1, z0, z1, r):
    b = box(x0, x1, y0, y1, z0, z1)
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0).z) > 0.99])

doc = FreeCAD.ActiveDocument if (FreeCAD.ActiveDocument and FreeCAD.ActiveDocument.Name == "rev7_case") else FreeCAD.newDocument("rev7_case")
for o in list(doc.Objects): doc.removeObject(o.Name)

# ---- cap ----
cap = rbox(OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, Z_PLATE_TOP, Z_TOP, 3.0)
cap = cap.cut(box(IN_X0, IN_X1, IN_Y0, IN_Y1, Z_PLATE_TOP - 1, CEIL))
for (cx, cy) in HOLES:
    cap = cap.fuse(cyl(PILLAR_D, CEIL - BOARD_T, cx, ky(cy), BOARD_T))
    cap = cap.cut(cyl(INSERT_D, INSERT_L, cx, ky(cy), BOARD_T))

def cut_ns(x0, x1, z0, z1, y_centre, r=1.0, depth=6):
    b = box(x0, x1, y_centre - depth, y_centre + depth, z0, z1)
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0).y) > 0.99])
def cut_ew(y0k, y1k, z0, z1, x_centre, r=1.0, depth=6):
    b = box(x_centre - depth, x_centre + depth, ky(y1k), ky(y0k), z0, z1)
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0).x) > 0.99])

NY, SY, EX = (IN_Y1 + OUT_Y1) / 2, (IN_Y0 + OUT_Y0) / 2, (IN_X1 + OUT_X1) / 2
conn = {c[0]: c for c in FP["CONNECTORS"]}

# north: two mini-DIN6, body 14.5 wide x ~12.7 tall above the board (MD-60S), face flush
MDIN_WALL = FP["MDIN_WALL"]
for ref in ("J10", "J12"):
    x = conn[ref][4]
    cap = cap.cut(cut_ns(x - 7.8, x + 7.8, BOARD_T + 0.3, BOARD_T + 13.2, NY, 1.5))
    cap = cap.cut(box(x - 8.0, x + 8.0, IN_Y1 - 0.01, OUT_Y1 - MDIN_WALL, BOARD_T + 0.3, BOARD_T + 13.4))   # pocket
# south: DE9 flanges behind a thinned wall with a D cutout + 4-40 jackscrew holes
DE9_X0, DE9_W = FP["DE9_X0"], FP["DE9_W"]
for ref in ("P1", "P2"):
    x = conn[ref][4]
    fx0, fx1 = x + DE9_X0 - 0.3, x + DE9_X0 + DE9_W + 0.3
    # pocket: the wall in front of the flange is thinned to DE9_WALL from the cavity outward
    pocket_y1 = OUT_Y0 + DE9_WALL                       # y_up of the thinned wall's inner face
    cap = cap.cut(box(fx0, fx1, pocket_y1, IN_Y0 + 0.01, BOARD_T + 0.3, BOARD_T + 13.4))
    # D cutout (shell 16.9 x 8.3 nominal + 0.6 clearance) centred on the pin field
    cx = x + 5.54                                       # DE9 pin-field centre: (-6.96+18.04)/2
    zc = BOARD_T + 6.5                                  # shell centre height above board top (approx)
    d = box(cx - 8.9, cx + 8.9, OUT_Y0 - 1, pocket_y1 + 1, zc - 4.5, zc + 4.5)
    d = d.makeFillet(2.0, [e for e in d.Edges if abs(e.tangentAt(0).y) > 0.99])
    cap = cap.cut(d)
    for sx in (cx - 12.5, cx + 12.5):                   # 4-40 jackscrews, 25.0 mm apart
        cap = cap.cut(cyl(3.4, 10, sx, OUT_Y0 - 1, zc, V(0, 1, 0)))
# south: USB-A, face flush, body 13.5 wide x ~7 tall
x = conn["J4"][4]
cap = cap.cut(cut_ns(x + 3.5 - 7.2, x + 3.5 + 7.2, BOARD_T + 0.3, BOARD_T + 7.6, SY, 1.0))
# east: USB-C face DE9_WALL behind the outer surface: pocket + plug opening
yc = FP["USBC_Y"]
cap = cap.cut(box(IN_X1 - 0.01, OUT_X1 - DE9_WALL, ky(yc + 5.5), ky(yc - 5.5), BOARD_T + 0.2, BOARD_T + 3.6))
o = box(OUT_X1 - DE9_WALL - 1, OUT_X1 + 1, ky(yc + 4.8), ky(yc - 4.8), BOARD_T - 0.2, BOARD_T + 3.4)
cap = cap.cut(o.makeFillet(1.4, [e for e in o.Edges if abs(e.tangentAt(0).x) > 0.99]))
# east: BOOT / RESET actuators (PTS645V: 3.5 mm wide stem, centre ~3.2 mm above the board top)
for sy in FP["SW_Y"]:
    cap = cap.cut(cyl(5.2, 10, OUT_X1 + 1, ky(sy), BOARD_T + 3.2, V(-1, 0, 0)))
# lid: OLED window and LED peek hole
ox, gy0 = FP["OLED_X0"] + 0.4, FP["OLED_GLASS_Y0"]
gw, gh = FP["OLED_GLASS"]
cap = cap.cut(box(ox - 0.3, ox + gw + 0.3, ky(gy0 + gh + 0.3), ky(gy0 - 0.3), CEIL - 1, Z_TOP + 1))
lx, ly = FP["LED"]
cap = cap.cut(cyl(2.2, 40, lx, ky(ly), CEIL - 1))

# ---- base plate ----
base = rbox(OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, Z_PLATE_BOT, Z_PLATE_TOP, 3.0)
base = base.fuse(box(IN_X0 + LIP_CLR, IN_X1 - LIP_CLR, IN_Y0 + LIP_CLR, IN_Y1 - LIP_CLR, Z_PLATE_TOP, Z_PLATE_TOP + LIP_H))
for (cx, cy) in HOLES:
    base = base.fuse(cyl(7.0, STAND, cx, ky(cy), Z_PLATE_TOP))
    base = base.cut(cyl(SCREW_D, 20, cx, ky(cy), Z_PLATE_BOT - 1))
    base = base.cut(Part.makeCone(3.4, 6.4, 1.7, V(cx, ky(cy), Z_PLATE_BOT - 0.01), V(0, 0, -1)).mirror(V(cx, ky(cy), Z_PLATE_BOT), V(0, 0, 1)))

# ---- board + tall parts as a fit-check dummy ----
board = box(0, W, ky(H), 0, 0, BOARD_T)
dummies = []
for ref in ("J10", "J12"):
    x = conn[ref][4]; dummies.append(box(x - 7.25, x + 7.25, ky(12.65 - FP["MDIN_OVH"]), ky(-FP["MDIN_OVH"]), BOARD_T + 0.3, BOARD_T + 12.9))
for ref in ("P1", "P2"):
    x = conn[ref][4]; cx = x + 5.54
    dummies.append(box(x + DE9_X0, x + DE9_X0 + DE9_W, ky(H + FP["DE9_FLANGE"]), ky(H - 13.3), BOARD_T + 0.3, BOARD_T + 12.9))
    dummies.append(box(cx - 8.3, cx + 8.3, ky(H + FP["DE9_FLANGE"] + 6.0), ky(H + FP["DE9_FLANGE"]), BOARD_T + 2.5, BOARD_T + 10.5))
x = conn["J4"][4]; dummies.append(box(x - 3.5, x + 10.5, ky(H + OVH), ky(H - 10), BOARD_T + 0.3, BOARD_T + 7.0))
dummies.append(box(W - 5.6, W + OVH - DE9_WALL, ky(yc + 4.5), ky(yc - 4.5), BOARD_T + 0.2, BOARD_T + 3.3))
ox0, oy0 = FP["OLED_X0"], FP["OLED_Y0"]
dummies.append(box(ox0, ox0 + 27.5, ky(oy0 + 27.8), ky(oy0), BOARD_T + 11.0, BOARD_T + 12.0))
dummies.append(box(ox0 + 0.4, ox0 + 0.4 + gw, ky(gy0 + gh), ky(gy0), BOARD_T + 12.0, FP["OLED_Z_TOP"]))
rx0, ry0 = FP["RM2_X0"], FP["RM2_Y0"]
dummies.append(box(rx0, rx0 + FP["RM2_SIZE"][0], ky(ry0 + FP["RM2_SIZE"][1]), ky(ry0), BOARD_T, BOARD_T + 2.55))
for sy in FP["SW_Y"]:
    dummies.append(box(W - 4.5, W - 0.2, ky(sy + 3.2), ky(sy - 3.2), BOARD_T, BOARD_T + 6.5))   # body
    dummies.append(box(W - 0.2, W + OVH, ky(sy + 1.75), ky(sy - 1.75), BOARD_T + 1.45, BOARD_T + 4.95))  # stem

def add(name, shape, color=None):
    o = doc.addObject("Part::Feature", name); o.Shape = shape
    if color and hasattr(o, "ViewObject") and o.ViewObject: o.ViewObject.ShapeColor = color
    return o
add("Cap", cap, (0.85, 0.85, 0.9)); add("Base", base, (0.6, 0.6, 0.65))
add("Board", board, (0.1, 0.45, 0.2))
parts = Part.makeCompound(dummies); add("Parts", parts, (0.8, 0.7, 0.2))
doc.recompute()
print("cap valid", cap.isValid(), "base valid", base.isValid())
print("outer", OUT_X1 - OUT_X0, "x", OUT_Y1 - OUT_Y0, "x", Z_TOP - Z_PLATE_BOT, "mm; lid underside", CEIL, "oled glass top", FP["OLED_Z_TOP"])
# interference check: parts vs cap
inter = cap.common(parts)
print("cap/parts interference volume", round(inter.Volume, 3))
for i, d in enumerate(dummies):
    v = cap.common(d).Volume
    if v > 1e-3: print("  dummy", i, "bbox", [round(c, 1) for c in (d.BoundBox.XMin, d.BoundBox.XMax, d.BoundBox.YMin, d.BoundBox.YMax, d.BoundBox.ZMin, d.BoundBox.ZMax)], "vol", round(v, 2))
if inter.Volume > 1e-3: add("Interference", inter, (1.0, 0.0, 0.0))
