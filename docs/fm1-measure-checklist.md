# FM1 measurement checklist

What the host can't settle and has to be measured on a real FM1. Every number in the code that is marked "host-measured" depends on these. Tick each item off with the date and the number you measured, then fix the code that depends on it.

**How to measure CPU:** `python src/hardware/fm1_perf.py run` with the synth firmware on the FM1 (`fm1_perf.py all` builds and installs it first). It runs the built-in PERF TEST (src/hardware/perf_test.c: idle, SINGLE x 1 / x 8, every architecture at its cap plain and heavy, each filter flavour, DUAL + MAQ + ARP at 167 BPM on the HOME / AMP / SEQ / MOD 1 pages) and adds its table to "PERF TEST runs" at the end of this file. The same test runs on the host (src/host/perf_test_host.c), scaled by X0X's calibration: 100 % of one core at 360 MHz is about 265–300 ns/sample in this PC's host sim. The synth does not shed voices: during the test a guard cuts any scenario after 8 late halves in a row; outside it nothing does (see "Overload" below).

## Voice caps (src/PresetStructures.h `varch_voice_cap`)

- [ ] **STRIKE ATTACK page, 2026-10-16 (synth_strike5.fwsc, installed).** DECAY / TO FILTER / START / VELOCITY / KEY TRACK, patches packed to 173 B (v7). PERF: STRIKE 40.1, STRIKE HEAVY 53.6, SEQ+ARP STRIKE 69.9 / 72.9, SEQ+ARP STRK FILT (TO FILTER 64) 69.9 / 73.2: pass at the edge (synth_strike4 read 70.1 before the step was cached). If it creeps over: STRIKE's cap to 7. By ear: TO FILTER sweeps (through a resonant filter), DECAY's range, START on the chiffs, the ATTACK page's outline. Patches saved before (v5 / v6) load the same after a power cycle.
- [x] **STRIKE at 8 voices, 2026-10-16: measured.** synth_strike1: STRIKE 40.4 % (SINGLE 40.0), STRIKE HEAVY 53.7 (53.0), SEQ+ARP STRIKE 70.6 / 73.4 peak (SEQ+ARP CELEST+PP 65.8): over budget. A RAM copy of the decode table and fetching the byte pair only on a new byte changed nothing (synth_strike2: 71.3): the cost is instructions (XIP), not loads. Fusing the attack into the voice's last loop (finish_strike_block, synth_strike3, installed): STRIKE 40.2, STRIKE HEAVY 53.6, SEQ+ARP STRIKE 69.4 / 72.1 peak: passes, thin margin. Cap stays 8. Still to do by ear: level against the pad, tuning across the keyboard, no click at the attack's end. Original note: STRIKE = SINGLE + a one-shot 8-bit mu-law attack (src/dsp/attacks.c, 71.7 KB in flash, read through XIP). Host: 0.89 ns per voice-sample for the attack read vs ~19-22 ns per SINGLE voice; test_voice_arch reads 1.00x SINGLE for held notes (the attack is over by then). Cap left at 8. Run PERF TEST rows STRIKE, STRIKE HEAVY and SEQ+ARP STRIKE (restarts the attack on every step: the worst case, XIP reads of up to 8 attacks at once) against SINGLE / SINGLE HEAVY / SEQ+ARP CELEST+PP. Also by ear: the attack's level against the pad, BOW PLUCK / LITHOPHONE in tune with OSC1 across the keyboard, no click when an attack ends.
- [ ] **Saved patches survive the v5 -> v6 patch format (2026-10-16).** FREC_VER_ENGINE 6 adds STRIKE's 3 bytes; patches saved before load through engine_from_v5 (test_preset_store checks it on a simulated NOR). On the device: before installing, note a few saved patches; after, power cycle and check they load and sound the same; save one, power cycle again.

- [ ] **SINGLE x 8 runs in real time.** Every cap is "8 / cost relative to SINGLE", so if this fails, every cap is wrong. Test the default patch and a heavy one (FX, mod slots, spread, ring mod).
- [ ] **Each architecture's cost against SINGLE.** On the host: DUAL OSC 1.46–1.61, SPREAD 1.36–1.63, DUAL FILTER 1.56–1.72, STACK 1.76–2.04, BINAURAL 1.78–2.04. Play each one at its cap (5 / 5 / 5 / 4 / 4) and check none overruns.
- [ ] **SINGLE + MS-20 LOW / HIGH at 8 voices.** The host read 1.09–1.16x SINGLE, which strictly means a cap of 7. It was left at 8 because that is within the host's noise. If it overruns, make MS-20 a third cap column (SINGLE 7).
- [ ] **LADDER cost.** The host read SINGLE 1.33–1.41, DUAL FILTER 2.06–2.65 (caps 6 / 4 / 4 / 3 / 3 / 3). It is the dearest flavour: 4 one-pole stages, 2 passes a sample, a clipper each pass.
- [ ] **64-bit multiplies on the pi32v2.** The LADDER and MS-20 one-pole stages (`ONEPOLE` in src/dsp/filter.c) and the LADDER output tap are 32 x 32 -> 64-bit multiplies. That is cheap on x86 but may be several instructions on the pi32v2, so the host ratios above may understate the device cost. If they are dear, split the multiply into two 32-bit halves, or keep the states in Q15 + 8 bits held as two int16.
- [ ] **FORMANT cost.** It now runs two full SVFs (before, its second band was a shortcut). The host said it was no dearer than the SVF; check that on the device.

