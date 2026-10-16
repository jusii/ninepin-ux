#!/usr/bin/env python3
"""structural checks for the rev7 board layout (ported from check_rev6_pcb.py).

these are the facts DRC cannot check on its own: where each connector sits
against the case wall, whether the RM2 antenna keep-out is really empty,
whether anything tall has crept under the OLED or above the case ceiling,
whether the supply rails really land in the Power netclass, whether the
planes are on the nets the pads use - and whether rev6 is still frozen.

every expected number comes from kicad/rev7/floorplan.py and placement.py,
imported here, so a floorplan change shows up as a checker result rather than
as silent drift between the generator and its gate.

kicad-cli and pcbnew only ever see a COPY of the rev7 project, staged into a
scratch directory outside the repo (pcb, pro, prl, lib tables, rev7.pretty,
and the schematic for the parity run), so nothing here can write a repo file.

the board is placed before it is routed.  sections that can only pass once
copper exists (unconnected items, the full-severity DRC, via/track widths)
fail closed until then, and are labelled "waiting for copper" so a run on an
unrouted board says what it is waiting for instead of crashing or passing.

usage (/nas is mounted noexec, so always via the interpreter):

    python3 tools/check_rev7_pcb.py [--work DIR] [--no-git] [board.kicad_pcb]

exit code 0 only when every assertion passes.
"""

from __future__ import annotations

import argparse
import collections
import fnmatch
import importlib.util
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

sys.dont_write_bytecode = True   # keep __pycache__ out of kicad/rev7

Pt = Tuple[float, float]
Poly = List[Pt]
BBox = Tuple[float, float, float, float]  # minx, miny, maxx, maxy

# --------------------------------------------------------------------------
# repo layout
# --------------------------------------------------------------------------

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(TOOLS_DIR)
REV7_DIR = os.path.join(REPO_ROOT, "kicad", "rev7")
BOARD_NAME = "amigahid-pico-rev7.kicad_pcb"
BOARD_REL = "kicad/rev7/" + BOARD_NAME
PROJECT_REL = "kicad/rev7/amigahid-pico-rev7.kicad_pro"
PRL_REL = "kicad/rev7/amigahid-pico-rev7.kicad_prl"
# what a scratch copy needs so kicad-cli and pcbnew behave exactly as in-tree.
# the schematic and its symbol library ride along for --schematic-parity.
STAGE_FILES = [
    BOARD_NAME,
    "amigahid-pico-rev7.kicad_pro",
    "amigahid-pico-rev7.kicad_prl",
    "fp-lib-table",
    "sym-lib-table",
    "amigahid-pico-rev7.kicad_sch",
    "rev7-symbols.kicad_sym",
]
STAGE_DIRS = ["rev7.pretty"]
KICAD_CLI = shutil.which("kicad-cli") or "/usr/bin/kicad-cli"

sys.path.insert(0, REV7_DIR)
import floorplan as F   # noqa: E402
import placement as PL  # noqa: E402

# --------------------------------------------------------------------------
# configuration (everything not derived from floorplan.py / placement.py)
# --------------------------------------------------------------------------

POS_TOL = 0.01     # mm: placed vs floorplan position
ROT_TOL = 0.01     # deg
KEEPOUT_TOUCH_EPS = 0.002   # copper abutting a keep-out boundary is flanking, not inside

# B: hole drills
M3_DRILL = (3.0, 3.5)
M2_DRILL = (2.0, 2.4)

# C: RM2 datasheet (RP-008943-DS-7 fig. 6) metal keep-out
RM2_KEEPOUT_DIMS = (18.5, 35.5)   # x (antenna axis, 13.5 beyond + 5.0 into the module) x y
RM2_REF = "U9"

# D: footprint-local face/edge geometry.  these restate the footprint facts the
# floorplan derives its origins from; the checker recomputes each face from
# the PLACED footprint (position + rotation) and compares it with the case rule.
MDIN_FACE_LOCAL = (0.0, -0.25)       # CUI MD-60S mating face
USBA_FACE_LOCAL = (0.0, 13.49)       # Molex 67643 mating face
DE9_NEAR_ROW_PADS = ["6", "7", "8", "9"]   # the row nearest the mating edge
DE9_EDGE_OFFSET = 7.70               # .318" family: near pin row behind the board edge
USBC_FACE_LOCAL = (0.0, 4.18)        # GCT USB4105 mating face
SW_TIP_LOCAL = (0.0, -5.85)          # PTS645Vx58 actuator tip
MDIN_REFS = ["J10", "J12"]
DE9_REFS = ["P1", "P2"]

# G: parts that may sit under the OLED module (matched against the footprint
# name, library prefix stripped).  a whitelist on purpose: an electrolytic, a
# header or a switch nobody thought to name must fail.
OLED_LOW_OK = [
    r"^R_0603_", r"^C_0603_", r"^L_0603_", r"^R_0805_", r"^C_0805_",
    r"^D_SOD-323$", r"^SSOP-24_", r"^R_Array_Convex_", r"^MSOP-10_",
    r"^SOT-23-6$", r"^SOT-23$", r"^D_SMA$", r"^MountingHole_2\.2mm_M2$", r"^WSON-6-",
]
OLED_LOW_OK_REFS = {"J3": r"^PinSocket_1x04_P2\.54mm_Vertical$"}   # the socket the module plugs into

# H: package heights above the top copper (mm), nominal max from the
# datasheets / KiCad 3D models.  unknown package = fail, never assume low.
PKG_HEIGHTS = [
    (r"^R_0603_|^C_0603_|^L_0603_", 0.55),
    (r"^R_0805_|^C_0805_", 1.35),
    (r"^R_Array_Convex_4x0603", 0.6),
    (r"^D_SOD-323$", 1.1),
    (r"^D_SOD-123$", 1.35),
    (r"^D_SOD-882$", 0.5),
    (r"^R_0402_", 0.4),
    (r"^C_0402_", 0.5),
    (r"^TSSOP-16_", 1.2),
    (r"^PinHeader_2x03_P2\.54mm_Vertical$", 8.5),
    (r"^R_Array_Convex_4x0402", 0.5),
    (r"^D_SMA$", 2.45),
    (r"^Fuse_1812_", 2.0),
    (r"^CP_Elec_6\.3x7\.7$", 8.0),
    (r"^SOT-23$", 1.25),
    (r"^SOT-23-6$", 1.45),
    (r"^WSON-6-", 0.8),
    (r"^QFN-60-", 0.9),
    (r"^SOIC-8_5\.3x5\.3mm", 2.16),
    (r"^MSOP-10_", 1.1),
    (r"^SSOP-24_", 1.75),
    (r"^Crystal_SMD_3225-", 0.9),
    (r"^L_Murata_DFE201610P$", 1.0),
    (r"^L_2016_AOTA$", 1.0),
    (r"^LED_WS2812B-2020_", 0.84),
    (r"^SolderJumper-", 0.05),
    (r"^MountingHole_", 0.0),
    (r"^PinHeader_1x0\d_P2\.54mm_Vertical$", 8.5),   # 2.5 body + 6.0 mating pin
    (r"^PinSocket_1x04_P2\.54mm_Vertical$", 8.5),
    (r"^MiniDIN6_PS2_Socket_CUI_MD-60S$", 13.0),
    (r"^DSUB-9_Pins_Horizontal_", 12.9),            # floorplan: DE9 14.5 above the underside
    (r"^USB_A_Molex_67643_", 7.1),
    (r"^USB_C_Receptacle_GCT_USB4105", 3.5),
    (r"^SW_Tactile_SPST_Angled_PTS645Vx58", 6.0),
    (r"^RaspberryPi_RM2$", 3.0),
]
# underside of the OLED pcb above the board's top copper: J3 socket + header plastic
OLED_PCB_BOTTOM = F.OLED_Z_TOP - F.BOARD_T - 1.0 - 1.7

