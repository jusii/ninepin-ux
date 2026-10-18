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
    sp = c.find("sheetpath"); ts = c.findtext("tstamps") or ""
    path = ((sp.get("tstamps") if sp is not None else "/") or "/").rstrip("/") + "/" + ts
    comps[ref] = dict(fp=c.findtext("footprint") or "", value=c.findtext("value") or "", fields=fields, path=path,
                      datasheet=c.findtext("datasheet") or "", description=c.findtext("description") or "")
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

def place(ref, fpid, value, x, y, rot, comp=None):
    fp = load_fp(fpid)
    lib, name = fpid.split(":", 1)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetReference(ref); fp.SetValue(value)
    fp.SetPosition(P(x, y)); fp.SetOrientationDegrees(rot)
    fp.Reference().SetLayer(pcbnew.F_Fab)        # refs off the silk: JLC assembles from the pos file
    fp.Reference().SetVisible(True)
    if comp is None:                             # board-only item (mounting holes)
        fp.SetBoardOnly(True)
        fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    else:
        fp.SetPath(pcbnew.KIID_PATH(comp["path"]))
        fp.SetField("Datasheet", comp.get("datasheet", "")); fp.SetField("Description", comp.get("description", ""))
        for k, v in comp["fields"].items():
            if k not in ("Footprint", "Datasheet", "Description", "Reference", "Value"):
                fp.SetField(k, v)
        for f in fp.GetFields():                 # extra fields are data, not silk
            if f.GetName() not in ("Reference", "Value"):
                f.SetVisible(False); f.SetLayer(pcbnew.F_Fab)
    for pad in fp.Pads():
        nn = padnet.get((ref, pad.GetNumber()))
        if nn:                                   # including the schematic's no-connect nets, so --schematic-parity sees the same netlist
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
    elif c["value"].startswith("PESD5V0S1") and padnet.get((ref, "1"), "").lstrip("/") in PL.TVS_ROWS: x, y, rot = PL.TVS_ROWS[padnet[(ref, "1")].lstrip("/")]
    else:
        x, y, rot = park_x, park_y, 0; park_y += 4.0; missing.append(ref)
    place(ref, fpid, c["value"], x, y, rot, c)
if missing: print("UNPLACED (parked east of the board):", missing)

# planes and pours
def zone(netname, layer, pts, prio, name, fill_clear=0.25, minw=0.2):
    z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(net(netname)); z.SetZoneName(name)
    z.SetAssignedPriority(prio); z.SetLocalClearance(mm(fill_clear)); z.SetMinThickness(mm(minw))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL); z.SetThermalReliefGap(mm(0.25)); z.SetThermalReliefSpokeWidth(mm(0.4))
    ol = z.Outline(); ol.NewOutline()
    for (x, y) in pts: ol.Append(mm(x), mm(y))
    b.Add(z); return z
board_pts = [(0, 0), (W, 0), (W, H), (0, H)]
zone("GND", pcbnew.In1_Cu, board_pts, 0, "gnd_plane")
zone("/+3V3", pcbnew.In2_Cu, board_pts, 0, "3v3_plane")
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

# regulator group: no F.Cu pour over CIN / L1 / COUT and their rows, so the regulator ground reaches the plane only
# through its own vias (RP2350 DS 6.3.8) and there is no copper under the inductor
z = pcbnew.ZONE(b); z.SetIsRuleArea(True)
z.SetDoNotAllowTracks(False); z.SetDoNotAllowVias(False); z.SetDoNotAllowPads(False); z.SetDoNotAllowZoneFills(True); z.SetDoNotAllowFootprints(False)
z.SetZoneName("vreg_no_pour"); ls = pcbnew.LSET(); ls.addLayer(pcbnew.F_Cu); z.SetLayerSet(ls); ol = z.Outline(); ol.NewOutline()
for (x, y) in ((36.95, 37.8), (43.8, 37.8), (43.8, 41.7), (36.95, 41.7)): ol.Append(mm(x), mm(y))
b.Add(z)
# and no In1 ground plane directly under the inductor's LX pad (DS p. 412: cut the copper under the LX node on 4+ layer boards)
z = pcbnew.ZONE(b); z.SetIsRuleArea(True)
z.SetDoNotAllowTracks(False); z.SetDoNotAllowVias(False); z.SetDoNotAllowPads(False); z.SetDoNotAllowZoneFills(True); z.SetDoNotAllowFootprints(False)
z.SetZoneName("vreg_lx_no_plane"); ls = pcbnew.LSET(); ls.addLayer(pcbnew.In1_Cu); z.SetLayerSet(ls); ol = z.Outline(); ol.NewOutline()
for (x, y) in ((40.7, 39.6), (42.6, 39.6), (42.6, 41.1), (40.7, 41.1)): ol.Append(mm(x), mm(y))
b.Add(z)

