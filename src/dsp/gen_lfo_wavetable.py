#!/usr/bin/env python3
"""Generate the LFO shape wavetable and the morph-knob table (src/dsp/lfo.c).

lfo_wavetable[4][256][2]: the four phase-only shapes (sine, triangle, saw, square), 256 steps a cycle. Each
step holds its start value and its rise across the step, so the LFO reads
    value = start + ((rise * frac_q15) >> 15)
from the top 8 phase bits (step) and the 15 bits below them (frac). The sine rises to the next step's start;
the straight shapes rise along their own slope, not toward the next step, so the saw and square edges stay
hard instead of being smeared over a step. Triangle, saw and square match the old per-call maths to within 1 LSB; the sine matches exactly.
A falling rise is clipped so the interpolation never passes -32768 (the >> 15 rounds toward minus infinity).

lfo_morph_stage[128], lfo_morph_w[128]: the SHAPE knob (spec 4.4) walks a ring of six shapes, sine,
triangle, saw, square, stepped random, slewed random, then back to sine; 127 wraps to the sine. For each knob
value, the ring stage it is in and the Q15 weight of the next stage.

Usage
  python gen_lfo_wavetable.py   print the tables, to paste over the ones in lfo.c
"""
import math

STEPS = 256
SHAPES = 6                       # ring length, including the two random shapes


def sine(p):                     # p = phase / 2^32
    return round(32767 * math.sin(2 * math.pi * p))


def tri(p):
    ph = int(p * 2 ** 32)
    return -32768 + (ph >> 15) if ph < 2 ** 31 else 32767 - ((ph - 2 ** 31) >> 15)


def saw(p):
    return 32767 - (int(p * 2 ** 32) >> 16)


def sqr(p):
    return 32767 if p < 0.5 else -32768


def frame(f):
    rows = []
    for i in range(STEPS):
        start = f(i / STEPS)
        if f is sine:
            rise = f((i + 1) / STEPS) - start              # smooth: interpolate to the next step
        else:
            rise = 2 * (f((i + 0.5) / STEPS) - start)      # straight within the step: twice the half-step rise
        while start + ((rise * 32767) >> 15) < -32768:
            rise += 1
        while start + ((rise * 32767) >> 15) > 32767:
            rise -= 1
        rows.append((start, rise))
    return rows


def morph():
    stage, w = [], []
    for k in range(128):
        pos = (k * SHAPES) % (SHAPES * 127)                # 127 wraps to 0
        stage.append(pos // 127)
        w.append(round((pos % 127) * 32768 / 127))
    return stage, w


def main():
    names = ("sine", "triangle", "saw", "square")
    print("static const int16_t lfo_wavetable[4][256][2] = {")
    for name, f in zip(names, (sine, tri, saw, sqr)):
        rows = frame(f)
        print("    {   /* %s */" % name)
        for i in range(0, STEPS, 8):
            print("        " + " ".join("{%6d,%5d}," % r for r in rows[i:i + 8]))
        print("    },")
    print("};")
    stage, w = morph()
    print("static const uint8_t lfo_morph_stage[128] = {")
    for i in range(0, 128, 32):
        print("    " + ",".join("%d" % v for v in stage[i:i + 32]) + ",")
    print("};")
    print("static const uint16_t lfo_morph_w[128] = {")
    for i in range(0, 128, 16):
        print("    " + " ".join("%5d," % v for v in w[i:i + 16]))
    print("};")


if __name__ == "__main__":
    main()
