"""Silkscreen the way a board should be read on the bench (standing rule for every board, 2026-10-18):
reference and value beside every part that has room, signal names at every header pin, and a
description block on the back instead of the fab's order-number anchor. make_board.py calls
label_pins() then label_parts() after the connector labels are on the board.

A text goes where its box, grown by MARGIN, overlaps nothing that matters: exposed copper (pads),
another part's body (pads + fab outline: text under a body is unreadable; the courtyard's clearance
ring is fair game), silk already on the board (footprint outlines, connector labels, earlier labels),
or the board edge zone. Parts are labelled largest-first so the ICs win the space. Text is 0.8 mm /
0.15 mm (JLCPCB's silk minimums), 1.0 mm for ICs, 0.7 mm for pin names. Whatever does not fit is
skipped and reported; nothing is squeezed.
"""
import pcbnew

MARGIN = 0.15
EDGE = 0.6
# header pin names on the silk: net -> short name; pin 5 of the A500 headers is the plug's key position
NET_SHORT = {"kbclock": "CLK", "kbdata": "DAT", "kbreset": "RST", "AMIGA_5V": "+5V", "KBIN_5V": "+5V", "kbin_clk": "CLK",
             "kbin_dat": "DAT", "GND": "GND", "kb_pwrled": "PWR", "kb_drvled": "DRV", "swclk": "CLK", "swdio": "DIO",
             "+3V3": "3V3", "i2c_scl": "SCL", "i2c_sda": "SDA", "enc_sw": "SW", "enc_sp": "SP", "enc_a": "A", "enc_b": "B"}
KEY_PINS = {("J1", "5"): "KEY", ("J8", "5"): "KEY"}
PIN_HEADERS = ("J1", "J8", "J2", "J7")      # not J3: the OLED socket sits under the module


def _mm(v): return int(round(v * 1e6))


def _bb(item, grow=0.0):
    r = item.GetBoundingBox()
    return (r.GetLeft() / 1e6 - grow, r.GetTop() / 1e6 - grow, r.GetRight() / 1e6 + grow, r.GetBottom() / 1e6 + grow)


def _overlap(a, b): return a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1]
def _union(a, b): return (min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3]))


_STATE = {}


def _obstacles(b, W, H):
    """pads, bodies, courtyards and silk on the board; built once per board, shared by both labellers"""
    if _STATE.get("board") is b: return _STATE
    _STATE.clear(); _STATE["board"] = b; _STATE["board_box"] = (EDGE, EDGE, W - EDGE, H - EDGE)
    pads, bodies, courts, silks = [], {}, {}, {}
    for fp in b.Footprints():
        ref = fp.GetReference(); body = None
        for p in fp.Pads():
            if p.IsOnLayer(pcbnew.F_Cu) or p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                pads.append(_bb(p, 0.1)); body = _bb(p) if body is None else _union(body, _bb(p))
        for it in fp.GraphicalItems():
            if it.GetLayer() == pcbnew.F_Fab and it.GetClass() == "PCB_SHAPE": body = _bb(it) if body is None else _union(body, _bb(it))   # not the fab-layer reference text
            if it.GetLayer() == pcbnew.F_SilkS: silks.setdefault(ref, []).append(_bb(it))
        c = fp.GetCourtyard(pcbnew.F_CrtYd)
        r = c.BBox() if c.OutlineCount() else fp.GetBoundingBox(False, False)
        courts[ref] = (r.GetLeft() / 1e6, r.GetTop() / 1e6, r.GetRight() / 1e6, r.GetBottom() / 1e6)
        bodies[ref] = body or courts[ref]
    silks["_board"] = [_bb(d) for d in b.Drawings() if d.GetLayer() == pcbnew.F_SilkS]
    _STATE.update(pads=pads, bodies=bodies, courts=courts, silks=silks)
    return _STATE


def _make(b, txt, x, y, rot, h, w=0.15):
    t = pcbnew.PCB_TEXT(b); t.SetText(txt); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(_mm(h), _mm(h))); t.SetTextThickness(_mm(w))
    t.SetPosition(pcbnew.VECTOR2I(_mm(x), _mm(y))); t.SetTextAngleDegrees(rot)
    return t


def _free(st, box, me=None, own_silk_ok=False):
    bb = st["board_box"]
    if not (bb[0] <= box[0] and box[2] <= bb[2] and bb[1] <= box[1] and box[3] <= bb[3]): return False
    g = (box[0] - MARGIN, box[1] - MARGIN, box[2] + MARGIN, box[3] + MARGIN)
    if any(_overlap(g, p) for p in st["pads"]): return False
    if any(_overlap(g, c) for r, c in st["bodies"].items() if r != me): return False
    for r, items in st["silks"].items():
        if own_silk_ok and r == me: continue
        if any(_overlap(g, s) for s in items): return False
    return True


