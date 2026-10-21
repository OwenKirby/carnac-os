#!/usr/bin/env python3
"""Generates src/dsp/arp_chords.c: the ARP page's Chord Genre Bank (mvave_fm1_master_specification.txt 8.2, 8.3).

    python src/dsp/gen_arp_chords.py            rewrites src/dsp/arp_chords.c

A chord key plays a 4-voice chord built on the key itself: the key's note is the bass, and the genre names the chord
quality for each of the 12 pitch classes (C .. B, as in 8.3's table, whose rows climb C3, C#3, D3, D#3). The stored form
is 4 intervals above that bass, so the same table serves every octave and the FM1's OCT buttons move chords with the
keys. 84 genres (spec 8.2's 83, then Phrygian) x 12 pitch classes x 4 bytes = 4,032 B of const.

Spec 8.3 lists only 10 genres x 4 keys, some garbled (Pop 1's third chord doubles C4 and has no D). Its sane voicings
are kept where they are distinctive (Trad Maj, Trad Min 1 / 2, Pop Min 1, Jazz 1 / 2's first keys); every other
voicing is authored here from the genre name. A draft to audition, not a reference.

Voicing: the bass is the key. The quality's colour tones (3 of them; a triad doubles its root) are placed in a fixed
register window above the C3 frame (W .. W + 11, at least a minor third over the bass), so neighbouring keys voice-lead
instead of jumping in parallel. 'open' then lifts the middle upper voice an octave. Stacks (4th / 5th / octave and
power chords) are literal and run in parallel on every key.
"""
import os

# colour tones above the root, in semitones (extensions keep their compound value: 14 = 9th, 17 = 11th, 18 = #11)
Q = {
    "maj": [4, 7], "min": [3, 7], "dim": [3, 6], "aug": [4, 8], "sus2": [2, 7], "sus4": [5, 7],
    "add9": [4, 7, 14], "madd9": [3, 7, 14], "M7": [4, 7, 11], "m7": [3, 7, 10], "7": [4, 7, 10],
    "m7b5": [3, 6, 10], "dim7": [3, 6, 9], "6": [4, 7, 9], "m6": [3, 7, 9], "69": [4, 9, 14], "m69": [3, 9, 14],
    "M9": [4, 11, 14], "m9": [3, 10, 14], "9": [4, 10, 14], "7sus4": [5, 7, 10], "9sus4": [5, 10, 14],
    "M7#11": [4, 11, 18], "m11": [3, 10, 17], "13": [4, 10, 21], "7#9": [4, 10, 15], "7b9": [4, 10, 13],
    "mM7": [3, 7, 11], "mM9": [3, 11, 14], "M7#5": [4, 8, 11], "M9#11": [4, 14, 18], "m9_11": [3, 14, 17],
}
# literal 4-voice shapes (intervals from the bass), the same on every key
LIT = {
    "oct": [0, 12, 19, 24],          # root, octave, the fifth above it, double octave
    "q4": [0, 5, 10, 15],            # stacked fourths
    "q5": [0, 7, 14, 21],            # stacked fifths
    "pow": [0, 7, 12, 19],           # power chord with its octave
}

# one entry per pitch class, C C# D D# E F F# G G# A A# B
def row(s):
    v = s.split()
    assert len(v) == 12, s
    return v

def par(q):                          # the same quality on every key (Utility, House, stacks)
    return [q] * 12

