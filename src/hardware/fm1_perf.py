#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The synth's PERF TEST on a real FM1, from the PC (Windows Python with mido + python-rtmidi).

  fm1_perf.py build              build build/fm1/synth.fwsc in WSL (src/hardware/build_fm1.py)
  fm1_perf.py install [--yes]    install it with X0X's tools/fm1_install.py (asks first unless --yes), then write
                                 the PCM waves and wavetable bank 0 where the FM1 lacks them (pcm_store_upload.py sync)
  fm1_perf.py run [--wt-ram]     start the test on the FM1 (~95 s), print the table, add it to the checklist;
                                 --wt-ram: the scenarios' wavetable banks read from a RAM copy instead of XIP flash
  fm1_perf.py read               print the last results the FM1 holds (and add them, with --write)
  fm1_perf.py all [--yes]        build, install, run
  fm1_perf.py bench              the profile build: what one integer / float operation costs on the FM1
  fm1_perf.py stats [reset]      the audio's health now: halves, late halves, slowest half, load, voices shed

The test (src/hardware/perf_test.c) plays its own scenarios and puts the live patch back afterwards; any panel
button, key or knob stops it. Pass rule (docs/fm1-measure-checklist.md): avg <= 70 %, peak < 90 %, no late halves.
Results go into docs/fm1-measure-checklist.md, section "PERF TEST runs", newest first.
"""
import datetime
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PKG = REPO / "build" / "fm1" / "synth.fwsc"
CHECKLIST = REPO / "docs" / "fm1-measure-checklist.md"
INSTALLER = REPO / "x0x-firmware" / "tools" / "fm1_install.py"
HDR = [0x7D, 0x46, 0x4C]
ED_INFO, ED_PERFT = 1, 44
PT_STATE = {0: "idle", 1: "running", 2: "done", 3: "stopped"}
SECTION = "## PERF TEST runs (device)"


def wsl_path(p):
    p = Path(p).resolve()
    return f"/mnt/{p.drive[0].lower()}{p.as_posix()[2:]}"


def port_name(names):
    return next((n for n in names if "FM-1" in n or "X0X" in n), None)


def ask(cmd, args, timeout=3.0, tries=3):
    import mido
    for _ in range(tries):
        name_i, name_o = port_name(mido.get_input_names()), port_name(mido.get_output_names())
        if not name_i or not name_o:
            time.sleep(1.0)
            continue
        with mido.open_input(name_i) as i, mido.open_output(name_o) as o:
            while i.poll():
                pass
            o.send(mido.Message("sysex", data=HDR + [cmd] + args))
            end = time.time() + timeout
            while time.time() < end:
                m = i.poll()
                if m is None:
                    time.sleep(0.002)
                    continue
                if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                    return list(m.data[4:])
    sys.exit("fm1_perf: no reply from the FM1 (not plugged in, or not running the synth firmware)")


def cstr(b, i):
    j = b.index(0, i) if 0 in b[i:] else len(b)
    return bytes(b[i:j]).decode("ascii", "replace")


def info():
    b = ask(ED_INFO, [])
    name = cstr(b, 0)
    if len(b) >= len(name) + 1 + 8:                      # since 2026-10-15: the cores, and why the second is off
        cores, why = b[-2], b[-1]
        name += " (2 cores)" if cores == 2 else f" (1 core{', second off: ' + {1: 'bad reads', 2: 'job unfinished', 3: 'barred'}.get(why, str(why)) if why else ''})"
    return name


def results():
    st, scen, n = ask(ED_PERFT, [1])[:3]
    rows = []
    for i in range(n):
        b = ask(ED_PERFT, [2, i])
        w = lambda k: b[k] | b[k + 1] << 7
        rows.append(dict(name=cstr(b, 12), avg=w(1) / 10, peak=w(3) / 10, late=w(5), fps=w(7) / 10,
                         ui_ms=b[9], voices=b[10], cut=bool(b[11])))
    return PT_STATE.get(st, st), rows


STAGES = ["TOTAL", "CTRL", "VOICE", "OSC", "EXTRAS", "FILTER", "AMP", "MIX", "FX", "CLOCK", "SEQ", "LTICK",
          "c.LFO", "c.ENV", "c.OSC", "c.FILT", "c.AMP"]                     # synth_prof.h (c.*: CTRL split)
CTRL_LAPS = {"CTRL", "c.LFO", "c.ENV", "c.OSC", "c.FILT", "c.AMP"}           # one lap a voice a control tick
PER_VOICE = {"VOICE", "OSC", "EXTRAS", "FILTER", "AMP", "MIX"} | CTRL_LAPS   # one lap a voice


def profile(rows):
    """the profile build's stage table: ticks a sample, raw and less the counter reads; None if not a profile build"""
    out, unit = [], None
    for i, r in enumerate(rows):
        b = ask(ED_PERFT, [4, i])
        cyc_ok, lap, n = b[1], b[2], b[3]
        if n == 0:
            return None
        unit = "core cycles" if cyc_ok else "24 MHz ticks"
        raw = [b[4 + 2 * k] | b[5 + 2 * k] << 7 for k in range(n)]
        v = max(r["voices"], 0)
        net = []
        for k, st in enumerate(STAGES[:n]):
            laps = v / 16 if st in PER_VOICE else 1     # a lap a voice a block / control tick (16 samples)
            net.append(max(0, round(raw[k] - laps * lap)))
        out.append((r["name"], v, raw, net, lap))
    lines = [f"Stage profile ({unit} a sample; one counter read costs {out[0][4]}, taken off once a lap: per voice per "
             "16-sample block (the voice stages, CTRL and its c.* split); 'other' = TOTAL less the rest: clock, sequencers, loops)", "",
             "| scenario | voices | " + " | ".join(STAGES) + " | other | per voice (VOICE+CTRL+MIX) |",
             "| --- | ---: |" + " ---: |" * (len(STAGES) + 2)]
    for name, v, raw, net, _ in out:
        d = dict(zip(STAGES, net))
        other = d["TOTAL"] - d["CTRL"] - d["VOICE"] - d["MIX"] - d["FX"] - d.get("CLOCK", 0) - d.get("SEQ", 0) -             d.get("LTICK", 0)
        pv = round((d["VOICE"] + d["CTRL"] + d["MIX"]) / v) if v else 0
        lines.append(f"| {name} | {v} | " + " | ".join(str(x) for x in net) + f" | {other} | {pv} |")
    return lines


