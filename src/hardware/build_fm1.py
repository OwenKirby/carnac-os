#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the synth for the FM1 on X0X's platform: app, update loader and an installable .fwsc.

  python3 src/hardware/build_fm1.py [--x0x DIR] [--out DIR] [--release X.Y]

Reuses X0X's tools/build.py (github.com/charlesvestal/fm1-x0x) whole: the loader, the image layout, the
package, and its checks (entry stub, no soft doubles, RAM / pool / slot sizes, register access only in hal/,
which here also covers our sources). Only the app's units differ:

  synth_fm1.c   -Os  X0X's firmware/src/x0x.c up to its app (slots.c), then UIStateMachine.c, the canvas
                     pieces, the screens and src/hardware/synth_main_fm1.c (the same cut as
                     src/host/x0x_unity_check.sh, minus the DSP)
  synth_dsp.c   -O2  every src/dsp/*.c the firmware uses (the render path, the sequencers)

Needs the JieLi toolchain and the AC79 SDK (x0x-firmware/BUILDING.md); runs natively on Linux x86-64 (WSL).
JIELI_TOOLCHAIN / AC79_SDK default to /opt/jieli/toolchain and /opt/fw-AC79_AIoT_SDK when unset.

X0X's tree must have LF line endings (its own scripts run under sh): build from a clone made with
core.autocrlf=false (default --x0x: ~/x0x-firmware), not the Windows checkout. The package and the
map / disassembly land in --out (default: build/fm1 in this repo).
"""
import argparse
import os
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DSP_SKIP = {"engine.c", "test_wavetables.c"}     # host-only, as in x0x_unity_check.sh (wavetables_image.c: in since 2026-10-16)
UI_SRCS = ["src/UIStateMachine.c", "src/ui_seq_draw.c", "src/ui_arp_draw.c", "src/ui_page_draw.c"]


def overlay(b, gen):
    """write the two unity files into X0X's build dir; returns them"""
    cut = []
    for ln in (b.FW / "src" / "x0x.c").read_text().splitlines(keepends=True):
        if ln.startswith('#include "app/slots.c"'):
            break
        cut.append(ln)
    else:
        raise SystemExit("build_fm1: x0x.c has no '#include \"app/slots.c\"' to cut at")
    inc = lambda p: f'#include "{(REPO / p).as_posix()}"\n'
    main = gen / "synth_fm1.c"
    main.write_text("".join(cut) + "/* ---- synth (X0X app cut; build_fm1.py) ---- */\n" +
                    "".join(inc(f"src/dsp/{h.name}") for h in sorted((REPO / "src" / "dsp").glob("*.h"))) +
                    inc("src/hardware/fm1_x0x_canvas.h") + "".join(inc(p) for p in UI_SRCS) +
                    inc("src/hardware/synth_main_fm1.c") + inc("src/hardware/fm1_divdi.c"))
    dsp = gen / "synth_dsp.c"
    dsp.write_text("/* the synth's DSP and sequencers, one -O2 unit (build_fm1.py) */\n" +
                   "".join(inc(f"src/dsp/{p.name}") for p in sorted((REPO / "src" / "dsp").glob("*.c"))
                           if p.name not in DSP_SKIP))
    return main, dsp


