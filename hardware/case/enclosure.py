"""Ninepin UX enclosure for the rev7 / rev7.1 board (FreeCAD, Part workbench only, runs headless).

Reads ../rev7.1/floorplan.py (board, connectors, case constants, OLED) and parts.json (every footprint's
courtyard and height, written by export_parts.py). KiCad x, y_up = -y_kicad, z = 0 at the board
underside. A clamshell split at Z_SPLIT, so the board drops in with its connectors sticking out on
three sides: TRAY = plate, standoffs and the walls up to the split (openings open upward), CAP =
walls above the split + lid + pillars (openings open downward). A lip on the tray's inner wall
locates the cap. Four M3 screws from below through the tray and the board into heat-set inserts in
the cap pillars on the board's corner holes.
Outer wall is the reference: mini-DIN and USB-A faces flush, DE9 flange and USB-C behind thinned
walls, BOOT/RESET actuators through the east wall, OLED window + encoder + LED hole in the lid.

Decisions taken here (2026-10-18), all parameters below:
  - encoder Bourns PEC11R (7 mm M7 bushing, nut on the lid) at ENC, knob left of the display
  - OLED window = the lit area + WIN_CLR, not the glass
  - A500 ribbon slot in the north wall above J1 (the only way to use the A500 headers closed)
  - debossed wall labels, 0.5 mm, DejaVu Bold
  - DE9 flange proud distance = floorplan DE9_FLANGE (0.9, still an assumption: measure on rev6)
Run: python3 hardware/case/run_case.py (feeds this file to FreeCAD) or exec it inside FreeCAD with REV7 (board dir) and CASE set.
"""
import json, os
import FreeCAD, Part
from FreeCAD import Vector as V

REV7 = globals().get("REV7") or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "rev7.1")
OUT_DIR = globals().get("CASE") or os.path.dirname(os.path.abspath(__file__))
FP = {}; exec(open(os.path.join(REV7, "floorplan.py")).read(), FP)
PARTS = json.load(open(os.path.join(OUT_DIR, "parts.json")))

W, H, BOARD_T = FP["W"], FP["H"], FP["BOARD_T"]
GAP, WALL, OVH = FP["GAP"], FP["WALL"], FP["OVH"]
STAND, BASE_T, CEIL, TOP_T = FP["STAND"], FP["BASE_T"], FP["CEIL"], FP["TOP_T"]
DE9_WALL, MDIN_WALL = FP["DE9_WALL"], FP["MDIN_WALL"]
HOLES = FP["HOLES"]
CONN = {c[0]: c for c in FP["CONNECTORS"]}

# ---- case parameters ----
CORNER_R = 3.0
INSERT_D, INSERT_L, SCREW_D, PILLAR_D = 4.0, 6.0, 3.4, 6.5     # M3 heat-set inserts in the cap pillars
Z_SPLIT = 6.0                                                  # tray / cap seam above the board underside
LIP_UP, LIP_T, LIP_CLR = 1.5, 1.2, 0.2                         # tray lip: height above the seam, thickness, clearance
FOOT_D, FOOT_H, FOOT_IN = 8.0, 0.6, 7.0                        # rubber-foot recesses under the base
ENC = (48.0, 28.0)                                             # knob centre (KiCad), from the 2026-10-18 fit scan
ENC_HOLE, ENC_BODY, ENC_BODY_H, ENC_PINS_H = 7.2, 13.5, 7.0, 10.0   # PEC11R: M7 bushing; body envelope below the lid
ENC_LUG = None                                                 # (dx, dy, dia) of an anti-rotation lug hole if the part has one
WIN_CLR = 0.3                                                  # window = lit area + this each side
LED_HOLE = 2.5
SW_STEM_DY = 2.25                                              # PTS645Vx58 actuator centre: half-way between pins 1 and 2,
SW_STEM = tuple(y + SW_STEM_DY for y in FP["SW_Y"])            # 2.25 south of the footprint origin (= pin 1, = SW_Y)
SW_STEM_Z = 3.5                                                # actuator centre above the board top (KiCad PTS645Vx58 model: 1.93-5.07)
A500_SLOT = True                                               # ribbon exit above J1 in the north wall
FONT = next(f for f in ("/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",                      # flatpak FreeCAD sees its own fonts
                        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",             # debian host
                        "/run/host/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf") if os.path.exists(f))
