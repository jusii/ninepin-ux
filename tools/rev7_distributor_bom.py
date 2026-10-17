#!/usr/bin/env python3
"""Price the rev7 BOM at Mouser and Digi-Key: what a bare-PCB + self-assembly build costs per board.

Reads kicad/rev7/fab/amigahid-pico-rev7-bom.csv (one line per LCSC part, written by make_fab.py), maps every line to
manufacturer part numbers (the JLC part first, then standard-brand equivalents for the China-only parts), picks the
first candidate the distributor has in stock, falls back to an in-stock keyword search for the passives, and writes
to kicad/rev7/bom/:
  rev7-<dist>-bom-1.csv        distributor BOM-import file for ONE board, exact per-board counts (multiply in the
                               distributor's BOM tool to order several)
  rev7-<dist>-bom-<N>.csv      the same for N boards, with each line raised to the cheaper price break
  rev7-distributor-bom.md      per-line table and totals at every board count, the substitutes, the unavailable lines

Needs ~/Devel/Omat/bom-tools (MOUSER_API_KEY, DIGIKEY_KEY/SECRET in ~/.config/bom-tools/distributors.env);
a distributor without credentials is skipped. usage: python3 tools/rev7_distributor_bom.py [--boards 2 10] [--spare 0]
"""
import csv, datetime, importlib, os, sys
sys.path.insert(0, os.path.expanduser("~/Devel/Omat/bom-tools"))
from bomprice import Cache, best_qty, resolve

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOM = os.path.join(REPO, "kicad/rev7/fab/amigahid-pico-rev7-bom.csv"); OUT = os.path.join(REPO, "kicad/rev7/bom")

