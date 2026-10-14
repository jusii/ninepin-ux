#!/usr/bin/env python3
"""Build amigahid-pico-rev7.kicad_pcb from the schematic netlist + floorplan.py + placement.py.
Creates the 4-layer board, outline, holes, every footprint with its nets, the GND/+3V3 planes,
the RM2 antenna keep-out and the placement guides. Tracks/vias are NOT touched by this script
once routing has started: it refuses to run if the existing board has copper tracks unless
--force is given (re-placement then loses the routing)."""
import os, re, sys, subprocess, tempfile, xml.etree.ElementTree as ET
import pcbnew
from pcbnew import VECTOR2I, FromMM as mm
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import floorplan as F
import placement as PL

OUT = os.path.join(HERE, "amigahid-pico-rev7.kicad_pcb")
SCH = os.path.join(HERE, "amigahid-pico-rev7.kicad_sch")
SYSFP = "/usr/share/kicad/footprints"

if os.path.exists(OUT) and "--force" not in sys.argv:
    if "(segment" in open(OUT).read():
        raise SystemExit("board already has tracks; use --force to rebuild (routing will be lost)")

# ---- netlist ----
tmp = tempfile.mkdtemp(prefix="rev7-mb-")
xmlp = os.path.join(tmp, "rev7.xml")
subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadxml", "-o", xmlp, SCH], check=True, capture_output=True)
root = ET.parse(xmlp).getroot()
comps = {}
for c in root.find("components"):
    ref = c.get("ref")
    fields = {f.get("name"): (f.text or "") for f in (c.find("fields") if c.find("fields") is not None else [])}
    comps[ref] = dict(fp=c.findtext("footprint") or "", value=c.findtext("value") or "", fields=fields)
padnet = {}
for n in root.find("nets"):
    for node in n:
        padnet[(node.get("ref"), node.get("pin"))] = n.get("name")

b = pcbnew.BOARD()
b.SetCopperLayerCount(4)
ds = b.GetDesignSettings()
ds.m_MinClearance = mm(0.15); ds.m_TrackMinWidth = mm(0.15); ds.m_ViasMinSize = mm(0.5)
ds.m_MinThroughDrill = mm(0.3); ds.m_CopperEdgeClearance = mm(0.3); ds.m_HoleClearance = mm(0.15)
ds.m_HoleToHoleMin = mm(0.5); ds.m_ViasMinAnnularWidth = mm(0.1); ds.m_SilkClearance = mm(0.15)
nc = ds.m_NetSettings.GetDefaultNetclass()
nc.SetClearance(mm(0.15)); nc.SetTrackWidth(mm(0.2)); nc.SetViaDiameter(mm(0.6)); nc.SetViaDrill(mm(0.3))

def P(x, y): return VECTOR2I(mm(x), mm(y))
def shape(kind, layer, w=0.1):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(kind); s.SetLayer(layer); s.SetWidth(mm(w)); b.Add(s); return s
def line(x0, y0, x1, y1, layer, w=0.1):
    s = shape(pcbnew.SHAPE_T_SEGMENT, layer, w); s.SetStart(P(x0, y0)); s.SetEnd(P(x1, y1))
def rect(x0, y0, x1, y1, layer, w=0.1):
    s = shape(pcbnew.SHAPE_T_RECTANGLE, layer, w); s.SetStart(P(x0, y0)); s.SetEnd(P(x1, y1))
def arc(cx, cy, r, a0, a1, layer, w=0.1):
    import math
    s = shape(pcbnew.SHAPE_T_ARC, layer, w); am = (a0 + a1) / 2
    pt = lambda a: P(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a)))
    s.SetArcGeometry(pt(a0), pt(am), pt(a1))
def text(t, x, y, layer, size=1.0, rot=0):
    s = pcbnew.PCB_TEXT(b); s.SetText(t); s.SetPosition(P(x, y)); s.SetLayer(layer)
    s.SetTextSize(VECTOR2I(mm(size), mm(size))); s.SetTextThickness(mm(size * 0.15)); s.SetTextAngleDegrees(rot); b.Add(s)

# outline
W, H, R = F.W, F.H, F.CORNER_R; E = pcbnew.Edge_Cuts
line(R, 0, W - R, 0, E); line(W, R, W, H - R, E); line(W - R, H, R, H, E); line(0, H - R, 0, R, E)
arc(W - R, R, R, 270, 360, E); arc(W - R, H - R, R, 0, 90, E); arc(R, H - R, R, 90, 180, E); arc(R, R, R, 180, 270, E)

nets = {}
def net(name):
    if name not in nets:
        ni = pcbnew.NETINFO_ITEM(b, name); b.Add(ni); nets[name] = ni
    return nets[name]

