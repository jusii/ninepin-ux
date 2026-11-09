#!/usr/bin/env python3
"""Generate amigahid-pico-rev7.1.kicad_sch from design.py.

design.py describes blocks of parts; each part is (ref, lib_id, value, footprint, pins, extra)
where pins maps pin number -> net name ("GND" gets a ground symbol, "NC" a no-connect, anything
else a local label on a short stub). The generator embeds the library symbols, lays each block
out in rows, and writes a flat single-sheet schematic. Re-runnable; the file is fully replaced.
"""
import os, re, sys, uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "amigahid-pico-rev7.1.kicad_sch")
SYSLIB = "/usr/share/kicad/symbols"
NS = uuid.UUID("7e3b4b2e-5a0c-4d6b-9c2a-1f0e8d7c6b5a")
def U(*k): return str(uuid.uuid5(NS, "|".join(str(x) for x in k)))

# ---------------- symbol library handling ----------------
_libcache = {}
def _libtext(lib):
    if lib not in _libcache:
        path = os.path.join(HERE, "rev7-symbols.kicad_sym") if lib == "rev7" else os.path.join(SYSLIB, lib + ".kicad_sym")
        _libcache[lib] = open(path).read()
    return _libcache[lib]

def _block(text, name):
    i = text.find('(symbol "%s"' % name)
    if i < 0: raise KeyError(name)
    d = 0; j = i
    while True:
        c = text[j]
        if c == '(': d += 1
        elif c == ')':
            d -= 1
            if d == 0: break
        j += 1
    return text[i:j + 1]

def _props(block):
    return dict(re.findall(r'\(property "([^"]+)" "((?:[^"\\]|\\.)*)"', block))

def flatten(lib_id):
    """Return the embedded lib_symbols text for lib_id (derived symbols flattened), and its pins."""
    lib, name = lib_id.split(":", 1)
    text = _libtext(lib)
    blk = _block(text, name)
    m = re.search(r'\(extends "([^"]+)"', blk)
    if m:
        base = m.group(1)
        bblk = _block(text, base)
        # override properties of the base with the derived symbol's full property blocks,
        # appending the ones the base does not have (eeschema flattens the same way)
        def prop_blocks(text):
            out = {}
            for pm in re.finditer(r'\(property "([^"]+)" "', text):
                i = pm.start(); d = 0; j = i
                while True:
                    ch = text[j]
                    if ch == '(': d += 1
                    elif ch == ')':
                        d -= 1
                        if d == 0: break
                    j += 1
                out[pm.group(1)] = text[i:j + 1]
            return out
        bp, dp = prop_blocks(bblk), prop_blocks(blk)
        for k, v in dp.items():
            if k in bp: bblk = bblk.replace(bp[k], v, 1)
            else:
                last = list(bp.values())[-1]; bblk = bblk.replace(last, last + "\n\t\t" + v, 1)
        bblk = bblk.replace('(symbol "%s"' % base, '(symbol "%s"' % name, 1)
        bblk = re.sub(r'\(symbol "%s_(\d+)_(\d+)"' % re.escape(base), lambda mm: '(symbol "%s_%s_%s"' % (name, mm.group(1), mm.group(2)), bblk)
        blk = bblk
    blk = blk.replace('(symbol "%s"' % name, '(symbol "%s"' % lib_id, 1)
    pins = []
    for pm in re.finditer(r'\(pin (\w+) \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length ([-\d.]+)\)(.*?)\(name "([^"]*)"(.*?)\(number "([^"]*)"', blk, re.S):
        pins.append(dict(type=pm.group(1), x=float(pm.group(2)), y=float(pm.group(3)), rot=int(pm.group(4)),
                         len=float(pm.group(5)), name=pm.group(7), num=pm.group(9), hide="(hide yes)" in pm.group(6)))
    props = _props(blk)
    return blk, pins, props