LABEL_DEPTH = 0.5
# lid logo, an inlay for a second filament (or a deboss in one colour): nine dots in the DE9 layout
# and a dot-matrix wordmark. dot and gap sizes are for a 0.5 mm nozzle (gap = one line)
LOGO_DEPTH = 0.6                                               # three layers, lid printed face down
LOGO_PINS = (17.0, 18.0, 5.0, 3.0)                             # centre x, y (KiCad), pitch, dot diameter
LOGO_TEXT, LOGO_TEXT_AT, LOGO_PITCH, LOGO_DOT = "NINEPIN UX", (50.0, 52.0), 2.0, 1.5
LOGO_FONT = {"N": ("1001", "1001", "1101", "1011", "1001", "1001", "1001"), "I": ("1",) * 7,
             "E": ("1111", "1000", "1000", "1110", "1000", "1000", "1111"),
             "P": ("1110", "1001", "1001", "1110", "1000", "1000", "1000"),
             "U": ("1001", "1001", "1001", "1001", "1001", "1001", "0110"),
             "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"), " ": ("0",) * 7}

OUT_X0, OUT_X1 = -OVH, W + OVH
OUT_Y0, OUT_Y1 = -(H + OVH), OVH
IN_X0, IN_X1 = -GAP, W + GAP
IN_Y0, IN_Y1 = -(H + GAP), GAP
Z_PLATE_TOP = -STAND
Z_PLATE_BOT = Z_PLATE_TOP - BASE_T
Z_TOP = CEIL + TOP_T
Z_BT = BOARD_T                                                 # board top


def ky(y): return -y
def box(x0, x1, y0, y1, z0, z1): return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, y0, z0))
def kbox(x0, x1, yk0, yk1, z0, z1): return box(x0, x1, ky(yk1), ky(yk0), z0, z1)    # KiCad y range
def cyl(d, h, x, y, z, axis=V(0, 0, 1)): return Part.makeCylinder(d / 2, h, V(x, y, z), axis)
def rbox(x0, x1, y0, y1, z0, z1, r):
    b = box(x0, x1, y0, y1, z0, z1)
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0).z) > 0.99])
def fillet_along(b, axis, r):
    """fillet the edges running along a wall-normal axis (0 x, 1 y) of a cut-out box"""
    return b.makeFillet(r, [e for e in b.Edges if abs(e.tangentAt(0)[axis]) > 0.99])
def cut_ns(x0, x1, z0, z1, y_centre, r=1.0, depth=6): return fillet_along(box(x0, x1, y_centre - depth, y_centre + depth, z0, z1), 1, r)
def cut_ew(yk0, yk1, z0, z1, x_centre, r=1.0, depth=6): return fillet_along(box(x_centre - depth, x_centre + depth, ky(yk1), ky(yk0), z0, z1), 0, r)

# ---- the closed box: plate, walls, lid; cut into tray and cap after the openings ----
cap = rbox(OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, Z_PLATE_BOT, Z_TOP, CORNER_R)
cap = cap.cut(box(IN_X0, IN_X1, IN_Y0, IN_Y1, Z_PLATE_TOP, CEIL))
pillars = None                                                 # the cap's, fused after the split
for (cx, cy) in HOLES:
    pl = cyl(PILLAR_D, CEIL - Z_BT, cx, ky(cy), Z_BT).cut(cyl(INSERT_D, INSERT_L, cx, ky(cy), Z_BT))
    pillars = pl if pillars is None else pillars.fuse(pl)

