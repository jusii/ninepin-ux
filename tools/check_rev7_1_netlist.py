#!/usr/bin/env python3
"""
amigahid-pico rev7.1 netlist cross-checker (re-targeted copy of check_rev7_netlist.py).

asserts the rev7 schematic's netlist against an explicit rev7 connectivity
spec held in this file, and cross-checks that spec against hardware/rev7.1/design.py
(the generator's input), so a change in either one shows up here.

usage (note: /nas is noexec, always invoke through the interpreter):

    python3 tools/check_rev7_1_netlist.py                  # export + check
    python3 tools/check_rev7_1_netlist.py --work DIR       # scratch dir for exports
    python3 tools/check_rev7_1_netlist.py --no-git         # skip the frozen-file guard

kicad-cli only ever runs on a scratch COPY of the rev7 project (schematic, project
file, library tables, symbol library), so nothing it writes can land in the repo.
the regenerated schematic for section J is written to the scratch dir as well.

exit code is 0 only when every assertion passes.

no third-party modules: stdlib only.
"""

import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

# ---------------------------------------------------------------------------
# repo layout
# ---------------------------------------------------------------------------

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(TOOLS_DIR)
REV7_DIR = os.path.join(REPO_ROOT, "hardware", "rev7.1")
SCH_NAME = "amigahid-pico-rev7.1.kicad_sch"
DEF_SCH = os.path.join(REV7_DIR, SCH_NAME)
# what the scratch copy of the project needs for export + erc to behave like in-tree
PROJECT_FILES = [SCH_NAME, "amigahid-pico-rev7.1.kicad_pro", "sym-lib-table",
                 "fp-lib-table", "rev7-symbols.kicad_sym"]
PROJECT_DIRS = ["rev7.pretty"]      # ERC resolves footprint links against fp-lib-table
KICAD_CLI = shutil.which("kicad-cli") or "/usr/bin/kicad-cli"

# the older boards left the tree in the ninepin-ux restructure; each lives on, byte-identical, in an
# annotated tag on the last commit that carried it. section I pins the tags and the key files in them.
FROZEN_COMMIT = "287d90977a97ed140ebd98286712b06c671f3bae"
FROZEN_TAGS = {
    "hw/upstream-rev5": {
        "kicad/amigahid-pico.kicad_sch": "b3d755153dbb16602e1fdb98f5ddc8c34d712ae2",
        "kicad/amigahid-pico.kicad_pcb": "5b342ab6f9a4c03c8174a9fad53fa15a68271456",
    },
    "hw/ux-rev1": {
        "kicad/rev6/amigahid-pico-rev6.kicad_sch": "13019d0945caa896db4429b03957830e76a38a21",
        "kicad/rev6/amigahid-pico-rev6.kicad_pcb": "a1de425e26d527ca3ef61e7ef07dbff99c632cb3",
    },
    "hw/ux-rev7": {
        "kicad/rev7/amigahid-pico-rev7.kicad_sch": "12921c2ea7572b0c4a472011dafa91b34145dc62",
        "kicad/rev7/amigahid-pico-rev7.kicad_pcb": "c5b7baae756f02c0433de69021ca787c8e7f7938",
    },
}

# ---------------------------------------------------------------------------
# THE SPEC
# ---------------------------------------------------------------------------

GND = "GND"
NC = "__NC__"          # pin must be unconnected

# Amiga-facing 5 V nets, in bus-switch order (design.py AMIGA_NETS)
AMIGA_NETS = [
    "kbreset", "kbdata", "kbclock", "cont1_b3", "cont1_b2", "cont1_v", "cont1_b1", "cont1_h", "cont1_vq", "cont1_hq",
    "kbin_clk", "kbin_dat", "cont2_v", "cont2_b1", "cont2_h", "cont2_vq", "cont2_hq", "cont2_b2", "cont2_b3",
]
RPACKS = {"RN1": ["cont1_b1", "cont1_v", "cont1_b2", "cont1_b3"], "RN2": ["NC", "cont1_hq", "cont1_vq", "cont1_h"],
          "RN3": ["NC", "kbdata", "kbclock", "NC"], "RN4": ["NC", "NC", "kbin_dat", "kbin_clk"]}
NO_PULLUP = {"kbreset"}                            # the A500 pulls KBRESET up itself; a board pull-up would let a USB-C-powered keyboard lift the I/O rail
PULLUPS = {"cont2_v": "R61", "cont2_b1": "R62", "cont2_h": "R63", "cont2_vq": "R64", "cont2_hq": "R65", "cont2_b2": "R66", "cont2_b3": "R67"}
RPACK_COMMON = {"RN4": "KBIN_5V"}                  # the kbin pull-ups follow the keyboard-in supply; every other array is on AMIGA_IO5V

# every connector pin each Amiga net must reach -- and nothing else of connector kind
AMIGA_CONN = {
    "kbclock":  {("J10", "5"), ("J1", "1")},
    "kbdata":   {("J10", "1"), ("J1", "2")},
    "kbreset":  {("J1", "3"), ("J8", "3")},
    "kbin_clk": {("J12", "5"), ("J8", "1")},
    "kbin_dat": {("J12", "1"), ("J8", "2")},
    "cont1_v":  {("P1", "1")}, "cont1_h":  {("P1", "2")}, "cont1_vq": {("P1", "3")},
    "cont1_hq": {("P1", "4")}, "cont1_b3": {("P1", "5")}, "cont1_b1": {("P1", "6")},
    "cont1_b2": {("P1", "9")},
    "cont2_v":  {("P2", "1")}, "cont2_h":  {("P2", "2")}, "cont2_vq": {("P2", "3")},
    "cont2_hq": {("P2", "4")}, "cont2_b3": {("P2", "5")}, "cont2_b1": {("P2", "6")},
    "cont2_b2": {("P2", "9")},
}

# switch placement: net index 0..9 -> U6 A1..A10, 10..18 -> U7 A1..A9
KBIN_SW = {"kbin_clk": "Q3", "kbin_dat": "Q2"}        # BSS138 shifters: drain (3) = Amiga side, source (2) = GPIO side, gate (1) = +3V3


def switch_slot(i):
    return ("U6", i) if i < 10 else ("U7", i - 10)


def a_pin(k):
    return str(2 + k)      # A1..A10 = pins 2..11


def b_pin(k):
    return str(22 - k)     # B1..B10 = pins 22..13


# pull-up array slot: net index i -> RN(1 + i//4) element i%4, pin i%4+1, common pin 8-i%4
def rpack_slot(i):
    net = AMIGA_NETS[i]
    if net in NO_PULLUP:
        return None, None, None
    if net in PULLUPS:
        return PULLUPS[net], "1", "2"
    for rn, nets in RPACKS.items():
        if net in nets:
            k = nets.index(net)
            return rn, str(k + 1), str(8 - k)
    raise KeyError(net)