# K: netclasses (as rev6)
POWER_NETCLASS = "Power"
POWER_NETCLASS_PATTERNS = [
    "/AMIGA_5V", "/AMIGA_5V_FUSED", "/AMIGA_IO5V", "/SYS5V", "/AUX_5V", "/USBA_5V", "/KBIN_5V",
]
POWER_NETCLASS_MIN_WIDTH = 0.6        # re-targeted to rev7's M2 copper floor (rev6: 1.0)
POWER_NETCLASS_MIN_CLEARANCE = 0.2  # rev7 routes the 5 V rails at 0.2 (rev6: 0.25)
NET_SETTINGS_MIN_VERSION = 3
V33_NETS = ["/+3V3", "+3V3"]          # must stay on Default, whichever spelling exists
# DRC rules that may never be excluded or set to ignore (rev6 list; rev6's
# hole_near_hole is hole_to_hole on KiCad 10).  F runs at --severity-error, so
# the ones F relies on must stay errors.
NEVER_IGNORE = [
    "annular_width", "clearance", "copper_edge_clearance", "courtyards_overlap",
    "drill_out_of_range", "hole_clearance", "hole_to_hole", "invalid_outline",
    "items_not_allowed", "malformed_courtyard", "padstack", "shorting_items",
    "through_hole_pad_without_hole", "track_width", "tracks_crossing",
    "unconnected_items", "zones_intersect",
]
MUST_BE_ERROR = [
    "annular_width", "clearance", "copper_edge_clearance", "courtyards_overlap",
    "drill_out_of_range", "hole_clearance", "invalid_outline", "items_not_allowed",
    "malformed_courtyard", "shorting_items", "through_hole_pad_without_hole",
    "track_width", "tracks_crossing", "unconnected_items", "zones_intersect",
]

# L: layer stack
COPPER_LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"]
EXPECTED_ZONES = {
    # name: (net, layers)
    "gnd_plane": ("GND", ["In1.Cu"]),
    "3v3_plane": ("/+3V3", ["In2.Cu"]),   # rev7 names the rail /+3V3 (a sheet net, not a power symbol)
    "gnd_top": ("GND", ["F.Cu"]),
    "gnd_bottom": ("GND", ["B.Cu"]),
}
# DRC violation types that only make sense to judge once copper is laid; any
# other type is a placement/silk/outline problem and is judged today.
COPPER_DEPENDENT_DRC = {
    "unconnected_items", "starved_thermal", "isolated_copper", "track_dangling",
    "via_dangling", "connection_width", "copper_sliver", "track_width",
    "via_diameter", "annular_width", "tracks_crossing", "track_angle",
    "track_segment_length", "too_many_vias", "length_out_of_range",
    "skew_out_of_range", "diff_pair_gap_out_of_range",
    "diff_pair_uncoupled_length_too_long", "track_not_centered_on_via",
}

# M: copper widths
VIA_MIN_DRILL = 0.3
VIA_MIN_DIA = 0.5                     # signal vias 0.5/0.3; Power-net vias 0.6/0.3 (POWER_VIA_MIN)
POWER_VIA_MIN = 0.6
POWER_VIA_EXCEPT = {(50.0, 46.6)}     # USBA_5V into U2 pin 5 between its rows: 0.5 is all that fits
POWER_TRACK_MIN = 0.3                 # rev7: 0.4 buses, 0.5 trunks, 0.3 short stubs (rev6: 1.0)
TRACK_MIN = 0.2

# N: rev6 freeze is check_rev7_netlist.py section I, imported verbatim
NETLIST_CHECKER = os.path.join(TOOLS_DIR, "check_rev7_netlist.py")

# --------------------------------------------------------------------------
# geometry (from check_rev6_pcb.py)
# --------------------------------------------------------------------------


def poly_bbox(pts: Sequence[Pt]) -> BBox:
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return (min(xs), min(ys), max(xs), max(ys))


def bbox_union(boxes: Sequence[BBox]) -> BBox:
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def bbox_overlap(a: BBox, b: BBox, slack: float = 0.0) -> bool:
    return not (a[2] + slack < b[0] or b[2] + slack < a[0]
                or a[3] + slack < b[1] or b[3] + slack < a[1])


def bbox_area_overlap(a: BBox, b: BBox) -> bool:
    """strict: the boxes share area, not just an edge."""
    return min(a[2], b[2]) - max(a[0], b[0]) > 1e-6 and min(a[3], b[3]) - max(a[1], b[1]) > 1e-6


def poly_area(pts: Sequence[Pt]) -> float:
    n = len(pts)
    if n < 3:
        return 0.0
    acc = 0.0
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        acc += x1 * y2 - x2 * y1
    return abs(acc) * 0.5


def point_in_poly(pt: Pt, poly: Sequence[Pt]) -> bool:
    x, y = pt
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            xint = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if xint > x:
                inside = not inside
    return inside


def _orient(a: Pt, b: Pt, c: Pt) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def segs_cross(a: Pt, b: Pt, c: Pt, d: Pt) -> bool:
    d1, d2 = _orient(c, d, a), _orient(c, d, b)
    d3, d4 = _orient(a, b, c), _orient(a, b, d)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return True
    eps = 1e-12

    def on_seg(p: Pt, q: Pt, r: Pt) -> bool:
        return (abs(_orient(p, q, r)) < eps
                and min(p[0], q[0]) - eps <= r[0] <= max(p[0], q[0]) + eps
                and min(p[1], q[1]) - eps <= r[1] <= max(p[1], q[1]) + eps)

    return on_seg(c, d, a) or on_seg(c, d, b) or on_seg(a, b, c) or on_seg(a, b, d)


def polys_overlap(a: Sequence[Pt], b: Sequence[Pt]) -> bool:
    if len(a) < 3 or len(b) < 3:
        return False
    if not bbox_overlap(poly_bbox(a), poly_bbox(b)):
        return False
    for p in a:
        if point_in_poly(p, b):
            return True
    for p in b:
        if point_in_poly(p, a):
            return True
    na, nb = len(a), len(b)
    for i in range(na):
        for j in range(nb):
            if segs_cross(a[i], a[(i + 1) % na], b[j], b[(j + 1) % nb]):
                return True
    return False


def shrink_poly(poly: Sequence[Pt], eps: float) -> Poly:
    if len(poly) < 3:
        return list(poly)
    cx = sum(p[0] for p in poly) / len(poly)
    cy = sum(p[1] for p in poly) / len(poly)
    out = []
    for x, y in poly:
        dx, dy = cx - x, cy - y
        d = math.hypot(dx, dy)
        out.append((cx, cy) if d <= eps else (x + dx / d * eps, y + dy / d * eps))
    return out


def rotate_kicad(dx: float, dy: float, angle_deg: float) -> Pt:
    """kicad's rotation: y grows downward, angles are counter-clockwise on screen."""
    a = math.radians(angle_deg)
    ca, sa = math.cos(a), math.sin(a)
    return (dx * ca + dy * sa, -dx * sa + dy * ca)


def rect_poly(x0: float, y0: float, x1: float, y1: float) -> Poly:
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def circle_poly(cx: float, cy: float, r: float, n: int = 24) -> Poly:
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n))
            for i in range(n)]


def capsule_polys(p1: Pt, p2: Pt, width: float) -> List[Poly]:
    r = width / 2.0
    dx, dy = p2[0] - p1[0], p2[1] - p1[1]
    ln = math.hypot(dx, dy)
    polys = [circle_poly(p1[0], p1[1], r, 12), circle_poly(p2[0], p2[1], r, 12)]
    if ln > 1e-9:
        ux, uy = -dy / ln * r, dx / ln * r
        polys.append([(p1[0] + ux, p1[1] + uy), (p2[0] + ux, p2[1] + uy),
                      (p2[0] - ux, p2[1] - uy), (p1[0] - ux, p1[1] - uy)])
    return polys


def tess_arc(start: Pt, mid: Pt, end: Pt, steps: int = 12) -> Poly:
    centre, r = arc_centre(start, mid, end)
    if centre is None:
        return [start, mid, end]
    ux, uy = centre

    def norm(x: float) -> float:
        while x <= -math.pi:
            x += 2 * math.pi
        while x > math.pi:
            x -= 2 * math.pi
        return x

    a0 = math.atan2(start[1] - uy, start[0] - ux)
    a1 = math.atan2(mid[1] - uy, mid[0] - ux)
    a2 = math.atan2(end[1] - uy, end[0] - ux)
    sweep = norm(a2 - a0)
    if norm(a1 - a0) * sweep < 0 or abs(norm(a1 - a0)) > abs(sweep):
        sweep = sweep - math.copysign(2 * math.pi, sweep)
    return [(ux + r * math.cos(a0 + sweep * i / steps), uy + r * math.sin(a0 + sweep * i / steps))
            for i in range(steps + 1)]


def arc_centre(start: Pt, mid: Pt, end: Pt) -> Tuple[Optional[Pt], float]:
    ax, ay = start
    bx, by = mid
    cx, cy = end
    d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    if abs(d) < 1e-12:
        return None, 0.0
    ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay)
          + (cx * cx + cy * cy) * (ay - by)) / d
    uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx)
          + (cx * cx + cy * cy) * (bx - ax)) / d
    return (ux, uy), math.hypot(ax - ux, ay - uy)


