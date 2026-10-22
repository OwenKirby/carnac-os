#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The screen's rune face (src/host/rune_font_gen.py GLYPHS, the 5 x 9 bitmap every word on the FM1 is drawn in) as a
web font for the site: web/site/carnac-rune.woff. Needs fontTools; run it again only when the glyphs change.

    python web/site/make_rune_font.py

One pixel is 100 units of a 1000-unit em, so at font-size 10n px each pixel is n screen pixels (20 px: 2, 30 px: 3).
The caps stand on the baseline, 9 pixels tall, a pixel apart. Lowercase maps to the capitals, as on the screen; the
typographic quotes and dashes to their plain glyphs. Anything else falls through to the next font in the CSS stack."""
import importlib.util
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

HERE = Path(__file__).resolve().parent
SRC = HERE.parents[1] / "src" / "host" / "rune_font_gen.py"
OUT = HERE / "carnac-rune.woff"
PX = 100                              # units a pixel
W, H = 5, 9

spec = importlib.util.spec_from_file_location("rune_font_gen", SRC)
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
GLYPHS = gen.GLYPHS

ALIAS = {"’": "'", "‘": "'", "–": "-", "—": "-", "−": "-", "·": "."}


def glyph(rows):
    """the pixels as rectangles: each row's runs, a run merged downward while the rows below repeat it"""
    pen = TTGlyphPen(None)
    runs = []
    for r, row in enumerate(rows):
        c = 0
        while c < W:
            if row[c] == "#":
                s = c
                while c < W and row[c] == "#":
                    c += 1
                runs.append((r, s, c))
            else:
                c += 1
    done = set()
    for r, s, e in runs:
        if (r, s, e) in done:
            continue
        r2 = r
        while (r2 + 1, s, e) in runs and (r2 + 1, s, e) not in done:
            r2 += 1
            done.add((r2, s, e))
        x0, x1 = s * PX, e * PX
        y1, y0 = (H - r) * PX, (H - 1 - r2) * PX        # row 0 at the top, row 8 on the baseline
        pen.moveTo((x0, y0))                            # clockwise: TrueType's outer contours
        pen.lineTo((x0, y1))
        pen.lineTo((x1, y1))
        pen.lineTo((x1, y0))
        pen.closePath()
    return pen.glyph()


def main():
    names = {ch: ("space" if ch == " " else f"rune{i:02d}") for i, ch in enumerate(GLYPHS)}
    order = [".notdef"] + list(names.values())
    glyphs = {".notdef": glyph(["#####", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", "#####"])}
    for ch, rows in GLYPHS.items():
        glyphs[names[ch]] = glyph(rows)
    cmap = {ord(ch): n for ch, n in names.items()}
    for ch, n in names.items():
        if ch.isalpha():
            cmap[ord(ch.lower())] = n
    for ch, to in ALIAS.items():
        cmap[ord(ch)] = names[to]

    fb = FontBuilder(1000, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: ((W + 1) * PX, 0) for n in order})
    fb.setupHorizontalHeader(ascent=1000, descent=-200)
    fb.setupNameTable({"familyName": "Carnac Rune", "styleName": "Regular",
                       "copyright": "Carnac OS, GPL-3.0-only (font exception: embedding allowed)"})
    fb.setupOS2(sTypoAscender=1000, sTypoDescender=-200, sTypoLineGap=0, usWinAscent=1000, usWinDescent=200,
                sCapHeight=H * PX, sxHeight=H * PX)
    fb.setupPost()
    fb.font.flavor = "woff"
    fb.save(str(OUT))
    print(f"{OUT.name}: {len(GLYPHS)} runes, {OUT.stat().st_size} B")


if __name__ == "__main__":
    main()