# ---------------- emit helpers ----------------
F = lambda v: ("%.2f" % v).rstrip("0").rstrip(".") if "." in "%.2f" % v else "%.2f" % v
def fx(v): return ("%.4f" % v).rstrip("0").rstrip(".")

def E_wire(x0, y0, x1, y1, key):
    return f'\t(wire\n\t\t(pts\n\t\t\t(xy {fx(x0)} {fx(y0)})\n\t\t\t(xy {fx(x1)} {fx(y1)})\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n\t\t(uuid "{U("w", key)}")\n\t)\n'

def E_label(name, x, y, rot, just, key):
    return f'\t(label "{name}"\n\t\t(at {fx(x)} {fx(y)} {rot})\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify {just})\n\t\t)\n\t\t(uuid "{U("l", key)}")\n\t)\n'

def E_nc(x, y, key):
    return f'\t(no_connect\n\t\t(at {fx(x)} {fx(y)})\n\t\t(uuid "{U("nc", key)}")\n\t)\n'

def E_text(t, x, y, size=2.5, bold=True, key=None):
    b = "\n\t\t\t\t(bold yes)" if bold else ""
    t = t.replace('"', '\\"')
    return f'\t(text "{t}"\n\t\t(exclude_from_sim no)\n\t\t(at {fx(x)} {fx(y)} 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size {size} {size}){b}\n\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "{U("t", key or t)}")\n\t)\n'

def E_prop(name, val, x, y, hide, rot=0):
    h = "\n\t\t\t(hide yes)" if hide else ""
    val = val.replace("\\", "\\\\").replace('"', '\\"')
    return f'\t\t(property "{name}" "{val}"\n\t\t\t(at {fx(x)} {fx(y)} {rot}){h}\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'

def E_symbol(lib_id, ref, value, fp, x, y, rot, pins, extra, props_lib, key, ref_xy=None, val_xy=None):
    in_bom = extra.get("in_bom", "yes")
    s = f'\t(symbol\n\t\t(lib_id "{lib_id}")\n\t\t(at {fx(x)} {fx(y)} {rot})\n\t\t(unit 1)\n\t\t(exclude_from_sim no)\n\t\t(in_bom {in_bom})\n\t\t(on_board yes)\n\t\t(dnp no)\n\t\t(uuid "{U("s", key)}")\n'
    rx, ry = ref_xy or (x, y - 2.54); vx, vy = val_xy or (x, y + 2.54)
    hidden_ref = ref.startswith("#")
    s += E_prop("Reference", ref, rx, ry, hidden_ref)
    s += E_prop("Value", value, vx, vy, False)
    s += E_prop("Footprint", fp, x, y, True)
    s += E_prop("Datasheet", extra.get("Datasheet", props_lib.get("Datasheet", "")), x, y, True)
    s += E_prop("Description", extra.get("Description", props_lib.get("Description", "")), x, y, True)
    for k in ("LCSC", "MPN", "Sourcing", "Note"):
        if k in extra: s += E_prop(k, extra[k], x, y, True)
    for p in pins:
        s += f'\t\t(pin "{p["num"]}"\n\t\t\t(uuid "{U("p", key, p["num"])}")\n\t\t)\n'
    s += "\t)\n"
    return s

GND_PROPS = {"Description": 'Power symbol creates a global label with name "GND" , ground'}
_counter = {"gnd": 0, "flag": 0}
def E_gnd(x, y, key, rot=0):
    """power:GND with its pin at (x,y); rot 0 hangs down, 90 points left, 270 points right."""
    _counter["gnd"] += 1
    vx, vy = {0: (x, y + 3.81), 90: (x - 3.81, y), 270: (x + 3.81, y)}[rot]
    return E_symbol("power:GND", "#PWR%03d" % _counter["gnd"], "GND", "", x, y, rot, [dict(num="1")], {}, GND_PROPS, ("gnd", key),
                    ref_xy=(x, y), val_xy=(vx, vy))

