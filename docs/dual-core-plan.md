# Dual-core rendering on the FM-1: plan

Status (2026-10-14): **step 2 done** (block-major render, single core, bit-identical); steps 1 and 3 onward wait for
the device. The synth still runs on one core everywhere, so the browser emulator and the host tests work as before.

Build check and watch list: [dual-core-watchlist.md](dual-core-watchlist.md). The two shared globals it found in the
voice path (`noise_seed`, `mod_tick`) are now per voice.

## Why

FM-1 load, estimated from X0X's measurements on a real FM-1 (see "Calibration" below):

| Load | One core |
|---|---|
| Plain patches at the voice caps | ~45–95 % |
| Heavy patches | ~70–130 % |

Felucca starts shedding voices at 85 %. X0X went from 115 % to 68 % by moving work to the second core.
Here we expect about 35–40 % less on core 0: for 8 SINGLE voices, roughly 190 → 115 ns/sample on the dev PC.

## What does not change

- The sound. The dual-core output must be **bit-identical** to one core's. The voices are independent
  and their outputs are summed in a fixed order, so it can be.
- The browser emulator and the host tests. They keep the single-core path. Dual-core is compiled only into the FM-1
  build (`FELUCCA_ID` + a `SYNTH_DUAL_CORE` switch), with a run-time fallback to one core.

## The one structural change: frame-major to block-major (done)

**As built (2026-10-14).** `synth_render_q15` renders passes of up to 128 frames (`render_pass`). The global work
still runs frame by frame on core 0: the clock, the MAQ and the ARP with their events, and the layer control tick.
The voices lag behind it. `voices_up_to(f)` renders every voice from where it stopped up to frame f, voice-major
(`render_voice_run`), and it is called just before anything a voice reads changes:

- a MIDI event (`synth_midi_3` catches the voices up first when it is called inside a pass);
- a clock pulse or clock reset (`midi_clock_frame_changes`);
- the layer control tick (every `SYNTH_CTRL_PERIOD` = 8 frames).

So a run is at most 8 frames, and between runs nothing a voice reads moves. The voices ask the clock about their own
frame (`midi_clock_phase_at`, `VoiceInput.frame`), not the frame the global work has reached. The voices' dry sums
and sends go into a `MixBus` (int64 per frame); the FX and the limiter run over it after the last run. The output
was checked bit-identical to the frame-major loop over 43 render hashes (every architecture with and without noise
and mod slots, two layers, MAQ + ARP + synced LFOs and retriggers on the internal and the MIDI clock, block sizes
7 / 32 / 128) and the two demo WAVs.

**For two cores** this means: inside `voices_up_to`, core 0 runs `render_voice_run(&bus0, 0, k, from, to)` and core 1
runs `render_voice_run(&bus1, k, 8, from, to)`; core 0 waits, and the tail adds bus1 into bus0 before the FX. The
hand-off happens once per run, so 16 or more times per 128-frame half. If that sync costs too much on the device,
lengthen the runs: compute the layer ticks of the whole pass first and store them per tick (the original plan below),
so only events and pulses end a run.

The original sketch, kept for that option:

Before, `synth_render_q15` was **frame-major**. For each sample it:

1. steps the clock, MAQ and ARP;
2. runs the layer control tick;
3. renders every voice;
4. sums the voices into the FX, then the limiter.

Sequencer events land between samples, in the middle of the voice loop's lifetime. Two cores need
**block-major**:

1. **Core 0, events:** run the clock, MAQ and ARP over the whole block and record every note-on/off,
   transpose and parameter change as a timestamped event (frame offset). No voice is touched yet.
2. **Both cores, voices:** each core renders its share of the voices through the block, applying the
   events at their frame offsets. Each core writes its own dry L/R and send buffers (int32 per frame).
3. **Core 0, tail:** wait for core 1, add the two sets of buffers in a fixed order, then run the FX and the limiter.

Things that must stay exactly where they are in time:

- note-ons/offs and voice stealing (`voice_start`, `enforce_voice_limits`), now applied at event
  offsets by the core that owns the voice;
- the layer-wide control tick (`synth_voice_begin_tick`: LFO1, OSCMIXER), computed by core 0
  for each `SYNTH_CTRL_PERIOD` boundary in the block and stored per tick, so both cores read the same values;
- `master_midi` (mod wheel, aftertouch, CCs), snapshotted per event, not read live.