def rot_eq(a: float, b: float) -> bool:
    d = (a - b) % 360.0
    return min(d, 360.0 - d) <= ROT_TOL


# --------------------------------------------------------------------------
# model (pcbnew only: every section needs kicad's own geometry)
# --------------------------------------------------------------------------


@dataclass
class Pad:
    ref: str
    number: str
    net: str
    pos: Pt
    polys: List[Poly]
    is_npth: bool
    drill: float
    on_copper: bool


@dataclass
class Fp:
    ref: str
    value: str
    fpname: str          # footprint name, library prefix stripped
    flipped: bool
    layer: str
    pos: Pt
    rot: float
    courtyard: Optional[BBox]
    body: BBox
    pads: List[Pad] = field(default_factory=list)

    def extent(self) -> BBox:
        return self.courtyard if self.courtyard else self.body

    def pad(self, number: str) -> List[Pad]:
        return [p for p in self.pads if p.number == number]


@dataclass
class Track:
    net: str
    layer: str
    width: float
    drill: float
    start: Pt
    end: Pt
    is_via: bool
    polys: List[Poly]


@dataclass
class ZoneM:
    name: str
    is_rule: bool
    layers: List[str]
    net: str
    outline: Poly
    keepout: Dict[str, bool]
    fills: Dict[str, List[Poly]]
    parent: str


@dataclass
class EdgeItem:
    kind: str           # "line" | "arc" | other
    closed: bool
    pts: Poly
    centre: Optional[Pt] = None
    radius: float = 0.0


@dataclass
class Model:
    copper_layers: List[str]
    edges: List[EdgeItem]
    fps: List[Fp]
    tracks: List[Track]
    zones: List[ZoneM]
    copper_gfx: List[Tuple[str, Poly]]
    netclass: Dict[str, str]
    pad_nets: Dict[str, int]
    native: object = None

    def by_ref(self, ref: str) -> Optional[Fp]:
        for f in self.fps:
            if f.ref == ref:
                return f
        return None


def _chain_to_pts(chain, scale: float = 1e-6) -> Poly:
    return [(chain.CPoint(i).x * scale, chain.CPoint(i).y * scale) for i in range(chain.PointCount())]


def _polyset_outers(ps, scale: float = 1e-6) -> List[Poly]:
    return [_chain_to_pts(ps.Outline(i), scale) for i in range(ps.OutlineCount())]


def load_model(path: str) -> Model:
    import pcbnew

    MM = 1e6
    board = pcbnew.LoadBoard(path)
    if board is None:
        raise RuntimeError("pcbnew could not load %s" % path)
    copper = [board.GetLayerName(l) for l in board.GetEnabledLayers().CuStack()]

    # fill every zone in memory (never saved) so the keep-out test sees the
    # copper the fab would get, not whatever fill state the file happens to hold
    try:
        pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    except Exception as exc:  # noqa: BLE001
        print("warning: in-memory zone fill failed (%s); using fills from the file" % exc, file=sys.stderr)

    edges: List[EdgeItem] = []
    copper_gfx: List[Tuple[str, Poly]] = []

    def collect_gfx(item) -> None:
        lname = board.GetLayerName(item.GetLayer())
        if item.GetClass() in ("PCB_TEXT", "PCB_TEXTBOX", "PCB_FIELD"):
            return
        if lname == "Edge.Cuts":
            st = item.GetShapeStr()
            s = (item.GetStart().x / MM, item.GetStart().y / MM)
            e = (item.GetEnd().x / MM, item.GetEnd().y / MM)
            if st == "Line":
                edges.append(EdgeItem("line", False, [s, e]))
            elif st == "Arc":
                m = (item.GetArcMid().x / MM, item.GetArcMid().y / MM)
                c = item.GetCenter()
                edges.append(EdgeItem("arc", False, tess_arc(s, m, e), (c.x / MM, c.y / MM),
                                      item.GetRadius() / MM))
            elif st == "Circle":
                r = math.hypot(e[0] - s[0], e[1] - s[1])
                edges.append(EdgeItem("circle", True, circle_poly(s[0], s[1], r, 32), s, r))
            elif st == "Rect":
                edges.append(EdgeItem("rect", True, [s, (e[0], s[1]), e, (s[0], e[1])]))
            else:
                outs = _polyset_outers(item.GetPolyShape()) if st == "Polygon" else []
                edges.append(EdgeItem(st.lower(), True, outs[0] if outs else []))
            return
        if lname in copper:
            ps = pcbnew.SHAPE_POLY_SET()
            try:
                item.TransformShapeToPolygon(ps, item.GetLayer(), 0, 5000, pcbnew.ERROR_OUTSIDE)
                for o in _polyset_outers(ps):
                    copper_gfx.append((lname, o))
            except Exception:  # noqa: BLE001
                pass

    for d in board.GetDrawings():
        collect_gfx(d)

    fps: List[Fp] = []
    pad_nets: Dict[str, int] = collections.Counter()
    for f in board.GetFootprints():
        ref = f.GetReference()
        pads: List[Pad] = []
        boxes: List[BBox] = []
        for p in f.Pads():
            polys: List[Poly] = []
            on_copper = False
            for cl in copper:
                lid = board.GetLayerID(cl)
                if not p.IsOnLayer(lid):
                    continue
                on_copper = True
                try:
                    ps2 = pcbnew.SHAPE_POLY_SET()
                    p.TransformShapeToPolygon(ps2, lid, 0, 5000, pcbnew.ERROR_OUTSIDE)
                    polys.extend(_polyset_outers(ps2))
                except Exception:  # noqa: BLE001
                    pass
            pos = (p.GetPosition().x / MM, p.GetPosition().y / MM)
            if not polys:
                hx, hy = p.GetSizeX() / MM / 2, p.GetSizeY() / MM / 2
                polys = [rect_poly(pos[0] - hx, pos[1] - hy, pos[0] + hx, pos[1] + hy)]
            npth = p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH
            net = p.GetNetname()
            if net:
                pad_nets[net] += 1
            pads.append(Pad(ref, p.GetNumber(), net, pos, polys, npth,
                            max(p.GetDrillSizeX(), p.GetDrillSizeY()) / MM, on_copper and not npth))
            boxes.extend(poly_bbox(pl) for pl in polys)
        for g in f.GraphicalItems():
            collect_gfx(g)
        cy: Optional[BBox] = None
        for lid in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            c = f.GetCourtyard(lid)
            if c.OutlineCount():
                bb = c.BBox()
                cy = (bb.GetX() / MM, bb.GetY() / MM, (bb.GetX() + bb.GetWidth()) / MM,
                      (bb.GetY() + bb.GetHeight()) / MM)
                break
        pos = (f.GetPosition().x / MM, f.GetPosition().y / MM)
        fps.append(Fp(ref, f.GetValue(), f.GetFPIDAsString().rsplit(":", 1)[-1], bool(f.IsFlipped()),
                      board.GetLayerName(f.GetLayer()), pos, f.GetOrientationDegrees(), cy,
                      bbox_union(boxes) if boxes else (pos[0], pos[1], pos[0], pos[1]), pads))

    tracks: List[Track] = []
    for t in board.GetTracks():
        s = (t.GetStart().x / MM, t.GetStart().y / MM)
        e = (t.GetEnd().x / MM, t.GetEnd().y / MM)
        w = t.GetWidth() / MM
        if t.GetClass() == "PCB_VIA":
            tracks.append(Track(t.GetNetname(), "via", w, t.GetDrillValue() / MM, s, e, True,
                                [circle_poly(s[0], s[1], w / 2.0, 16)]))
        else:
            tracks.append(Track(t.GetNetname(), board.GetLayerName(t.GetLayer()), w, 0.0, s, e, False,
                                capsule_polys(s, e, w)))

    zones: List[ZoneM] = []

    def add_zone(z, parent: str) -> None:
        lnames = [board.GetLayerName(l) for l in z.GetLayerSet().Seq()]
        outline = _polyset_outers(z.Outline())
        keep = {
            "tracks": bool(z.GetDoNotAllowTracks()), "vias": bool(z.GetDoNotAllowVias()),
            "pads": bool(z.GetDoNotAllowPads()), "copperpour": bool(z.GetDoNotAllowZoneFills()),
            "footprints": bool(z.GetDoNotAllowFootprints()),
        }
        fills: Dict[str, List[Poly]] = {}
        if not z.GetIsRuleArea():
            for ln in lnames:
                if ln in copper:
                    try:
                        fills[ln] = _polyset_outers(z.GetFilledPolysList(board.GetLayerID(ln)))
                    except Exception:  # noqa: BLE001
                        pass
        zones.append(ZoneM(z.GetZoneName(), bool(z.GetIsRuleArea()), lnames, z.GetNetname(),
                           outline[0] if outline else [], keep, fills, parent))

    for z in board.Zones():
        add_zone(z, "")
    for f in board.GetFootprints():
        for z in list(f.Zones()):
            add_zone(z, f.GetReference())

    netclass: Dict[str, str] = {}
    info = board.GetNetInfo()
    for code in range(info.GetNetCount()):
        item = info.GetNetItem(code)
        if item is not None and item.GetNetname():
            netclass[item.GetNetname()] = item.GetNetClassName()

    return Model(copper, edges, fps, tracks, zones, copper_gfx, netclass, dict(pad_nets), board)


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------