def _place(b, st, txt, spots, h, me=None, own_silk_ok=False):
    for x, y, rot in spots:
        t = _make(b, txt, x, y, rot, h)
        if _free(st, _bb(t), me, own_silk_ok):
            b.Add(t); st["silks"].setdefault("_board", []).append(_bb(t)); return True
    return False


def label_pins(b, W, H, h=0.8):
    """pin names outside the header outline, in line with each pin: vertical text above/below the row
    (the nearer side first for two-row headers), else horizontal text east/west of the pin"""
    st = _obstacles(b, W, H); placed, missed = [], []
    st["board_box"] = (0.5, 0.5, W - 0.5, H - 0.5)                      # pin names may come 0.5 from the edge (parts keep EDGE)
    for ref in PIN_HEADERS:
        fp = b.FindFootprintByReference(ref)
        if fp is None: continue
        cx0, cy0, cx1, cy1 = st["courts"][ref]; cyc = (cy0 + cy1) / 2
        for p in fp.Pads():
            num = p.GetNumber(); net = p.GetNetname().lstrip("/")
            name = KEY_PINS.get((ref, num)) or NET_SHORT.get(net)
            if not name: continue
            px, py = p.GetPosition().x / 1e6, p.GetPosition().y / 1e6
            probe = _make(b, name, 0, 0, 0, h); pb = _bb(probe); tw = pb[2] - pb[0]
            g = MARGIN
            above, below = (px, cy0 - g - tw / 2, 90), (px, cy1 + g + tw / 2, 90)
            nudge = lambda sp, d: (sp[0], sp[1] + d, sp[2])                    # a touch closer to the pin row (the courtyard ring has slack)
            spots = ([above, nudge(above, 0.3), below, nudge(below, -0.3)] if py <= cyc else [below, nudge(below, -0.3), above, nudge(above, 0.3)])
            spots += [(cx1 + g + tw / 2, py, 0), (cx0 - g - tw / 2, py, 0)]
            (placed if _place(b, st, name, spots, h, ref, own_silk_ok=False) else missed).append("%s.%s%s" % (ref, num, "" if True else name))
    return placed, missed


def label_parts(b, W, H, skip_prefix=("H", "JP"), skip_refs=(), big=("U", "Y", "L", "F", "LED")):
    """reference, then value, beside every part: north, south, east, west of the courtyard, then the corners;
    vertical text at the sides of upright parts"""
    st = _obstacles(b, W, H); courts = st["courts"]; st["board_box"] = (EDGE, EDGE, W - EDGE, H - EDGE)
    fps = [fp for fp in b.Footprints() if not fp.GetReference().startswith(tuple(skip_prefix)) and fp.GetReference() not in skip_refs]

    def spots_for(ref, tw, th):
        x0, y0, x1, y1 = courts[ref]; cx, cy = (x0 + x1) / 2, (y0 + y1) / 2; g = MARGIN
        upright = (y1 - y0) > (x1 - x0) * 1.3
        ns = [(cx, y0 - th / 2 - g, 0), (cx, y1 + th / 2 + g, 0)]
        ew = [(x1 + th / 2 + g, cy, 90), (x0 - th / 2 - g, cy, 90)] if upright else [(x1 + tw / 2 + g, cy, 0), (x0 - tw / 2 - g, cy, 0)]
        corners = [(x0 + tw / 2, y0 - th / 2 - g, 0), (x1 - tw / 2, y0 - th / 2 - g, 0), (x0 + tw / 2, y1 + th / 2 + g, 0), (x1 - tw / 2, y1 + th / 2 + g, 0)]
        other = [(x1 + th / 2 + g, cy, 90), (x0 - th / 2 - g, cy, 90), (x1 + tw / 2 + g, cy, 0), (x0 - tw / 2 - g, cy, 0)]
        return (ew + ns if upright else ns + ew) + corners + other

    order = sorted(fps, key=lambda f: -((courts[f.GetReference()][2] - courts[f.GetReference()][0]) * (courts[f.GetReference()][3] - courts[f.GetReference()][1])))
    placed, missed = [], []
    for fp in order:
        ref = fp.GetReference(); h = 1.0 if ref.rstrip("0123456789") in big else 0.8
        pb = _bb(_make(b, ref, 0, 0, 0, h)); got_ref = _place(b, st, ref, spots_for(ref, pb[2] - pb[0], pb[3] - pb[1]), h, ref)
        val = fp.GetValue(); got_val = False
        if got_ref and val and val != ref and len(val) <= 14:
            pb = _bb(_make(b, val, 0, 0, 0, h)); got_val = _place(b, st, val, spots_for(ref, pb[2] - pb[0], pb[3] - pb[1]), h, ref)
        (placed if got_ref else missed).append(ref + ("" if got_val or not got_ref else "(no value)"))
    return placed, missed
