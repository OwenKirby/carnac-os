#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Carnac web editor: the parameter list, read from the firmware's own tables.

    python web/editor/gen_params.py [SRC_DIR] [-o web/editor/params.js] [--json]

SRC_DIR is the firmware's src/ folder (default: this repo's src/). It reads, never writes, these:
  src/UIStateMachine.c / .h   the CC map (CC_MAP), the page table (UI_PAGES), every page's rows and knobs, the lists'
                              names, the architectures (ARCH_ORDER, VARCH_NAMES), the MFX's knobs and rows by type
  src/PresetStructures.h, src/dsp/*.h   the enums and #defines the tables count with (VAL_MAX, FLT_COUNT, ...)
  src/dsp/wavetables.c        the wavetable banks' names (the WAVE list's first entries)
  src/dsp/pcm_area*.bin       the PCM waves as built (the WAVE list's rest; an FM1 with other waves uploaded differs)
and writes params.js: `window.CARNAC_PARAMS = {...}`, a plain script (no module), so index.html opens from file://.

It is a small C reader for these tables, not a C compiler: if the firmware's tables change shape it stops with an
error naming what it could not read, rather than guess. The knob tables a page uses by architecture / MFX type come
from ui_sm_knob's switch, which is code: PAGE_KNOBS below mirrors it and says so.
"""
import argparse
import json
import pathlib
import re
import struct
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent

# ------------------------------------------------------------------------------------------------ C text
def strip_comments(s):
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == "\\" else 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith("/*", i):
            j = s.find("*/", i + 2)
            out.append(" ")
            i = n if j < 0 else j + 2
        elif s.startswith("//", i):
            j = s.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_top(s):
    """split on commas outside (), {}, [] and strings"""
    parts, depth, cur, i = [], 0, [], 0
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while s[j] != '"':
                j += 2 if s[j] == "\\" else 1
            cur.append(s[i:j + 1])
            i = j + 1
            continue
        if c in "({[":
            depth += 1
        elif c in ")}]":
            depth -= 1
        if c == "," and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    last = "".join(cur).strip()
    if last:
        parts.append(last)
    return parts


def unbrace(s):
    s = s.strip()
    if not (s.startswith("{") and s.endswith("}")):
        raise ValueError(f"not a braced initializer: {s[:80]!r}")
    return s[1:-1]


def balanced(s, i):
    """s[i] is an opening bracket: the index just past its match"""
    depth, j = 0, i
    while j < len(s):
        if s[j] in "({[":
            depth += 1
        elif s[j] in ")}]":
            depth -= 1
            if depth == 0:
                return j + 1
        elif s[j] == '"':
            j += 1
            while s[j] != '"':
                j += 2 if s[j] == "\\" else 1
        j += 1
    raise ValueError("unbalanced")