NY, SY, EX = (IN_Y1 + OUT_Y1) / 2, (IN_Y0 + OUT_Y0) / 2, (IN_X1 + OUT_X1) / 2
# north: mini-DIN sockets, face flush, wall thinned to MDIN_WALL over the body
for ref in ("J10", "J12"):
    x = CONN[ref][4]
    cap = cap.cut(cut_ns(x - 7.8, x + 7.8, Z_BT + 0.3, Z_BT + 13.2, NY, 1.5))
    cap = cap.cut(box(x - 8.0, x + 8.0, IN_Y1 - 0.01, OUT_Y1 - MDIN_WALL, Z_BT + 0.3, Z_BT + 13.4))
# north: A500 ribbon slot above J1 (header top 8.5 above the board top)
if A500_SLOT:
    x = CONN["J1"][4] + 3.5 * 2.54
    cap = cap.cut(cut_ns(x - 7.0, x + 7.0, Z_BT + 10.0, CEIL - 0.6, NY, 1.0))
# south: DE9 flanges behind a wall thinned to DE9_WALL, D cut-out, 4-40 jackscrew holes
DE9_X0, DE9_W = FP["DE9_X0"], FP["DE9_W"]
for ref in ("P1", "P2"):
    x = CONN[ref][4]
    cap = cap.cut(box(x + DE9_X0 - 0.3, x + DE9_X0 + DE9_W + 0.3, OUT_Y0 + DE9_WALL, IN_Y0 + 0.01, Z_BT + 0.3, Z_BT + 13.4))
    cx, zc = x + 5.54, Z_BT + 6.5                      # pin-field centre, shell centre height
    cap = cap.cut(fillet_along(box(cx - 8.9, cx + 8.9, OUT_Y0 - 1, OUT_Y0 + DE9_WALL + 1, zc - 4.5, zc + 4.5), 1, 2.0))
    for sx in (cx - 12.5, cx + 12.5):
        cap = cap.cut(cyl(3.4, 10, sx, OUT_Y0 - 1, zc, V(0, 1, 0)))
# south: USB-A, face flush
x = CONN["J4"][4]
cap = cap.cut(cut_ns(x + 3.5 - 7.2, x + 3.5 + 7.2, Z_BT + 0.3, Z_BT + 7.6, SY, 1.0))
# east: USB-C face DE9_WALL behind the outer surface
yc = FP["USBC_Y"]
cap = cap.cut(kbox(IN_X1 - 0.01, OUT_X1 - DE9_WALL, yc - 5.5, yc + 5.5, Z_BT + 0.2, Z_BT + 3.6))
cap = cap.cut(fillet_along(kbox(OUT_X1 - DE9_WALL - 1, OUT_X1 + 1, yc - 4.8, yc + 4.8, Z_BT - 0.2, Z_BT + 3.4), 0, 1.4))
# the receptacle drops into the tray: its pocket opens up to the seam and through the tray lip
# (the face stays behind the wall)
cap = cap.cut(kbox(IN_X1 - 0.01, OUT_X1 - DE9_WALL, yc - 5.5, yc + 5.5, Z_BT + 0.2, Z_SPLIT + LIP_UP + LIP_CLR + 0.01))
# east: BOOT / RESET actuators
for sy in SW_STEM:
    cap = cap.cut(cyl(5.2, 10, OUT_X1 + 1, ky(sy), Z_BT + SW_STEM_Z, V(-1, 0, 0)))
    # the stem passes the tray lip on the way down: a notch in the lip above the hole
    cap = cap.cut(kbox(IN_X1 - 0.01, IN_X1 + LIP_T + LIP_CLR + 0.01, sy - 2.0, sy + 2.0, Z_BT + SW_STEM_Z, Z_SPLIT + LIP_UP + LIP_CLR + 0.01))