WAIT = "waiting for copper"


class Report:
    def __init__(self) -> None:
        self.results: List[Tuple[str, str, str, bool, List[str], bool]] = []
        self.section = ""

    def sec(self, name: str) -> None:
        self.section = name

    def add(self, ident: str, title: str, ok: bool, detail: Optional[List[str]] = None,
            waiting: bool = False) -> bool:
        self.results.append((self.section, ident, title, bool(ok), list(detail or []), waiting and not ok))
        return ok

    def check(self, ident: str, title: str, ok: bool, fail_msg: str = "", waiting: bool = False) -> bool:
        return self.add(ident, title, ok, [] if ok else [fail_msg], waiting)

    @property
    def failures(self):
        return [r for r in self.results if not r[3]]

    def emit(self) -> None:
        order: List[str] = []
        for r in self.results:
            if r[0] not in order:
                order.append(r[0])
        for sec in order:
            rows = [r for r in self.results if r[0] == sec]
            bad = [r for r in rows if not r[3]]
            if not bad:
                state = "PASS"
            elif all(r[5] for r in bad):
                state = "WAIT"
            else:
                state = "FAIL"
            print("[%s] %s  (%d/%d)" % (state, sec, len(rows) - len(bad), len(rows)))
            for _sec, ident, title, ok, detail, waiting in rows:
                print("  %-5s %-4s %s%s" % (ident, "PASS" if ok else "FAIL", title, "  [%s]" % WAIT if waiting else ""))
                for line in detail:
                    print("             %s" % line)
            print()
        failed = self.failures
        waiting = [r for r in failed if r[5]]
        real = [r for r in failed if not r[5]]
        print("summary: %d assertions, %d passed, %d FAILED (%d %s, %d structural)"
              % (len(self.results), len(self.results) - len(failed), len(failed), len(waiting), WAIT, len(real)))
        if waiting:
            print("waiting for copper : %s" % " ".join(r[1] for r in waiting))
        if real:
            print("structural failures: %s" % " ".join(r[1] for r in real))
        print("RESULT: %s" % ("PASS" if not failed else "FAIL"))


# --------------------------------------------------------------------------
# scratch staging and kicad-cli
# --------------------------------------------------------------------------


def stage(src_dir: str, board_path: str, dest: str) -> str:
    """copy the project into dest so kicad-cli / pcbnew can never write the repo."""
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    for name in STAGE_FILES:
        p = os.path.join(src_dir, name)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(dest, name))
    for name in STAGE_DIRS:
        p = os.path.join(src_dir, name)
        if os.path.isdir(p):
            shutil.copytree(p, os.path.join(dest, name))
    # a non-default board path still lands under the project's board name
    shutil.copy2(board_path, os.path.join(dest, BOARD_NAME))
    return os.path.join(dest, BOARD_NAME)


def run_drc(board_copy: str, extra: List[str]) -> Tuple[Optional[dict], str]:
    out = os.path.join(os.path.dirname(board_copy), "drc.json")
    cmd = [KICAD_CLI, "pcb", "drc", "--format", "json", "-o", out] + extra + [board_copy]
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=600,
                             cwd=os.path.dirname(board_copy))
    except (OSError, subprocess.SubprocessError) as exc:
        return None, "cannot run kicad-cli: %s" % exc
    if not os.path.exists(out):
        return None, "kicad-cli wrote no report (rc %d): %s" % (res.returncode, (res.stdout + res.stderr).strip()[-300:])
    try:
        with open(out) as fh:
            return json.load(fh), " ".join(cmd[1:])
    except (OSError, ValueError) as exc:
        return None, "cannot read DRC report: %s" % exc


def drc_summary(items: Sequence[dict], limit: int = 12) -> List[str]:
    c = collections.Counter((v.get("type"), v.get("severity")) for v in items)
    return ["%4d x %s (%s)" % (n, t, s) for (t, s), n in c.most_common(limit)]


def drc_examples(items: Sequence[dict], limit: int = 6) -> List[str]:
    out = []
    for v in items[:limit]:
        where = "; ".join(i.get("description", "") for i in v.get("items", [])[:2])
        out.append("e.g. %s: %s -- %s" % (v.get("type"), v.get("description"), where))
    return out


# --------------------------------------------------------------------------
# outline
# --------------------------------------------------------------------------


@dataclass
class Outline:
    outer: Optional[Poly]
    open_chains: List[Poly]
    cutouts: List[Poly]
    bbox: Optional[BBox]


def build_outline(model: Model, tol: float = 0.01) -> Outline:
    closed = [e.pts for e in model.edges if e.closed and e.pts]
    pool = [list(e.pts) for e in model.edges if not e.closed]
    chains: List[Poly] = []
    unresolved: List[Poly] = []
    while pool:
        cur = pool.pop()
        changed = True
        while changed:
            changed = False
            for i, cand in enumerate(pool):
                if math.dist(cur[-1], cand[0]) <= tol:
                    cur = cur + cand[1:]
                elif math.dist(cur[-1], cand[-1]) <= tol:
                    cur = cur + list(reversed(cand))[1:]
                elif math.dist(cur[0], cand[-1]) <= tol:
                    cur = cand[:-1] + cur
                elif math.dist(cur[0], cand[0]) <= tol:
                    cur = list(reversed(cand))[:-1] + cur
                else:
                    continue
                pool.pop(i)
                changed = True
                break
        if len(cur) >= 3 and math.dist(cur[0], cur[-1]) <= tol:
            chains.append(cur[:-1])
        else:
            unresolved.append(cur)
    allc = chains + closed
    if not allc:
        return Outline(None, unresolved, [], None)
    areas = [poly_area(c) for c in allc]
    idx = areas.index(max(areas))
    outer = allc[idx]
    return Outline(outer, unresolved, [c for i, c in enumerate(allc) if i != idx], poly_bbox(outer))


# --------------------------------------------------------------------------
# A. outline
# --------------------------------------------------------------------------


def check_a(rep: Report, model: Model, ol: Outline) -> None:
    rep.sec("A. outline envelope %g x %g, R%g corners" % (F.W, F.H, F.CORNER_R))
    detail = ["Edge.Cuts chain does not close: %d points, ends %.3f,%.3f .. %.3f,%.3f"
              % (len(c), c[0][0], c[0][1], c[-1][0], c[-1][1]) for c in ol.open_chains]
    if ol.outer is None:
        detail.append("no closed Edge.Cuts chain at all")
    if ol.cutouts:
        detail.append("%d extra closed Edge.Cuts chain(s) (cutouts) - the floorplan has none" % len(ol.cutouts))
    rep.add("A1", "exactly one closed Edge.Cuts chain, no cutouts", not detail, detail)

    want = (0.0, 0.0, F.W, F.H)
    if ol.bbox:
        ok = all(abs(a - b) <= POS_TOL for a, b in zip(ol.bbox, want))
        rep.check("A2", "outline bbox is (0,0)-(%g,%g)" % (F.W, F.H), ok,
                  "outline bbox %.3f,%.3f - %.3f,%.3f (%.3f x %.3f)"
                  % (ol.bbox + (ol.bbox[2] - ol.bbox[0], ol.bbox[3] - ol.bbox[1])))
    else:
        rep.check("A2", "outline bbox is (0,0)-(%g,%g)" % (F.W, F.H), False, "no outline")

    R = F.CORNER_R
    want_c = [(R, R), (F.W - R, R), (R, F.H - R), (F.W - R, F.H - R)]
    arcs = [e for e in model.edges if e.kind == "arc"]
    bad = []
    for c in want_c:
        hit = [a for a in arcs if a.centre and math.dist(a.centre, c) <= POS_TOL and abs(a.radius - R) <= POS_TOL]
        if len(hit) != 1:
            bad.append("corner centred %.2f,%.2f has %d arc(s) of R%g" % (c[0], c[1], len(hit), R))
    if len(arcs) != 4:
        bad.append("%d Edge.Cuts arcs, expected 4" % len(arcs))
    others = [e.kind for e in model.edges if e.kind not in ("arc", "line")]
    if others:
        bad.append("unexpected Edge.Cuts shapes: %s" % ", ".join(others))
    rep.add("A3", "four R%g corner arcs at the floorplan corners" % R, not bad, bad)