def load_fp(fpid):
    lib, name = fpid.split(":", 1)
    path = os.path.join(HERE, lib + ".pretty") if lib == "rev7" else os.path.join(SYSFP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    assert fp, fpid
    return fp

def place(ref, fpid, value, x, y, rot, fields=None):
    fp = load_fp(fpid)
    fp.SetReference(ref); fp.SetValue(value)
    fp.SetPosition(P(x, y)); fp.SetOrientationDegrees(rot)
    for k, v in (fields or {}).items():
        if k in ("LCSC", "MPN") and hasattr(fp, "SetField"):
            try: fp.SetField(k, v)
            except Exception: pass
    for pad in fp.Pads():
        nn = padnet.get((ref, pad.GetNumber()))
        if nn and not nn.startswith("unconnected-"):
            pad.SetNet(net(nn))
    b.Add(fp)
    return fp

# connectors / holes / OLED from the floorplan
conn_pos = {c[0]: (c[4], c[5], c[6]) for c in F.CONNECTORS}
conn_pos[F.J3[0]] = (F.J3[4], F.J3[5], F.J3[6])
placed = set()
for i, h in enumerate(F.HOLES):
    place("H%d" % (i + 1), "MountingHole:MountingHole_3.2mm_M3", "M3", h[0], h[1], 0)
for i, h in enumerate(F.OLED_HOLES):
    place("H%d" % (5 + i), "MountingHole:MountingHole_2.2mm_M2", "M2 OLED post", h[0], h[1], 0)

# TVS by net
tvs_by_net = {}
for ref, c in comps.items():
    if c["value"] == "PESD5V0S1BA":
        n = padnet.get((ref, "1")); tvs_by_net[n] = ref

park_x, park_y = 110.0, 5.0
missing = []
for ref, c in sorted(comps.items(), key=lambda kv: (re.sub(r"\d+", "", kv[0]), int(re.sub(r"\D", "", kv[0]) or 0))):
    fpid = c["fp"]
    if not fpid: raise SystemExit("no footprint: " + ref)
    if ref in conn_pos: x, y, rot = conn_pos[ref]
    elif ref in PL.P: x, y, rot = PL.P[ref]
    elif c["value"] == "PESD5V0S1BA" and padnet.get((ref, "1"), "").lstrip("/") in PL.TVS_ROWS: x, y, rot = PL.TVS_ROWS[padnet[(ref, "1")].lstrip("/")]
    else:
        x, y, rot = park_x, park_y, 0; park_y += 4.0; missing.append(ref)
    place(ref, fpid, c["value"], x, y, rot, c["fields"])
if missing: print("UNPLACED (parked east of the board):", missing)

# planes and pours
def zone(netname, layer, pts, prio, name, fill_clear=0.25, minw=0.2):
    z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(net(netname)); z.SetZoneName(name)
    z.SetAssignedPriority(prio); z.SetLocalClearance(mm(fill_clear)); z.SetMinThickness(mm(minw))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL); z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.3))
    ol = z.Outline(); ol.NewOutline()
    for (x, y) in pts: ol.Append(mm(x), mm(y))
    b.Add(z); return z
board_pts = [(0, 0), (W, 0), (W, H), (0, H)]
zone("GND", pcbnew.In1_Cu, board_pts, 0, "gnd_plane")
zone("+3V3", pcbnew.In2_Cu, board_pts, 0, "3v3_plane")
zone("GND", pcbnew.F_Cu, board_pts, 0, "gnd_top")
zone("GND", pcbnew.B_Cu, board_pts, 0, "gnd_bottom")

# RM2 antenna keep-out: all copper layers
kx0, ky0, kx1, ky1 = F.RM2_KEEPOUT
z = pcbnew.ZONE(b); z.SetIsRuleArea(True)
z.SetDoNotAllowTracks(True); z.SetDoNotAllowVias(True); z.SetDoNotAllowPads(True); z.SetDoNotAllowZoneFills(True); z.SetDoNotAllowFootprints(False)   # copper only: the RM2 itself and the SW corner hole sit here
z.SetZoneName("rm2_antenna_keepout")
ls = pcbnew.LSET()
for L in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu): ls.addLayer(L)
z.SetLayerSet(ls); ol = z.Outline(); ol.NewOutline()
for (x, y) in ((max(kx0, 0), ky0), (kx1, ky0), (kx1, ky1), (max(kx0, 0), ky1)): ol.Append(mm(x), mm(y))
b.Add(z)

# guides
C, D = pcbnew.Cmts_User, pcbnew.Dwgs_User
ox, oy = F.OLED_X0, F.OLED_Y0
rect(ox, oy, ox + F.OLED_PCB[0], oy + F.OLED_PCB[1], C)
rect(ox + 0.4, F.OLED_GLASS_Y0, ox + 0.4 + F.OLED_GLASS[0], F.OLED_GLASS_Y0 + F.OLED_GLASS[1], C)
text("OLED 0.96in on 11 mm posts", ox + F.OLED_PCB[0] / 2, oy + F.OLED_PCB[1] + 1.5, C, 0.8)
rect(kx0, ky0, kx1, ky1, C); text("RM2 antenna keep-out, no metal, all layers", 3.0, ky0 - 1.2, C, 0.7)
g = F.GAP; o = F.OVH
rect(-g, -g, W + g, H + g, D); rect(-o, -o, W + o, H + o, D)
text("hid-pico Amiga UX rev7", W / 2, H + o + 2.5, D, 1.2)
text("hid-pico Amiga UX rev7", 50, 59.0, pcbnew.F_SilkS, 1.0)

b.Save(OUT)
print("saved", OUT, "footprints", len(list(b.GetFootprints())), "nets", len(nets))