# (name, 12 qualities, voicing, register W as a MIDI note over the C3 = 48 frame)
GENRES = [
    ("Pop_1", row("maj dim7 min maj min maj m7b5 maj maj min maj dim"), "open", 55),
    ("Pop_2", row("add9 dim7 m7 maj m7 add9 m7b5 sus4 maj m7 add9 m7b5"), "open", 55),
    ("Jazz_1", row("69 7#9 m9 7#9 m9 M7#11 m7b5 13 7#9 m9 9 m7b5"), "close", 52),
    ("Jazz_2", row("69 9 m9 9 m7 M9 m7b5 13 9 7b9 9 m7b5"), "open", 52),
    ("Jazz_3", row("M9 M9 m9 M9 m9 M9 m7b5 9sus4 M9 m9 M9 7b9"), "close", 55),
    ("Blues", row("7 9 9 9 7#9 9 dim7 9 9 7#9 9 dim7"), "close", 52),
    ("Trad_Maj", row("maj dim7 min dim7 min maj dim7 maj maj min maj dim"), "close", 62),
    ("Trad_Min_1", row("min maj dim maj dim7 min dim7 maj maj dim7 maj dim7"), "close", 62),
    ("Trad_Min_2", row("min maj dim aug dim7 min dim7 7 maj m7b5 maj dim7"), "close", 62),
    ("Pop_Min_1", row("madd9 M7 m7b5 M7 dim7 m7 dim7 m7 M7 m7b5 add9 dim7"), "close", 60),
    ("Pop_Min_2", row("madd9 sus2 m7b5 add9 dim7 madd9 dim7 min add9 dim add9 dim7"), "open", 55),
    ("Jazz_Min_1", row("m9 7 m7b5 M7#5 dim7 m9 dim7 7b9 M7 m7b5 9 dim7"), "close", 52),
    ("Jazz_Min_2", row("m69 M7#11 m7b5 M9 7#9 m11 m7b5 7#9 M9 m7b5 13 dim7"), "open", 55),
    ("Oct_Stack", par("oct"), "lit", 0),
    ("4th_Stack", par("q4"), "lit", 0),
    ("5th_Stack", par("q5"), "lit", 0),
    ("Utility_Maj", par("maj"), "close", 55),
    ("Utility_Min", par("min"), "close", 55),
    ("Utility_M7", par("M7"), "close", 55),
    ("Utility_m7", par("m7"), "close", 55),
    ("Utility_M9", par("M9"), "close", 55),
    ("Utility_m9", par("m9"), "close", 55),
    ("Utility_M9_11", par("M9#11"), "close", 55),
    ("Utility_m9_11", par("m9_11"), "close", 55),
    ("Pop_Inversions", row("maj dim7 min maj min maj m7b5 maj maj min maj dim"), "close", 57),
    ("Minor_Inversions", row("min maj dim maj dim7 min dim7 min maj dim maj dim7"), "close", 57),
    ("Pop_Synth_1", row("add9 dim7 madd9 add9 madd9 add9 m7b5 add9 add9 madd9 add9 m7b5"), "open", 57),
    ("Pop_Synth_2", row("sus2 dim7 sus2 sus2 sus4 sus2 m7b5 sus4 sus2 sus2 sus2 sus4"), "close", 57),
    ("Pop_Synth_3", par("pow"), "lit", 0),
    ("Pop_Synth_4", row("M7 dim7 m7 M7 m7 M7 m7b5 9sus4 M7 m7 M7 m7b5"), "open", 60),
    ("Pop_Synth_5", row("maj dim7 min maj min maj m7b5 maj maj min maj dim"), "close", 64),
    ("Pop_Synth_6", row("m7 maj m7b5 M7 dim7 m9 dim7 m7 M7 m7b5 add9 dim7"), "open", 57),
    ("Cinematic_1", row("min maj sus2 maj maj min dim7 sus4 maj maj maj maj"), "open", 55),
    ("Cinematic_2", row("madd9 M7#11 sus2 M7 aug madd9 m7b5 7sus4 M7#11 sus2 add9 mM7"), "open", 52),
    ("Cinematic_3", row("mM7 maj dim7 M7#5 dim7 m6 dim7 7b9 M7 m7b5 maj dim7"), "close", 52),
    ("Cinematic_4", row("sus4 9sus4 sus2 9sus4 sus4 sus2 9sus4 sus4 sus2 9sus4 sus2 sus4"), "open", 57),
    ("Cinematic_5", row("add9 maj add9 maj madd9 add9 M7#11 sus4 maj madd9 add9 madd9"), "open", 60),
    ("Cinematic_6", row("min maj dim maj aug min dim maj maj dim maj dim7"), "close", 48),
    ("Synthwave_1", row("min maj dim maj dim7 min dim7 min maj dim maj dim7"), "open", 57),
    ("Synthwave_2", row("madd9 maj m7b5 add9 dim7 madd9 dim7 m7 add9 m7b5 add9 sus4"), "open", 55),
    ("Synthwave_3", row("m7 M7 m7b5 M7 dim7 m9 dim7 m7 M7 m7b5 add9 7sus4"), "close", 57),
    ("Synthwave_4", par("sus2"), "close", 60),
    ("Synthwave_5", row("M7 dim7 m7 add9 m7 M7 m7b5 add9 M7 m7 add9 m7b5"), "open", 60),
    ("Synthwave_6", row("m9 M9 m7b5 M9 7b9 m9 dim7 m9 M9 m7b5 9sus4 dim7"), "open", 52),
    ("House_1", par("m7"), "close", 57),
    ("House_2", par("m9"), "open", 55),
    ("House_3", par("m11"), "close", 57),
    ("House_4", row("M9 dim7 m9 M7 m7 M9 m7b5 9sus4 M7 m9 add9 m7b5"), "open", 57),
    ("House_5", row("m7 M7 m7b5 M7 dim7 m9 dim7 m7 M7 m7b5 9sus4 dim7"), "close", 57),
    ("Jazz_House_1", row("m9 13 m9 M9 m11 M9 m7b5 9sus4 13 m9 M9 7#9"), "open", 52),
    ("Jazz_House_2", row("M9 7#9 m11 9 m9 M7#11 m7b5 13 M9 m11 9sus4 7b9"), "close", 55),
    ("Techno_1", par("m7"), "close", 48),
    ("Techno_2", par("madd9"), "close", 48),
    ("EDM_1", row("maj dim7 min maj min maj m7b5 maj maj min maj dim"), "open", 60),
    ("EDM_2", row("add9 dim7 madd9 add9 madd9 add9 m7b5 add9 add9 madd9 add9 m7b5"), "close", 60),
    ("EDM_3", row("madd9 add9 m7b5 add9 dim7 madd9 dim7 madd9 add9 m7b5 add9 dim7"), "open", 57),
    ("EDM_4", par("sus2"), "open", 60),
    ("EDM_5", row("M7 dim7 m7 M7 m7 M7 m7b5 7sus4 M7 m7 7 m7b5"), "close", 57),
    ("EDM_6", row("min maj dim maj dim7 min dim7 maj maj dim maj dim7"), "close", 64),
    ("Gospel_RB_1", row("M9 dim7 m9 dim7 m7 M9 dim7 13 9 m9 9 7#9"), "open", 52),
    ("Gospel_RB_2", row("69 7b9 m11 9 7#9 M7#11 m7b5 9sus4 13 7b9 13 m7b5"), "open", 55),
    ("Gospel_RB_3", row("M7 dim7 m7 dim7 7 M9 dim7 13 M7 m9 9 7b9"), "close", 57),
    ("Lofi_RB_1", row("M9 dim7 m9 M7 m7 M9 m7b5 9sus4 M7 m9 9 m7b5"), "close", 52),
    ("Lofi_RB_2", row("m9 M7 m7b5 M9 7#9 m9 dim7 m7 M9 m7b5 13 dim7"), "open", 52),
    ("Funk_1", par("9"), "close", 57),
    ("Funk_2", row("m7 9 m7 9 7#9 9 dim7 m7 9 7#9 9 7b9"), "close", 57),
    ("Neo_Soul_1", row("M9 7#9 m9 M9 m9 M7#11 m7b5 9sus4 M9 m9 13 7b9"), "open", 52),
    ("Neo_Soul_2", par("m11"), "open", 52),
    ("Neo_Soul_3", row("m9 M7#11 m11 M9 7#9 m9 m7b5 m9 M9 m7b5 13 dim7"), "open", 55),
    ("Neo_Soul_4", row("69 9 m11 M7 m9 69 m7b5 13 M9 m11 9 7#9"), "close", 52),
    ("Neo_Soul_5", row("M7#11 13 m9 M7#11 m7 M9 m7b5 9sus4 M7#11 m9 13 m7b5"), "open", 55),
    ("Neo_Soul_6", row("m11 M9 m7b5 M9 7#9 m11 dim7 m9 M7#11 m7b5 9sus4 7b9"), "close", 52),
    ("Neo_Soul_7", row("9 13 m9 13 m9 9 m7b5 13 9 m9 13 7#9"), "close", 55),
    ("Neo_Soul_8", row("m69 M9 m7b5 M9 7b9 m69 dim7 m9 M7 m7b5 9 dim7"), "open", 57),
    ("Neo_Soul_9", row("mM9 13 m7b5 M7#5 7b9 m9 m7b5 7#9 M9 m7b5 9sus4 dim7"), "open", 52),
    ("Bossa_Nova_1", row("69 dim7 m9 dim7 m7 M7 m7b5 13 9 m7 9 m7b5"), "close", 52),
    ("Bossa_Nova_2", row("m6 7 m7b5 M7 7b9 m6 m7b5 7b9 M7 m7b5 9 dim7"), "open", 52),
    ("Classical_1", row("maj dim7 min dim7 min maj dim7 maj aug min maj dim"), "close", 57),
    ("Classical_2", row("M7 dim7 m7 dim7 m7 M7 dim7 7 M7 m7 7 m7b5"), "close", 57),
    ("Classical_3", row("min maj dim aug dim7 min dim7 7 maj dim maj dim7"), "close", 57),
    ("Classical_4", row("maj 7 m6 maj 7 m6 dim7 7b9 maj min 7 dim7"), "open", 55),
    ("Modern_1", row("M7#11 9sus4 sus2 M7#11 sus2 M7#11 9sus4 sus4 M7#11 sus2 M7#11 9sus4"), "open", 57),
    ("Modern_2", row("add9 madd9 madd9 add9 madd9 add9 mM7 add9 add9 madd9 add9 madd9"), "close", 64),
    # added after spec 8.2's list (2026-10-17, the default genre): C Phrygian's seventh chords on its keys (i, bII maj7,
    # bIII7, iv7, v half-diminished, bVI maj7, bvii7), a diminished seventh on the five keys outside it
    ("Phrygian", row("min M7 dim7 7 dim7 m7 dim7 m7b5 M7 dim7 m7 dim7"), "close", 55),
]
EXTRA_NAMES = ["Phrygian"]           # genres after spec 8.2's, in this order