def dsp_ram_ld(b):
    """X0X's app.ld with synth_dsp.o's code moved from the XIP flash into RAM: into .c1_text (X0X's second-core code,
    empty here), which the start-up copies to RAM and the PC guard's RAM window (_rt_start.._rt_end = _c1_end) already
    allows. The section goes to the top 64 KiB of the pool (0x01C64000, below the stacks; the pool keeps 272 KiB):
    a call reaches +-4 MiB (R_PI32V2_LONG_JUMP_23M2) and the XIP code at 0x0200xxxx is just over 4 MiB from the bottom
    of RAM, so the UI's calls into the DSP and the DSP's into the platform only reach RAM this high up. The guard's
    RAM window then spans 0x01C00000.._c1_end. Its constants stay in the flash."""
    ld = (b.FW / "app.ld").read_text()
    subs = [("KEEP(*(.text.crt0)) *(.text*)", "KEEP(*(.text.crt0)) *(EXCLUDE_FILE(*synth_dsp.o) .text*)"),
            # the start-up copies .ram_text to here, not to _rt_end (= _c1_end, now at the top of RAM): see
            # synth_main_fm1.c _rt_text_end
            ("_rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); }",
             "_rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_text_end = .; }"),
            ("KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP",
             "KEEP(*(.c1_text*)) *synth_dsp.o(.text*) . = ALIGN(4); _c1_end = .; } > FAST AT > XIP"),
            ("POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000",
             "POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x44000\n"
             "    FAST   (rwx) : ORIGIN = 0x01C64000, LENGTH = 0x10000")]
    for old, new in subs:
        if ld.count(old) != 1:
            raise SystemExit(f"build_fm1 --dsp-ram: X0X's app.ld changed, no '{old}'")
        ld = ld.replace(old, new)
    out = b.OUT / "synth" / "app_dsp_ram.ld"
    out.write_text(ld)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--x0x", type=Path, default=Path.home() / "x0x-firmware", help="X0X checkout (LF line endings)")
    ap.add_argument("--out", type=Path, default=REPO / "build" / "fm1", help="where the package goes")
    ap.add_argument("--release", metavar="X.Y", help="version string shown on the device (SYNTH_VERSION)")
    ap.add_argument("--prof", action="store_true",
                    help="the profile build: the render times its stages (-DSYNTH_PROF, src/dsp/synth_prof.h)")
    ap.add_argument("--dsp-ram", action="store_true",
                    help="run the DSP's code from RAM instead of the flash (XIP) through the I-cache (dsp_ram_ld)")
    a = ap.parse_args()
    os.environ.setdefault("JIELI_TOOLCHAIN", "/opt/jieli/toolchain")
    os.environ.setdefault("AC79_SDK", "/opt/fw-AC79_AIoT_SDK")
    x0x = a.x0x.resolve()
    if not (x0x / "tools" / "build.py").exists():
        raise SystemExit(f"build_fm1: no X0X tree in {x0x} (git -c core.autocrlf=false clone ... {x0x})")
    if b"\r\n" in (x0x / "tools" / "gen_builtin_break.sh").read_bytes():
        raise SystemExit(f"build_fm1: {x0x} has CRLF line endings; clone it with core.autocrlf=false")
    sys.path.insert(0, str(x0x / "tools"))
    import build as b                                   # X0X's tools/build.py

    def generate():                                     # the cut x0x.c needs only the font
        b.GEN.mkdir(parents=True, exist_ok=True)
        r = __import__("subprocess").run([sys.executable, str(b.SRC / "tools" / "gen_font.py"),
                                          str(b.GEN / "felucca_font.h")], capture_output=True, text=True)
        if r.returncode:
            sys.stderr.write(r.stdout + r.stderr)
            raise SystemExit("build_fm1: gen_font.py failed")

    def build_app():
        gen = b.OUT / "synth"
        gen.mkdir(parents=True, exist_ok=True)
        main_c, dsp_c = overlay(b, gen)
        inc = [f"-I{(REPO / d).as_posix()}" for d in ("src", "src/dsp", "src/hardware")]
        flags = [*b.CFLAGS, *b.FPU, "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen", *inc,
                 f'-DFELUCCA_ID="{b.PRODUCT}"']
        if a.release:
            flags.append(f'-DSYNTH_VERSION="{a.release.upper()}"')
        if a.prof:
            flags += ["-DSYNTH_PROF", '-DSYNTH_VERSION="PROF"']
        o2 = [f if f != "-Os" else "-O2" for f in flags]
        objs = [b.OUT / "crt0.o", b.OUT / "fm1_vec.o", b.OUT / "fm1_isr.o", b.OUT / "fm1_cpu1.o",
                gen / "synth_fm1.o", gen / "synth_dsp.o"]
        b.tc_all(("cc", "-c", b.FW / "crt0.S", "-o", objs[0]),
                 ("cc", "-c", b.FW / "hal" / "fm1_vec.S", "-o", objs[1]),
                 ("cc", "-c", b.FW / "hal" / "fm1_isr.S", "-o", objs[2]),
                 ("cc", "-c", b.FW / "hal" / "fm1_cpu1.S", "-o", objs[3]),
                 ("cc", *flags, "-c", main_c, "-o", objs[4]),
                 ("cc", *o2, "-c", dsp_c, "-o", objs[5]))
        return link(objs)

    def link(objs):
        # X0X's build_app from the link on, unchanged: borrow it with the compile step stubbed out
        real_all, real_o2 = b.tc_all, b.O2_UNITS
        first = [True]

        def tc_all(*cmds):
            if first[0]:                                # the compile batch: ours already ran
                first[0] = False
                return []
            return real_all(*cmds)
        real_tc = b.tc

        def tc(tool, *args):
            if tool == "pi32v2/bin/ld":                 # swap X0X's objects for ours
                args = [x for x in args if not (isinstance(x, Path) and x.suffix == ".o")]
                if a.dsp_ram:
                    args = [dsp_ram_ld(b) if isinstance(x, Path) and x.name == "app.ld" else x for x in args]
                i = args.index("-o")
                args = [*args[:i], *objs, *args[i:]]
            return real_tc(tool, *args)
        b.tc_all, b.tc, b.O2_UNITS = tc_all, tc, []
        try:
            return real_build_app()
        finally:
            b.tc_all, b.tc, b.O2_UNITS = real_all, real_tc, real_o2

    real_mmio = b.mmio_check

    def mmio_check():                                   # X0X's rule (registers only in hal/) over our sources too
        fw = b.FW
        errors = real_mmio()
        shadow = b.OUT / "synth" / "mmio"
        shutil.rmtree(shadow, ignore_errors=True)
        (shadow / "src").mkdir(parents=True)
        shutil.copytree(fw / "hal", shadow / "hal")
        for p in [*(REPO / "src").rglob("*.[ch]")]:
            if "host" in p.parts:
                continue
            d = shadow / "src" / p.relative_to(REPO / "src")
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(p, d)
        b.FW = shadow
        try:
            return errors + real_mmio()
        finally:
            b.FW = fw

    real_build_app = b.build_app
    b.generate, b.build_app, b.mmio_check = generate, build_app, mmio_check
    # x0x.fwsc / x0x.bin are X0X's names inside its build dir; copy ours out under the synth's name
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "synth.fwsc").unlink(missing_ok=True)      # never leave an older package to be installed by mistake
    (b.OUT / "x0x.fwsc").unlink(missing_ok=True)
    sys.argv = [sys.argv[0]]                            # X0X's own --release would change the package identity
    b.main()
    # the start-up, simulated on the host (fm1_preflight.py): no package unless RAM comes out as the program expects
    r = __import__("subprocess").run([sys.executable, str(REPO / "src" / "hardware" / "fm1_preflight.py"),
                                      str(b.OUT / "x0x.elf"), str(b.OUT / "x0x.bin"),
                                      str(REPO / "src" / "hardware" / "synth_main_fm1.c")])
    if r.returncode:
        (b.OUT / "x0x.fwsc").unlink(missing_ok=True)
        raise SystemExit("build_fm1: preflight FAILED: no package written (do not install this build)")
    # 64-bit stores with a scaled register index (2026-10-16): the JieLi compiler placed such stores 4 bytes low when
    # it fused the mix bus clear (synth_runtime.c render_pass: the "digital buzz with reverb"). None is left in the
    # firmware; one appearing again needs its loop read in x0x.dis before the build may be installed.
    import re
    bad = [ln.strip() for ln in (b.OUT / "x0x.dis").read_text(errors="replace").splitlines()
           if re.search(r"\bd\[r\d+\+r\d+<<\d\] = ", ln)]
    if bad:
        (b.OUT / "x0x.fwsc").unlink(missing_ok=True)
        raise SystemExit("build_fm1: %d indexed 64-bit store(s) in the code (the miscompile behind the reverb buzz), "
                         "e.g.\n  %s\nno package written: check that loop" % (len(bad), bad[0]))
    print("  ok    no indexed 64-bit stores (the mix bus clear miscompile, 2026-10-16)")
    for src, dst in ((b.OUT / "x0x.fwsc", "synth.fwsc"), (b.OUT / "x0x.bin", "synth.bin"),
                     (b.OUT / "x0x.elf", "synth.elf"), (b.OUT / "x0x.dis", "synth.dis")):
        shutil.copy(src, a.out / dst)
    print(f"build_fm1: {a.out / 'synth.fwsc'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