# guides
C, D = pcbnew.Cmts_User, pcbnew.Dwgs_User
ox, oy = F.OLED_X0, F.OLED_Y0
rect(ox, oy, ox + F.OLED_PCB[0], oy + F.OLED_PCB[1], C)
rect(F.OLED_GLASS_X0, F.OLED_GLASS_Y0, F.OLED_GLASS_X0 + F.OLED_GLASS[0], F.OLED_GLASS_Y0 + F.OLED_GLASS[1], C)
text("OLED 0.96in on 11 mm posts", ox + F.OLED_PCB[0] / 2, oy + F.OLED_PCB[1] + 1.5, C, 0.8)
rect(kx0, ky0, kx1, ky1, C); text("RM2 antenna keep-out, no metal, all layers", 3.0, ky0 - 1.2, C, 0.7)
g = F.GAP; o = F.OVH
rect(-g, -g, W + g, H + g, D); rect(-o, -o, W + o, H + o, D)
text("hid-pico Amiga UX rev7", W / 2, H + o + 2.5, D, 1.2)

# top silk: connector and button labels (1.0 mm / 0.16 mm, JLCPCB minimums), and the JLCPCB order-number anchor on the back
def silk(txt, x, y, rot=0, layer=pcbnew.F_SilkS, h=1.0, w=0.16):
    t = pcbnew.PCB_TEXT(b); t.SetText(txt); t.SetPosition(P(x, y)); t.SetLayer(layer)
    t.SetTextSize(VECTOR2I(mm(h), mm(h))); t.SetTextThickness(mm(w)); t.SetTextAngleDegrees(rot)
    if layer == pcbnew.B_SilkS: t.SetMirrored(True)
    b.Add(t)
for txt, x, y, rot in (("KBD OUT", 26.5, 11.6, 0), ("KBD IN", 63.6, 4.0, 90),
                       ("SWD", 17.0, 8.0, 0), ("ENC", 86.8, 9.2, 0), ("BOOT", 90.6, 17.0, 90), ("RESET", 96.5, 53.65, 0),
                       ("PORT 1", 33.5, 47.6, 0), ("PORT 2", 89.6, 48.6, 0)):
    silk(txt, x, y, rot)
silk("A500", 59.3, 2.2, 90, h=0.8, w=0.15); silk("OUT", 60.4, 2.0, 90, h=0.8, w=0.15); silk("A500 IN", 50.8, 16.6, 0, h=0.8, w=0.15)   # out of the way of the pin names, J1 outline and JP1
# back silk: what the board is (no JLCPCB order-number anchor: tick "remove order number" when ordering)
for i, (txt, h) in enumerate((("amigahid-pico-UX  rev7  2026-10", 1.5), ("Amiga keyboard, mouse and joystick adapter  |  C64 joystick ports", 1.0),
                              ("USB HID host  |  Bluetooth HID  |  USB device for C64 Ultimate", 1.0), ("github.com/jusii/amigahid-pico", 1.0))):
    silk(txt, 50, 23.5 + i * 2.2 + (0 if i == 0 else 0.6), 0, pcbnew.B_SilkS, h, 0.2 if h > 1 else 0.15)   # between the OLED post rows (y 13 / 35.2)
# pin names on the headers, then reference + value beside every part that has room (silk_labels.py)
import silk_labels
pp, pm = silk_labels.label_pins(b, W, H)
lp, lm = silk_labels.label_parts(b, W, H, skip_refs=("P1", "P2", "J10", "J12", "J4", "J13", "SW1", "SW2", "J1", "J8", "J2", "J3", "J7", "U9"))   # these carry their own labels
print("silk: %d pin names (%d missed: %s); %d parts labelled, %d without room: %s" % (len(pp), len(pm), " ".join(pm), len(lp), len(lm), " ".join(lm)))
# footprint silk below JLCPCB's 0.15 mm minimum line width (library 0.12 mm outlines) is thickened
for fp in b.Footprints():
    for it in fp.GraphicalItems():
        if it.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS) and it.GetClass() == "PCB_SHAPE" and it.GetWidth() < mm(0.15):
            it.SetWidth(mm(0.15))

