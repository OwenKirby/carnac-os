#!/usr/bin/env python3
"""Generate spread_ratio_q28 for the oscillator spread knob (src/dsp/oscillators.c).

For each knob value 0..127, the pitch ratio of each of the four playheads, Q28 (1.0 = 1 << 28).
Playheads 0 and 3 are the outer pair, 1 and 2 the inner pair.

  0 .. CHORUS_END     detune. The outer pair detunes by +-CHORUS_CENTS on a power curve (flange near 0,
                      chorus through the middle, supersaw from about 77, wider past it), the inner pair by
                      INNER of that.
  CHORUS_END+1 .. 127 harmonic stacks. The playheads glide from one interval set (STACKS) to the next:
                      each set holds still for PLATEAU values and the values between two sets are the
                      detuned in-betweens. The detune eases from CHORUS_CENTS to STACK_CENTS over the
                      first glide, so the stacks keep a gentler chorus.

Usage
  python gen_spread.py          print the table, to paste over the one in oscillators.c
"""
import math

CHORUS_END = 99            # last value of the detune zone (~4/5 of the knob)
CHORUS_CENTS = 55.0        # outer detune at CHORUS_END, cents (supersaw, ~+-35, lands near 77)
STACK_CENTS = 18.0         # outer detune under the harmonic stacks, cents
CURVE = 1.8                # power curve of the detune zone; higher = finer near 0
INNER = 0.45               # inner pair's detune, as a fraction of the outer pair's

# semitones of playheads 0..3 (outer down, inner down, inner up, outer up), in knob order
STACKS = [
    (0, 0, 7, 7),          # fifth
    (0, 0, 12, 12),        # octave
    (0, 7, 12, 19),        # fifths stacked
    (0, 12, 19, 24),       # organ: octave, twelfth, two octaves
    (-12, 0, 12, 24),      # wide octaves
]
GLIDE = 2                  # in-between values before each stack
PLATEAU = 3                # values each stack holds (the last one runs to 127)
CHORUS_SPLIT = (-1.0, -INNER, INNER, 1.0)


def chorus(t):
    return CHORUS_CENTS * t ** CURVE


def table():
    rows = []
    for x in range(CHORUS_END + 1):
        c = chorus(x / CHORUS_END)
        rows.append([c * s for s in CHORUS_SPLIT])

    base = [STACK_CENTS * s for s in CHORUS_SPLIT]
    prev = (0, 0, 0, 0)
    for k, stack in enumerate(STACKS):
        for g in range(1, GLIDE + 1):
            f = g / (GLIDE + 1)
            cents = STACK_CENTS + (CHORUS_CENTS - STACK_CENTS) * (1 - f) if k == 0 else STACK_CENTS
            rows.append([cents * s + 100 * (p + (n - p) * f) for s, p, n in zip(CHORUS_SPLIT, prev, stack)])
        for _ in range(PLATEAU):
            rows.append([b + 100 * n for b, n in zip(base, stack)])
        prev = stack
    while len(rows) < 128:
        rows.append(rows[-1])
    assert len(rows) == 128, len(rows)
    return [[round(2 ** (c / 1200) * (1 << 28)) for c in r] for r in rows]


def main():
    t = table()
    print("static const uint32_t spread_ratio_q28[128][4] = {")
    for x, r in enumerate(t):
        print("    {%s},  /* %3d */" % (", ".join("%10d" % v for v in r), x))
    print("};")


if __name__ == "__main__":
    main()
