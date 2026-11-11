#!/usr/bin/env python3
"""fab drift check for the rev7.1 board: is hardware/rev7.1/fab/ cut from the board file in the tree?

make_fab.py records the sha256 of the .kicad_pcb it cut the package from, and of the gerber zip, in
fab-manifest.md. This checks both against the files on disk and that both BOM / CPL pairs (SMT-only
and full assembly) are there. Run it last, after the board gates and make_fab.py: any board change (a
DRC refill-and-save included, if it moves a byte) makes it fail until the package is cut again.

    python3 tools/check_rev7_1_fab.py
"""
import hashlib
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REV = os.path.join(REPO_ROOT, "hardware", "rev7.1")
NAME = "ninepin-ux-rev7.1"
FAB = os.path.join(REV, "fab")


def sha256(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def main():
    results = []

    def chk(ident, ok, msg):
        results.append(ok)
        print("  %-4s %s %s" % (ident, "PASS" if ok else "FAIL", msg))

    print("=== ninepin ux rev7.1 fab drift check ===")
    manifest = os.path.join(FAB, "fab-manifest.md")
    if not os.path.isfile(manifest):
        chk("F0", False, "no fab-manifest.md: run hardware/rev7.1/fab/make_fab.py")
        return 1
    text = open(manifest).read()

    m = re.search(r"^Board `([^`]+)` sha256 ([0-9a-f]{64})$", text, re.M)
    pcb = os.path.join(REV, NAME + ".kicad_pcb")
    if chk("F1", bool(m) and m.group(1) == os.path.basename(pcb), "manifest names the board and its sha256") or not m:
        pass
    if m:
        chk("F2", sha256(pcb) == m.group(2),
            "board on disk is the one the package was cut from" if sha256(pcb) == m.group(2) else
            "board changed since the package was cut (%s, manifest %s): run make_fab.py" % (sha256(pcb)[:12], m.group(2)[:12]))

    z = re.search(r"^`(%s-gerbers\.zip)` sha256 ([0-9a-f]{16})," % re.escape(NAME), text, re.M)
    zpath = os.path.join(FAB, NAME + "-gerbers.zip")
    chk("F3", bool(z) and os.path.isfile(zpath) and sha256(zpath)[:16] == z.group(2),
        "gerber zip matches the manifest" if z else "manifest has no gerber zip line")
    for ident, suffix in (("F4", "-bom.csv"), ("F5", "-cpl.csv"), ("F6", "-full-bom.csv"), ("F7", "-full-cpl.csv")):
        p = os.path.join(FAB, NAME + suffix)
        chk(ident, os.path.isfile(p) and os.path.getsize(p) > 0, "%s present" % os.path.basename(p))

    failed = results.count(False)
    print("summary: %d assertions, %d passed, %d FAILED" % (len(results), len(results) - failed, failed))
    print("RESULT: %s" % ("PASS" if not failed else "FAIL"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