FLAG_PROPS = {"Description": "Special symbol for telling ERC where power comes from"}
def E_flag(x, y, key):
    _counter["flag"] += 1
    return E_symbol("power:PWR_FLAG", "#FLG%03d" % _counter["flag"], "PWR_FLAG", "", x, y, 0, [dict(num="1")], {}, FLAG_PROPS, ("flag", key),
                    ref_xy=(x, y), val_xy=(x, y - 3.81))

# ---------------- layout ----------------
STUB = 5.08
LABEL_ROOM = 28.0     # horizontal room for a label beside a stub
VROOM = 22.0
GRID = 1.27
def snap(v): return round(round(v / GRID) * GRID, 4)

def outward(rot):
    # symbol coords (y up): rot 0 pin points right (body to the right) -> outward is -x
    return {0: (-1, 0), 180: (1, 0), 90: (0, -1), 270: (0, 1)}[rot]

def place_part(part, X, Y, body):
    """Emit one part at (X,Y). Returns text."""
    ref, lib_id, value, fp, pinmap, extra = part
    blk, pins, props = flatten(lib_id)
    body["libs"][lib_id] = blk
    out = ""
    vis = [p for p in pins if not p["hide"]]
    xs = [p["x"] for p in vis] or [0]; ys = [p["y"] for p in vis] or [0]
    top = Y - max(ys) - 3.0
    ref_xy = (X + min(xs) - 1.0, top - 1.27); val_xy = (X + min(xs) - 1.0, top + 1.27)
    if len(vis) <= 2:
        ref_xy = (X + 2.0, Y - 1.27); val_xy = (X + 2.0, Y + 1.27)
    out += E_symbol(lib_id, ref, value, fp, X, Y, 0, pins, extra, props, ("part", ref), ref_xy, val_xy)
    seen = set()
    for p in pins:
        if p["num"] in seen: continue
        seen.add(p["num"])
        net = pinmap.get(p["num"])
        if net is None:
            raise SystemExit(f"{ref}: pin {p['num']} ({p['name']}) has no net in design.py")
        px, py = X + p["x"], Y - p["y"]
        ox, oy = outward(p["rot"]); oy = -oy   # to schematic y-down
        if p["hide"]:
            continue   # hidden power pins connect by name
        key = (ref, p["num"])
        if net == "NC":
            out += E_nc(px, py, key); continue
        if net == "GND":
            if (ox, oy) == (0, 1):
                out += E_wire(px, py, px, py + STUB, key) + E_gnd(px, py + STUB, key)
            elif (ox, oy) == (0, -1):
                out += E_wire(px, py, px, py - STUB, key) + E_wire(px, py - STUB, px + 5.08, py - STUB, (key, "g")) + E_gnd(px + 5.08, py - STUB, key, 270)
            else:
                ex, ey = px + ox * STUB, py + oy * STUB
                out += E_wire(px, py, ex, ey, key)
                out += E_gnd(ex, ey, key, 90 if ox < 0 else 270)
            continue
        ex, ey = px + ox * STUB, py + oy * STUB
        out += E_wire(px, py, ex, ey, key)
        if (ox, oy) == (-1, 0):   rot, just = 180, "right bottom"
        elif (ox, oy) == (1, 0):  rot, just = 0, "left bottom"
        elif (ox, oy) == (0, -1): rot, just = 90, "left bottom"
        else:                     rot, just = 270, "right bottom"
        out += E_label(net, ex, ey, rot, just, key)
        body["netlabels"].setdefault(net, []).append((ex, ey))
    return out

def part_extent(part):
    blk, pins, props = flatten(part[1])
    vis = [p for p in pins if not p["hide"]]
    xs = [p["x"] for p in vis] or [0]; ys = [p["y"] for p in vis] or [0]
    left = -min(xs) + STUB + LABEL_ROOM; right = max(xs) + STUB + LABEL_ROOM
    up = max(ys) + VROOM; down = -min(ys) + STUB + 2.54 + 8.0
    if len(vis) <= 2: left = right = max(left, right) * 0.6
    return left, right, up, down