# lid: OLED window on the lit area, LED hole, encoder bushing hole
wx, wy, ww, wh = FP["OLED_WINDOW"]
win = kbox(wx - WIN_CLR, wx + ww + WIN_CLR, wy - WIN_CLR, wy + wh + WIN_CLR, CEIL - 1, Z_TOP + 1)
cap = cap.cut(win.makeFillet(1.0, [e for e in win.Edges if abs(e.tangentAt(0).z) > 0.99]))
lx, ly = FP["LED"]
cap = cap.cut(cyl(LED_HOLE, 40, lx, ky(ly), CEIL - 1))
cap = cap.cut(cyl(ENC_HOLE, 40, ENC[0], ky(ENC[1]), CEIL - 1))
if ENC_LUG: cap = cap.cut(cyl(ENC_LUG[2], 40, ENC[0] + ENC_LUG[0], ky(ENC[1] + ENC_LUG[1]), CEIL - 1))

# ---- debossed labels ----
def label(text, size, origin, xdir, ydir, into):
    """text centred at origin, reading along xdir, up along ydir, cut LABEL_DEPTH along 'into'"""
    chars = Part.makeWireString(text, FONT, size, 0)
    faces = [Part.makeFace(wires, "Part::FaceMakerBullseye") for wires in chars if wires]   # a space has no wires
    sh = Part.makeCompound(faces); bb = sh.BoundBox
    sh.translate(V(-(bb.XMin + bb.XMax) / 2, -(bb.YMin + bb.YMax) / 2, 0))
    zd = xdir.cross(ydir); m = FreeCAD.Matrix()
    m.A11, m.A21, m.A31 = xdir.x, xdir.y, xdir.z
    m.A12, m.A22, m.A32 = ydir.x, ydir.y, ydir.z
    m.A13, m.A23, m.A33 = zd.x, zd.y, zd.z
    sh = sh.transformGeometry(m); sh.translate(origin - into * 0.01)
    return Part.makeCompound([f.extrude(into * (LABEL_DEPTH + 0.01)) for f in sh.Faces])
X, Y, Z = V(1, 0, 0), V(0, 1, 0), V(0, 0, 1)
ZL = Z_PLATE_TOP + (Z_BT + 0.3 - Z_PLATE_TOP) / 2 + 0.3          # label line: the band below every opening, same on all walls
BOOT_SHIFT = 3.5                                               # BOOT sits off its button, away from the USB-C label
LS = 3.5                                                       # strokes about 0.65 mm: a 0.5 mm nozzle prints them as an inlay
labels = [label("PORT 1", LS, V(CONN["P1"][4] + 5.54, OUT_Y0, ZL), X, Z, Y),
          label("PORT 2", LS, V(CONN["P2"][4] + 5.54, OUT_Y0, ZL), X, Z, Y),
          label("USB", LS, V(CONN["J4"][4] + 3.5, OUT_Y0, ZL), X, Z, Y),
          label("HOST", LS, V(CONN["J10"][4], OUT_Y1, ZL), -X, Z, -Y),
          label("KEYB", LS, V(CONN["J12"][4], OUT_Y1, ZL), -X, Z, -Y),
          label("BOOT", LS, V(OUT_X1, ky(SW_STEM[0] - BOOT_SHIFT), ZL), Y, Z, -X),
          label("RST", LS, V(OUT_X1, ky(SW_STEM[1]), ZL), Y, Z, -X),
          label("USB-C", LS, V(OUT_X1, ky(yc), ZL), Y, Z, -X)]
for i, a in enumerate(labels):                                  # neighbours on a wall keep their distance
    for b in labels[i + 1:]:
        d = a.distToShape(b)[0]
        if d < 8: print("label gap %.2f mm" % d)
        assert d >= 1.5, "wall labels closer than 1.5 mm"
label_inlay = Part.makeCompound([s for l in labels for s in cap.common(l).Solids])
for l in labels: cap = cap.cut(l)