# --------------------------------------------------------------------------
# B. holes
# --------------------------------------------------------------------------


def _holes(model: Model, lo: float, hi: float) -> List[Tuple[str, Pt, float]]:
    out = []
    for f in model.fps:
        if not f.fpname.startswith("MountingHole"):
            continue
        for p in f.pads:
            if p.is_npth and lo <= p.drill <= hi:
                out.append((f.ref, p.pos, p.drill))
    return out


def check_b(rep: Report, model: Model) -> None:
    rep.sec("B. M3 corner holes and M2 OLED posts")
    for ident, label, want, rng in (("B1", "M3", F.HOLES, M3_DRILL), ("B2", "M2 OLED post", F.OLED_HOLES, M2_DRILL)):
        have = _holes(model, *rng)
        bad = []
        for (x, y) in want:
            hit = [h for h in have if math.dist(h[1], (x, y)) <= POS_TOL]
            if len(hit) != 1:
                bad.append("%s hole at %.2f,%.2f: found %d" % (label, x, y, len(hit)))
        strays = [h for h in have if not any(math.dist(h[1], w) <= POS_TOL for w in want)]
        bad += ["%s %s d=%.2f at %.2f,%.2f is not a floorplan hole" % (label, h[0], h[2], h[1][0], h[1][1]) for h in strays]
        rep.add(ident, "%d %s NPTH holes (d %g-%g) exactly at the floorplan positions"
                % (len(want), label, rng[0], rng[1]), not bad, bad)
    mh = [f.ref for f in model.fps if f.fpname.startswith("MountingHole")]
    n = len(F.HOLES) + len(F.OLED_HOLES)
    rep.check("B3", "exactly %d MountingHole footprints" % n, len(mh) == n,
              "found %d: %s" % (len(mh), " ".join(sorted(mh))))


# --------------------------------------------------------------------------
# C. RM2 antenna keep-out
# --------------------------------------------------------------------------


def _copper_in(model: Model, area: Poly, own_ref: Optional[str], fills: bool) -> List[str]:
    zb = poly_bbox(area)
    out: List[str] = []
    for t in model.tracks:
        if any(bbox_overlap(poly_bbox(p), zb) and polys_overlap(p, area) for p in t.polys):
            out.append("%s on %r at %.2f,%.2f" % ("via" if t.is_via else "track " + t.layer, t.net, t.start[0], t.start[1]))
    for f in model.fps:
        if f.ref == own_ref:
            continue
        for p in f.pads:
            if any(bbox_overlap(poly_bbox(q), zb) and polys_overlap(q, area) for q in p.polys):
                out.append("pad %s/%s%s at %.2f,%.2f" % (p.ref, p.number, " (NPTH)" if p.is_npth else "", p.pos[0], p.pos[1]))
    if fills:
        for z in model.zones:
            if z.is_rule:
                continue
            for ln, polys in z.fills.items():
                if any(bbox_overlap(poly_bbox(q), zb) and polys_overlap(q, area) for q in polys):
                    out.append("fill of zone %r on %s" % (z.name or z.net, ln))
        for ln, poly in model.copper_gfx:
            if bbox_overlap(poly_bbox(poly), zb) and polys_overlap(poly, area):
                out.append("copper graphic on %s" % ln)
    return sorted(set(out))


def check_c(rep: Report, model: Model, ol: Outline) -> None:
    rep.sec("C. RM2 antenna keep-out")
    kx0, ky0, kx1, ky1 = F.RM2_KEEPOUT
    dims = (kx1 - kx0, ky1 - ky0)
    bad = []
    if any(abs(a - b) > POS_TOL for a, b in zip(dims, RM2_KEEPOUT_DIMS)):
        bad.append("floorplan RM2_KEEPOUT is %.2f x %.2f, datasheet says %.1f x %.1f" % (dims + RM2_KEEPOUT_DIMS))
    u9 = model.by_ref(RM2_REF)
    if u9 is None:
        bad.append("%s (RM2) is missing" % RM2_REF)
    else:
        if math.dist(u9.pos, F.RM2_CENTRE) > POS_TOL or not rot_eq(u9.rot, F.RM2_ROT):
            bad.append("%s at %.2f,%.2f rot %g, floorplan %.2f,%.2f rot %g"
                       % (RM2_REF, u9.pos[0], u9.pos[1], u9.rot, F.RM2_CENTRE[0], F.RM2_CENTRE[1], F.RM2_ROT))
        if abs(kx0 + 13.5 - F.RM2_X0) > POS_TOL or ol.bbox is None or F.RM2_X0 - ol.bbox[0] > 0.5:
            bad.append("antenna edge x=%.2f is not on the west board edge" % F.RM2_X0)
    rep.add("C0", "RM2 at the floorplan centre/rotation, antenna on the west edge, keep-out %g x %g"
            % RM2_KEEPOUT_DIMS, not bad, bad)

    # C1: the rule area
    want = (max(kx0, ol.bbox[0] if ol.bbox else 0.0), ky0, kx1, ky1)
    cands = []
    notes = []
    for z in model.zones:
        if not z.is_rule or not z.outline:
            continue
        b = poly_bbox(z.outline)
        if not bbox_overlap(b, want):
            continue
        why = []
        if any(abs(a - c) > POS_TOL for a, c in zip(b, want)):
            why.append("covers %.2f,%.2f-%.2f,%.2f, want %.2f,%.2f-%.2f,%.2f" % (b + want))
        if abs(poly_area(z.outline) - (want[2] - want[0]) * (want[3] - want[1])) > 0.01:
            why.append("is not a plain rectangle")
        miss = [l for l in COPPER_LAYERS if l not in z.layers]
        if miss:
            why.append("not on " + ",".join(miss))
        allows = [k for k in ("tracks", "vias", "pads", "copperpour") if not z.keepout.get(k)]
        if allows:
            why.append("allows " + "+".join(allows))
        if why:
            notes.append("rule area %r: %s" % (z.name or "(unnamed)", "; ".join(why)))
        else:
            cands.append(z)
    if not cands and not notes:
        notes.append("no rule area overlaps floorplan.RM2_KEEPOUT")
    rep.add("C1", "rule area on all 4 copper layers = RM2_KEEPOUT clipped to the board, no tracks/vias/pads/pour",
            bool(cands), [] if cands else notes)

    # C2: nothing on copper inside the on-board keep-out (fills refilled in memory)
    area = shrink_poly(rect_poly(*want), KEEPOUT_TOUCH_EPS)
    off = _copper_in(model, area, RM2_REF, fills=True)
    rep.add("C2", "no copper (tracks, vias, other pads, zone fill, graphics) inside the on-board keep-out",
            not off, off[:20] + (["... %d more" % (len(off) - 20)] if len(off) > 20 else []))

    # C3: datasheet rule, the full 35.5 x 18.5 rectangle, off-board part included
    full = shrink_poly(rect_poly(kx0, ky0, kx1, ky1), KEEPOUT_TOUCH_EPS)
    off = _copper_in(model, full, RM2_REF, fills=False)
    rep.add("C3", "no pad/via/track of any other footprint in the full %g x %g datasheet keep-out"
            % RM2_KEEPOUT_DIMS, not off, off[:20])


# --------------------------------------------------------------------------
# D. edge assignments
# --------------------------------------------------------------------------


def _local(f: Fp, local: Pt) -> Pt:
    dx, dy = rotate_kicad(local[0], local[1], f.rot)
    return (f.pos[0] + dx, f.pos[1] + dy)


