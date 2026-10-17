#!/usr/bin/env python3
"""Cut the JLCPCB order package for the rev7 board.

Writes, next to this script:
  amigahid-pico-rev7-gerbers.zip   fab layers only (4 copper, mask, paste, silk, outline, drills, job file)
  amigahid-pico-rev7-bom.csv       JLCPCB SMT/THT BOM: Comment, Designator, Footprint, LCSC Part #
  amigahid-pico-rev7-cpl.csv       JLCPCB pick-and-place: Designator, Mid X, Mid Y, Layer, Rotation
  fab-manifest.md                  what was cut from which commit, drill table, the DNP and hand-solder lines

Every value comes from the board file (footprint fields written by make_board.py from design.py), so the
package can only be regenerated, never hand-edited. kicad-cli rewrites the project file on export; the
script restores it from a copy taken first. Run from anywhere: python3 kicad/rev7/fab/make_fab.py
"""
import csv, hashlib, io, os, re, shutil, subprocess, sys, tempfile, zipfile
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__)); REV7 = os.path.dirname(HERE); REPO = os.path.dirname(os.path.dirname(REV7))
PCB = os.path.join(REV7, "amigahid-pico-rev7.kicad_pcb"); PRO = os.path.join(REV7, "amigahid-pico-rev7.kicad_pro"); PRL = os.path.join(REV7, "amigahid-pico-rev7.kicad_prl")
NAME = "amigahid-pico-rev7"
LAYERS = "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Mask,B.Mask,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,Edge.Cuts"
HAND = {"P1", "P2"}                      # deselect at JLC if their DE9 stock is short; hand-solder (Kycon K22X-E9P-N)


def run(*args):
    r = subprocess.run(args, capture_output=True, text=True)
    out = "\n".join(l for l in (r.stdout + r.stderr).splitlines() if 'assert "' not in l)
    if r.returncode: sys.exit("%s\n%s" % (" ".join(args), out))
    return out


def git(*args): return subprocess.run(["git", "-C", REPO] + list(args), capture_output=True, text=True).stdout.strip()


def main():
    head = git("rev-parse", "--short", "HEAD"); dirty = git("status", "--short", "--", "kicad/rev7/*.py", "kicad/rev7/*.kicad_pcb", "kicad/rev7/*.kicad_sch", "kicad/rev7/rev7.pretty", "tools")
    pro_bak = PRO + ".fabbak"; prl_bak = PRL + ".fabbak"; shutil.copy(PRO, pro_bak); shutil.copy(PRL, prl_bak)
    tmp = tempfile.mkdtemp(prefix="rev7fab-")
    try:
        run("kicad-cli", "pcb", "export", "gerbers", "--layers", LAYERS, "--no-x2", "--no-netlist", "--subtract-soldermask",
            "--use-drill-file-origin", "-o", tmp + "/", PCB)
        run("kicad-cli", "pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm", "--excellon-zeros-format", "decimal",
            "--drill-origin", "absolute", "--excellon-separate-th", "-o", tmp + "/", PCB)
    finally:
        shutil.move(pro_bak, PRO); shutil.move(prl_bak, PRL)
    files = sorted(f for f in os.listdir(tmp) if "drl_map" not in f)                    # JLC wants the fab layers and drills only
    zpath = os.path.join(HERE, NAME + "-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in files: z.write(os.path.join(tmp, f), f)

    # BOM + CPL from the board itself
    b = pcbnew.LoadBoard(PCB); bb = b.GetBoardEdgesBoundingBox(); y0 = bb.GetBottom() / 1e6
    bom, cpl, dnp, nolcsc = {}, [], [], []
    for fp in sorted(b.Footprints(), key=lambda f: (re.sub(r"\d+", "", f.GetReference()), int(re.sub(r"\D", "", f.GetReference()) or 0))):
        ref = fp.GetReference(); fields = {f.GetName(): f.GetText() for f in fp.GetFields()}
        if fp.IsDNP() or ref.startswith(("H", "JP")): dnp.append(ref); continue      # mounting holes and solder jumpers are not parts
        fpname = fp.GetFPIDAsString().split(":")[-1]; val = fp.GetValue(); lcsc = fields.get("LCSC", "").strip()
        if not lcsc: nolcsc.append(ref)
        key = (val, fpname, lcsc); bom.setdefault(key, []).append(ref)
        pos = fp.GetPosition(); layer = "Top" if fp.GetLayer() == pcbnew.F_Cu else "Bottom"
        cpl.append((ref, round(pos.x / 1e6, 4), round((y0 - pos.y / 1e6), 4), layer, round(fp.GetOrientationDegrees(), 2)))
    with open(os.path.join(HERE, NAME + "-bom.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (val, fpname, lcsc), refs in bom.items(): w.writerow([val, ",".join(refs), fpname, lcsc])
    with open(os.path.join(HERE, NAME + "-cpl.csv"), "w", newline="") as f:
        w = csv.writer(f); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in cpl: w.writerow([r[0], "%.4fmm" % r[1], "%.4fmm" % r[2], r[3], r[4]])

    # manifest: drill table from the excellon files, counts, hashes
    drills = {}
    for f in files:
        if f.endswith(".drl"):
            txt = open(os.path.join(tmp, f)).read(); tools = dict(re.findall(r"^(T\d+)C([\d.]+)", txt, re.M))
            hits = {}; cur = None
            for line in txt.splitlines():
                m = re.match(r"^(T\d+)$", line)
                if m: cur = m.group(1); continue
                if cur and re.match(r"^X[-\d.]+Y[-\d.]+", line): hits[cur] = hits.get(cur, 0) + 1
            drills[f] = [(tools[t], hits.get(t, 0)) for t in sorted(tools, key=lambda t: float(tools[t]))]
    sha = hashlib.sha256(open(zpath, "rb").read()).hexdigest()[:16]
    lines = ["# %s fab package" % NAME, "",
             "Cut from `board-rev7` @ `%s` (%s), `kicad-cli` %s, outline %.2f x %.2f mm (centreline), 4 layers." % (
                 head, "working tree clean" if not dirty else "WORKING TREE DIRTY: " + dirty.replace("\n", "; "),
                 run("kicad-cli", "--version").strip(), bb.GetWidth() / 1e6 - 0.1, bb.GetHeight() / 1e6 - 0.1), "",
             "`%s` sha256 %s, %d files: %s" % (os.path.basename(zpath), sha, len(files), ", ".join(files)), "",
             "## Drill table", ""]
    for f, tbl in drills.items():
        lines += ["%s: %s hits" % (f, sum(h for _, h in tbl))] + ["- %s mm x %d" % (d, h) for d, h in tbl] + [""]
    lines += ["## BOM: %d lines, %d placements; CPL rows %d" % (len(bom), sum(len(v) for v in bom.values()), len(cpl)), "",
              "- not placed / not in the BOM: %s (DNP and the mounting holes)" % ", ".join(dnp),
              "- without an LCSC code: %s" % (", ".join(nolcsc) or "none"),
              "- hand-solder candidates if JLC stock is short: %s" % ", ".join(sorted(HAND)),
              "- CPL Y is measured upward from the south board edge (KiCad origin flipped); rotations are KiCad's - check JLC's placement preview for the polarised parts (D1-D6, C4, U1-U3, U6-U10, Q1-Q3, L1, LED1)", ""]
    open(os.path.join(HERE, "fab-manifest.md"), "w").write("\n".join(lines))
    shutil.rmtree(tmp)
    print("\n".join(lines[:4] + lines[-6:]))


if __name__ == "__main__":
    main()