# ---- lid logo ----
def logo_dots():
    px, py, pp, pd = LOGO_PINS
    dots = [(px + (i - 2) * pp, py - pp / 2, pd) for i in range(5)] + [(px + (i - 1.5) * pp, py + pp / 2, pd) for i in range(4)]
    cols = sum(len(LOGO_FONT[c][0]) + 1 for c in LOGO_TEXT) - 1
    x = LOGO_TEXT_AT[0] - (cols - 1) * LOGO_PITCH / 2; y = LOGO_TEXT_AT[1] - 3 * LOGO_PITCH
    for c in LOGO_TEXT:
        for r, row in enumerate(LOGO_FONT[c]):
            dots += [(x + q * LOGO_PITCH, y + r * LOGO_PITCH, LOGO_DOT) for q, b in enumerate(row) if b == "1"]
        x += (len(LOGO_FONT[c][0]) + 1) * LOGO_PITCH
    return dots
logo_inlay = Part.makeCompound([cyl(d, LOGO_DEPTH, x, ky(y), Z_TOP - LOGO_DEPTH) for x, y, d in logo_dots()])
cap = cap.cut(Part.makeCompound([cyl(d, LOGO_DEPTH + 1, x, ky(y), Z_TOP - LOGO_DEPTH) for x, y, d in logo_dots()]))

# ---- standoffs, screw holes, foot recesses ----
for (cx, cy) in HOLES:
    cap = cap.fuse(cyl(7.0, STAND, cx, ky(cy), Z_PLATE_TOP))
    cap = cap.cut(cyl(SCREW_D, STAND + BASE_T + 1.1, cx, ky(cy), Z_PLATE_BOT - 1))
    cap = cap.cut(Part.makeCone(3.4, 6.4, 1.7, V(cx, ky(cy), Z_PLATE_BOT - 0.01), V(0, 0, -1)).mirror(V(cx, ky(cy), Z_PLATE_BOT), V(0, 0, 1)))
for fx in (OUT_X0 + FOOT_IN, OUT_X1 - FOOT_IN):
    for fy in (OUT_Y0 + FOOT_IN, OUT_Y1 - FOOT_IN):
        cap = cap.cut(cyl(FOOT_D, FOOT_H + 1, fx, fy, Z_PLATE_BOT - 1))

# ---- the split: tray below Z_SPLIT plus a lip on the inner wall, cap the rest ----
shell = cap
assert OVH - GAP - LIP_T - LIP_CLR >= 1.0, "cap wall outside the lip groove under 1 mm"
def lip_ring(grow, z0, z1):
    """the inner LIP_T of the wall (+ grow outward), corners following the outer radius"""
    off = OVH - GAP - LIP_T - grow                             # wall left outside the ring
    return rbox(OUT_X0 + off, OUT_X1 - off, OUT_Y0 + off, OUT_Y1 - off, z0, z1, CORNER_R - off).cut(
        box(IN_X0, IN_X1, IN_Y0, IN_Y1, z0 - 1, z1 + 1))
low = box(OUT_X0 - 1, OUT_X1 + 1, OUT_Y0 - 1, OUT_Y1 + 1, Z_PLATE_BOT - 1, Z_SPLIT)
tray = shell.common(low.fuse(lip_ring(0, Z_SPLIT - 0.01, Z_SPLIT + LIP_UP)))
cap = shell.cut(low.fuse(lip_ring(LIP_CLR, Z_SPLIT - 0.01, Z_SPLIT + LIP_UP + LIP_CLR))).fuse(pillars)
tray = tray.removeSplitter(); cap = cap.removeSplitter()

# ---- fit check: every footprint from the board, plus the bodies the footprints do not describe ----
THROUGH_WALL = {"J10", "J12", "P1", "P2", "J4", "J13", "SW1", "SW2"}      # modelled explicitly below
checks = []
for p in PARTS:
    if p["ref"] in THROUGH_WALL or p["ref"].startswith(("H", "JP")) or not p["h"]: continue
    x0, y0, x1, y1 = p["box"]
    checks.append((p["ref"], kbox(x0, x1, y0, y1, Z_BT, Z_BT + p["h"])))
for ref in ("J10", "J12"):
    x = CONN[ref][4]; checks.append((ref, kbox(x - 7.25, x + 7.25, -FP["MDIN_OVH"], 12.65 - FP["MDIN_OVH"], Z_BT + 0.3, Z_BT + 12.9)))
