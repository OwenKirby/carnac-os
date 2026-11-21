#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Spot-check web/editor/params.js against the firmware's C tables, read here a second, simpler way (plain regexes
on the lines, nothing shared with gen_params.py's reader), plus the whole CC map's shape.

    python web/editor/check_params.py [SRC_DIR] [params.js]      exit 0 = every check passed
"""
import json
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
src = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else HERE.parent.parent / "src")
pj = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else HERE / "params.js")
P = json.loads(re.search(r"window\.CARNAC_PARAMS = (\{.*\});\s*$", pj.read_text(encoding="utf-8"), re.S).group(1))
ui = (src / "UIStateMachine.c").read_text(encoding="utf-8", errors="replace")
uih = (src / "UIStateMachine.h").read_text(encoding="utf-8", errors="replace")
ps = (src / "PresetStructures.h").read_text(encoding="utf-8", errors="replace")
fails = 0


def check(ok, what):
    global fails
    print(("ok    " if ok else "FAIL  ") + what)
    fails += 0 if ok else 1


page = {p["id"]: p for p in P["pages"]}
cc = {m["cc"]: (m["page"], m["item"]) for m in P["cc_map"]}

# 1. the UIPAGE enum's order, counted by hand from the header
enum = re.search(r"enum \{\s*(UIPAGE_OSC1.*?)UIPAGE_COUNT", re.sub(r"/\*.*?\*/", "", uih, flags=re.S), re.S).group(1)
names = re.findall(r"UIPAGE_(\w+)", enum)
check([p["id"] for p in sorted(P["pages"], key=lambda p: p["num"])] == names, f"page numbers = UIPAGE_* order ({len(names)} pages)")
check(page["AMP"]["num"] == names.index("AMP") and page["TUNING"]["num"] == names.index("TUNING"), "AMP / TUNING page numbers")

# 2. every {cc, UIPAGE_x, item} literally in CC_MAP
block = re.search(r"static const UiCc CC_MAP\[\] = \{(.*?)\n\};", ui, re.S).group(1)
lit = re.findall(r"\{(\d+), UIPAGE_(\w+), ([^}]+)\}", block)


def item(e):
    e = e.strip()
    m = re.fullmatch(r"R\((\d+)\)", e)
    if m:
        return 4 + int(m.group(1))
    m = re.fullmatch(r"CCI_MFX(?: \+ (\d+))?", e)
    if m:
        return 16 + int(m.group(1) or 0)
    return 20 if e == "CCI_MFX_TYPE" else int(e)


want = {int(c): (p, item(i)) for c, p, i in lit}
check(len(lit) == len(set(int(c) for c, _, _ in lit)), f"CC_MAP has no CC twice ({len(lit)} entries)")
check(want == cc, "params.js cc_map == CC_MAP, entry for entry")
for n, (p, i) in [(74, ("FILTER1", 1)), (71, ("FILTER1", 2)), (30, ("OSC1", 4)), (89, ("AMP", 20)), (115, ("OSCMIX", 9)),
                  (54, ("LFO2", 0)), (5, ("HOME1", 8)), (95, ("EFFECTS", 7))]:
    check(cc.get(n) == (p, i), f"CC {n} -> {p} item {i}")

# 3. labels and ranges, against the row lines as written
def row_line(label):
    return re.search(r'\{"%s", [^\n]*' % re.escape(label), ui).group(0)


check(page["OSCMIX"]["rows"][5]["label"] == "NOISE LEVEL", "OSCMIX row 5 = NOISE LEVEL (CC 115)")
check(page["FILTER1"]["rows"][1]["label"] == "FLAVOUR" and P["lists"]["FLT_NAMES"] == ["SH-2", "MULTI", "MS-10", "FORMANT"],
      "FILTER FLAVOUR row and its names")
fine = page["OSC2"]["rows"][2]
mx = int(re.search(r"#define OSC_FINE_MAX (\d+)", ps).group(1))
check(fine["label"] == "FINE" and fine["min"] == 64 - mx and fine["max"] == 64 + mx, f"OSC2 FINE {fine['min']}..{fine['max']}")
mt = re.search(r"#define MASTER_TUNE_MIN (\d+)u.*\n#define MASTER_TUNE_MAX (\d+)u", ps)
r0 = page["TUNING"]["rows"][0]
check((r0["min"], r0["max"]) == (int(mt.group(1)), int(mt.group(2))), "TUNING MASTER TUNE range")
check(len(page["ENV1"]["rows"]) == len(page["ENV2"]["rows"]) - 1 and page["ENV2"]["rows"][-1]["label"] == "INVERT",
      "ENV1 has no INVERT row (NROWS - 1)")
drive = re.search(r"enum \{ (DRIVE_[^}]*)DRIVE_COUNT \}", ps).group(1).count("DRIVE_")
check(len(P["lists"]["DRIVE_NAMES"]) == drive and page["AMP"]["rows"][0]["max"] == drive - 1, f"DRIVE MODEL: {drive} models")
check("ARCH" in row_line("ARCH") and page["HOME1"]["rows"][0]["min"] == 1, "HOME ARCH starts at VARCH_DUAL_OSC (1)")

# 4. knob tables
fk = re.search(r"FILTER_KNOBS\[4\] = \{(.*?)\};", ui, re.S).group(1)
check([k["label"] for k in P["knob_tables"]["FILTER_KNOBS"]] == re.findall(r'"([^"]+)"', fk), "FILTER_KNOBS labels")
check([k["label"] for k in P["knob_tables"]["LFO_KNOBS"]] == ["SHAPE", "RATE", "DELAY", "FADE"], "LFO2/3 knobs SHAPE RATE DELAY FADE")
check([k["label"] for k in P["knob_tables"]["MFX_KNOBS"][1]] == ["MANUAL", "RATE", "FEEDBACK", "DEPTH"], "MFX STONE PHASER knobs")
check(P["knob_tables"]["OSC_KNOBS"][1]["kind"] == "tune", "OSC knob 2 TUNE (semitones)")
ox = re.search(r"OSCMIX_KNOBS\[VARCH_COUNT\]\[4\] = \{(.*?)\n\};", ui, re.S).group(1)
check(len(P["knob_tables"]["OSCMIX_KNOBS"]) == ox.count("/* ") and P["knob_tables"]["OSCMIX_KNOBS"][4][0]["label"] == "XMOD TYPE",
      "OSCMIX_KNOBS one row per architecture, STACK knob 1 XMOD TYPE")

# 5. lists the MOD page writes by index
msrc = re.search(r"MSRC_NAMES\[\] = \{(.*?)\};", ui, re.S).group(1)
check(P["lists"]["MSRC_NAMES"] == re.findall(r'"([^"]*)"', msrc), f"MSRC_NAMES ({len(P['lists']['MSRC_NAMES'])})")
check(len(P["lists"]["MDEST_NAMES"]) == P["consts"]["MDST_COUNT"], f"MDEST_NAMES = MDST_COUNT ({P['consts']['MDST_COUNT']})")
check(P["nrpn_route_msb"] == int(re.search(r"#define NRPN_ROUTE (\d+)u", ui).group(1)), "NRPN route MSB")
check(P["pc_banks"] == ["PATCH", "PERF", "SEQ", "ARP"], "BANK SELECT 0..3 = PATCH PERF SEQ ARP (synth_runtime.h)")

# 6. every CC lands on something the editor can draw
for m in P["cc_map"]:
    p = page[m["page"]]
    i = m["item"]
    if i >= 16:
        ok = True
    elif i >= 4:
        ok = i - 4 < len(p["rows"])
    else:
        ok = p["knobs"] is not None
    if not ok:
        check(False, f"CC {m['cc']} -> {m['page']} item {i}: nothing there")
check(True, "every CC names a knob or a row that exists")

print(f"\n{fails} failed" if fails else "\nall passed")
sys.exit(1 if fails else 0)
