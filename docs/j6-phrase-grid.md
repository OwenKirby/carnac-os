# Roland J-6 phrase grid vs the ARP page (2026-11-09)

The J-6 manual (p.34) gives a **9 STYLE (columns) x 12 VARIATION (rows)** grid. It spells out what styles 1-5 are and
says nothing about styles 6-9 beyond their names. This is what the manual states, what I infer, and how each cell maps
onto our ARP knobs (RATE, OCTAVE, STYLE, RHYTHM, and the pattern's TIMING).

## What the manual states

| Style | Category | Variations 1-6 | Variations 7-12 |
|---|---|---|---|
| 1 | Arpeggio, 8th notes | UP1, UP&DOWN1, DOWN1, DOWN2, UP&DOWN2, UP2 | the same, 8th triplets |
| 2 | Arpeggio, 16th notes | the same | the same, 16th triplets |
| 3 | Beat | double whole, whole, half, quarter, eighth, sixteenth note | the same, as triplets |
| 4 | Beat | `o___ _o__ __o_ ___o o~o_ o_o~` | `o~~o _o~o ooo_ oo_o o_oo _ooo` |
| 5 | Beat | `oo__ o_o_ o__o _oo_ _o_o __oo` | syncopation `o__ _o_ __o oo_ o_o _oo` |
| 6 | Phrase | Chord Phrases (Simple) | |
| 7 | Phrase | Chord Phrases (Rhythmic) | |
| 8 | Phrase | Strummed Chord Phrases (Simple) | |
| 9 | Phrase | Strummed Chord Phrases (Rhythmic) | |

`o` a 16th, `_` a 16th rest, `~` ties the next 16th on (eighth), `~~` a dotted eighth.
Pattern-level STYLE / VARIATION with a per-step override (steps with STYLE off keep the previous step's phrase), and
"for a shorter beat pick a style-3 phrase whose length matches" ("Using the Phrase Function" and "Changing the step length").

## What matches today

**Styles 4 and 5 are our rhythm table exactly.** `CARP_RHYTHM[row][col]` in arp_seq.c is style 4 (column A) and
style 5 (column B), cell for cell, rows 1-12 = variations 1-12 (checked 2026-11-09). Our "07T..12T" are style 5's
syncopation variations (3 cells). So our RHYTHM knob, "05A", is J-6 STYLE 4 / VARIATION 5, "05B" is STYLE 5 / VARIATION 5.

## What I infer for the cells the manual leaves open

1. **Styles 1 and 2 (arpeggio):** the six names are symmetric around the middle: UP, UP&DOWN, DOWN | DOWN, UP&DOWN, UP.
   The only thing that differs between "1" and "2" is therefore the range, 1 octave vs 2 octaves (UP1 / UP2 are the
   same direction). Variations 7-12 are the same six at triplet rate. Style 1 is 8th rate, style 2 16th rate; the
   arpeggio runs the whole step (a continuous stream, every cell struck).
   - Ours: STYLE UP / UP-DN / DOWN, OCTAVE 1 or 2, RATE 1/8 or 1/16 (1/8T, 1/16T for 7-12). All four knobs exist.
   - **Gap:** our rhythm table has no all-cells-struck pattern (`oooo`, `ooo` for a triplet). A steady arpeggio needs it.
2. **Style 3 (beat):** the chord, struck once, lasting that note length (double whole = two bars) and repeating at it:
   the manual's own use is to shorten a step's feel by choosing a shorter note.
   - Ours: BLOCK (or HOLD for the long ones) with the step's TIMING / RATE.
   - **Gap:** RATE stops at 1/2, so a whole or double whole note per step is only reachable through the pattern's TIMING
     (2 and 1), not per step. Triplet lengths (4/3 of a bar .. 1/6 of a beat) have TIMING / RATE entries only for the
     1/4T .. 1/32T range.
3. **Styles 6-9 (chord phrases):** four families of 12. By analogy with 4 and 5 (simple single hits / more rhythmic
   syncopated), I infer: 6 = chord (BLOCK) on style 4's rhythm for the variation, 7 = BLOCK on style 5's, 8 = STRUM on
   style 4's, 9 = STRUM on style 5's. That is 4 x 12 = 48 phrases, all of which our STYLE (BLOCK, STRUM UP / DN) x
   RHYTHM (24) already plays. **This is a guess**: the manual does not list the 48 patterns, so the real J-6 may play
   its own. It cannot be checked without the unit or a MIDI capture of one.

## What the ARP page would need to show the J-6's grid

| Option | What changes | Cost |
|---|---|---|
| A. A PHRASE view over the existing knobs | STYLE 1-9 + VARIATION 1-12 pick a (STYLE, OCTAVE, RATE, RHYTHM) combination and write those four stored bytes; nothing stored changes | no format change; the combination can be edited afterwards, and a hand-edited step has no J-6 name |
| B. Replace STYLE / RHYTHM with STYLE 1-9 / VARIATION 1-12 | stored bytes become style (9) x variation (12) | format bump; we lose independent order x rhythm (UP-DN x 06B etc.) and HOLD |
| C. Keep our knobs, rename to the J-6's words | RHYTHM list shows "4.05" / "5.05" (style.variation) | cosmetic; styles 1-3 and 6-9 stay unreachable by name |

Both A and C need the all-cells rhythm for the steady arpeggio and style 3's repeating chord: added 2026-11-09 as RHYTHM entry 25, "ALL" (`oooo`, 4 cells; no triplet `ooo` entry yet).
Recommendation: A (+ the new all-cells rhythm), since it adds the J-6 grid without losing anything of ours.

## Built 2026-11-09

The ARP page's fourth knob is now VARIATION (was RHYTHM), listing the J-6's style.variation: `4.01 5.01 .. 4.12 5.12` (style 4 = our A column, style 5 = our B / T column), then `ALL`. STYLE keeps HOLD (default, its VARIATION is the strum NONE / UP / DOWN), BLOCK, the note orders and STRUM UP / DN; RATE and OCTAVE stay free, which makes the J-6's arpeggio styles 1-2, note-length style 3 and variations 1-vs-2 octave / triplet 7-12 redundant. Stored bytes did not change.
