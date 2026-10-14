"""amigahid-pico-UX enclosure. KiCad x, y_up = -y_kicad, z = 0 at board underside.
Base plate + tall cap, four M3x16 screws from below through base, standoff, board, into
heat-set inserts in cap pillars sitting on the board's corner holes."""
import FreeCAD, Part, math
from FreeCAD import Vector as V

# ---- parameters ----
BW, BH = 100.0, 82.76          # board
GAP, WALL = 1.5, 2.5
BASE_T = 3.0                   # base plate thickness
STAND = 5.0                    # standoff height (board underside above plate)
BOARD_T = 1.6
CEIL = 22.5                    # inner ceiling above board underside
TOP_T = 3.0
INSERT_D, INSERT_L = 4.0, 6.0  # M3 heat-set hole
SCREW_D = 3.4
PILLAR_D = 6.5
LIP_H, LIP_CLR = 1.2, 0.2
CORNERS = [(4, 4), (96, 4), (4, 78.76), (96, 78.76)]   # kicad coords

OUT_X0, OUT_X1 = -(GAP + WALL), BW + GAP + WALL
OUT_Y0, OUT_Y1 = -(BH + GAP + WALL), GAP + WALL        # y_up
IN_X0, IN_X1 = -GAP, BW + GAP
IN_Y0, IN_Y1 = -(BH + GAP), GAP
Z_PLATE_TOP = -STAND
Z_PLATE_BOT = Z_PLATE_TOP - BASE_T
Z_TOP = CEIL + TOP_T

def box(x0, x1, y0, y1, z0, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))

def cyl(d, h, x, y, z):
    return Part.makeCylinder(d / 2, h, V(x, y, z))

def rbox(x0, x1, y0, y1, z0, z1, r):
    b = box(x0, x1, y0, y1, z0, z1)
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0).z) > 0.99])

def ky(y):  # kicad y -> y_up
    return -y

doc = FreeCAD.ActiveDocument or FreeCAD.newDocument("amigahid_ux_case")
for o in list(doc.Objects):
    doc.removeObject(o.Name)

# ---- cap ----
cap = rbox(OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, Z_PLATE_TOP, Z_TOP, 3.0)
cav = box(IN_X0, IN_X1, IN_Y0, IN_Y1, Z_PLATE_TOP - 1, CEIL)
cap = cap.cut(cav)
# pillars from ceiling down onto the board top at the corner holes
for (cx, cy) in CORNERS:
    p = cyl(PILLAR_D, CEIL - BOARD_T, cx, ky(cy), BOARD_T)
    cap = cap.fuse(p)
    cap = cap.cut(cyl(INSERT_D, INSERT_L, cx, ky(cy), BOARD_T))

# wall openings (x ranges kicad, z above board underside)
def wall_cut_ns(x0, x1, z0, z1, y_up_centre, r=1.5):
    b = box(x0, x1, y_up_centre - 6, y_up_centre + 6, z0, z1)
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0).y) > 0.99])
def wall_cut_ew(y0k, y1k, z0, z1, x_centre, r=1.5):
    b = box(x_centre - 6, x_centre + 6, ky(y1k), ky(y0k), z0, z1)
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0).x) > 0.99])

NY = (IN_Y1 + OUT_Y1) / 2   # north wall centre y_up
SY = (IN_Y0 + OUT_Y0) / 2
# north: J9 DIN5 c=18, J10 mDIN c=38, J11 DIN5 c=64, J12 mDIN c=84
for c in (18.0, 64.0):
    cap = cap.cut(wall_cut_ns(c - 11.5, c + 11.5, 1.0, 22.0, NY))
for c in (38.0, 84.0):
    cap = cap.cut(wall_cut_ns(c - 8.0, c + 8.0, 1.0, 15.5, NY))
# south: P1 flange 8.5-39.5, USB-A 42.2-58.4, P2 61.1-92.1
for (x0, x1) in ((8.0, 40.0), (60.6, 92.6)):
    cap = cap.cut(wall_cut_ns(x0, x1, 1.0, 15.0, SY, 2.0))
cap = cap.cut(wall_cut_ns(41.9, 58.7, 1.0, 10.0, SY, 1.0))
# east: USB-C J13 kicad y 24.4-35.6, z 1.6-4.9
EX = (IN_X1 + OUT_X1) / 2
cap = cap.cut(wall_cut_ew(23.0, 37.0, 0.8, 7.0, EX, 2.5))
# (no west opening: the pico's micro-usb is reached with the cap off)