def block_height(block, width):
    """Estimate the block's height with the same packing as the placement loop."""
    x0 = 0.0; yy = 8 + 3.5 * len(block.get("notes", [])) + 6
    cx, rowh, cy = x0, 0.0, yy
    for part in block["parts"]:
        l, r, u, d = part_extent(part)
        if cx + l + r > x0 + width and cx > x0:
            cx = x0; cy += rowh + 6; rowh = 0
        cx += l + r; rowh = max(rowh, u + d)
    return cy + rowh + 20

def main():
    body = {"libs": {}, "netlabels": {}}
    for pw in ("power:GND", "power:PWR_FLAG"):
        body["libs"][pw] = flatten(pw)[0]
    items = ""
    # blocks flow down a column; a new column starts when the page height would be exceeded
    COL_W, PAGE_H, COL_X0 = 390.0, 820.0, 20.0
    col, x0, y0 = 0, COL_X0, 20.0
    for block in D.BLOCKS:
        title, parts, width = block["title"], block["parts"], block.get("width", COL_W - 20.0)
        est = block_height(block, width)
        if y0 + est > PAGE_H and y0 > 20.0:
            col += 1; x0 = COL_X0 + col * COL_W; y0 = 20.0
        notes = block.get("notes", [])
        items += E_text(title, x0, y0 + 4, 3.0, True, key=("title", title))
        yy = y0 + 8
        for i, n in enumerate(notes):
            items += E_text(n, x0, yy + 3.5 * (i + 1), 1.5, False, key=("note", title, i)); 
        yy += 3.5 * len(notes) + 6
        cx, rowh, cy = x0, 0.0, yy
        for part in parts:
            l, r, u, d = part_extent(part)
            if cx + l + r > x0 + width and cx > x0:
                cx = x0; cy += rowh + 6; rowh = 0
            X, Y = snap(cx + l), snap(cy + u)
            items += place_part(part, X, Y, body)
            cx += l + r; rowh = max(rowh, u + d)
        y0 = cy + rowh + 20
    # power flags on the rails that need them
    for net in D.POWER_FLAG_NETS:
        pts = body["netlabels"].get(net)
        if not pts: raise SystemExit(f"PWR_FLAG net {net} has no label")
        ex, ey = pts[0]
        items += E_wire(ex, ey, ex, ey - 2.54, ("flagw", net)) + E_flag(ex, ey - 2.54, net)
    # GND needs a PWR_FLAG too: nothing on the sheet has a power-output pin on it
    gx, gy = 20.32, 35.56
    items += E_flag(gx, gy, "GND") + E_wire(gx, gy, gx, gy + 2.54, ("flagw", "GND")) + E_gnd(gx, gy + 2.54, ("flag", "GND"))
    # single-label nets are usually mistakes
    for net, pts in sorted(body["netlabels"].items()):
        if len(pts) < 2: print("WARNING: net with one label:", net)
    libs = "".join(body["libs"][k] for k in sorted(body["libs"]))
    libs = "\n".join("\t" + ln if ln else ln for ln in libs.splitlines())
    hdr = f'(kicad_sch\n\t(version 20260306)\n\t(generator "eeschema")\n\t(generator_version "10.0")\n\t(uuid "{U("sheet")}")\n\t(paper "A0")\n\t(title_block\n\t\t(title "{D.TITLE}")\n\t\t(date "{D.DATE}")\n\t\t(rev "7.1")\n'
    for i, c in enumerate(D.COMMENTS): hdr += f'\t\t(comment {i+1} "{c}")\n'
    hdr += "\t)\n\t(lib_symbols\n" + libs + "\n\t)\n"
    tail = '\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n\t(embedded_fonts no)\n)\n'
    open(OUT, "w").write(hdr + items + tail)
    print("wrote", OUT, "parts:", sum(len(b["parts"]) for b in D.BLOCKS), "nets:", len(body["netlabels"]))

if __name__ == "__main__":
    main()
