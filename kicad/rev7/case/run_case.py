#!/usr/bin/env python3
"""Rebuild the rev7 case: dump the footprints from the board, then run enclosure.py in a headless
FreeCAD (the flatpak's freecadcmd, or a freecadcmd on PATH). Writes the STLs, STEP, FCStd and
fit-report.txt next to this file. usage: python3 kicad/rev7/case/run_case.py"""
import os, shutil, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); REV7 = os.path.dirname(HERE)
subprocess.run([sys.executable, os.path.join(HERE, "export_parts.py")], check=True)
stub = 'REV7 = %r\nexec(compile(open(REV7 + "/case/enclosure.py").read(), REV7 + "/case/enclosure.py", "exec"), {"REV7": REV7, "__name__": "__main__"})\n' % REV7
stub_path = os.path.join(HERE, ".run_stub.py"); open(stub_path, "w").write(stub)
try:
    if shutil.which("freecadcmd"): cmd = ["freecadcmd", "-c", open(stub_path).read()]
    else: cmd = ["flatpak", "run", "--command=freecadcmd", "--filesystem=" + REV7, "org.freecad.FreeCAD", "-c", open(stub_path).read()]
    r = subprocess.run(cmd, capture_output=True, text=True)
    print("\n".join(l for l in (r.stdout + r.stderr).splitlines() if not l.startswith(("\x1b", "******", "** ", " Step", "Recompute", "saving"))))
    sys.exit(r.returncode)
finally:
    os.remove(stub_path)
