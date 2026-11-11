#!/usr/bin/env python3
"""Generates src/dsp/arp_chords.c: the ARP page's Chord Genre Bank, the Roland J-6's 100 chord sets (its manual pp. 25-33,
read into arp_chords_j6.txt by extract_j6_chords.py) and, after them, Phrygian.

    python src/dsp/gen_arp_chords.py            rewrites src/dsp/arp_chords.c

A chord key plays the J-6's chord for that key as printed: up to 4 notes, an inversion or slash chord's bass not always
the key. Each chord is stored against the J-6's own bass for the key (REF: C3 .. E3, then F2 .. B2), so a chord key
played at note N plays N + (printed note - REF): the J-6's voicing at whatever octave the key is in, bass on the key
for the root-position chords. Stored in 3 bytes a chord (21 bits): the lowest note's offset from REF + 20 (6 bits),
then the gaps up to the next notes (5 bits each; a gap of 0 ends the chord, so 2- and 3-note chords fit).

Phrygian (2026-10-17, the default genre) is ours, voiced with its bass on the key.
"""
import os

NAMES = ("Pop_1 Pop_2 Jazz_1 Jazz_2 Jazz_3 Blues Trad_Maj Trad_Min_1 Trad_Min_2 Pop_Min_1 Pop_Min_2 Jazz_Min_1 "
         "Jazz_Min_2 Oct_Stack 4th_Stack 5th_Stack Utility_Maj Utility_Min Utility_M7 Utility_m7 Utility_M9 "
         "Utility_m9 Utility_M9_11 Utility_m9_11 Pop_Inversions Minor_Inversions Pop_Synth_1 Pop_Synth_2 "
         "Pop_Synth_3 Pop_Synth_4 Pop_Synth_5 Pop_Synth_6 Cinematic_1 Cinematic_2 Cinematic_3 Cinematic_4 "
         "Cinematic_5 Cinematic_6 Synthwave_1 Synthwave_2 Synthwave_3 Synthwave_4 Synthwave_5 Synthwave_6 "
         "Synthwave_7 Synthwave_8 Synth_House Trance House_1 House_2 House_3 House_4 House_5 House_6 "
         "Jazz_House_1 Jazz_House_2 House_Techno Techno EDM_1 EDM_2 EDM_3 EDM_4 EDM_5 EDM_6 "
         "Gospel_RB_1 Gospel_RB_2 Gospel_RB_3 Lofi_RB_1 Lofi_RB_2 Funk_1 Funk_2 "
         "Neo_Soul_1 Neo_Soul_2 Neo_Soul_3 Neo_Soul_4 Neo_Soul_5 Neo_Soul_6 Neo_Soul_7 Neo_Soul_8 Neo_Soul_9 "
         "Neo_Soul_10 Jazz_Bossa Bossa_Nova_1 Bossa_Nova_2 Jazz_4 Jazz_5 Jazz_6 Jazz_7 Jazz_8 Jazz_9 Jazz_10 "
         "Jazz_11 Jazz_12 Jazz_13 Classical_1 Classical_2 Classical_3 Classical_4 Modern_1 Modern_2").split()
EXTRA_NAMES = ["Phrygian"]           # after the J-6's sets
# C Phrygian's seventh chords on its keys (i, bII maj7, bIII7, iv7, v half-diminished, bVI maj7, bvii7; dim7 on the keys
# outside it), as they were voiced before the J-6 bank: semitones above the key, which is the bass
PHRYGIAN = [[0, 7, 12, 15], [0, 7, 11, 16], [0, 6, 9, 15], [0, 4, 7, 10], [0, 3, 6, 9], [0, 3, 7, 10], [0, 3, 6, 9],
            [0, 3, 6, 10], [0, 4, 7, 11], [0, 3, 6, 9], [0, 3, 7, 10], [0, 3, 6, 9]]
BIAS = 20


def ref(pc):
    """the J-6's bass for a key: C3 .. E3, then F2 .. B2"""
    return 48 + pc if pc <= 4 else 36 + pc