# spec 8.3 rows kept as written (intervals from the bass, read off the C3 .. D#3 rows)
SPEC = {
    ("Trad_Maj", 0): [0, 16, 19, 24], ("Trad_Maj", 1): [0, 15, 18, 21], ("Trad_Maj", 2): [0, 12, 15, 19],
    ("Trad_Maj", 3): [0, 15, 18, 21],
    ("Trad_Min_1", 0): [0, 15, 19, 24], ("Trad_Min_1", 1): [0, 12, 16, 19], ("Trad_Min_1", 2): [0, 12, 15, 18],
    ("Trad_Min_1", 3): [0, 12, 16, 19],
    ("Trad_Min_2", 0): [0, 15, 19, 24], ("Trad_Min_2", 1): [0, 12, 16, 19], ("Trad_Min_2", 2): [0, 12, 15, 18],
    ("Trad_Min_2", 3): [0, 12, 16, 20],
    ("Pop_Min_1", 0): [0, 14, 15, 19], ("Pop_Min_1", 1): [0, 7, 11, 16], ("Pop_Min_1", 2): [0, 10, 15, 18],
    ("Pop_Min_1", 3): [0, 7, 11, 16],
    ("Jazz_1", 0): [0, 4, 9, 14], ("Jazz_1", 1): [0, 4, 10, 15], ("Jazz_1", 2): [0, 3, 10, 14],
    ("Jazz_1", 3): [0, 4, 10, 15],
    ("Jazz_2", 0): [0, 4, 9, 14], ("Jazz_2", 1): [0, 4, 10, 14], ("Jazz_2", 2): [0, 3, 10, 14],
    ("Jazz_2", 3): [0, 4, 10, 14],
}