class CSource:
    """the enums, #defines and macros of a set of files, and expression evaluation over them"""

    NO_EXPAND = {"NROWS", "OFS", "HOFS", "FKT_OFS", "ENV_X_OFS", "LFO_X_OFS", "EOFS", "PERF", "ENGB", "INST", "ENG",
                 "MFX_ROW", "NOTE_IS_WHITE", "UI_BARRIER", "PS_STATIC_ASSERT", "offsetof", "NULL"}

    def __init__(self):
        self.consts = {}
        self.defines = {}            # object-like: name -> body
        self.macros = {}             # function-like: name -> (params, body)

    def add(self, text):
        text = strip_comments(text).replace("\\\r\n", " ").replace("\\\n", " ")
        for line in text.splitlines():
            m = re.match(r"\s*#\s*define\s+(\w+)(\(([^)]*)\))?\s*(.*)$", line)
            if not m:
                continue
            name, params, body = m.group(1), m.group(3), m.group(4).strip()
            if m.group(2) is not None and line[line.index(name) + len(name)] == "(":
                self.macros.setdefault(name, ([p.strip() for p in params.split(",") if p.strip()], body))
            else:
                self.defines.setdefault(name, body)
        for m in re.finditer(r"\benum\s*\w*\s*\{(.*?)\}", text, re.S):
            val = -1
            for item in split_top(m.group(1)):
                if "=" in item:
                    k, e = item.split("=", 1)
                    try:
                        val = self.ev(e)
                    except (KeyError, SyntaxError, ValueError):
                        break                    # (an enum counted from elsewhere: the editor never needs it)
                else:
                    k = item
                    val += 1
                self.consts.setdefault(k.strip(), val)
        return text

    def expand(self, s, depth=0):
        """expand the known macros in s (not the offset helpers: an offset is never evaluated)"""
        if depth > 20:
            raise ValueError("macro recursion: " + s[:80])
        out, i = [], 0
        changed = False
        for m in re.finditer(r'"(?:\\.|[^"\\])*"|\b[A-Za-z_]\w*\b', s):
            tok = m.group(0)
            if m.start() < i or tok.startswith('"') or tok in self.NO_EXPAND:
                continue
            if tok in self.macros:
                j = m.end()
                while j < len(s) and s[j].isspace():
                    j += 1
                if j >= len(s) or s[j] != "(":
                    continue
                end = balanced(s, j)
                args = split_top(s[j + 1:end - 1])
                params, body = self.macros[tok]
                for p, a in zip(params, args):
                    body = re.sub(r"\b%s\b" % re.escape(p), lambda _m, a=a: a, body)
                body = re.sub(r"\s*##\s*", "", body)
                out.append(s[i:m.start()])
                out.append(body)
                i = end
                changed = True
            elif tok in self.defines and (self.defines[tok].startswith("{") or
                                          re.match(r"(\w+)\s*\(", self.defines[tok]) and
                                          re.match(r"(\w+)", self.defines[tok]).group(1) in self.macros):
                out.append(s[i:m.start()])
                out.append(self.defines[tok])
                i = m.end()
                changed = True
        out.append(s[i:])
        r = "".join(out)
        return self.expand(r, depth + 1) if changed else r

    def ev(self, expr):
        s = self.expand(expr)
        s = re.sub(r"\(\s*(?:const\s+)?(?:u?int(?:8|16|32|64)_t|unsigned|int)\s*\)", "", s)
        s = re.sub(r"\b(0x[0-9A-Fa-f]+|\d+)[uUlL]+\b", r"\1", s)

        def ident(m):
            k = m.group(0)
            if k in self.consts:
                return str(self.consts[k])
            if k in self.defines:
                return "(%d)" % self.ev(self.defines[k])
            raise KeyError(k)
        s = re.sub(r"\b[A-Za-z_]\w*\b", ident, s)
        s = s.replace("/", "//")
        return int(eval("(" + " ".join(s.split()) + ")", {"__builtins__": {}}, {}))


# ------------------------------------------------------------------------------------------------ tables
def c_string(tok):
    tok = tok.strip()
    if tok == "NULL":
        return None
    m = re.fullmatch(r'"((?:\\.|[^"\\])*)"', tok)
    if not m:
        raise ValueError(f"not a string: {tok!r}")
    return m.group(1).encode().decode("unicode_escape")


def read_name_arrays(text):
    out = {}
    for m in re.finditer(r"static\s+const\s+char\s*\*\s*const\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", text, re.S):
        out[m.group(1)] = [c_string(t) for t in split_top(m.group(2))]
    for m in re.finditer(r"const\s+char\s*\*\s*const\s+(\w+)\s*\[[^\]]*\]\s*=\s*\{(.*?)\}\s*;", text, re.S):
        out.setdefault(m.group(1), [c_string(t) for t in split_top(m.group(2))])
    return out


def initializer(text, ctype, name):
    m = re.search(r"static\s+const\s+%s\s+%s\s*(\[[^\]]*\])*\s*=\s*" % (ctype, re.escape(name)), text)
    if not m:
        raise SystemExit(f"gen_params: no {ctype} {name} in the source")
    j = text.index(";", m.end())
    return text[m.end():j].strip()


def flag_names(expr):
    return [f for f in re.findall(r"\bRF_\w+", expr)]


def read_rows(src, text, names, table):
    body = src.expand(initializer(text, "UiRow", table))
    rows = []
    for e in split_top(unbrace(body)):
        f = split_top(unbrace(e))
        if len(f) != 8:
            raise SystemExit(f"gen_params: {table}: a row of {len(f)} fields: {e[:80]}")
        nm = f[6].strip()
        if nm != "NULL" and nm not in names:
            names_ref = None                     # an extern list (arp_genre_names): not needed by the editor
        else:
            names_ref = None if nm == "NULL" else nm
        rows.append({
            "label": c_string(f[0]),
            "min": src.ev(f[2]), "max": src.ev(f[3]),
            "rk": f[4].strip(),
            "flags": flag_names(f[5]),
            "names": names_ref,
            "mask": src.ev(f[7]),
        })
    return rows


