#!/usr/bin/env python3
"""Dump every footprint's courtyard box and package height to parts.json for the case fit check
(FreeCAD cannot import pcbnew). KiCad coordinates, mm. Run after any board change:
python3 hardware/case/export_parts.py"""
import json, os, sys
import pcbnew
HERE = os.path.dirname(os.path.abspath(__file__)); REV7 = os.path.join(os.path.dirname(HERE), "rev7.1"); REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
import check_rev7_1_pcb as C
b = pcbnew.LoadBoard(os.path.join(REV7, "amigahid-pico-rev7.1.kicad_pcb"))
out = []
for fp in sorted(b.Footprints(), key=lambda f: f.GetReference()):
    name = fp.GetFPIDAsString().split(":")[-1]
    c = fp.GetCourtyard(pcbnew.F_CrtYd); bb = c.BBox() if c.OutlineCount() else fp.GetBoundingBox(False, False)
    out.append({"ref": fp.GetReference(), "fp": name, "h": C.pkg_height(name),
                "box": [round(v / 1e6, 3) for v in (bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom())],
                "at": [round(fp.GetPosition().x / 1e6, 3), round(fp.GetPosition().y / 1e6, 3), fp.GetOrientationDegrees()]})
json.dump(out, open(os.path.join(HERE, "parts.json"), "w"), indent=0)
print(len(out), "footprints ->", os.path.join(HERE, "parts.json"))