SPEC_NAMES = ("Pop_1 Pop_2 Jazz_1 Jazz_2 Jazz_3 Blues Trad_Maj Trad_Min_1 Trad_Min_2 Pop_Min_1 Pop_Min_2 Jazz_Min_1 "
              "Jazz_Min_2 Oct_Stack 4th_Stack 5th_Stack Utility_Maj Utility_Min Utility_M7 Utility_m7 Utility_M9 "
              "Utility_m9 Utility_M9_11 Utility_m9_11 Pop_Inversions Minor_Inversions Pop_Synth_1 Pop_Synth_2 "
              "Pop_Synth_3 Pop_Synth_4 Pop_Synth_5 Pop_Synth_6 Cinematic_1 Cinematic_2 Cinematic_3 Cinematic_4 "
              "Cinematic_5 Cinematic_6 Synthwave_1 Synthwave_2 Synthwave_3 Synthwave_4 Synthwave_5 Synthwave_6 "
              "House_1 House_2 House_3 House_4 House_5 Jazz_House_1 Jazz_House_2 Techno_1 Techno_2 EDM_1 EDM_2 EDM_3 "
              "EDM_4 EDM_5 EDM_6 Gospel_RB_1 Gospel_RB_2 Gospel_RB_3 Lofi_RB_1 Lofi_RB_2 Funk_1 Funk_2 Neo_Soul_1 "
              "Neo_Soul_2 Neo_Soul_3 Neo_Soul_4 Neo_Soul_5 Neo_Soul_6 Neo_Soul_7 Neo_Soul_8 Neo_Soul_9 Bossa_Nova_1 "
              "Bossa_Nova_2 Classical_1 Classical_2 Classical_3 Classical_4 Modern_1 Modern_2").split()