# 3D bodies: rev6's STEP models for the mini-DIN sockets and the OLED-on-posts, simple VRML bodies for the RM2, the 1812
# fuses and the WS2812 (kicad 10 ships none), and the QFN-56 7x7 body standing in for the QFN-60 (same package outline)
D3 = "${KIPRJMOD}/rev7.3dshapes/"
def model(fp, path, replace=False):
    if replace: fp.Models().clear()
    m = pcbnew.FP_3DMODEL(); m.m_Filename = path; fp.Add3DModel(m)
for fp in b.GetFootprints():
    ref = fp.GetReference(); name = str(fp.GetFPID().GetLibItemName())
    if ref in ("J10", "J12"): model(fp, D3 + "MiniDIN6_Socket_MD-60S.step")
    if ref == "U9": model(fp, D3 + "RaspberryPi_RM2.wrl")
    if ref in ("H5", "H6", "H7", "H8"): model(fp, D3 + "M2_post_11mm.step")
    if ref == "H6": model(fp, D3 + "OLED_0.96in_128x64_on_posts.step")
    if name.startswith("Fuse_1812_"): model(fp, D3 + "Fuse_1812.wrl", replace=True)
    if name.startswith("LED_WS2812B-2020"): model(fp, D3 + "LED_WS2812B-2020.wrl", replace=True)
    if name.startswith("QFN-60-1EP_7x7mm"): model(fp, "${KICAD10_3DMODEL_DIR}/Package_DFN_QFN.3dshapes/QFN-56-1EP_7x7mm_P0.4mm_EP3.2x3.2mm.step", replace=True)
b.Save(OUT)
# pcbnew's Save rewrites the project file and drops the netclass patterns: re-apply the project
# settings so the file is deterministic and the Power class survives (pcbnew's Save() drops the netclass patterns).
import json
PRO = OUT.replace(".kicad_pcb", ".kicad_pro")
j = json.load(open(PRO))
ns = j["net_settings"]
default = [c for c in ns["classes"] if c["name"] == "Default"][0]
default.update({"clearance": 0.15, "track_width": 0.2, "via_diameter": 0.6, "via_drill": 0.3, "diff_pair_width": 0.2, "diff_pair_gap": 0.15})
power = dict(default); power.update({"name": "Power", "clearance": 0.2, "track_width": 0.6, "via_diameter": 0.8, "via_drill": 0.4})
ns["classes"] = [default, power]
ns["netclass_patterns"] = [{"netclass": "Power", "pattern": p} for p in ("/AMIGA_5V", "/AMIGA_5V_FUSED", "/AMIGA_IO5V", "/SYS5V", "/AUX_5V", "/USBA_5V", "/KBIN_5V", "/cont1_5v", "/cont2_5v")]
if ns.get("meta", {}).get("version", 0) < 3: ns["meta"] = {"version": 3}
if not j.get("schematic", {}).get("top_level_sheets"):              # kicad-cli sch export empties this; put the root sheet back
    j.setdefault("schematic", {})["top_level_sheets"] = [{"filename": "amigahid-pico-rev7.kicad_sch", "name": "amigahid-pico-rev7", "uuid": "00000000-0000-0000-0000-000000000000"}]
r = j["board"]["design_settings"]["rules"]
r.update({"min_clearance": 0.15, "min_track_width": 0.15, "min_via_diameter": 0.5, "min_through_hole_diameter": 0.3, "min_hole_to_hole": 0.5,
          "min_hole_clearance": 0.15, "min_copper_edge_clearance": 0.3, "min_silk_clearance": 0.15, "min_via_annular_width": 0.1})
j["board"]["design_settings"]["track_widths"] = [0.0, 0.2, 0.3, 0.6, 1.0]
j["board"]["design_settings"]["via_dimensions"] = [{"diameter": 0.0, "drill": 0.0}, {"diameter": 0.6, "drill": 0.3}, {"diameter": 0.8, "drill": 0.4}]
open(PRO, "w").write(json.dumps(j, indent=2) + "\n")
print("saved", OUT, "footprints", len(list(b.GetFootprints())), "nets", len(nets), "| project netclasses re-applied")