# OLED: module x 71.8-99.3, y 38.5-66.3 (header north). two stacked 8.4 mm sockets on J3
# (16.8 + 2.54 module header) and 20 mm posts put the glass just under the lid top.
POSTS = 16.8 + 2.54
OLED_Z_TOP = BOARD_T + POSTS + 1.0 + 1.7
print("glass top", OLED_Z_TOP, "lid top", Z_TOP, "gap", Z_TOP - OLED_Z_TOP)
# plain window sized to the glass (26.7 x 19.3) + 0.5
win = box(85.55 - 13.6, 85.55 + 13.6, ky(64.5), ky(44.5), CEIL - 1, Z_TOP + 1)
cap = cap.cut(win)
# LED peek holes LED1 (4,26) LED2 (9,26)
for (lx, ly) in ((4.0, 26.0), (9.0, 26.0)):
    cap = cap.cut(cyl(2.2, 40, lx, ky(ly), CEIL - 1))

# ---- base plate ----
base = rbox(OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, Z_PLATE_BOT, Z_PLATE_TOP, 3.0)
lip = box(IN_X0 + LIP_CLR, IN_X1 - LIP_CLR, IN_Y0 + LIP_CLR, IN_Y1 - LIP_CLR, Z_PLATE_TOP, Z_PLATE_TOP + LIP_H)
base = base.fuse(lip)
for (cx, cy) in CORNERS:
    base = base.fuse(cyl(7.0, STAND, cx, ky(cy), Z_PLATE_TOP))
    base = base.cut(cyl(SCREW_D, 20, cx, ky(cy), Z_PLATE_BOT - 1))
    base = base.cut(Part.makeCone(3.4, 6.4, 1.7, V(cx, ky(cy), Z_PLATE_BOT - 0.01), V(0, 0, -1)).mirror(V(cx, ky(cy), Z_PLATE_BOT), V(0, 0, 1)))
# vent slots in base under the pico/mosfet area
for i in range(6):
    base = base.cut(box(20 + i * 8, 24 + i * 8, ky(60), ky(42), Z_PLATE_BOT - 1, Z_PLATE_TOP + 2))

oc = doc.addObject("Part::Feature", "Cap"); oc.Shape = cap
ob = doc.addObject("Part::Feature", "Base"); ob.Shape = base
doc.recompute()
print("cap valid", cap.isValid(), "base valid", base.isValid())
print("outer", OUT_X1 - OUT_X0, OUT_Y1 - OUT_Y0, "cap h", Z_TOP - Z_PLATE_TOP, "total h", Z_TOP - Z_PLATE_BOT)

# ---- debossed wall labels ----
import Draft
FONT = "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf"
DEPTH = 0.5
def label(text, size, origin, xdir, ydir, into):
    """text centred at origin (Vector), reading along xdir, up along ydir, cut 'into' direction."""
    ss = Draft.make_shapestring(text, FONT, size)
    doc.recompute()
    sh = ss.Shape.copy()
    bb = sh.BoundBox
    sh.translate(V(-(bb.XMin + bb.XMax) / 2, -(bb.YMin + bb.YMax) / 2, 0))
    m = FreeCAD.Matrix()
    zd = xdir.cross(ydir)
    m.A11, m.A21, m.A31 = xdir.x, xdir.y, xdir.z
    m.A12, m.A22, m.A32 = ydir.x, ydir.y, ydir.z
    m.A13, m.A23, m.A33 = zd.x, zd.y, zd.z
    sh = sh.transformGeometry(m)
    sh.translate(origin)
    faces = [Part.Face(w) for w in sh.Wires]
    solid = Part.makeCompound([Part.Face(sh.Wires and sh.Wires or [])]) if False else None
    # proper face with holes
    f = Part.makeFace(sh.Wires, "Part::FaceMakerBullseye")
    prism = f.extrude(into * DEPTH)
    doc.removeObject(ss.Name)
    return prism

X, Y, Z = V(1, 0, 0), V(0, 1, 0), V(0, 0, 1)
labels = []
# south wall (outer face y = OUT_Y0), viewer looks +y: text runs +x
for txt, x in (("PORT 1", 24.0), ("PORT 2", 76.6)):
    labels.append(label(txt, 4.0, V(x, OUT_Y0, 18.5), X, Z, Y))
labels.append(label("USB", 4.0, V(50.3, OUT_Y0, 13.5), X, Z, Y))
# north wall (outer face y = OUT_Y1), viewer looks -y: text runs -x
labels.append(label("AMIGA", 3.0, V(38.0, OUT_Y1, 19.0), -X, Z, -Y))
labels.append(label("KEYB", 3.0, V(84.0, OUT_Y1, 19.0), -X, Z, -Y))
# east wall (outer face x = OUT_X1), viewer looks -x: text runs +y
labels.append(label("5V", 4.0, V(OUT_X1, ky(30.0), 10.5), Y, Z, -X))
for l in labels:
    cap = cap.cut(l)
oc.Shape = cap
doc.recompute()
print("cap with labels valid", cap.isValid())