for ref in ("P1", "P2"):
    x = CONN[ref][4]; cx = x + 5.54
    checks.append((ref + " body", kbox(x + DE9_X0, x + DE9_X0 + DE9_W, H - 13.3, H + FP["DE9_FLANGE"], Z_BT + 0.3, Z_BT + 12.9)))
    checks.append((ref + " shell", kbox(cx - 8.3, cx + 8.3, H + FP["DE9_FLANGE"], H + FP["DE9_FLANGE"] + 6.0, Z_BT + 2.5, Z_BT + 10.5)))
x = CONN["J4"][4]; checks.append(("J4", kbox(x - 3.5, x + 10.5, H - 10, H + OVH, Z_BT + 0.3, Z_BT + 7.0)))
checks.append(("J13", kbox(W - 5.6, W + OVH - DE9_WALL, yc - 4.5, yc + 4.5, Z_BT + 0.2, Z_BT + 3.3)))
for ref, sy in zip(("SW1", "SW2"), SW_STEM):
    checks.append((ref + " body", kbox(W - 4.5, W - 0.2, sy - 3.2, sy + 3.2, Z_BT, Z_BT + 6.5)))
    checks.append((ref + " stem", kbox(W - 0.2, W + OVH, sy - 1.75, sy + 1.75, Z_BT + SW_STEM_Z - 1.75, Z_BT + SW_STEM_Z + 1.75)))
ox0, oy0 = FP["OLED_X0"], FP["OLED_Y0"]; pw, ph = FP["OLED_PCB"]
checks.append(("OLED pcb", kbox(ox0, ox0 + pw, oy0, oy0 + ph, FP["OLED_Z_PCB_BOTTOM"], FP["OLED_Z_PCB_BOTTOM"] + FP["OLED_PCB_T"])))
gx0, gy0 = FP["OLED_GLASS_X0"], FP["OLED_GLASS_Y0"]; gw, gh = FP["OLED_GLASS"]
checks.append(("OLED glass", kbox(gx0, gx0 + gw, gy0, gy0 + gh, FP["OLED_Z_PCB_BOTTOM"] + FP["OLED_PCB_T"], FP["OLED_Z_TOP"])))
for i, (hx, hy) in enumerate(FP["OLED_HOLES"]): checks.append(("OLED post %d" % i, cyl(4.0, FP["OLED_Z_PCB_BOTTOM"] - Z_BT, hx, ky(hy), Z_BT)))
checks.append(("encoder body", kbox(ENC[0] - ENC_BODY / 2, ENC[0] + ENC_BODY / 2, ENC[1] - ENC_BODY / 2, ENC[1] + ENC_BODY / 2, CEIL - ENC_BODY_H, CEIL - 0.01)))
checks.append(("encoder pins", kbox(ENC[0] - ENC_BODY / 2, ENC[0] + ENC_BODY / 2, ENC[1] - ENC_BODY / 2, ENC[1] + ENC_BODY / 2, CEIL - ENC_PINS_H, CEIL - ENC_BODY_H)))

report = ["outer %.1f x %.1f x %.1f mm (tray %.1f to the seam + cap %.1f); lid underside %.2f above the board underside, glass top %.2f, gap %.2f"
          % (OUT_X1 - OUT_X0, OUT_Y1 - OUT_Y0, Z_TOP - Z_PLATE_BOT, Z_SPLIT - Z_PLATE_BOT, Z_TOP - Z_SPLIT, CEIL, FP["OLED_Z_TOP"], CEIL - FP["OLED_Z_TOP"]),
          "tray valid %s (%d solid), cap valid %s (%d solid)" % (tray.isValid(), len(tray.Solids), cap.isValid(), len(cap.Solids)),
          "inlays: lid logo %d dots %.1f mm3 (in the cap %.2f), labels %d bodies %.1f mm3 (in the tray %.2f, in the cap %.2f)"
          % (len(logo_inlay.Solids), logo_inlay.Volume, cap.common(logo_inlay).Volume, len(label_inlay.Solids), label_inlay.Volume,
             tray.common(label_inlay).Volume, cap.common(label_inlay).Volume)]