def load():
    """chords[genre][pc] = ascending notes as MIDI, the J-6's sets then Phrygian (bass on the key, over REF)"""
    here = os.path.dirname(os.path.abspath(__file__))
    sets = []
    for line in open(os.path.join(here, "arp_chords_j6.txt")):
        if line.startswith("#") or not line.strip():
            continue
        n, rest = line.split(":")
        assert int(n) == len(sets) + 1
        sets.append([[int(x) for x in c.split(",")] for c in rest.strip().split(";")])
    assert len(sets) == 100 and len(NAMES) == 100, (len(sets), len(NAMES))
    phry = []
    for pc, iv in enumerate(PHRYGIAN):
        phry.append([ref(pc) + t for t in iv])
    return NAMES + EXTRA_NAMES, sets + [phry]


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
    "Techno": ("MINOR", 0), "House_Techno": ("MINOR", 0),
    "Phrygian": ("PHRYGIAN", 0),
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
                    s += 1.0 if inn == len(chords[k]) else inn / 8.0
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


def pack(chord, pc):
    """a chord's 21 bits: the lowest note's offset from REF + BIAS, then the three gaps (0 = none)"""
    lo = chord[0] - ref(pc) + BIAS
    gaps = [b - a for a, b in zip(chord, chord[1:])] + [0, 0, 0]
    gaps = gaps[:3]
    assert 0 <= lo < 64 and 1 <= len(chord) <= 4 and max(gaps) < 32 and min(chord) >= 0, (chord, pc)
    assert all(g > 0 for g in gaps[:len(chord) - 1]), chord
    return lo | gaps[0] << 6 | gaps[1] << 11 | gaps[2] << 16


def main():
    names, table = load()
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "arp_chords.c")
    ivs = [[[(n - k) % 12 for n in c] for k, c in enumerate(g)] for g in table]   # as the old fit read them: from the key
    with open(out, "w", newline="\n") as f:
        f.write("/* GENERATED by src/dsp/gen_arp_chords.py - do not edit. The Chord Genre Bank: the Roland J-6's chord sets, per\n"
                " * genre, per pitch class of the chord key (C .. B), 1..4 notes against the J-6's bass for the key. */\n")
        f.write('#include "arp_chords.h"'+chr(10)*2)
        f.write("const char *const arp_genre_names[ARP_GENRE_COUNT] = {\n")
        for i in range(0, len(names), 6):
            f.write("    " + " ".join('"%s",' % n for n in names[i:i + 6]) + "\n")
        f.write("};\n\n")
        f.write("/* 21 bits a chord in 3 bytes, low byte first: lowest note - REF + 20 (6), then 3 gaps (5 each, 0 = none) */\n")
        f.write("const uint8_t arp_chord_pk[ARP_GENRE_COUNT][12][3] = {\n")
        for name, chords in zip(names, table):
            f.write("    /* %s */\n    {" % name)
            parts = []
            for pc, c in enumerate(chords):
                v = pack(c, pc)
                parts.append("{0x%02x,0x%02x,0x%02x}" % (v & 255, v >> 8 & 255, v >> 16))
            f.write(",".join(parts[:6]) + ",\n     " + ",".join(parts[6:]) + "},\n")
        f.write("};\n\n")
        f.write("/* each genre's scale for a SEQ NOTE lane on SCALE = GENRE: SEQSCALE_* | root << 4 (root: pitch class) */\n")
        f.write("const uint8_t arp_genre_scale[ARP_GENRE_COUNT] = {\n")
        scales = genre_scales(names, ivs)
        for (si, r), name in zip(scales, names):
            f.write("    SEQSCALE_%s | %d << 4,%s/* %s */\n" % (SCALES[si][0], r, " " * (12 - len(SCALES[si][0])), name))
        f.write("};\n")
    print("arp_chords.c: %d genres x 12 x 3 = %d B" % (len(names), len(names) * 36))


if __name__ == "__main__":
    main()