def check_d(rep: Report, model: Model) -> None:
    rep.sec("D. connectors against the case wall")
    placed = list(F.CONNECTORS) + [F.J3]
    bad = []
    for ref, _val, _lib, fpname, x, y, rot, _note in placed:
        f = model.by_ref(ref)
        if f is None:
            bad.append("%s missing" % ref)
            continue
        if f.fpname != fpname:
            bad.append("%s is %s, floorplan says %s" % (ref, f.fpname, fpname))
        if math.dist(f.pos, (x, y)) > POS_TOL or not rot_eq(f.rot, rot):
            bad.append("%s at %.3f,%.3f rot %g, floorplan %.3f,%.3f rot %g" % (ref, f.pos[0], f.pos[1], f.rot, x, y, rot))
    rep.add("D1", "%d floorplan parts (connectors, switches, LED, J3) at floorplan position/rotation/footprint"
            % len(placed), not bad, bad)

    def face_check(ident: str, title: str, refs: Sequence[str], local: Pt, axis: int, want: float) -> None:
        bad = []
        for ref in refs:
            f = model.by_ref(ref)
            if f is None:
                bad.append("%s missing" % ref)
                continue
            got = _local(f, local)[axis]
            if abs(got - want) > POS_TOL:
                bad.append("%s face at %s=%.3f, case rule wants %.3f" % (ref, "xy"[axis], got, want))
        rep.add(ident, title, not bad, bad)

    face_check("D2", "mini-DIN faces at y=%.2f (overhang MDIN_OVH %.2f)" % (-F.MDIN_OVH, F.MDIN_OVH),
               MDIN_REFS, MDIN_FACE_LOCAL, 1, -F.MDIN_OVH)
    face_check("D3", "USB-A face at y=%.2f (overhang OVH %.2f)" % (F.H + F.OVH, F.OVH),
               ["J4"], USBA_FACE_LOCAL, 1, F.H + F.OVH)

    bad = []
    for ref in DE9_REFS:
        f = model.by_ref(ref)
        if f is None:
            bad.append("%s missing" % ref)
            continue
        for n in DE9_NEAR_ROW_PADS:
            ps = f.pad(n)
            if len(ps) != 1:
                bad.append("%s has %d pad(s) %s" % (ref, len(ps), n))
            elif abs(ps[0].pos[1] - (F.H - DE9_EDGE_OFFSET)) > POS_TOL:
                bad.append("%s pin %s row at y=%.3f, %.2f behind the edge is %.3f"
                           % (ref, n, ps[0].pos[1], DE9_EDGE_OFFSET, F.H - DE9_EDGE_OFFSET))
        out = [p.number for p in f.pads if p.pos[1] >= F.H]
        if out:
            bad.append("%s pads past the board edge: %s" % (ref, out))
    rep.add("D4", "DE9 near pin row %.2f mm behind the south edge (y=%.2f)" % (DE9_EDGE_OFFSET, F.H - DE9_EDGE_OFFSET),
            not bad, bad)

    face_check("D5", "USB-C face at x=%.2f (USBC_X: DE9_WALL %.1f behind the outer wall)"
               % (F.USBC_X + USBC_FACE_LOCAL[1], F.DE9_WALL),
               ["J13"], USBC_FACE_LOCAL, 0, F.W + F.OVH - F.DE9_WALL)

    bad = []
    for i, ref in enumerate(("SW1", "SW2")):
        f = model.by_ref(ref)
        if f is None:
            bad.append("%s missing" % ref)
            continue
        if abs(f.pos[0] - F.SW_X) > POS_TOL or abs(f.pos[1] - F.SW_Y[i]) > POS_TOL:
            bad.append("%s at %.3f,%.3f, floorplan SW_X/SW_Y %.3f,%.3f" % (ref, f.pos[0], f.pos[1], F.SW_X, F.SW_Y[i]))
        tip = _local(f, SW_TIP_LOCAL)[0]
        if abs(tip - (F.W + F.OVH)) > POS_TOL:
            bad.append("%s actuator tip at x=%.3f, outer wall at %.3f" % (ref, tip, F.W + F.OVH))
    rep.add("D6", "BOOT/RESET at SW_X=%.2f, actuator tips flush with the outer wall x=%.2f" % (F.SW_X, F.W + F.OVH),
            not bad, bad)


# --------------------------------------------------------------------------
# E. side and placement
# --------------------------------------------------------------------------


def check_e(rep: Report, model: Model, ol: Outline) -> None:
    rep.sec("E. all parts top side, placed, none parked")
    bad = ["%s is %s on %s" % (f.ref, "flipped" if f.flipped else "placed", f.layer)
           for f in model.fps if f.flipped or f.layer != "F.Cu"]
    rep.add("E1", "all %d footprints on F.Cu (top side)" % len(model.fps), not bad, sorted(bad))

    bad = []
    if ol.outer is None:
        bad.append("no outline")
    else:
        for f in model.fps:
            if not bbox_area_overlap(f.extent(), ol.bbox):
                bad.append("%s at %.2f,%.2f lies entirely off the board" % (f.ref, f.pos[0], f.pos[1]))
                continue
            outside = [p.number for p in f.pads if not point_in_poly(p.pos, ol.outer)]
            if outside:
                bad.append("%s pad(s) %s outside the outline" % (f.ref, ",".join(outside)))
    rep.add("E2", "no footprint parked outside the outline, every pad centre inside it", not bad, sorted(bad))

    # E3: everything accounted for and where placement.py says
    conn = {c[0] for c in F.CONNECTORS} | {F.J3[0]}
    holes = {"H%d" % (i + 1) for i in range(len(F.HOLES) + len(F.OLED_HOLES))}
    bad = []
    tvs_seen = 0
    for f in model.fps:
        if f.ref in conn or f.ref in holes:
            continue
        if f.ref in PL.P:
            x, y, rot = PL.P[f.ref]
            if math.dist(f.pos, (x, y)) > POS_TOL or not rot_eq(f.rot, rot):
                bad.append("%s at %.3f,%.3f rot %g, placement.py %.3f,%.3f rot %g" % (f.ref, f.pos[0], f.pos[1], f.rot, x, y, rot))
            continue
        if f.value.startswith("PESD5V0S1"):      # SOD-323 BA and SOD-882 UL clamps
            p1 = f.pad("1")
            net = p1[0].net.lstrip("/") if p1 else ""
            if net not in PL.TVS_ROWS:
                bad.append("TVS %s on net %r has no TVS_ROWS slot" % (f.ref, net))
                continue
            x, y, rot = PL.TVS_ROWS[net]
            tvs_seen += 1
            if math.dist(f.pos, (x, y)) > POS_TOL or not rot_eq(f.rot, rot):
                bad.append("TVS %s (%s) at %.3f,%.3f, TVS_ROWS %.3f,%.3f" % (f.ref, net, f.pos[0], f.pos[1], x, y))
            continue
        bad.append("%s (%s) is in neither floorplan.py nor placement.py - parked?" % (f.ref, f.fpname))
    missing = [r for r in PL.P if model.by_ref(r) is None]
    if missing:
        bad.append("placement.py refs missing from the board: %s" % " ".join(sorted(missing)))
    if tvs_seen != len(PL.TVS_ROWS):
        bad.append("%d TVS placed by net, TVS_ROWS has %d slots" % (tvs_seen, len(PL.TVS_ROWS)))
    rep.add("E3", "every other part at its placement.py / TVS_ROWS position (%d + %d)"
            % (len(PL.P), len(PL.TVS_ROWS)), not bad, bad)


# --------------------------------------------------------------------------
# F. courtyards and errors (kicad-cli on a scratch copy)
# --------------------------------------------------------------------------


def check_f(rep: Report, drc: Optional[dict], how: str) -> None:
    rep.sec("F. courtyards and DRC errors (placement)")
    if drc is None:
        rep.check("F1", "zero courtyard overlaps", False, how)
        rep.check("F2", "zero DRC errors (unconnected items excluded, see L3)", False, how)
        return
    v = drc.get("violations") or []
    cy = [x for x in v if x.get("type") == "courtyards_overlap"]
    rep.add("F1", "zero courtyard overlaps", not cy, drc_summary(cy) + drc_examples(cy))
    errs = [x for x in v if x.get("severity") == "error"]
    rep.add("F2", "zero DRC errors at --severity-error (unconnected items excluded, see L3)", not errs,
            ["run: %s" % how] + drc_summary(errs) + drc_examples(errs) if errs else [])


# --------------------------------------------------------------------------
# G. under the OLED, H. under the ceiling
# --------------------------------------------------------------------------


def pkg_height(fpname: str) -> Optional[float]:
    for pat, h in PKG_HEIGHTS:
        if re.search(pat, fpname):
            return h
    return None