hits = []
for name, shape in checks:
    for half, hs in (("cap", cap), ("tray", tray)):
        v = hs.common(shape).Volume
        if v > 1e-3: hits.append("%s vs %s %.2f mm3" % (name, half, v))
# assembly: the board and its parts drop straight down into the tray, then the cap comes down over
# them. a part's path is its box swept up (relative to the tray) or down (relative to the cap)
board_parts = [(n, s) for n, s in checks if not n.startswith("encoder")] + [("board", box(0, W, ky(H), 0, 0, BOARD_T))]
sweep_hits = []
for name, shape in board_parts:
    bb = shape.BoundBox
    up = box(bb.XMin, bb.XMax, bb.YMin, bb.YMax, bb.ZMin, Z_TOP + 10)
    down = box(bb.XMin, bb.XMax, bb.YMin, bb.YMax, Z_PLATE_BOT - 10, bb.ZMax)
    v = tray.common(up).Volume
    if v > 1e-3: sweep_hits.append("%s into the tray %.2f mm3" % (name, v))
    v = cap.common(down).Volume
    if v > 1e-3: sweep_hits.append("cap over %s %.2f mm3" % (name, v))
CORNER = 0.5     # mm3: a square stand-in box touching a rounded opening corner, not a real clash
real = [h for h in sweep_hits if float(h.split()[-2]) >= CORNER]
report.append("assembly: " + (", ".join(real) if real else "board drops into the tray, cap goes on: clear")
              + ("; corner contacts under %.1f mm3: %d" % (CORNER, len(sweep_hits) - len(real)) if len(sweep_hits) > len(real) else ""))
# the encoder body against every board part (it hangs 7 mm below the lid, parts up to 3.9 mm are fine)
enc = checks[-2][1].fuse(checks[-1][1])
for p in PARTS:
    if p["h"] and not p["ref"].startswith(("H", "JP")):
        x0, y0, x1, y1 = p["box"]; v = enc.common(kbox(x0, x1, y0, y1, Z_BT, Z_BT + p["h"])).Volume
        if v > 1e-3: hits.append("encoder vs %s %.2f mm3" % (p["ref"], v))
report.append("interference: " + (", ".join(hits) if hits else "none"))
print("\n".join(report))

# ---- document + exports ----
doc = FreeCAD.newDocument("ninepin_ux_case")
def add(name, shape, color):
    o = doc.addObject("Part::Feature", name); o.Shape = shape
    try: o.ViewObject.ShapeColor = color
    except Exception: pass
    return o
add("Cap", cap, (0.85, 0.85, 0.9)); add("Tray", tray, (0.6, 0.6, 0.65))
add("CapLogo", logo_inlay, (1.0, 0.6, 0.1)); add("TrayLabels", label_inlay, (1.0, 0.6, 0.1))
add("Board", box(0, W, ky(H), 0, 0, BOARD_T), (0.1, 0.45, 0.2))
add("Parts", Part.makeCompound([s for _, s in checks]), (0.8, 0.7, 0.2))
doc.recompute()
import MeshPart
for name, shape in (("cap", cap), ("tray", tray), ("cap-logo", logo_inlay), ("tray-labels", label_inlay)):
    MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.05, AngularDeflection=0.3).write(os.path.join(OUT_DIR, "ninepin-ux-case-%s.stl" % name))
Part.export([doc.getObject(n) for n in ("Cap", "Tray", "CapLogo", "TrayLabels")], os.path.join(OUT_DIR, "ninepin-ux-case.step"))
doc.saveAs(os.path.join(OUT_DIR, "ninepin-ux-case.FCStd"))
open(os.path.join(OUT_DIR, "fit-report.txt"), "w").write("\n".join(report) + "\n")
print("exported to", OUT_DIR)