def read_knobs(src, text, table):
    body = src.expand(initializer(text, "UiKnob", table))

    def one(e):
        f = split_top(unbrace(e))
        if len(f) != 5:
            raise SystemExit(f"gen_params: {table}: a knob of {len(f)} fields: {e[:80]}")
        nm = f[4].strip()
        return {"label": c_string(f[0]), "kind": f[2].strip(),
                "n": src.ev(f[3]) if f[3].strip() not in ("0", "0u") else 0,
                "names": None if nm == "NULL" else nm}
    items = split_top(unbrace(body))
    if items and items[0].lstrip().startswith("{") and split_top(unbrace(items[0]))[0].lstrip().startswith("{"):
        return [[one(k) for k in split_top(unbrace(row))] for row in items]       # [n][4]
    return [one(k) for k in items]


# ui_sm_knob's switch (UIStateMachine.c): which knob table a page reads. Code, not a table: kept in step by hand, and
# check_params.py fails if a table named here is gone.
PAGE_KNOBS = {
    "OSC1": "OSC_KNOBS", "OSC2": "OSC_KNOBS", "OSCMIX": "OSCMIX_KNOBS[arch]", "FILTER1": "FILTER_KNOBS",
    "FILTER2": "FILTER_KNOBS", "FLTMIX": "FLTMIX_KNOBS[arch]", "ENV1": "ENV_KNOBS", "ENV2": "ENV_KNOBS",
    "ENV3": "ENV_KNOBS", "LFO1": "LFO1_KNOBS", "LFO2": "LFO_KNOBS", "LFO3": "LFO_KNOBS", "AMP": "AMP_KNOBS",
    "EFFECTS": "EFFECTS_KNOBS", "MIX": "MIX_KNOBS", "HOME1": "HOME_KNOBS", "HOME2": "HOME_KNOBS",
    "HOME3": "ZONES_KNOBS", "GLOBAL": "GLOBAL_KNOBS", "TUNING": "TUNING_KNOBS",
}
KNOB_KIND = {"UIKNOB_NONE": "none", "UIKNOB_RAW": "raw", "UIKNOB_BIP": "bip", "UIKNOB_DEST": "dest",
             "UIKNOB_TUNE": "tune", "UIKNOB_SRC": "src", "UIKNOB_PERF": "perf", "UIKNOB_SEQ": "seq",
             "UIKNOB_LIST": "list", "UIKNOB_BPM": "bpm", "UIKNOB_NOTE": "note", "UIKNOB_PORTA": "porta",
             "UIKNOB_DTIME": "dtime"}


def pcm_wave_names(src_dir):
    """the WAVE list's PCM part as built: every PCM_WAVE entry, area 0's, then 1's, then 2's (pcm_store.c list_waves)"""
    names = []
    for a in range(4):
        f = src_dir / "dsp" / f"pcm_area{a}.bin"
        if not f.exists():
            continue
        img = f.read_bytes()
        magic, ver, count = struct.unpack_from("<IHH", img)
        if magic != 0x57505243:                  # "CRPW" (pcm_store.h PCM_MAGIC); a wavetable area is not PCM
            continue
        for i in range(count):
            e = img[32 + 36 * i: 32 + 36 * (i + 1)]
            if e[12] == 3:                       # PCM_WAVE
                names.append(e[:12].split(b"\0")[0].decode("ascii", "replace"))
    return names