def voice(q, how, w, pc):
    """4 intervals above the bass for the chord of quality q on pitch class pc"""
    if how == "lit":
        return list(LIT[q])
    tones = list(Q[q])
    if len(tones) == 2:
        tones.append(12)                             # a triad doubles its root on top
    root = 48 + pc                                   # the C3 frame of spec 8.3
    up = []
    for t in tones:
        cls = (pc + t) % 12
        a = w + (cls - w) % 12                       # the window W .. W + 11
        while a < root + 3:                          # never a second or less over the bass
            a += 12
        up.append(a - root)
    up.sort()
    if how == "open":
        up = sorted([up[0], up[2], up[1] + 12])
    return [0] + up


def build():
    names = [g[0] for g in GENRES]
    assert names == SPEC_NAMES + EXTRA_NAMES, "genre order must be spec 8.2's, then the additions"
    table = []
    for name, quals, how, w in GENRES:
        chords = []
        for pc in range(12):
            c = SPEC.get((name, pc)) or voice(quals[pc], how, w, pc)
            assert len(c) == 4 and c[0] == 0 and all(c[i] < c[i + 1] for i in range(3)), (name, pc, c)
            assert c[3] <= 40, (name, pc, c)
            chords.append(c)
        table.append(chords)
    return names, table


# The SEQ's FOLLOW GENRE scale (2026-10-16): each genre's scale and root, for a NOTE lane's SCALE = GENRE. In the order
# of PresetStructures.h SEQSCALE_* from SEQSCALE_MAJOR (index 0 here = SEQSCALE_MAJOR).
SCALES = [("MAJOR", [0, 2, 4, 5, 7, 9, 11]), ("MINOR", [0, 2, 3, 5, 7, 8, 10]), ("DORIAN", [0, 2, 3, 5, 7, 9, 10]),
          ("PHRYGIAN", [0, 1, 3, 5, 7, 8, 10]), ("LYDIAN", [0, 2, 4, 6, 7, 9, 11]),
          ("MIXOLYDIAN", [0, 2, 4, 5, 7, 9, 10]), ("HARM_MINOR", [0, 2, 3, 5, 7, 8, 11]),
          ("MAJ_PENTA", [0, 2, 4, 7, 9]), ("MIN_PENTA", [0, 3, 5, 7, 10]), ("BLUES", [0, 3, 5, 6, 7, 10])]