# LCSC code -> (candidate MPNs in preference order, keyword fallback, description tokens the fallback must contain)
# The first MPN is what JLC sells under that code; the rest are the same part from brands the western distributors carry
# (the cheapest stocked candidate wins; a keyword hit must carry every token, e.g. the value and the package).
# Connectors, switches, the inductor, the crystal and the transistors have no keyword fallback: a search hit of the
# right value in a different package or height is not a substitute. The passives and TVS diodes may fall back.
# SC1509 is Raspberry Pi's own order code for the RP2350A, SC1169 for the RM2 (SC1631 is the Pico 2 board, not the module).
Y06 = lambda v: "RC0603FR-07%sL" % v
Y04 = lambda v: "RC0402FR-07%sL" % v
PARTS = {
    "C45783":  (["CL21A226MAQNNNE", "GRM21BR61E226ME44L", "CL21A226MAYNNNE", "C2012X5R1E226M125AB", "TMK212ABJ226MG-T", "C2012X5R1E226M125AC"], "22uF 0805 25V X5R", ["22uF", "0805", "25V"]),
    "C15850":  (["CL21A106KAYNNNE", "CL21B106KAYQNNE", "GRM21BR61E106KA73L", "C2012X5R1E106K125AB"], "10uF 0805 25V X5R", ["10uF", "0805", "25V"]),
    "C14663":  (["CC0603KRX7R9BB104", "CL10B104KB8NNNC", "GRM188R71H104KA93D"], "100nF 0603 50V X7R", ["0.1uF", "0603", "50V"]),
    "C413656": (["EEE-FKC151XAP", "EEEFKC151XAP", "EEE-FK1C151P", "865080345010"], None, []),
    "C15849":  (["CL10A105KB8NNNC", "CL10B105KB8NNNC", "CL10B105KB8NQNC", "GRM188R61H105KAALD", "CC0603KRX7R9BB105"], "1uF 0603 50V", ["1uF", "0603", "50V"]),
    "C23733":  (["CL05A475MP5NRNC", "CL05A475MQ5NRNC", "GRM155R61A475MEAAD", "C1005X5R1A475M050BC"], "4.7uF 0402 10V X5R", ["4.7uF", "0402", "10V"]),
    "C1525":   (["CL05B104KO5NNNC", "CL05B104KP5NNNC", "CC0402KRX7R7BB104", "GRM155R71C104KA88D"], "100nF 0402 16V X7R", ["0.1uF", "0402", "16V"]),
    "C1644":   (["CL10C150JB8NNNC", "CC0603JRNPO9BN150", "GRM1885C1H150JA01D"], "15pF 0603 50V C0G", ["15pF", "0603"]),
    "C1779":   (["CL21A475KAQNNNE", "CL21B475KAQNNNE", "GRM21BR61E475KA12L"], "4.7uF 0805 25V X5R", ["4.7uF", "0805", "25V"]),
    "C19666":  (["CL10A475KO8NNNC", "CL10A475KP8NNNC", "GRM188R61E475KE11D"], "4.7uF 0603 16V X5R", ["4.7uF", "0603"]),
    "C8678":   (["SS34", "SS34-E3/57T", "SS34-E3/9AT", "SS34FA", "B340A-13-F", "SS34-TP", "SX34_R1_00001"], None, []),
    "C8598":   (["B5819WSL-TP", "1N5819HW-7-F", "B5819W-TP", "B5819W-7-F"], "1N5819 SOD-123 schottky", ["SOD-123"]),
    "C19224":  (["PESD5V0S1BA,115", "PESD5V0S1BA", "PESD5V0S1BA-QX"], "PESD5V0S1BA", ["PESD5V0S1BA"]),
    "C84374":  (["PESD5V0S1BL,315", "PESD5V0S1BL-QYL", "PESD5V0S1BL"], "PESD5V0S1BL", ["PESD5V0S1BL"]),
    "C84140":  (["MF-MSMF075-2", "MF-MSMF075-2-99"], "MF-MSMF075", ["MSMF075"]),
    "C17313":  (["MF-MSMF050-2", "MF-MSMF050-2-99"], "MF-MSMF050", ["MSMF050"]),
    "C19330":  (["BLM18AG601SN1D", "BLM18AG601SN1"], "0603 ferrite bead 600 ohm", ["600", "0603"]),
    "C492407": (["61300811121", "TSW-108-07-G-S", "PH1-08-UA"], None, []),
    "C2937625": (["61300311121", "TSW-103-07-G-S", "PH1-03-UA"], None, []),
    "C2897367": (["61300411821", "PPTC041LFBN-RC", "SSW-104-01-G-S"], None, []),
    "C564920": (["67643-2910", "676432910", "0676432910"], "Molex 67643 USB-A receptacle", ["67643"]),
    "C65114":  (["61300621121", "TSW-103-07-G-D", "PH2-06-UA"], None, []),
    "C6089346": (["MD-60S", "MD-60SM"], None, []),
    "C5184243": (["USB4105-GF-A-120", "USB4105-GF-A"], None, []),
    "C17701150": (["MTQH201612S3R3MBT", "AOTA-B201610S3R3-101-T", "DFE201612E-3R3M=P2", "DFE201612E-3R3M", "CIGT201610EH3R3MNE", "74438323033"], None, []),
    "C52917434": (["WS2812B-2020-V6", "WS2812B-2020"], None, []),
    "C305939": (["5747840-4", "K22X-E9P-N", "L717SDE09PA4CH4R"], None, []),
    "C8545":   (["2N7002", "2N7002LT1G", "2N7002,215", "2N7002NXAKR", "2N7002HR"], None, []),
    "C82045":  (["BSS138LT1G", "BSS138", "BSS138,215", "BSS138P,215"], None, []),
    "C23162":  ([Y06("4K7"), "CRCW06034K70FKEA", "ERJ-3EKF4701V"], "0603 4.7k 1%", ["4.7k", "0603", "1%"]),
    "C23186":  ([Y06("5K1"), "CRCW06035K10FKEA", "ERJ-3EKF5101V"], "0603 5.1k 1%", ["5.1k", "0603", "1%"]),
    "C23179":  ([Y06("470R"), "CRCW0603470RFKEA", "ERJ-3EKF4700V"], "0603 470 ohm 1%", ["470", "0603", "1%"]),
    "C25803":  ([Y06("100K"), "CRCW0603100KFKEA", "ERJ-3EKF1003V"], "0603 100k 1%", ["100k", "0603", "1%"]),
    "C21190":  ([Y06("1K"), "CRCW06031K00FKEA", "ERJ-3EKF1001V"], "0603 1k 1%", ["1k", "0603", "1%"]),
    "C23140":  ([Y06("33R"), "CRCW060333R0FKEA", "ERJ-3EKF33R0V"], "0603 33 ohm 1%", ["33", "0603", "1%"]),
    "C25804":  ([Y06("10K"), "CRCW060310K0FKEA", "ERJ-3EKF1002V"], "0603 10k 1%", ["10k", "0603", "1%"]),
    "C25190":  ([Y06("27R"), "CRCW060327R0FKEA", "ERJ-3EKF27R0V"], "0603 27 ohm 1%", ["27", "0603", "1%"]),
    "C321830": ([Y06("27R"), "CRCW060327R0FKEA", "ERJ-3EKF27R0V"], "0603 27 ohm 1%", ["27", "0603", "1%"]),
    "C25744":  ([Y04("10K"), "CRCW040210K0FKED", "ERJ-2RKF1002X"], "0402 10k 1%", ["10k", "0402", "1%"]),
    "C25756":  ([Y04("15K"), "CRCW040215K0FKED", "ERJ-2RKF1502X"], "0402 15k 1%", ["15k", "0402", "1%"]),
    "C25725":  (["YC124-JR-0710KL", "EXB-28V103JX", "CAY10-103J4LF"], "resistor array 4x0402 10k", ["10k", "0402"]),
    "C29718":  (["YC164-JR-0710KL", "EXB-38V103JV", "CAY16-103J4LF"], "resistor array 4x0603 10k", ["10k", "0603"]),
    "C285519": (["PTS645VK392LFS", "PTS645VK39-2 LFS", "PTS645VK83-2 LFS"], None, []),
    "C42411118": (["RP2350A", "SC1509(13)-A4", "SC1509-A4", "SC1509(13)", "SC1509"], "RP2350A", ["RP2350A"]),
    "C7519":   (["USBLC6-2SC6", "USBLC6-2SC6Y"], "USBLC6-2SC6", ["USBLC6"]),
    "C2868428": (["TLV75733PDRVR"], "TLV75733PDRVR", ["TLV75733"]),
    "C44508":  (["SN74CBTD3861DBQR"], "SN74CBTD3861", ["CBTD3861"]),
    "C97521":  (["W25Q128JVSIQ", "W25Q128JVSIQ TR"], "W25Q128JVSIQ", ["W25Q128JV"]),
    "C49451547": (["RMC20452T", "SC1169"], None, []),
    "C477924": (["TCA9554PWR"], "TCA9554PWR", ["TCA9554"]),
    "C20625731": (["ABM8-272-T3", "ABM8-12.000MHZ-B2-T"], None, []),
}
ELSEWHERE = {"C84374": "Mouser (Nexperia PESD5V0S1BL; Digi-Key lists only a 10000-piece reel)", "C52917434": "LCSC / AliExpress (Worldsemi WS2812B-2020)", "C49451547": "Raspberry Pi resellers (RM2 is sold through the Pi channel)"}