# ------------------------------------------------------------------------------------------------ main
def build(src_dir):
    src = CSource()
    first = [src_dir / "PresetStructures.h", src_dir / "UIStateMachine.h"]
    for p in first + sorted(set((src_dir / "dsp").glob("*.h")) | set(src_dir.glob("*.h")) - set(first)):
        src.add(p.read_text(encoding="utf-8", errors="replace"))
    ui_c = src.add((src_dir / "UIStateMachine.c").read_text(encoding="utf-8", errors="replace"))
    names = read_name_arrays(ui_c)
    bank_names = read_name_arrays((src_dir / "dsp" / "wavetables.c").read_text(encoding="utf-8", errors="replace"))
    banks = bank_names.get("wavetable_bank_names")
    if not banks:
        raise SystemExit("gen_params: no wavetable_bank_names in src/dsp/wavetables.c")

    # the pages, by number (UIPAGE_*)
    page_ids = sorted((v, k[len("UIPAGE_"):]) for k, v in src.consts.items()
                      if k.startswith("UIPAGE_") and k != "UIPAGE_COUNT")
    page_num = {name: n for n, name in page_ids}
    pages_init = split_top(unbrace(src.expand(initializer(ui_c, "UiPage", "UI_PAGES"))))
    if len(pages_init) != src.consts["UIPAGE_COUNT"]:
        raise SystemExit(f"gen_params: UI_PAGES has {len(pages_init)} entries, UIPAGE_COUNT {src.consts['UIPAGE_COUNT']}")

    row_cache = {}

    def rows_of(table):
        if table not in row_cache:
            row_cache[table] = read_rows(src, ui_c, names, table)
        return row_cache[table]

    pages = []
    for (n, pid), e in zip(page_ids, pages_init):
        f = split_top(unbrace(e))
        table, nrows_expr, store = f[0].strip(), f[1].strip(), f[2].strip()
        rows = rows_of(table)
        nrows = src.ev(re.sub(r"NROWS\(\s*(\w+)\s*\)", lambda m: str(len(rows_of(m.group(1)))), nrows_expr))
        pages.append({"id": pid, "num": n, "store": store[len("ST_"):].lower(), "rows": rows[:nrows],
                      "knobs": PAGE_KNOBS.get(pid)})

    knob_tables = {}
    for t in sorted({v.split("[")[0] for v in PAGE_KNOBS.values()} | {"NOISE_KNOBS", "MFX_KNOBS", "MOD_KNOBS"}):
        knob_tables[t] = read_knobs(src, ui_c, t)

    def knob_out(k):
        return {"label": k["label"], "kind": KNOB_KIND[k["kind"]], "n": k["n"], "names": k["names"]}
    knob_tables = {t: ([[knob_out(k) for k in row] for row in v] if v and isinstance(v[0], list)
                       else [knob_out(k) for k in v]) for t, v in knob_tables.items()}

    # the CC map
    cc_body = initializer(ui_c, "UiCc", "CC_MAP")
    cc_body = re.sub(r"\bR\(\s*(\d+)\s*\)", lambda m: "(CCI_ROW + %s)" % m.group(1), cc_body)
    cc_map = []
    for e in split_top(unbrace(cc_body)):
        f = split_top(unbrace(e))
        cc_map.append({"cc": src.ev(f[0]), "page": f[1].strip()[len("UIPAGE_"):], "item": src.ev(f[2])})

    # the architectures: the offered ones in the ARCH row's order, and the pages each uses (varch_uses_*)
    order = [src.ev(x) for x in split_top(unbrace(re.search(
        r"ARCH_ORDER\[\]\s*=\s*(\{.*?\})\s*;", ui_c, re.S).group(1)))]
    ps = strip_comments((src_dir / "PresetStructures.h").read_text(encoding="utf-8", errors="replace"))
    uses = {}
    for fn in ("varch_uses_osc2", "varch_uses_filter2", "varch_uses_fltmix"):
        m = re.search(r"%s\s*\(uint8_t a\)\s*\{(.*?)\}" % fn, ps, re.S)
        if not m:
            raise SystemExit(f"gen_params: no {fn} in PresetStructures.h")
        uses[fn[len("varch_uses_"):]] = sorted(src.consts[v] for v in re.findall(r"\bVARCH_\w+", m.group(1)))
    varch = {k[len("VARCH_"):]: v for k, v in src.consts.items()
             if k.startswith("VARCH_") and k not in ("VARCH_COUNT", "VARCH_ALL", "VARCH_BIT")}
    archs = [{"code": c, "name": names["VARCH_NAMES"][c], "id": [k for k, v in varch.items() if v == c][0],
              "osc2": c in uses["osc2"], "filter2": c in uses["filter2"], "fltmix": c in uses["fltmix"]}
             for c in order]

    mfx_types = names["MFX_TYPE_NAMES"]
    mfx_ids = {k[len("MFX_"):]: v for k, v in src.consts.items()
               if re.fullmatch(r"MFX_[A-Z_]+", k) and k not in ("MFX_OFFERED", "MFX_COUNT") and v < len(mfx_types)
               and not k.startswith(("MFX_K_", "MFX_R_", "MFX_BF_", "MFX_QE_", "MFX_RS_"))}

    # the lists the editor shows, every one a table names (only those used)
    used = set()
    for p in pages:
        for r in p["rows"]:
            if r["names"]:
                used.add(r["names"])
    for t in knob_tables.values():
        for k in (sum(t, []) if t and isinstance(t[0], list) else t):
            if k["names"]:
                used.add(k["names"])
    used |= {"MSRC_NAMES", "MDEST_NAMES", "MFX_TYPE_NAMES", "MORPH_LABEL"}
    used.update(r["names"] for r in rows_of("MFX_ROWS") if r["names"])
    lists = {k: names[k] for k in sorted(used) if k in names}

    pcm = pcm_wave_names(src_dir)
    c = src.consts
    out = {
        "generated_from": "src/UIStateMachine.c CC_MAP, UI_PAGES, the row and knob tables (web/editor/gen_params.py)",
        "consts": {k: src.ev(k) for k in (
            "VAL_MAX", "NROUTE", "MSRC_COUNT", "MDST_COUNT", "OSC_COARSE_ZERO", "OSC_COARSE_MAX", "OSC_FINE_MAX",
            "MASTER_TUNE_MIN", "MASTER_TUNE_MAX", "MIDI_CLOCK_BPM_BASE", "NLAYER", "MFX_OFFERED", "NUM_BANKS",
            "CCI_ROW", "CCI_MFX", "CCI_MFX_TYPE", "CCI_MFX_ROW")},
        "nrpn_route_msb": src.ev(re.search(r"#define\s+NRPN_ROUTE\s+(\w+)", ui_c).group(1)),
        "pc_banks": [k[len("SYNTH_PCBANK_"):] for k, v in sorted(c.items(), key=lambda kv: kv[1])
                     if k.startswith("SYNTH_PCBANK_") and k != "SYNTH_PCBANK_COUNT"],
        "pages": pages,
        "knob_tables": knob_tables,
        "mfx": {"types": mfx_types, "ids": mfx_ids, "rows": rows_of("MFX_ROWS")},
        "archs": archs,
        "cc_map": cc_map,
        "nrpn_map": nrpn_map(src_dir),
        "lists": lists,
        "waves": {"banks": banks, "pcm": pcm},
    }
    return out