# RP2350A: gpio -> package pin (QFN-60) and gpio -> net
GPIO_PIN = {
    0: "2", 1: "3", 2: "4", 3: "5", 4: "7", 5: "8", 6: "9", 7: "10", 8: "12", 9: "13",
    10: "14", 11: "15", 12: "16", 13: "17", 14: "18", 15: "19", 16: "27", 17: "28",
    18: "29", 19: "31", 20: "32", 21: "33", 22: "34", 23: "35", 24: "36", 25: "37",
    26: "40", 27: "41", 28: "42", 29: "43",
}
GPIO_NET = {
    0: "i2c_sda", 1: "i2c_scl", 2: "ws_drv", 3: "exp_int",
    4: "kbreset_lo", 5: "kbdata_lo", 6: "kbclock_lo",
    7: "cont1_b3_lo", 8: "cont1_b2_lo", 9: "cont1_v_lo", 10: "cont1_b1_lo",
    11: "cont1_h_lo", 12: "cont1_vq_lo", 13: "cont1_hq_lo",
    14: "kbin_clk_lo", 15: "kbin_dat_lo",
    16: "cont2_v_lo", 17: "cont2_b1_lo", 18: "cont2_h_lo", 19: "cont2_vq_lo", 20: "cont2_hq_lo", 21: "cont2_b2_lo", 22: "cont2_b3_lo",
    23: "wl_on", 24: "wl_d", 25: "wl_cs",
    26: "pio_dp", 27: "pio_dm", 28: "amiga_sense", 29: "wl_clk",
}
# the non-Amiga gpio nets: exact node sets (U1's own pin included)
GPIO_OTHER_NODES = {
    "exp_int":     {("U10", "13")},
    "i2c_sda":     {("J3", "4"), ("R41", "2"), ("U10", "15")},
    "i2c_scl":     {("J3", "3"), ("R42", "2"), ("U10", "14")},
    "ws_drv":      {("Q1", "1")},
    "pio_dp":      {("R70", "1"), ("R72", "1")},
    "pio_dm":      {("R71", "1"), ("R73", "1")},
    "wl_on":       {("U9", "12"), ("U9", "13")},
    "wl_d":        {("U9", "5"), ("R55", "2"), ("R56", "2")},
    "wl_cs":       {("U9", "9")},
    "amiga_sense": {("R46", "2"), ("R47", "1"), ("C11", "1")},
    "wl_clk":      {("U9", "3")},
    "kbin_clk_lo": {("Q3", "2")},
    "kbin_dat_lo": {("Q2", "2")},
}

U1_IOVDD = ["1", "11", "20", "30", "38", "45"]
U1_3V3_OTHER = ["49", "53", "54"]           # VREG_VIN, USB_OTP_VDD, QSPI_IOVDD
U1_DVDD = ["6", "23", "39", "50"]           # 50 = VREG_FB, tied to DVDD
# RP2350 QSPI pin -> (function, W25Q128 pin)
QSPI = {
    "55": ("QSPI_SD3", "7"), "56": ("QSPI_SCLK", "6"), "57": ("QSPI_SD0", "5"),
    "58": ("QSPI_SD2", "3"), "59": ("QSPI_SD1", "2"), "60": ("~{QSPI_SS}", "1"),
}

RM2_GND = ["1", "4", "7", "11", "15", "21"]

# BOM: every assembled part carries an LCSC number, THT connectors and switches included (JLCPCB places
# them too, decided 2026-10-15 after the parts review); only board-only items are exempt.
LCSC_EXEMPT = {}
GENERIC_PASSIVE_LIBS = {"Device:R", "Device:C", "Device:L", "Device:FerriteBead"}
BOARD_ONLY_LIBS = ("Jumper:SolderJumper",)    # copper only, nothing to source
# value regex -> footprint regex: every part whose value matches must use that footprint
VALUE_FOOTPRINT = [
    (r"^SS34$",          r"^Diode_SMD:D_SMA$"),
    (r"^PESD5V0S1BA$",   r"^Diode_SMD:D_SOD-323$"),
    (r"^SN74CBTD3861",   r"^Package_SO:SSOP-24_"),
    (r"^TLV75733",       r"^Package_SON:WSON-6-1EP_2x2mm"),
    (r"^RM2$",           r"^rev7:RaspberryPi_RM2$"),
    (r"^RP2350A$",       r"^Package_DFN_QFN:QFN-60-"),
]
# minimum population of each listed value (catches a part silently dropped)
VALUE_COUNT = {"SS34": 2, "B5819W": 3, "BSS138": 2, "PESD5V0S1BA": 14, "PESD5V0S1BL": 7, "SN74CBTD3861DBQR": 2, "TLV75733PDRVR": 1, "TCA9554PWR": 1,
               "RM2": 1, "RP2350A": 1}

# ---------------------------------------------------------------------------
# value parsing (resistors / capacitors)
# ---------------------------------------------------------------------------

_MULT = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "": 1.0,
         "r": 1.0, "k": 1e3, "M": 1e6}


def parse_value(v):
    """'4.7k' -> 4700, '5k1' -> 5100, '100nF' -> 1e-7, '27' -> 27. None if unparseable."""
    v = (v or "").strip().replace("Ω", "").replace("ohm", "")
    v = re.sub(r"[FfHh]$", "", v)
    m = re.fullmatch(r"(\d+)([pnuµmrkKM])(\d+)", v)
    if m:
        unit = m.group(2) if m.group(2) != "K" else "k"
        return float("%s.%s" % (m.group(1), m.group(3))) * _MULT[unit]
    m = re.fullmatch(r"(\d+(?:\.\d+)?)([pnuµmrkKM]?)", v)
    if m:
        unit = m.group(2) if m.group(2) != "K" else "k"
        return float(m.group(1)) * _MULT[unit]
    return None


def near(a, b, tol=0.05):
    return a is not None and b is not None and abs(a - b) <= tol * abs(b)


# ---------------------------------------------------------------------------
# netlist model (kicadxml)
# ---------------------------------------------------------------------------


def norm(raw):
    """'/AMIGA_5V' -> 'AMIGA_5V'; unconnected-(...) nets -> NC."""
    if raw.startswith("unconnected-"):
        return NC
    return raw[1:] if raw.startswith("/") else raw


class Netlist(object):
    def __init__(self, path):
        root = ET.parse(path).getroot()
        if root.tag != "export":
            raise ValueError("%s: not a kicad xml netlist" % path)
        self.comps = {}          # ref -> dict
        self.net_nodes = {}      # net -> set((ref, pin))
        self.pin_net = {}        # (ref, pin) -> net (NC for unconnected)
        self.pin_func = {}       # (ref, pin) -> pin function without the _<pin> suffix
        self.comp_pins = {}      # ref -> [pin]
        self.raw_names = {}      # net -> set(raw)
        comps = root.find("components")
        for comp in (comps if comps is not None else []):
            ref = comp.get("ref")
            fields = {}
            fl = comp.find("fields")
            for f in (fl if fl is not None else []):
                fields[f.get("name")] = (f.text or "").strip()
            ls = comp.find("libsource")
            self.comps[ref] = {
                "value": (comp.findtext("value") or "").strip(),
                "footprint": (comp.findtext("footprint") or "").strip(),
                "lib_id": ("%s:%s" % (ls.get("lib"), ls.get("part"))) if ls is not None else "",
                "fields": fields,
            }
        unconnected = 0
        nets = root.find("nets")
        for net in (nets if nets is not None else []):
            raw = net.get("name", "")
            key = norm(raw)
            if key == NC:
                unconnected += 1
            self.raw_names.setdefault(key, set()).add(raw)
            for node in net.findall("node"):
                ref, pin = node.get("ref"), node.get("pin")
                fn = node.get("pinfunction", "") or ""
                fn = re.sub(r"_%s$" % re.escape(pin), "", fn)
                self.pin_func[(ref, pin)] = fn
                self.comp_pins.setdefault(ref, []).append(pin)
                self.pin_net[(ref, pin)] = key
                if key != NC:
                    self.net_nodes.setdefault(key, set()).add((ref, pin))
        self.unconnected = unconnected

    def net_of(self, ref, pin):
        return self.pin_net.get((ref, pin))

    def nodes(self, net):
        return self.net_nodes.get(net, set())

    def value(self, ref):
        return self.comps.get(ref, {}).get("value", "")

    def lib_id(self, ref):
        return self.comps.get(ref, {}).get("lib_id", "")

    def field(self, ref, name):
        return self.comps.get(ref, {}).get("fields", {}).get(name, "")

    def other_end(self, ref, pin):
        """net on the other pin of a 2-pin part."""
        pins = self.comp_pins.get(ref, [])
        if len(pins) != 2 or pin not in pins:
            return None
        o = pins[0] if pins[1] == pin else pins[1]
        return self.net_of(ref, o)

    def bridge(self, ref):
        pins = self.comp_pins.get(ref, [])
        if len(pins) != 2:
            return None
        return frozenset(self.net_of(ref, p) for p in pins)

    def parts_between(self, a, b, lib=None, value=None):
        out = []
        for ref in sorted(self.comps):
            if self.bridge(ref) != frozenset((a, b)):
                continue
            if lib and self.lib_id(ref) != lib:
                continue
            if value is not None and not near(parse_value(self.value(ref)), value):
                continue
            out.append(ref)
        return out

    def two_pin_on(self, net, lib=None):
        """2-pin parts with one pin on net: [(ref, pin_on_net, other_net)]"""
        out = []
        for ref, pin in sorted(self.nodes(net)):
            if len(self.comp_pins.get(ref, [])) == 2 and (lib is None or self.lib_id(ref) == lib):
                out.append((ref, pin, self.other_end(ref, pin)))
        return out


