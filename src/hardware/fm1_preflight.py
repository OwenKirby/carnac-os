#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Preflight for an FM1 build: prove, on the host, that the start-up leaves RAM as the program expects.

  python3 src/hardware/fm1_preflight.py ELF IMAGE GLUE.c      (build_fm1.py runs it on every build)

Why: on 2026-10-15 a build that moved the DSP's code to RAM installed fine and then ran on corrupted RAM. X0X's
start-up copied .ram_text up to _rt_end, which the new layout had moved to the top of RAM, so the copy wrote flash
bytes over all of .data, .bss and .pool. Every variable started as 0xFF; the FM1 looped through a crash every 31 s
and needed the chip's own update mode to recover. Nothing on the host looked at the start-up.

What it does: reads fm1_cstart's zeroing and copy loops from the glue's source (and refuses any loop it does not
know, so a change to the start-up forces this model to be updated with it), runs them on a byte model of RAM with
the image's own bytes (what XIP reads past the image is the packager's 0xFF padding), then checks:
  - .data holds its initial values, .bss and .pool are zero, .ram_text and .c1_text hold their code;
  - no loop wrote a byte outside its own destination section, nor into .noinit, the stacks or the reserved top;
  - every copy read inside the image;
  - the sections do not overlap, and every RAM-resident code section lies inside the PC guard's RAM window
    [_rt_start, _rt_end) (fm1_guard.h), the only RAM the CPU may execute.