def nrpn_map(src_dir):
    """src/nrpn_map.h: the fixed NRPN numbers (frozen at V1), N(msb, lsb, PAGE, item) each"""
    t = (src_dir / "nrpn_map.h").read_text(encoding="utf-8")
    t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
    out = [{"msb": int(a), "lsb": int(b), "page": p, "item": int(i)}
           for a, b, p, i in re.findall(r"N\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,\s*(\d+)\s*\)", t)]
    if not out:
        raise SystemExit("gen_params: no N(...) lines in nrpn_map.h")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", nargs="?", default=str(REPO / "src"), help="the firmware's src/ folder")
    ap.add_argument("-o", "--out", default=str(HERE / "params.js"))
    ap.add_argument("--json", action="store_true", help="print the JSON instead of writing params.js")
    a = ap.parse_args()
    src_dir = pathlib.Path(a.src)
    if not (src_dir / "UIStateMachine.c").exists():
        raise SystemExit(f"gen_params: no UIStateMachine.c in {src_dir}")
    data = build(src_dir)
    if not data["cc_map"]:
        raise SystemExit(f"gen_params: {src_dir} has no CC_MAP (the CC map lives on proto/cc-map)")
    js = json.dumps(data, indent=1, ensure_ascii=False)
    if a.json:
        print(js)
        return
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* GENERATED by web/editor/gen_params.py from the firmware's src/ (UIStateMachine.c and friends).\n"
                "   Do not edit: run  python web/editor/gen_params.py <src>  again. */\n")
        f.write("window.CARNAC_PARAMS = " + js + ";\n")
    print(f"gen_params: {len(data['cc_map'])} CCs, {len(data['pages'])} pages, "
          f"{len(data['waves']['banks'])} banks + {len(data['waves']['pcm'])} PCM waves -> {a.out}")


if __name__ == "__main__":
    main()