# ---------------------------------------------------------------------------
# report
# ---------------------------------------------------------------------------


class Report(object):
    def __init__(self):
        self.results = []   # (section, id, ok, message)
        self.notes = []
        self.warns = []
        self.section = "general"

    def sec(self, name):
        self.section = name

    def chk(self, ident, ok, message):
        self.results.append((self.section, ident, bool(ok), message))
        return bool(ok)

    def eq(self, ident, got, want, what):
        return self.chk(ident, got == want, "%s: expected %r, got %r" % (what, want, got))

    def note(self, msg):
        self.notes.append(msg)

    def warn(self, msg):
        self.warns.append(msg)

    @property
    def failures(self):
        return [r for r in self.results if not r[2]]

    def emit(self):
        order = []
        for sec, _, _, _ in self.results:
            if sec not in order:
                order.append(sec)
        print("")
        for sec in order:
            rows = [r for r in self.results if r[0] == sec]
            bad = [r for r in rows if not r[2]]
            state = "FAIL" if bad else "PASS"
            print("[%s] %s  (%d/%d checks passed)" % (state, sec, len(rows) - len(bad), len(rows)))
            for _, ident, _, msg in bad:
                print("        FAIL %-34s %s" % (ident, msg))
        if self.warns:
            print("")
            print("warnings (not fatal):")
            for w in self.warns:
                print("    ! %s" % w)
        if self.notes:
            print("")
            print("notes:")
            for n in self.notes:
                print("    - %s" % n)
        total = len(self.results)
        bad = len(self.failures)
        print("")
        print("summary: %d assertions, %d passed, %d FAILED" % (total, total - bad, bad))
        print("RESULT: %s" % ("PASS" if bad == 0 else "FAIL"))


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    old = sys.dont_write_bytecode
    sys.dont_write_bytecode = True     # keep __pycache__ out of hardware/rev7.1
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.dont_write_bytecode = old
    return mod


def design_parts(D):
    """ref -> (lib_id, value, footprint, pins, extra) from design.py."""
    out = {}
    for block in D.BLOCKS:
        for ref, lib, val, fp, pins, extra in block["parts"]:
            out[ref] = (lib, val, fp, pins, extra)
    return out


def fmt(nodes):
    return ", ".join("%s.%s" % n for n in sorted(nodes)) or "(none)"


def run(cmd, cwd):
    print("running: %s" % " ".join(cmd))
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, check=False)


# ---------------------------------------------------------------------------
# A. netlist export
# ---------------------------------------------------------------------------


def stage_project(sch, work):
    """copy the project into work/proj so kicad-cli cannot touch the repo."""
    src = os.path.dirname(os.path.abspath(sch))
    proj = os.path.join(work, "proj")
    if os.path.isdir(proj):
        shutil.rmtree(proj)
    os.makedirs(proj)
    for name in PROJECT_FILES:
        p = os.path.join(src, name)
        if os.path.exists(p):
            shutil.copy2(p, os.path.join(proj, name))
    for name in PROJECT_DIRS:
        p = os.path.join(src, name)
        if os.path.isdir(p):
            shutil.copytree(p, os.path.join(proj, name))
    return os.path.join(proj, os.path.basename(sch))


def check_export(rep, sch_copy, work):
    rep.sec("A. netlist export (kicadxml)")
    out = os.path.join(work, "rev7.xml")
    if os.path.exists(out):
        os.remove(out)
    res = run([KICAD_CLI, "sch", "export", "netlist", "--format", "kicadxml",
               "--output", out, sch_copy], work)
    ok = rep.chk("A.export", res.returncode == 0 and os.path.exists(out),
                 "kicad-cli netlist export failed (rc %d): %s"
                 % (res.returncode, (res.stdout + res.stderr).strip()[-400:]))
    if not ok:
        return None
    try:
        nl = Netlist(out)
    except (OSError, ValueError, ET.ParseError) as exc:
        rep.chk("A.parse", False, "cannot parse %s: %s" % (out, exc))
        return None
    rep.chk("A.parse", len(nl.comps) > 0 and len(nl.net_nodes) > 0,
            "netlist has %d components / %d nets" % (len(nl.comps), len(nl.net_nodes)))
    clashes = ["%s <- %s" % (k, sorted(v)) for k, v in sorted(nl.raw_names.items())
               if k != NC and len(v) > 1]
    rep.chk("A.no_name_collisions", not clashes,
            "distinct nets collapse to one name: " + "; ".join(clashes[:6]))
    print("netlist   : %s" % out)
    print("parsed    : %d components, %d nets (%d unconnected pins)"
          % (len(nl.comps), len(nl.net_nodes), nl.unconnected))
    return nl


# ---------------------------------------------------------------------------
# B. ERC
# ---------------------------------------------------------------------------


