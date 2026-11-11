# MFX cost model (2026-11-11)

What an MFX costs on the FM1, why, and how the voice pool prices it. Measured with the PERF TEST LOOP rows
(`src/hardware/perf_test.c`, `K_LOOP`): a DUAL perf, layer 1 a one-note-a-step sequence at 1/16, layer 2 a pad ARP, 120 BPM,
long releases (RELEASE 110), TUMULUS reverb + tape ping-pong delay, STACK on both layers (4 voices sounding).
Two cores, normal build; the baseline row (LOOP STACK, no MFX) was 58.2 % in the run below (60.8 % in an earlier build:
the I-cache layout lottery moves rows by 2..3 points, so compare inside one run).

## How the runtime schedules the MFX (`synth_runtime.c`, the block after the layer sums)

* **One MFX on:** `mfx_layer_plan` on core 0 (what both sides share), then the left side on core 0 and the right on core 1,
  at the same time. Wall cost = a fixed part (~3 points) + half the MFX's full cost.
* **Both on:** layer 1's MFX whole on core 0, layer 2's whole on core 1 (`mfx_job`), at the same time. Wall cost = the dearer
  of the two (+ ~2 when the types differ), NOT the sum.
* So adding a cheap MFX on the other layer of a heavy one is NOT free (it ends the two-core split of the heavy one), and a
  second MFX of the same type is cheap (it only adds the difference between the two schemes).
* The MFX stage runs after the voices, so its wall cost comes off every core's share in the pool pricing.

## Measured cost over the no-MFX loop (points of the pass, average)

| MFX type | one layer | both layers (= its full cost X) |
|---|---|---|
| STONE PHASER | +6.2 | +7.6 |
| ENSEMBLE | +8.9 | +10.1 |
| ENS+PHASER | +10.4 | +12.1 |
| CHORUS | | +8.6 |
| TAPE FLANGER | | +10.8 |
| BBD FLANGER | | +8.9 |
| PITCH SHIFT | | +9.8 |
| BBD ECHO | +7.6 | +7.5 (does not split across the cores: one layer costs the same as both) |
| BRONZE AGE | +12.4 | +18.9 (EQ engaged: +20.5) |
| WIND HARP | +8.9 | +10.5 |
| RINGING STONES | | +10.0 |

Mixed pairs (both layers, different types), over the loop with none: phaser (L1) + BRONZE AGE (L2) +19.7; BRONZE AGE (L1) +
phaser (L2) +21.2; ENS+PHASER (L1) + BRONZE AGE (L2) +21.9; ENSEMBLE (L1) + phaser (L2) +12.2. Each is 1..3 above the dearer
one alone; BRONZE AGE on core 0 reads ~1.5 more than on core 1.

An earlier run (baseline 60.8 %) gave the same shape: phaser one +5.7 / both +7.0, BRONZE AGE one +11.8 / both +18.3, phaser +
BRONZE AGE +18.4.

Earlier 1-core stage profile (2026-11-11, build/fm1prof): IDLE phaser +22 ticks (4 points), two +40; BRONZE AGE one +86 ticks,
two +168 (the sum on one core; the wall time on two cores is the above).

## The pool's price (`PresetStructures.h`: `mfx_full_cost`, `voice_overhead`)

Tenths of a plain voice (1 tenth = 0.95 pass points): X = 0, 8, 11, 13, 9, 11, 9, 10, 8, 20, 11, 11, 10 by `MFX_*` order.
One layer: 3 + X/2 (rounded up). Both layers: max(X) + 2 if the types differ. SINGL perf mode: only layer 1's MFX runs.
Before 2026-11-11 every MFX cost a flat 5 per layer (10 for two): that over-charged two phasers (actual ~7.6) and under-charged
BRONZE AGE by ~7 (one layer) and ~9 (both), so patches with BRONZE AGE ran with more voices than the cores carry.

Effect on caps: STACK + BRONZE AGE 6 -> 4 voices; two STACK layers each with BRONZE AGE 3+3 -> 2+2; phaser patches unchanged.

## Not done / open

* BBD ECHO one-layer price over-charges it by ~3 (it does not split across the cores).
* Making the both-on path split the work evenly across the cores when the two MFX differ (phaser + BRONZE: ~3 points).
* Re-measure after the type-aware pricing: LOOP BRNZ rows now sound fewer voices (see the checklist run of that date).