def check_g(rep: Report, model: Model) -> None:
    rep.sec("G. only low parts under the OLED module")
    rect = (F.OLED_X0, F.OLED_Y0, F.OLED_X0 + F.OLED_PCB[0], F.OLED_Y0 + F.OLED_PCB[1])
    under = [f for f in model.fps if bbox_area_overlap(f.extent(), rect)]
    bad = []
    for f in under:
        ok = any(re.search(p, f.fpname) for p in OLED_LOW_OK)
        if f.ref in OLED_LOW_OK_REFS:
            ok = bool(re.search(OLED_LOW_OK_REFS[f.ref], f.fpname))
        if not ok:
            bad.append("%s (%s) courtyard reaches under the OLED" % (f.ref, f.fpname))
    rep.add("G1", "every courtyard under the OLED (%.1f,%.1f-%.1f,%.1f) is an allow-listed low package"
            % rect, not bad, sorted(bad) + ["under the OLED: %s" % " ".join(sorted(f.ref for f in under))])
    bad = []
    for f in under:
        h = pkg_height(f.fpname)
        if h is None or h >= OLED_PCB_BOTTOM:
            bad.append("%s (%s) height %s vs OLED pcb underside %.2f" % (f.ref, f.fpname, h, OLED_PCB_BOTTOM))
    rep.add("G2", "every part under the OLED is lower than the module's underside (%.2f mm)" % OLED_PCB_BOTTOM,
            not bad, bad)


def check_h(rep: Report, model: Model) -> None:
    rep.sec("H. nothing above the case ceiling")
    limit = F.CEIL - F.BOARD_T
    unknown = sorted("%s (%s)" % (f.ref, f.fpname) for f in model.fps if pkg_height(f.fpname) is None)
    rep.add("H1", "every footprint has a known package height", not unknown,
            ["no height for: %s" % ", ".join(unknown)] if unknown else [])
    tall = sorted("%s (%s) %.2f mm" % (f.ref, f.fpname, pkg_height(f.fpname)) for f in model.fps
                  if (pkg_height(f.fpname) or 0.0) > limit)
    tallest = max(((pkg_height(f.fpname) or 0.0, f.ref) for f in model.fps), default=(0.0, "-"))
    rep.add("H2", "no part taller than CEIL - BOARD_T = %.2f mm (tallest: %s %.2f)" % (limit, tallest[1], tallest[0]),
            not tall, tall)
    rep.check("H3", "OLED glass top %.2f <= CEIL %.2f" % (F.OLED_Z_TOP, F.CEIL), F.OLED_Z_TOP <= F.CEIL,
              "OLED_Z_TOP %.2f above CEIL %.2f" % (F.OLED_Z_TOP, F.CEIL))


# --------------------------------------------------------------------------
# K. project netclasses and DRC severities
# --------------------------------------------------------------------------


def check_k(rep: Report, proj_path: str, model: Model) -> None:
    rep.sec("K. Power netclass and DRC severities")
    try:
        with open(proj_path) as fh:
            proj = json.load(fh)
    except (OSError, ValueError) as exc:
        for ident in ("K2", "K2b", "K3"):
            rep.check(ident, "project file readable", False, "cannot read %s: %s" % (proj_path, exc))
        return
    nets = proj.get("net_settings") or {}
    classes = {c.get("name"): c for c in (nets.get("classes") or [])}
    bad = []
    pcls = classes.get(POWER_NETCLASS)
    if pcls is None:
        bad.append("no %r netclass in the project - the supply rails are on Default" % POWER_NETCLASS)
    else:
        w = float(pcls.get("track_width") or 0.0)
        if w < POWER_NETCLASS_MIN_WIDTH - 1e-9:
            bad.append("%s track_width %g mm, under %g" % (POWER_NETCLASS, w, POWER_NETCLASS_MIN_WIDTH))
        g = float(pcls.get("clearance") or 0.0)
        if g < POWER_NETCLASS_MIN_CLEARANCE - 1e-9:
            bad.append("%s clearance %g mm, under %g" % (POWER_NETCLASS, g, POWER_NETCLASS_MIN_CLEARANCE))
    assigned = {p.get("pattern") for p in (nets.get("netclass_patterns") or []) if p.get("netclass") == POWER_NETCLASS}
    orphans = [p for p in POWER_NETCLASS_PATTERNS if p not in assigned]
    if orphans:
        bad.append("netclass_patterns lacks %s -> %s" % (", ".join(orphans), POWER_NETCLASS))
    strays = sorted(assigned - set(POWER_NETCLASS_PATTERNS))
    if strays:
        bad.append("%s also claims %s" % (POWER_NETCLASS, ", ".join(strays)))
    version = (nets.get("meta") or {}).get("version")
    try:
        too_old = int(version) < NET_SETTINGS_MIN_VERSION
    except (TypeError, ValueError):
        too_old = True
    if too_old:
        bad.append("net_settings.meta.version is %r; netclass_patterns needs v%d" % (version, NET_SETTINGS_MIN_VERSION))
    if model.native is None:
        bad.append("cannot ask kicad to resolve the netclasses - K2 never passes on a file read alone")
    else:
        for pattern in POWER_NETCLASS_PATTERNS:
            got = model.netclass.get(pattern)
            if got is None:
                bad.append("%s is not a net on the board" % pattern)
            elif got != POWER_NETCLASS:
                bad.append("kicad resolves %s to %r, not %r" % (pattern, got, POWER_NETCLASS))
    rep.add("K2", "kicad resolves the seven 5 V rails to netclass %r" % POWER_NETCLASS, not bad, bad)

    bad = []
    seen = [n for n in V33_NETS if n in model.netclass]
    if not seen:
        bad.append("no +3V3 net on the board")
    for n in seen:
        if model.netclass[n] != "Default":
            bad.append("%s resolves to %r" % (n, model.netclass[n]))
    claims = [p.get("pattern") for p in (nets.get("netclass_patterns") or []) if "3V3" in (p.get("pattern") or "")]
    if claims:
        bad.append("netclass_patterns names %s" % ", ".join(claims))
    rep.add("K2b", "+3V3 stays on Default (%s)" % ", ".join("%s=%s" % (n, model.netclass.get(n)) for n in seen),
            not bad, bad)

    board = proj.get("board") or {}
    bad = []
    excl = board.get("drc_exclusions") or []
    if excl:
        bad.append("%d DRC exclusion(s): %s" % (len(excl), "; ".join(str(e) for e in excl[:5])))
    sev = (board.get("design_settings") or {}).get("rule_severities") or {}
    for rule in NEVER_IGNORE:
        if sev.get(rule) == "ignore":
            bad.append("%s is set to ignore" % rule)
    for rule in MUST_BE_ERROR:
        if sev.get(rule, "error") != "error":
            bad.append("%s is %r - F runs at --severity-error and would not see it" % (rule, sev.get(rule)))
    rep.add("K3", "no DRC exclusions, no guarded rule ignored or downgraded", not bad, bad)


# --------------------------------------------------------------------------
# L. layer stack, unconnected, full DRC
# --------------------------------------------------------------------------


def check_l(rep: Report, model: Model, drc: Optional[dict], how: str, routed: bool) -> None:
    rep.sec("L. 4-layer stack, planes, full DRC")
    rep.check("L1", "4 copper layers %s" % "/".join(COPPER_LAYERS), model.copper_layers == COPPER_LAYERS,
              "copper layers are %s" % model.copper_layers)

    bad = []
    copper_zones = [z for z in model.zones if not z.is_rule and not z.parent]
    for name, (net, layers) in EXPECTED_ZONES.items():
        hit = [z for z in copper_zones if z.name == name]
        if len(hit) != 1:
            bad.append("zone %r: found %d" % (name, len(hit)))
            continue
        z = hit[0]
        if z.net != net:
            bad.append("zone %r is on net %r, expected %r" % (name, z.net, net))
        if sorted(z.layers) != sorted(layers):
            bad.append("zone %r is on %s, expected %s" % (name, z.layers, layers))
        if not model.pad_nets.get(z.net):
            same = sorted(n for n in model.pad_nets if n.rsplit("/", 1)[-1] == z.net.rsplit("/", 1)[-1])
            bad.append("zone %r net %r carries no pads - it connects nothing%s"
                       % (name, z.net, " (the pads are on %s)" % ", ".join("%r" % s for s in same) if same else ""))
    extra = [z.name or z.net for z in copper_zones if z.name not in EXPECTED_ZONES]
    if extra:
        bad.append("unexpected copper zones: %s" % ", ".join(extra))
    rep.add("L2", "zones gnd_plane/In1 GND, 3v3_plane/In2 +3V3, gnd_top/F GND, gnd_bottom/B GND, on nets the pads use",
            not bad, bad)

    if drc is None:
        rep.check("L3", "zero unconnected items", False, how, waiting=not routed)
        rep.check("L4", "zero placement/silk DRC violations at --severity-all --refill-zones", False, how)
        rep.check("L5", "zero copper DRC violations at --severity-all --refill-zones", False, how, waiting=not routed)
        rep.check("L6", "zero schematic parity issues", False, how)
        return
    unc = drc.get("unconnected_items") or []
    rep.add("L3", "zero unconnected items", not unc,
            ["%d unconnected items (after zone refill)%s" % (len(unc), "; board has no tracks yet" if not routed else "")]
            if unc else [], waiting=not routed)
    v = drc.get("violations") or []
    struct = [x for x in v if x.get("type") not in COPPER_DEPENDENT_DRC]
    cop = [x for x in v if x.get("type") in COPPER_DEPENDENT_DRC]
    rep.add("L4", "zero placement/silk/outline DRC violations (any severity) - judged now", not struct,
            ["run: %s" % how] + drc_summary(struct) + drc_examples(struct, 3) if struct else [])
    rep.add("L5", "zero copper DRC violations (any severity)", not cop,
            drc_summary(cop) + drc_examples(cop, 3) if cop else [], waiting=not routed)
    par = drc.get("schematic_parity") or []
    rep.add("L6", "zero schematic parity issues", not par, drc_summary(par) + drc_examples(par, 3) if par else [])