def check_erc(rep, sch_copy, work):
    rep.sec("B. ERC (all severities)")
    out = os.path.join(work, "rev7-erc.rpt")
    if os.path.exists(out):
        os.remove(out)
    res = run([KICAD_CLI, "sch", "erc", "--severity-all", "--exit-code-violations",
               "--output", out, sch_copy], work)
    text = ""
    if os.path.exists(out):
        with open(out, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    m = re.search(r"\*\*\s*ERC messages:\s*(\d+)", text)
    found = re.search(r"Found (\d+) violations", res.stdout + res.stderr)
    nviol = int(found.group(1)) if found else (int(m.group(1)) if m else None)
    rep.chk("B.erc_ran", os.path.exists(out), "no ERC report written (rc %d)" % res.returncode)
    rep.chk("B.zero_violations", res.returncode == 0 and nviol == 0,
            "ERC rc %d, %s violations (report %s)" % (res.returncode, nviol, out))


# ---------------------------------------------------------------------------
# C. Amiga-side integrity + gpio map
# ---------------------------------------------------------------------------


def check_amiga(rep, nl, D):
    rep.sec("C. Amiga-side integrity and GPIO map")
    # the spec in this file vs design.py
    rep.eq("C.spec.amiga_nets", list(D.AMIGA_NETS), AMIGA_NETS, "design.py AMIGA_NETS")
    rep.eq("C.spec.gpio_map", dict(D.GPIO), GPIO_NET, "design.py GPIO")
    rep.eq("C.spec.gpio_pins", {g: p for p, g in D.RP_PINS.items()}, GPIO_PIN, "design.py RP_PINS")

    for i, net in enumerate(AMIGA_NETS):
        sw, k = switch_slot(i)
        rn, rn_pin, rn_com = rpack_slot(i)
        tvs = "D%d" % (10 + i)
        nodes = nl.nodes(net)
        if net in KBIN_SW:                                       # BSS138 shifter instead of a CBTD channel
            sw = KBIN_SW[net]; a, bp, afn, bfn = "3", "2", "D", "S"
        else:
            a, bp, afn, bfn = a_pin(k), b_pin(k), "A%d" % (k + 1), "B%d" % (k + 1)
        want = set(AMIGA_CONN[net]) | {(sw, a), (tvs, "1")} | ({(rn, rn_pin)} if rn else set())
        rep.chk("C.%s.nodes" % net, nodes == want,
                "net %s: expected exactly {%s}, got {%s}" % (net, fmt(want), fmt(nodes)))
        # the parts on it are what they should be
        if net in KBIN_SW:
            cbtd = [r for r, _ in nodes if nl.value(r) == "BSS138"]
            rep.eq("C.%s.gate" % net, nl.net_of(sw, "1"), "+3V3", "%s gate" % sw)
        else:
            cbtd = [r for r, _ in nodes if nl.value(r).startswith("SN74CBTD3861")]
        packs = [r for r, _ in nodes if (nl.lib_id(r) == "Device:R_Pack04" or nl.lib_id(r) == "Device:R")
                 and near(parse_value(nl.value(r).replace("4x", "")), 10e3)]
        pesd = [r for r, _ in nodes if nl.value(r) in ("PESD5V0S1BA", "PESD5V0S1BL")]
        rep.chk("C.%s.parts" % net, len(cbtd) == 1 and len(packs) == (0 if net in NO_PULLUP else 1) and len(pesd) == 1,
                "net %s: want one switch / one 10k pull-up / one PESD5V0S1BA, got %s / %s / %s"
                % (net, cbtd, packs, pesd))
        rep.eq("C.%s.pin_func" % net, nl.pin_func.get((sw, a)), afn, "%s pin %s function" % (sw, a))
        rep.eq("C.%s.tvs_gnd" % net, nl.net_of(tvs, "2"), GND, "%s pin 2" % tvs)
        # B side, same switch, mirrored position, one gpio
        lo = net + "_lo"
        gpios = [g for g, n in GPIO_NET.items() if n == lo]
        rep.chk("C.%s.gpio_unique" % net, len(gpios) == 1, "%s maps to gpios %s" % (lo, gpios))
        if len(gpios) != 1:
            continue
        g = gpios[0]
        want_lo = {(sw, bp), ("U1", GPIO_PIN[g])}
        got_lo = nl.nodes(lo)
        rep.chk("C.%s.lo_nodes" % net, got_lo == want_lo,
                "net %s: expected exactly {%s}, got {%s}" % (lo, fmt(want_lo), fmt(got_lo)))
        rep.eq("C.%s.lo_func" % net, nl.pin_func.get((sw, bp)), bfn, "%s pin %s function" % (sw, bp))
    # U7 A1/A2 + B1/B2 (kbin moved to Q2/Q3) and B10/A10 unused
    for p in ("2", "3", "21", "22", "11", "13"):
        rep.eq("C.U7.%s_nc" % p, nl.net_of("U7", p), NC, "U7 pin %s open" % p)
    rep.eq("C.spec.rpacks", {k: list(v) for k, v in D.RPACKS.items()}, RPACKS, "design.py RPACKS")
    rep.eq("C.spec.pullups", dict(D.PULLUPS), PULLUPS, "design.py PULLUPS")
    for rn, nets in RPACKS.items():
        for k, n in enumerate(nets):
            if n == "NC":
                rep.eq("C.%s.%d_nc" % (rn, k + 1), (nl.net_of(rn, str(k + 1)), nl.net_of(rn, str(8 - k))), (NC, RPACK_COMMON.get(rn, "AMIGA_IO5V")), "%s element %d (spare: common side tied)" % (rn, k + 1))

    # full gpio map against the RP2350A pins, by pin number, and the symbol's pin names
    for g in range(30):
        pin = GPIO_PIN[g]
        fn = nl.pin_func.get(("U1", pin), "")
        rep.chk("C.gpio%d.func" % g, re.match(r"GPIO%d(/|$)" % g, fn) is not None,
                "U1 pin %s is %r, expected GPIO%d" % (pin, fn, g))
        rep.eq("C.gpio%d.net" % g, nl.net_of("U1", pin), GPIO_NET[g], "U1 pin %s (GPIO%d) net" % (pin, g))
    for net, extra in sorted(GPIO_OTHER_NODES.items()):
        g = [k for k, v in GPIO_NET.items() if v == net][0]
        want = set(extra) | {("U1", GPIO_PIN[g])}
        got = nl.nodes(net)
        rep.chk("C.%s.nodes" % net, got == want,
                "net %s: expected exactly {%s}, got {%s}" % (net, fmt(want), fmt(got)))


# ---------------------------------------------------------------------------
# D. power tree
# ---------------------------------------------------------------------------


def check_power(rep, nl):
    rep.sec("D. power tree")
    pf = nl.pin_func
    # F1: AMIGA_5V -> AMIGA_5V_FUSED
    rep.eq("D.F1", (nl.net_of("F1", "1"), nl.net_of("F1", "2")), ("AMIGA_5V", "AMIGA_5V_FUSED"), "F1 pins 1/2")
    rep.eq("D.AMIGA_5V.nodes", nl.nodes("AMIGA_5V"), {("F1", "1"), ("J1", "4"), ("J10", "4"), ("D4", "2")},
           "AMIGA_5V members")
    # D1 / D2 orientation by pin function
    for d, anode_net in (("D1", "AMIGA_5V_FUSED"), ("D2", "AUX_5V")):
        rep.eq("D.%s.func" % d, (pf.get((d, "1")), pf.get((d, "2"))), ("K", "A"), "%s pin functions" % d)
        rep.eq("D.%s.anode" % d, nl.net_of(d, "2"), anode_net, "%s anode" % d)
        rep.eq("D.%s.cathode" % d, nl.net_of(d, "1"), "SYS5V", "%s cathode" % d)
    rep.eq("D.D1.value", (nl.value("D1"), nl.value("D2")), ("SS34", "SS34"), "D1/D2 values")
    # AMIGA_5V_FUSED: exactly the fuse, D1, the bleeder and the I/O-rail diode D4
    fused = {("F1", "2"), ("D1", "2"), ("R60", "1")}
    got = nl.nodes("AMIGA_5V_FUSED")
    rep.chk("D.AMIGA_5V_FUSED.nodes", got == fused,
            "AMIGA_5V_FUSED: expected exactly {%s}, got {%s}" % (fmt(fused), fmt(got)))
    # AMIGA_IO5V (2026-10-16): the three OR diodes' cathodes, the switch VCCs, decoupling, array commons (not RN4), port-2 pull-ups, sense divider
    io = {("D4", "1"), ("D5", "1"), ("D6", "1"), ("U6", "24"), ("U7", "24"), ("C13", "1"), ("C14", "1"), ("R46", "1")}
    for rn, nets in RPACKS.items():
        if RPACK_COMMON.get(rn, "AMIGA_IO5V") == "AMIGA_IO5V":
            for k in range(4):
                io.add((rn, str(8 - k)))          # all commons, spare elements included
    for r in PULLUPS.values():
        io.add((r, "2"))
    got = nl.nodes("AMIGA_IO5V")
    rep.chk("D.AMIGA_IO5V.nodes", got == io, "AMIGA_IO5V: expected exactly {%s}, got {%s}" % (fmt(io), fmt(got)))
    for d, anode in (("D4", "AMIGA_5V"), ("D5", "cont1_5v"), ("D6", "cont2_5v")):
        rep.eq("D.%s" % d, (nl.value(d), pf.get((d, "1")), pf.get((d, "2")), nl.net_of(d, "1"), nl.net_of(d, "2")), ("B5819W", "K", "A", "AMIGA_IO5V", anode), "%s value / orientation" % d)
    rep.eq("D.cont1_5v.nodes", nl.nodes("cont1_5v"), {("P1", "7"), ("D5", "2")}, "port 1 pin 7 feeds only D5")
    rep.eq("D.cont2_5v.nodes", nl.nodes("cont2_5v"), {("P2", "7"), ("D6", "2")}, "port 2 pin 7 feeds only D6")
    rep.eq("D.io_amiga_bridge", nl.parts_between("AMIGA_IO5V", "AMIGA_5V"), ["D4"], "parts between AMIGA_IO5V and AMIGA_5V")
    rep.eq("D.io_fused_bridge", nl.parts_between("AMIGA_IO5V", "AMIGA_5V_FUSED"), [], "nothing bridges AMIGA_IO5V to AMIGA_5V_FUSED")
    rep.eq("D.io_sys_bridge", nl.parts_between("AMIGA_IO5V", "SYS5V"), [], "nothing bridges AMIGA_IO5V to SYS5V")
    rn4 = {("RN4", p) for p in ("5", "6", "7", "8")}
    rep.chk("D.RN4.kbin5v", rn4 <= nl.nodes("KBIN_5V"), "RN4 commons on KBIN_5V: {%s}" % fmt(nl.nodes("KBIN_5V")))
    for u in ("U6", "U7"):
        rep.eq("D.%s.vcc" % u, nl.net_of(u, "24"), "AMIGA_IO5V", "%s pin 24 (VCC)" % u)
        rep.eq("D.%s.vcc_func" % u, pf.get((u, "24")), "VCC", "%s pin 24 function" % u)
        rep.eq("D.%s.oe" % u, (pf.get((u, "23")), nl.net_of(u, "23")), ("~{OE}", GND), "%s pin 23 (/OE)" % u)
        rep.eq("D.%s.gnd" % u, nl.net_of(u, "12"), GND, "%s pin 12" % u)
    commons = {}
    for rn in RPACKS:
        for p in ("5", "6", "7", "8"):
            net = nl.net_of(rn, p)
            if net != NC:
                commons.setdefault(rn, set()).add(net)
    rep.chk("D.arrays.common", len(commons) == len(RPACKS) and all(v == {RPACK_COMMON.get(k, "AMIGA_IO5V")} for k, v in commons.items()),
            "array common sides: %s" % {k: sorted(v) for k, v in sorted(commons.items())})
    for bad in ("SYS5V", "+3V3", "AUX_5V"):
        leak = {(r, p) for r, p in nl.nodes(bad) if r in ("U6", "U7") or r.startswith("RN")}
        rep.chk("D.no_%s_on_amiga_side" % bad, not leak,
                "bus switch / pull-up pins on %s: %s" % (bad, fmt(leak)))
    # only D1 bridges AMIGA_5V_FUSED into SYS5V
    rep.eq("D.fused_sys_bridge", nl.parts_between("AMIGA_5V_FUSED", "SYS5V"), ["D1"],
           "parts between AMIGA_5V_FUSED and SYS5V")
    # USB-C VBUS -> AUX_5V
    vbus = {("J13", p) for p in ("A4", "B4", "A9", "B9")}
    got = nl.nodes("AUX_5V")
    rep.chk("D.AUX_5V.vbus", vbus <= got and ("D2", "2") in got,
            "AUX_5V {%s} must hold J13 VBUS A4/B4/A9/B9 and D2 anode" % fmt(got))
    for p in ("A4", "B4", "A9", "B9"):
        rep.eq("D.J13.%s" % p, pf.get(("J13", p)), "VBUS", "J13 %s function" % p)
    # SYS5V -> F2 -> USBA_5V = J4 VBUS ; SYS5V -> F3 -> KBIN_5V
    rep.eq("D.F2", (nl.net_of("F2", "1"), nl.net_of("F2", "2")), ("SYS5V", "USBA_5V"), "F2 pins 1/2")
    rep.eq("D.J4.vbus", (pf.get(("J4", "1")), nl.net_of("J4", "1")), ("VBUS", "USBA_5V"), "J4 pin 1")
    rep.eq("D.F3", (nl.net_of("F3", "1"), nl.net_of("F3", "2")), ("SYS5V", "KBIN_5V"), "F3 pins 1/2")
    rep.eq("D.KBIN_5V.conn", (nl.net_of("J12", "4"), nl.net_of("J8", "4")), ("KBIN_5V", "KBIN_5V"),
           "J12 pin 4 / J8 pin 4")
    for r in ("F1", "F2", "F3"):
        rep.eq("D.%s.lib" % r, nl.lib_id(r), "Device:Polyfuse", "%s symbol" % r)
    # U3
    rep.eq("D.U3.in_en", (pf.get(("U3", "6")), nl.net_of("U3", "6"), pf.get(("U3", "4")), nl.net_of("U3", "4")),
           ("IN", "SYS5V", "EN", "SYS5V"), "U3 IN/EN")
    rep.eq("D.U3.out", (pf.get(("U3", "1")), nl.net_of("U3", "1")), ("OUT", "+3V3"), "U3 OUT")
    rep.eq("D.U3.gnd", (nl.net_of("U3", "3"), nl.net_of("U3", "7")), (GND, GND), "U3 GND + thermal pad")
    # +3V3 has exactly one source
    src = [(r, p) for r, p in nl.nodes("+3V3") if (r, p) != ("U3", "1") and pf.get((r, p)) in ("OUT", "VOUT")]
    rep.chk("D.3V3.single_source", not src, "other regulator outputs on +3V3: %s" % fmt(src))
    # DE9 pin 7s feed only the I/O rail diodes (2026-10-16), J10 pin 4
    for p, n in (("P1", "cont1_5v"), ("P2", "cont2_5v")):
        rep.eq("D.%s.pin7" % p, nl.net_of(p, "7"), n, "%s pin 7 (+5 V) into its OR diode" % p)
    rep.eq("D.J10.pin4", nl.net_of("J10", "4"), "AMIGA_5V", "J10 pin 4")
    # bulk caps on the rails
    # SYS5V: >= 30 uF of ceramics (C1 + C2) kept modest for Amiga-port inrush; USBA_5V: the host-port bulk behind F2
    for net, cap in (("SYS5V", 30e-6), ("USBA_5V", 100e-6)):
        total = sum((parse_value(nl.value(r)) or 0) for r in nl.parts_between(net, GND, lib="Device:C"))
        rep.chk("D.%s.bulk" % net, total >= cap, "only %.0fuF of bulk on %s, want >=%guF" % (total * 1e6, net, cap * 1e6))


# ---------------------------------------------------------------------------
# E. USB
# ---------------------------------------------------------------------------


def check_usb(rep, nl):
    rep.sec("E. USB: native pair to USB-C, PIO-USB pair to USB-A, ESD, CC")
    pf = nl.pin_func
    # data pairs: connector + USBLC6 on both lines + the 27R series resistor
    pairs = [("usbc_dp", {("J13", "A6"), ("J13", "B6"), ("U4", "3"), ("U4", "4"), ("R57", "2")}),
             ("usbc_dm", {("J13", "A7"), ("J13", "B7"), ("U4", "1"), ("U4", "6"), ("R58", "2")}),
             ("usba_dp", {("J4", "3"), ("U2", "3"), ("U2", "4"), ("R70", "2")}),
             ("usba_dm", {("J4", "2"), ("U2", "1"), ("U2", "6"), ("R71", "2")})]
    for net, r in (("pio_dp", "R72"), ("pio_dm", "R73")):                       # 15k host pull-downs on the MCU side of the 27R
        rs = nl.parts_between(net, GND, lib="Device:R", value=15e3)
        rep.chk("E.%s.15k" % net, rs == [r], "15k pull-down on %s: %s" % (net, rs))
    for net, nodes in pairs:
        got = nl.nodes(net)
        rep.chk("E.%s.nodes" % net, got == nodes,
                "net %s: expected exactly {%s}, got {%s}" % (net, fmt(nodes), fmt(got)))
    for u, vbus in (("U4", "AUX_5V"), ("U2", "USBA_5V")):
        rep.eq("E.%s.value" % u, nl.value(u), "USBLC6-2SC6", "%s value" % u)
        rep.eq("E.%s.rails" % u, (nl.net_of(u, "2"), nl.net_of(u, "5")), (GND, vbus), "%s pins 2/5" % u)
    for fn_p, fn_m, pin_p, pin_m in (("D+", "D-", "A6", "A7"), ("D+", "D-", "B6", "B7")):
        rep.eq("E.J13.%s" % pin_p, (pf.get(("J13", pin_p)), pf.get(("J13", pin_m))), (fn_p, fn_m),
               "J13 %s/%s functions" % (pin_p, pin_m))
    rep.eq("E.J4.data", (pf.get(("J4", "2")), pf.get(("J4", "3"))), ("D-", "D+"), "J4 data functions")
    # 27R series: native PHY pins 51/52 -> USB-C pair; GPIO26/27 (PIO-USB, D+ on the lower GPIO) -> USB-A pair
    for cnet, rnet, upin, ufn in (("usbc_dp", "usb_dp", "52", "USB_DP"), ("usbc_dm", "usb_dm", "51", "USB_DM"),
                                  ("usba_dp", "pio_dp", "40", "GPIO26/ADC0"), ("usba_dm", "pio_dm", "41", "GPIO27/ADC1")):
        rs = nl.parts_between(cnet, rnet, lib="Device:R", value=27)
        rep.chk("E.%s.27R" % rnet, len(rs) == 1, "27R between %s and %s: %s" % (cnet, rnet, rs))
        rep.eq("E.%s.u1" % rnet, (pf.get(("U1", upin)), nl.net_of("U1", upin)), (ufn, rnet), "U1 pin %s" % upin)
        rep.eq("E.%s.nodes" % rnet, len(nl.nodes(rnet)), 3 if rnet.startswith("pio") else 2, "%s member count" % rnet)
    rep.chk("E.no_mux", "U5" not in nl.comps, "the USB mux U5 was removed on 2026-10-15: both ports have their own pair")
    # CC pull-downs
    for pin, net in (("A5", "usb_cc1"), ("B5", "usb_cc2")):
        rep.eq("E.J13.%s" % pin, nl.net_of("J13", pin), net, "J13 %s" % pin)
        rs = nl.parts_between(net, GND, lib="Device:R", value=5.1e3)
        rep.chk("E.%s.5k1" % net, len(rs) == 1 and len(nl.nodes(net)) == 2,
                "%s: 5.1k to GND %s, members {%s}" % (net, rs, fmt(nl.nodes(net))))
    for p in ("A1", "B1", "A12", "B12", "SH"):
        rep.eq("E.J13.gnd.%s" % p, nl.net_of("J13", p), GND, "J13 %s" % p)
    rep.eq("E.J4.gnd", (nl.net_of("J4", "4"), nl.net_of("J4", "SH")), (GND, GND), "J4 GND/shield")
    # I2C expander U10 (TCA9554: internal pull-ups, review 2026-10-15), the encoder header and the VBUS sense divider
    rep.eq("E.U10.value", (nl.value("U10"), nl.lib_id("U10")), ("TCA9554PWR", "Interface_Expansion:TCA9554PW"), "U10")
    u10 = {"1": GND, "2": GND, "3": GND, "4": "enc_a", "5": "enc_b", "6": "enc_sw", "7": "enc_sp", "8": GND, "9": "vbus_sense",
           "13": "exp_int", "14": "i2c_scl", "15": "i2c_sda", "16": "+3V3"}
    for pin, net in u10.items():
        rep.eq("E.U10.%s" % pin, nl.net_of("U10", pin), net, "U10 pin %s" % pin)
    for pin in ("10", "11", "12"):
        rep.chk("E.U10.%s.nc" % pin, nl.net_of("U10", pin) == NC, "U10 pin %s must be left open" % pin)
    j7 = {"1": "enc_sw", "2": "enc_sp", "3": "enc_b", "4": GND, "5": "enc_a", "6": "+3V3"}
    for pin, net in j7.items():
        rep.eq("E.J7.%s" % pin, nl.net_of("J7", pin), net, "J7 pin %s" % pin)
    for net in ("enc_a", "enc_b", "enc_sw", "enc_sp"):
        rep.eq("E.%s.nodes" % net, len(nl.nodes(net)), 2, "%s: U10 pin and J7 pin only" % net)
    rs = nl.parts_between("AUX_5V", "vbus_sense", lib="Device:R", value=4.7e3)
    rep.chk("E.vbus.top", len(rs) == 1, "4.7k from AUX_5V to vbus_sense (TCA9554 VIH 0.8 VCC): %s" % rs)
    rs = nl.parts_between("vbus_sense", GND, lib="Device:R", value=10e3)
    rep.chk("E.vbus.bottom", len(rs) == 1 and len(nl.nodes("vbus_sense")) == 3, "10k from vbus_sense to GND %s, members {%s}" % (rs, fmt(nl.nodes("vbus_sense"))))


# ---------------------------------------------------------------------------
# F. RP2350 core
# ---------------------------------------------------------------------------


def check_core(rep, nl):
    rep.sec("F. RP2350 core")
    pf = nl.pin_func
    rep.eq("F.U1.value", (nl.value("U1"), nl.lib_id("U1")), ("RP2350A", "MCU_RaspberryPi:RP2350A"), "U1")
    rep.eq("F.U8.value", nl.value("U8"), "W25Q128JVSIQ", "U8 value")
    for up, (fn, fp) in sorted(QSPI.items()):
        net = nl.net_of("U1", up)
        rep.eq("F.qspi.%s.func" % up, pf.get(("U1", up)), fn, "U1 pin %s function" % up)
        rep.chk("F.qspi.%s" % up, net not in (None, NC, GND) and net == nl.net_of("U8", fp),
                "U1 pin %s (%s) on %s, U8 pin %s on %s" % (up, fn, net, fp, nl.net_of("U8", fp)))
    rep.eq("F.U8.power", (nl.net_of("U8", "8"), nl.net_of("U8", "4")), ("+3V3", GND), "U8 VCC/GND")
    # crystal
    rep.eq("F.xin", (pf.get(("U1", "21")), nl.net_of("U1", "21")), ("XIN", "xin"), "U1 pin 21")
    rep.eq("F.xout", (pf.get(("U1", "22")), nl.net_of("U1", "22")), ("XOUT", "xout"), "U1 pin 22")
    rep.eq("F.Y1", (nl.value("Y1"), nl.net_of("Y1", "1"), nl.net_of("Y1", "3"), nl.net_of("Y1", "2"), nl.net_of("Y1", "4")),
           ("12MHz", "xin", "xout_r", GND, GND), "Y1 value/pins 1,3,2,4")
    rep.eq("F.R54", nl.parts_between("xout", "xout_r", lib="Device:R", value=1e3), ["R54"], "1k xout -> xout_r")
    for net in ("xin", "xout_r"):
        caps = nl.parts_between(net, GND, lib="Device:C", value=15e-12)
        rep.chk("F.%s.15p" % net, len(caps) == 1, "15pF %s -> GND: %s" % (net, caps))
    # RUN / BOOT
    rep.eq("F.run", (pf.get(("U1", "26")), nl.net_of("U1", "26")), ("RUN", "run"), "U1 pin 26")
    rep.eq("F.R53", nl.parts_between("run", "run_sw", lib="Device:R", value=1e3), ["R53"], "1k run -> run_sw")
    rep.eq("F.SW2", nl.bridge("SW2"), frozenset(("run_sw", GND)), "SW2 nets")
    # rev7.1: U11 TLV803S supervisor wired-OR with SW2 on run_sw (open drain), VDD on +3V3, own 100nF
    rep.eq("F.U11", (nl.value("U11"), nl.field("U11", "LCSC"), nl.net_of("U11", "1"), nl.net_of("U11", "2"), nl.net_of("U11", "3")),
           ("TLV803SDBZR", "C132016", GND, "run_sw", "+3V3"), "U11 value/LCSC/pins 1 GND, 2 RESET, 3 VDD")
    rep.eq("F.run_sw.members", sorted(r for r, _ in nl.nodes("run_sw")), ["R53", "SW2", "U11"], "run_sw members: R53, SW2, U11 only")
    rep.eq("F.C40", (nl.value("C40"), nl.net_of("C40", "1"), nl.net_of("C40", "2")), ("100nF", "+3V3", GND), "C40 = U11 VDD decoupling")
    rep.eq("F.R52", nl.parts_between("qspi_ss", "qspi_ss_sw", lib="Device:R", value=1e3), ["R52"],
           "1k qspi_ss -> qspi_ss_sw")
    rep.eq("F.SW1", nl.bridge("SW1"), frozenset(("qspi_ss_sw", GND)), "SW1 nets")
    rep.eq("F.SW.values", (nl.value("SW1"), nl.value("SW2")), ("BOOT", "RESET"), "SW1/SW2 values")
    # SWD
    rep.eq("F.swd", (pf.get(("U1", "24")), pf.get(("U1", "25"))), ("SWCLK", "SWDIO"), "U1 pins 24/25")
    rep.eq("F.J2", (nl.net_of("J2", "1"), nl.net_of("J2", "2"), nl.net_of("J2", "3")),
           (nl.net_of("U1", "24"), GND, nl.net_of("U1", "25")), "J2 pins 1/2/3")
    rep.eq("F.swd.members", (len(nl.nodes("swclk")), len(nl.nodes("swdio"))), (2, 2), "swclk/swdio member counts")
    # supplies
    for p in U1_IOVDD:
        rep.eq("F.iovdd.%s" % p, (pf.get(("U1", p)), nl.net_of("U1", p)), ("IOVDD", "+3V3"), "U1 pin %s" % p)
    for p, fn in zip(U1_3V3_OTHER, ("VREG_VIN", "USB_OTP_VDD", "QSPI_IOVDD")):
        rep.eq("F.3v3.%s" % p, (pf.get(("U1", p)), nl.net_of("U1", p)), (fn, "+3V3"), "U1 pin %s" % p)
    for p in U1_DVDD:
        rep.eq("F.dvdd.%s" % p, nl.net_of("U1", p), "dvdd", "U1 pin %s (%s)" % (p, pf.get(("U1", p))))
    rep.eq("F.vreg_lx", (pf.get(("U1", "48")), nl.net_of("U1", "48")), ("VREG_LX", "vreg_lx"), "U1 pin 48")
    rep.eq("F.L1", nl.parts_between("vreg_lx", "dvdd", lib="Device:L"), ["L1"], "inductor vreg_lx -> dvdd")
    rep.chk("F.L1.value", near(parse_value(nl.value("L1")), 3.3e-6), "L1 is %r, want 3.3uH" % nl.value("L1"))
    rep.eq("F.vreg_lx.members", len(nl.nodes("vreg_lx")), 2, "vreg_lx member count")
    rep.eq("F.pgnd", (nl.net_of("U1", "47"), nl.net_of("U1", "61")), (GND, GND), "U1 pins 47/61")
    for p, net, r, fn in (("46", "vreg_avdd", "R59", "VREG_AVDD"), ("44", "adc_avdd", "R50", "ADC_AVDD")):
        rep.eq("F.%s.pin" % net, (pf.get(("U1", p)), nl.net_of("U1", p)), (fn, net), "U1 pin %s" % p)
        rep.eq("F.%s.filter" % net, nl.parts_between("+3V3", net, lib="Device:R"), [r], "R from +3V3 to %s" % net)
        rep.chk("F.%s.cap" % net, nl.parts_between(net, GND, lib="Device:C"), "no decoupling on %s" % net)
    n100 = nl.parts_between("+3V3", GND, lib="Device:C", value=100e-9)
    rep.chk("F.3v3.100n", len(n100) >= 8, "%d x 100nF on +3V3 (%s), want >= 8" % (len(n100), ", ".join(n100)))
    n47 = nl.parts_between("dvdd", GND, lib="Device:C", value=4.7e-6)
    rep.chk("F.dvdd.4u7", len(n47) >= 2, "%d x 4.7uF on dvdd, want >= 2 (pin 23 + regulator output)" % len(n47))
    rep.eq("F.L1.polarity", nl.net_of("L1", "1"), "dvdd", "L1 pad 1 (polarity dot) must be on dvdd / +1V1, pad 2 on VREG_LX")


# ---------------------------------------------------------------------------
# G. RM2
# ---------------------------------------------------------------------------


def check_rm2(rep, nl):
    rep.sec("G. RM2 radio")
    pf = nl.pin_func
    rep.eq("G.U9", (nl.value("U9"), nl.comps.get("U9", {}).get("footprint")),
           ("RM2", "rev7:RaspberryPi_RM2"), "U9 value/footprint")
    for pad, fn, g in (("3", "SCLK", 29), ("9", "CS", 25), ("5", "DIN", 24), ("12", "WL_ON", 23), ("13", "BT_ON", 23)):
        rep.eq("G.pad%s.func" % pad, pf.get(("U9", pad)), fn, "U9 pad %s function" % pad)
        rep.chk("G.pad%s.gpio%d" % (pad, g), nl.net_of("U9", pad) == nl.net_of("U1", GPIO_PIN[g]) == GPIO_NET[g],
                "U9 pad %s on %s, GPIO%d (U1 pin %s) on %s, want %s"
                % (pad, nl.net_of("U9", pad), g, GPIO_PIN[g], nl.net_of("U1", GPIO_PIN[g]), GPIO_NET[g]))
    for pad, fn, ohms in (("6", "DOUT", 470), ("10", "nIRQ", 10e3)):
        net = nl.net_of("U9", pad)
        rep.eq("G.pad%s.func" % pad, pf.get(("U9", pad)), fn, "U9 pad %s function" % pad)
        rs = nl.parts_between(net, "wl_d", lib="Device:R", value=ohms)
        rep.chk("G.pad%s.series" % pad, net not in (None, NC) and len(rs) == 1 and len(nl.nodes(net)) == 2,
                "U9 pad %s (%s) on %s: want one %g ohm to wl_d alone, got %s, members {%s}"
                % (pad, fn, net, ohms, rs, fmt(nl.nodes(net))))
    vin = nl.net_of("U9", "16")
    rep.eq("G.vin_vddio", (pf.get(("U9", "14")), pf.get(("U9", "16")), nl.net_of("U9", "14")),
           ("VDDIO", "VIN", vin), "U9 pads 14/16")
    rep.chk("G.vin_not_3v3", vin not in (None, NC, "+3V3", GND), "U9 VIN on %s, want a filtered rail" % vin)
    fb = nl.parts_between("+3V3", vin, lib="Device:FerriteBead")
    rep.eq("G.FB1", fb, ["FB1"], "ferrite bead +3V3 -> %s" % vin)
    rep.chk("G.vin.caps", len(nl.parts_between(vin, GND, lib="Device:C")) >= 2, "decoupling on %s" % vin)
    for pad in RM2_GND:
        rep.eq("G.gnd.%s" % pad, (pf.get(("U9", pad)), nl.net_of("U9", pad)), ("GND", GND), "U9 pad %s" % pad)


# ---------------------------------------------------------------------------
# H. BOM sanity
# ---------------------------------------------------------------------------


def check_bom(rep, nl, D):
    rep.sec("H. BOM sanity")
    nofp = sorted(r for r, c in nl.comps.items() if not c["footprint"])
    rep.chk("H.footprints", not nofp, "parts without a footprint: %s" % ", ".join(nofp))
    # rev7: footprints must exist in rev7.pretty
    missing = []
    for r, c in sorted(nl.comps.items()):
        if c["footprint"].startswith("rev7:"):
            p = os.path.join(REV7_DIR, "rev7.pretty", c["footprint"][5:] + ".kicad_mod")
            if not os.path.exists(p):
                missing.append("%s (%s)" % (r, c["footprint"]))
    rep.chk("H.rev7_footprints_exist", not missing, "rev7: footprints not in rev7.pretty: %s" % ", ".join(missing))
    # LCSC
    nolcsc = []
    for r, c in sorted(nl.comps.items()):
        if c["fields"].get("LCSC"):
            continue
        if r in LCSC_EXEMPT or c["lib_id"].startswith(BOARD_ONLY_LIBS):
            continue
        nolcsc.append("%s (%s %s)" % (r, c["lib_id"], c["value"]))
    rep.chk("H.lcsc", not nolcsc, "parts without an LCSC field: %s" % "; ".join(nolcsc))
    for r, why in sorted(LCSC_EXEMPT.items()):
        src = nl.field(r, "Sourcing")
        rep.chk("H.exempt.%s" % r, r in nl.comps and why in src.lower(),
                "%s is LCSC-exempt but its Sourcing field %r does not say %r" % (r, src, why))
    # value <-> footprint
    for vre, fre in VALUE_FOOTPRINT:
        hits = [r for r, c in nl.comps.items() if re.search(vre, c["value"])]
        bad = ["%s=%s" % (r, nl.comps[r]["footprint"]) for r in sorted(hits)
               if not re.search(fre, nl.comps[r]["footprint"])]
        rep.chk("H.fp.%s" % vre.strip("^$"), hits and not bad,
                "value /%s/ needs footprint /%s/: %s" % (vre, fre, ", ".join(bad) or "no such part"))
    for val, n in sorted(VALUE_COUNT.items()):
        got = sum(1 for c in nl.comps.values() if c["value"] == val)
        rep.chk("H.count.%s" % val, got >= n, "%d x %s, want >= %d" % (got, val, n))
    # netlist vs design.py part list (nothing dropped or added by the generator)
    dp = design_parts(D)
    rep.eq("H.refs_match_design", sorted(nl.comps), sorted(dp), "netlist refs vs design.py refs")
    mism = ["%s: %r/%r vs %r/%r" % (r, nl.value(r), nl.comps[r]["footprint"], v[1], v[2])
            for r, v in sorted(dp.items()) if r in nl.comps
            and (nl.value(r) != v[1] or nl.comps[r]["footprint"] != v[2] or nl.lib_id(r) != v[0])]
    rep.chk("H.values_match_design", not mism, "netlist differs from design.py: " + "; ".join(mism[:6]))


# ---------------------------------------------------------------------------
# I. frozen files
# ---------------------------------------------------------------------------


def git(repo, *args):
    return subprocess.run(["git", "-C", repo] + list(args), capture_output=True, check=False)


def check_frozen(rep, repo, enabled):
    rep.sec("I. old boards frozen in their hw/* tags")
    if not enabled:
        rep.note("frozen-tag git check skipped (--no-git)")
        return
    for tag, files in sorted(FROZEN_TAGS.items()):
        try:
            kind = git(repo, "cat-file", "-t", "refs/tags/" + tag)
        except OSError as exc:
            rep.chk("I.git", False, "cannot run git: %s" % exc)
            return
        if not rep.chk("I.%s.annotated" % tag, kind.returncode == 0 and kind.stdout.decode().strip() == "tag",
                       "tag %s missing or not annotated (%s)" % (tag, (kind.stdout + kind.stderr).decode().strip())):
            continue
        peeled = git(repo, "rev-parse", tag + "^{commit}").stdout.decode().strip()
        rep.chk("I.%s.commit" % tag, peeled == FROZEN_COMMIT,
                "tag %s points at %s, expected %s; never move these tags" % (tag, peeled[:12], FROZEN_COMMIT[:12]))
        for path, want in sorted(files.items()):
            got = git(repo, "rev-parse", "%s:%s" % (tag, path))
            have = got.stdout.decode().strip() if got.returncode == 0 else None
            rep.chk("I.%s:%s" % (tag, os.path.basename(path)), have == want,
                    "%s:%s is %s, expected blob %s" % (tag, path, have or "missing", want[:12]))


# ---------------------------------------------------------------------------
# J. generator + floorplan consistency
# ---------------------------------------------------------------------------


def check_generator(rep, sch, work, D):
    rep.sec("J. generator and floorplan consistency")
    out = os.path.join(work, "regen-" + SCH_NAME)
    if os.path.exists(out):
        os.remove(out)
    code = ("import sys; sys.dont_write_bytecode = True; sys.path.insert(0, sys.argv[1]); "
            "import make_sch as M; M.OUT = sys.argv[2]; M.main()")
    res = run([sys.executable, "-B", "-c", code, REV7_DIR, out], work)
    if not rep.chk("J.regen_ran", res.returncode == 0 and os.path.exists(out),
                   "make_sch.py failed (rc %d): %s" % (res.returncode, (res.stdout + res.stderr).strip()[-400:])):
        return
    warns = [ln for ln in res.stdout.splitlines() if ln.startswith("WARNING")]
    rep.chk("J.regen_no_warnings", not warns, "; ".join(warns))
    with open(out, "rb") as fh:
        new = fh.read()
    with open(sch, "rb") as fh:
        cur = fh.read()
    detail = ""
    if new != cur:
        a, b = cur.decode(errors="replace").splitlines(), new.decode(errors="replace").splitlines()
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                detail = "first difference at line %d: %r vs %r" % (i + 1, x[:80], y[:80])
                break
        else:
            detail = "lengths differ: %d vs %d lines" % (len(a), len(b))
    rep.chk("J.regen_identical", new == cur,
            "regenerated schematic differs from %s (%s); regenerated copy at %s" % (sch, detail, out))

    try:
        F = load_module("rev7_floorplan", os.path.join(REV7_DIR, "floorplan.py"))
    except Exception as exc:  # noqa: BLE001 - report any import failure
        rep.chk("J.floorplan_import", False, "cannot import floorplan.py: %s" % exc)
        return
    dp = design_parts(D)
    placed = list(F.CONNECTORS) + [F.J3]
    for ref, _val, lib, fp, *_rest in placed:
        want = "%s:%s" % (lib, fp)
        got = dp.get(ref, (None, None, None))[2]
        rep.chk("J.floorplan.%s" % ref, got == want,
                "floorplan %s footprint %s, design.py has %s" % (ref, want, got if ref in dp else "no such ref"))
    refs = [p[0] for p in placed]
    rep.chk("J.floorplan.unique", len(refs) == len(set(refs)), "duplicate refs in floorplan: %s" % refs)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main(argv=None):
    ap = argparse.ArgumentParser(description="cross-check the rev7 netlist against the rev7 spec")
    ap.add_argument("--sch", default=DEF_SCH, help="rev7 schematic (default: %s)" % DEF_SCH)
    ap.add_argument("--repo", default=REPO_ROOT, help="repository root")
    ap.add_argument("--work", default=None, help="scratch dir for exports (default: a fresh temp dir)")
    ap.add_argument("--no-git", action="store_true", help="skip the frozen-file git check")
    args = ap.parse_args(argv)

    work = args.work or tempfile.mkdtemp(prefix="rev7-check-")
    work = os.path.abspath(work)
    repo = os.path.abspath(args.repo)
    if work == repo or work.startswith(repo + os.sep):
        print("FATAL: --work must be outside the repository (%s)" % work)
        return 2
    os.makedirs(work, exist_ok=True)

    print("=== amigahid-pico rev7 netlist checker ===")
    print("schematic : %s" % args.sch)
    print("scratch   : %s" % work)
    if not os.path.exists(args.sch):
        print("\nFATAL: schematic not found: %s" % args.sch)
        return 2

    D = load_module("rev7_design", os.path.join(REV7_DIR, "design.py"))
    rep = Report()
    sch_copy = stage_project(args.sch, work)
    nl = check_export(rep, sch_copy, work)
    check_erc(rep, sch_copy, work)
    if nl is not None:
        check_amiga(rep, nl, D)
        check_power(rep, nl)
        check_usb(rep, nl)
        check_core(rep, nl)
        check_rm2(rep, nl)
        check_bom(rep, nl, D)
    check_frozen(rep, repo, not args.no_git)
    check_generator(rep, args.sch, work, D)

    rep.emit()
    return 0 if not rep.failures else 1


if __name__ == "__main__":
    sys.exit(main())