- [ ] **SH-2 filter, DRIVE, one MS-20 (2026-10-21, in source, not installed).** The SH-2 replaced the SVF: four OTA stages (a cubic each), a clipping input mixer (a cubic) and a tanh-bounded resonance (3 mul + a divide) a sample. Host, per SINGLE voice (bench_voice_arch, resonance 90): SH-2 42.3 ns, MULTI 22.0, MS-20 22.5, FORMANT 10.3, so an SH-2 voice is ~1.9x a MULTI one. Estimate from the 2026-10-15 profile (FILTER ~21 % of a core for 8 voices): 8 SH-2 voices on both cores add ~10 % a core, SINGLE x 8 ~38 -> ~48 %, heavy ~53 -> ~63 %. Run the PERF TEST: row SINGLE SH-2 (new, the MS-20 HIGH row's place), and a DUAL FILTER / STACK patch with both filters SH-2 at their caps (7 / 6): the dearest case, not a PERF row yet. Caps are per architecture only; if SH-2 overruns, the cheap fix is a filter-flavour cost column in varch_voice_cap, NOT fewer voices for everything. Also check: FILTER DRIVE (new first row) on every flavour; MS-20 morph LP .. BP .. HP; saved v8 patches and performances load after a power cycle (SVF ones as MULTI LP12 .. BP .. HP12, MS-20 LOW / HIGH ones as the MS-20 with their old drive); the SH-2 self-oscillates in tune up to the top of the cutoff, at DRIVE 0 and 127.

- [ ] **Float filters (2026-10-15, build/fm1/synth_float1.fwsc, profile build synth_float1_prof.fwsc).** All flavours are now single-precision float. SVF, FORMANT, LADDER and MULTI are zero-delay (TPT) circuits that run one pass a sample instead of two. The oscillator's table reads are worked out once per tick (bit-identical). On the host, per voice: SINGLE 20.3 -> 16.8 ns, DUAL FILTER 32 -> 22, SINGLE LADDER 28 -> 23. On the device, compare the FILTER / OSC / EXTRAS stages of the profile against the 2026-10-15 13:01 profile. Expected: FILTER well down (no int64 MACs, one pass), OSC a little down. **Device 2026-10-15 (synth_float2.fwsc, normal build): smaller win than the host said.** SINGLE x 8 120.7 -> 115.0 %, SINGLE HEAVY 140.4 -> 135.6, per SINGLE voice ~12.4 -> ~11.7 % (host predicted ~17 % less per voice, device gave ~6 %). DUAL FILTER x 5 100.7 -> 92.7, SINGLE LADDER/MULTI x 6 100.7 -> 95.3, DUAL FLT LADDER x 3 73.5 -> 68.7 (now passes). Stage split not measured yet (needs synth_float2_prof.fwsc).
- [x] **MULTI (FLT_MULTI) cost.** MULTI replaced LADDER in flavour slot 1 (2026-10-15). It is the same cascade with one more output tap, and LADDER's slopes are its first four modes. It keeps LADDER's cap column (flt_heavy). Expected: the same cost as LADDER's float version. **Device 2026-10-15:** the SINGLE LADDER and DUAL FLT LADDER scenarios now run MULTI (slot 1): 6 voices 95.3 %, DUAL FILTER x 3 68.7 %, about 1.05x SINGLE per voice ((95.3 - 21.3) / 6 = 12.3 % vs 11.7 %), so its own cap column (6) is now stricter than it needs to be. **Done 2026-10-15:** at the normal caps every architecture passes with the heavy patch on MULTI (worst SPREAD x 7, 64.4 %), so the separate MULTI caps (flt_heavy) were removed.

- [x] **Optimisation batch 2026-10-15 afternoon (device, normal builds).** SINGLE x 8: float2 115.0 % -> synth_amp1 101.9 % -> synth_ctrl1 101.5 %; IDLE 21.3 -> 19.0; per SINGLE voice ~11.7 -> ~10.3 %. (1) MAQ/ARP idle fast path, bit-exact (src/host/test_seq_idle.c): stopped SEQ 45 -> 29 ticks a sample. (2) Float AMP + MIX (tick gains folded: pan, routing, mix gain, sends; float mix bus; the filter's float output used directly): AMP 71 -> 53, MIX 45 -> 19 ticks at 8 voices; output within 0.02 dB of the int version, difference -54 dB (rounding; A/B WAVs in build/listen_float_amp/). (3) FOLD drive now folds (it clamped first: a clipper). (4) CTRL: divides -> tables (gen_ctrl_tables.py), NONE sources skip the call, bit-exact: c.OSC 40 -> 38, total -0.4 %: not worth more. CTRL split (8 SINGLE, ticks a sample): LFO2/3 28, ENV 22, pitch+osc 38, filter prep 18 (heavy 58), amp 8.

- [x] **Two cores + blocks + FX, 2026-10-15 evening (device, synth_fx4.fwsc).** Two cores (voices 1,3,5,7 on core 1; one core / two / fallback give the same output), block-by-block voices (bit-identical), block FX in int (float would be slower here: float adds ~6 cycles), the reverb on core 1 beside the delay. SINGLE x 8 37.5-38.2 %, heavy ~52.7, SEQ+ARP ~64.5, every architecture at its cap under 62 %. 26 of 27 pass.
- [ ] **SEQ+ARP CELEST+PP: 71.9 % avg, 74.2 % peak, no late halves (budget 70).** The torture test (both layers, 4-note chords at 1/32 with 4x ratchet at 167 BPM, ARP over 4 octaves) plus CELESTIAL and PING PONG. Options if it matters in practice: overlap the FX with the next pass's voices, or give the sequencer events fewer runs.
- [ ] **Listen on the FM1: the new reverbs** (EFFECTS > REVERB TYPE): MOD (Lexicon-like chorused tail), CELESTIAL (octave shimmer; DAMP tames it), DOLMEN (small bright stone chamber), TUMULUS (huge dark cavern). Host checks: all stable at every TIME / DAMP, tails die to silence, L/R correlation 0.03-0.3.
- [ ] **Listen: ROOM's decay changed (a fix).** Its combs held a residue for ever (up to ~90 LSB, so REVERB TIME read ~22 s at every setting); now 0.9 / 1.8 / 3.6 s at TIME 30 / 90 / 127. Render hashes rewritten for it.
- [ ] **Listen: PING PONG and TAPE DEGRADE** (EFFECTS page). DEGRADE 36 = the old sound exactly; 127 = slow + fast warble, dark and thin.
- [ ] **UI frame rate under the SEQ+ARP torture test:** 5-19 fps on the SEQ and MOD 1 pages (the audio takes ~65 % of core 0).
- [ ] **Full-screen popups, 2026-10-16.** The parameter popup (Select Encoder) and the preset list now redraw the whole 240 x 240 (two 240 x 120 canvases, 115 KB over the SPI) on every cursor or value change, where they used to redraw ~29 KB. Turn the Algorithm Encoder fast with the popup up, with and without the SEQ+ARP torture test: the moon and the value should keep up and the audio must not glitch. If it lags, redraw only the band that changed (the value and the moon sit across both bands today).

- [ ] **Listen: tank reverbs after the 2026-10-15 guard** (MOD, CELESTIAL, DOLMEN, TUMULUS). Their spin and wander now update every 16 samples (a 0.01-sample step of the swept delay: should be inaudible), and a guard ducks the feedback and input when a line passes 16000 of 32767, so a hot send at REVERB TIME 127 levels off instead of clipping in the loop (host: clipped tank writes 0.016 % -> 0.005 % at a -6 dB send, a full-scale square torture input ~8x fewer clips). fx hash goldens rewritten. Check that loud pads into TUMULUS / CELESTIAL at TIME 127 no longer buzz and that quiet tails sound as before.
- [ ] **Listen: the reverb buzz fix, 2026-10-16 (synth_revbuzz1.fwsc).** Cause: the voices' summed reverb and delay sends were hard-clipped at full scale before the effects (a hot patch reaches it with 3-4 voices: 8 driven voices at full send peak +8 dB, 21 % of the samples clipped), the wet outputs were clipped again after them, and the tank sat in its guard 50-60 % of the time, ducking in -0.5 dB steps every 16 samples. Now each send has a limiter (the master limiter's law), the wet goes unclipped to the master limiter, the tank runs 6 dB lower inside (the guard rests except TUMULUS ~19 % on the hottest case; quiet tails within 0.7 dB of before), the guard glides instead of stepping, and ROOM's output allpasses run at half level. Host: zero clamp hits on every type at 2-8 hot voices. PERF TEST unchanged (HEAVY CELEST+PP 56.5 %). Sound changes: loud chords into MOD / CELESTIAL / TUMULUS now give a tail up to ~6-9 dB louder than before (it was being squashed). Compare: build/listen_reverb_buzz/*_before.wav / *_after.wav (6 driven voices, full send, TIME 110). Check: play a hot chord with reverb (and with the delay): no buzz; tails not too loud at full send.
- [ ] **Listen: PLATE / SPRING / ROOM / HALL whine fixed, 2026-10-16 (synth_revbuzz2.fwsc).** User: "high pitch buzz as soon as I send any sound to the plate reverb, and it keeps buzzing eternally". Cause: the ROOM network's two output allpasses halved their feedback with a floor (o >> 1), so a few LSB circulated for ever after any sound (0 .. -2 LSB before 2026-10-16, -4 after the x2 output gain; ~7000 value changes a second). Now halved toward zero: exactly silent 30 s after release on every reverb type. Check: send a note to PLATE, let it fade: silence.
- [ ] **Listen: SPRING reverb and BBD delay, 2026-10-16 (synth_spring2.fwsc).** SPRING (REVERB TYPE, was the ROOM network): two dispersive springs (34.7 / 43.2 ms trips, 12 stretched allpasses each, float loop): every echo a falling chirp (170 Hz 5.3 ms behind 4 kHz in the first echo, more each trip), RT60 0.3-4.5 s over TIME, level as PLATE, tails end in exact silence. Device: HEAVY SPRING+PP 55.4 % vs HEAVY CELEST+PP 56.3 % (a 16-stage version cost 57.1: over). DELAY TYPE (new EFFECTS row, perf field delay_type, FREC_VER_PERF 4): TAPE or BBD (2048 stages, an MN3008's: held buckets, so long delays alias and crumble; fixed dark 2-pole filters ~2.7 kHz at DEGRADE 36; compander noise that follows the echo). Device: HEAVY CEL+BBD PP 56.2 % = the tape's. TAPE DEGRADE also drives the BBD's darkness, wobble and noise. Demos: build/listen_spring_bbd/*.wav. Check: SPRING's drip and boing on short notes; BBD dark and crumbly at long DELAY TIME; DEGRADE sweeps on BBD.
- [ ] **Listen: both delays self-oscillate, 2026-10-16 (synth_selfosc1.fwsc).** FEEDBACK 0-100 unchanged (bit-exact); 100-127 the loop gain passes unity (~114) up to 1.22 (1.62 at DEGRADE 127, so worn settings still howl), a low-pass in the feedback closes to ~2 kHz at 127 (a dark howl, not a roar), the record saturation holds the level (settles -2 .. -10 dBFS; peaks up to +2 dBFS go to the master limiter), and a faint hiss seed starts it from silence in ~20 s. Host: 110 never runs away, 118 holds on fresh settings and fades on worn ones, 127 howls everywhere, on the note that went in; DELAY TIME bends its pitch. PERF TEST unchanged (39 / 40). Demos: build/listen_spring_bbd/selfosc_*.wav. Check: it sounds musical, not harsh; pulling FEEDBACK back stops it.
- [ ] **Listen: sequencer event timing, 2026-10-16 (synth_seqq1.fwsc).** MAQ / ARP events between the voices' run boundaries (ratchet bursts, strum notes, gate ends) now wait for the next boundary, at most 15 frames (0.34 ms), instead of cutting every voice's run (synth_runtime.c seq_q). Events on a clock pulse or a layer tick (most steps' notes) are exact as before. Device: SEQ+ARP HOME 64.0 -> 60.8 %, CELEST+PP 68.4 -> 65.3, SPR+BBD 67.4 -> 64.3, SPRD CEL 79.4 -> 76.2 (still over). Check: fast ratchets and strums still feel tight.
- [ ] **Try: SEQ SCALE / ROOT, 2026-10-16 (synth_scale1.fwsc).** NOTE lanes' footer: SCALE (OFF, Genr, Maj, Min, Dor, Phr, Lyd, Mix, HMin, MajP, MinP, Blus) after TARGET, ROOT (C..B) after it with a fixed scale. A knob detent is a scale degree (past the top note: TIE), a set value and keyboard capture snap to the scale, a stored note outside it plays (and shows) as the nearest scale note (ties down) and is kept, so OFF or another scale gives it back. Genr follows the ARP's GENRE BANK (arp_genre_scale, generated: fitted to the chords, by hand for the one-chord-type genres); TRANSPOSE MAQ / a TRANSPOSE lane move the scale with the notes. SequencerLane 104 B (FREC_VER_SEQ 2), UiClipboard 861. PERF TEST: SEQ+ARP rows within 0.5 % of before. Check: the footer reads well on the device; the genre scales feel right (list in src/dsp/arp_chords.c).
- [ ] **Try: SCALE with TRANSPOSE MAQ, 2026-10-16 (synth_scale2.fwsc).** The scale now applies after the transposition (after the QY-10 manual pp. 36-37: patterns in C, re-mapped to the chord): the scale stays in its key and the ARP's chord roots move the notes within it, so C E G in C major plays each chord of the key (Am: A C E, not A C# E). SCALE OFF: the plain transposition as before. Check: a NOTE lane on Genr with TRANSPOSE MAQ over an ARP progression stays in key.
- [ ] **Listen: DOLMEN more present, 2026-10-16 (synth_dolmen1.fwsc).** User: hard to hear. Its level going in matched the others; its tail was the problem (RT60 0.36-1.0 s, covered by the dry note). Decay range 0.18-0.65 -> 0.45-0.82: RT60 ~0.6 s (TIME 0), ~1 s (64), ~1.7 s (110), ~2.2 s (127), tail 7-8 dB up (~2 dB over PLATE); still a small bright chamber, tails end in silence. Check: audible behind a dry note, still not a hall.
- [x] **The reverb buzz, found and fixed 2026-10-16 (synth_buzzfix3.fwsc).** Cause: the JieLi compiler miscompiled render_pass's mix-bus clear (one statement clearing both buses) into indexed 64-bit stores 4 bytes low, so bus 0's frame 127 reverb send was never cleared: it kept the last voices' send and fed the reverb every pass (a 345 Hz pulse train, for ever, until a reboot; layout-dependent, so it came and went between builds). Found on the device with the new STATS meters (`fm1_perf.py stats`) and a write trap; the host (gcc) never showed it. Fix: each bus cleared through a volatile pointer, a frame a step. build_fm1.py now refuses a build with any indexed 64-bit store. Also added as precautions: core 1's writes (odd voices, mix bus 1, the reverb's back state and rings, fx_rev_l/r) on 64-byte cache lines of their own. Device: FOG BANK + chord: reverb silent 20 s after release (was -13 dBFS for ever). PERF TEST within ~0.6 % of before (IDLE 16.6, SINGLE HEAVY 52.7). Debug tools kept: ED_STATS (46) levels / live settings / core-1 stack, ED_DEBUG (47) load a performance, reverb back on core 0, effects reset.
- [ ] **Try: presets survive power-off, 2026-10-16 (synth_presets1.fwsc).** The wavetable image (233,472 B) is in the firmware now (image 430 KB of 581 KB; WT_INFO: attached, store 0 KiB; the old upload commands are refused). Its old store flash (0xA0000) holds the preset store (src/hardware/preset_store_fm1.c): patches, performances, sequences, arps, 128 slots each, 25 chunks x 2 sectors (200 KiB), each save into the copy that is not current, header last; host test test_preset_store (power cuts, other layouts, damaged copies). SAVED shows once the chunk is in flash; a save mutes the audio for the erase (tens of ms). The THEME persists too (OBJ_SET). PERF TEST unchanged. Check: save a patch, a performance, a sequence and an arp pattern, set a THEME, power-cycle: all back.

## Filter sound (src/dsp/filter.c)

- [ ] **MS-20 runs one pass a sample (44.1 kHz).** Up to about 1 kHz it is the same as before. Around 3 kHz the resonance peak drops from about +14 to +7.5 dB, and at the top of the knob it tops out near 6 kHz at about +2 dB. Listen to high-cutoff sweeps with the resonance up: if the loss is audible and the CPU has room, go back to two passes for MS-20 HIGH only.
- [ ] **Self-oscillation.** LADDER and MS-20 self-oscillate at the top of the resonance knob, but only at low cutoffs (roughly below knob 50); higher up they ring without sustaining. Decide whether that is acceptable or whether the feedback should rise with the cutoff.
- [ ] **Levels.** Check LADDER at full resonance (the passband drops about 8 dB, the Moog thinning) and MS-20 DRIVE at 127 (hot signals come out about 7 dB down) against the SVF, so switching flavours doesn't jump in volume.
- [ ] **Filter KEY TRACK (2026-10-21, build/fm1/synth_fltkt1.fwsc, host-only so far).** Third row on FILTER1 / FILTER2, 0..127, 127 = the cutoff moves an octave per octave, pivoting on C4. With resonance high and KEY TRACK 127, the self-oscillating peak should play roughly in tune across the keyboard. Old patches and performances must load with KEY TRACK 0 and sound the same; save a patch with KEY TRACK set, power-cycle, and check it comes back (it is stored in the high nibbles of the filter's MOD SRC / MORPH MOD SRC bytes, and those two rows must still read right).

## Screen (src/ui_seq_draw.c `sq_stone`, src/ui_page_draw.c)

- [ ] **Stone drawing cost.** Every stone pixel takes 2–4 hashes. The worst cases:
  - the SEQ grid redraws a lane on every step (16 stones of 12 x 21)
  - the AMP page redraws whenever a voice's level changes (8 voices, up to 3 echo rows, and fog over the whole 228 x 50 plain)

  Measure the UI frame time on those two pages while playing, and check that audio `cpu_pct` doesn't move. If it is slow, cache the fog and each stone's grain mask instead of hashing per pixel.
- [ ] **How it looks on the panel.** Check the stones' grain (the 3/4 and 1/2 shades of RGB565) and the fog's specks on the real LCD at its real brightness. Too faint means raise the grain's contrast; too busy means coarsen it.
- [ ] **MOD page tomb (240 x 154, two bands).** While the slot's output moves, the whole tomb redraws every UI frame: two stone pillars (54 x 132, hashed per pixel, plus the lit-edge pass of `pu_block_lit`: outline precomputed per stone, a few table lookups a pixel), the capstone, the chamber's motes, about 74 KB over the SPI. Measure the UI frame time with an LFO driving the slot. If it is slow, redraw only the chamber (x 66 .. 173) and leave the stones as they were.
- [ ] **Watched mod slot (`synth_mod_watch`).** On a MOD page each control pass also runs that slot (each voice, plus each layer's voice-less pass), even when nothing reads it. Check that audio `cpu_pct` stays the same with a MOD page open as with the HOME page open.
- [ ] **SEQ step stones (12 x 21).** Check they read clearly at arm's length: the value fill, a muted step's cross, a skipped step's ruin.
- [ ] **SEQ lane fog, slab and grass.** With the cursor on LANE, the active lane's grid redraws on every step, and each pixel of its 240 x 29 fog does two bilinear value-noise lookups (8 hashes, 2 divides). Measure that lane's redraw time while playing. If it is slow, compute the fog once per palette into a 240 x 43 tone map (2 bits a pixel, about 2.6 KB). Also check on the panel that the fog's darkest tones (10..46 R) are not crushed to black, and that the grass blades read as grass and not as noise.
- [ ] **Key LED levels on SEQ / ARP (`ui_sm_key_leds`, 2026-10-14).** Three levels: lit, mid (Felucca's `fm1_led_mid`, 1 frame in 8) and dim (the glow). Hold OCT- while a lane plays and check, in a dark room and in daylight, that the loop boundary (mid) stands out from the rest of the loop window (dim) and from the playhead (lit), in DIM HI, DIM LO and INV. If mid and dim look alike, raise mid (1 frame in 4) for these keys, or make the boundary blink slowly instead.

- [ ] **HOME / SAVE / GLOBAL / OSC FINE, 2026-10-17 (synth_oscfine1.fwsc, installed, sha256 2ef24d0c236e9ebd...).** HOME: LAYER (knobs VELOCITY, MOD WHEEL, CC1, CC2; follows HOME+OCT) -> ZONES (knobs SPLIT, FADE, VEL SPLIT, VEL FADE) -> PERF (browser, PERF MODE, dolmen: pillar width = how much each part holds) -> GLOBAL (knobs TUNE, CURVE, CLOCK, TEMPO). SAVE: opens on what the page saves at its slot; empty slot saves at once, used asks; bank stones + moon; SAVED returns to the page. SAVE held + OCT- copy / OCT+ paste; holding HOME or SAVE shows the strip. Check: (1) MASTER TUNE against a tuner (+-50 cents) and it survives power-off with VEL CURVE / CLOCK OUT / THEME; (2) CLOCK OUT USB: a DAW follows tempo, START / STOP with PLAY; (3) VEL CURVE on a MIDI keyboard; (4) CC1/CC2 knobs move a CUSTOMCC mod destination; (5) OSC FINE TUNE reaches +-700 cents, fine near centre; SINGLE knob 3 = SCAN AMT. PERF: 42 of 44 (SEQ+ARP STRIKE 69.9, STRK FILT 70.2, SPRD CEL 77.3 as before). Web emulator: no hold, its HOME/GLOBAL knob labels are stale.
- [ ] **MOD slot a*b with one source, 2026-10-17 (synth_modpass1.fwsc, installed, sha256 6e06b147fe2f4462...).** A missing source in a*b counts as 1: SOURCE A alone (B none) passes A, B alone passes B, both none = 0. Other functions unchanged. Check: MOD1 = LFO1, a*b, none, AMOUNT up; F1 CUTOFF MOD SRC = MODSLOT1: the filter moves (before, silence).
- [ ] **HOME follows the layer focus, 2026-10-16 (synth_homelayer1.fwsc, installed, sha256 471ca58b938e42e8...).** One HOME page: View 1 with layer 1 in focus, View 2 (L2 rows, PERF MODE on top) with layer 2. HOME + OCT on HOME swaps them in place, cursor on the same row. DUAL / SPLIT: HOME -> ZONES -> GLOBAL -> HOME; SINGL: HOME -> GLOBAL. Header tabs: HOME ZONES GLOBAL. Idle after install: load 15 %, 0 late halves.
- [ ] **GLOBAL moved to HOME, 2026-10-16 (synth_globalhome1.fwsc, installed, sha256 87104dc0d3cffa2e...).** GLO now cycles AMP -> EFFECTS -> AMP. HOME cycles View 1 -> 2 -> 3 -> GLOBAL -> 1 in DUAL / SPLIT and View 1 -> GLOBAL -> 1 in SINGL. On GLOBAL the HOME LED lights (not GLO), the header tabs read HOME1 HOME2 ZONES GLOBAL, knob 4 is still INT TEMPO, and the Preset Encoder steps performances. Idle after install: load 16 %, 0 late halves.
- [ ] **Panel changes 2026-10-15 (synth_ui2.fwsc).** (1) Only the open page's button lights (the glow on the others is off). (2) SAVE flashes at 2 Hz on the save page while an ACTION is armed (SAVE by default: one press asks to save) and under the confirm popup. (3) HOME knob 2 (PITCH BEND) springs back: 150 ms after its last detent it glides to centre in ~0.2 s. (4) The Select Encoder opens a granite slab with the page's parameters in double-size runes for 1.5 s (the Algorithm Encoder keeps it up). (5) The popups' stone is orange (red on the red palette), no grey. Check each on the panel; for (4) also the UI frame time when it opens over the SEQ page (216 x 136 slab, flecks hashed per pixel).

- [ ] **Layer palettes and HOME+OCT layer switch (2026-10-15).** The first install coloured pages by page (HOME 1/2/3 got three colours), which the user found confusing. Fixed and installed 2026-10-16: every page now takes the focused layer's palette, OAK (L1, wood) or GRANITE (L2, stone), so HOME + OCT recolours the whole screen. The synth boots in SINGL, where there is no L2: pick DUAL or SPLIT first. The header shows a tree-ring sign (L1) or a menhir (L2) where "L1"/"L2" used to be. Check on the panel:
  1. Granite still reads as live, not as greyed-out rows. If it looks disabled, tint it towards slate (the blue-grey from build/palette-preview/stone-wood).
  2. Oak's khaki hasn't gone muddy.
  3. The ring and menhir signs are clear at 13 px.

  For the switch: tap HOME (it changes page on release), then hold HOME and press OCT- / OCT+. The layer should change, the page you were editing should stay, and the octave shouldn't move. After 200 ms of holding HOME, the focused layer's OCT LED should be lit and the other flashing at 4 Hz; in SINGL only OCT- lights. PRESETS shouldn't load while HOME is held. The OCT-+OCT+ 5 s update countdown should still work.

## First boot on X0X's platform (src/hardware/synth_main_fm1.c, 2026-10-14)

- [ ] **It boots to HOME and the way back works.** Before anything else, check that the web installer reaches it, and that installing X0X or the stock firmware over it works. Safe mode cannot be triggered by power-cycling: the boot guard lives in `.noinit` RAM, which survives a crash or watchdog reset but not a power-off. Only two crashes or hangs within 30 s of starting bring up SYNTH SAFE MODE. To see it on purpose, a test build that faults at startup is needed. Never hold OCT- + OCT+ for 5 s: that enters the chip's update mode (UBOOT), which only X0X's Mac rescue script can reach.
- [ ] **DSP at -O2 (2026-10-15).** src/hardware/build_fm1.py compiles src/dsp as its own -O2 unit (X0X's DSP setting) and the platform, screens and glue at -Os. The host benchmarks above were -Os, so the device may do slightly better than they predict. Compare the PERF TEST with the host table from src/host/perf_test_host.c.
- [ ] **Overload shedding (2026-10-15, synth_ui2.fwsc).** The glue now sheds: a half over 88 % of its time (or late) takes one voice off each layer's limit (the voice left out fades in a few ms), at most one every 4 halves; a calm second (every half under 65 %) gives one back. Off during the PERF TEST. Reported buzz "with reverb": the PERF TEST's worst realistic row SEQ+ARP SPRD CEL is 82.1 % avg / 84.7 % peak, no late halves, just under the trigger. Check: play SPREAD heavy chords + CELESTIAL + SEQ/ARP and listen for buzz (should be gone) and for voices dropping (the price). If voices drop audibly in normal playing, lower SPREAD's cap to 6 instead.
- [ ] **Overload.** Nothing outside the PERF TEST cuts the render when it runs past its deadline. (Superseded by the shedding above; kept for the history.) A patch or sequence that keeps the audio over 100 % starves the main loop, and after 8 s the watchdog restarts the FM1 (after 30 s from boot it does not count towards safe mode). If the PERF TEST shows any realistic scenario near 100 %, add voice shedding (Felucca sheds above 85 % of the deadline, audio.c `SHED_TICKS`) before playing it live.
- [ ] **64-bit divides in the screens.** The filter-curve plot divides 64-bit values (ui_page_draw.c `pu_ladder`), which the FM1 does in software (src/hardware/fm1_divdi.c, shift-and-subtract, 64 steps). Check the PERF TEST's slowest UI frame on a page with a filter plot.
- [ ] **Output level.** Q15 full scale at full MASTER reaches 2^22 (X0X's NORMAL level, which is Felucca's). Check it against X0X and the stock firmware at the same MASTER position: not quieter, and no clipping.
- [ ] **Latency.** X0X's 512-frame half buffer (11.6 ms) plus keys and MIDI landing only at 256-frame block starts. Play fast notes and check that it doesn't feel late or uneven. If it does, drain MIDI per 128-frame pass, or go back to 256-frame halves.
- [ ] **Preset Encoder press.** The HAL reports 14 buttons and no encoder push, so spec 5.2's press-to-switch-layer is HOME held + Preset Encoder turned for now. Check whether the encoder has a switch at all (a matrix position the calibration never asks for).
- [ ] **Lowest key = F3 = MIDI 53 at octave 0.** Check that against the stock firmware's notes over USB MIDI.
- [ ] **Key LEDs.** X0X drives two LED levels (lit, dim); ui_sm_key_leds' "mid" shows as lit. Check the SEQ playhead over its loop window and the ARP record cursor still read clearly; if not, port Felucca's fm1_led_mid.

- [ ] **32-slot banks, whole performances (2026-10-17, synth_bank32_1.fwsc, installed).** Sequences, arps and performances are 32 slots each, and a performance is a full snapshot (both patches, sequence, arp) kept in flash only and read when it loads (preset_store_fm1.c, main loop, outside syn_lock). Already checked over USB (ED_DEBUG perf loads 0 / 1 / 20 / 31): each brings its own effects, tempo and both layers' architectures; slot 32 is refused; no late halves. Still by hand: (1) SAVE a performance after changing the patch, the sequence and the arp, power off and on, turn the Preset Encoder away and back: everything returns. (2) Turning the Preset Encoder on HOME through the performances: no audible glitch while each one is read from flash (about 3.5 KB a load in 256-byte IRQ-off windows). (3) PLAY on an ARP-led factory performance (e.g. 1 CARNAC DAWN): the chords play and the bass follows each chord's root (TRANSPOSE MAQ). On a SEQ-led one (e.g. 20 CAIRN BASS) the sequence plays and the arp stays silent. Host listening renders: listening/factory_perfs/. Since synth_arpplay1 (below): PLAY on HOME starts both; on the ARP page only the arp.
- [ ] **ARP / SEQ transports, RATE, HOLD, Phrygian (2026-10-17, synth_arpplay1.fwsc, installed, sha256 55a41c831a9b03e1...).** PLAY on SEQ starts / stops the MAQ only, on ARP the arp only, on any other page both (stops whichever plays; starts both when both are stopped); the PLAY LED shows what PLAY would stop. One started while the other plays waits for the other's next bar (96 pulses from its first step): check by ear that they land together at a few tempos, and with CLOCK SOURCE MIDI that a DAW's Start starts both and Stop stops both. ARP knob 1 = each step's RATE (2 .. 1/32); GATE moved to the ARP menu (where TIME DIVISION was). STYLE HOLD: the chord struck once a step and held, a repeated chord not struck again; RHYTHM reads STRUM (NONE, UP 1..12, DN 1..11). Default GENRE = Phrygian (new, after Modern_2). AMP / EFFECTS row TAPE DEGRADE now reads DEGRADE. Formats: FREC_VER_ARP 4, FREC_VER_PERF 7; v3 arps and v6 performances migrate (step rates = the old TIME DIVISION, the pattern's GATE = its first chord step's gate; test_preset_store checks it on a simulated NOR). Check: (1) arps and performances saved under synth_bank32_1 still load and play at the same speed after a power cycle; save one, power cycle, it returns. (2) An ARP-led factory performance from HOME: chords and bass together. (3) Tablets: style + rate on line 2, the octave range as a pebble cairn by the step number. Not yet run: PERF TEST (the arp now starts with the MAQ in the SEQ rows, as before).

## Web installer (web/site, 2026-10-21)

- [ ] **One real install from the web page.** Host-tested only (web/site/test_installer.py: X0X's simulated FM1 in headless Edge). Serve build/site locally (`python -m http.server -d build/site`, open http://127.0.0.1:8000/install/ in Chrome or Edge) and install the current release over a Carnac 0.9.x: the page should name the version on the FM1 before writing, the progress bar should reach 100 and the last line should be "Done. The FM1 runs Carnac OS <version>". Do it once from stock V15 as well.
- [ ] **Editor INFO answers right after the splash.** The page reads the version up to 6 times, 1.5 s apart, after the FM1 comes back. If it reports "did not report its version" on a good install, lengthen that wait.
- [ ] **Safe mode after two failed starts.** The installer page tells people that two failed starts bring up safe mode with USB working (X0X's bootguard, kept in synth_main_fm1.c). Never checked on Carnac. Also check what safe mode answers the handshake with; the page treats it as "other firmware".

## Calibration (this unit)

- [x] **X0X PERF TEST, one core (2026-10-15).** Factory loop 43.5 % (peak 52 %), all five 83.2 % (peak 100 %), worst case 113 % (peak 135 %). The reference FM1 read 43 / 83 / 115 %, so this unit runs at the same speed and the host-to-device scale from X0X (100 % ≈ 265–300 ns/sample on this PC) holds. Peaks sit about 1.2x above the average: judge the synth by its peak, not only its average.

## Other open estimates (from earlier work)

- [ ] **Boot CRC** of the 229 KB wavetable image through XIP: estimated about 10 ms, never measured.
- [ ] **Block-major render overhead** (src/dsp/synth_runtime.c `render_pass`, 2026-10-14). On the host it reads about 3–5 % slower than the old frame-major loop (SINGLE x 8: ~207 -> ~219 ns/sample, clock() resolution so noisy): a `midi_clock_frame_changes` check per frame and the per-frame sums going through a 4 KB bus. Read `cpu_pct` on one core; if it costs more than that, the bus can drop to int32 (the sums fit) and the check can fold into `midi_clock_frame`.
- [ ] **Battery moon** (src/hardware/synth_main_fm1.c `carnac_battery_moon`, 2026-10-21, installed as synth_battmoon1). Empty is put at ADC 501 by extending the stock thresholds (531 / 561 / 591 = 1/3, 2/3, FULL) one more third down: unverified. On battery, note the moon's phase from full charge until the FM1 shuts down. If it switches off with the moon still showing a crescent, raise the floor. If it sits dark for a long time first, lower it. Also check: does the moon flicker between two phases (the hysteresis is a third of a step, ~1 ADC count)? Is it full with stars on a computer's USB? What does it show on a USB charger with no host (expected: a high reading, the cell charging)?
- [ ] **Factory bank RITES OF STONE** (src/hardware/factory_presets.c, 2026-10-21, installed as synth_rites3). Measured on the device, each performance loaded with ED_DEBUG and played as an 8-note chord twice (each patch's voice cap applies): every peak at or under 75 % (ANCESTOR PULSE 75, DAWN PROCESSION / SKARA BRAE FIRE / CURSUS WIND 74), no late halves, nothing shed. NEWGRANGE DAWN (the power-on performance, STACK x 6 with PCM CHOIR and CELESTIAL) held: 64 % load, 69 % peak, after its noise was taken out (with noise level 8: 71 / 77). Not measured: the ARP-led pads with PLAY running (SEQ+ARP adds about 9 points). Levels balanced on the host only (a held chord within a few dB). Still to do by ear: every patch's MOD WHEEL job, the patches' levels against each other, the DUAL (SKY BURIAL) and SPLIT (MENEC ALIGNMENT) performances on the panel keys. This unit has its own saves in performance slots 0 and 30, which shadow NEWGRANGE DAWN and SKY BURIAL in the PERF browser (power-on always plays the factory NEWGRANGE DAWN).
- [ ] **Dual core** (docs/dual-core-plan.md): X0X went from 115 % to 68 % with 2 cores. Measure only once the single-core numbers above are known. Device items for two cores: docs/dual-core-watchlist.md sections C and D (core-1 stack high-water, I-cache with mixed architectures, XIP contention, flash-write parking, load balance).

## PERF TEST runs (device)

Written by src/hardware/fm1_perf.py. Pass: avg <= 70 %, peak < 90 %, no late halves. One core, factory patch + the scenario's settings (src/hardware/perf_test.c).

### 2026-10-21 21:50, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 17.3 | 17.7 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 24.4 | 25.3 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 44.0 | 44.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 55.6 | 56.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 57.6 | 58.4 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD | 45.3 | 46.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 57.4 | 58.3 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 59.9 | 60.9 | 0 | 6 | 56.2 | 8 | pass |
| BINAURAL | 52.7 | 53.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 63.7 | 64.5 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 53.8 | 54.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 66.6 | 67.6 | 0 | 7 | 49.2 | 8 | pass |
| STACK HEAVY | 66.0 | 67.1 | 0 | 6 | 50.2 | 8 | pass |
| BINAURAL HEAVY | 67.8 | 68.9 | 0 | 6 | 44.2 | 9 | pass |
| SINGLE LADDER | 44.1 | 44.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 42.1 | 43.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE SH-2 | 49.1 | 49.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 42.4 | 43.2 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 57.4 | 58.2 | 0 | 7 | 57.2 | 7 | pass |
| SEQ+ARP HOME | 65.2 | 67.8 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 65.4 | 68.2 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 64.6 | 67.8 | 0 | 8 | 12.4 | 127 | pass |
| SEQ+ARP MOD1 | 65.2 | 69.1 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 44.0 | 44.7 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.6 | 59.6 | 0 | 8 | 57.5 | 8 | pass |
| HEAVY CELEST+PP | 60.4 | 61.3 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP CELEST+PP | 70.3 | 72.9 | 0 | 8 | 43.6 | 10 | FAIL |
| SINGLE MULTI H | 57.1 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC MULTI H | 64.8 | 65.7 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 55.1 | 56.0 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 70.3 | 71.4 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK MULTI H | 68.2 | 69.4 | 0 | 6 | 43.9 | 9 | pass |
| BINAUR MULTI H | 70.5 | 71.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 59.7 | 60.8 | 0 | 6 | 56.9 | 8 | pass |
| DUALOSC H CEL+PP | 69.5 | 70.6 | 0 | 5 | 43.2 | 9 | pass |
| SPREAD H TUM+PP | 57.5 | 58.6 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.9 | 84.9 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 59.4 | 60.3 | 0 | 8 | 56.9 | 8 | pass |
| HEAVY CEL+BBD PP | 60.4 | 61.5 | 0 | 8 | 55.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 69.0 | 72.0 | 0 | 8 | 44.6 | 10 | pass |
| STRIKE | 44.9 | 46.0 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 56.0 | 56.9 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 74.5 | 77.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 75.3 | 78.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 62.2 | 63.4 | 0 | 8 | 53.2 | 8 | pass |
| DUALOSC SH2 H | 68.6 | 69.6 | 0 | 5 | 45.6 | 9 | pass |
| SPREAD SH2 H | 58.9 | 59.8 | 0 | 6 | 57.2 | 8 | pass |
| DUALFLT SH2 H | 80.2 | 81.4 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 75.7 | 76.8 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 78.0 | 79.1 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 62.6 | 63.7 | 0 | 8 | 52.8 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.7 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT SH2 CEL | 85.0 | 86.2 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 76.5 | 79.5 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 80.2 | 81.4 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 78.0 | 79.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 48.4 | 49.5 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.9 | 56.8 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD PCM | 51.0 | 51.8 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 61.8 | 62.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 65.8 | 66.8 | 0 | 6 | 50.5 | 8 | pass |
| BINAURAL PCM | 60.4 | 61.4 | 0 | 6 | 54.6 | 8 | pass |
| DUALOSC MULTI H PCM | 59.5 | 60.6 | 0 | 5 | 56.6 | 8 | pass |
| DUALFLT MULTI H PCM | 74.9 | 76.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 78.1 | 79.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 44.1 | 45.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE RS PCM | 51.7 | 53.5 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 57.6 | 59.1 | 0 | 5 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.4 | 61.0 | 0 | 5 | 56.5 | 8 | pass |
| SEQ+ARP HOME PCM | 73.6 | 77.2 | 0 | 8 | 43.2 | 2 | FAIL |

### 2026-10-21 21:03, CARNAC OS 0.9.2 (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.7 | 17.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 23.7 | 24.7 | 0 | 1 | 60.6 | 3 | pass |
| SINGLE | 43.3 | 44.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 55.0 | 55.7 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 56.8 | 57.8 | 0 | 5 | 57.5 | 7 | pass |
| SPREAD | 44.7 | 45.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 56.4 | 57.3 | 0 | 7 | 57.5 | 1 | pass |
| STACK | 59.2 | 60.2 | 0 | 6 | 57.2 | 8 | pass |
| BINAURAL | 52.3 | 53.3 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.3 | 64.3 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 53.1 | 54.0 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 65.7 | 66.6 | 0 | 7 | 50.5 | 8 | pass |
| STACK HEAVY | 65.4 | 66.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 67.3 | 68.4 | 0 | 6 | 48.2 | 9 | pass |
| SINGLE LADDER | 43.3 | 44.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 41.4 | 42.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 48.4 | 49.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 41.6 | 42.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 56.4 | 57.2 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 64.7 | 67.4 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 64.8 | 67.8 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 64.1 | 67.4 | 0 | 8 | 13.5 | 127 | pass |
| SEQ+ARP MOD1 | 64.7 | 68.7 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 43.3 | 44.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.9 | 58.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 60.0 | 61.0 | 0 | 8 | 55.6 | 8 | pass |
| SEQ+ARP CELEST+PP | 70.0 | 73.3 | 0 | 8 | 43.6 | 10 | pass |
| SINGLE MULTI H | 56.3 | 57.2 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 64.1 | 65.0 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 54.3 | 55.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 69.5 | 70.4 | 0 | 7 | 44.2 | 9 | pass |
| STACK MULTI H | 67.4 | 68.8 | 0 | 6 | 46.6 | 9 | pass |
| BINAUR MULTI H | 69.9 | 71.1 | 0 | 6 | 42.9 | 1 | pass |
| SPREAD H CEL+PP | 59.1 | 60.4 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.8 | 70.3 | 0 | 5 | 44.2 | 9 | pass |
| SPREAD H TUM+PP | 56.8 | 57.7 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.4 | 84.7 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 58.7 | 59.7 | 0 | 8 | 57.5 | 8 | pass |
| HEAVY CEL+BBD PP | 59.9 | 60.9 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP SPR+BBD | 68.5 | 71.2 | 0 | 8 | 44.9 | 10 | pass |
| STRIKE | 44.1 | 45.0 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 55.4 | 56.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 74.9 | 78.7 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 75.8 | 78.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 61.5 | 62.6 | 0 | 8 | 55.2 | 8 | pass |
| DUALOSC SH2 H | 67.8 | 69.0 | 0 | 5 | 44.6 | 9 | pass |
| SPREAD SH2 H | 58.1 | 59.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT SH2 H | 79.3 | 80.3 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 74.9 | 76.3 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 77.4 | 78.6 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 62.0 | 63.1 | 0 | 8 | 53.8 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.0 | 64.4 | 0 | 6 | 52.2 | 8 | pass |
| DUALFLT SH2 CEL | 84.1 | 85.4 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 76.4 | 79.9 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 79.3 | 80.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 77.4 | 78.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 47.7 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.3 | 56.4 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD PCM | 50.2 | 51.2 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 60.8 | 61.8 | 0 | 7 | 54.5 | 8 | pass |
| STACK PCM | 65.2 | 66.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 59.3 | 60.3 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC MULTI H PCM | 58.8 | 60.0 | 0 | 5 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 74.0 | 75.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 77.0 | 78.1 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 43.4 | 45.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE RS PCM | 51.0 | 53.3 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 56.9 | 58.8 | 0 | 5 | 57.5 | 1 | pass |
| DUALOSC RS PCM | 58.9 | 61.0 | 0 | 5 | 57.2 | 8 | pass |
| SEQ+ARP HOME PCM | 73.3 | 76.4 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-21 20:51, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.7 | 17.0 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.7 | 24.4 | 0 | 1 | 60.3 | 3 | pass |
| SINGLE | 43.3 | 44.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 55.0 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 56.8 | 57.7 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD | 44.6 | 45.5 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 56.4 | 57.2 | 0 | 7 | 57.5 | 1 | pass |
| STACK | 59.2 | 60.2 | 0 | 6 | 57.2 | 8 | pass |
| BINAURAL | 52.3 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 63.3 | 64.2 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 53.1 | 54.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 65.7 | 66.6 | 0 | 7 | 50.2 | 8 | pass |
| STACK HEAVY | 65.4 | 66.5 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 67.4 | 68.5 | 0 | 6 | 46.6 | 9 | pass |
| SINGLE LADDER | 43.3 | 44.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 41.4 | 42.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 48.4 | 49.3 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 41.6 | 42.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 56.4 | 57.3 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 64.7 | 67.4 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 64.8 | 67.6 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 64.1 | 67.4 | 0 | 8 | 13.2 | 127 | pass |
| SEQ+ARP MOD1 | 64.8 | 68.9 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 43.3 | 44.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.9 | 58.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 60.0 | 61.0 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP CELEST+PP | 70.0 | 73.3 | 0 | 8 | 43.2 | 10 | pass |
| SINGLE MULTI H | 56.3 | 57.2 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 64.1 | 65.1 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 54.3 | 55.3 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 69.5 | 70.4 | 0 | 7 | 43.9 | 9 | pass |
| STACK MULTI H | 67.5 | 68.6 | 0 | 6 | 46.2 | 9 | pass |
| BINAUR MULTI H | 69.9 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| SPREAD H CEL+PP | 59.1 | 60.2 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.9 | 70.2 | 0 | 5 | 43.6 | 9 | pass |
| SPREAD H TUM+PP | 56.8 | 57.8 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.4 | 85.2 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 58.7 | 59.7 | 0 | 8 | 57.5 | 8 | pass |
| HEAVY CEL+BBD PP | 59.9 | 60.9 | 0 | 8 | 56.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 68.6 | 71.9 | 0 | 8 | 46.9 | 10 | pass |
| STRIKE | 44.1 | 45.1 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 55.4 | 56.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 74.9 | 78.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 75.8 | 80.0 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 61.4 | 62.5 | 0 | 8 | 54.9 | 8 | pass |
| DUALOSC SH2 H | 67.8 | 69.1 | 0 | 5 | 44.2 | 1 | pass |
| SPREAD SH2 H | 58.1 | 59.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT SH2 H | 79.3 | 80.5 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.9 | 76.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 77.4 | 78.6 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 62.0 | 63.1 | 0 | 8 | 53.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.0 | 64.2 | 0 | 6 | 52.6 | 8 | pass |
| DUALFLT SH2 CEL | 84.1 | 85.5 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 76.4 | 79.4 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 79.3 | 80.5 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 77.4 | 78.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 47.7 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.3 | 56.4 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD PCM | 50.2 | 51.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 60.8 | 61.8 | 0 | 7 | 53.5 | 8 | pass |
| STACK PCM | 65.2 | 66.4 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 59.3 | 60.3 | 0 | 6 | 56.9 | 8 | pass |
| DUALOSC MULTI H PCM | 58.8 | 60.1 | 0 | 5 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 74.0 | 75.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 77.0 | 78.1 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 43.4 | 45.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.0 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC RS | 56.9 | 58.9 | 0 | 5 | 57.2 | 7 | pass |
| DUALOSC RS PCM | 58.9 | 60.9 | 0 | 5 | 57.2 | 8 | pass |
| SEQ+ARP HOME PCM | 73.4 | 76.8 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-21 20:43, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.7 | 17.1 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.8 | 24.3 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 43.5 | 44.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 55.2 | 56.4 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 57.0 | 57.8 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD | 44.8 | 45.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 56.7 | 57.5 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 59.1 | 60.2 | 0 | 6 | 57.6 | 8 | pass |
| BINAURAL | 52.2 | 53.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.3 | 64.3 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD HEAVY | 55.0 | 55.9 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 72.1 | 73.3 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK HEAVY | 64.9 | 65.9 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 67.4 | 68.5 | 0 | 6 | 46.2 | 9 | pass |
| SINGLE LADDER | 43.6 | 44.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 41.6 | 42.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 48.4 | 49.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 42.0 | 42.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 56.7 | 57.6 | 0 | 7 | 57.5 | 7 | pass |
| SEQ+ARP HOME | 65.8 | 68.8 | 0 | 8 | 50.9 | 10 | pass |
| SEQ+ARP AMP | 65.8 | 69.1 | 0 | 8 | 49.9 | 12 | pass |
| SEQ+ARP SEQ | 65.0 | 68.4 | 0 | 8 | 12.4 | 127 | pass |
| SEQ+ARP MOD1 | 65.6 | 69.7 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 43.5 | 44.2 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.3 | 59.4 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 60.3 | 61.2 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP CELEST+PP | 71.2 | 74.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE MULTI H | 56.5 | 57.6 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC MULTI H | 64.3 | 65.2 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 54.4 | 55.5 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 70.0 | 71.2 | 0 | 7 | 42.9 | 1 | pass |
| STACK MULTI H | 67.4 | 68.5 | 0 | 6 | 46.9 | 9 | pass |
| BINAUR MULTI H | 70.1 | 71.1 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 59.2 | 60.4 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 69.0 | 70.6 | 0 | 5 | 44.5 | 9 | pass |
| SPREAD H TUM+PP | 56.9 | 57.9 | 0 | 6 | 57.3 | 7 | pass |
| SEQ+ARP SPRD CEL | 82.3 | 85.7 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 58.9 | 59.8 | 0 | 8 | 57.2 | 8 | pass |
| HEAVY CEL+BBD PP | 60.3 | 61.4 | 0 | 8 | 55.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 69.4 | 72.6 | 0 | 8 | 44.2 | 10 | pass |
| STRIKE | 44.3 | 45.0 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 55.7 | 56.6 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 74.6 | 78.0 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 74.9 | 78.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 61.5 | 62.6 | 0 | 8 | 55.2 | 8 | pass |
| DUALOSC SH2 H | 67.8 | 68.9 | 0 | 5 | 44.2 | 1 | pass |
| SPREAD SH2 H | 58.1 | 59.3 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT SH2 H | 80.5 | 81.7 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 74.6 | 75.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 77.3 | 78.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.9 | 63.0 | 0 | 8 | 53.3 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.3 | 64.4 | 0 | 6 | 52.2 | 8 | pass |
| DUALFLT SH2 CEL | 85.5 | 86.9 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 76.7 | 80.0 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 80.5 | 81.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 77.3 | 78.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 47.8 | 48.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 55.4 | 56.4 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD PCM | 60.0 | 60.9 | 0 | 6 | 56.2 | 8 | pass |
| DUAL FILTER PCM | 60.6 | 61.7 | 0 | 7 | 54.2 | 8 | pass |
| STACK PCM | 65.0 | 66.0 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 58.2 | 59.1 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC MULTI H PCM | 59.1 | 60.0 | 0 | 5 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 73.5 | 74.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 76.2 | 77.3 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 43.5 | 45.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.1 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 57.0 | 58.6 | 0 | 5 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.0 | 60.6 | 0 | 5 | 57.2 | 8 | pass |
| SEQ+ARP HOME PCM | 73.3 | 76.3 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-21 19:57, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 23.9 | 24.7 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 43.7 | 44.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 55.0 | 55.8 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 57.2 | 58.0 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD | 44.8 | 45.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 56.8 | 57.6 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 59.4 | 60.4 | 0 | 6 | 57.5 | 8 | pass |
| BINAURAL | 52.7 | 53.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.2 | 64.1 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD HEAVY | 53.0 | 53.8 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 65.9 | 66.9 | 0 | 7 | 50.9 | 8 | pass |
| STACK HEAVY | 65.1 | 66.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 67.7 | 68.9 | 0 | 6 | 44.6 | 9 | pass |
| SINGLE LADDER | 43.7 | 44.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 41.7 | 42.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 49.1 | 49.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 41.9 | 42.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 56.8 | 57.7 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 64.8 | 67.8 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 64.9 | 67.8 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 64.1 | 67.7 | 0 | 8 | 12.8 | 127 | pass |
| SEQ+ARP MOD1 | 64.8 | 69.4 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 43.6 | 44.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.1 | 59.1 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 59.9 | 60.9 | 0 | 8 | 55.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 69.9 | 73.8 | 0 | 8 | 44.2 | 10 | pass |
| SINGLE MULTI H | 56.7 | 57.7 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC MULTI H | 64.4 | 65.4 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 54.4 | 55.4 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 69.3 | 70.4 | 0 | 7 | 43.2 | 9 | pass |
| STACK MULTI H | 67.6 | 68.9 | 0 | 6 | 45.6 | 9 | pass |
| BINAUR MULTI H | 70.4 | 71.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 59.2 | 60.2 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 69.2 | 70.2 | 0 | 5 | 44.5 | 9 | pass |
| SPREAD H TUM+PP | 57.1 | 58.0 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.9 | 85.7 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 58.6 | 59.4 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 59.6 | 60.6 | 0 | 8 | 56.6 | 8 | pass |
| SEQ+ARP SPR+BBD | 68.1 | 71.4 | 0 | 8 | 44.9 | 10 | pass |
| STRIKE | 44.6 | 45.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 55.7 | 56.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 75.4 | 78.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 75.3 | 78.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 62.0 | 63.0 | 0 | 8 | 53.5 | 8 | pass |
| DUALOSC SH2 H | 68.4 | 69.3 | 0 | 5 | 43.2 | 9 | pass |
| SPREAD SH2 H | 58.5 | 59.4 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT SH2 H | 79.8 | 80.9 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 75.4 | 76.6 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 78.6 | 79.8 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 62.8 | 63.9 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.9 | 65.0 | 0 | 6 | 52.2 | 8 | pass |
| DUALFLT SH2 CEL | 85.1 | 86.4 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 78.2 | 81.9 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 79.8 | 81.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 78.6 | 79.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 47.8 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 54.3 | 55.3 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD PCM | 40.6 | 41.5 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 61.2 | 62.1 | 0 | 7 | 55.2 | 8 | pass |
| STACK PCM | 65.5 | 66.8 | 0 | 6 | 51.6 | 8 | pass |
| BINAURAL PCM | 60.6 | 61.5 | 0 | 6 | 55.2 | 8 | pass |
| DUALOSC MULTI H PCM | 58.7 | 59.9 | 0 | 5 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 73.8 | 74.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 78.2 | 79.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 43.7 | 45.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.0 | 53.2 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 57.3 | 58.8 | 0 | 5 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 58.2 | 60.2 | 0 | 5 | 57.6 | 1 | pass |
| SEQ+ARP HOME PCM | 72.5 | 75.7 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-21 19:47, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 17.3 | 17.6 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 24.5 | 25.1 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 44.3 | 44.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 55.5 | 56.2 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 62.2 | 63.0 | 0 | 5 | 52.5 | 8 | pass |
| SPREAD | 45.4 | 46.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 57.3 | 58.0 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 63.8 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL | 52.9 | 53.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 64.3 | 65.2 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 53.5 | 54.3 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 66.2 | 67.2 | 0 | 7 | 48.9 | 8 | pass |
| STACK HEAVY | 65.8 | 66.7 | 0 | 6 | 51.5 | 8 | pass |
| BINAURAL HEAVY | 67.7 | 68.6 | 0 | 6 | 44.9 | 9 | pass |
| SINGLE LADDER | 44.3 | 44.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 42.2 | 42.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 49.3 | 50.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 42.4 | 43.1 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 57.2 | 58.0 | 0 | 7 | 57.5 | 7 | pass |
| SEQ+ARP HOME | 65.0 | 67.9 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 65.1 | 68.1 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 64.4 | 67.3 | 0 | 8 | 12.8 | 127 | pass |
| SEQ+ARP MOD1 | 65.1 | 69.0 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 44.3 | 44.9 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.6 | 59.5 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 60.4 | 61.4 | 0 | 8 | 56.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 70.1 | 73.7 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE MULTI H | 57.2 | 58.0 | 0 | 8 | 57.3 | 7 | pass |
| DUALOSC MULTI H | 65.8 | 66.6 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 54.9 | 55.7 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 69.8 | 70.7 | 0 | 7 | 42.9 | 1 | pass |
| STACK MULTI H | 68.3 | 69.3 | 0 | 6 | 43.9 | 9 | pass |
| BINAUR MULTI H | 70.5 | 71.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 59.8 | 60.8 | 0 | 6 | 56.6 | 8 | pass |
| DUALOSC H CEL+PP | 70.6 | 71.8 | 0 | 5 | 42.9 | 1 | FAIL |
| SPREAD H TUM+PP | 57.6 | 58.6 | 0 | 6 | 57.3 | 7 | pass |
| SEQ+ARP SPRD CEL | 82.1 | 85.9 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 59.0 | 59.8 | 0 | 8 | 57.2 | 8 | pass |
| HEAVY CEL+BBD PP | 60.3 | 61.3 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP SPR+BBD | 68.6 | 71.6 | 0 | 8 | 44.9 | 10 | pass |
| STRIKE | 45.1 | 45.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 55.9 | 56.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 74.5 | 78.1 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 74.7 | 78.0 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 62.2 | 63.3 | 0 | 8 | 52.8 | 8 | pass |
| DUALOSC SH2 H | 69.4 | 70.2 | 0 | 5 | 43.6 | 9 | pass |
| SPREAD SH2 H | 58.7 | 59.6 | 0 | 6 | 57.2 | 8 | pass |
| DUALFLT SH2 H | 80.0 | 81.1 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 75.9 | 76.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 78.2 | 79.2 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 62.9 | 63.8 | 0 | 8 | 51.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 64.0 | 65.0 | 0 | 6 | 52.2 | 8 | pass |
| DUALFLT SH2 CEL | 85.1 | 86.2 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 76.8 | 79.6 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 79.9 | 80.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 78.2 | 79.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 50.5 | 51.5 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 73.1 | 74.2 | 0 | 5 | 43.2 | 1 | FAIL |
| SPREAD PCM | 42.7 | 43.4 | 0 | 6 | 57.3 | 1 | pass |
| DUAL FILTER PCM | 63.5 | 64.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 72.1 | 73.4 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAURAL PCM | 62.6 | 63.5 | 0 | 6 | 52.2 | 8 | pass |
| DUALOSC MULTI H PCM | 80.9 | 81.9 | 0 | 5 | 42.9 | 1 | FAIL |
| DUALFLT MULTI H PCM | 76.1 | 77.2 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR MULTI H PCM | 80.3 | 81.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 44.3 | 45.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE RS PCM | 51.7 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC RS | 62.2 | 63.7 | 0 | 5 | 52.9 | 8 | pass |
| DUALOSC RS PCM | 76.0 | 77.1 | 0 | 5 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PCM | 72.5 | 75.9 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-21 17:37, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.7 | 17.1 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.7 | 24.3 | 0 | 1 | 60.9 | 3 | pass |
| SINGLE | 43.6 | 44.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 55.0 | 55.8 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 57.0 | 57.8 | 0 | 5 | 57.5 | 7 | pass |
| SPREAD | 44.9 | 45.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 56.5 | 57.2 | 0 | 7 | 57.5 | 1 | pass |
| STACK | 58.5 | 59.3 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 51.8 | 52.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 62.8 | 63.6 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD HEAVY | 53.1 | 53.9 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 65.8 | 66.8 | 0 | 7 | 51.5 | 8 | pass |
| STACK HEAVY | 64.0 | 64.9 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 66.9 | 67.9 | 0 | 6 | 49.9 | 8 | pass |
| SINGLE LADDER | 43.7 | 44.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 41.7 | 42.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 48.7 | 49.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 41.9 | 42.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 56.5 | 57.2 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 65.0 | 68.3 | 0 | 8 | 51.5 | 10 | pass |
| SEQ+ARP AMP | 65.1 | 68.1 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 64.5 | 67.7 | 0 | 8 | 12.1 | 127 | pass |
| SEQ+ARP MOD1 | 65.0 | 69.0 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 43.7 | 44.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.2 | 59.1 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 59.8 | 60.8 | 0 | 8 | 55.5 | 8 | pass |
| SEQ+ARP CELEST+PP | 70.1 | 73.6 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE MULTI H | 56.6 | 57.4 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC MULTI H | 64.2 | 65.0 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 54.4 | 55.3 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 69.3 | 70.4 | 0 | 7 | 43.9 | 9 | pass |
| STACK MULTI H | 66.9 | 67.9 | 0 | 6 | 51.2 | 8 | pass |
| BINAUR MULTI H | 69.5 | 70.6 | 0 | 6 | 43.2 | 9 | pass |
| SPREAD H CEL+PP | 59.3 | 60.4 | 0 | 6 | 56.9 | 8 | pass |
| DUALOSC H CEL+PP | 68.9 | 70.1 | 0 | 5 | 44.6 | 9 | pass |
| SPREAD H TUM+PP | 57.3 | 58.3 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.9 | 85.4 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 58.7 | 59.6 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 59.7 | 60.6 | 0 | 8 | 56.5 | 8 | pass |
| SEQ+ARP SPR+BBD | 68.6 | 72.0 | 0 | 8 | 44.6 | 10 | pass |
| STRIKE | 44.3 | 45.1 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 55.2 | 56.0 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 74.7 | 78.0 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 73.4 | 77.2 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 61.7 | 62.5 | 0 | 8 | 54.9 | 8 | pass |
| DUALOSC SH2 H | 67.9 | 68.9 | 0 | 5 | 43.2 | 9 | pass |
| SPREAD SH2 H | 58.3 | 59.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT SH2 H | 79.4 | 80.4 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.1 | 75.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 77.0 | 77.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.9 | 62.9 | 0 | 8 | 54.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.3 | 64.4 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT SH2 CEL | 84.2 | 85.3 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 76.8 | 80.4 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 79.4 | 80.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 77.0 | 78.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 43.7 | 45.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS ATK | 45.4 | 47.5 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC RS | 57.1 | 58.6 | 0 | 5 | 57.5 | 7 | pass |
| DUALOSC RS ATK | 58.8 | 61.0 | 0 | 5 | 57.2 | 8 | pass |
| SPREAD RS | 45.0 | 46.5 | 0 | 6 | 57.2 | 1 | pass |
| SPREAD RS ATK | 46.7 | 48.7 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT RS | 56.5 | 58.3 | 0 | 7 | 57.2 | 1 | pass |
| DUALFLT RS ATK | 58.2 | 60.5 | 0 | 7 | 57.2 | 7 | pass |
| STACK RS | 58.6 | 60.1 | 0 | 6 | 57.5 | 7 | pass |
| STACK RS ATK | 60.2 | 62.5 | 0 | 6 | 55.5 | 8 | pass |
| BINAUR RS | 51.9 | 53.7 | 0 | 6 | 57.2 | 1 | pass |
| BINAUR RS ATK | 53.5 | 55.6 | 0 | 6 | 57.2 | 1 | pass |
| SPREAD SH2 RS | 58.3 | 59.9 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD SH2 RS ATK | 60.0 | 62.1 | 0 | 6 | 56.5 | 8 | pass |
| DUALFLT SH2 RS | 79.4 | 81.1 | 0 | 7 | 42.9 | 1 | FAIL |
| DUALFLT SH2 RS ATK | 81.1 | 82.4 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 RS | 74.1 | 75.2 | 0 | 6 | 43.2 | 1 | FAIL |
| STACK SH2 RS ATK | 75.4 | 76.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 RS | 77.0 | 79.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 RS ATK | 78.4 | 80.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ATK | 67.7 | 71.5 | 0 | 8 | 45.9 | 10 | pass |
| SEQ+ARP SPRD CEL ATK | 85.0 | 88.9 | 0 | 6 | 43.2 | 12 | FAIL |
| SEQ+ARP SH2 CEL ATK | 79.6 | 83.2 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP DUALFLT SH2 | 89.9 | 93.6 | 0 | 7 | 43.2 | 13 | FAIL |
| SEQ+ARP DFLT SH2 ATK | 92.6 | 97.2 | 0 | 7 | 38.4 | 35 | FAIL |
| SEQ+ARP STACK SH2 | 104.3 | 106.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STCK SH2 ATK | 107.0 | 109.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP BINAUR SH2 | 108.3 | 110.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP BIN SH2 ATK | 111.0 | 113.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRIKE CAP | 73.6 | 77.2 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-21 14:12, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 17.1 | 17.5 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.8 | 24.6 | 0 | 1 | 61.0 | 4 | pass |
| SINGLE | 42.9 | 43.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 54.3 | 55.1 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 56.3 | 57.0 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD | 44.3 | 45.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 55.4 | 56.1 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 57.9 | 58.8 | 0 | 6 | 57.2 | 7 | pass |
| BINAURAL | 51.0 | 51.8 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.1 | 62.9 | 0 | 5 | 52.5 | 8 | pass |
| SPREAD HEAVY | 52.6 | 53.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 64.8 | 65.7 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 64.6 | 65.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 67.5 | 68.4 | 0 | 6 | 45.6 | 9 | pass |
| SINGLE LADDER | 42.9 | 43.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 40.7 | 41.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 47.8 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 41.1 | 41.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 55.4 | 56.1 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 63.3 | 66.2 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP AMP | 63.6 | 66.4 | 0 | 8 | 52.1 | 20 | pass |
| SEQ+ARP SEQ | 62.9 | 65.8 | 0 | 8 | 14.9 | 127 | pass |
| SEQ+ARP MOD1 | 63.5 | 67.0 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 42.9 | 43.7 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.5 | 58.5 | 0 | 8 | 56.9 | 7 | pass |
| HEAVY CELEST+PP | 58.9 | 59.9 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 68.3 | 71.5 | 0 | 8 | 45.2 | 20 | pass |
| SINGLE MULTI H | 55.8 | 56.7 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC MULTI H | 63.4 | 64.4 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 53.9 | 54.7 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 68.1 | 69.1 | 0 | 7 | 42.9 | 9 | pass |
| STACK MULTI H | 66.2 | 67.2 | 0 | 6 | 50.6 | 9 | pass |
| BINAUR MULTI H | 68.8 | 69.8 | 0 | 6 | 44.9 | 9 | pass |
| SPREAD H CEL+PP | 58.6 | 59.5 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 67.7 | 68.7 | 0 | 5 | 44.5 | 9 | pass |
| SPREAD H TUM+PP | 56.8 | 57.6 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 80.7 | 83.8 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 57.8 | 58.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 59.1 | 60.1 | 0 | 8 | 56.9 | 8 | pass |
| SEQ+ARP SPR+BBD | 67.0 | 69.4 | 0 | 8 | 47.9 | 20 | pass |
| STRIKE | 43.6 | 44.4 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.8 | 55.6 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 73.3 | 76.4 | 0 | 8 | 42.9 | 13 | FAIL |
| SEQ+ARP STRK FILT | 74.5 | 78.2 | 0 | 8 | 42.9 | 13 | FAIL |
| SINGLE SH2 H | 60.8 | 61.7 | 0 | 8 | 54.8 | 8 | pass |
| DUALOSC SH2 H | 67.2 | 68.1 | 0 | 5 | 49.2 | 9 | pass |
| SPREAD SH2 H | 57.6 | 58.4 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT SH2 H | 78.1 | 79.0 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.8 | 74.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 76.4 | 77.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.2 | 62.1 | 0 | 8 | 53.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.4 | 63.5 | 0 | 6 | 52.9 | 8 | pass |
| DUALFLT SH2 CEL | 82.7 | 83.8 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.0 | 78.1 | 0 | 8 | 42.9 | 13 | FAIL |
| DUALFLT SH2 HP | 78.1 | 78.9 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 76.5 | 77.5 | 0 | 6 | 42.9 | 1 | FAIL |

### 2026-10-21 12:04, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.3 | 16.7 | 0 | 0 | 62.0 | 3 | pass |
| SINGLE 1V | 23.2 | 23.9 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 42.3 | 43.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.3 | 54.1 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 55.7 | 56.5 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 43.7 | 44.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 54.7 | 55.5 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 57.3 | 58.1 | 0 | 6 | 57.2 | 7 | pass |
| BINAURAL | 50.6 | 51.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.6 | 62.3 | 0 | 5 | 55.5 | 8 | pass |
| SPREAD HEAVY | 51.9 | 52.8 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 64.2 | 65.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.0 | 63.9 | 0 | 6 | 52.5 | 8 | pass |
| BINAURAL HEAVY | 65.9 | 66.8 | 0 | 6 | 51.5 | 8 | pass |
| SINGLE LADDER | 42.3 | 43.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 40.1 | 40.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 47.3 | 48.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 40.4 | 41.1 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 54.7 | 55.5 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 62.4 | 65.3 | 0 | 8 | 52.5 | 20 | pass |
| SEQ+ARP AMP | 62.5 | 65.5 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 62.2 | 65.2 | 0 | 8 | 21.3 | 56 | pass |
| SEQ+ARP MOD1 | 62.5 | 65.8 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 42.3 | 43.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.6 | 57.5 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 57.9 | 58.9 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 67.2 | 70.2 | 0 | 8 | 48.2 | 20 | pass |
| SINGLE MULTI H | 55.0 | 55.8 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 62.9 | 63.7 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 53.3 | 54.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 67.4 | 68.4 | 0 | 7 | 46.5 | 9 | pass |
| STACK MULTI H | 65.5 | 66.4 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 68.2 | 69.2 | 0 | 6 | 44.9 | 9 | pass |
| SPREAD H CEL+PP | 58.1 | 59.1 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 67.3 | 68.7 | 0 | 5 | 46.5 | 9 | pass |
| SPREAD H TUM+PP | 56.2 | 57.0 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.3 | 82.5 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 57.0 | 57.8 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CEL+BBD PP | 57.9 | 58.8 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 66.1 | 68.7 | 0 | 8 | 50.2 | 20 | pass |
| STRIKE | 43.0 | 43.9 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.2 | 55.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.8 | 75.4 | 0 | 8 | 42.9 | 12 | FAIL |
| SEQ+ARP STRK FILT | 71.9 | 75.4 | 0 | 8 | 42.9 | 12 | FAIL |
| SINGLE SH2 H | 60.0 | 60.8 | 0 | 8 | 55.9 | 8 | pass |
| DUALOSC SH2 H | 66.6 | 67.5 | 0 | 5 | 50.5 | 9 | pass |
| SPREAD SH2 H | 57.1 | 58.0 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT SH2 H | 77.5 | 78.6 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.0 | 74.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 75.8 | 76.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 60.5 | 61.5 | 0 | 8 | 55.5 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.0 | 63.0 | 0 | 6 | 53.2 | 8 | pass |
| DUALFLT SH2 CEL | 82.6 | 83.8 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.0 | 77.2 | 0 | 8 | 42.9 | 12 | FAIL |
| DUALFLT SH2 HP | 77.6 | 78.6 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 75.9 | 77.1 | 0 | 6 | 42.9 | 1 | FAIL |

### 2026-10-21 11:56, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.3 | 16.7 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 23.1 | 23.7 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 42.2 | 42.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 53.9 | 54.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 55.7 | 56.4 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 43.5 | 44.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 54.9 | 55.7 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 57.3 | 58.1 | 0 | 6 | 57.2 | 7 | pass |
| BINAURAL | 50.5 | 51.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.1 | 62.9 | 0 | 5 | 53.5 | 8 | pass |
| SPREAD HEAVY | 52.4 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 65.7 | 66.6 | 0 | 7 | 51.2 | 8 | pass |
| STACK HEAVY | 64.3 | 65.4 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 67.0 | 68.1 | 0 | 6 | 49.9 | 9 | pass |
| SINGLE LADDER | 42.2 | 42.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 40.1 | 40.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 47.6 | 48.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 41.2 | 41.8 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 54.9 | 55.6 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 63.0 | 65.6 | 0 | 8 | 52.5 | 20 | pass |
| SEQ+ARP AMP | 63.2 | 66.2 | 0 | 8 | 52.1 | 20 | pass |
| SEQ+ARP SEQ | 62.9 | 65.4 | 0 | 8 | 20.8 | 58 | pass |
| SEQ+ARP MOD1 | 63.2 | 66.6 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 42.2 | 42.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.1 | 57.8 | 0 | 8 | 56.9 | 7 | pass |
| HEAVY CELEST+PP | 58.3 | 59.1 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 67.6 | 70.8 | 0 | 8 | 45.5 | 20 | pass |
| SINGLE MULTI H | 54.9 | 55.7 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.8 | 63.6 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 53.1 | 53.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 67.5 | 68.5 | 0 | 7 | 45.6 | 9 | pass |
| STACK MULTI H | 65.6 | 66.5 | 0 | 6 | 51.6 | 8 | pass |
| BINAUR MULTI H | 68.3 | 69.4 | 0 | 6 | 44.6 | 9 | pass |
| SPREAD H CEL+PP | 57.5 | 58.4 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 67.2 | 68.1 | 0 | 5 | 49.5 | 9 | pass |
| SPREAD H TUM+PP | 55.6 | 56.4 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 78.7 | 82.3 | 0 | 6 | 43.2 | 12 | FAIL |
| HEAVY SPRING+PP | 57.6 | 58.3 | 0 | 8 | 56.9 | 7 | pass |
| HEAVY CEL+BBD PP | 58.5 | 59.3 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 66.8 | 69.3 | 0 | 8 | 49.2 | 20 | pass |
| STRIKE | 42.8 | 43.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.7 | 55.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.8 | 75.3 | 0 | 8 | 43.2 | 12 | FAIL |
| SEQ+ARP STRK FILT | 71.8 | 75.1 | 0 | 8 | 42.9 | 12 | FAIL |
| SINGLE SH2 H | 60.3 | 61.1 | 0 | 8 | 56.2 | 8 | pass |
| DUALOSC SH2 H | 66.8 | 67.6 | 0 | 5 | 50.2 | 9 | pass |
| SPREAD SH2 H | 57.2 | 58.0 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT SH2 H | 80.4 | 81.4 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 75.8 | 76.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 78.7 | 79.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 60.6 | 61.4 | 0 | 8 | 53.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 61.9 | 62.9 | 0 | 6 | 54.2 | 8 | pass |
| DUALFLT SH2 CEL | 85.3 | 86.6 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.5 | 78.3 | 0 | 8 | 42.9 | 12 | FAIL |
| DUALFLT SH2 HP | 80.4 | 81.5 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 78.7 | 79.9 | 0 | 6 | 42.9 | 1 | FAIL |

### 2026-10-21 11:47, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.3 | 16.6 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 23.0 | 23.6 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 41.9 | 42.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.7 | 54.5 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 55.3 | 56.1 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 43.4 | 44.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 54.3 | 55.2 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 56.7 | 57.5 | 0 | 6 | 57.2 | 7 | pass |
| BINAURAL | 50.1 | 50.9 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.7 | 62.5 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD HEAVY | 52.3 | 53.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 64.7 | 65.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 63.4 | 64.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 66.1 | 67.2 | 0 | 6 | 49.9 | 9 | pass |
| SINGLE LADDER | 41.9 | 42.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 40.0 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 47.8 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.9 | 41.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 54.3 | 55.2 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 62.9 | 65.8 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 63.1 | 66.1 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP SEQ | 62.9 | 65.4 | 0 | 8 | 20.7 | 58 | pass |
| SEQ+ARP MOD1 | 63.2 | 66.6 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 41.9 | 42.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.8 | 58.6 | 0 | 8 | 56.5 | 7 | pass |
| HEAVY CELEST+PP | 58.5 | 59.4 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 67.7 | 70.6 | 0 | 8 | 46.2 | 20 | pass |
| SINGLE MULTI H | 54.7 | 55.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 62.6 | 63.5 | 0 | 5 | 52.9 | 8 | pass |
| SPREAD MULTI H | 53.0 | 53.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 67.0 | 67.9 | 0 | 7 | 50.2 | 9 | pass |
| STACK MULTI H | 65.1 | 66.0 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.8 | 68.9 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD H CEL+PP | 57.8 | 58.8 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 67.1 | 68.1 | 0 | 5 | 48.6 | 9 | pass |
| SPREAD H TUM+PP | 55.8 | 56.7 | 0 | 6 | 57.3 | 1 | pass |
| SEQ+ARP SPRD CEL | 78.7 | 81.8 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 57.4 | 58.3 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 58.5 | 59.4 | 0 | 8 | 56.9 | 8 | pass |
| SEQ+ARP SPR+BBD | 66.9 | 69.7 | 0 | 8 | 47.9 | 20 | pass |
| STRIKE | 42.6 | 43.3 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.3 | 55.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.6 | 74.7 | 0 | 8 | 42.9 | 12 | FAIL |
| SEQ+ARP STRK FILT | 71.7 | 75.5 | 0 | 8 | 43.3 | 21 | FAIL |
| SINGLE SH2 H | 60.5 | 61.3 | 0 | 8 | 55.2 | 8 | pass |
| DUALOSC SH2 H | 66.9 | 67.7 | 0 | 5 | 50.6 | 9 | pass |
| SPREAD SH2 H | 57.4 | 58.3 | 0 | 6 | 57.2 | 8 | pass |
| DUALFLT SH2 H | 80.3 | 81.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.5 | 75.6 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 76.6 | 77.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.0 | 61.8 | 0 | 8 | 54.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.3 | 63.4 | 0 | 6 | 52.9 | 8 | pass |
| DUALFLT SH2 CEL | 85.0 | 86.2 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.2 | 78.6 | 0 | 8 | 42.9 | 13 | FAIL |
| DUALFLT SH2 HP | 81.8 | 82.8 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 77.5 | 78.5 | 0 | 6 | 42.9 | 1 | FAIL |

### 2026-10-21 11:34, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.3 | 16.7 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 23.2 | 23.9 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 42.3 | 43.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 53.9 | 54.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 55.7 | 56.4 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 43.6 | 44.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 54.7 | 55.5 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 57.2 | 58.1 | 0 | 6 | 57.2 | 7 | pass |
| BINAURAL | 50.5 | 51.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.0 | 63.0 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD HEAVY | 52.3 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 65.6 | 66.5 | 0 | 7 | 51.5 | 8 | pass |
| STACK HEAVY | 64.3 | 65.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 67.0 | 67.8 | 0 | 6 | 50.9 | 9 | pass |
| SINGLE LADDER | 42.3 | 43.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 40.3 | 40.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 47.7 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 41.3 | 42.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 54.7 | 55.5 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 62.9 | 65.7 | 0 | 8 | 52.5 | 20 | pass |
| SEQ+ARP AMP | 63.1 | 66.0 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP SEQ | 62.7 | 65.6 | 0 | 8 | 21.2 | 58 | pass |
| SEQ+ARP MOD1 | 63.2 | 66.5 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 42.3 | 43.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.2 | 58.0 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CELEST+PP | 58.5 | 59.4 | 0 | 8 | 57.6 | 8 | pass |
| SEQ+ARP CELEST+PP | 67.7 | 70.5 | 0 | 8 | 45.9 | 20 | pass |
| SINGLE MULTI H | 55.0 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.8 | 63.6 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 53.2 | 54.0 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 67.4 | 68.4 | 0 | 7 | 45.5 | 9 | pass |
| STACK MULTI H | 65.5 | 66.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 68.2 | 69.2 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD H CEL+PP | 57.8 | 58.9 | 0 | 6 | 57.6 | 7 | pass |
| DUALOSC H CEL+PP | 67.2 | 68.1 | 0 | 5 | 49.6 | 9 | pass |
| SPREAD H TUM+PP | 55.9 | 56.9 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 78.8 | 81.7 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 57.6 | 58.3 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 58.6 | 59.5 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 66.7 | 69.2 | 0 | 8 | 48.5 | 20 | pass |
| STRIKE | 42.9 | 43.7 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.8 | 55.6 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 72.1 | 74.9 | 0 | 8 | 42.9 | 12 | FAIL |
| SEQ+ARP STRK FILT | 72.2 | 75.6 | 0 | 8 | 42.9 | 13 | FAIL |
| SINGLE SH2 H | 60.4 | 61.2 | 0 | 8 | 55.9 | 8 | pass |
| DUALOSC SH2 H | 66.7 | 67.6 | 0 | 5 | 50.5 | 9 | pass |
| SPREAD SH2 H | 57.4 | 58.3 | 0 | 6 | 56.9 | 7 | pass |
| DUALFLT SH2 H | 78.5 | 79.6 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.8 | 74.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 76.6 | 77.6 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.0 | 62.0 | 0 | 8 | 54.8 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.2 | 63.3 | 0 | 6 | 53.2 | 8 | pass |
| DUALFLT SH2 CEL | 83.6 | 84.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.6 | 77.9 | 0 | 8 | 42.9 | 12 | FAIL |

### 2026-10-17 16:16, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.5 | 16.9 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 22.6 | 23.2 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 39.9 | 40.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.0 | 53.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 53.7 | 54.4 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.6 | 42.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.7 | 50.4 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.6 | 54.3 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.9 | 47.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.5 | 62.3 | 0 | 5 | 54.2 | 8 | pass |
| SPREAD HEAVY | 51.8 | 52.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.8 | 64.6 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 62.8 | 63.7 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 65.5 | 66.4 | 0 | 6 | 51.5 | 8 | pass |
| SINGLE LADDER | 41.4 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.1 | 41.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.1 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.2 | 40.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.2 | 54.0 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.6 | 64.2 | 0 | 8 | 52.9 | 20 | pass |
| SEQ+ARP AMP | 61.8 | 64.8 | 0 | 8 | 53.6 | 20 | pass |
| SEQ+ARP SEQ | 61.5 | 64.1 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.9 | 65.3 | 0 | 8 | 15.8 | 70 | pass |
| LADDER FILTER PG | 41.4 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.2 | 57.0 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 57.8 | 58.8 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.2 | 69.1 | 0 | 8 | 49.5 | 20 | pass |
| SINGLE MULTI H | 54.2 | 55.1 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.3 | 63.1 | 0 | 5 | 52.5 | 8 | pass |
| SPREAD MULTI H | 52.7 | 53.5 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 66.1 | 67.0 | 0 | 7 | 50.9 | 8 | pass |
| STACK MULTI H | 64.4 | 65.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.4 | 68.4 | 0 | 6 | 47.6 | 9 | pass |
| SPREAD H CEL+PP | 57.4 | 58.4 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 67.0 | 68.1 | 0 | 5 | 50.5 | 9 | pass |
| SPREAD H TUM+PP | 55.3 | 56.2 | 0 | 6 | 57.3 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.4 | 80.4 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.8 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 57.7 | 58.7 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 65.3 | 68.2 | 0 | 8 | 50.5 | 20 | pass |
| STRIKE | 40.3 | 41.1 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.9 | 54.6 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 70.0 | 73.0 | 0 | 8 | 43.5 | 13 | pass |
| SEQ+ARP STRK FILT | 70.0 | 73.3 | 0 | 8 | 43.5 | 20 | pass |

### 2026-10-17 10:54, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 17.2 | 17.6 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.4 | 24.0 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 40.8 | 41.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.5 | 54.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 54.6 | 55.4 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 42.4 | 43.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 50.4 | 51.2 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 54.2 | 55.1 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 47.6 | 48.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.0 | 62.9 | 0 | 5 | 53.8 | 8 | pass |
| SPREAD HEAVY | 52.4 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 64.2 | 65.1 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 63.2 | 64.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 66.1 | 67.1 | 0 | 6 | 51.6 | 9 | pass |
| SINGLE LADDER | 42.2 | 43.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 41.0 | 42.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 41.0 | 41.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.9 | 41.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.8 | 54.6 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 62.0 | 64.7 | 0 | 8 | 52.8 | 20 | pass |
| SEQ+ARP AMP | 62.2 | 65.0 | 0 | 8 | 52.5 | 20 | pass |
| SEQ+ARP SEQ | 61.9 | 64.9 | 0 | 8 | 21.2 | 56 | pass |
| SEQ+ARP MOD1 | 62.3 | 65.8 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 42.2 | 42.9 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.8 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 58.2 | 59.2 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 66.8 | 69.7 | 0 | 8 | 48.9 | 20 | pass |
| SINGLE MULTI H | 54.9 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 63.1 | 64.1 | 0 | 5 | 52.5 | 8 | pass |
| SPREAD MULTI H | 53.3 | 54.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 66.5 | 67.5 | 0 | 7 | 49.2 | 8 | pass |
| STACK MULTI H | 64.9 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 68.0 | 69.0 | 0 | 6 | 44.6 | 9 | pass |
| SPREAD H CEL+PP | 57.9 | 58.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 67.5 | 68.8 | 0 | 5 | 45.6 | 9 | pass |
| SPREAD H TUM+PP | 55.9 | 56.7 | 0 | 6 | 57.3 | 1 | pass |
| SEQ+ARP SPRD CEL | 78.1 | 81.1 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 57.3 | 58.2 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 58.2 | 59.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.7 | 68.3 | 0 | 8 | 50.5 | 20 | pass |
| STRIKE | 41.0 | 41.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.4 | 55.3 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 70.5 | 73.2 | 0 | 8 | 43.2 | 12 | FAIL |
| SEQ+ARP STRK FILT | 70.6 | 73.7 | 0 | 8 | 43.2 | 20 | FAIL |

### 2026-10-17 10:27, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 16.9 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 22.6 | 23.3 | 0 | 1 | 61.2 | 3 | pass |
| SINGLE | 40.0 | 40.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 53.0 | 53.8 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.8 | 54.5 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 41.7 | 42.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.9 | 50.7 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.6 | 54.5 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.9 | 47.8 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.5 | 62.3 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD HEAVY | 51.8 | 52.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.8 | 64.7 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 62.8 | 63.7 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 65.5 | 66.5 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 41.5 | 42.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.2 | 41.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.2 | 41.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.4 | 41.1 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.4 | 54.3 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.4 | 63.6 | 0 | 8 | 53.9 | 20 | pass |
| SEQ+ARP AMP | 61.4 | 64.4 | 0 | 8 | 53.2 | 20 | pass |
| SEQ+ARP SEQ | 61.3 | 63.8 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.2 | 64.2 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.4 | 42.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.2 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.6 | 58.4 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 65.9 | 68.4 | 0 | 8 | 50.2 | 20 | pass |
| SINGLE MULTI H | 54.2 | 55.0 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 62.4 | 63.4 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 52.7 | 53.6 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 66.1 | 67.0 | 0 | 7 | 50.5 | 8 | pass |
| STACK MULTI H | 64.4 | 65.4 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 67.3 | 68.3 | 0 | 6 | 49.6 | 9 | pass |
| SPREAD H CEL+PP | 57.2 | 58.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 66.8 | 67.8 | 0 | 5 | 50.2 | 9 | pass |
| SPREAD H TUM+PP | 55.1 | 56.0 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.3 | 80.3 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.8 | 57.6 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 57.6 | 58.4 | 0 | 8 | 56.9 | 7 | pass |
| SEQ+ARP SPR+BBD | 65.1 | 67.7 | 0 | 8 | 51.8 | 20 | pass |
| STRIKE | 40.4 | 41.4 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.8 | 54.6 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 69.9 | 72.9 | 0 | 8 | 43.2 | 20 | pass |
| SEQ+ARP STRK FILT | 70.2 | 73.0 | 0 | 8 | 42.9 | 13 | FAIL |

### 2026-10-17 10:20, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.7 | 23.3 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 40.0 | 40.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.0 | 53.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 53.7 | 54.5 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.7 | 42.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.7 | 50.5 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.6 | 54.4 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.9 | 47.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.4 | 62.2 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD HEAVY | 51.7 | 52.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.6 | 64.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 62.7 | 63.7 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 65.5 | 66.3 | 0 | 6 | 52.2 | 8 | pass |
| SINGLE LADDER | 41.5 | 42.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.3 | 41.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 HIGH | 40.3 | 41.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE FORMANT | 40.4 | 41.1 | 0 | 8 | 57.5 | 1 | pass |
| DUAL FLT LADDER | 53.1 | 53.7 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.4 | 63.9 | 0 | 8 | 52.5 | 20 | pass |
| SEQ+ARP AMP | 61.5 | 64.2 | 0 | 8 | 52.5 | 20 | pass |
| SEQ+ARP SEQ | 61.2 | 63.8 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.2 | 64.4 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.5 | 42.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.3 | 57.1 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.6 | 58.5 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.0 | 68.6 | 0 | 8 | 50.5 | 20 | pass |
| SINGLE MULTI H | 54.1 | 54.7 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.1 | 62.9 | 0 | 5 | 52.5 | 8 | pass |
| SPREAD MULTI H | 52.6 | 53.3 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.6 | 66.3 | 0 | 7 | 50.9 | 8 | pass |
| STACK MULTI H | 64.3 | 65.3 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.4 | 68.2 | 0 | 6 | 47.6 | 9 | pass |
| SPREAD H CEL+PP | 57.2 | 58.2 | 0 | 6 | 57.6 | 7 | pass |
| DUALOSC H CEL+PP | 66.6 | 67.7 | 0 | 5 | 47.9 | 9 | pass |
| SPREAD H TUM+PP | 55.2 | 56.0 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.2 | 80.0 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.6 | 57.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 57.6 | 58.6 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 65.3 | 68.0 | 0 | 8 | 50.8 | 20 | pass |
| STRIKE | 40.3 | 41.0 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.7 | 54.4 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.0 | 72.7 | 0 | 8 | 43.5 | 20 | pass |
| SEQ+ARP STRK FILT | 70.4 | 73.1 | 0 | 8 | 43.2 | 12 | FAIL |

### 2026-10-17 10:15, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 17.2 | 17.6 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.3 | 23.9 | 0 | 1 | 61.3 | 4 | pass |
| SINGLE | 40.6 | 41.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 53.6 | 54.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 54.3 | 55.1 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 42.2 | 43.0 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 50.4 | 51.2 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 54.2 | 55.0 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 47.6 | 48.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.0 | 62.7 | 0 | 5 | 54.5 | 8 | pass |
| SPREAD HEAVY | 52.4 | 53.2 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 64.2 | 64.9 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.1 | 64.0 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 66.1 | 66.8 | 0 | 6 | 50.9 | 8 | pass |
| SINGLE LADDER | 42.0 | 42.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.9 | 41.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.9 | 41.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.9 | 41.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.6 | 54.4 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 62.0 | 64.7 | 0 | 8 | 52.9 | 20 | pass |
| SEQ+ARP AMP | 62.1 | 64.8 | 0 | 8 | 52.9 | 20 | pass |
| SEQ+ARP SEQ | 61.8 | 64.3 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.8 | 64.7 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 42.0 | 42.6 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.8 | 57.6 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 58.1 | 59.0 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 66.6 | 69.4 | 0 | 8 | 49.2 | 20 | pass |
| SINGLE MULTI H | 54.6 | 55.3 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.8 | 63.7 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 53.1 | 53.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 66.4 | 67.3 | 0 | 7 | 48.5 | 9 | pass |
| STACK MULTI H | 64.8 | 65.6 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.9 | 68.8 | 0 | 6 | 45.2 | 9 | pass |
| SPREAD H CEL+PP | 57.9 | 58.8 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 67.3 | 68.2 | 0 | 5 | 48.2 | 9 | pass |
| SPREAD H TUM+PP | 55.8 | 56.5 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.9 | 80.8 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 57.2 | 58.0 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CEL+BBD PP | 58.2 | 59.2 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 65.8 | 68.4 | 0 | 8 | 51.2 | 20 | pass |
| STRIKE | 41.1 | 41.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.3 | 55.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.5 | 73.7 | 0 | 8 | 43.2 | 20 | FAIL |
| SEQ+ARP STRK FILT | 70.7 | 73.8 | 0 | 8 | 42.9 | 12 | FAIL |

### 2026-10-16 23:01, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 16.9 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.5 | 23.2 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 39.8 | 40.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.9 | 53.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.7 | 54.3 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.5 | 42.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.6 | 50.2 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.5 | 54.2 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.8 | 47.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.3 | 62.0 | 0 | 5 | 54.2 | 8 | pass |
| SPREAD HEAVY | 51.6 | 52.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.4 | 64.2 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 62.7 | 63.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 65.5 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 41.3 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.2 | 41.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.2 | 40.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.2 | 40.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.0 | 53.6 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.3 | 63.6 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 61.4 | 64.2 | 0 | 8 | 53.1 | 20 | pass |
| SEQ+ARP SEQ | 61.2 | 63.9 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.2 | 64.3 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.3 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.2 | 57.0 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 57.5 | 58.3 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.0 | 68.7 | 0 | 8 | 50.9 | 20 | pass |
| SINGLE MULTI H | 54.0 | 54.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 62.1 | 62.9 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD MULTI H | 52.4 | 53.1 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 65.7 | 66.5 | 0 | 7 | 50.2 | 8 | pass |
| STACK MULTI H | 64.0 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.8 | 67.7 | 0 | 6 | 48.2 | 9 | pass |
| SPREAD H CEL+PP | 57.1 | 58.0 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 66.6 | 67.6 | 0 | 5 | 49.2 | 9 | pass |
| SPREAD H TUM+PP | 55.0 | 55.9 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.1 | 80.3 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 56.6 | 57.4 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 57.4 | 58.4 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 65.0 | 67.7 | 0 | 8 | 51.2 | 20 | pass |
| STRIKE | 40.2 | 40.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.6 | 54.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 69.7 | 72.7 | 0 | 8 | 43.5 | 20 | pass |
| SEQ+ARP STRK FILT | 69.8 | 72.9 | 0 | 8 | 43.9 | 20 | pass |

### 2026-10-16 22:40, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 22.5 | 23.3 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 39.8 | 40.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.8 | 53.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.5 | 54.1 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 41.4 | 42.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.5 | 50.2 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.6 | 54.3 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.7 | 47.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.2 | 61.9 | 0 | 5 | 53.5 | 8 | pass |
| SPREAD HEAVY | 51.6 | 52.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.4 | 64.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 62.9 | 63.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 65.4 | 66.2 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 41.2 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.0 | 41.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.0 | 40.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.2 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.9 | 53.6 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.4 | 63.8 | 0 | 8 | 53.9 | 20 | pass |
| SEQ+ARP AMP | 61.5 | 64.1 | 0 | 8 | 53.2 | 20 | pass |
| SEQ+ARP SEQ | 61.3 | 64.0 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.2 | 64.5 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.2 | 41.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.1 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.4 | 58.3 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.0 | 68.7 | 0 | 8 | 50.5 | 20 | pass |
| SINGLE MULTI H | 53.9 | 54.6 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 61.9 | 62.7 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD MULTI H | 52.4 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.7 | 66.3 | 0 | 7 | 51.5 | 8 | pass |
| STACK MULTI H | 64.2 | 65.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.9 | 67.7 | 0 | 6 | 48.9 | 8 | pass |
| SPREAD H CEL+PP | 57.0 | 57.9 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 66.6 | 67.6 | 0 | 5 | 49.2 | 9 | pass |
| SPREAD H TUM+PP | 55.0 | 55.8 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.2 | 80.0 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.6 | 57.4 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 57.4 | 58.2 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 65.1 | 67.7 | 0 | 8 | 51.8 | 20 | pass |
| STRIKE | 40.1 | 40.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.6 | 54.4 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 69.9 | 72.9 | 0 | 8 | 43.5 | 20 | pass |
| SEQ+ARP STRK FILT | 69.9 | 73.2 | 0 | 8 | 43.2 | 20 | pass |

### 2026-10-16 22:35, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 22.5 | 23.1 | 0 | 1 | 61.6 | 3 | pass |
| SINGLE | 39.8 | 40.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.8 | 53.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.5 | 54.2 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 41.5 | 42.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.5 | 50.3 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.7 | 54.5 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.8 | 47.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.2 | 62.0 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD HEAVY | 51.6 | 52.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.5 | 64.3 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 62.8 | 63.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 65.4 | 66.2 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 41.2 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.0 | 41.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.0 | 40.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.2 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.7 | 53.5 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.3 | 63.7 | 0 | 8 | 53.2 | 20 | pass |
| SEQ+ARP AMP | 61.4 | 64.2 | 0 | 8 | 52.5 | 20 | pass |
| SEQ+ARP SEQ | 61.1 | 64.0 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.2 | 64.5 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.2 | 41.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.1 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.5 | 58.3 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.9 | 68.6 | 0 | 8 | 49.9 | 20 | pass |
| SINGLE MULTI H | 54.0 | 54.7 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.1 | 62.8 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD MULTI H | 52.4 | 53.2 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 65.7 | 66.5 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 64.2 | 65.1 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 66.9 | 67.9 | 0 | 6 | 48.9 | 9 | pass |
| SPREAD H CEL+PP | 57.1 | 58.0 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 66.6 | 67.6 | 0 | 5 | 49.2 | 8 | pass |
| SPREAD H TUM+PP | 55.0 | 55.7 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.1 | 80.1 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.6 | 57.6 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 57.4 | 58.3 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 65.1 | 67.6 | 0 | 8 | 51.2 | 20 | pass |
| STRIKE | 40.2 | 40.9 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.6 | 54.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.1 | 73.1 | 0 | 8 | 43.5 | 20 | FAIL |
| SEQ+ARP STRK FILT | 70.1 | 73.1 | 0 | 8 | 42.9 | 13 | FAIL |

### 2026-10-16 22:08, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.7 | 23.3 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 40.0 | 40.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.0 | 53.7 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.6 | 54.4 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.7 | 42.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.8 | 50.5 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.6 | 54.4 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.9 | 47.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.2 | 62.0 | 0 | 5 | 54.2 | 8 | pass |
| SPREAD HEAVY | 51.7 | 52.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.7 | 64.4 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 62.7 | 63.5 | 0 | 6 | 52.5 | 8 | pass |
| BINAURAL HEAVY | 65.4 | 66.2 | 0 | 6 | 52.2 | 8 | pass |
| SINGLE LADDER | 41.5 | 42.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.1 | 41.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 HIGH | 40.1 | 40.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 40.4 | 41.0 | 0 | 8 | 57.5 | 1 | pass |
| DUAL FLT LADDER | 53.3 | 53.9 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.3 | 63.8 | 0 | 8 | 53.2 | 20 | pass |
| SEQ+ARP AMP | 61.4 | 64.1 | 0 | 8 | 52.8 | 20 | pass |
| SEQ+ARP SEQ | 61.1 | 63.8 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.1 | 64.3 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.4 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.1 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.5 | 58.2 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.9 | 68.5 | 0 | 8 | 51.2 | 20 | pass |
| SINGLE MULTI H | 54.1 | 54.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.1 | 62.9 | 0 | 5 | 54.5 | 8 | pass |
| SPREAD MULTI H | 52.6 | 53.3 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.9 | 66.6 | 0 | 7 | 50.2 | 8 | pass |
| STACK MULTI H | 64.3 | 65.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.1 | 67.9 | 0 | 6 | 50.9 | 9 | pass |
| SPREAD H CEL+PP | 57.1 | 57.9 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 66.4 | 67.3 | 0 | 5 | 50.2 | 9 | pass |
| SPREAD H TUM+PP | 55.0 | 55.8 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.1 | 79.9 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.7 | 57.6 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 57.5 | 58.4 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 65.2 | 67.9 | 0 | 8 | 50.2 | 20 | pass |
| STRIKE | 40.2 | 40.9 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.6 | 54.4 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 69.4 | 72.1 | 0 | 8 | 43.9 | 12 | pass |

### 2026-10-16 22:02, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 22.7 | 23.4 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 40.0 | 40.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.0 | 53.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 53.6 | 54.3 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.7 | 42.3 | 0 | 6 | 57.3 | 1 | pass |
| DUAL FILTER | 49.7 | 50.4 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.5 | 54.3 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.9 | 47.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.3 | 62.0 | 0 | 5 | 53.2 | 8 | pass |
| SPREAD HEAVY | 51.7 | 52.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.6 | 64.4 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 62.6 | 63.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 65.3 | 66.1 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 41.5 | 42.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.1 | 41.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.1 | 40.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.4 | 41.1 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.2 | 53.8 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.3 | 63.7 | 0 | 8 | 53.5 | 20 | pass |
| SEQ+ARP AMP | 61.4 | 64.1 | 0 | 8 | 53.2 | 20 | pass |
| SEQ+ARP SEQ | 61.1 | 63.8 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.1 | 64.3 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.4 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.1 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.4 | 58.2 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.9 | 68.6 | 0 | 8 | 50.5 | 20 | pass |
| SINGLE MULTI H | 54.1 | 54.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.1 | 62.9 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD MULTI H | 52.6 | 53.3 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.9 | 66.7 | 0 | 7 | 50.8 | 8 | pass |
| STACK MULTI H | 64.3 | 65.3 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.0 | 68.0 | 0 | 6 | 49.6 | 9 | pass |
| SPREAD H CEL+PP | 57.0 | 57.8 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 66.5 | 67.4 | 0 | 5 | 49.2 | 9 | pass |
| SPREAD H TUM+PP | 55.0 | 55.8 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.1 | 79.8 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.8 | 57.6 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CEL+BBD PP | 57.5 | 58.3 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 65.2 | 67.8 | 0 | 8 | 50.5 | 20 | pass |
| STRIKE | 40.4 | 41.1 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.8 | 54.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.3 | 74.4 | 0 | 8 | 43.2 | 12 | FAIL |

### 2026-10-16 21:56, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.8 | 23.3 | 0 | 1 | 61.2 | 3 | pass |
| SINGLE | 40.0 | 40.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 53.0 | 53.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 53.6 | 54.3 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 41.7 | 42.4 | 0 | 6 | 57.3 | 1 | pass |
| DUAL FILTER | 49.8 | 50.5 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.6 | 54.4 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 47.0 | 47.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.3 | 62.0 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD HEAVY | 51.7 | 52.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.6 | 64.3 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 62.5 | 63.4 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 65.3 | 66.1 | 0 | 6 | 51.5 | 8 | pass |
| SINGLE LADDER | 41.5 | 42.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.1 | 41.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.1 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.4 | 41.1 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.3 | 54.0 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.2 | 63.7 | 0 | 8 | 52.8 | 20 | pass |
| SEQ+ARP AMP | 61.3 | 63.8 | 0 | 8 | 53.2 | 20 | pass |
| SEQ+ARP SEQ | 61.1 | 63.7 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.1 | 64.3 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.4 | 42.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.1 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.4 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.8 | 68.3 | 0 | 8 | 50.2 | 20 | pass |
| SINGLE MULTI H | 54.1 | 54.8 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.1 | 62.9 | 0 | 5 | 54.2 | 8 | pass |
| SPREAD MULTI H | 52.5 | 53.2 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 65.8 | 66.6 | 0 | 7 | 51.5 | 8 | pass |
| STACK MULTI H | 64.2 | 65.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.0 | 67.8 | 0 | 6 | 50.5 | 9 | pass |
| SPREAD H CEL+PP | 57.0 | 57.9 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 66.5 | 67.4 | 0 | 5 | 49.2 | 8 | pass |
| SPREAD H TUM+PP | 54.9 | 55.6 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.1 | 79.9 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 56.7 | 57.5 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CEL+BBD PP | 57.5 | 58.3 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 65.2 | 67.5 | 0 | 8 | 50.8 | 20 | pass |
| STRIKE | 40.4 | 41.2 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.7 | 54.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.6 | 73.4 | 0 | 8 | 42.9 | 13 | FAIL |

### 2026-10-16 16:14, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 16.9 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.6 | 23.2 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 39.8 | 40.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.7 | 53.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.8 | 54.5 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 41.8 | 42.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.3 | 50.0 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.2 | 54.0 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.6 | 47.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.0 | 61.8 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD HEAVY | 51.5 | 52.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.2 | 63.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 62.2 | 63.0 | 0 | 6 | 53.6 | 8 | pass |
| BINAURAL HEAVY | 65.1 | 66.0 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 41.2 | 41.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.2 | 41.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.2 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.2 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.7 | 53.5 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.6 | 64.2 | 0 | 8 | 52.8 | 20 | pass |
| SEQ+ARP AMP | 61.8 | 64.6 | 0 | 8 | 53.1 | 20 | pass |
| SEQ+ARP SEQ | 61.5 | 64.1 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.5 | 64.4 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 41.2 | 41.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.0 | 56.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.1 | 57.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 66.0 | 68.8 | 0 | 8 | 49.9 | 20 | pass |
| SINGLE MULTI H | 54.2 | 54.9 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 61.9 | 62.6 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD MULTI H | 52.3 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.3 | 66.1 | 0 | 7 | 51.5 | 8 | pass |
| STACK MULTI H | 63.9 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.7 | 67.5 | 0 | 6 | 47.9 | 9 | pass |
| SPREAD H CEL+PP | 56.8 | 57.6 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 66.2 | 67.0 | 0 | 5 | 48.5 | 8 | pass |
| SPREAD H TUM+PP | 55.1 | 55.9 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 76.7 | 79.5 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 56.3 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 57.0 | 57.8 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 65.3 | 67.8 | 0 | 8 | 50.5 | 20 | pass |

### 2026-10-16 16:10, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 17.5 | 17.9 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.5 | 24.1 | 0 | 1 | 61.0 | 1 | pass |
| SINGLE | 40.7 | 41.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.6 | 54.3 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 54.8 | 55.5 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 42.7 | 43.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 50.3 | 50.9 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 54.1 | 54.8 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 47.5 | 48.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.9 | 62.5 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD HEAVY | 52.4 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 64.2 | 64.8 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.1 | 63.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 66.0 | 66.7 | 0 | 6 | 49.9 | 8 | pass |
| SINGLE LADDER | 42.2 | 42.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 41.1 | 42.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 41.1 | 41.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 41.1 | 41.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 53.7 | 54.4 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 62.6 | 65.1 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 62.7 | 65.0 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 62.4 | 65.1 | 0 | 8 | 21.3 | 56 | pass |
| SEQ+ARP MOD1 | 62.4 | 65.1 | 0 | 8 | 6.0 | 127 | pass |
| LADDER FILTER PG | 42.2 | 42.9 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.0 | 57.8 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CELEST+PP | 58.0 | 58.8 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.9 | 69.8 | 0 | 8 | 47.2 | 20 | pass |
| SINGLE MULTI H | 55.1 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 62.8 | 63.6 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 53.3 | 54.0 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 66.2 | 67.0 | 0 | 7 | 49.5 | 8 | pass |
| STACK MULTI H | 64.8 | 65.6 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 67.6 | 68.5 | 0 | 6 | 45.6 | 9 | pass |
| SPREAD H CEL+PP | 57.7 | 58.6 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 67.2 | 68.0 | 0 | 5 | 49.2 | 9 | pass |
| SPREAD H TUM+PP | 56.0 | 56.9 | 0 | 6 | 57.3 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.7 | 80.5 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 57.2 | 57.9 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CEL+BBD PP | 58.0 | 58.8 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 66.2 | 68.5 | 0 | 8 | 49.5 | 20 | pass |

### 2026-10-16 16:05, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.7 | 20.1 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 25.7 | 26.4 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 42.9 | 43.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 55.8 | 56.5 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 56.9 | 57.5 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD | 45.0 | 45.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 52.4 | 53.2 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 56.3 | 57.1 | 0 | 6 | 57.2 | 7 | pass |
| BINAURAL | 49.7 | 50.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 64.0 | 64.8 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 54.6 | 55.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 66.3 | 67.1 | 0 | 7 | 47.9 | 8 | pass |
| STACK HEAVY | 65.3 | 66.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 68.1 | 69.1 | 0 | 6 | 43.2 | 9 | pass |
| SINGLE LADDER | 44.4 | 45.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 43.3 | 44.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 43.3 | 44.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 43.3 | 44.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 55.9 | 56.6 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.7 | 67.4 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP AMP | 64.8 | 67.5 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP SEQ | 64.4 | 66.8 | 0 | 8 | 17.9 | 59 | pass |
| SEQ+ARP MOD1 | 64.6 | 67.7 | 0 | 8 | 5.3 | 127 | pass |
| LADDER FILTER PG | 44.4 | 45.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 59.1 | 60.0 | 0 | 8 | 56.5 | 8 | pass |
| HEAVY CELEST+PP | 60.1 | 61.0 | 0 | 8 | 55.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 69.1 | 71.8 | 0 | 8 | 45.2 | 20 | pass |
| SINGLE MULTI H | 57.3 | 58.0 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC MULTI H | 64.9 | 65.5 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 55.4 | 56.2 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT MULTI H | 68.5 | 69.3 | 0 | 7 | 43.2 | 9 | pass |
| STACK MULTI H | 67.0 | 67.8 | 0 | 6 | 49.6 | 9 | pass |
| BINAUR MULTI H | 69.8 | 70.6 | 0 | 6 | 43.6 | 9 | pass |
| SPREAD H CEL+PP | 59.9 | 60.8 | 0 | 6 | 55.2 | 8 | pass |
| DUALOSC H CEL+PP | 69.2 | 70.2 | 0 | 5 | 43.9 | 9 | pass |
| SPREAD H TUM+PP | 58.2 | 59.0 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 79.8 | 82.8 | 0 | 6 | 43.2 | 13 | FAIL |
| HEAVY SPRING+PP | 59.4 | 60.1 | 0 | 8 | 56.9 | 8 | pass |
| HEAVY CEL+BBD PP | 60.1 | 61.0 | 0 | 8 | 53.9 | 8 | pass |
| SEQ+ARP SPR+BBD | 68.3 | 70.9 | 0 | 8 | 44.9 | 20 | pass |

### 2026-10-16 14:08, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.9 | 16.2 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 21.7 | 22.3 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 39.0 | 39.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.1 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.1 | 53.9 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.0 | 41.7 | 0 | 6 | 57.3 | 1 | pass |
| DUAL FILTER | 48.8 | 49.5 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 52.5 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.0 | 46.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.3 | 61.0 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD HEAVY | 50.9 | 51.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.6 | 63.4 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 61.6 | 62.3 | 0 | 6 | 53.2 | 8 | pass |
| BINAURAL HEAVY | 64.6 | 65.3 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 40.7 | 41.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.4 | 40.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.4 | 40.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.6 | 40.2 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.4 | 53.1 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.0 | 63.5 | 0 | 8 | 53.9 | 20 | pass |
| SEQ+ARP AMP | 61.1 | 63.6 | 0 | 8 | 53.2 | 20 | pass |
| SEQ+ARP SEQ | 60.9 | 63.2 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 60.9 | 63.8 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 40.7 | 41.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.5 | 56.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.8 | 57.6 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.7 | 68.4 | 0 | 8 | 50.5 | 20 | pass |
| SINGLE MULTI H | 53.8 | 54.6 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 61.5 | 62.2 | 0 | 5 | 53.2 | 8 | pass |
| SPREAD MULTI H | 51.9 | 52.5 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.1 | 65.8 | 0 | 7 | 52.2 | 8 | pass |
| STACK MULTI H | 63.5 | 64.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 66.3 | 67.2 | 0 | 6 | 48.2 | 8 | pass |
| SPREAD H CEL+PP | 56.5 | 57.4 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 65.9 | 66.8 | 0 | 5 | 50.2 | 8 | pass |
| SPREAD H TUM+PP | 54.7 | 55.6 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 76.8 | 79.5 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 55.6 | 56.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 56.6 | 57.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.4 | 66.8 | 0 | 8 | 51.9 | 20 | pass |

### 2026-10-16 13:08, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.9 | 16.3 | 0 | 0 | 61.6 | 2 | pass |
| SINGLE 1V | 21.8 | 22.3 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 39.0 | 39.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.0 | 52.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.2 | 53.8 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 41.0 | 41.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 48.7 | 49.3 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 52.7 | 53.3 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.0 | 46.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.4 | 61.0 | 0 | 5 | 55.9 | 8 | pass |
| SPREAD HEAVY | 50.8 | 51.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.7 | 63.5 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 61.7 | 62.4 | 0 | 6 | 54.6 | 8 | pass |
| BINAURAL HEAVY | 64.6 | 65.3 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 40.6 | 41.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.3 | 40.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.3 | 40.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.5 | 40.1 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.4 | 52.9 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 61.1 | 63.6 | 0 | 8 | 53.5 | 20 | pass |
| SEQ+ARP AMP | 61.0 | 63.7 | 0 | 8 | 53.5 | 20 | pass |
| SEQ+ARP SEQ | 61.0 | 63.6 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 61.0 | 63.6 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 40.6 | 41.2 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.3 | 56.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.4 | 57.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 65.7 | 68.5 | 0 | 8 | 50.8 | 20 | pass |
| SINGLE MULTI H | 53.7 | 54.3 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 61.3 | 62.0 | 0 | 5 | 53.2 | 8 | pass |
| SPREAD MULTI H | 51.8 | 52.4 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.1 | 65.9 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 63.5 | 64.3 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 66.3 | 67.2 | 0 | 6 | 48.6 | 8 | pass |
| SPREAD H CEL+PP | 56.2 | 57.0 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 65.5 | 66.3 | 0 | 5 | 51.2 | 8 | pass |
| SPREAD H TUM+PP | 54.2 | 54.9 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 76.7 | 79.9 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 55.6 | 56.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 56.5 | 57.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.7 | 67.3 | 0 | 8 | 52.2 | 20 | pass |

### 2026-10-16 12:47, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.9 | 16.2 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 21.7 | 22.1 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 38.9 | 39.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.9 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 52.9 | 53.5 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.1 | 41.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 48.7 | 49.3 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 52.5 | 53.1 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.0 | 46.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.2 | 60.9 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD HEAVY | 50.8 | 51.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.5 | 63.1 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 61.6 | 62.3 | 0 | 6 | 53.6 | 8 | pass |
| BINAURAL HEAVY | 64.5 | 65.3 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 40.6 | 41.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.3 | 40.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.3 | 40.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.5 | 40.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.4 | 53.0 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 60.8 | 63.0 | 0 | 8 | 54.2 | 20 | pass |
| SEQ+ARP AMP | 60.7 | 63.0 | 0 | 8 | 54.6 | 20 | pass |
| SEQ+ARP SEQ | 60.6 | 63.1 | 0 | 8 | 21.5 | 47 | pass |
| SEQ+ARP MOD1 | 60.5 | 63.2 | 0 | 8 | 6.1 | 127 | pass |
| LADDER FILTER PG | 40.6 | 41.2 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.2 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.1 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 65.3 | 68.2 | 0 | 8 | 50.9 | 20 | pass |
| SINGLE MULTI H | 53.6 | 54.2 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 61.3 | 62.0 | 0 | 5 | 53.5 | 8 | pass |
| SPREAD MULTI H | 51.8 | 52.5 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.1 | 65.7 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 63.4 | 64.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.3 | 67.1 | 0 | 6 | 48.2 | 8 | pass |
| SPREAD H CEL+PP | 56.0 | 56.8 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 65.6 | 66.4 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 54.4 | 55.1 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 76.2 | 78.9 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 55.4 | 56.0 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 56.2 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.3 | 66.6 | 0 | 8 | 51.9 | 20 | pass |

### 2026-10-16 12:26, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.9 | 16.2 | 0 | 0 | 61.6 | 2 | pass |
| SINGLE 1V | 21.6 | 22.1 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 38.8 | 39.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.8 | 52.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 52.8 | 53.4 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 40.9 | 41.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 48.5 | 49.0 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 52.4 | 53.0 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 45.9 | 46.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.2 | 60.9 | 0 | 5 | 53.2 | 8 | pass |
| SPREAD HEAVY | 50.7 | 51.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.3 | 62.9 | 0 | 7 | 52.9 | 8 | pass |
| STACK HEAVY | 61.4 | 62.1 | 0 | 6 | 53.6 | 8 | pass |
| BINAURAL HEAVY | 64.4 | 65.2 | 0 | 6 | 52.2 | 8 | pass |
| SINGLE LADDER | 40.5 | 41.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.2 | 40.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.2 | 39.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.3 | 39.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.1 | 52.7 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.0 | 66.4 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.0 | 66.6 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 63.7 | 66.1 | 0 | 8 | 19.2 | 58 | pass |
| SEQ+ARP MOD1 | 63.9 | 65.8 | 0 | 8 | 5.7 | 127 | pass |
| LADDER FILTER PG | 40.4 | 41.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.1 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.1 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.4 | 71.0 | 0 | 8 | 45.9 | 20 | pass |
| SINGLE MULTI H | 53.4 | 54.0 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 61.1 | 61.9 | 0 | 5 | 54.2 | 8 | pass |
| SPREAD MULTI H | 51.6 | 52.3 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 64.8 | 65.4 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 63.2 | 63.9 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.1 | 66.7 | 0 | 6 | 49.6 | 8 | pass |
| SPREAD H CEL+PP | 55.9 | 56.7 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 65.5 | 66.3 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 54.3 | 55.0 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.4 | 81.8 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 55.3 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 56.1 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 67.4 | 69.7 | 0 | 8 | 46.5 | 20 | pass |

### 2026-10-16 12:06, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.8 | 16.1 | 0 | 0 | 62.0 | 1 | pass |
| SINGLE 1V | 21.5 | 22.0 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 38.7 | 39.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.8 | 52.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 52.7 | 53.4 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 40.9 | 41.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 48.4 | 49.0 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 52.1 | 52.9 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 45.7 | 46.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.1 | 60.8 | 0 | 5 | 54.2 | 8 | pass |
| SPREAD HEAVY | 50.6 | 51.3 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 62.3 | 63.0 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 61.3 | 62.0 | 0 | 6 | 54.2 | 8 | pass |
| BINAURAL HEAVY | 64.2 | 64.9 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 40.4 | 41.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.1 | 39.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.1 | 39.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.3 | 40.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.0 | 52.6 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 63.9 | 66.2 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.0 | 66.5 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 63.6 | 65.8 | 0 | 8 | 19.3 | 58 | pass |
| SEQ+ARP MOD1 | 63.9 | 66.4 | 0 | 8 | 5.7 | 127 | pass |
| LADDER FILTER PG | 40.3 | 41.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.1 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.3 | 57.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.4 | 70.8 | 0 | 8 | 46.6 | 20 | pass |
| SINGLE MULTI H | 53.3 | 53.9 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.9 | 61.5 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD MULTI H | 51.5 | 52.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 64.6 | 65.3 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 62.8 | 63.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 65.7 | 66.5 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 56.0 | 56.9 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 65.3 | 66.0 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 54.2 | 54.9 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.2 | 81.7 | 0 | 6 | 43.2 | 12 | FAIL |
| HEAVY SPRING+PP | 55.4 | 56.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 56.2 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 67.4 | 69.5 | 0 | 8 | 48.2 | 20 | pass |

### 2026-10-16 12:00, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.8 | 16.1 | 0 | 0 | 61.6 | 2 | pass |
| SINGLE 1V | 21.4 | 22.0 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 38.7 | 39.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE HEAVY | 51.8 | 52.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 52.7 | 53.4 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 40.8 | 41.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 48.4 | 49.1 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 52.1 | 52.8 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 45.7 | 46.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.1 | 60.8 | 0 | 5 | 53.5 | 8 | pass |
| SPREAD HEAVY | 50.6 | 51.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.3 | 62.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 61.2 | 61.9 | 0 | 6 | 54.2 | 8 | pass |
| BINAURAL HEAVY | 64.3 | 65.0 | 0 | 6 | 52.2 | 8 | pass |
| SINGLE LADDER | 40.3 | 40.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.1 | 40.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.1 | 39.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.4 | 40.1 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 51.8 | 52.5 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 63.9 | 65.9 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.0 | 66.3 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP SEQ | 63.6 | 66.1 | 0 | 8 | 19.3 | 58 | pass |
| SEQ+ARP MOD1 | 63.9 | 65.9 | 0 | 8 | 5.7 | 127 | pass |
| LADDER FILTER PG | 40.3 | 40.9 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.1 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.3 | 57.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.4 | 70.8 | 0 | 8 | 45.9 | 20 | pass |
| SINGLE MULTI H | 53.3 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 60.9 | 61.7 | 0 | 5 | 54.5 | 8 | pass |
| SPREAD MULTI H | 51.5 | 52.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 64.6 | 65.3 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 63.0 | 63.6 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 65.9 | 66.7 | 0 | 6 | 48.2 | 8 | pass |
| SPREAD H CEL+PP | 56.0 | 56.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 65.4 | 66.1 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 54.2 | 55.0 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.2 | 81.4 | 0 | 6 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 57.1 | 57.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CEL+BBD PP | 56.3 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 69.1 | 71.4 | 0 | 8 | 43.2 | 12 | pass |

### 2026-10-16 09:11, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.0 | 16.3 | 0 | 0 | 61.6 | 2 | pass |
| SINGLE 1V | 21.9 | 22.5 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 39.2 | 39.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.2 | 52.9 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.2 | 53.8 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 41.2 | 41.8 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 48.9 | 49.7 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 52.6 | 53.4 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.0 | 46.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.5 | 61.2 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD HEAVY | 50.9 | 51.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.8 | 63.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 61.8 | 62.7 | 0 | 6 | 52.9 | 8 | pass |
| BINAURAL HEAVY | 64.6 | 65.5 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 40.9 | 41.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.6 | 40.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.6 | 40.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.8 | 40.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.5 | 53.2 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.3 | 66.5 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.4 | 66.6 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP SEQ | 64.1 | 66.6 | 0 | 8 | 18.0 | 58 | pass |
| SEQ+ARP MOD1 | 64.4 | 66.9 | 0 | 8 | 5.4 | 127 | pass |
| LADDER FILTER PG | 40.9 | 41.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.2 | 56.0 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.5 | 57.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.6 | 71.2 | 0 | 8 | 44.2 | 20 | pass |
| SINGLE MULTI H | 53.7 | 54.4 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 61.5 | 62.1 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD MULTI H | 51.8 | 52.5 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.0 | 65.7 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 63.5 | 64.3 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.4 | 67.1 | 0 | 6 | 48.6 | 8 | pass |
| SPREAD H CEL+PP | 56.2 | 56.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 65.8 | 66.7 | 0 | 5 | 50.5 | 8 | pass |
| SPREAD H TUM+PP | 54.3 | 55.1 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.5 | 81.9 | 0 | 6 | 42.9 | 13 | FAIL |

### 2026-10-15 23:37, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.5 | 16.8 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.3 | 22.9 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 39.5 | 40.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.4 | 53.0 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 53.4 | 53.9 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 41.7 | 42.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.1 | 49.7 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.0 | 53.6 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.4 | 47.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.7 | 61.4 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD HEAVY | 51.2 | 51.8 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.9 | 63.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 62.0 | 62.7 | 0 | 6 | 55.2 | 8 | pass |
| BINAURAL HEAVY | 64.9 | 65.6 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 40.9 | 41.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 39.8 | 40.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.8 | 40.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.9 | 40.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.5 | 53.1 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.3 | 66.5 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.4 | 66.5 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 64.0 | 66.1 | 0 | 8 | 18.6 | 58 | pass |
| SEQ+ARP MOD1 | 64.2 | 66.7 | 0 | 8 | 5.5 | 127 | pass |
| LADDER FILTER PG | 40.9 | 41.4 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.8 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.9 | 57.7 | 0 | 8 | 56.9 | 7 | pass |
| SEQ+ARP CELEST+PP | 68.6 | 70.9 | 0 | 8 | 44.5 | 20 | pass |
| SINGLE MULTI H | 53.9 | 54.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 61.5 | 62.2 | 0 | 5 | 53.5 | 8 | pass |
| SPREAD MULTI H | 52.0 | 52.7 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.0 | 65.7 | 0 | 7 | 51.5 | 8 | pass |
| STACK MULTI H | 63.7 | 64.4 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.5 | 67.4 | 0 | 6 | 48.2 | 8 | pass |
| SPREAD H CEL+PP | 56.4 | 57.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALOSC H CEL+PP | 65.9 | 66.6 | 0 | 5 | 50.9 | 8 | pass |
| SPREAD H TUM+PP | 54.3 | 55.0 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.5 | 81.7 | 0 | 6 | 42.9 | 13 | FAIL |

### 2026-10-15 23:28, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 16.9 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.5 | 23.2 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 39.8 | 40.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.8 | 53.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 53.8 | 54.5 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 41.9 | 42.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 49.4 | 50.1 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 53.4 | 54.2 | 0 | 6 | 57.2 | 1 | pass |
| BINAURAL | 46.6 | 47.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.1 | 61.8 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD HEAVY | 51.4 | 52.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.2 | 64.0 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 62.3 | 63.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 65.1 | 66.0 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 41.3 | 41.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 40.1 | 41.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 40.1 | 40.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.2 | 41.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 52.9 | 53.7 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 65.0 | 67.3 | 0 | 8 | 51.5 | 20 | pass |
| SEQ+ARP AMP | 65.1 | 67.2 | 0 | 8 | 51.9 | 20 | pass |
| SEQ+ARP SEQ | 64.7 | 67.2 | 0 | 8 | 17.4 | 59 | pass |
| SEQ+ARP MOD1 | 65.0 | 67.8 | 0 | 8 | 5.3 | 127 | pass |
| LADDER FILTER PG | 41.3 | 42.0 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.7 | 58.5 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 58.7 | 59.6 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 70.8 | 73.3 | 0 | 8 | 42.9 | 13 | FAIL |
| SINGLE MULTI H | 54.3 | 55.0 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 61.9 | 62.6 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD MULTI H | 52.3 | 53.0 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 65.4 | 66.2 | 0 | 7 | 52.2 | 8 | pass |
| STACK MULTI H | 63.9 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 66.8 | 67.6 | 0 | 6 | 47.9 | 8 | pass |
| SPREAD H CEL+PP | 58.2 | 59.0 | 0 | 6 | 56.6 | 7 | pass |
| DUALOSC H CEL+PP | 67.7 | 68.7 | 0 | 5 | 44.6 | 9 | pass |
| SPREAD H TUM+PP | 56.5 | 57.1 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 81.6 | 84.3 | 0 | 6 | 43.2 | 12 | FAIL |

### 2026-10-15 22:55, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.4 | 16.7 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 22.2 | 22.8 | 0 | 1 | 61.6 | 1 | pass |
| SINGLE | 39.4 | 40.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.4 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 53.3 | 54.0 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 49.6 | 50.2 | 0 | 7 | 57.2 | 1 | pass |
| DUAL FILTER | 40.6 | 41.3 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 40.4 | 41.1 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 36.2 | 36.8 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.6 | 61.4 | 0 | 5 | 55.8 | 8 | pass |
| SPREAD HEAVY | 62.7 | 63.4 | 0 | 7 | 51.9 | 1 | pass |
| DUAL FLT HEAVY | 51.4 | 52.1 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 46.8 | 47.5 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 48.8 | 49.5 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 40.9 | 41.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 39.6 | 40.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.6 | 40.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.8 | 40.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 43.6 | 44.2 | 0 | 5 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.8 | 67.1 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.8 | 67.4 | 0 | 8 | 51.6 | 20 | pass |
| SEQ+ARP SEQ | 64.5 | 67.0 | 0 | 8 | 17.9 | 58 | pass |
| SEQ+ARP MOD1 | 64.8 | 68.2 | 0 | 8 | 5.4 | 127 | pass |
| LADDER FILTER PG | 40.9 | 41.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.6 | 58.5 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 58.9 | 59.7 | 0 | 8 | 56.9 | 7 | pass |
| SEQ+ARP CELEST+PP | 71.2 | 73.9 | 0 | 8 | 42.9 | 13 | FAIL |
| SINGLE MULTI H | 54.0 | 54.5 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 61.6 | 62.2 | 0 | 5 | 53.5 | 8 | pass |
| SPREAD MULTI H | 64.2 | 64.9 | 0 | 7 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 53.1 | 53.7 | 0 | 5 | 57.2 | 1 | pass |
| STACK MULTI H | 48.0 | 48.7 | 0 | 4 | 57.2 | 1 | pass |
| BINAUR MULTI H | 50.0 | 50.7 | 0 | 4 | 57.2 | 1 | pass |
| SPREAD H CEL+PP | 70.6 | 71.4 | 0 | 7 | 42.9 | 1 | FAIL |
| DUALOSC H CEL+PP | 68.1 | 68.8 | 0 | 5 | 43.2 | 9 | pass |
| SPREAD H TUM+PP | 69.3 | 70.1 | 0 | 7 | 43.6 | 9 | pass |
| SEQ+ARP SPRD CEL | 82.1 | 84.7 | 0 | 7 | 42.9 | 12 | FAIL |

### 2026-10-15 19:25, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.4 | 16.8 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.3 | 22.8 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 39.6 | 40.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.7 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 53.5 | 54.0 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 50.0 | 50.6 | 0 | 7 | 57.2 | 1 | pass |
| DUAL FILTER | 40.9 | 41.4 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 40.6 | 41.2 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 36.3 | 36.9 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.0 | 61.7 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD HEAVY | 63.1 | 63.8 | 0 | 7 | 51.9 | 8 | pass |
| DUAL FLT HEAVY | 51.7 | 52.4 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 47.1 | 47.7 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 49.0 | 49.8 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 41.2 | 41.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 39.9 | 40.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.8 | 40.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.0 | 40.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 43.9 | 44.5 | 0 | 5 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.5 | 66.8 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.5 | 66.7 | 0 | 8 | 52.1 | 20 | pass |
| SEQ+ARP SEQ | 64.2 | 66.5 | 0 | 8 | 18.6 | 58 | pass |
| SEQ+ARP MOD1 | 64.5 | 66.6 | 0 | 8 | 5.7 | 127 | pass |
| LADDER FILTER PG | 41.2 | 41.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.6 | 59.4 | 0 | 8 | 56.9 | 7 | pass |
| HEAVY CELEST+PP | 59.9 | 60.7 | 0 | 8 | 56.6 | 8 | pass |
| SEQ+ARP CELEST+PP | 71.8 | 74.0 | 0 | 8 | 42.9 | 13 | FAIL |
| SINGLE MULTI H | 54.2 | 54.9 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 61.9 | 62.5 | 0 | 5 | 54.2 | 8 | pass |
| SPREAD MULTI H | 64.6 | 65.4 | 0 | 7 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 53.5 | 54.1 | 0 | 5 | 57.2 | 1 | pass |
| STACK MULTI H | 48.3 | 48.9 | 0 | 4 | 57.2 | 1 | pass |
| BINAUR MULTI H | 50.2 | 50.9 | 0 | 4 | 57.2 | 1 | pass |

### 2026-10-15 19:15, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.2 | 16.5 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 21.7 | 22.4 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 38.0 | 38.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.7 | 53.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 52.3 | 52.9 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 48.3 | 49.0 | 0 | 7 | 57.2 | 1 | pass |
| DUAL FILTER | 38.6 | 39.3 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 39.3 | 40.0 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 34.7 | 35.4 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.8 | 61.6 | 0 | 5 | 54.5 | 8 | pass |
| SPREAD HEAVY | 63.0 | 64.0 | 0 | 7 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 51.8 | 52.6 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 47.1 | 48.0 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 49.0 | 49.9 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 40.9 | 41.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 39.7 | 40.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.7 | 40.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.0 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 43.6 | 44.3 | 0 | 5 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.8 | 66.9 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.8 | 67.1 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 64.4 | 66.5 | 0 | 8 | 18.6 | 58 | pass |
| SEQ+ARP MOD1 | 64.7 | 67.5 | 0 | 8 | 5.4 | 127 | pass |
| LADDER FILTER PG | 40.9 | 41.4 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.6 | 59.4 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 60.0 | 60.8 | 0 | 8 | 54.9 | 8 | pass |
| SEQ+ARP CELEST+PP | 72.3 | 74.7 | 0 | 8 | 42.9 | 12 | FAIL |
| SINGLE MULTI H | 54.1 | 54.8 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 61.7 | 62.5 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD MULTI H | 64.4 | 65.3 | 0 | 7 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 53.3 | 54.0 | 0 | 5 | 57.2 | 1 | pass |
| STACK MULTI H | 48.2 | 49.0 | 0 | 4 | 57.2 | 1 | pass |
| BINAUR MULTI H | 50.1 | 50.8 | 0 | 4 | 57.2 | 1 | pass |

### 2026-10-15 19:05, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.2 | 16.6 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 21.7 | 22.3 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 38.0 | 38.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.6 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 52.4 | 53.0 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 40.4 | 41.1 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FILTER | 38.6 | 39.3 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 39.2 | 39.9 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 34.7 | 35.4 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.8 | 61.5 | 0 | 5 | 54.8 | 8 | pass |
| SPREAD HEAVY | 51.4 | 52.2 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 51.7 | 52.5 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 47.1 | 47.8 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 49.0 | 49.7 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 34.8 | 35.5 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 39.8 | 40.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.7 | 40.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.0 | 40.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 34.6 | 35.2 | 0 | 3 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.8 | 66.9 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.7 | 66.9 | 0 | 8 | 51.5 | 20 | pass |
| SEQ+ARP SEQ | 64.4 | 66.4 | 0 | 8 | 18.7 | 58 | pass |
| SEQ+ARP MOD1 | 64.7 | 67.7 | 0 | 8 | 5.4 | 127 | pass |
| LADDER FILTER PG | 34.8 | 35.4 | 0 | 6 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 58.5 | 59.4 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 59.9 | 60.6 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP CELEST+PP | 72.3 | 74.4 | 0 | 8 | 42.9 | 12 | FAIL |

### 2026-10-15 16:56, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.3 | 16.6 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 22.0 | 22.5 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 38.2 | 38.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.7 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 52.5 | 53.2 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD | 47.4 | 48.1 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FILTER | 38.7 | 39.4 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 39.4 | 40.1 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 34.9 | 35.7 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.0 | 61.7 | 0 | 5 | 54.8 | 8 | pass |
| SPREAD HEAVY | 58.2 | 59.0 | 0 | 5 | 56.6 | 7 | pass |
| DUAL FLT HEAVY | 51.7 | 52.4 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 47.2 | 48.0 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 49.0 | 49.7 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 35.1 | 35.9 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 39.9 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.9 | 40.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.1 | 40.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 34.7 | 35.5 | 0 | 3 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.5 | 66.8 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.6 | 66.9 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 64.4 | 66.4 | 0 | 8 | 18.4 | 58 | pass |
| SEQ+ARP MOD1 | 64.5 | 66.8 | 0 | 8 | 5.5 | 127 | pass |
| LADDER FILTER PG | 35.1 | 35.7 | 0 | 6 | 57.6 | 1 | pass |
| HEAVY MOD+PP | 58.5 | 59.3 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 59.9 | 60.7 | 0 | 8 | 55.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 71.9 | 74.2 | 0 | 8 | 42.9 | 13 | FAIL |

### 2026-10-15 16:52, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.9 | 16.2 | 0 | 0 | 61.6 | 2 | pass |
| SINGLE 1V | 21.3 | 21.9 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 37.5 | 38.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.0 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 51.9 | 52.5 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 46.6 | 47.2 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FILTER | 38.1 | 38.7 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 38.7 | 39.3 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 34.2 | 34.8 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.4 | 61.2 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD HEAVY | 57.5 | 58.2 | 0 | 5 | 57.2 | 7 | pass |
| DUAL FLT HEAVY | 51.1 | 51.8 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 46.5 | 47.2 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 48.4 | 49.1 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 34.4 | 35.0 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 39.1 | 40.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.1 | 39.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.4 | 39.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 34.1 | 34.6 | 0 | 3 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 63.9 | 66.2 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.0 | 66.3 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP SEQ | 63.8 | 65.9 | 0 | 8 | 19.4 | 58 | pass |
| SEQ+ARP MOD1 | 64.0 | 65.7 | 0 | 8 | 5.7 | 127 | pass |
| LADDER FILTER PG | 34.4 | 35.0 | 0 | 6 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.6 | 58.3 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CELEST+PP | 58.8 | 59.5 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 70.8 | 73.3 | 0 | 8 | 42.9 | 12 | FAIL |

### 2026-10-15 16:46, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.1 | 16.5 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 21.6 | 22.2 | 0 | 1 | 61.3 | 1 | pass |
| SINGLE | 37.8 | 38.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.3 | 53.0 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 52.2 | 52.7 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 47.0 | 47.5 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FILTER | 38.4 | 39.1 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 39.1 | 39.7 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 34.6 | 35.3 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.7 | 61.3 | 0 | 5 | 55.2 | 1 | pass |
| SPREAD HEAVY | 57.8 | 58.5 | 0 | 5 | 57.2 | 7 | pass |
| DUAL FLT HEAVY | 51.5 | 52.2 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 46.9 | 47.5 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 48.7 | 49.3 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 34.7 | 35.5 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 39.5 | 40.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 39.5 | 40.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 39.8 | 40.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 34.5 | 35.1 | 0 | 3 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.3 | 66.4 | 0 | 8 | 52.2 | 20 | pass |
| SEQ+ARP AMP | 64.3 | 66.5 | 0 | 8 | 52.1 | 20 | pass |
| SEQ+ARP SEQ | 64.0 | 66.1 | 0 | 8 | 19.3 | 58 | pass |
| SEQ+ARP MOD1 | 64.2 | 66.4 | 0 | 8 | 5.7 | 127 | pass |
| LADDER FILTER PG | 34.7 | 35.4 | 0 | 6 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 64.1 | 65.2 | 0 | 8 | 52.2 | 8 | pass |
| HEAVY CELEST+PP | 65.2 | 66.1 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP CELEST+PP | 77.3 | 80.3 | 0 | 8 | 42.9 | 12 | FAIL |

### 2026-10-15 16:27, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.2 | 18.5 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 23.7 | 24.3 | 0 | 1 | 60.6 | 1 | pass |
| SINGLE | 39.8 | 40.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 54.3 | 54.9 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 54.2 | 54.9 | 0 | 5 | 57.2 | 1 | pass |
| SPREAD | 49.0 | 49.6 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FILTER | 40.3 | 40.9 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 41.0 | 41.6 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 36.6 | 37.2 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.7 | 63.3 | 0 | 5 | 51.8 | 1 | pass |
| SPREAD HEAVY | 60.0 | 60.6 | 0 | 5 | 56.9 | 8 | pass |
| DUAL FLT HEAVY | 53.3 | 54.0 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 48.9 | 49.5 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 50.7 | 51.3 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 36.8 | 37.3 | 0 | 6 | 57.3 | 1 | pass |
| SINGLE MS20 LOW | 41.7 | 42.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 41.7 | 42.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 41.7 | 42.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 36.3 | 36.9 | 0 | 3 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 66.3 | 68.6 | 0 | 8 | 50.2 | 20 | pass |
| SEQ+ARP AMP | 66.2 | 68.4 | 0 | 8 | 50.2 | 20 | pass |
| SEQ+ARP SEQ | 65.9 | 68.2 | 0 | 8 | 17.2 | 58 | pass |
| SEQ+ARP MOD1 | 66.4 | 68.9 | 0 | 8 | 5.3 | 127 | pass |
| LADDER FILTER PG | 36.8 | 37.5 | 0 | 6 | 57.3 | 1 | pass |

### 2026-10-15 16:23, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.3 | 18.5 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 23.6 | 24.2 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 43.5 | 44.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 61.0 | 61.9 | 0 | 8 | 55.5 | 8 | pass |
| DUAL OSC | 54.2 | 54.8 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 49.0 | 49.6 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FILTER | 40.3 | 41.0 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 41.0 | 41.7 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 36.6 | 37.2 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.7 | 63.4 | 0 | 5 | 51.8 | 8 | pass |
| SPREAD HEAVY | 59.9 | 60.6 | 0 | 5 | 55.9 | 8 | pass |
| DUAL FLT HEAVY | 53.3 | 54.0 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 48.9 | 49.7 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 50.7 | 51.4 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 36.8 | 37.4 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 45.7 | 46.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 45.7 | 46.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 45.9 | 46.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 36.4 | 37.0 | 0 | 3 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 73.2 | 75.6 | 0 | 8 | 42.9 | 13 | FAIL |
| SEQ+ARP AMP | 73.2 | 75.8 | 0 | 8 | 42.9 | 24 | FAIL |
| SEQ+ARP SEQ | 72.8 | 74.8 | 0 | 8 | 14.0 | 81 | FAIL |
| SEQ+ARP MOD1 | 73.5 | 76.1 | 0 | 8 | 4.3 | 127 | FAIL |
| LADDER FILTER PG | 36.8 | 37.4 | 0 | 6 | 57.6 | 1 | pass |

### 2026-10-15 16:19, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.5 | 19.9 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 24.7 | 25.2 | 0 | 1 | 61.0 | 4 | pass |
| SINGLE | 44.5 | 45.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 62.1 | 62.8 | 0 | 8 | 54.8 | 8 | pass |
| DUAL OSC | 55.4 | 55.9 | 0 | 5 | 57.6 | 1 | pass |
| SPREAD | 50.0 | 50.5 | 0 | 5 | 57.2 | 1 | pass |
| DUAL FILTER | 41.4 | 42.0 | 0 | 5 | 57.2 | 1 | pass |
| STACK | 42.1 | 42.8 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 37.8 | 38.4 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 63.8 | 64.5 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD HEAVY | 60.9 | 61.5 | 0 | 5 | 54.6 | 8 | pass |
| DUAL FLT HEAVY | 54.4 | 55.1 | 0 | 5 | 57.2 | 1 | pass |
| STACK HEAVY | 50.0 | 50.7 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL HEAVY | 51.9 | 52.5 | 0 | 4 | 57.2 | 1 | pass |
| SINGLE LADDER | 37.8 | 38.3 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 46.7 | 47.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 HIGH | 46.7 | 47.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 47.0 | 47.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 37.6 | 38.1 | 0 | 3 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 74.0 | 76.7 | 0 | 8 | 42.9 | 12 | FAIL |
| SEQ+ARP AMP | 74.1 | 76.3 | 0 | 8 | 42.9 | 24 | FAIL |
| SEQ+ARP SEQ | 73.7 | 75.8 | 0 | 8 | 12.7 | 82 | FAIL |
| SEQ+ARP MOD1 | 74.4 | 76.6 | 0 | 8 | 4.1 | 127 | FAIL |
| LADDER FILTER PG | 37.8 | 38.3 | 0 | 6 | 57.6 | 1 | pass |

### 2026-10-15 16:10, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.6 | 19.9 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 29.9 | 30.3 | 0 | 1 | 58.3 | 4 | pass |
| SINGLE | 68.7 | 69.5 | 0 | 8 | 42.9 | 1 | pass |
| SINGLE HEAVY | 80.7 | 81.5 | 0 | 8 | 42.9 | 1 | FAIL |
| DUAL OSC | 62.4 | 62.9 | 0 | 5 | 53.2 | 8 | pass |
| SPREAD | 61.3 | 61.8 | 0 | 5 | 54.2 | 8 | pass |
| DUAL FILTER | 58.4 | 58.9 | 0 | 5 | 56.9 | 7 | pass |
| STACK | 54.4 | 55.0 | 0 | 4 | 57.2 | 1 | pass |
| BINAURAL | 54.0 | 54.6 | 0 | 4 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 68.7 | 69.2 | 0 | 5 | 42.9 | 1 | pass |
| SPREAD HEAVY | 68.9 | 69.4 | 0 | 5 | 42.9 | 1 | pass |
| DUAL FLT HEAVY | 68.6 | 69.1 | 0 | 5 | 42.9 | 1 | pass |
| STACK HEAVY | 60.3 | 60.9 | 0 | 4 | 53.6 | 8 | pass |
| BINAURAL HEAVY | 63.7 | 64.3 | 0 | 4 | 51.9 | 8 | pass |
| SINGLE LADDER | 52.7 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE MS20 LOW | 71.5 | 72.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE MS20 HIGH | 71.2 | 72.0 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE FORMANT | 71.5 | 72.1 | 0 | 8 | 42.9 | 1 | FAIL |
| DUAL FLT LADDER | 48.6 | 49.0 | 0 | 3 | 57.3 | 1 | pass |
| SEQ+ARP HOME | 89.9 | 92.2 | 0 | 8 | 42.9 | 24 | FAIL |
| SEQ+ARP AMP | 89.7 | 91.3 | 0 | 8 | 30.1 | 46 | FAIL |
| SEQ+ARP SEQ | 89.5 | 91.4 | 0 | 8 | 6.4 | 127 | FAIL |
| SEQ+ARP MOD1 | 90.3 | 92.4 | 0 | 8 | 1.6 | 127 | FAIL |
| LADDER FILTER PG | 52.9 | 53.3 | 0 | 6 | 57.2 | 1 | pass |

### 2026-10-15 15:41, CARNAC OS PROF, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.0 | 21.3 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 36.5 | 36.9 | 0 | 1 | 57.3 | 1 | pass |
| SINGLE | 146.6 | 148.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 167.2 | 169.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 102.5 | 104.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 117.2 | 118.7 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 96.2 | 96.6 | 0 | 5 | 42.5 | 1 | FAIL |
| STACK | 98.3 | 98.8 | 0 | 4 | 42.5 | 12 | FAIL |
| BINAURAL | 97.6 | 98.1 | 0 | 4 | 42.2 | 12 | FAIL |
| DUAL OSC HEAVY | 113.6 | 114.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 130.3 | 131.7 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 113.5 | 115.1 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 110.9 | 112.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 117.7 | 119.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 119.6 | 121.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 149.6 | 151.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 150.9 | 152.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 150.6 | 152.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 70.4 | 70.8 | 0 | 3 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME | 176.0 | 177.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 175.6 | 176.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 175.9 | 176.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 177.0 | 178.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 119.6 | 122.3 | 8 | 6 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice for the voice stages, a 16th of that for CTRL and its c.* split; 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 111 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 50 | 9 | 29 | 6 | 0 | 0 | 0 | 0 | 0 | 17 | 0 |
| SINGLE 1V | 1 | 196 | 16 | 57 | 10 | 11 | 13 | 6 | 2 | 50 | 9 | 29 | 6 | 3 | 2 | 4 | 2 | 1 | 27 | 75 |
| SINGLE | 8 | 795 | 130 | 463 | 87 | 95 | 113 | 53 | 18 | 52 | 10 | 29 | 7 | 28 | 22 | 38 | 18 | 8 | 86 | 76 |
| SINGLE HEAVY | 8 | 907 | 170 | 537 | 88 | 145 | 138 | 53 | 19 | 50 | 10 | 29 | 7 | 30 | 22 | 36 | 58 | 10 | 85 | 91 |
| DUAL OSC | 5 | 555 | 97 | 289 | 0 | 0 | 0 | 0 | 12 | 52 | 9 | 29 | 7 | 19 | 14 | 39 | 12 | 6 | 60 | 80 |
| SPREAD | 5 | 635 | 93 | 373 | 139 | 59 | 70 | 33 | 12 | 52 | 10 | 29 | 7 | 19 | 15 | 34 | 12 | 6 | 59 | 96 |
| DUAL FILTER | 5 | 520 | 94 | 259 | 0 | 0 | 0 | 0 | 12 | 50 | 10 | 29 | 6 | 18 | 13 | 23 | 27 | 6 | 60 | 73 |
| STACK | 4 | 532 | 86 | 290 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 6 | 15 | 10 | 29 | 18 | 7 | 52 | 96 |
| BINAURAL | 4 | 528 | 86 | 286 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 6 | 15 | 10 | 18 | 32 | 5 | 52 | 95 |
| DUAL OSC HEAVY | 5 | 615 | 120 | 328 | 0 | 0 | 0 | 0 | 11 | 50 | 9 | 29 | 6 | 18 | 14 | 38 | 35 | 6 | 62 | 92 |
| SPREAD HEAVY | 5 | 706 | 116 | 422 | 139 | 91 | 87 | 33 | 12 | 50 | 9 | 30 | 7 | 18 | 15 | 34 | 36 | 6 | 60 | 110 |
| DUAL FLT HEAVY | 5 | 615 | 121 | 327 | 0 | 0 | 0 | 0 | 12 | 50 | 10 | 29 | 6 | 18 | 15 | 23 | 52 | 6 | 60 | 92 |
| STACK HEAVY | 4 | 600 | 108 | 335 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 30 | 6 | 14 | 12 | 30 | 38 | 7 | 52 | 113 |
| BINAURAL HEAVY | 4 | 638 | 116 | 364 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 6 | 15 | 11 | 19 | 60 | 5 | 54 | 122 |
| SINGLE LADDER | 6 | 648 | 101 | 368 | 65 | 70 | 106 | 39 | 14 | 50 | 10 | 29 | 7 | 22 | 17 | 28 | 17 | 7 | 69 | 80 |
| SINGLE MS20 LOW | 8 | 811 | 130 | 482 | 88 | 94 | 135 | 53 | 19 | 52 | 10 | 29 | 7 | 28 | 24 | 36 | 18 | 8 | 82 | 79 |
| SINGLE MS20 HIGH | 8 | 818 | 130 | 486 | 87 | 94 | 135 | 56 | 19 | 51 | 10 | 30 | 7 | 30 | 22 | 38 | 20 | 10 | 85 | 79 |
| SINGLE FORMANT | 8 | 817 | 132 | 487 | 88 | 94 | 138 | 56 | 19 | 50 | 10 | 29 | 7 | 30 | 24 | 38 | 20 | 8 | 83 | 80 |
| DUAL FLT LADDER | 3 | 380 | 60 | 177 | 0 | 0 | 0 | 0 | 7 | 50 | 10 | 29 | 6 | 11 | 7 | 14 | 19 | 3 | 41 | 81 |
| SEQ+ARP HOME | 8 | 954 | 182 | 536 | 90 | 144 | 138 | 53 | 19 | 50 | 11 | 133 | 8 | 30 | 30 | 38 | 60 | 10 | 15 | 92 |
| SEQ+ARP AMP | 8 | 952 | 180 | 536 | 87 | 145 | 138 | 53 | 19 | 50 | 11 | 131 | 8 | 30 | 30 | 38 | 58 | 10 | 17 | 92 |
| SEQ+ARP SEQ | 8 | 954 | 180 | 540 | 90 | 145 | 138 | 53 | 19 | 50 | 11 | 131 | 7 | 32 | 30 | 38 | 58 | 10 | 16 | 92 |
| SEQ+ARP MOD1 | 8 | 960 | 184 | 539 | 90 | 145 | 138 | 52 | 19 | 50 | 10 | 132 | 10 | 30 | 30 | 40 | 58 | 10 | 16 | 93 |
| LADDER FILTER PG | 6 | 648 | 102 | 367 | 68 | 71 | 106 | 40 | 14 | 51 | 10 | 29 | 7 | 22 | 18 | 28 | 18 | 7 | 68 | 80 |

### 2026-10-15 15:40, CARNAC OS DEV, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.0 | 19.4 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 29.2 | 29.6 | 0 | 1 | 58.0 | 1 | pass |
| SINGLE | 101.5 | 103.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 122.3 | 124.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 89.3 | 89.8 | 0 | 5 | 42.9 | 1 | FAIL |
| SPREAD | 87.7 | 88.2 | 0 | 5 | 43.2 | 1 | FAIL |
| DUAL FILTER | 83.3 | 83.8 | 0 | 5 | 43.2 | 1 | FAIL |
| STACK | 87.9 | 88.7 | 0 | 4 | 42.9 | 1 | FAIL |
| BINAURAL | 87.1 | 87.6 | 0 | 4 | 43.2 | 1 | FAIL |
| DUAL OSC HEAVY | 100.6 | 102.3 | 13 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 101.3 | 102.7 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 100.9 | 102.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 99.5 | 100.0 | 2 | 4 | 16.4 | 47 | FAIL |
| BINAURAL HEAVY | 106.9 | 107.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 84.7 | 85.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE MS20 LOW | 105.9 | 107.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 104.7 | 106.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 106.3 | 108.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 62.3 | 62.8 | 0 | 3 | 54.2 | 8 | pass |
| SEQ+ARP HOME | 131.4 | 132.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 131.4 | 132.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 130.6 | 131.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 131.7 | 133.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 84.7 | 85.2 | 0 | 6 | 42.9 | 1 | FAIL |

### 2026-10-15 15:34, CARNAC OS PROF, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.0 | 21.3 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 36.5 | 36.9 | 0 | 1 | 57.2 | 1 | pass |
| SINGLE | 146.2 | 148.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 167.3 | 168.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 102.8 | 104.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 117.3 | 118.7 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 96.2 | 96.6 | 0 | 5 | 41.5 | 1 | FAIL |
| STACK | 98.5 | 99.0 | 0 | 4 | 40.0 | 12 | FAIL |
| BINAURAL | 97.6 | 98.1 | 0 | 4 | 41.9 | 12 | FAIL |
| DUAL OSC HEAVY | 113.6 | 114.6 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 130.5 | 131.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 113.4 | 114.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 110.9 | 112.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 117.6 | 119.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 120.1 | 122.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 151.4 | 153.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 151.7 | 153.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 151.9 | 153.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 70.8 | 71.3 | 0 | 3 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME | 176.5 | 178.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 175.9 | 177.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 175.2 | 175.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 176.4 | 177.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 120.1 | 122.9 | 8 | 6 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice for the voice stages, a 16th of that for CTRL and its c.* split; 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 111 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 50 | 9 | 29 | 6 | 0 | 0 | 0 | 0 | 0 | 17 | 0 |
| SINGLE 1V | 1 | 196 | 16 | 57 | 10 | 11 | 13 | 6 | 2 | 50 | 9 | 29 | 6 | 3 | 2 | 5 | 2 | 1 | 27 | 75 |
| SINGLE | 8 | 793 | 130 | 459 | 87 | 93 | 112 | 53 | 19 | 52 | 10 | 29 | 7 | 28 | 22 | 40 | 18 | 8 | 87 | 76 |
| SINGLE HEAVY | 8 | 907 | 170 | 537 | 87 | 145 | 138 | 53 | 19 | 51 | 10 | 29 | 7 | 30 | 24 | 40 | 56 | 8 | 84 | 91 |
| DUAL OSC | 5 | 556 | 99 | 289 | 0 | 0 | 0 | 0 | 12 | 52 | 10 | 29 | 7 | 18 | 15 | 41 | 11 | 6 | 58 | 80 |
| SPREAD | 5 | 635 | 93 | 372 | 139 | 59 | 69 | 33 | 12 | 52 | 10 | 29 | 7 | 18 | 14 | 36 | 12 | 6 | 60 | 95 |
| DUAL FILTER | 5 | 521 | 95 | 259 | 0 | 0 | 0 | 0 | 12 | 50 | 10 | 29 | 6 | 18 | 13 | 24 | 26 | 5 | 60 | 73 |
| STACK | 4 | 533 | 87 | 290 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 6 | 14 | 10 | 31 | 18 | 7 | 52 | 96 |
| BINAURAL | 4 | 528 | 86 | 286 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 6 | 14 | 10 | 19 | 31 | 5 | 52 | 95 |
| DUAL OSC HEAVY | 5 | 615 | 121 | 326 | 0 | 0 | 0 | 0 | 12 | 50 | 10 | 29 | 7 | 18 | 15 | 40 | 34 | 6 | 60 | 92 |
| SPREAD HEAVY | 5 | 707 | 117 | 423 | 140 | 91 | 86 | 33 | 11 | 50 | 10 | 29 | 6 | 18 | 15 | 36 | 34 | 6 | 61 | 110 |
| DUAL FLT HEAVY | 5 | 614 | 121 | 325 | 0 | 0 | 0 | 0 | 11 | 51 | 10 | 29 | 7 | 18 | 14 | 24 | 51 | 6 | 60 | 91 |
| STACK HEAVY | 4 | 601 | 108 | 336 | 0 | 0 | 0 | 0 | 9 | 50 | 9 | 29 | 7 | 15 | 11 | 31 | 37 | 7 | 53 | 113 |
| BINAURAL HEAVY | 4 | 637 | 116 | 365 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 7 | 15 | 11 | 20 | 59 | 5 | 51 | 122 |
| SINGLE LADDER | 6 | 650 | 101 | 370 | 68 | 70 | 106 | 40 | 14 | 50 | 10 | 29 | 6 | 21 | 17 | 29 | 17 | 7 | 70 | 81 |
| SINGLE MS20 LOW | 8 | 821 | 130 | 487 | 90 | 95 | 135 | 53 | 19 | 53 | 10 | 29 | 7 | 28 | 22 | 38 | 18 | 8 | 86 | 80 |
| SINGLE MS20 HIGH | 8 | 822 | 130 | 490 | 91 | 94 | 135 | 57 | 19 | 50 | 10 | 29 | 7 | 28 | 22 | 38 | 18 | 8 | 87 | 80 |
| SINGLE FORMANT | 8 | 823 | 132 | 491 | 87 | 95 | 138 | 57 | 19 | 50 | 10 | 29 | 7 | 28 | 24 | 40 | 20 | 8 | 85 | 80 |
| DUAL FLT LADDER | 3 | 382 | 60 | 177 | 0 | 0 | 0 | 0 | 7 | 50 | 10 | 29 | 6 | 11 | 7 | 14 | 19 | 3 | 43 | 81 |
| SEQ+ARP HOME | 8 | 957 | 180 | 539 | 89 | 145 | 138 | 53 | 19 | 50 | 11 | 134 | 8 | 30 | 30 | 40 | 56 | 10 | 16 | 92 |
| SEQ+ARP AMP | 8 | 954 | 180 | 539 | 89 | 144 | 139 | 53 | 19 | 50 | 11 | 132 | 8 | 30 | 30 | 40 | 58 | 10 | 15 | 92 |
| SEQ+ARP SEQ | 8 | 950 | 180 | 535 | 87 | 144 | 138 | 52 | 20 | 50 | 10 | 131 | 7 | 30 | 30 | 40 | 56 | 8 | 17 | 92 |
| SEQ+ARP MOD1 | 8 | 956 | 182 | 535 | 87 | 145 | 138 | 53 | 19 | 50 | 11 | 131 | 10 | 30 | 30 | 40 | 56 | 8 | 18 | 92 |
| LADDER FILTER PG | 6 | 650 | 103 | 370 | 68 | 70 | 106 | 40 | 14 | 50 | 10 | 30 | 7 | 22 | 17 | 30 | 17 | 7 | 66 | 81 |

### 2026-10-15 15:30, CARNAC OS PROF, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 20.9 | 21.2 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 36.2 | 36.6 | 0 | 1 | 57.6 | 1 | pass |
| SINGLE | 144.2 | 146.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 165.8 | 167.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 101.5 | 103.1 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 116.0 | 117.5 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 95.0 | 95.4 | 0 | 5 | 42.9 | 1 | FAIL |
| STACK | 97.3 | 97.8 | 0 | 4 | 43.2 | 12 | FAIL |
| BINAURAL | 96.2 | 96.7 | 0 | 4 | 42.9 | 1 | FAIL |
| DUAL OSC HEAVY | 112.3 | 113.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 129.2 | 131.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 112.3 | 113.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 110.0 | 111.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 116.6 | 118.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 118.0 | 120.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 148.8 | 150.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 148.9 | 150.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 150.5 | 152.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 69.9 | 70.4 | 0 | 3 | 43.2 | 9 | pass |
| SEQ+ARP HOME | 174.4 | 176.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 174.0 | 174.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 173.3 | 174.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 175.0 | 176.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 118.1 | 121.0 | 8 | 6 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice for the voice stages, an 8th of that for CTRL; 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 111 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 50 | 9 | 29 | 6 | 17 | 0 |
| SINGLE 1V | 1 | 194 | 14 | 57 | 10 | 11 | 13 | 6 | 2 | 50 | 9 | 29 | 6 | 27 | 73 |
| SINGLE | 8 | 782 | 120 | 458 | 87 | 94 | 110 | 53 | 19 | 52 | 10 | 29 | 7 | 87 | 75 |
| SINGLE HEAVY | 8 | 899 | 157 | 541 | 87 | 145 | 141 | 53 | 19 | 50 | 9 | 29 | 7 | 87 | 90 |
| DUAL OSC | 5 | 549 | 91 | 287 | 0 | 0 | 0 | 0 | 12 | 52 | 9 | 29 | 7 | 62 | 78 |
| SPREAD | 5 | 628 | 86 | 373 | 140 | 59 | 69 | 33 | 11 | 51 | 10 | 29 | 7 | 61 | 94 |
| DUAL FILTER | 5 | 514 | 87 | 259 | 0 | 0 | 0 | 0 | 12 | 50 | 10 | 29 | 6 | 61 | 72 |
| STACK | 4 | 526 | 82 | 289 | 0 | 0 | 0 | 0 | 9 | 50 | 9 | 29 | 6 | 52 | 95 |
| BINAURAL | 4 | 520 | 80 | 283 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 6 | 53 | 93 |
| DUAL OSC HEAVY | 5 | 608 | 113 | 326 | 0 | 0 | 0 | 0 | 11 | 50 | 10 | 29 | 7 | 62 | 90 |
| SPREAD HEAVY | 5 | 700 | 109 | 423 | 139 | 90 | 88 | 33 | 11 | 50 | 10 | 30 | 6 | 61 | 109 |
| DUAL FLT HEAVY | 5 | 608 | 113 | 327 | 0 | 0 | 0 | 0 | 11 | 50 | 10 | 29 | 6 | 62 | 90 |
| STACK HEAVY | 4 | 595 | 104 | 335 | 0 | 0 | 0 | 0 | 9 | 50 | 10 | 29 | 6 | 52 | 112 |
| BINAURAL HEAVY | 4 | 631 | 110 | 365 | 0 | 0 | 0 | 0 | 9 | 50 | 9 | 30 | 6 | 52 | 121 |
| SINGLE LADDER | 6 | 639 | 93 | 367 | 65 | 70 | 107 | 40 | 14 | 50 | 10 | 29 | 6 | 70 | 79 |
| SINGLE MS20 LOW | 8 | 806 | 120 | 484 | 87 | 94 | 135 | 53 | 19 | 52 | 10 | 29 | 7 | 85 | 78 |
| SINGLE MS20 HIGH | 8 | 807 | 120 | 485 | 87 | 94 | 135 | 56 | 19 | 51 | 9 | 29 | 7 | 87 | 78 |
| SINGLE FORMANT | 8 | 816 | 121 | 494 | 88 | 94 | 142 | 57 | 19 | 50 | 10 | 29 | 7 | 86 | 79 |
| DUAL FLT LADDER | 3 | 377 | 56 | 177 | 0 | 0 | 0 | 0 | 7 | 50 | 9 | 29 | 6 | 43 | 80 |
| SEQ+ARP HOME | 8 | 946 | 169 | 539 | 87 | 145 | 140 | 53 | 19 | 50 | 10 | 133 | 7 | 19 | 91 |
| SEQ+ARP AMP | 8 | 944 | 169 | 537 | 87 | 145 | 139 | 53 | 19 | 51 | 10 | 132 | 8 | 18 | 91 |
| SEQ+ARP SEQ | 8 | 940 | 168 | 537 | 87 | 144 | 138 | 53 | 19 | 50 | 10 | 130 | 7 | 19 | 90 |
| SEQ+ARP MOD1 | 8 | 949 | 172 | 539 | 86 | 144 | 141 | 53 | 19 | 50 | 10 | 131 | 10 | 18 | 91 |
| LADDER FILTER PG | 6 | 640 | 93 | 368 | 65 | 71 | 105 | 40 | 13 | 50 | 10 | 29 | 6 | 71 | 79 |

### 2026-10-15 15:28, CARNAC OS DEV, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.0 | 19.4 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 29.2 | 29.6 | 0 | 1 | 58.0 | 1 | pass |
| SINGLE | 101.9 | 104.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 122.5 | 124.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 89.6 | 90.1 | 0 | 5 | 42.9 | 1 | FAIL |
| SPREAD | 87.9 | 88.4 | 0 | 5 | 43.2 | 1 | FAIL |
| DUAL FILTER | 83.6 | 84.1 | 0 | 5 | 43.2 | 1 | FAIL |
| STACK | 88.3 | 88.9 | 0 | 4 | 42.9 | 1 | FAIL |
| BINAURAL | 87.1 | 87.6 | 0 | 4 | 43.2 | 1 | FAIL |
| DUAL OSC HEAVY | 100.9 | 102.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 101.1 | 102.5 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 101.1 | 102.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 100.0 | 100.5 | 103 | 4 | 37.6 | 59 | FAIL |
| BINAURAL HEAVY | 107.1 | 108.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 84.9 | 85.5 | 0 | 6 | 43.2 | 1 | FAIL |
| SINGLE MS20 LOW | 106.0 | 107.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 105.4 | 107.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 106.6 | 108.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 62.4 | 62.9 | 0 | 3 | 53.2 | 8 | pass |
| SEQ+ARP HOME | 131.5 | 132.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 131.3 | 132.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 131.0 | 132.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 132.0 | 133.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 84.8 | 85.3 | 0 | 6 | 43.2 | 1 | FAIL |

### 2026-10-15 15:14, CARNAC OS PROF, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 23.3 | 23.6 | 0 | 0 | 60.9 | 3 | pass |
| SINGLE 1V | 40.2 | 40.8 | 0 | 1 | 57.6 | 1 | pass |
| SINGLE | 158.5 | 160.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 179.5 | 181.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 109.8 | 111.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 125.7 | 127.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 104.3 | 106.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 105.8 | 107.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL | 103.4 | 105.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 120.7 | 123.0 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 138.7 | 140.6 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 121.2 | 123.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 117.5 | 119.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 122.8 | 124.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 128.2 | 130.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 161.9 | 163.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 161.3 | 163.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 162.0 | 165.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 75.9 | 76.4 | 0 | 3 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME | 184.3 | 186.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 183.8 | 184.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 183.4 | 184.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 183.7 | 184.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 128.9 | 131.9 | 8 | 6 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice for the voice stages, an 8th of that for CTRL; 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 124 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 48 | 9 | 45 | 6 | 16 | 0 |
| SINGLE 1V | 1 | 216 | 14 | 62 | 11 | 11 | 14 | 8 | 5 | 48 | 10 | 45 | 6 | 26 | 81 |
| SINGLE | 8 | 859 | 118 | 498 | 87 | 94 | 117 | 71 | 45 | 50 | 10 | 45 | 7 | 86 | 83 |
| SINGLE HEAVY | 8 | 974 | 155 | 575 | 88 | 145 | 145 | 70 | 46 | 49 | 10 | 45 | 7 | 87 | 97 |
| DUAL OSC | 5 | 595 | 90 | 305 | 0 | 0 | 0 | 0 | 28 | 50 | 10 | 45 | 7 | 60 | 85 |
| SPREAD | 5 | 681 | 85 | 396 | 140 | 59 | 74 | 43 | 28 | 50 | 10 | 45 | 7 | 60 | 102 |
| DUAL FILTER | 5 | 565 | 88 | 277 | 0 | 0 | 0 | 0 | 29 | 48 | 10 | 45 | 7 | 61 | 79 |
| STACK | 4 | 573 | 84 | 305 | 0 | 0 | 0 | 0 | 23 | 48 | 10 | 45 | 7 | 51 | 103 |
| BINAURAL | 4 | 560 | 82 | 293 | 0 | 0 | 0 | 0 | 23 | 48 | 10 | 46 | 7 | 51 | 100 |
| DUAL OSC HEAVY | 5 | 654 | 114 | 343 | 0 | 0 | 0 | 0 | 29 | 48 | 10 | 44 | 7 | 59 | 97 |
| SPREAD HEAVY | 5 | 752 | 108 | 444 | 139 | 91 | 91 | 44 | 29 | 48 | 10 | 45 | 7 | 61 | 116 |
| DUAL FLT HEAVY | 5 | 656 | 111 | 348 | 0 | 0 | 0 | 0 | 29 | 48 | 9 | 45 | 7 | 59 | 98 |
| STACK HEAVY | 4 | 636 | 102 | 349 | 0 | 0 | 0 | 0 | 23 | 49 | 10 | 45 | 7 | 51 | 118 |
| BINAURAL HEAVY | 4 | 665 | 108 | 374 | 0 | 0 | 0 | 0 | 23 | 49 | 10 | 45 | 7 | 49 | 126 |
| SINGLE LADDER | 6 | 694 | 91 | 391 | 66 | 71 | 109 | 52 | 35 | 48 | 9 | 45 | 7 | 68 | 86 |
| SINGLE MS20 LOW | 8 | 878 | 117 | 517 | 87 | 95 | 138 | 70 | 46 | 50 | 10 | 45 | 7 | 86 | 85 |
| SINGLE MS20 HIGH | 8 | 875 | 117 | 515 | 88 | 95 | 138 | 69 | 46 | 48 | 10 | 45 | 7 | 87 | 85 |
| SINGLE FORMANT | 8 | 879 | 119 | 518 | 87 | 94 | 145 | 70 | 46 | 48 | 10 | 45 | 7 | 86 | 85 |
| DUAL FLT LADDER | 3 | 410 | 55 | 188 | 0 | 0 | 0 | 0 | 17 | 48 | 9 | 45 | 6 | 42 | 87 |
| SEQ+ARP HOME | 8 | 1000 | 168 | 572 | 88 | 144 | 145 | 70 | 45 | 49 | 11 | 134 | 8 | 13 | 98 |
| SEQ+ARP AMP | 8 | 997 | 166 | 572 | 86 | 145 | 145 | 70 | 46 | 48 | 11 | 134 | 8 | 12 | 98 |
| SEQ+ARP SEQ | 8 | 995 | 165 | 571 | 87 | 145 | 145 | 70 | 47 | 48 | 10 | 133 | 7 | 14 | 98 |
| SEQ+ARP MOD1 | 8 | 997 | 169 | 569 | 88 | 144 | 143 | 70 | 46 | 48 | 10 | 132 | 10 | 13 | 98 |
| LADDER FILTER PG | 6 | 698 | 90 | 394 | 66 | 70 | 109 | 53 | 35 | 48 | 10 | 45 | 7 | 69 | 86 |

### 2026-10-15 15:05, CARNAC OS DEV, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.3 | 21.6 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 32.9 | 33.3 | 0 | 1 | 57.3 | 1 | pass |
| SINGLE | 115.0 | 117.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 135.6 | 137.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 98.1 | 98.6 | 0 | 5 | 38.1 | 12 | FAIL |
| SPREAD | 97.2 | 97.7 | 0 | 5 | 43.2 | 12 | FAIL |
| DUAL FILTER | 92.7 | 93.2 | 0 | 5 | 42.9 | 1 | FAIL |
| STACK | 95.8 | 96.3 | 0 | 4 | 42.9 | 12 | FAIL |
| BINAURAL | 93.2 | 93.9 | 0 | 4 | 42.9 | 1 | FAIL |
| DUAL OSC HEAVY | 109.7 | 110.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 110.7 | 112.1 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 110.1 | 111.5 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 108.2 | 109.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 113.4 | 114.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 95.3 | 95.9 | 0 | 6 | 43.2 | 1 | FAIL |
| SINGLE MS20 LOW | 119.1 | 120.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 118.2 | 120.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 119.5 | 121.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 68.7 | 69.2 | 0 | 3 | 42.9 | 1 | pass |
| SEQ+ARP HOME | 141.4 | 142.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 141.4 | 142.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 140.5 | 141.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 141.4 | 142.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 95.2 | 95.9 | 0 | 6 | 43.2 | 1 | FAIL |

### 2026-10-15 13:54, CARNAC OS DEV, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.4 | 21.7 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 33.9 | 34.4 | 0 | 1 | 57.3 | 1 | pass |
| SINGLE | 120.7 | 122.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 140.4 | 142.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 102.6 | 103.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 109.5 | 110.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 100.7 | 102.0 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 98.7 | 99.6 | 0 | 4 | 38.2 | 12 | FAIL |
| BINAURAL | 100.8 | 101.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 116.8 | 118.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 121.8 | 123.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 115.7 | 117.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 112.9 | 114.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 118.7 | 119.9 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 100.7 | 102.6 | 13 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 124.0 | 125.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 123.0 | 124.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 125.1 | 126.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 73.5 | 74.1 | 0 | 3 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME | 146.1 | 147.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 145.8 | 146.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 145.2 | 145.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 146.5 | 147.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 100.9 | 103.5 | 8 | 6 | – | – | FAIL (cut by the guard) |

### 2026-10-15 13:03, CARNAC OS PROF, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 25.5 | 25.8 | 0 | 0 | 60.0 | 1 | pass |
| SINGLE 1V | 45.9 | 46.4 | 0 | 1 | 57.2 | 1 | pass |
| SINGLE | 189.3 | 192.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 215.5 | 218.3 | 0 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 132.7 | 134.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 155.3 | 157.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 130.9 | 132.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 125.8 | 127.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL | 127.3 | 129.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 151.0 | 153.1 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 171.8 | 173.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 149.6 | 151.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 142.2 | 143.9 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 149.6 | 151.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 154.5 | 156.7 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 194.1 | 197.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 192.8 | 195.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 193.3 | 196.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 94.3 | 94.9 | 0 | 3 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME | 221.7 | 223.2 | 0 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 221.7 | 223.3 | 0 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 221.9 | 223.1 | 0 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 223.9 | 226.0 | 0 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 154.4 | 157.7 | 8 | 6 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice for the voice stages, an 8th of that for CTRL; 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 136 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 49 | 10 | 45 | 14 | 18 | 0 |
| SINGLE 1V | 1 | 247 | 28 | 65 | 15 | 10 | 15 | 8 | 8 | 49 | 10 | 45 | 13 | 29 | 101 |
| SINGLE | 8 | 1027 | 229 | 523 | 124 | 87 | 122 | 69 | 63 | 52 | 10 | 45 | 15 | 90 | 102 |
| SINGLE HEAVY | 8 | 1170 | 298 | 600 | 125 | 138 | 146 | 70 | 63 | 49 | 10 | 45 | 15 | 90 | 120 |
| DUAL OSC | 5 | 719 | 168 | 327 | 0 | 0 | 0 | 0 | 39 | 51 | 10 | 45 | 14 | 65 | 107 |
| SPREAD | 5 | 842 | 157 | 461 | 209 | 55 | 76 | 44 | 39 | 51 | 10 | 45 | 14 | 65 | 131 |
| DUAL FILTER | 5 | 709 | 168 | 316 | 0 | 0 | 0 | 0 | 40 | 52 | 10 | 45 | 14 | 64 | 105 |
| STACK | 4 | 682 | 152 | 322 | 0 | 0 | 0 | 0 | 31 | 51 | 10 | 46 | 14 | 56 | 126 |
| BINAURAL | 4 | 690 | 152 | 330 | 0 | 0 | 0 | 0 | 31 | 52 | 10 | 45 | 14 | 56 | 128 |
| DUAL OSC HEAVY | 5 | 819 | 212 | 384 | 0 | 0 | 0 | 0 | 39 | 49 | 10 | 45 | 14 | 66 | 127 |
| SPREAD HEAVY | 5 | 932 | 200 | 508 | 209 | 87 | 90 | 44 | 39 | 49 | 10 | 45 | 14 | 67 | 149 |
| DUAL FLT HEAVY | 5 | 811 | 210 | 379 | 0 | 0 | 0 | 0 | 39 | 49 | 10 | 45 | 14 | 65 | 126 |
| STACK HEAVY | 4 | 771 | 186 | 379 | 0 | 0 | 0 | 0 | 32 | 50 | 10 | 45 | 14 | 55 | 149 |
| BINAURAL HEAVY | 4 | 811 | 200 | 405 | 0 | 0 | 0 | 0 | 32 | 49 | 10 | 45 | 14 | 56 | 159 |
| SINGLE LADDER | 6 | 837 | 175 | 423 | 93 | 65 | 118 | 53 | 47 | 49 | 10 | 45 | 14 | 74 | 108 |
| SINGLE MS20 LOW | 8 | 1053 | 235 | 544 | 124 | 87 | 138 | 70 | 63 | 52 | 10 | 45 | 15 | 89 | 105 |
| SINGLE MS20 HIGH | 8 | 1046 | 234 | 539 | 126 | 87 | 134 | 71 | 63 | 50 | 10 | 45 | 15 | 90 | 104 |
| SINGLE FORMANT | 8 | 1049 | 228 | 548 | 124 | 87 | 144 | 70 | 63 | 50 | 10 | 45 | 15 | 90 | 105 |
| DUAL FLT LADDER | 3 | 510 | 103 | 220 | 0 | 0 | 0 | 0 | 23 | 49 | 10 | 45 | 14 | 46 | 115 |
| SEQ+ARP HOME | 8 | 1203 | 315 | 597 | 124 | 139 | 142 | 69 | 63 | 50 | 11 | 110 | 16 | 41 | 122 |
| SEQ+ARP AMP | 8 | 1203 | 313 | 598 | 125 | 138 | 141 | 70 | 64 | 50 | 10 | 110 | 16 | 42 | 122 |
| SEQ+ARP SEQ | 8 | 1204 | 313 | 600 | 123 | 138 | 144 | 69 | 63 | 50 | 11 | 109 | 16 | 42 | 122 |
| SEQ+ARP MOD1 | 8 | 1216 | 321 | 598 | 124 | 138 | 144 | 70 | 63 | 50 | 11 | 109 | 22 | 42 | 123 |
| LADDER FILTER PG | 6 | 837 | 175 | 423 | 94 | 65 | 117 | 52 | 47 | 49 | 10 | 45 | 15 | 73 | 108 |

### 2026-10-15 12:59, CARNAC OS PROF, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 23.3 | 23.7 | 0 | 0 | 61.0 | 3 | pass |
| SINGLE 1V | 43.8 | 44.3 | 0 | 1 | 57.3 | 1 | pass |
| SINGLE | 187.6 | 190.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 214.2 | 216.7 | 0 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 130.7 | 132.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 153.4 | 155.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 128.9 | 130.6 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 123.9 | 125.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL | 125.1 | 127.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 149.0 | 151.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 169.8 | 171.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 147.8 | 149.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 140.5 | 142.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 147.9 | 149.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 152.4 | 154.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 192.0 | 194.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 190.7 | 193.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 190.9 | 194.4 | 7 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 92.1 | 92.8 | 0 | 3 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME | 220.3 | 221.9 | 0 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 220.2 | 221.8 | 0 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 219.1 | 220.7 | 0 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 221.9 | 223.8 | 0 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 152.3 | 155.5 | 8 | 6 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice for the voice stages, an 8th of that for CTRL; 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 124 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 48 | 76 | 0 |
| SINGLE 1V | 1 | 235 | 28 | 65 | 15 | 10 | 15 | 8 | 8 | 49 | 85 | 101 |
| SINGLE | 8 | 1018 | 229 | 525 | 125 | 87 | 121 | 71 | 63 | 51 | 150 | 102 |
| SINGLE HEAVY | 8 | 1163 | 298 | 603 | 125 | 138 | 145 | 70 | 64 | 49 | 149 | 121 |
| DUAL OSC | 5 | 708 | 169 | 326 | 0 | 0 | 0 | 0 | 39 | 51 | 123 | 107 |
| SPREAD | 5 | 832 | 157 | 460 | 210 | 54 | 76 | 44 | 40 | 50 | 125 | 131 |
| DUAL FILTER | 5 | 698 | 168 | 318 | 0 | 0 | 0 | 0 | 39 | 51 | 122 | 105 |
| STACK | 4 | 671 | 154 | 321 | 0 | 0 | 0 | 0 | 31 | 51 | 114 | 126 |
| BINAURAL | 4 | 678 | 152 | 329 | 0 | 0 | 0 | 0 | 32 | 51 | 114 | 128 |
| DUAL OSC HEAVY | 5 | 808 | 212 | 385 | 0 | 0 | 0 | 0 | 40 | 49 | 122 | 127 |
| SPREAD HEAVY | 5 | 921 | 200 | 509 | 211 | 86 | 90 | 43 | 39 | 49 | 124 | 150 |
| DUAL FLT HEAVY | 5 | 801 | 212 | 378 | 0 | 0 | 0 | 0 | 39 | 49 | 123 | 126 |
| STACK HEAVY | 4 | 761 | 188 | 380 | 0 | 0 | 0 | 0 | 31 | 49 | 113 | 150 |
| BINAURAL HEAVY | 4 | 802 | 202 | 405 | 0 | 0 | 0 | 0 | 32 | 49 | 114 | 160 |
| SINGLE LADDER | 6 | 826 | 176 | 421 | 94 | 66 | 117 | 52 | 47 | 50 | 132 | 107 |
| SINGLE MS20 LOW | 8 | 1042 | 235 | 545 | 124 | 87 | 139 | 70 | 63 | 50 | 149 | 105 |
| SINGLE MS20 HIGH | 8 | 1035 | 235 | 539 | 124 | 87 | 135 | 70 | 63 | 48 | 150 | 105 |
| SINGLE FORMANT | 8 | 1036 | 229 | 546 | 125 | 87 | 142 | 70 | 64 | 49 | 148 | 105 |
| DUAL FLT LADDER | 3 | 498 | 103 | 219 | 0 | 0 | 0 | 0 | 23 | 49 | 104 | 115 |
| SEQ+ARP HOME | 8 | 1195 | 315 | 601 | 125 | 137 | 145 | 70 | 63 | 49 | 167 | 122 |
| SEQ+ARP AMP | 8 | 1195 | 315 | 600 | 126 | 137 | 145 | 70 | 63 | 49 | 168 | 122 |
| SEQ+ARP SEQ | 8 | 1189 | 314 | 597 | 124 | 138 | 142 | 70 | 63 | 49 | 166 | 122 |
| SEQ+ARP MOD1 | 8 | 1204 | 320 | 601 | 125 | 139 | 145 | 69 | 63 | 49 | 171 | 123 |
| LADDER FILTER PG | 6 | 826 | 174 | 423 | 94 | 66 | 118 | 53 | 47 | 49 | 133 | 107 |

### 2026-10-15 12:54, CARNAC OS DEV, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 23.5 | 23.8 | 0 | 0 | 60.9 | 3 | pass |
| SINGLE 1V | 38.8 | 39.4 | 0 | 1 | 57.3 | 1 | pass |
| SINGLE | 146.3 | 148.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 172.4 | 174.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 121.4 | 123.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 127.5 | 129.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 119.4 | 121.6 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 116.5 | 117.9 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL | 117.8 | 119.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 139.6 | 141.7 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 143.5 | 145.4 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 138.4 | 140.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 133.1 | 134.9 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 140.2 | 141.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 121.6 | 123.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 151.1 | 153.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 149.9 | 152.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 150.3 | 152.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 86.5 | 87.1 | 0 | 3 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME | 178.3 | 179.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 178.1 | 179.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 177.7 | 178.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 179.7 | 181.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 121.5 | 124.6 | 8 | 6 | – | – | FAIL (cut by the guard) |

### 2026-10-15 12:54, CARNAC OS DEV, done, wavetables in RAM

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 23.5 | 23.8 | 0 | 0 | 60.9 | 3 | pass |
| SINGLE 1V | 38.9 | 39.4 | 0 | 1 | 57.6 | 1 | pass |
| SINGLE | 146.5 | 149.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 172.2 | 175.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 121.7 | 123.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 127.9 | 129.7 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 119.5 | 121.6 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 116.7 | 118.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL | 118.1 | 119.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 139.8 | 141.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 144.2 | 145.8 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 138.3 | 140.0 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 133.3 | 135.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 140.5 | 142.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 121.8 | 124.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 151.0 | 153.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 150.0 | 152.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 150.4 | 153.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 86.5 | 87.1 | 0 | 3 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME | 178.4 | 179.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 178.2 | 179.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 177.9 | 178.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 179.9 | 181.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 121.6 | 124.7 | 8 | 6 | – | – | FAIL (cut by the guard) |

### 2026-10-15 12:46, CARNAC OS DEV, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 23.0 | 23.3 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 37.6 | 37.8 | 0 | 1 | 57.6 | 1 | pass |
| SINGLE | 142.1 | 144.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 167.7 | 169.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 117.8 | 119.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 124.0 | 125.5 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 116.1 | 117.6 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 113.0 | 114.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL | 114.3 | 115.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 135.8 | 137.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 139.9 | 141.3 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 134.6 | 136.1 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 129.4 | 130.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 136.4 | 137.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 118.2 | 120.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 146.6 | 148.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 145.5 | 147.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 145.9 | 148.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 84.5 | 84.8 | 0 | 3 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME | 173.0 | 174.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 173.1 | 174.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 173.0 | 174.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 175.1 | 176.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 118.2 | 120.0 | 8 | 6 | – | – | FAIL (cut by the guard) |

### 2026-10-15 10:00, SYNTH DEV, done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 23.3 | 23.6 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 38.6 | 39.0 | 0 | 1 | 57.6 | 1 | pass |
| SINGLE | 146.1 | 149.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 172.7 | 175.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 120.7 | 122.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD | 127.2 | 129.2 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 119.2 | 121.1 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK | 115.9 | 117.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL | 117.5 | 119.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 139.7 | 141.5 | 8 | 5 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 143.9 | 145.9 | 8 | 5 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 138.6 | 140.5 | 8 | 5 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 133.3 | 135.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 140.2 | 142.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 121.6 | 123.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 LOW | 150.3 | 153.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 HIGH | 149.7 | 152.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 150.6 | 153.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT LADDER | 86.5 | 87.0 | 0 | 3 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME | 178.7 | 180.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 178.7 | 179.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 178.2 | 179.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 180.2 | 181.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 121.7 | 125.1 | 8 | 6 | – | – | FAIL (cut by the guard) |

- [ ] Factory mod section (2026-10-16, factory_presets.c FM_TAB): every factory patch now uses its 3 MODSLOTs plus 3-6 routes. Host-only: compile + name alignment. On device: PERF TEST the heaviest presets (BONFIRE, WICKER MAN, STONE AND FIRE, EROSION) for added CPU; listen for vibrato depth on the wheel (pitch amt 9-13), AT/VEL ranges, LUNAR GATE chop, DRUID/VOTIVE vowel drift, DUST/HEARTH/BONFIRE crackle; check aftertouch and mod wheel actually reach the voice.

- [ ] MOD page radiating portal + destination rows (2026-10-16, build/fm1-modui/synth_modui1.fwsc; modui2: bent rings, destinations on the pillars, sealed at amount 0 -- the bent rings are 12 cv_line rings, re-time on device). Host-only (gcc -Os): the portal alone 4.8 us a band vs the spiral's 1.9 us, about 1 % of the whole tomb (~480 us, the stone textures). On device: the MOD page's frame rate and the SEQ+ARP MOD1 PERF TEST row against the spiral build; Select walks the destinations in the popup, Algorithm sets the amount, the chain row is skipped; the equation capstone fits for long sources (QUANTIZE TOUCH TO WHEEL drops to the small runes).

- [ ] Free-running oscillators + Roland envelope (2026-10-16, synth_voice.c voice_render_block silent path, envelopes.c). Host-only estimate: a silent voice now does ~3 adds and 2 osc_set_phase (10 stores) per block, well under 0.1 % for 8 idle voices; active voices unchanged. On device: PERF TEST a SINGLE x8 row against the last run to confirm no change; listen to decay-to-sustain feel, fast repeated notes (retrigger from the current level), unison stack at detune 0. Decay time constant is 2.5x the table (ENV_DECAY_SLOWING_Q10 = 410) to keep the old body after the attack; render_hash_golden.txt rewritten 2026-10-16 for this change.

- [ ] ANALOG (2026-10-21, LAYER page row L1/L2 ANALOG, patch byte, dsp/analog.c): per-voice-slot character, slow drift, note scatter, all at control rate. Host-only: 8 held SINGLE voices +2.7 % render time at ANALOG 127 vs 0 (render_analog_ab; 0 is bit-exact, render hashes unchanged). On device: PERF TEST SINGLE x8 and heavy rows with the patch at ANALOG 127 against 0 (estimate under 1 point); listen to listening/analog_ab (0/50/90/127): drift too wide or too slow at 127? DUAL OSC same-pitch pairs beat (average level ~3 dB under the locked pair at 0). Save a patch at ANALOG 90, power cycle, check it comes back (last stored byte).

- [ ] PCM wave store (2026-10-21, src/dsp/pcm_store.c v2, src/hardware/pcm_store_fm1.c, pcm_store_upload.py). Flash layout change: the preset store ends after chunk 22 (PS_NCHUNK 23); area 0 = 0x97000..0x9FFFF (36 KiB, X0X's old objects), area 1 = 0xCE000..0xDBFFF (56 KiB). Host-only: test_pcm_store (simulated NOR: uploads, reboots, cut-off uploads, bad images incl. version-2 wave references). On device: saved patches / performances / seqs / arps still load after the install; `pcm_store_upload.py info` shows both areas empty (area 0 may hold X0X data: must read empty); upload src/dsp/pcm_area0.bin to area 0 and pcm_area1.bin to area 1, `info` lists the entries; power cycle, `info` again; boot time with both areas full (CRC through XIP at boot).

- [ ] PCM waves on the oscillators (2026-10-21, src/dsp/pcm_osc.c, WAVE row: banks, PCM waves, attacks). Host-only: test_pcm_osc (block = one-sample path, every wave in every architecture, fallback to bank 0, list round-trip); host cost per voice-sample bank 26.6 ns vs CHOIR at SCAN 64 29.5 ns vs WOOD CHIMES re-struck 30.6 ns (+11..15 %: the mu-law decode loads). Reads are int like the wavetables. On device: the PERF TEST PCM rows against their baselines (SINGLE / DUAL OSC / SPREAD / DUAL FILTER / STACK / BINAURAL PCM, the MULTI H PCM rows, SINGLE / DUALOSC RS PCM, SEQ+ARP HOME PCM) with the store uploaded; a voice cap stays unless its PCM row fails. Listen: listening/pcm engine_* renders, then on the panel: WAVE row steps banks -> PCM waves -> attacks, OSC page picture and stones, SCAN density on the chimes, attack at each note, no clicks at SCAN moves or the attack's end.

- [ ] Hollow Reeds (2026-10-21, wavetable bank 15, gen_wavetables.hollow_reeds): banks 0..14 byte-identical (each added bank its own dither stream). On device: bank 15 plays and SCAN walks ocarina -> pan flute -> bone flute -> rim flute -> whistle; with STRIKE + BONE CHIFF / REED CHIFF and NOISE as in listening/flute_bank.