# genres that play one chord type on every key fit no scale well (the fit scores ~3-4.5 of 7): by their chord's type
SCALE_PICK = {
    "Blues": ("BLUES", 0), "4th_Stack": ("MIN_PENTA", 0), "5th_Stack": ("MAJ_PENTA", 0),
    "Utility_Maj": ("MAJOR", 0), "Utility_M7": ("MAJOR", 0), "Utility_M9": ("MAJOR", 0), "Utility_M9_11": ("LYDIAN", 0),
    "Utility_Min": ("MINOR", 0), "Utility_m7": ("MINOR", 0), "Utility_m9": ("MINOR", 0), "Utility_m9_11": ("MINOR", 0),
    "Techno_1": ("MINOR", 0), "Techno_2": ("MINOR", 0),
    "House_1": ("DORIAN", 0), "House_2": ("DORIAN", 0), "House_3": ("DORIAN", 0), "Neo_Soul_2": ("DORIAN", 0),
    "Funk_1": ("MIXOLYDIAN", 0), "Neo_Soul_7": ("MIXOLYDIAN", 0), "Phrygian": ("PHRYGIAN", 0),
}


def fit_scale(chords):
    """the scale and root that hold the most of the genre's chords whole (a chord on a key outside the scale does not
    count; a chord partly outside counts its share / 2); ties: root C (the tables are written in C), then SCALES'
    order. Returns (scale index, root)."""
    best = None
    for si, (_, pcs) in enumerate(SCALES):
        for r in range(12):
            m = set((p + r) % 12 for p in pcs)
            s = 0.0
            for k in range(12):
                if k in m:
                    inn = sum((k + iv) % 12 in m for iv in chords[k])
                    s += 1.0 if inn == 4 else inn / 8.0
            key = (s - (0.01 if r else 0.0) - si * 0.001, si, r)
            if best is None or key[0] > best[0]:
                best = key
    return best[1], best[2]


def genre_scales(names, table):
    out = []
    order = [n for n, _ in SCALES]
    for name, chords in zip(names, table):
        if name in SCALE_PICK:
            si, r = order.index(SCALE_PICK[name][0]), SCALE_PICK[name][1]
        else:
            si, r = fit_scale(chords)
        out.append((si, r))
    return out


def main():
    names, table = build()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "arp_chords.c")
    pcs = "C C# D D# E F F# G G# A A# B".split()
    with open(out, "w", newline="\n") as f:
        f.write("/* GENERATED by src/dsp/gen_arp_chords.py - do not edit. The Chord Genre Bank (spec 8.2, 8.3): per genre, per\n"
                " * pitch class of the chord key (C .. B), the 4 voices as semitones above the key, which is the bass. */\n")
        f.write('#include "arp_chords.h"\n\n')
        f.write("const char *const arp_genre_names[ARP_GENRE_COUNT] = {\n")
        for i in range(0, len(names), 6):
            f.write("    " + " ".join('"%s",' % n for n in names[i:i + 6]) + "\n")
        f.write("};\n\n")
        f.write("const uint8_t arp_chord_iv[ARP_GENRE_COUNT][12][ARP_CHORD_NOTES] = {\n")
        for name, chords in zip(names, table):
            f.write("    /* %s */\n    {" % name)
            parts = ["{%s}" % ",".join(str(v) for v in c) for c in chords]
            f.write(",".join(parts[:6]) + ",\n     " + ",".join(parts[6:]) + "},\n")
        f.write("};\n\n")
        f.write("/* each genre's scale for a SEQ NOTE lane on SCALE = GENRE: SEQSCALE_* | root << 4 (root: pitch class) */\n")
        f.write("const uint8_t arp_genre_scale[ARP_GENRE_COUNT] = {\n")
        scales = genre_scales(names, table)
        for (si, r), name in zip(scales, names):
            f.write("    SEQSCALE_%s | %d << 4,%s/* %s */\n" % (SCALES[si][0], r, " " * (12 - len(SCALES[si][0])), name))
        f.write("};\n")
    print("arp_chords.c: %d genres x 12 x 4 = %d B" % (len(names), len(names) * 48))
    del pcs


if __name__ == "__main__":
    main()