def dist_module(name):
    m = importlib.import_module(name); m.load_env()
    need = {"mouser": ["MOUSER_API_KEY"], "digikey": ["DIGIKEY_KEY", "DIGIKEY_SECRET"]}[name]
    return m if all(os.environ.get(k) for k in need) else None


def main():
    args = sys.argv[1:]; boards = [2, 10]; spare = 0
    if "--boards" in args: i = args.index("--boards"); boards = [int(a) for a in args[i + 1:] if a.isdigit()]
    if "--spare" in args: spare = int(args[args.index("--spare") + 1])
    lines = list(csv.DictReader(open(BOM)))
    md = ["# rev7 distributor BOM (%s)" % datetime.date.today(), "",
          "Source `%s`; mapping in `tools/rev7_distributor_bom.py`. Parts are picked per line: the JLC part when the distributor stocks it, "
          "otherwise a standard-brand equivalent (marked *sub*), otherwise the cheapest in-stock keyword hit (*kw*). Order quantities are rounded "
          "to the seller's minimum/multiple%s and raised to the next price break whenever that lowers the line total (10 resistors at the "
          "10-piece price are cheaper than 6 at the 1-piece price); the quantity shown is what to order. Totals exclude shipping and VAT." % (
              os.path.relpath(BOM, REPO), " plus %d spare per line" % spare if spare else ""), ""]
    os.makedirs(OUT, exist_ok=True)
    for dname in ("mouser", "digikey"):
        dist = dist_module(dname)
        if not dist: md += ["## %s: skipped (no credentials in ~/.config/bom-tools/distributors.env)" % dname.title(), ""]; continue
        cache = Cache(dname); pnkey = dname + "_pn"
        rows, totals, missing = [], {n: 0.0 for n in boards}, []
        for ln in lines:
            code = ln["LCSC Part #"]; refs = ln["Designator"].split(","); per = len(refs)
            cands, kw, toks = PARTS.get(code, ([], None, []))
            hit, how = resolve(dist, cache, cands, kw, toks, need=per * max(boards) + spare)
            if hit is None or how == "unavailable":
                missing.append((ln, hit, cands)); rows.append((ln, per, hit, how, {})); continue
            q = {}
            for n in boards:
                q[n] = best_qty(per * n + spare, hit); totals[n] += q[n][2]
            rows.append((ln, per, hit, how, q))
        cur = next((r[2]["prices"][0][2] for r in rows if r[2] and r[2]["prices"]), "EUR")
        md += ["## %s (%s)" % (dname.title(), cur), "",
               "| Refs | /board | Part | Mfr | %s # | Stock | %s |" % (dname.title(), " | ".join("x%d qty / unit / line" % n for n in boards)),
               "|---|---|---|---|---|---|%s|" % "|".join("---" for _ in boards)]
        for ln, per, hit, how, q in rows:
            tag = {"exact": "", "substitute": " *sub*", "keyword": " *kw*"}.get(how, " **n/a**")
            if not q:
                md.append("| %s | %d | %s%s | %s | | %s | %s |" % (ln["Designator"].replace(",", ", "), per, (hit or {}).get("mpn") or ln["Comment"], tag,
                          (hit or {}).get("manufacturer") or "", (hit or {}).get("stock", ""), " | ".join("-" for _ in boards)))
                continue
            md.append("| %s | %d | %s%s | %s | %s | %d | %s |" % (ln["Designator"].replace(",", ", "), per, hit["mpn"], tag, hit["manufacturer"], hit[pnkey], hit["stock"],
                      " | ".join("%d / %.3f / %.2f" % q[n] for n in boards)))
        md += ["", "**Totals** (lines with a stocked part): " + ", ".join("%d boards %.2f %s (%.2f/board)" % (n, totals[n], cur, totals[n] / n) for n in boards), ""]
        if missing:
            md += ["Not available at %s:" % dname.title(), ""]
            for ln, hit, cands in missing:
                md.append("- %s (%s, JLC %s): tried %s%s" % (ln["Designator"], ln["Comment"], ln["LCSC Part #"], ", ".join(cands) or "nothing",
                          "; buy at " + ELSEWHERE[ln["LCSC Part #"]] if ln["LCSC Part #"] in ELSEWHERE else "; stock 0 / on order" if hit else ""))
            md.append("")
        for n in [1] + boards:
            with open(os.path.join(OUT, "rev7-%s-bom-%d.csv" % (dname, n)), "w", newline="") as f:
                w = csv.writer(f); w.writerow(["%s Part Number" % dname.title(), "Manufacturer Part Number", "Quantity", "Customer Reference", "Description"])
                for ln, per, hit, how, q in rows:
                    if q: w.writerow([hit[pnkey], hit["mpn"], per if n == 1 else q[n][0], ln["Designator"], hit["description"]])
        print("%s: %d lines priced, %d unavailable; %s" % (dname, len(rows) - len(missing), len(missing), ", ".join("x%d %.2f" % (n, totals[n]) for n in boards)))
    open(os.path.join(OUT, "rev7-distributor-bom.md"), "w").write("\n".join(md))


if __name__ == "__main__":
    main()