# --------------------------------------------------------------------------
# M. via and track widths
# --------------------------------------------------------------------------


def check_m(rep: Report, model: Model, routed: bool) -> None:
    rep.sec("M. via and track sizes")
    vias = [t for t in model.tracks if t.is_via]
    segs = [t for t in model.tracks if not t.is_via]
    if not routed:
        for ident, title in (("M1", "vias drill >= %g, diameter >= %g" % (VIA_MIN_DRILL, VIA_MIN_DIA)),
                             ("M2", "Power-class tracks >= %g mm" % POWER_TRACK_MIN),
                             ("M3", "no track thinner than %g mm" % TRACK_MIN)):
            rep.check(ident, title, False, "no copper yet: 0 tracks, 0 vias", waiting=True)
        return
    bad = ["via on %r at %.2f,%.2f: drill %.3f dia %.3f" % (v.net, v.start[0], v.start[1], v.drill, v.width)
           for v in vias if v.drill < VIA_MIN_DRILL - 1e-6 or v.width < VIA_MIN_DIA - 1e-6]
    power = set(POWER_NETCLASS_PATTERNS) | {n for n, c in model.netclass.items() if c == POWER_NETCLASS}
    bad += ["Power via on %r at %.2f,%.2f: dia %.3f < %g" % (v.net, v.start[0], v.start[1], v.width, POWER_VIA_MIN)
            for v in vias if v.net in power and v.width < POWER_VIA_MIN - 1e-6
            and not any(abs(v.start[0] - x) < 0.01 and abs(v.start[1] - y) < 0.01 for x, y in POWER_VIA_EXCEPT)]
    rep.add("M1", "%d vias: drill >= %g, diameter >= %g (Power nets %g, %d listed exceptions)" % (len(vias), VIA_MIN_DRILL, VIA_MIN_DIA, POWER_VIA_MIN, len(POWER_VIA_EXCEPT)), not bad, bad[:20])
    pt = [t for t in segs if t.net in power]
    bad = ["%s %s %.3f mm at %.2f,%.2f" % (t.net, t.layer, t.width, t.start[0], t.start[1])
           for t in pt if t.width < POWER_TRACK_MIN - 1e-6]
    unrouted = sorted(n for n in POWER_NETCLASS_PATTERNS if not any(t.net == n for t in segs))
    if unrouted:
        bad.append("no track segment yet on %s - a rail with no copper cannot be measured" % " ".join(unrouted))
    rep.add("M2", "Power-class tracks >= %g mm (%d segments), every 5 V rail routed" % (POWER_TRACK_MIN, len(pt)),
            not bad, bad[:20])
    bad = ["%s %s %.3f mm at %.2f,%.2f" % (t.net, t.layer, t.width, t.start[0], t.start[1])
           for t in segs if t.width < TRACK_MIN - 1e-6]
    rep.add("M3", "no track thinner than %g mm (%d segments)" % (TRACK_MIN, len(segs)), not bad, bad[:20])


# --------------------------------------------------------------------------
# N. frozen files
# --------------------------------------------------------------------------


def load_module(name: str, path: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def check_n(rep: Report, repo: str, enabled: bool) -> None:
    rep.sec("N. rev5 + rev6 frozen (check_rev7_netlist.py section I), rev7 project clean")
    if not enabled:
        rep.add("N0", "git checks skipped (--no-git)", True)
        return
    try:
        NL = load_module("check_rev7_netlist", NETLIST_CHECKER)
        nrep = NL.Report()
        NL.check_frozen(nrep, repo, True)
    except Exception as exc:  # noqa: BLE001 - fail closed
        rep.check("N1", "rev6 freeze guard ran", False, "cannot run check_rev7_netlist.check_frozen: %s" % exc)
        return
    bad = ["%s: %s" % (ident, msg) for _sec, ident, ok, msg in nrep.results if not ok]
    rep.add("N1", "check_rev7_netlist section I: %d/%d frozen-file assertions pass"
            % (len(nrep.results) - len(bad), len(nrep.results)), bool(nrep.results) and not bad,
            bad or ([] if nrep.results else ["section I produced no assertions"]))
    for w in nrep.warns:
        print("note (N1): %s" % w)

    dirty = []
    for rel in (PROJECT_REL, PRL_REL):
        try:
            r = subprocess.run(["git", "-C", repo, "status", "--porcelain", "--", rel],
                               capture_output=True, text=True, check=False)
        except OSError as exc:
            dirty.append("git failed: %s" % exc)
            continue
        if r.returncode != 0:
            dirty.append("git status %s failed: %s" % (rel, r.stderr.strip()))
        elif r.stdout.strip():
            dirty.append("%s differs from HEAD - a kicad-cli/pcbnew save rewrote it; "
                         "git checkout -- it before trusting DRC" % rel)
    rep.add("N2", "rev7 .kicad_pro/.kicad_prl unmodified in the working tree", not dirty, dirty)


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="structural checks for the rev7 board")
    ap.add_argument("board", nargs="?", default=os.path.join(REPO_ROOT, BOARD_REL))
    ap.add_argument("--work", default=None, help="scratch dir (default: a fresh temp dir); must be outside the repo")
    ap.add_argument("--no-git", action="store_true", help="skip section N")
    args = ap.parse_args(argv)

    board_path = os.path.abspath(args.board)
    if not os.path.isfile(board_path):
        print("board file not found: %s" % board_path, file=sys.stderr)
        return 2
    work = os.path.abspath(args.work or tempfile.mkdtemp(prefix="rev7-pcb-check-"))
    if work == REPO_ROOT or work.startswith(REPO_ROOT + os.sep):
        print("FATAL: --work must be outside the repository (%s)" % work, file=sys.stderr)
        return 2
    os.makedirs(work, exist_ok=True)
    src_dir = REV7_DIR

    print("=== amigahid-pico rev7 board checker ===")
    print("board  : %s" % board_path)
    print("scratch: %s" % work)

    geom_copy = stage(src_dir, board_path, os.path.join(work, "geom"))
    try:
        model = load_model(geom_copy)
    except Exception as exc:  # noqa: BLE001 - no pcbnew, no verdict
        print("FATAL: pcbnew cannot load the board (%s) - every section needs it" % exc)
        print("RESULT: FAIL")
        return 1
    routed = any(True for _ in model.tracks)
    nseg = sum(1 for t in model.tracks if not t.is_via)
    print("content: %d footprints, %d track segments, %d vias, %d zones; copper %s"
          % (len(model.fps), nseg, len(model.tracks) - nseg, len(model.zones), "/".join(model.copper_layers)))
    if not routed:
        print("NOTE   : no tracks or vias on the board - routing-dependent assertions fail closed, "
              "labelled [%s]" % WAIT)
    print()

    f_copy = stage(src_dir, board_path, os.path.join(work, "drc-placement"))
    drc_f, how_f = run_drc(f_copy, ["--severity-error"])
    l_copy = stage(src_dir, board_path, os.path.join(work, "drc-full"))
    drc_l, how_l = run_drc(l_copy, ["--severity-all", "--refill-zones", "--schematic-parity"])
    print()

    ol = build_outline(model)
    rep = Report()
    check_a(rep, model, ol)
    check_b(rep, model)
    check_c(rep, model, ol)
    check_d(rep, model)
    check_e(rep, model, ol)
    check_f(rep, drc_f, how_f)
    check_g(rep, model)
    check_h(rep, model)
    check_k(rep, os.path.join(os.path.dirname(geom_copy), "amigahid-pico-rev7.kicad_pro"), model)
    check_l(rep, model, drc_l, how_l, routed)
    check_m(rep, model, routed)
    check_n(rep, REPO_ROOT, not args.no_git)
    rep.emit()
    return 0 if not rep.failures else 1


if __name__ == "__main__":
    sys.exit(main())