Exit 0 and "preflight: OK" only if all hold.
"""
import re
import subprocess
import sys
from pathlib import Path

APP_XIP = 0x02000120
RAM_LO, RAM_HI = 0x01C00000, 0x01C80000
STACKS = (0x01C74000, 0x01C7C000)          # app.ld: _guard0 .. _sstack_top
NOINIT = (0x01C7C000, 0x01C7FD50)
TOP = (0x01C7FD50, 0x01C80000)             # boot info, mailbox, vectors
OBJDUMP = "/opt/jieli/toolchain/common/bin/objdump"

# the loops fm1_cstart may contain, in the order they run: (regex on the loop, action)
LOOPS = [
    (r"for \(d = _bss_start; d < _bss_end; d\+\+\)\s*\*d = 0;", ("zero", "_bss_start", "_bss_end")),
    (r"for \(d = _pool_start; d < _pool_end; d\+\+\)\s*\*d = 0;", ("zero", "_pool_start", "_pool_end")),
    (r"for \(s = _data_load, d = _data_start; d < _data_end; s\+\+, d\+\+\)\s*\*d = \*s;",
     ("copy", "_data_load", "_data_start", "_data_end")),
    (r"uint32_t \*end = \(uintptr_t\)_rt_text_end \? _rt_text_end : _rt_end;\s*"
     r"for \(s = _rt_load, d = _rt_start; d < end; s\+\+, d\+\+\)\s*\*d = \*s;",
     ("copy", "_rt_load", "_rt_start", "_rt_text_end|_rt_end")),
    (r"for \(s = _rt_load, d = _rt_start; d < _rt_end; s\+\+, d\+\+\)\s*\*d = \*s;",
     ("copy", "_rt_load", "_rt_start", "_rt_end")),
    (r"for \(s = _c1_load, d = _c1_start; d < _c1_end; s\+\+, d\+\+\)\s*\*d = \*s;",
     ("copy", "_c1_load", "_c1_start", "_c1_end")),
]
# which section each loop's destination must stay inside
DEST = {"_bss_start": ".bss", "_pool_start": ".pool", "_data_start": ".data", "_rt_start": ".ram_text",
        "_c1_start": ".c1_text"}


def run(*a):
    return subprocess.run(a, capture_output=True, text=True, check=True).stdout


def cstart_loops(glue):
    src = Path(glue).read_text(encoding="utf-8")
    m = re.search(r"^void fm1_cstart\(void\)\s*\{(.*?)^\}", src, re.S | re.M)
    if not m:
        raise SystemExit("preflight: no fm1_cstart in " + str(glue))
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    found = []
    for pat, act in LOOPS:
        for mm in re.finditer(pat, body):
            found.append((mm.start(), mm.end(), act))
    found.sort()
    covered = [(a, b) for a, b, _ in found]
    for mm in re.finditer(r"\bfor \(", body):          # every loop must be one the model knows
        if not any(a <= mm.start() < b for a, b in covered):
            line = body[mm.start():body.index("\n", mm.start())].strip()
            if "*d" in body[mm.start():mm.start() + 200]:
                raise SystemExit(f"preflight: fm1_cstart has a loop the model does not know: {line!r}; "
                                 "update src/hardware/fm1_preflight.py with it")
    return [act for _, _, act in found]


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    elf, image, glue = sys.argv[1:]
    img = Path(image).read_bytes()
    syms = {}
    for ln in run(OBJDUMP, "-t", elf).splitlines():
        p = ln.split()
        if len(p) >= 2 and re.fullmatch(r"[0-9a-f]{8}", p[0]):
            syms.setdefault(p[-1], int(p[0], 16))
    # LLVM's objdump -h lists size and address only: the load addresses are app.ld's own symbols
    lmas = {".ram_text": "_rt_load", ".c1_text": "_c1_load", ".data": "_data_load"}
    secs = {}
    for ln in run(OBJDUMP, "-h", elf).splitlines():
        p = ln.split()
        if len(p) >= 4 and p[0].isdigit() and re.fullmatch(r"[0-9a-f]+", p[2]) and re.fullmatch(r"[0-9a-f]+", p[3]):
            vma, size = int(p[3], 16), int(p[2], 16)
            secs[p[1]] = (vma, syms.get(lmas.get(p[1], ""), vma), size)      # vma, lma, size
    errors = []

    def sym(name):
        if "|" in name:                                  # weak: the first one defined and non-zero
            for n in name.split("|"):
                if syms.get(n):
                    return syms[n]
            raise SystemExit(f"preflight: none of {name} defined")
        if name not in syms:
            raise SystemExit(f"preflight: symbol {name} missing")
        return syms[name]

    def xip(addr, n):
        if not (APP_XIP <= addr and addr + n <= APP_XIP + len(img)):
            errors.append(f"copy reads {addr:#x}+{n} outside the image (XIP {APP_XIP:#x}..{APP_XIP + len(img):#x})")
        o = addr - APP_XIP
        return bytes(img[o + i] if 0 <= o + i < len(img) else 0xFF for i in range(n))

    ram = bytearray(b"\xA5" * (RAM_HI - RAM_LO))         # power-on garbage: never what the program expects
    for act in cstart_loops(glue):
        if act[0] == "zero":
            lo, hi, dest = sym(act[1]), sym(act[2]), act[1]
            data = bytes(hi - lo)
        else:
            src, lo, hi, dest = sym(act[1]), sym(act[2]), sym(act[3]), act[2]
            data = xip(src, hi - lo)
        sec = DEST[dest]
        vma, _, size = secs.get(sec, (lo, 0, 0))
        if sec == ".ram_text" and ".c1_text" in secs:    # X0X's layout: .c1_text right after it, one copy for both
            cv, _, cs = secs[".c1_text"]
            if (vma + size + 3) & ~3 == cv:
                size = cv + cs - vma
        if hi < lo:
            errors.append(f"{sec}: loop end {hi:#x} below its start {lo:#x}")
            continue
        if hi > lo and not (vma <= lo and hi <= vma + size + 3):
            errors.append(f"{sec}: the start-up writes {lo:#x}..{hi:#x}, outside the section {vma:#x}..{vma + size:#x}")
        for name, (a, b) in (("the stacks", STACKS), (".noinit", NOINIT), ("the reserved top", TOP)):
            if lo < b and hi > a:
                errors.append(f"{sec}: the start-up writes {lo:#x}..{hi:#x} over {name} ({a:#x}..{b:#x})")
        if RAM_LO <= lo and hi <= RAM_HI:
            ram[lo - RAM_LO:hi - RAM_LO] = data

    def check(sec, want):
        if sec not in secs:
            return
        vma, lma, size = secs[sec]
        got = bytes(ram[vma - RAM_LO:vma - RAM_LO + size])
        exp = want(vma, lma, size)
        if got != exp:
            bad = next(i for i in range(size) if got[i] != exp[i])
            errors.append(f"{sec} is wrong after the start-up: first at {vma + bad:#x} "
                          f"(got {got[bad]:#04x}, want {exp[bad]:#04x}); {sum(g != e for g, e in zip(got, exp))} bytes")
    for sec in (".data", ".ram_text", ".c1_text"):
        check(sec, lambda vma, lma, size: xip(lma, size))
    for sec in (".bss", ".pool"):
        check(sec, lambda vma, lma, size: bytes(size))

    ram_secs = sorted((v, v + s, n) for n, (v, l, s) in secs.items() if RAM_LO <= v < RAM_HI and s)
    for (a0, a1, an), (b0, b1, bn) in zip(ram_secs, ram_secs[1:]):
        if b0 < a1:
            errors.append(f"sections overlap: {an} {a0:#x}..{a1:#x} and {bn} {b0:#x}..{b1:#x}")
    win = (sym("_rt_start"), sym("_rt_end"))
    for sec in (".ram_text", ".c1_text"):
        if sec in secs and secs[sec][2]:
            v, _, s = secs[sec]
            if not (win[0] <= v and v + s <= win[1]):
                errors.append(f"{sec} {v:#x}..{v + s:#x} is outside the PC guard's RAM window {win[0]:#x}..{win[1]:#x}")

    for e in errors:
        print("preflight: FAIL", e)
    if errors:
        return 1
    print(f"preflight: OK (start-up simulated: .data, .bss, .pool, .ram_text, .c1_text as expected; "
          f"guard window {win[0]:#x}..{win[1]:#x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