MB = ["loop alone", "int32 multiply", "int32 divide", "uint32 divide", "32x32->64 multiply-add", "float add",
      "float multiply", "float divide", "int->float->int", "RAM table load"]


def bench():
    """the profile build's operation costs (SysEx PERFT [5]): core cycles an operation, the loop taken off"""
    b = ask(ED_PERFT, [5], timeout=5.0)
    if len(b) < 3 or b[2] == 0:
        sys.exit("fm1_perf: not a profile build (build_fm1.py --prof)")
    cyc_ok, n, k = b[0], b[1] * 100, b[2]
    t = [b[3 + 3 * i] | b[4 + 3 * i] << 7 | b[5 + 3 * i] << 14 for i in range(k)]
    per_tick = 1 if cyc_ok else 360 / 24                 # 24 MHz ticks -> 360 MHz core cycles
    print(f"operation costs on the FM1 (core cycles each, {n} in a dependent chain, the loop's own cost taken off)")
    for i, name in enumerate(MB[:k]):
        c = t[i] * per_tick / n
        print(f"  {name:24s} {c:6.1f}" if i == 0 else f"  {name:24s} {c - t[0] * per_tick / n:6.1f}")
    return 0


def verdict(r):
    return "pass" if not r["cut"] and r["avg"] <= 70 and r["peak"] < 90 and r["late"] == 0 else "FAIL"


def table(rows):
    out = ["| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |",
           "| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |"]
    for r in rows:
        ui = "– | –" if r["cut"] else f"{r['fps']:.1f} | {r['ui_ms']}"   # cut: ~0.1 s, the screen starved
        out.append(f"| {r['name']} | {r['avg']:.1f} | {r['peak']:.1f} | {r['late']} | {r['voices']} | "
                   f"{ui} | {verdict(r)}{' (cut by the guard)' if r['cut'] else ''} |")
    return out


def write(state, rows, firmware, note="", prof=None):
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    run = [f"### {stamp}, {firmware}, {state}{note}", ""] + table(rows) + [""] + ((prof + [""]) if prof else [])
    text = CHECKLIST.read_text(encoding="utf-8")
    if SECTION not in text:
        text = text.rstrip("\n") + "\n\n" + SECTION + "\n\n" + \
            "Written by src/hardware/fm1_perf.py. Pass: avg <= 70 %, peak < 90 %, no late halves. One core, " \
            "factory patch + the scenario's settings (src/hardware/perf_test.c).\n\n"
    head, tail = text.split(SECTION, 1)
    intro, sep, rest = tail.partition("\n### ")
    text = head + SECTION + intro.rstrip("\n") + "\n\n" + "\n".join(run) + ("\n### " + rest if sep else "")
    CHECKLIST.write_text(text.rstrip("\n") + "\n", encoding="utf-8")
    print(f"added to {CHECKLIST}")


