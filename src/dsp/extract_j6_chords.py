#!/usr/bin/env python3
"""Reads the Roland J-6 manual's Chord Set List (pp. 25-33) into src/dsp/arp_chords_j6.txt: 100 sets x 12 keys, the
chord's notes as printed (absolute MIDI notes, C3 = 48), one line per set, keys C .. B separated by ';'.

    python src/dsp/extract_j6_chords.py ["docs/roland J6 Manual/J-6_eng02_W.pdf"]     needs PyMuPDF

Each set is a band between the table's row rules; a note is a word like E4 below the cell's name/notes divider (a chord
name such as C9 or G7 looks like a note and sits above it). gen_arp_chords.py turns this file into arp_chords.c."""
import collections
import os
import re
import sys

import pymupdf

NOTE = re.compile(r"^([A-G])([#b]?)(-?\d)$")
PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(t):
    m = NOTE.match(t)
    return 12 * (int(m.group(3)) + 1) + PC[m.group(1)] + {"#": 1, "b": -1, "": 0}[m.group(2)]


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    pdf = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "..", "docs", "roland J6 Manual", "J-6_eng02_W.pdf")
    doc = pymupdf.open(pdf)
    sets = []
    for p in range(24, 33):
        pg = doc[p]
        c = collections.Counter()
        for r in pg.get_drawings():
            R = r["rect"]
            if R.height < 2 and 20 < R.width < 80:
                c[round(R.y0)] += 1
        rules = []
        for y in sorted(y for y, n in c.items() if n >= 13):    # the rules between sets (12 key cells + the number cell)
            if not rules or y - rules[-1] >= 5:
                rules.append(y)
        dividers = sorted(y for y, n in c.items() if n == 12)   # inside a set: the chord names / notes divider
        notes = [w for w in pg.get_text("words") if NOTE.match(w[4]) and w[0] > 130]
        xs, cent = sorted(set(round(w[0]) for w in notes)), []
        for x in xs:
            if cent and x - cent[-1][-1] <= 12:
                cent[-1].append(x)
            else:
                cent.append([x])
        cent = [sum(q) / len(q) for q in cent]
        assert len(cent) == 12, (p + 1, cent)
        for lo, hi in zip(rules, rules[1:]):
            if hi - lo < 20:
                continue
            div = max([y for y in dividers if lo < y < hi] or [lo])
            cols = [[] for _ in range(12)]
            for w in notes:
                if div < w[1] < hi:
                    cols[min(range(12), key=lambda k: abs(cent[k] - w[0]))].append(midi(w[4]))
            if any(cols):
                sets.append([sorted(set(x)) for x in cols])
    assert len(sets) == 100, len(sets)
    out = os.path.join(here, "arp_chords_j6.txt")
    with open(out, "w", newline="\n") as f:
        f.write("# Roland J-6 Chord Set List (manual pp. 25-33): per set, the chord on each key C .. B as MIDI notes, C3 = 48.\n")
        f.write("# Written by extract_j6_chords.py; read by gen_arp_chords.py.\n")
        for i, cols in enumerate(sets, 1):
            f.write("%d: %s\n" % (i, ";".join(",".join(map(str, c)) for c in cols)))
    print("arp_chords_j6.txt: %d sets" % len(sets))


if __name__ == "__main__":
    main()