**Testing:** src/host/test_render_hash.c renders those scenarios and compares each hash with
src/host/render_hash_golden.txt. Run it after every step below: a second core must leave all 43 lines unchanged.

## Splitting the voices

- Static first: voices 0..3 to core 0, 4..7 to core 1. Core 0 also carries the events, the FX and the limiter,
  so give core 1 more voices if core 0 turns out to be the critical path (X0X found exactly this and rebalanced).
- Later, per block: split by the voices actually sounding and their architecture cost (varch cost table in
  PresetStructures.h).
- Voice allocation and stealing stay on core 0. A voice is only reassigned between blocks.

## Core 1 bring-up (from X0X's notes, docs/plans/2026-10-04-dual-core-and-clock.md, GPL-3.0)

Reuse X0X's `hal/fm1_cpu1.{h,S}`, with credit; Felucca and X0X are both GPL-3.0. Its traps, found on hardware:

- Start core 1 **at power-on**, before the audio and the timers. Started later, it never reaches its entry.
- Open the **PC limits** while it boots through ROM, then arm them again.
- After `rti` core 1 runs on **usp**: give that a real stack (X0X needed 828 B at the deepest; 256 B overflowed).
- Core 1's **idle loop lives in RAM** (`.c1_text`, inside the PC window). The image builder must include that section.
- Hold before a reset: set C1_CON bit 1, then clear bit 3.
- Handoff: a single-writer flag per direction plus `csync`. The caches are shared, so no flushes are needed.
- **Fallback:** some FM-1s read wrong data on core 1 (X0X: "1 CORE: BAD READS"). Detect it at start-up with a
  known render compared against core 0's, then run single-core and say so on the ABOUT/performance screen.

## Flash writes: park core 1

Nothing may execute from or read the SPI flash while it is being written. Core 1 must finish its job and spin in
RAM for:

- patch / performance / sequence saves (flash records, spec 6.4);
- autosave and globals;
- the **wavetable store upload** (src/hardware/wt_store_fm1.c, SysEx 96..99);
- OTA.

Core 0 raises "park", waits for the acknowledgement, writes, then lowers the flag.

## Risks specific to this synth

- **Wavetables are read from flash (XIP).** With both cores rendering wavetable voices, they compete for the
  flash and the shared 32 KB I-cache. This can only be measured on the device. If it hurts, copy the banks a
  patch uses into the .pool (RAM has ~260 KB free), or move the hot render loops into `.ram_text`.
- **Code size in the I-cache:** two different voice architectures rendering at once on two cores may evict
  each other.
- **Unity build:** core 1's entry and its RAM section must survive Felucca's single translation unit. Add a
  `c1_`/`synth_` prefix to everything new.

## Order at deploy time

1. Felucca glue working on one core (see the integration notes). Measure `cpu_pct` with the plain and heavy
   patches, and run X0X's PERF TEST once on the same unit to check the calibration.
2. ~~Block-major render, single core, bit-identical~~ (done 2026-10-14).
3. Port X0X's core-1 bring-up; core 1 runs an empty job; check start-up, reset and the flash-write parking.
4. Move voices 4..7 to core 1 (`render_voice_run` with its own `MixBus`, see "As built"); compare against single-core
   output on the device (bit-identical: the same scenarios as test_render_hash) and read `cpu_pct`.
5. Rebalance (static, then per block). Add the bad-reads fallback and an overload guard (shed a voice above 85 %,
   as Felucca does).
6. Only then consider raising the voice caps in PresetStructures.h.

**Main risk:** the wavetables are read from flash, so two cores rendering at once compete for it (see "Risks"
above). If that hurts, copy the banks a patch uses into RAM.

**Step 2 was the only step that could be done without hardware, and it is done.** Everything left needs the FM-1.

## Calibration (2026-10-13)

- X0X's PERF TEST on a real FM-1, one core: 43 / 83 / 115 %. The same patterns in X0X's host simulator on the dev PC
  (gcc -O2): 113 / 246 / 347 ns/sample. So 100 % of an FM-1 core ≈ 265–300 ns/sample on that PC.
- X0X measured the core clock at **360 MHz** (M-VAVE's boot sets it; it cannot be raised further).
- X0X's code is floating-point and this synth is integer, so the scale may be up to ~1.4x worse for us. This gives
  the upper ends of the ranges above.