def show(state, rows):
    print(f"PERF TEST: {state}")
    print("\n".join(table(rows)))
    fails = [r["name"] for r in rows if verdict(r) != "pass"]
    print(f"{len(rows) - len(fails)} of {len(rows)} pass" + (f"; over budget: {', '.join(fails)}" if fails else ""))


def cmd_run(wt_ram=False):
    firmware = info()
    print(f"FM1 runs: {firmware}" + (" (wavetables in RAM for this run)" if wt_ram else ""))
    if ask(ED_PERFT, [3] if wt_ram else [])[:1] != [1]:
        sys.exit("fm1_perf: the FM1 did not start the test")
    last = -1
    while True:
        time.sleep(1.0)
        st, scen, n = ask(ED_PERFT, [1])[:3]
        if scen != last and st == 1:
            print(f"  {scen + 1}/{n}", flush=True)
            last = scen
        if st != 1:
            break
    state, rows = results()
    if state != "done":                                  # a panel press stops it: its rows are empty, not passes
        print(f"PERF TEST {state} before the end (a button, key or knob on the FM1 stops it): nothing recorded")
        return 1
    show(state, rows)
    prof = profile(rows)
    if prof:
        print("\n".join(prof))
    write(state, rows, firmware, ", wavetables in RAM" if wt_ram else "", prof)
    return 0


def sync_waves():
    """after an install: the PCM waves and wavetable bank 0 (pcm_store_upload.py sync), each only when the FM1 does
    not hold this repo's image already"""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import pcm_store_upload
    print("waves: checking the PCM areas and bank 0")
    pcm_store_upload.sync(pcm_store_upload.Link(wait=20.0))


def main():
    a = sys.argv[1:]
    if a and a[0] == "bench":
        return bench()
    if a and a[0] == "stats":
        r = ask(46, [1] if "reset" in a else [])
        if not r or len(r) < 35:
            sys.exit("fm1_perf: no STATS reply (an older firmware?)")
        n = len(r) // 5
        v = [sum(r[i * 5 + k] << (7 * k) for k in range(5)) for i in range(n)]
        print("halves %d  late %d  slowest %d us  load %d %%  peak %d %%  shed %d  held off %d" % tuple(v[:7]))
        if n >= 27:
            import math
            db = lambda x: "%.1f dBFS" % (20 * math.log10(x / 32768)) if x else "silent"
            rt = ["SPRING", "TOMB", "MOD", "CELESTIAL", "DOLMEN", "TUMULUS"]
            print("levels since last read: reverb %s, delay %s, dry %s; voices sounding %d" %
                  (db(v[7]), db(v[8]), db(v[9]), v[10]))
            print("reverb %s TIME %d DAMP %d PREDELAY %d | delay %s TIME %d FEEDBACK %d DEGRADE %d PINGPONG %d" %
                  (rt[v[11]] if v[11] < 8 else v[11], v[12], v[13], v[14], "BBD" if v[15] else "TAPE", v[16], v[17],
                   v[18], v[19]))
            print("sends: L1 reverb %d delay %d, L2 reverb %d delay %d; perf mode %d; arch L1 %d L2 %d" % tuple(v[20:27]))
        if n >= 29:
            print("reverb send going in %s; core 1 stack used %d of 2816 B" % (db(v[27]), v[28]))

        return 0
    if not a or a[0] not in ("build", "install", "run", "read", "all"):
        sys.exit(__doc__)
    yes = ["--yes"] if "--yes" in a else []
    if a[0] in ("build", "all"):
        r = subprocess.run(["wsl.exe", "--", "python3", wsl_path(REPO / "src" / "hardware" / "build_fm1.py")])
        if r.returncode:
            sys.exit("fm1_perf: build failed")
    if a[0] in ("install", "all"):
        r = subprocess.run([sys.executable, str(INSTALLER), str(PKG), *yes])
        if r.returncode:
            sys.exit(f"fm1_perf: install failed ({r.returncode})")
        time.sleep(3.0)                              # the FM1 restarts; the installer has checked its identity
        sync_waves()
    if a[0] in ("run", "all"):
        return cmd_run("--wt-ram" in a)
    if a[0] == "read":
        state, rows = results()
        if state != "done":
            sys.exit(f"PERF TEST {state}: no complete results on the FM1")
        show(state, rows)
        if "--write" in a:
            write(state, rows, info())
    return 0


if __name__ == "__main__":
    sys.exit(main())
