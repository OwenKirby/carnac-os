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

### 2026-11-03 20:53, CARNAC OS DEV (2 cores), done

build/fm1-bronze2 (claude-synth-a1): HEAD 59966af + BRONZE AGE MFX as register passes (bit-identical to the one-loop version) + PERF TEST index 14-bit (129 rows). BRONZE AGE: IDLE +10.0 (one layer, sides on two cores), x2 +18.9, SPREAD H CEL BRNZ 82.4 vs SPREAD H CEL+PP 70.9 (+11.5). The one-loop version (build/fm1-bronze1, 20:37) measured IDLE +11.5, x2 +21.8; its heavy row was not readable (7-bit index).

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.8 | 17.4 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.5 | 27.4 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 47.7 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 53.3 | 54.5 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 59.3 | 60.3 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD | 59.6 | 60.7 | 0 | 8 | 55.9 | 8 | pass |
| DUAL FILTER | 61.1 | 62.1 | 0 | 7 | 53.9 | 8 | pass |
| STACK | 68.2 | 69.4 | 0 | 6 | 46.6 | 9 | pass |
| BINAURAL | 57.5 | 58.8 | 0 | 6 | 57.6 | 8 | pass |
| DUAL OSC HEAVY | 59.4 | 60.5 | 0 | 6 | 55.6 | 8 | pass |
| SPREAD HEAVY | 64.6 | 65.8 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 64.4 | 65.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 67.0 | 68.3 | 0 | 6 | 45.9 | 9 | pass |
| BINAURAL HEAVY | 63.9 | 65.1 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 47.7 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 45.4 | 46.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 53.8 | 54.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 46.3 | 47.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 61.2 | 62.2 | 0 | 7 | 54.9 | 8 | pass |
| SEQ+ARP HOME | 63.0 | 66.6 | 0 | 8 | 52.5 | 10 | pass |
| SEQ+ARP AMP | 63.1 | 67.3 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 62.5 | 66.0 | 0 | 8 | 14.3 | 127 | pass |
| SEQ+ARP MOD1 | 63.0 | 67.8 | 0 | 8 | 14.5 | 70 | pass |
| LADDER FILTER PG | 47.7 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.7 | 57.8 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 58.0 | 59.2 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 67.5 | 71.8 | 0 | 8 | 46.6 | 10 | pass |
| SINGLE MULTI H | 54.5 | 55.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.0 | 61.0 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD MULTI H | 66.3 | 67.5 | 0 | 8 | 49.6 | 9 | pass |
| DUALFLT MULTI H | 67.7 | 68.9 | 0 | 7 | 45.9 | 9 | pass |
| STACK MULTI H | 69.5 | 70.8 | 0 | 6 | 43.2 | 9 | pass |
| BINAUR MULTI H | 66.0 | 67.1 | 0 | 6 | 50.9 | 9 | pass |
| SPREAD H CEL+PP | 70.9 | 72.2 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALOSC H CEL+PP | 64.7 | 66.1 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 70.4 | 71.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP SPRD CEL | 80.0 | 84.0 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.6 | 56.8 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 57.4 | 58.6 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP SPR+BBD | 64.9 | 69.1 | 0 | 8 | 51.5 | 10 | pass |
| STRIKE | 48.8 | 50.0 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 54.9 | 56.0 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 74.5 | 79.4 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 78.5 | 82.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 60.9 | 62.0 | 0 | 8 | 55.5 | 8 | pass |
| DUALOSC SH2 H | 65.2 | 66.3 | 0 | 6 | 51.5 | 8 | pass |
| SPREAD SH2 H | 72.9 | 74.0 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 78.4 | 79.6 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 77.9 | 79.3 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 74.4 | 75.8 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 64.5 | 65.6 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 77.6 | 78.9 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 83.0 | 84.4 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.3 | 78.1 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 78.5 | 79.8 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 74.4 | 75.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 52.3 | 53.6 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 60.6 | 61.8 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD PCM | 65.4 | 66.4 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 65.7 | 66.9 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 69.4 | 70.8 | 0 | 6 | 42.9 | 9 | pass |
| BINAURAL PCM | 63.1 | 64.5 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 59.8 | 61.1 | 0 | 6 | 55.6 | 8 | pass |
| DUALFLT MULTI H PCM | 72.4 | 73.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.5 | 72.9 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.8 | 49.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.2 | 57.2 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 59.3 | 61.3 | 0 | 6 | 56.5 | 8 | pass |
| DUALOSC RS PCM | 63.8 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 71.2 | 75.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 52.1 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 52.3 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 52.1 | 53.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 53.1 | 54.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.8 | 54.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.9 | 54.1 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 74.1 | 75.6 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 70.8 | 72.3 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 62.5 | 63.7 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP HOME TUBE | 69.8 | 74.1 | 0 | 8 | 43.2 | 10 | pass |
| FULL | 53.0 | 54.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL HEAVY | 52.7 | 53.9 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 54.1 | 55.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 60.3 | 61.2 | 0 | 4 | 54.5 | 8 | pass |
| FULL H CEL+PP | 59.4 | 60.7 | 0 | 4 | 55.9 | 8 | pass |
| FULL MULTI H PCM | 54.9 | 56.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 60.8 | 61.9 | 0 | 4 | 55.9 | 8 | pass |
| FULL SH2 CEL+PP | 65.4 | 66.9 | 0 | 4 | 51.2 | 8 | pass |
| DSPRD | 49.1 | 50.2 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 53.2 | 54.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 53.4 | 54.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 57.7 | 59.0 | 0 | 4 | 57.2 | 8 | pass |
| DSPRD H CEL+PP | 58.8 | 60.5 | 0 | 4 | 56.5 | 8 | pass |
| DSPRD MULTI H PCM | 59.5 | 60.6 | 0 | 4 | 56.2 | 8 | pass |
| DSPRD SH2 CEL+PP | 63.2 | 64.5 | 0 | 4 | 51.9 | 8 | pass |
| SEQ+ARP DSPRD CEL | 67.9 | 71.9 | 0 | 4 | 45.6 | 10 | pass |
| IDLE PHASER | 18.9 | 19.8 | 0 | 0 | 61.6 | 1 | pass |
| IDLE PHASER x2 | 20.4 | 21.1 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD PHASER | 63.6 | 64.8 | 0 | 8 | 51.9 | 8 | pass |
| SPREAD H CEL PHSR | 75.6 | 77.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 69.8 | 74.6 | 0 | 8 | 42.9 | 2 | pass |
| IDLE ENSEMBLE | 19.6 | 20.3 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 21.2 | 22.0 | 0 | 0 | 61.2 | 1 | pass |
| IDLE ENS+PHSR | 20.2 | 21.0 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR x2 | 22.1 | 22.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 19.2 | 19.9 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 20.7 | 21.5 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL ENPH | 78.5 | 80.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 72.6 | 77.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SPREAD H CEL MIX | 73.4 | 74.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 70.3 | 75.1 | 0 | 8 | 42.9 | 10 | FAIL |
| HEAVY SPRING MIX | 58.3 | 59.5 | 0 | 8 | 56.8 | 8 | pass |
| IDLE TAPE FLANGER | 20.2 | 20.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE TAPE FLNG x2 | 22.1 | 22.8 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 78.7 | 80.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 72.4 | 75.9 | 0 | 8 | 43.2 | 2 | FAIL |
| IDLE BBD FLANGER | 19.3 | 20.1 | 0 | 0 | 61.3 | 3 | pass |
| IDLE BBD FLNG x2 | 20.8 | 21.6 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL BBDF | 78.0 | 79.4 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.8 | 20.7 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 52.4 | 54.3 | 0 | 8 | 57.2 | 1 | pass |
| IDLE PITCH SHFT x2 | 22.1 | 23.1 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 76.4 | 78.2 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 20.3 | 21.3 | 0 | 0 | 61.6 | 1 | pass |
| IDLE BBD ECHO x2 | 20.5 | 21.3 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ECHO | 76.7 | 78.3 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE BRONZE AGE | 26.8 | 27.5 | 0 | 0 | 60.3 | 4 | pass |
| IDLE BRONZE AGE x2 | 35.7 | 36.5 | 0 | 0 | 57.2 | 1 | pass |
| SPREAD H CEL BRNZ | 82.4 | 83.8 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-03 20:37, CARNAC OS DEV (2 cores), done

build/fm1-bronze1 (claude-synth-a1): only row 0 recorded here: 129 rows wrapped the 7-bit count. Rows read by hand: IDLE BRONZE AGE 28.3 / 28.9, IDLE BRONZE AGE x2 38.6 / 39.3 (IDLE 16.8).

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.8 | 17.4 | 0 | 0 | 61.6 | 3 | pass |

### 2026-11-03 20:26, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.2 | 0 | 0 | 62.0 | 3 | pass |
| SINGLE 1V | 25.9 | 27.0 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 47.2 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.7 | 54.0 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 58.4 | 59.5 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD | 58.8 | 60.1 | 0 | 8 | 57.2 | 8 | pass |
| DUAL FILTER | 60.5 | 61.6 | 0 | 7 | 56.6 | 8 | pass |
| STACK | 67.3 | 68.6 | 0 | 6 | 45.5 | 9 | pass |
| BINAURAL | 56.9 | 58.0 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 58.2 | 59.4 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD HEAVY | 64.1 | 65.5 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 63.5 | 64.9 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 65.9 | 67.1 | 0 | 6 | 51.6 | 8 | pass |
| BINAURAL HEAVY | 63.3 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 47.2 | 48.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 44.9 | 46.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 52.8 | 54.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 45.7 | 46.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.5 | 61.7 | 0 | 7 | 56.2 | 8 | pass |
| SEQ+ARP HOME | 62.1 | 66.2 | 0 | 8 | 52.9 | 10 | pass |
| SEQ+ARP AMP | 62.2 | 66.0 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.3 | 64.9 | 0 | 8 | 15.5 | 119 | pass |
| SEQ+ARP MOD1 | 61.9 | 67.1 | 0 | 8 | 16.7 | 69 | pass |
| LADDER FILTER PG | 47.1 | 48.4 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 56.2 | 57.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.7 | 58.0 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.1 | 70.3 | 0 | 8 | 49.6 | 10 | pass |
| SINGLE MULTI H | 54.5 | 55.7 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 59.4 | 60.7 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD MULTI H | 66.0 | 67.3 | 0 | 8 | 51.9 | 9 | pass |
| DUALFLT MULTI H | 67.3 | 68.7 | 0 | 7 | 46.9 | 9 | pass |
| STACK MULTI H | 69.1 | 70.6 | 0 | 6 | 43.6 | 9 | pass |
| BINAUR MULTI H | 65.7 | 66.9 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 69.7 | 71.3 | 0 | 8 | 43.9 | 9 | pass |
| DUALOSC H CEL+PP | 64.4 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 69.4 | 70.9 | 0 | 8 | 42.9 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.9 | 83.7 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.3 | 56.5 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.7 | 57.9 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP SPR+BBD | 64.9 | 69.0 | 0 | 8 | 50.5 | 10 | pass |
| STRIKE | 48.7 | 49.9 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 54.9 | 56.1 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.6 | 75.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.8 | 77.2 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 60.1 | 61.4 | 0 | 8 | 56.3 | 8 | pass |
| DUALOSC SH2 H | 64.5 | 65.9 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 71.0 | 72.5 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 77.6 | 79.0 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 77.6 | 79.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 74.2 | 75.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.4 | 62.6 | 0 | 8 | 53.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 75.4 | 77.0 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 82.5 | 84.0 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.5 | 78.5 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 77.6 | 79.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 74.2 | 75.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.2 | 52.6 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 59.8 | 61.0 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD PCM | 64.1 | 65.3 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 64.4 | 65.7 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 68.3 | 69.6 | 0 | 6 | 48.9 | 9 | pass |
| BINAURAL PCM | 62.3 | 63.6 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 59.0 | 60.2 | 0 | 6 | 56.9 | 8 | pass |
| DUALFLT MULTI H PCM | 71.4 | 73.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.0 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.2 | 49.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.2 | 56.3 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 58.4 | 60.2 | 0 | 6 | 56.2 | 8 | pass |
| DUALOSC RS PCM | 63.2 | 65.1 | 0 | 6 | 52.5 | 8 | pass |
| SEQ+ARP HOME PCM | 70.3 | 74.4 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE DRV SOFT | 51.1 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.3 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 51.2 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.2 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.2 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 52.3 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 73.7 | 75.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 71.7 | 73.1 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 59.7 | 60.8 | 0 | 8 | 56.2 | 8 | pass |
| SEQ+ARP HOME TUBE | 67.5 | 72.2 | 0 | 8 | 48.2 | 10 | pass |
| FULL | 52.8 | 54.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL HEAVY | 51.7 | 53.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 53.4 | 54.6 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 59.5 | 60.7 | 0 | 4 | 56.2 | 8 | pass |
| FULL H CEL+PP | 58.5 | 59.9 | 0 | 4 | 56.9 | 8 | pass |
| FULL MULTI H PCM | 54.2 | 55.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 59.6 | 60.8 | 0 | 4 | 55.2 | 8 | pass |
| FULL SH2 CEL+PP | 64.2 | 65.6 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 48.2 | 49.4 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 51.6 | 52.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 52.7 | 54.0 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 55.1 | 56.3 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 57.2 | 58.6 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD MULTI H PCM | 58.4 | 59.5 | 0 | 4 | 56.6 | 8 | pass |
| DSPRD SH2 CEL+PP | 59.8 | 61.0 | 0 | 4 | 55.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 67.5 | 71.4 | 0 | 4 | 47.6 | 10 | pass |
| IDLE PHASER | 18.9 | 19.6 | 0 | 0 | 61.6 | 1 | pass |
| IDLE PHASER x2 | 20.4 | 21.1 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD PHASER | 62.4 | 63.7 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 74.5 | 76.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 68.2 | 72.5 | 0 | 8 | 46.6 | 10 | pass |
| IDLE ENSEMBLE | 19.7 | 20.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 21.3 | 22.1 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR | 20.3 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR x2 | 22.2 | 22.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 19.2 | 20.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 20.7 | 21.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 78.2 | 79.8 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 72.4 | 75.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SPREAD H CEL MIX | 72.1 | 73.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 69.0 | 72.9 | 0 | 8 | 45.2 | 10 | pass |
| HEAVY SPRING MIX | 58.0 | 59.4 | 0 | 8 | 57.5 | 8 | pass |
| IDLE TAPE FLANGER | 20.0 | 20.8 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.9 | 22.7 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 77.2 | 78.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 71.0 | 75.3 | 0 | 8 | 43.2 | 10 | FAIL |
| IDLE BBD FLANGER | 19.4 | 20.3 | 0 | 0 | 61.3 | 3 | pass |
| IDLE BBD FLNG x2 | 20.9 | 21.7 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL BBDF | 75.6 | 77.2 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.9 | 20.8 | 0 | 0 | 61.6 | 1 | pass |
| PITCH SHIFT HELD | 53.1 | 54.9 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 22.3 | 23.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 76.7 | 78.5 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 20.3 | 21.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD ECHO x2 | 20.4 | 21.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ECHO | 77.1 | 78.6 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-03 20:08, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.2 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 25.8 | 27.0 | 0 | 1 | 60.6 | 4 | pass |
| SINGLE | 47.1 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.9 | 54.2 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 58.6 | 59.8 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD | 58.8 | 60.0 | 0 | 8 | 56.9 | 8 | pass |
| DUAL FILTER | 60.6 | 61.8 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 68.0 | 69.4 | 0 | 6 | 47.5 | 9 | pass |
| BINAURAL | 57.3 | 58.5 | 0 | 6 | 57.6 | 7 | pass |
| DUAL OSC HEAVY | 58.5 | 59.8 | 0 | 6 | 56.6 | 8 | pass |
| SPREAD HEAVY | 64.5 | 66.0 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 63.7 | 65.0 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 66.6 | 68.0 | 0 | 6 | 49.6 | 9 | pass |
| BINAURAL HEAVY | 63.6 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 47.1 | 48.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 45.3 | 46.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 53.3 | 54.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 46.0 | 47.2 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.6 | 61.8 | 0 | 7 | 55.9 | 8 | pass |
| SEQ+ARP HOME | 61.9 | 65.9 | 0 | 8 | 53.2 | 10 | pass |
| SEQ+ARP AMP | 62.0 | 66.3 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.2 | 65.0 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 61.8 | 66.9 | 0 | 8 | 16.7 | 69 | pass |
| LADDER FILTER PG | 47.1 | 48.4 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 56.5 | 57.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 57.1 | 58.3 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.8 | 70.0 | 0 | 8 | 50.2 | 10 | pass |
| SINGLE MULTI H | 54.3 | 55.6 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 59.8 | 61.0 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD MULTI H | 66.0 | 67.3 | 0 | 8 | 51.9 | 9 | pass |
| DUALFLT MULTI H | 67.3 | 68.7 | 0 | 7 | 47.9 | 9 | pass |
| STACK MULTI H | 70.4 | 72.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR MULTI H | 66.2 | 67.3 | 0 | 6 | 51.6 | 9 | pass |
| SPREAD H CEL+PP | 70.0 | 71.6 | 0 | 8 | 42.9 | 9 | pass |
| DUALOSC H CEL+PP | 64.7 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 69.7 | 71.2 | 0 | 8 | 42.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 79.2 | 84.5 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.3 | 56.5 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.7 | 58.2 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 64.4 | 68.4 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 48.2 | 49.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.5 | 55.8 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.2 | 75.5 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 72.0 | 76.5 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE SH2 H | 60.7 | 61.8 | 0 | 8 | 56.2 | 8 | pass |
| DUALOSC SH2 H | 65.0 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 71.5 | 73.0 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 78.7 | 80.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 80.9 | 82.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 75.4 | 76.8 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.6 | 62.9 | 0 | 8 | 53.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 76.1 | 77.6 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 83.0 | 84.7 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 73.9 | 78.5 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 78.6 | 80.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 75.5 | 76.9 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.5 | 53.1 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.8 | 61.1 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD PCM | 64.4 | 65.8 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 65.0 | 66.4 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 68.8 | 70.3 | 0 | 6 | 44.2 | 9 | pass |
| BINAURAL PCM | 62.7 | 63.8 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 59.4 | 60.7 | 0 | 6 | 56.2 | 8 | pass |
| DUALFLT MULTI H PCM | 71.9 | 73.5 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.6 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.2 | 49.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE RS PCM | 54.5 | 56.8 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 58.6 | 60.5 | 0 | 6 | 56.5 | 8 | pass |
| DUALOSC RS PCM | 63.4 | 65.6 | 0 | 6 | 52.2 | 8 | pass |
| SEQ+ARP HOME PCM | 70.4 | 74.4 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE DRV SOFT | 51.1 | 52.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.3 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 51.2 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.2 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.1 | 53.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.1 | 53.3 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 73.9 | 75.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 71.1 | 72.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 59.2 | 60.4 | 0 | 8 | 56.6 | 8 | pass |
| SEQ+ARP HOME TUBE | 67.3 | 71.4 | 0 | 8 | 46.5 | 10 | pass |
| FULL | 52.6 | 53.7 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 51.8 | 53.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 53.4 | 54.7 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 59.7 | 60.8 | 0 | 4 | 55.2 | 8 | pass |
| FULL H CEL+PP | 58.9 | 60.3 | 0 | 4 | 56.9 | 8 | pass |
| FULL MULTI H PCM | 54.0 | 55.3 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 59.4 | 60.5 | 0 | 4 | 56.5 | 8 | pass |
| FULL SH2 CEL+PP | 64.2 | 65.6 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 48.3 | 49.6 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 52.2 | 53.5 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 53.0 | 54.4 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 55.9 | 57.1 | 0 | 4 | 57.5 | 7 | pass |
| DSPRD H CEL+PP | 57.7 | 59.1 | 0 | 4 | 57.2 | 8 | pass |
| DSPRD MULTI H PCM | 58.7 | 60.2 | 0 | 4 | 56.9 | 8 | pass |
| DSPRD SH2 CEL+PP | 60.8 | 62.0 | 0 | 4 | 55.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 67.4 | 71.1 | 0 | 4 | 47.2 | 10 | pass |
| IDLE PHASER | 18.9 | 19.7 | 0 | 0 | 61.6 | 1 | pass |
| IDLE PHASER x2 | 20.4 | 21.1 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD PHASER | 62.9 | 64.1 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 75.0 | 76.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 67.9 | 72.1 | 0 | 8 | 44.9 | 10 | pass |
| IDLE ENSEMBLE | 19.7 | 20.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 21.3 | 22.1 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 20.4 | 21.2 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR x2 | 22.3 | 23.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 19.3 | 20.1 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 20.9 | 21.7 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL ENPH | 78.8 | 80.5 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 72.1 | 75.6 | 0 | 8 | 43.2 | 2 | FAIL |
| SPREAD H CEL MIX | 72.3 | 73.9 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 68.7 | 73.4 | 0 | 8 | 45.6 | 10 | pass |
| HEAVY SPRING MIX | 58.3 | 59.6 | 0 | 8 | 57.3 | 8 | pass |
| IDLE TAPE FLANGER | 20.0 | 20.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 22.0 | 22.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL TFLG | 77.9 | 79.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 70.8 | 75.4 | 0 | 8 | 43.2 | 11 | FAIL |
| IDLE BBD FLANGER | 19.4 | 20.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE BBD FLNG x2 | 20.9 | 21.7 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL BBDF | 76.2 | 77.7 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 20.1 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 53.6 | 55.4 | 0 | 8 | 57.6 | 1 | pass |
| IDLE PITCH SHFT x2 | 22.4 | 23.4 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 77.0 | 78.9 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 20.3 | 21.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD ECHO x2 | 20.4 | 21.2 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ECHO | 77.7 | 79.3 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-03 19:55, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.7 | 17.2 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 27.0 | 28.1 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 48.9 | 50.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE HEAVY | 54.2 | 55.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.5 | 61.6 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD | 61.0 | 62.2 | 0 | 8 | 55.9 | 8 | pass |
| DUAL FILTER | 62.9 | 64.0 | 0 | 7 | 51.9 | 8 | pass |
| STACK | 69.4 | 70.9 | 0 | 6 | 43.2 | 9 | pass |
| BINAURAL | 58.3 | 59.7 | 0 | 6 | 57.2 | 8 | pass |
| DUAL OSC HEAVY | 59.9 | 61.3 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD HEAVY | 66.1 | 67.4 | 0 | 8 | 51.5 | 9 | pass |
| DUAL FLT HEAVY | 66.1 | 67.3 | 0 | 7 | 52.2 | 9 | pass |
| STACK HEAVY | 67.8 | 69.5 | 0 | 6 | 47.2 | 9 | pass |
| BINAURAL HEAVY | 64.8 | 66.0 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 49.0 | 50.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 46.9 | 48.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 55.2 | 56.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 47.5 | 48.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 62.9 | 64.1 | 0 | 7 | 52.2 | 8 | pass |
| SEQ+ARP HOME | 62.9 | 66.8 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 63.1 | 67.0 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 62.3 | 66.5 | 0 | 8 | 15.3 | 125 | pass |
| SEQ+ARP MOD1 | 62.9 | 67.6 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 49.0 | 50.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.8 | 59.1 | 0 | 8 | 57.5 | 8 | pass |
| HEAVY CELEST+PP | 58.6 | 59.8 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 67.2 | 71.0 | 0 | 8 | 47.9 | 10 | pass |
| SINGLE MULTI H | 56.2 | 57.4 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC MULTI H | 61.3 | 62.6 | 0 | 6 | 54.2 | 8 | pass |
| SPREAD MULTI H | 67.9 | 69.2 | 0 | 8 | 47.2 | 9 | pass |
| DUALFLT MULTI H | 69.9 | 71.4 | 0 | 7 | 43.9 | 9 | pass |
| STACK MULTI H | 70.8 | 72.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR MULTI H | 67.3 | 68.9 | 0 | 6 | 47.6 | 9 | pass |
| SPREAD H CEL+PP | 71.8 | 73.4 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALOSC H CEL+PP | 65.8 | 67.4 | 0 | 6 | 51.9 | 9 | pass |
| SPREAD H TUM+PP | 71.5 | 73.0 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP SPRD CEL | 81.0 | 86.1 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 57.0 | 58.2 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 58.2 | 59.6 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 65.7 | 69.7 | 0 | 8 | 50.9 | 10 | pass |
| STRIKE | 50.4 | 51.6 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 55.7 | 57.0 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP STRIKE | 72.5 | 77.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 73.6 | 77.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 62.5 | 63.8 | 0 | 8 | 52.8 | 8 | pass |
| DUALOSC SH2 H | 67.3 | 68.8 | 0 | 6 | 45.2 | 9 | pass |
| SPREAD SH2 H | 73.9 | 75.6 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 80.6 | 82.3 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 79.9 | 81.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 76.1 | 77.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 63.8 | 65.1 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 78.0 | 79.8 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 84.4 | 86.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.2 | 80.0 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 80.6 | 82.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 76.1 | 77.9 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 53.9 | 55.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC PCM | 62.5 | 63.9 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD PCM | 66.9 | 68.4 | 0 | 8 | 47.6 | 9 | pass |
| DUAL FILTER PCM | 67.7 | 68.9 | 0 | 7 | 47.2 | 9 | pass |
| STACK PCM | 70.5 | 72.0 | 0 | 6 | 42.9 | 9 | FAIL |
| BINAURAL PCM | 64.6 | 65.8 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 61.8 | 63.1 | 0 | 6 | 52.6 | 8 | pass |
| DUALFLT MULTI H PCM | 74.9 | 76.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 73.0 | 74.4 | 0 | 6 | 43.2 | 1 | FAIL |
| SINGLE RS | 49.1 | 51.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 56.8 | 58.7 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC RS | 60.5 | 62.8 | 0 | 6 | 55.9 | 8 | pass |
| DUALOSC RS PCM | 65.9 | 67.7 | 0 | 6 | 51.9 | 9 | pass |
| SEQ+ARP HOME PCM | 71.0 | 74.7 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE DRV SOFT | 53.1 | 54.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 53.2 | 54.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 53.4 | 54.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 54.2 | 55.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 53.9 | 55.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 54.0 | 55.1 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 74.5 | 76.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 71.6 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 60.5 | 61.7 | 0 | 8 | 56.2 | 8 | pass |
| SEQ+ARP HOME TUBE | 68.0 | 72.1 | 0 | 8 | 45.5 | 10 | pass |
| FULL | 54.0 | 55.2 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 52.9 | 54.3 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 56.4 | 57.7 | 0 | 4 | 57.6 | 7 | pass |
| FULL SH2 H | 60.9 | 62.1 | 0 | 4 | 55.9 | 8 | pass |
| FULL H CEL+PP | 61.0 | 62.4 | 0 | 4 | 54.5 | 8 | pass |
| FULL MULTI H PCM | 57.1 | 58.4 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H PCM | 62.1 | 63.4 | 0 | 4 | 52.5 | 8 | pass |
| FULL SH2 CEL+PP | 65.9 | 67.3 | 0 | 4 | 51.5 | 9 | pass |
| DSPRD | 49.9 | 51.2 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 53.3 | 54.5 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 54.3 | 55.5 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 57.8 | 59.0 | 0 | 4 | 57.2 | 8 | pass |
| DSPRD H CEL+PP | 58.8 | 60.0 | 0 | 4 | 57.2 | 8 | pass |
| DSPRD MULTI H PCM | 60.4 | 61.6 | 0 | 4 | 56.2 | 8 | pass |
| DSPRD SH2 CEL+PP | 62.1 | 63.5 | 0 | 4 | 52.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 68.3 | 72.8 | 0 | 4 | 45.6 | 10 | pass |
| IDLE PHASER | 18.9 | 19.6 | 0 | 0 | 61.6 | 1 | pass |
| IDLE PHASER x2 | 20.3 | 21.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 64.7 | 66.0 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 76.7 | 78.4 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 68.9 | 73.6 | 0 | 8 | 43.9 | 11 | pass |
| IDLE ENSEMBLE | 19.7 | 20.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 21.4 | 22.2 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR | 20.5 | 21.4 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 22.4 | 23.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS | 19.5 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 21.1 | 21.9 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL ENPH | 81.2 | 82.9 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 72.9 | 76.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SPREAD H CEL MIX | 74.9 | 76.9 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 70.1 | 73.8 | 0 | 8 | 43.6 | 10 | FAIL |
| HEAVY SPRING MIX | 60.0 | 61.4 | 0 | 8 | 56.2 | 8 | pass |
| IDLE TAPE FLANGER | 20.0 | 20.8 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 22.0 | 22.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL TFLG | 78.9 | 80.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 71.2 | 75.6 | 0 | 8 | 43.2 | 10 | FAIL |
| IDLE BBD FLANGER | 19.6 | 20.4 | 0 | 0 | 61.3 | 1 | pass |
| IDLE BBD FLNG x2 | 21.0 | 21.9 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL BBDF | 78.0 | 79.7 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 20.1 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 55.2 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| IDLE PITCH SHFT x2 | 22.4 | 23.5 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 78.8 | 80.6 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE BBD ECHO | 20.5 | 21.2 | 0 | 0 | 61.3 | 1 | pass |
| IDLE BBD ECHO x2 | 20.5 | 21.3 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL ECHO | 78.8 | 80.4 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-03 18:06, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.2 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.4 | 27.8 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 47.7 | 49.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.5 | 53.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 59.5 | 60.7 | 0 | 6 | 56.5 | 8 | pass |
| SPREAD | 59.3 | 60.7 | 0 | 8 | 56.2 | 8 | pass |
| DUAL FILTER | 61.3 | 62.7 | 0 | 7 | 54.5 | 8 | pass |
| STACK | 68.2 | 69.6 | 0 | 6 | 46.6 | 9 | pass |
| BINAURAL | 57.5 | 58.8 | 0 | 6 | 57.2 | 8 | pass |
| DUAL OSC HEAVY | 58.7 | 59.8 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD HEAVY | 64.0 | 65.3 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 63.9 | 65.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 66.2 | 67.6 | 0 | 6 | 49.9 | 9 | pass |
| BINAURAL HEAVY | 63.2 | 64.4 | 0 | 6 | 51.9 | 8 | pass |
| SINGLE LADDER | 47.7 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 45.4 | 46.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 53.7 | 54.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.9 | 47.1 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 61.3 | 62.6 | 0 | 7 | 53.5 | 8 | pass |
| SEQ+ARP HOME | 61.7 | 65.6 | 0 | 8 | 53.5 | 10 | pass |
| SEQ+ARP AMP | 61.6 | 65.8 | 0 | 8 | 48.2 | 24 | pass |
| SEQ+ARP SEQ | 60.6 | 65.7 | 0 | 8 | 9.8 | 127 | pass |
| SEQ+ARP MOD1 | 61.2 | 66.1 | 0 | 8 | 12.3 | 82 | pass |
| LADDER FILTER PG | 47.6 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 56.1 | 57.7 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CELEST+PP | 56.9 | 58.3 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.4 | 70.8 | 0 | 8 | 49.5 | 11 | pass |
| SINGLE MULTI H | 54.6 | 56.0 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 60.7 | 62.1 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD MULTI H | 66.1 | 67.5 | 0 | 8 | 50.9 | 9 | pass |
| DUALFLT MULTI H | 68.2 | 69.5 | 0 | 7 | 48.6 | 9 | pass |
| STACK MULTI H | 69.7 | 71.4 | 0 | 6 | 43.6 | 9 | pass |
| BINAUR MULTI H | 66.2 | 67.5 | 0 | 6 | 50.9 | 9 | pass |
| SPREAD H CEL+PP | 70.1 | 71.8 | 0 | 8 | 43.2 | 9 | FAIL |
| DUALOSC H CEL+PP | 65.5 | 67.4 | 0 | 6 | 51.6 | 9 | pass |
| SPREAD H TUM+PP | 69.5 | 71.2 | 0 | 8 | 43.2 | 9 | pass |
| SEQ+ARP SPRD CEL | 80.1 | 84.2 | 0 | 8 | 42.9 | 13 | FAIL |
| HEAVY SPRING+PP | 55.5 | 56.6 | 0 | 8 | 57.6 | 7 | pass |
| HEAVY CEL+BBD PP | 57.1 | 58.4 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP SPR+BBD | 64.7 | 68.9 | 0 | 8 | 51.9 | 11 | pass |
| STRIKE | 48.8 | 50.0 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 54.4 | 55.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.5 | 76.8 | 0 | 8 | 43.2 | 3 | FAIL |
| SEQ+ARP STRK FILT | 72.7 | 77.5 | 0 | 8 | 42.9 | 3 | FAIL |
| SINGLE SH2 H | 60.5 | 61.7 | 0 | 8 | 55.9 | 8 | pass |
| DUALOSC SH2 H | 65.6 | 66.9 | 0 | 6 | 52.2 | 9 | pass |
| SPREAD SH2 H | 71.7 | 73.3 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 78.1 | 79.6 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 77.9 | 79.6 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 73.9 | 75.5 | 0 | 6 | 43.2 | 1 | FAIL |
| STRIKE SH2 H | 61.6 | 62.7 | 0 | 8 | 53.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 76.1 | 78.0 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 83.1 | 84.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.4 | 78.4 | 0 | 8 | 43.2 | 3 | FAIL |
| DUALFLT SH2 HP | 78.1 | 79.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 73.9 | 75.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 52.5 | 53.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC PCM | 60.9 | 62.4 | 0 | 6 | 53.9 | 8 | pass |
| SPREAD PCM | 65.6 | 67.2 | 0 | 8 | 52.2 | 9 | pass |
| DUAL FILTER PCM | 67.1 | 68.6 | 0 | 7 | 46.6 | 9 | pass |
| STACK PCM | 69.2 | 70.7 | 0 | 6 | 43.2 | 9 | pass |
| BINAURAL PCM | 63.7 | 65.0 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 60.7 | 61.9 | 0 | 6 | 54.9 | 8 | pass |
| DUALFLT MULTI H PCM | 73.6 | 75.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 72.0 | 73.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.7 | 49.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.2 | 57.3 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 59.5 | 61.5 | 0 | 6 | 55.5 | 8 | pass |
| DUALOSC RS PCM | 64.3 | 66.7 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 69.8 | 74.2 | 0 | 8 | 43.9 | 11 | pass |
| SINGLE DRV SOFT | 51.9 | 53.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.9 | 53.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 52.1 | 53.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 52.7 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 52.5 | 53.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 52.5 | 53.8 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 72.8 | 74.5 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR HEAVY OD | 70.4 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 59.2 | 60.3 | 0 | 8 | 56.9 | 8 | pass |
| SEQ+ARP HOME TUBE | 67.0 | 71.5 | 0 | 8 | 48.2 | 11 | pass |
| FULL | 53.2 | 54.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 52.0 | 53.3 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 54.5 | 55.8 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H | 60.4 | 61.6 | 0 | 4 | 56.2 | 8 | pass |
| FULL H CEL+PP | 59.5 | 60.9 | 0 | 4 | 56.2 | 8 | pass |
| FULL MULTI H PCM | 57.0 | 58.2 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 60.8 | 62.0 | 0 | 4 | 54.6 | 8 | pass |
| FULL SH2 CEL+PP | 64.8 | 66.3 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 48.9 | 50.2 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 52.3 | 53.5 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 53.3 | 54.5 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 56.0 | 57.2 | 0 | 4 | 57.5 | 7 | pass |
| DSPRD H CEL+PP | 57.7 | 58.9 | 0 | 4 | 57.2 | 8 | pass |
| DSPRD MULTI H PCM | 59.4 | 60.6 | 0 | 4 | 56.2 | 8 | pass |
| DSPRD SH2 CEL+PP | 60.6 | 61.8 | 0 | 4 | 55.5 | 8 | pass |
| SEQ+ARP DSPRD CEL | 67.4 | 71.6 | 0 | 4 | 46.9 | 11 | pass |
| IDLE PHASER | 18.9 | 19.6 | 0 | 0 | 61.3 | 1 | pass |
| IDLE PHASER x2 | 20.3 | 21.0 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD PHASER | 63.2 | 64.6 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 75.4 | 77.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 68.0 | 72.6 | 0 | 8 | 45.3 | 11 | pass |
| IDLE ENSEMBLE | 19.8 | 20.6 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 21.3 | 22.1 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR | 20.4 | 21.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 22.2 | 23.1 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 19.2 | 19.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS x2 | 20.8 | 21.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 79.8 | 81.2 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 72.1 | 76.5 | 0 | 8 | 42.9 | 3 | FAIL |
| SPREAD H CEL MIX | 72.8 | 74.5 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 69.2 | 73.5 | 0 | 8 | 44.2 | 11 | pass |
| HEAVY SPRING MIX | 58.5 | 59.7 | 0 | 8 | 56.8 | 8 | pass |
| IDLE TAPE FLANGER | 20.1 | 20.9 | 0 | 0 | 61.3 | 1 | pass |
| IDLE TAPE FLNG x2 | 21.9 | 22.8 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 77.9 | 79.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 70.9 | 74.3 | 0 | 8 | 43.2 | 11 | FAIL |
| IDLE BBD FLANGER | 19.4 | 20.1 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 20.9 | 21.6 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 76.8 | 78.7 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE PITCH SHIFT | 20.0 | 20.8 | 0 | 0 | 61.3 | 3 | pass |
| PITCH SHIFT HELD | 53.6 | 55.4 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 22.3 | 23.3 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 77.5 | 79.5 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE BBD ECHO | 20.4 | 21.1 | 0 | 0 | 61.3 | 3 | pass |
| IDLE BBD ECHO x2 | 20.4 | 21.1 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL ECHO | 77.4 | 79.1 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-03 17:02, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.2 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 26.4 | 27.5 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 47.6 | 48.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE HEAVY | 52.3 | 53.5 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 59.4 | 60.5 | 0 | 6 | 56.5 | 8 | pass |
| SPREAD | 59.3 | 60.5 | 0 | 8 | 56.6 | 8 | pass |
| DUAL FILTER | 61.5 | 62.7 | 0 | 7 | 54.2 | 8 | pass |
| STACK | 68.3 | 69.6 | 0 | 6 | 48.2 | 9 | pass |
| BINAURAL | 57.8 | 59.2 | 0 | 6 | 57.2 | 8 | pass |
| DUAL OSC HEAVY | 58.3 | 59.5 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD HEAVY | 63.7 | 65.0 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FLT HEAVY | 63.6 | 64.8 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 66.0 | 67.3 | 0 | 6 | 51.9 | 9 | pass |
| BINAURAL HEAVY | 63.1 | 64.4 | 0 | 6 | 52.2 | 8 | pass |
| SINGLE LADDER | 47.6 | 48.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 45.4 | 46.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 53.5 | 54.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.8 | 46.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 61.5 | 62.8 | 0 | 7 | 53.5 | 8 | pass |
| SEQ+ARP HOME | 60.9 | 64.9 | 0 | 8 | 54.2 | 9 | pass |
| SEQ+ARP AMP | 61.1 | 64.5 | 0 | 8 | 54.2 | 11 | pass |
| SEQ+ARP SEQ | 60.5 | 64.1 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 60.9 | 66.1 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 47.5 | 48.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 55.9 | 57.1 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 56.7 | 57.9 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.6 | 70.0 | 0 | 8 | 50.5 | 10 | pass |
| SINGLE MULTI H | 54.3 | 55.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 59.9 | 61.1 | 0 | 6 | 54.5 | 8 | pass |
| SPREAD MULTI H | 65.9 | 67.1 | 0 | 8 | 52.2 | 9 | pass |
| DUALFLT MULTI H | 68.0 | 69.4 | 0 | 7 | 47.6 | 9 | pass |
| STACK MULTI H | 69.0 | 70.5 | 0 | 6 | 43.2 | 9 | pass |
| BINAUR MULTI H | 66.1 | 67.4 | 0 | 6 | 52.2 | 9 | pass |
| SPREAD H CEL+PP | 70.0 | 71.5 | 0 | 8 | 43.9 | 9 | pass |
| DUALOSC H CEL+PP | 64.5 | 66.0 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 69.3 | 71.1 | 0 | 8 | 42.9 | 1 | pass |
| SEQ+ARP SPRD CEL | 79.5 | 84.4 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.0 | 56.0 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.5 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 63.6 | 67.8 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 48.5 | 49.8 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 53.9 | 55.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.8 | 74.4 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.8 | 76.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 60.0 | 61.3 | 0 | 8 | 55.6 | 8 | pass |
| DUALOSC SH2 H | 66.6 | 68.0 | 0 | 6 | 49.2 | 9 | pass |
| SPREAD SH2 H | 71.4 | 72.9 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 78.4 | 79.9 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 79.0 | 80.6 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 74.5 | 76.1 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 61.1 | 62.2 | 0 | 8 | 55.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 76.1 | 77.5 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 83.2 | 84.7 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 73.9 | 78.5 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 78.4 | 80.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 74.5 | 76.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 53.0 | 54.5 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 60.7 | 61.8 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD PCM | 65.6 | 66.8 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 67.8 | 69.1 | 0 | 7 | 47.6 | 9 | pass |
| STACK PCM | 69.5 | 71.1 | 0 | 6 | 43.6 | 9 | pass |
| BINAURAL PCM | 63.9 | 65.1 | 0 | 6 | 52.2 | 8 | pass |
| DUALOSC MULTI H PCM | 59.6 | 60.7 | 0 | 6 | 55.2 | 8 | pass |
| DUALFLT MULTI H PCM | 74.1 | 76.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 72.3 | 73.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.6 | 50.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.4 | 57.3 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 59.4 | 61.1 | 0 | 6 | 55.9 | 8 | pass |
| DUALOSC RS PCM | 64.0 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 69.2 | 73.2 | 0 | 8 | 43.2 | 10 | pass |
| SINGLE DRV SOFT | 51.8 | 53.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.9 | 53.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 52.0 | 53.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.6 | 53.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.2 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.4 | 53.7 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 72.6 | 74.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 71.4 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 58.4 | 59.6 | 0 | 8 | 56.9 | 8 | pass |
| SEQ+ARP HOME TUBE | 66.1 | 70.5 | 0 | 8 | 51.2 | 10 | pass |
| FULL | 53.1 | 54.2 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 51.5 | 52.7 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 54.1 | 55.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 61.0 | 62.3 | 0 | 4 | 54.2 | 8 | pass |
| FULL H CEL+PP | 59.0 | 60.4 | 0 | 4 | 57.2 | 8 | pass |
| FULL MULTI H PCM | 56.3 | 57.6 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H PCM | 62.3 | 63.6 | 0 | 4 | 52.2 | 8 | pass |
| FULL SH2 CEL+PP | 65.4 | 66.8 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 49.0 | 50.4 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 51.9 | 53.2 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 52.7 | 54.1 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 55.8 | 57.0 | 0 | 4 | 57.5 | 7 | pass |
| DSPRD H CEL+PP | 57.0 | 58.2 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 59.1 | 60.3 | 0 | 4 | 56.9 | 8 | pass |
| DSPRD SH2 CEL+PP | 60.4 | 61.5 | 0 | 4 | 54.9 | 8 | pass |
| SEQ+ARP DSPRD CEL | 66.5 | 70.6 | 0 | 4 | 50.5 | 10 | pass |
| IDLE PHASER | 18.8 | 19.5 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 20.3 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 63.1 | 64.5 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 74.7 | 76.4 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 67.3 | 71.8 | 0 | 8 | 49.2 | 10 | pass |
| IDLE ENSEMBLE | 19.6 | 20.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 21.2 | 22.0 | 0 | 0 | 61.2 | 3 | pass |
| IDLE ENS+PHSR | 20.3 | 21.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR x2 | 22.2 | 23.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS | 19.1 | 19.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS x2 | 20.7 | 21.5 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ENPH | 78.8 | 80.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 70.9 | 74.5 | 0 | 8 | 43.2 | 10 | FAIL |
| SPREAD H CEL MIX | 72.9 | 74.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 68.4 | 72.6 | 0 | 8 | 45.9 | 10 | pass |
| HEAVY SPRING MIX | 58.1 | 59.1 | 0 | 8 | 57.2 | 8 | pass |
| IDLE TAPE FLANGER | 19.9 | 20.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.8 | 22.6 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL TFLG | 77.4 | 79.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 69.8 | 74.5 | 0 | 8 | 43.6 | 10 | pass |
| IDLE BBD FLANGER | 19.3 | 20.1 | 0 | 0 | 61.6 | 1 | pass |
| IDLE BBD FLNG x2 | 20.8 | 21.6 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 76.6 | 78.1 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.9 | 20.9 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 53.3 | 55.0 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 22.2 | 23.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 76.8 | 78.7 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 20.2 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD ECHO x2 | 20.3 | 21.0 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ECHO | 76.5 | 78.1 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-03 16:25, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.2 | 16.7 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 25.5 | 26.6 | 0 | 1 | 60.6 | 4 | pass |
| SINGLE | 46.2 | 47.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.4 | 52.9 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 57.4 | 58.6 | 0 | 6 | 57.5 | 8 | pass |
| SPREAD | 58.3 | 59.4 | 0 | 8 | 57.2 | 8 | pass |
| DUAL FILTER | 60.6 | 61.8 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 66.7 | 68.0 | 0 | 6 | 48.6 | 9 | pass |
| BINAURAL | 57.3 | 58.6 | 0 | 6 | 57.6 | 7 | pass |
| DUAL OSC HEAVY | 56.2 | 57.5 | 0 | 6 | 57.6 | 7 | pass |
| SPREAD HEAVY | 63.1 | 64.5 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FLT HEAVY | 63.8 | 65.2 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 64.7 | 66.1 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 63.6 | 65.1 | 0 | 6 | 52.2 | 8 | pass |
| SINGLE LADDER | 46.2 | 47.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 43.8 | 44.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 51.4 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 44.7 | 45.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.6 | 61.9 | 0 | 7 | 55.9 | 8 | pass |
| SEQ+ARP HOME | 59.7 | 63.6 | 0 | 8 | 55.9 | 9 | pass |
| SEQ+ARP AMP | 59.9 | 64.0 | 0 | 8 | 55.5 | 11 | pass |
| SEQ+ARP SEQ | 58.9 | 62.7 | 0 | 8 | 15.9 | 116 | pass |
| SEQ+ARP MOD1 | 59.6 | 64.7 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.3 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.7 | 56.1 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.7 | 57.3 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 64.1 | 68.1 | 0 | 8 | 52.2 | 10 | pass |
| SINGLE MULTI H | 53.2 | 54.5 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 57.8 | 59.2 | 0 | 6 | 57.5 | 8 | pass |
| SPREAD MULTI H | 65.3 | 66.5 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 67.9 | 69.2 | 0 | 7 | 47.9 | 9 | pass |
| STACK MULTI H | 68.1 | 69.4 | 0 | 6 | 48.6 | 9 | pass |
| BINAUR MULTI H | 66.3 | 67.6 | 0 | 6 | 51.6 | 9 | pass |
| SPREAD H CEL+PP | 69.3 | 71.1 | 0 | 8 | 43.2 | 9 | pass |
| DUALOSC H CEL+PP | 62.0 | 63.3 | 0 | 6 | 52.6 | 8 | pass |
| SPREAD H TUM+PP | 68.8 | 70.4 | 0 | 8 | 43.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 78.0 | 81.8 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 53.5 | 54.8 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 55.7 | 57.0 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 62.5 | 66.2 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 47.7 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.2 | 54.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.8 | 75.7 | 0 | 8 | 43.9 | 10 | FAIL |
| SEQ+ARP STRK FILT | 70.8 | 75.0 | 0 | 8 | 43.2 | 11 | FAIL |
| SINGLE SH2 H | 58.1 | 59.3 | 0 | 8 | 57.6 | 8 | pass |
| DUALOSC SH2 H | 61.6 | 62.9 | 0 | 6 | 52.9 | 8 | pass |
| SPREAD SH2 H | 69.8 | 71.3 | 0 | 8 | 43.2 | 9 | pass |
| DUALFLT SH2 H | 77.3 | 78.8 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.4 | 76.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 73.7 | 75.2 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 60.3 | 61.6 | 0 | 8 | 55.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 74.7 | 76.3 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 81.7 | 83.3 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 71.1 | 75.3 | 0 | 8 | 43.2 | 10 | FAIL |
| DUALFLT SH2 HP | 77.3 | 78.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 73.7 | 75.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.6 | 53.1 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 60.1 | 61.3 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD PCM | 63.8 | 65.0 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 65.9 | 66.9 | 0 | 7 | 50.9 | 8 | pass |
| STACK PCM | 70.5 | 72.0 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAURAL PCM | 63.4 | 64.6 | 0 | 6 | 52.2 | 8 | pass |
| DUALOSC MULTI H PCM | 59.7 | 60.9 | 0 | 6 | 55.2 | 8 | pass |
| DUALFLT MULTI H PCM | 73.0 | 74.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 72.5 | 74.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.3 | 48.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.1 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 57.5 | 59.1 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 62.7 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 67.8 | 71.9 | 0 | 8 | 46.2 | 10 | pass |
| SINGLE DRV SOFT | 52.5 | 53.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 52.6 | 54.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 52.5 | 53.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 53.4 | 54.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 53.2 | 54.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 53.3 | 54.6 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 73.8 | 75.7 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 71.0 | 73.1 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 59.7 | 61.1 | 0 | 8 | 56.3 | 8 | pass |
| SEQ+ARP HOME TUBE | 67.5 | 72.2 | 0 | 8 | 46.9 | 10 | pass |
| FULL | 51.9 | 53.1 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 51.0 | 52.3 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 52.9 | 54.2 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H | 58.1 | 59.3 | 0 | 4 | 57.2 | 8 | pass |
| FULL H CEL+PP | 57.1 | 58.5 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H PCM | 54.8 | 56.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 60.9 | 62.2 | 0 | 4 | 55.2 | 8 | pass |
| FULL SH2 CEL+PP | 62.8 | 64.1 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 47.5 | 48.9 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 51.5 | 52.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 51.7 | 53.0 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 54.2 | 55.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 55.8 | 57.1 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD MULTI H PCM | 58.0 | 59.2 | 0 | 4 | 57.2 | 8 | pass |
| DSPRD SH2 CEL+PP | 59.2 | 60.3 | 0 | 4 | 56.5 | 8 | pass |
| SEQ+ARP DSPRD CEL | 65.0 | 69.0 | 0 | 4 | 51.2 | 10 | pass |
| IDLE PHASER | 18.3 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 19.7 | 20.4 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD PHASER | 62.2 | 63.5 | 0 | 8 | 52.5 | 8 | pass |
| SPREAD H CEL PHSR | 73.0 | 74.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 65.6 | 69.8 | 0 | 8 | 51.2 | 10 | pass |
| IDLE ENSEMBLE | 19.0 | 19.7 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENSEMBLE x2 | 20.6 | 21.3 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR | 19.7 | 20.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 21.6 | 22.4 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 18.7 | 19.4 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS x2 | 20.3 | 21.1 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ENPH | 77.9 | 79.3 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 68.8 | 72.6 | 0 | 8 | 44.9 | 10 | pass |
| SPREAD H CEL MIX | 71.9 | 73.5 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 66.8 | 71.0 | 0 | 8 | 49.9 | 10 | pass |
| HEAVY SPRING MIX | 56.3 | 57.7 | 0 | 8 | 57.3 | 7 | pass |
| IDLE TAPE FLANGER | 19.4 | 20.1 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.3 | 22.0 | 0 | 0 | 61.2 | 1 | pass |
| SPREAD H CEL TFLG | 75.2 | 77.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 67.9 | 72.5 | 0 | 8 | 44.9 | 10 | pass |
| IDLE BBD FLANGER | 19.0 | 19.8 | 0 | 0 | 61.3 | 3 | pass |
| IDLE BBD FLNG x2 | 20.4 | 21.1 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL BBDF | 75.4 | 77.2 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.2 | 20.0 | 0 | 0 | 61.6 | 1 | pass |
| PITCH SHIFT HELD | 51.4 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.5 | 22.6 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 75.3 | 77.1 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 19.7 | 20.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD ECHO x2 | 19.8 | 20.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ECHO | 75.1 | 76.8 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-03 14:41, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.0 | 16.5 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 25.4 | 26.4 | 0 | 1 | 60.6 | 4 | pass |
| SINGLE | 46.6 | 47.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.1 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 57.9 | 59.0 | 0 | 6 | 57.5 | 8 | pass |
| SPREAD | 58.8 | 59.9 | 0 | 8 | 56.5 | 8 | pass |
| DUAL FILTER | 60.5 | 61.4 | 0 | 7 | 55.9 | 8 | pass |
| STACK | 67.6 | 69.0 | 0 | 6 | 47.2 | 9 | pass |
| BINAURAL | 62.3 | 63.7 | 0 | 6 | 51.9 | 8 | pass |
| DUAL OSC HEAVY | 57.4 | 58.5 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD HEAVY | 63.7 | 65.1 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FLT HEAVY | 63.6 | 64.8 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 65.3 | 66.5 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 67.9 | 69.6 | 0 | 6 | 47.9 | 9 | pass |
| SINGLE LADDER | 46.7 | 47.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.5 | 45.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 51.9 | 53.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE FORMANT | 44.9 | 46.0 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.5 | 61.6 | 0 | 7 | 55.9 | 8 | pass |
| SEQ+ARP HOME | 60.6 | 64.4 | 0 | 8 | 55.2 | 10 | pass |
| SEQ+ARP AMP | 60.9 | 64.5 | 0 | 8 | 54.9 | 11 | pass |
| SEQ+ARP SEQ | 60.0 | 63.8 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 60.5 | 65.8 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.6 | 47.7 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.2 | 56.6 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.0 | 57.4 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 65.2 | 69.8 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE MULTI H | 53.5 | 54.6 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 58.5 | 59.6 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD MULTI H | 65.7 | 66.9 | 0 | 8 | 51.5 | 8 | pass |
| DUALFLT MULTI H | 67.2 | 68.3 | 0 | 7 | 45.9 | 9 | pass |
| STACK MULTI H | 68.3 | 69.6 | 0 | 6 | 47.9 | 9 | pass |
| BINAUR MULTI H | 71.1 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 69.4 | 71.0 | 0 | 8 | 43.2 | 9 | pass |
| DUALOSC H CEL+PP | 63.5 | 64.9 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 68.7 | 70.4 | 0 | 8 | 43.6 | 9 | pass |
| SEQ+ARP SPRD CEL | 79.8 | 85.1 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 53.7 | 54.9 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 55.5 | 56.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 63.0 | 67.2 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 47.3 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.7 | 53.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.7 | 75.8 | 0 | 8 | 43.5 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.2 | 75.3 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE SH2 H | 58.9 | 60.0 | 0 | 8 | 56.9 | 8 | pass |
| DUALOSC SH2 H | 62.5 | 63.7 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 70.7 | 72.2 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 77.1 | 78.7 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 77.6 | 79.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 80.2 | 81.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.5 | 60.9 | 0 | 8 | 55.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 75.3 | 76.8 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 82.4 | 83.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 72.5 | 78.1 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 77.2 | 78.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 80.2 | 81.8 | 0 | 6 | 43.2 | 1 | FAIL |
| SINGLE PCM | 51.7 | 53.1 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.7 | 60.8 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD PCM | 64.8 | 65.9 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 65.8 | 66.9 | 0 | 7 | 50.6 | 8 | pass |
| STACK PCM | 69.7 | 71.0 | 0 | 6 | 43.2 | 9 | pass |
| BINAURAL PCM | 69.7 | 71.5 | 0 | 6 | 43.9 | 9 | pass |
| DUALOSC MULTI H PCM | 58.6 | 59.8 | 0 | 6 | 56.2 | 8 | pass |
| DUALFLT MULTI H PCM | 72.8 | 74.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 78.1 | 79.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.7 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.5 | 56.2 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 58.0 | 59.9 | 0 | 6 | 56.9 | 8 | pass |
| DUALOSC RS PCM | 62.8 | 65.1 | 0 | 6 | 52.2 | 8 | pass |
| SEQ+ARP HOME PCM | 68.6 | 73.2 | 0 | 8 | 44.6 | 10 | pass |
| SINGLE DRV SOFT | 51.7 | 52.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.9 | 53.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 51.9 | 53.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.7 | 53.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.4 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.5 | 53.8 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 73.3 | 75.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 79.0 | 80.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 59.8 | 61.1 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP HOME TUBE | 67.5 | 71.4 | 0 | 8 | 48.6 | 10 | pass |
| FULL | 52.6 | 53.8 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 51.7 | 52.9 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 53.5 | 54.6 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 58.6 | 59.9 | 0 | 4 | 56.6 | 8 | pass |
| FULL H CEL+PP | 58.6 | 59.9 | 0 | 4 | 56.6 | 8 | pass |
| FULL MULTI H PCM | 55.6 | 56.7 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 60.3 | 61.4 | 0 | 4 | 55.6 | 8 | pass |
| FULL SH2 CEL+PP | 64.1 | 65.3 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 47.9 | 49.0 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 51.8 | 52.9 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 51.9 | 53.1 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 54.3 | 55.5 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 56.3 | 57.7 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD MULTI H PCM | 58.6 | 59.7 | 0 | 4 | 56.9 | 8 | pass |
| DSPRD SH2 CEL+PP | 59.3 | 60.5 | 0 | 4 | 55.5 | 8 | pass |
| SEQ+ARP DSPRD CEL | 67.1 | 71.7 | 0 | 4 | 48.6 | 10 | pass |
| IDLE PHASER | 18.1 | 18.8 | 0 | 0 | 61.3 | 1 | pass |
| IDLE PHASER x2 | 19.5 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 62.6 | 63.7 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 74.6 | 76.2 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 66.8 | 70.8 | 0 | 8 | 48.5 | 10 | pass |
| IDLE ENSEMBLE | 19.3 | 20.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 20.8 | 21.5 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 19.8 | 20.7 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 21.7 | 22.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 18.5 | 19.3 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 20.0 | 20.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 79.1 | 80.7 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 70.5 | 74.8 | 0 | 8 | 43.2 | 11 | FAIL |
| SPREAD H CEL MIX | 72.5 | 74.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 67.9 | 72.2 | 0 | 8 | 45.9 | 10 | pass |
| HEAVY SPRING MIX | 56.9 | 58.2 | 0 | 8 | 57.6 | 1 | pass |
| IDLE TAPE FLANGER | 19.1 | 19.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.0 | 21.8 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 77.5 | 79.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 69.0 | 73.3 | 0 | 8 | 43.9 | 10 | pass |
| IDLE BBD FLANGER | 18.6 | 19.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 20.1 | 20.9 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL BBDF | 76.4 | 78.1 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.1 | 19.9 | 0 | 0 | 61.3 | 3 | pass |
| PITCH SHIFT HELD | 52.5 | 54.2 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.3 | 22.4 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 76.2 | 78.3 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 19.6 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD ECHO x2 | 19.6 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ECHO | 76.2 | 78.0 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-03 14:30, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.0 | 16.5 | 0 | 0 | 61.6 | 2 | pass |
| SINGLE 1V | 25.6 | 26.6 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 46.7 | 47.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.5 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 58.3 | 59.5 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD | 58.6 | 59.7 | 0 | 8 | 56.9 | 8 | pass |
| DUAL FILTER | 61.0 | 62.1 | 0 | 7 | 55.5 | 8 | pass |
| STACK | 67.8 | 69.2 | 0 | 6 | 47.2 | 9 | pass |
| BINAURAL | 62.0 | 63.4 | 0 | 6 | 51.9 | 8 | pass |
| DUAL OSC HEAVY | 56.9 | 58.1 | 0 | 6 | 57.2 | 1 | pass |
| SPREAD HEAVY | 63.3 | 64.7 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 63.6 | 64.9 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 64.9 | 66.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 66.3 | 67.8 | 0 | 6 | 51.9 | 9 | pass |
| SINGLE LADDER | 46.7 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 44.5 | 45.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 52.0 | 53.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.9 | 45.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 61.0 | 62.2 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 60.3 | 63.7 | 0 | 8 | 55.9 | 10 | pass |
| SEQ+ARP AMP | 60.5 | 64.1 | 0 | 8 | 55.2 | 11 | pass |
| SEQ+ARP SEQ | 59.6 | 63.5 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 60.1 | 65.1 | 0 | 8 | 17.2 | 59 | pass |
| LADDER FILTER PG | 46.7 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.7 | 56.0 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.6 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 64.8 | 69.3 | 0 | 8 | 51.5 | 10 | pass |
| SINGLE MULTI H | 53.6 | 54.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 58.7 | 59.8 | 0 | 6 | 56.5 | 8 | pass |
| SPREAD MULTI H | 65.4 | 66.6 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 67.6 | 68.9 | 0 | 7 | 46.9 | 9 | pass |
| STACK MULTI H | 68.0 | 69.3 | 0 | 6 | 46.6 | 9 | pass |
| BINAUR MULTI H | 70.7 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 69.2 | 70.8 | 0 | 8 | 42.9 | 1 | pass |
| DUALOSC H CEL+PP | 63.3 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 68.3 | 69.8 | 0 | 8 | 48.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 78.8 | 83.0 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 53.7 | 55.0 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.4 | 57.0 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP SPR+BBD | 62.7 | 66.6 | 0 | 8 | 52.5 | 10 | pass |
| STRIKE | 47.4 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.7 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.4 | 75.1 | 0 | 8 | 43.2 | 11 | FAIL |
| SEQ+ARP STRK FILT | 70.8 | 75.7 | 0 | 8 | 43.6 | 11 | FAIL |
| SINGLE SH2 H | 58.8 | 60.0 | 0 | 8 | 56.9 | 8 | pass |
| DUALOSC SH2 H | 62.4 | 63.7 | 0 | 6 | 52.5 | 8 | pass |
| SPREAD SH2 H | 70.3 | 71.6 | 0 | 8 | 43.9 | 9 | FAIL |
| DUALFLT SH2 H | 77.7 | 79.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 76.1 | 77.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 80.2 | 81.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.5 | 60.8 | 0 | 8 | 56.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 74.7 | 76.2 | 0 | 8 | 43.2 | 1 | FAIL |
| DUALFLT SH2 CEL | 82.3 | 83.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 72.0 | 77.1 | 0 | 8 | 43.2 | 10 | FAIL |
| DUALFLT SH2 HP | 77.7 | 79.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 80.2 | 81.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 52.1 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 60.2 | 61.5 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD PCM | 65.0 | 66.2 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 66.1 | 67.3 | 0 | 7 | 51.6 | 9 | pass |
| STACK PCM | 70.3 | 71.8 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAURAL PCM | 69.9 | 71.5 | 0 | 6 | 43.6 | 9 | pass |
| DUALOSC MULTI H PCM | 59.3 | 60.5 | 0 | 6 | 55.9 | 8 | pass |
| DUALFLT MULTI H PCM | 73.0 | 74.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 77.9 | 79.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.8 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.7 | 56.6 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 58.4 | 60.4 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC RS PCM | 63.4 | 65.5 | 0 | 6 | 52.2 | 8 | pass |
| SEQ+ARP HOME PCM | 68.3 | 72.2 | 0 | 8 | 45.6 | 10 | pass |
| SINGLE DRV SOFT | 51.7 | 53.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.9 | 53.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 52.0 | 53.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.8 | 53.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.4 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.5 | 53.6 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 73.1 | 74.7 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 76.5 | 78.2 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 58.8 | 60.1 | 0 | 8 | 56.9 | 8 | pass |
| SEQ+ARP HOME TUBE | 67.1 | 71.5 | 0 | 8 | 47.9 | 10 | pass |
| FULL | 53.1 | 54.3 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 51.5 | 52.8 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 54.0 | 55.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 58.9 | 60.1 | 0 | 4 | 56.9 | 8 | pass |
| FULL H CEL+PP | 58.4 | 59.9 | 0 | 4 | 57.2 | 8 | pass |
| FULL MULTI H PCM | 56.8 | 58.1 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H PCM | 60.2 | 61.4 | 0 | 4 | 55.2 | 8 | pass |
| FULL SH2 CEL+PP | 64.0 | 65.5 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 48.0 | 49.2 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 51.9 | 53.1 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 52.0 | 53.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 54.6 | 56.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 56.3 | 58.0 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD MULTI H PCM | 58.7 | 60.4 | 0 | 4 | 56.5 | 8 | pass |
| DSPRD SH2 CEL+PP | 59.1 | 60.5 | 0 | 4 | 56.6 | 8 | pass |
| SEQ+ARP DSPRD CEL | 66.7 | 70.9 | 0 | 4 | 48.6 | 10 | pass |
| IDLE PHASER | 18.2 | 18.9 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 19.6 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 62.6 | 63.9 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 74.0 | 75.5 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 66.7 | 70.6 | 0 | 8 | 48.9 | 10 | pass |
| IDLE ENSEMBLE | 19.3 | 20.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 20.9 | 21.7 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR | 19.9 | 20.9 | 0 | 0 | 61.2 | 3 | pass |
| IDLE ENS+PHSR x2 | 21.7 | 22.6 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 18.5 | 19.3 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 20.0 | 20.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 78.3 | 79.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 69.9 | 73.7 | 0 | 8 | 43.9 | 11 | pass |
| SPREAD H CEL MIX | 72.2 | 73.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 67.5 | 71.8 | 0 | 8 | 47.6 | 10 | pass |
| HEAVY SPRING MIX | 56.9 | 58.2 | 0 | 8 | 57.3 | 7 | pass |
| IDLE TAPE FLANGER | 19.2 | 19.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.0 | 21.8 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 77.0 | 79.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 68.7 | 72.9 | 0 | 8 | 43.9 | 10 | pass |
| IDLE BBD FLANGER | 18.8 | 19.6 | 0 | 0 | 61.3 | 1 | pass |
| IDLE BBD FLNG x2 | 20.2 | 21.0 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL BBDF | 75.7 | 77.1 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.2 | 20.3 | 0 | 0 | 61.6 | 1 | pass |
| PITCH SHIFT HELD | 52.3 | 54.2 | 0 | 8 | 57.6 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.4 | 22.5 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 75.8 | 77.7 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 19.6 | 20.4 | 0 | 0 | 61.6 | 1 | pass |
| IDLE BBD ECHO x2 | 19.7 | 20.4 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ECHO | 75.6 | 77.3 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-03 14:17, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.0 | 16.6 | 0 | 0 | 61.6 | 2 | pass |
| SINGLE 1V | 26.3 | 27.3 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 48.2 | 49.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.7 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 59.8 | 60.9 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD | 60.0 | 61.0 | 0 | 8 | 54.2 | 8 | pass |
| DUAL FILTER | 62.5 | 63.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK | 69.0 | 70.5 | 0 | 6 | 43.2 | 9 | pass |
| BINAURAL | 62.3 | 63.6 | 0 | 6 | 52.2 | 8 | pass |
| DUAL OSC HEAVY | 58.4 | 59.5 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD HEAVY | 64.3 | 65.6 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FLT HEAVY | 64.9 | 66.0 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 66.1 | 67.2 | 0 | 6 | 51.9 | 9 | pass |
| BINAURAL HEAVY | 66.3 | 67.6 | 0 | 6 | 50.9 | 9 | pass |
| SINGLE LADDER | 48.2 | 49.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 45.5 | 46.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 53.8 | 54.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 46.1 | 47.2 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 62.5 | 63.6 | 0 | 7 | 52.2 | 8 | pass |
| SEQ+ARP HOME | 60.9 | 65.3 | 0 | 8 | 53.9 | 10 | pass |
| SEQ+ARP AMP | 61.1 | 65.1 | 0 | 8 | 55.5 | 11 | pass |
| SEQ+ARP SEQ | 60.2 | 64.2 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 60.7 | 66.5 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 48.2 | 49.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 56.2 | 57.4 | 0 | 8 | 57.2 | 7 | pass |
| HEAVY CELEST+PP | 56.9 | 58.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 65.6 | 70.1 | 0 | 8 | 50.9 | 10 | pass |
| SINGLE MULTI H | 54.9 | 56.0 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 60.1 | 61.4 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD MULTI H | 66.7 | 67.7 | 0 | 8 | 48.9 | 9 | pass |
| DUALFLT MULTI H | 69.1 | 70.3 | 0 | 7 | 43.9 | 9 | pass |
| STACK MULTI H | 69.0 | 70.7 | 0 | 6 | 43.2 | 9 | pass |
| BINAUR MULTI H | 71.0 | 72.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 70.5 | 72.3 | 0 | 8 | 43.2 | 9 | FAIL |
| DUALOSC H CEL+PP | 64.5 | 66.0 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 69.8 | 71.4 | 0 | 8 | 43.6 | 9 | pass |
| SEQ+ARP SPRD CEL | 79.8 | 84.1 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.0 | 56.2 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.8 | 58.1 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 63.6 | 67.6 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 49.5 | 50.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 54.5 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.5 | 76.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.3 | 76.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 60.4 | 61.5 | 0 | 8 | 55.2 | 8 | pass |
| DUALOSC SH2 H | 64.2 | 65.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 71.9 | 73.4 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 79.1 | 80.7 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 77.4 | 79.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 80.8 | 82.2 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 62.0 | 63.2 | 0 | 8 | 53.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 76.3 | 77.9 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 83.9 | 85.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 73.2 | 77.4 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 79.2 | 80.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 80.9 | 82.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 53.7 | 55.1 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC PCM | 63.3 | 64.6 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD PCM | 66.8 | 67.8 | 0 | 8 | 46.9 | 9 | pass |
| DUAL FILTER PCM | 68.9 | 70.3 | 0 | 7 | 43.9 | 9 | pass |
| STACK PCM | 71.5 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAURAL PCM | 69.1 | 70.6 | 0 | 6 | 43.2 | 9 | pass |
| DUALOSC MULTI H PCM | 62.5 | 63.7 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT MULTI H PCM | 75.8 | 77.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 77.5 | 79.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 48.2 | 50.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 56.3 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC RS | 59.8 | 61.9 | 0 | 6 | 54.9 | 8 | pass |
| DUALOSC RS PCM | 66.2 | 67.8 | 0 | 6 | 50.2 | 9 | pass |
| SEQ+ARP HOME PCM | 69.2 | 73.6 | 0 | 8 | 43.9 | 10 | pass |
| SINGLE DRV SOFT | 53.0 | 54.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 53.1 | 54.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 53.1 | 54.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 53.9 | 54.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 53.5 | 54.6 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 53.6 | 54.8 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 73.6 | 75.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 75.4 | 77.1 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 60.3 | 61.5 | 0 | 8 | 55.2 | 8 | pass |
| SEQ+ARP HOME TUBE | 67.3 | 71.9 | 0 | 8 | 47.2 | 10 | pass |
| FULL | 53.6 | 54.7 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 52.1 | 53.4 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 54.4 | 55.6 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 60.0 | 61.4 | 0 | 4 | 55.5 | 8 | pass |
| FULL H CEL+PP | 58.7 | 60.0 | 0 | 4 | 56.9 | 8 | pass |
| FULL MULTI H PCM | 56.2 | 57.4 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H PCM | 61.3 | 62.6 | 0 | 4 | 54.2 | 8 | pass |
| FULL SH2 CEL+PP | 64.2 | 65.6 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 48.8 | 49.9 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 52.2 | 53.3 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 52.7 | 53.8 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 55.4 | 56.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 57.0 | 58.3 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD MULTI H PCM | 59.7 | 60.8 | 0 | 4 | 54.9 | 8 | pass |
| DSPRD SH2 CEL+PP | 59.9 | 61.2 | 0 | 4 | 55.6 | 8 | pass |
| SEQ+ARP DSPRD CEL | 66.6 | 70.9 | 0 | 4 | 46.9 | 10 | pass |
| IDLE PHASER | 18.2 | 18.9 | 0 | 0 | 61.3 | 1 | pass |
| IDLE PHASER x2 | 19.6 | 20.4 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD PHASER | 64.0 | 65.2 | 0 | 8 | 51.9 | 8 | pass |
| SPREAD H CEL PHSR | 75.4 | 77.4 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 67.3 | 71.2 | 0 | 8 | 46.6 | 10 | pass |
| IDLE ENSEMBLE | 19.2 | 20.1 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 20.8 | 21.6 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 19.8 | 20.7 | 0 | 0 | 61.2 | 3 | pass |
| IDLE ENS+PHSR x2 | 21.7 | 22.6 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 18.5 | 19.1 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 20.0 | 20.8 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL ENPH | 80.0 | 81.5 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 70.5 | 75.3 | 0 | 8 | 43.2 | 10 | FAIL |
| SPREAD H CEL MIX | 73.6 | 75.2 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 68.2 | 72.8 | 0 | 8 | 45.6 | 10 | pass |
| HEAVY SPRING MIX | 58.3 | 59.4 | 0 | 8 | 57.2 | 8 | pass |
| IDLE TAPE FLANGER | 19.3 | 20.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.1 | 21.8 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 78.3 | 80.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 69.7 | 73.8 | 0 | 8 | 43.5 | 10 | pass |
| IDLE BBD FLANGER | 18.8 | 19.6 | 0 | 0 | 61.3 | 1 | pass |
| IDLE BBD FLNG x2 | 20.3 | 21.1 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL BBDF | 77.5 | 79.1 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.2 | 20.2 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 53.5 | 55.3 | 0 | 8 | 57.6 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.4 | 22.4 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 77.1 | 79.0 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 19.7 | 20.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD ECHO x2 | 19.7 | 20.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ECHO | 77.5 | 79.5 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-03 12:11, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.9 | 16.4 | 0 | 0 | 61.9 | 2 | pass |
| SINGLE 1V | 24.7 | 25.7 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 45.1 | 46.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 50.6 | 51.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 56.7 | 57.9 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD | 56.9 | 58.0 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FILTER | 59.1 | 60.2 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 65.2 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL | 54.6 | 55.9 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 56.3 | 57.5 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD HEAVY | 61.7 | 62.8 | 0 | 8 | 52.5 | 8 | pass |
| DUAL FLT HEAVY | 63.2 | 64.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.5 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.0 | 62.0 | 0 | 6 | 56.6 | 8 | pass |
| SINGLE LADDER | 45.2 | 46.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 42.5 | 43.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 50.2 | 51.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.0 | 45.2 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.1 | 60.2 | 0 | 7 | 56.5 | 8 | pass |
| SEQ+ARP HOME | 58.8 | 62.2 | 0 | 8 | 56.5 | 9 | pass |
| SEQ+ARP AMP | 58.9 | 62.2 | 0 | 8 | 56.2 | 11 | pass |
| SEQ+ARP SEQ | 58.4 | 62.2 | 0 | 8 | 16.3 | 116 | pass |
| SEQ+ARP MOD1 | 58.7 | 63.6 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 45.1 | 46.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 53.6 | 54.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.4 | 55.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 63.1 | 66.8 | 0 | 8 | 52.2 | 10 | pass |
| SINGLE MULTI H | 51.9 | 52.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 56.9 | 58.0 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD MULTI H | 63.5 | 64.7 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 66.1 | 67.4 | 0 | 7 | 51.6 | 8 | pass |
| STACK MULTI H | 66.0 | 67.3 | 0 | 6 | 50.9 | 8 | pass |
| BINAUR MULTI H | 63.1 | 64.4 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 67.1 | 68.4 | 0 | 8 | 46.9 | 9 | pass |
| DUALOSC H CEL+PP | 60.9 | 62.2 | 0 | 6 | 55.6 | 8 | pass |
| SPREAD H TUM+PP | 66.7 | 68.0 | 0 | 8 | 48.5 | 9 | pass |
| SEQ+ARP SPRD CEL | 75.8 | 79.9 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 53.1 | 54.2 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 54.0 | 55.2 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 61.5 | 65.1 | 0 | 8 | 53.2 | 10 | pass |
| STRIKE | 45.9 | 47.0 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 51.4 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 67.3 | 71.9 | 0 | 8 | 48.6 | 10 | pass |
| SEQ+ARP STRK FILT | 68.1 | 72.3 | 0 | 8 | 45.9 | 10 | pass |
| SINGLE SH2 H | 56.9 | 57.9 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC SH2 H | 62.3 | 63.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 68.5 | 69.9 | 0 | 8 | 46.2 | 9 | pass |
| DUALFLT SH2 H | 76.3 | 78.1 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.4 | 74.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 70.7 | 72.2 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 57.3 | 58.3 | 0 | 8 | 57.6 | 1 | pass |
| SPREAD SH2 CEL+PP | 72.9 | 74.5 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.7 | 82.2 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 69.6 | 73.9 | 0 | 8 | 44.5 | 10 | pass |
| DUALFLT SH2 HP | 76.3 | 77.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.8 | 72.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 49.3 | 50.6 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 57.8 | 59.0 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 62.3 | 63.3 | 0 | 8 | 52.9 | 8 | pass |
| DUAL FILTER PCM | 62.8 | 63.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 66.5 | 67.6 | 0 | 6 | 50.2 | 9 | pass |
| BINAURAL PCM | 60.2 | 61.2 | 0 | 6 | 54.2 | 8 | pass |
| DUALOSC MULTI H PCM | 56.6 | 57.8 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 69.5 | 70.9 | 0 | 7 | 42.9 | 1 | pass |
| BINAUR MULTI H PCM | 68.7 | 69.9 | 0 | 6 | 44.9 | 9 | pass |
| SINGLE RS | 45.2 | 47.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 52.3 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 56.8 | 58.2 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 61.2 | 62.8 | 0 | 6 | 54.5 | 8 | pass |
| SEQ+ARP HOME PCM | 66.3 | 70.0 | 0 | 8 | 48.2 | 10 | pass |
| SINGLE DRV SOFT | 49.6 | 50.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 49.8 | 50.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 49.7 | 50.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 50.5 | 51.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 50.4 | 51.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 50.5 | 51.6 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 71.3 | 72.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 68.5 | 69.6 | 0 | 6 | 46.9 | 9 | pass |
| STRIKE HEAVY TUBE | 57.0 | 58.1 | 0 | 8 | 57.6 | 7 | pass |
| SEQ+ARP HOME TUBE | 65.2 | 68.8 | 0 | 8 | 51.9 | 10 | pass |
| FULL | 50.7 | 51.9 | 0 | 4 | 57.5 | 1 | pass |
| FULL HEAVY | 50.4 | 51.6 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 51.9 | 53.0 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H | 58.7 | 59.9 | 0 | 4 | 56.9 | 8 | pass |
| FULL H CEL+PP | 56.1 | 57.6 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H PCM | 51.7 | 53.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 60.4 | 61.6 | 0 | 4 | 54.5 | 8 | pass |
| FULL SH2 CEL+PP | 63.3 | 64.8 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 46.5 | 47.6 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 50.0 | 51.2 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 49.8 | 51.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 52.9 | 54.2 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 54.0 | 55.2 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 56.2 | 57.2 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 57.5 | 58.8 | 0 | 4 | 57.2 | 7 | pass |
| SEQ+ARP DSPRD CEL | 63.0 | 67.0 | 0 | 4 | 52.5 | 10 | pass |
| IDLE PHASER | 18.0 | 18.5 | 0 | 0 | 61.3 | 1 | pass |
| IDLE PHASER x2 | 19.5 | 20.1 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD PHASER | 59.6 | 60.8 | 0 | 8 | 57.2 | 8 | pass |
| SPREAD H CEL PHSR | 71.2 | 72.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 64.3 | 68.1 | 0 | 8 | 51.9 | 10 | pass |
| IDLE ENSEMBLE | 18.6 | 19.3 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 20.2 | 20.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR | 19.2 | 20.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR x2 | 21.1 | 21.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS | 18.5 | 19.2 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 20.0 | 20.6 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL ENPH | 73.9 | 75.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 67.2 | 71.3 | 0 | 8 | 47.6 | 10 | pass |
| SPREAD H CEL MIX | 69.5 | 71.2 | 0 | 8 | 42.9 | 9 | pass |
| SEQ+ARP CEL MIX | 65.9 | 69.4 | 0 | 8 | 51.5 | 10 | pass |
| HEAVY SPRING MIX | 56.0 | 57.0 | 0 | 8 | 57.5 | 7 | pass |
| IDLE TAPE FLANGER | 19.4 | 20.0 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.3 | 22.0 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 74.6 | 76.3 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 67.2 | 71.0 | 0 | 8 | 46.9 | 10 | pass |
| IDLE BBD FLANGER | 18.5 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 20.0 | 20.7 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 72.4 | 74.0 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE PITCH SHIFT | 18.9 | 19.8 | 0 | 0 | 61.3 | 3 | pass |
| PITCH SHIFT HELD | 49.8 | 51.4 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.2 | 22.2 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 72.3 | 74.1 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 19.5 | 20.1 | 0 | 0 | 61.6 | 1 | pass |
| IDLE BBD ECHO x2 | 19.5 | 20.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ECHO | 73.1 | 74.6 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-03 11:03, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.8 | 16.3 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 24.2 | 25.1 | 0 | 1 | 61.0 | 4 | pass |
| SINGLE | 44.7 | 45.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 50.0 | 51.1 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 56.7 | 57.7 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD | 56.6 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FILTER | 59.0 | 60.0 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 65.1 | 66.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL | 54.3 | 55.4 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 55.6 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| SPREAD HEAVY | 61.7 | 62.7 | 0 | 8 | 53.2 | 8 | pass |
| DUAL FLT HEAVY | 63.1 | 64.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.4 | 64.6 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.0 | 62.1 | 0 | 6 | 55.2 | 8 | pass |
| SINGLE LADDER | 44.7 | 45.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 42.2 | 43.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 49.9 | 50.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 43.4 | 44.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.0 | 60.0 | 0 | 7 | 57.2 | 8 | pass |
| SEQ+ARP HOME | 58.3 | 62.0 | 0 | 8 | 56.9 | 9 | pass |
| SEQ+ARP AMP | 58.4 | 62.5 | 0 | 8 | 56.8 | 11 | pass |
| SEQ+ARP SEQ | 57.7 | 61.7 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 58.1 | 63.7 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 44.6 | 45.6 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 53.1 | 54.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 53.9 | 55.2 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 62.8 | 66.7 | 0 | 8 | 51.8 | 10 | pass |
| SINGLE MULTI H | 51.5 | 52.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 56.8 | 58.0 | 0 | 6 | 57.6 | 7 | pass |
| SPREAD MULTI H | 63.3 | 64.5 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 66.1 | 67.2 | 0 | 7 | 51.6 | 8 | pass |
| STACK MULTI H | 65.8 | 66.9 | 0 | 6 | 51.2 | 8 | pass |
| BINAUR MULTI H | 62.8 | 64.0 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 67.0 | 68.3 | 0 | 8 | 46.9 | 9 | pass |
| DUALOSC H CEL+PP | 61.0 | 62.6 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD H TUM+PP | 66.7 | 68.1 | 0 | 8 | 48.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 76.1 | 80.0 | 0 | 8 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 52.5 | 53.6 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 54.0 | 55.2 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 61.4 | 65.5 | 0 | 8 | 53.2 | 9 | pass |
| STRIKE | 45.4 | 46.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 51.2 | 52.2 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 67.3 | 72.0 | 0 | 8 | 48.2 | 10 | pass |
| SEQ+ARP STRK FILT | 68.4 | 72.9 | 0 | 8 | 46.2 | 10 | pass |
| SINGLE SH2 H | 56.6 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC SH2 H | 62.0 | 63.1 | 0 | 6 | 54.2 | 8 | pass |
| SPREAD SH2 H | 68.3 | 69.4 | 0 | 8 | 45.6 | 9 | pass |
| DUALFLT SH2 H | 75.7 | 76.9 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.7 | 75.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 70.5 | 71.8 | 0 | 6 | 43.2 | 9 | FAIL |
| STRIKE SH2 H | 57.5 | 58.5 | 0 | 8 | 57.6 | 1 | pass |
| SPREAD SH2 CEL+PP | 72.7 | 74.2 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.1 | 81.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 69.4 | 74.2 | 0 | 8 | 44.5 | 11 | pass |
| DUALFLT SH2 HP | 75.7 | 76.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.5 | 71.9 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 48.7 | 49.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 57.5 | 58.7 | 0 | 6 | 57.2 | 1 | pass |
| SPREAD PCM | 62.1 | 63.1 | 0 | 8 | 52.9 | 8 | pass |
| DUAL FILTER PCM | 62.5 | 63.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 66.5 | 67.5 | 0 | 6 | 51.6 | 9 | pass |
| BINAURAL PCM | 59.6 | 60.7 | 0 | 6 | 55.9 | 8 | pass |
| DUALOSC MULTI H PCM | 56.6 | 57.6 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 69.4 | 70.7 | 0 | 7 | 42.9 | 1 | pass |
| BINAUR MULTI H PCM | 68.1 | 69.2 | 0 | 6 | 46.6 | 9 | pass |
| SINGLE RS | 44.8 | 46.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.7 | 53.6 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 56.7 | 58.3 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 60.8 | 62.7 | 0 | 6 | 55.5 | 8 | pass |
| SEQ+ARP HOME PCM | 65.9 | 69.6 | 0 | 8 | 50.9 | 10 | pass |
| SINGLE DRV SOFT | 49.3 | 50.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 49.4 | 50.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 49.2 | 50.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 50.1 | 51.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 50.0 | 51.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 50.1 | 51.2 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 70.6 | 72.1 | 0 | 6 | 43.5 | 9 | FAIL |
| BINAUR HEAVY OD | 68.3 | 69.5 | 0 | 6 | 46.2 | 9 | pass |
| STRIKE HEAVY TUBE | 56.9 | 58.0 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 64.6 | 68.5 | 0 | 8 | 51.5 | 10 | pass |
| FULL | 50.9 | 51.9 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 50.4 | 51.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 51.7 | 52.7 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H | 58.7 | 59.7 | 0 | 4 | 56.9 | 8 | pass |
| FULL H CEL+PP | 56.4 | 57.5 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H PCM | 51.9 | 53.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 60.6 | 61.6 | 0 | 4 | 54.5 | 8 | pass |
| FULL SH2 CEL+PP | 63.2 | 64.6 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 46.5 | 47.4 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 50.0 | 51.1 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 50.0 | 51.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 52.9 | 53.8 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 54.1 | 55.4 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 56.2 | 57.3 | 0 | 4 | 57.5 | 7 | pass |
| DSPRD SH2 CEL+PP | 57.7 | 58.9 | 0 | 4 | 57.2 | 7 | pass |
| SEQ+ARP DSPRD CEL | 63.5 | 67.3 | 0 | 4 | 51.9 | 10 | pass |
| IDLE PHASER | 17.8 | 18.4 | 0 | 0 | 61.6 | 1 | pass |
| IDLE PHASER x2 | 19.3 | 19.9 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD PHASER | 59.5 | 60.9 | 0 | 8 | 56.8 | 8 | pass |
| SPREAD H CEL PHSR | 71.0 | 72.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 63.7 | 67.4 | 0 | 8 | 52.2 | 10 | pass |
| IDLE ENSEMBLE | 18.4 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 20.0 | 20.6 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 19.1 | 19.8 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR x2 | 20.9 | 21.6 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 18.3 | 18.8 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 19.7 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 74.5 | 76.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 67.2 | 71.2 | 0 | 8 | 46.9 | 10 | pass |
| SPREAD H CEL MIX | 69.4 | 71.0 | 0 | 8 | 43.2 | 9 | pass |
| SEQ+ARP CEL MIX | 65.7 | 69.7 | 0 | 8 | 50.9 | 10 | pass |
| HEAVY SPRING MIX | 55.4 | 56.5 | 0 | 8 | 57.3 | 1 | pass |
| IDLE TAPE FLANGER | 19.4 | 20.1 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.2 | 21.9 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 74.3 | 75.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 67.2 | 71.2 | 0 | 8 | 48.2 | 10 | pass |
| IDLE BBD FLANGER | 18.3 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.8 | 20.4 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL BBDF | 71.3 | 72.9 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 18.7 | 19.4 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 49.1 | 50.7 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.0 | 21.9 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL PSHF | 71.7 | 73.6 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE BBD ECHO | 19.3 | 20.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE BBD ECHO x2 | 19.3 | 19.9 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ECHO | 72.3 | 73.9 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-03 09:34, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.0 | 15.6 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 24.2 | 25.0 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 44.6 | 45.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 49.2 | 50.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 56.3 | 57.3 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD | 56.5 | 57.7 | 0 | 8 | 57.5 | 7 | pass |
| DUAL FILTER | 59.4 | 60.3 | 0 | 7 | 55.9 | 8 | pass |
| STACK | 65.2 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL | 54.2 | 55.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 54.8 | 55.9 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD HEAVY | 60.8 | 61.9 | 0 | 8 | 55.6 | 8 | pass |
| DUAL FLT HEAVY | 61.6 | 62.7 | 0 | 7 | 53.2 | 8 | pass |
| STACK HEAVY | 63.0 | 64.0 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.5 | 61.6 | 0 | 6 | 54.6 | 8 | pass |
| SINGLE LADDER | 44.6 | 45.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 42.3 | 43.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 49.8 | 50.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 42.5 | 43.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.3 | 60.3 | 0 | 7 | 56.5 | 8 | pass |
| SEQ+ARP HOME | 56.9 | 60.6 | 0 | 8 | 57.2 | 9 | pass |
| SEQ+ARP AMP | 57.0 | 60.5 | 0 | 8 | 57.2 | 11 | pass |
| SEQ+ARP SEQ | 56.3 | 59.9 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 56.8 | 61.8 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 44.6 | 45.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 52.6 | 53.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.2 | 55.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 61.7 | 65.4 | 0 | 8 | 53.5 | 20 | pass |
| SINGLE MULTI H | 51.6 | 52.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 56.5 | 57.5 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD MULTI H | 63.2 | 64.4 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 66.3 | 67.5 | 0 | 7 | 49.9 | 8 | pass |
| STACK MULTI H | 66.3 | 67.7 | 0 | 6 | 49.9 | 8 | pass |
| BINAUR MULTI H | 63.3 | 64.4 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 67.6 | 69.0 | 0 | 8 | 45.6 | 9 | pass |
| DUALOSC H CEL+PP | 60.7 | 61.9 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD H TUM+PP | 65.8 | 67.3 | 0 | 8 | 51.5 | 8 | pass |
| SEQ+ARP SPRD CEL | 75.8 | 80.4 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 50.9 | 52.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 53.6 | 54.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 59.4 | 62.9 | 0 | 8 | 56.2 | 9 | pass |
| STRIKE | 45.1 | 46.0 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 50.0 | 51.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 66.4 | 70.6 | 0 | 8 | 51.2 | 10 | pass |
| SEQ+ARP STRK FILT | 67.2 | 71.0 | 0 | 8 | 47.9 | 12 | pass |
| SINGLE SH2 H | 56.7 | 57.6 | 0 | 8 | 57.3 | 7 | pass |
| DUALOSC SH2 H | 61.2 | 62.2 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD SH2 H | 68.6 | 69.6 | 0 | 8 | 45.2 | 9 | pass |
| DUALFLT SH2 H | 75.5 | 76.8 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.7 | 75.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.0 | 72.3 | 0 | 6 | 42.9 | 9 | FAIL |
| STRIKE SH2 H | 57.4 | 58.4 | 0 | 8 | 57.2 | 7 | pass |
| SPREAD SH2 CEL+PP | 73.0 | 74.6 | 0 | 8 | 43.2 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.3 | 81.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 69.0 | 73.4 | 0 | 8 | 44.5 | 10 | pass |
| DUALFLT SH2 HP | 75.4 | 76.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.1 | 72.3 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 48.0 | 49.3 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 57.0 | 58.1 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 62.9 | 64.1 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 62.5 | 63.6 | 0 | 7 | 52.5 | 8 | pass |
| STACK PCM | 65.9 | 67.0 | 0 | 6 | 50.6 | 8 | pass |
| BINAURAL PCM | 59.2 | 60.3 | 0 | 6 | 56.2 | 8 | pass |
| DUALOSC MULTI H PCM | 56.1 | 57.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 69.4 | 70.9 | 0 | 7 | 42.9 | 1 | pass |
| BINAUR MULTI H PCM | 67.9 | 69.1 | 0 | 6 | 46.2 | 9 | pass |
| SINGLE RS | 44.6 | 46.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.1 | 53.2 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 56.3 | 57.6 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 60.5 | 62.1 | 0 | 6 | 53.9 | 8 | pass |
| SEQ+ARP HOME PCM | 64.4 | 68.1 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 48.5 | 49.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 48.7 | 49.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 48.5 | 49.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 49.5 | 50.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 49.2 | 50.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 49.4 | 50.4 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 69.4 | 70.8 | 0 | 6 | 43.2 | 9 | pass |
| BINAUR HEAVY OD | 67.4 | 68.6 | 0 | 6 | 45.2 | 9 | pass |
| STRIKE HEAVY TUBE | 54.8 | 55.8 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 62.2 | 65.6 | 0 | 8 | 53.2 | 10 | pass |
| FULL | 50.3 | 51.4 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 48.9 | 49.9 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 51.1 | 52.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 57.1 | 58.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL H CEL+PP | 55.5 | 56.9 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H PCM | 50.9 | 52.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 56.8 | 58.1 | 0 | 4 | 57.5 | 7 | pass |
| FULL SH2 CEL+PP | 61.9 | 63.2 | 0 | 4 | 52.5 | 8 | pass |
| DSPRD | 46.2 | 47.4 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 49.4 | 50.6 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 50.9 | 52.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 53.4 | 54.5 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD H CEL+PP | 55.8 | 57.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 56.6 | 57.8 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 58.4 | 59.7 | 0 | 4 | 57.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 63.9 | 67.6 | 0 | 4 | 51.9 | 10 | pass |
| IDLE PHASER | 17.0 | 17.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.5 | 19.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 59.7 | 60.8 | 0 | 8 | 55.8 | 8 | pass |
| SPREAD H CEL PHSR | 71.8 | 73.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 62.2 | 66.1 | 0 | 8 | 52.6 | 10 | pass |
| IDLE ENSEMBLE | 17.6 | 18.3 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.2 | 19.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR | 18.4 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR x2 | 20.3 | 20.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS | 17.6 | 18.3 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 19.0 | 19.7 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL ENPH | 74.5 | 76.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 65.3 | 69.5 | 0 | 8 | 50.9 | 10 | pass |
| SPREAD H CEL MIX | 69.2 | 70.9 | 0 | 8 | 43.6 | 9 | pass |
| SEQ+ARP CEL MIX | 63.3 | 67.3 | 0 | 8 | 52.6 | 10 | pass |
| HEAVY SPRING MIX | 53.1 | 54.2 | 0 | 8 | 57.6 | 1 | pass |
| IDLE TAPE FLANGER | 18.7 | 19.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 20.6 | 21.4 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL TFLG | 76.0 | 77.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 65.7 | 69.1 | 0 | 8 | 50.9 | 10 | pass |
| IDLE BBD FLANGER | 17.3 | 17.9 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 18.8 | 19.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 72.3 | 74.1 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 17.9 | 18.7 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 48.8 | 50.4 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 20.2 | 21.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 72.3 | 74.3 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE BBD ECHO | 18.3 | 18.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE BBD ECHO x2 | 18.3 | 19.0 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL ECHO | 73.6 | 75.2 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-03 09:23, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.0 | 15.5 | 0 | 0 | 62.3 | 2 | pass |
| SINGLE 1V | 24.2 | 25.1 | 0 | 1 | 61.0 | 4 | pass |
| SINGLE | 44.6 | 45.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 49.1 | 50.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 56.3 | 57.3 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD | 56.5 | 57.8 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FILTER | 59.3 | 60.3 | 0 | 7 | 55.5 | 8 | pass |
| STACK | 65.1 | 66.3 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 54.1 | 55.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 54.7 | 55.9 | 0 | 6 | 57.6 | 1 | pass |
| SPREAD HEAVY | 60.8 | 61.9 | 0 | 8 | 55.6 | 8 | pass |
| DUAL FLT HEAVY | 61.6 | 62.7 | 0 | 7 | 53.5 | 8 | pass |
| STACK HEAVY | 63.1 | 64.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.6 | 61.7 | 0 | 6 | 55.2 | 8 | pass |
| SINGLE LADDER | 44.6 | 45.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 42.4 | 43.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE SH-2 | 49.8 | 50.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 42.5 | 43.5 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 59.3 | 60.4 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 56.9 | 60.6 | 0 | 8 | 57.2 | 9 | pass |
| SEQ+ARP AMP | 57.0 | 61.0 | 0 | 8 | 57.2 | 11 | pass |
| SEQ+ARP SEQ | 56.4 | 60.2 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 56.8 | 61.2 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 44.6 | 45.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 52.6 | 53.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.2 | 55.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 61.7 | 65.6 | 0 | 8 | 52.9 | 10 | pass |
| SINGLE MULTI H | 51.6 | 52.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 56.5 | 57.6 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD MULTI H | 63.2 | 64.4 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 66.3 | 67.3 | 0 | 7 | 50.9 | 8 | pass |
| STACK MULTI H | 66.4 | 67.8 | 0 | 6 | 51.2 | 8 | pass |
| BINAUR MULTI H | 63.2 | 64.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 67.6 | 69.0 | 0 | 8 | 44.9 | 9 | pass |
| DUALOSC H CEL+PP | 60.6 | 61.9 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD H TUM+PP | 65.7 | 66.9 | 0 | 8 | 51.2 | 8 | pass |
| SEQ+ARP SPRD CEL | 75.7 | 79.6 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 50.9 | 51.9 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 53.6 | 54.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 59.4 | 62.9 | 0 | 8 | 55.9 | 9 | pass |
| STRIKE | 45.1 | 46.0 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 50.0 | 51.0 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP STRIKE | 66.3 | 70.0 | 0 | 8 | 49.9 | 10 | pass |
| SEQ+ARP STRK FILT | 67.2 | 71.4 | 0 | 8 | 47.3 | 10 | pass |
| SINGLE SH2 H | 56.7 | 57.7 | 0 | 8 | 57.6 | 7 | pass |
| DUALOSC SH2 H | 61.2 | 62.2 | 0 | 6 | 56.6 | 8 | pass |
| SPREAD SH2 H | 68.5 | 69.9 | 0 | 8 | 45.2 | 9 | pass |
| DUALFLT SH2 H | 75.4 | 76.7 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.8 | 75.1 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.0 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 57.3 | 58.4 | 0 | 8 | 57.2 | 7 | pass |
| SPREAD SH2 CEL+PP | 73.0 | 74.4 | 0 | 8 | 43.2 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.3 | 81.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 69.0 | 73.1 | 0 | 8 | 44.9 | 10 | pass |
| DUALFLT SH2 HP | 75.5 | 76.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.1 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 48.0 | 49.4 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 57.0 | 58.1 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 62.9 | 63.9 | 0 | 8 | 52.5 | 8 | pass |
| DUAL FILTER PCM | 62.5 | 63.6 | 0 | 7 | 52.9 | 8 | pass |
| STACK PCM | 65.9 | 66.9 | 0 | 6 | 50.5 | 8 | pass |
| BINAURAL PCM | 59.3 | 60.3 | 0 | 6 | 56.6 | 8 | pass |
| DUALOSC MULTI H PCM | 56.1 | 57.2 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 69.4 | 70.7 | 0 | 7 | 42.9 | 1 | pass |
| BINAUR MULTI H PCM | 67.9 | 69.2 | 0 | 6 | 45.6 | 9 | pass |
| SINGLE RS | 44.7 | 46.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.1 | 53.3 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 56.3 | 57.7 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC RS PCM | 60.5 | 62.1 | 0 | 6 | 54.2 | 8 | pass |
| SEQ+ARP HOME PCM | 64.4 | 68.0 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 48.4 | 49.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 48.7 | 49.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 48.5 | 49.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 49.4 | 50.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 49.2 | 50.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 49.4 | 50.4 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 69.4 | 70.8 | 0 | 6 | 43.2 | 9 | pass |
| BINAUR HEAVY OD | 67.4 | 68.5 | 0 | 6 | 44.9 | 9 | pass |
| STRIKE HEAVY TUBE | 54.8 | 55.7 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 62.1 | 66.1 | 0 | 8 | 53.2 | 10 | pass |
| FULL | 50.3 | 51.4 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 48.9 | 50.0 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 51.1 | 52.2 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H | 57.1 | 58.3 | 0 | 4 | 57.5 | 7 | pass |
| FULL H CEL+PP | 55.5 | 56.7 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H PCM | 50.9 | 52.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 56.8 | 58.0 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 CEL+PP | 61.9 | 63.0 | 0 | 4 | 52.9 | 8 | pass |
| DSPRD | 46.2 | 47.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD HEAVY | 49.4 | 50.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 51.0 | 52.2 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 53.4 | 54.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 55.8 | 57.1 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 56.6 | 57.9 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 58.5 | 59.7 | 0 | 4 | 57.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 63.9 | 67.6 | 0 | 4 | 52.2 | 10 | pass |
| IDLE PHASER | 17.0 | 17.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.5 | 19.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 59.6 | 60.8 | 0 | 8 | 55.5 | 8 | pass |
| SPREAD H CEL PHSR | 71.8 | 73.5 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 62.2 | 66.0 | 0 | 8 | 53.2 | 10 | pass |
| IDLE ENSEMBLE | 17.7 | 18.4 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.2 | 19.9 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR | 18.4 | 19.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR x2 | 20.3 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS | 17.5 | 18.2 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 19.1 | 19.8 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ENPH | 74.5 | 76.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 65.2 | 68.5 | 0 | 8 | 50.6 | 10 | pass |
| SPREAD H CEL MIX | 69.2 | 70.6 | 0 | 8 | 43.2 | 9 | pass |
| SEQ+ARP CEL MIX | 63.3 | 67.6 | 0 | 8 | 51.9 | 10 | pass |
| HEAVY SPRING MIX | 53.1 | 54.1 | 0 | 8 | 57.3 | 1 | pass |
| IDLE TAPE FLANGER | 18.7 | 19.5 | 0 | 0 | 61.6 | 1 | pass |
| IDLE TAPE FLNG x2 | 20.5 | 21.4 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL TFLG | 75.9 | 77.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 65.7 | 69.3 | 0 | 8 | 50.6 | 10 | pass |
| IDLE BBD FLANGER | 17.3 | 17.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE BBD FLNG x2 | 18.7 | 19.4 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL BBDF | 72.3 | 74.0 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE PITCH SHIFT | 17.9 | 18.7 | 0 | 0 | 61.3 | 1 | pass |
| PITCH SHIFT HELD | 48.8 | 50.4 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 20.2 | 21.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 72.3 | 74.3 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE BBD ECHO | 18.3 | 18.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE BBD ECHO x2 | 18.3 | 19.0 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL ECHO | 73.6 | 75.5 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-02 20:06, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.2 | 15.6 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 23.9 | 24.6 | 0 | 1 | 61.0 | 4 | pass |
| SINGLE | 44.5 | 45.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 48.8 | 49.9 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 55.4 | 56.4 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD | 55.8 | 56.8 | 0 | 8 | 57.5 | 1 | pass |
| DUAL FILTER | 57.9 | 58.9 | 0 | 7 | 57.5 | 1 | pass |
| STACK | 64.6 | 65.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 54.4 | 55.4 | 0 | 6 | 57.6 | 7 | pass |
| DUAL OSC HEAVY | 53.7 | 54.9 | 0 | 6 | 57.6 | 1 | pass |
| SPREAD HEAVY | 59.9 | 60.9 | 0 | 8 | 54.9 | 8 | pass |
| DUAL FLT HEAVY | 60.0 | 61.0 | 0 | 7 | 54.6 | 8 | pass |
| STACK HEAVY | 61.1 | 62.3 | 0 | 6 | 55.9 | 8 | pass |
| BINAURAL HEAVY | 59.2 | 60.4 | 0 | 6 | 55.6 | 8 | pass |
| SINGLE LADDER | 44.5 | 45.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 42.2 | 43.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 49.7 | 50.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 42.5 | 43.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 57.9 | 58.9 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 56.6 | 60.8 | 0 | 8 | 57.5 | 9 | pass |
| SEQ+ARP AMP | 56.8 | 60.5 | 0 | 8 | 57.2 | 11 | pass |
| SEQ+ARP SEQ | 56.1 | 59.8 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 56.5 | 61.9 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 44.4 | 45.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 52.9 | 53.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 53.2 | 54.6 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 61.2 | 65.4 | 0 | 8 | 53.5 | 9 | pass |
| SINGLE MULTI H | 51.2 | 52.1 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 55.3 | 56.4 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD MULTI H | 62.6 | 63.5 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 64.7 | 65.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 65.2 | 66.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 62.8 | 63.8 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 66.6 | 67.9 | 0 | 8 | 51.2 | 9 | pass |
| DUALOSC H CEL+PP | 59.8 | 61.2 | 0 | 6 | 56.6 | 8 | pass |
| SPREAD H TUM+PP | 65.3 | 66.6 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 74.7 | 78.8 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 51.0 | 52.1 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 53.3 | 54.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 59.5 | 63.1 | 0 | 8 | 55.9 | 9 | pass |
| STRIKE | 45.5 | 46.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 50.0 | 51.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 66.1 | 70.9 | 0 | 8 | 50.2 | 10 | pass |
| SEQ+ARP STRK FILT | 67.0 | 71.5 | 0 | 8 | 48.2 | 10 | pass |
| SINGLE SH2 H | 56.3 | 57.4 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC SH2 H | 59.3 | 60.5 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD SH2 H | 67.3 | 68.5 | 0 | 8 | 45.9 | 9 | pass |
| DUALFLT SH2 H | 74.4 | 75.8 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 72.1 | 73.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 70.3 | 71.6 | 0 | 6 | 42.9 | 9 | FAIL |
| STRIKE SH2 H | 58.3 | 59.3 | 0 | 8 | 57.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 72.0 | 73.7 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 78.7 | 80.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 69.4 | 73.7 | 0 | 8 | 44.6 | 10 | pass |
| DUALFLT SH2 HP | 74.4 | 75.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.4 | 71.6 | 0 | 6 | 42.9 | 9 | FAIL |
| SINGLE PCM | 48.6 | 49.7 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 56.5 | 57.6 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 60.6 | 61.6 | 0 | 8 | 55.9 | 8 | pass |
| DUAL FILTER PCM | 61.5 | 62.5 | 0 | 7 | 54.6 | 8 | pass |
| STACK PCM | 66.0 | 67.3 | 0 | 6 | 51.2 | 8 | pass |
| BINAURAL PCM | 59.9 | 60.9 | 0 | 6 | 56.2 | 8 | pass |
| DUALOSC MULTI H PCM | 55.2 | 56.3 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 68.4 | 69.6 | 0 | 7 | 46.6 | 9 | pass |
| BINAUR MULTI H PCM | 68.2 | 69.3 | 0 | 6 | 44.9 | 9 | pass |
| SINGLE RS | 44.5 | 46.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.7 | 53.6 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 55.4 | 56.9 | 0 | 6 | 57.6 | 1 | pass |
| DUALOSC RS PCM | 60.0 | 61.7 | 0 | 6 | 54.9 | 8 | pass |
| SEQ+ARP HOME PCM | 64.7 | 68.8 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 48.3 | 49.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 48.5 | 49.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 48.4 | 49.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 49.3 | 50.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 49.1 | 50.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 49.2 | 50.2 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 67.4 | 68.8 | 0 | 6 | 45.9 | 9 | pass |
| BINAUR HEAVY OD | 65.5 | 66.8 | 0 | 6 | 51.6 | 8 | pass |
| STRIKE HEAVY TUBE | 54.2 | 55.4 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 61.6 | 65.1 | 0 | 8 | 53.5 | 10 | pass |
| FULL | 49.6 | 50.6 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 47.5 | 48.6 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 50.2 | 51.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 55.4 | 56.6 | 0 | 4 | 57.2 | 1 | pass |
| FULL H CEL+PP | 54.4 | 55.8 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H PCM | 50.2 | 51.3 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 54.9 | 56.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 CEL+PP | 60.2 | 61.6 | 0 | 4 | 55.6 | 8 | pass |
| DSPRD | 45.4 | 46.3 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD HEAVY | 47.6 | 48.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 49.1 | 50.2 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 51.8 | 52.8 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 53.5 | 55.0 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 54.6 | 55.8 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 CEL+PP | 56.2 | 57.5 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 61.8 | 65.7 | 0 | 4 | 53.2 | 9 | pass |
| IDLE PHASER | 17.8 | 18.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 19.2 | 19.8 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD PHASER | 60.1 | 61.2 | 0 | 8 | 54.2 | 8 | pass |
| SPREAD H CEL PHSR | 70.6 | 72.3 | 0 | 8 | 43.2 | 9 | FAIL |
| SEQ+ARP HOME PHSR2 | 63.0 | 66.8 | 0 | 8 | 51.9 | 10 | pass |
| IDLE ENSEMBLE | 18.0 | 18.6 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 19.6 | 20.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 18.6 | 19.1 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR x2 | 20.5 | 21.1 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS | 18.0 | 18.6 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 19.6 | 20.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 72.4 | 74.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 64.6 | 68.8 | 0 | 8 | 51.9 | 10 | pass |
| SPREAD H CEL MIX | 68.4 | 69.7 | 0 | 8 | 46.9 | 9 | pass |
| SEQ+ARP CEL MIX | 62.9 | 67.1 | 0 | 8 | 51.9 | 10 | pass |
| HEAVY SPRING MIX | 53.4 | 54.4 | 0 | 8 | 57.2 | 1 | pass |
| IDLE TAPE FLANGER | 19.6 | 20.3 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.5 | 22.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 73.1 | 74.7 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 64.6 | 68.4 | 0 | 8 | 51.6 | 10 | pass |
| IDLE BBD FLANGER | 18.1 | 18.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.5 | 20.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL BBDF | 72.7 | 74.2 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 18.5 | 19.1 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 49.1 | 50.5 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 20.8 | 21.7 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL PSHF | 71.0 | 73.0 | 0 | 8 | 43.2 | 1 | FAIL |

### 2026-11-02 19:55, CARNAC OS PROF (1 core), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.4 | 22.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 31.6 | 32.7 | 0 | 1 | 58.0 | 4 | pass |
| SINGLE | 84.0 | 85.5 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE HEAVY | 91.2 | 92.7 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC | 126.9 | 127.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD | 107.6 | 109.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 98.0 | 99.6 | 0 | 7 | 39.4 | 12 | FAIL |
| STACK | 121.8 | 122.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL | 101.3 | 103.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 123.2 | 124.7 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 114.7 | 116.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 102.3 | 103.9 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 114.5 | 116.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 109.3 | 110.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 84.0 | 85.7 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE MS20 | 79.1 | 80.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE SH-2 | 94.8 | 96.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE FORMANT | 79.7 | 81.3 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FLT LADDER | 98.1 | 99.6 | 0 | 7 | 38.4 | 12 | FAIL |
| SEQ+ARP HOME | 100.9 | 105.1 | 29 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 101.8 | 104.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 100.2 | 105.0 | 70 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 101.4 | 106.7 | 31 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 83.8 | 85.2 | 0 | 8 | 42.9 | 1 | FAIL |
| HEAVY MOD+PP | 98.0 | 99.9 | 0 | 8 | 38.9 | 12 | FAIL |
| HEAVY CELEST+PP | 98.7 | 100.4 | 29 | 8 | 33.2 | 24 | FAIL |
| SEQ+ARP CELEST+PP | 109.9 | 112.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MULTI H | 95.7 | 97.5 | 0 | 8 | 43.2 | 1 | FAIL |
| DUALOSC MULTI H | 126.5 | 126.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD MULTI H | 119.2 | 120.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H | 109.7 | 111.4 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK MULTI H | 121.1 | 122.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H | 116.0 | 117.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL+PP | 126.7 | 128.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC H CEL+PP | 134.6 | 136.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H TUM+PP | 124.4 | 126.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPRD CEL | 137.1 | 140.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 95.8 | 97.2 | 0 | 8 | 43.2 | 1 | FAIL |
| HEAVY CEL+BBD PP | 100.5 | 101.0 | 10 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPR+BBD | 107.0 | 109.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STRIKE | 83.8 | 85.3 | 0 | 8 | 42.9 | 1 | FAIL |
| STRIKE HEAVY | 90.9 | 92.4 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP STRIKE | 115.1 | 117.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRK FILT | 116.6 | 119.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE SH2 H | 107.5 | 109.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC SH2 H | 134.7 | 136.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 H | 129.4 | 131.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 H | 127.5 | 129.6 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK SH2 H | 138.3 | 140.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 H | 131.8 | 133.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE SH2 H | 114.8 | 116.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 CEL+PP | 137.8 | 139.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 CEL | 136.0 | 138.0 | 8 | 7 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SH2 CEL | 125.5 | 128.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 HP | 127.6 | 129.7 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 HP | 131.9 | 133.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE PCM | 90.4 | 92.1 | 0 | 8 | 42.9 | 1 | FAIL |
| DUAL OSC PCM | 111.7 | 113.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD PCM | 137.8 | 140.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER PCM | 111.2 | 112.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK PCM | 129.9 | 131.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL PCM | 120.8 | 122.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC MULTI H PCM | 111.0 | 112.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H PCM | 121.7 | 122.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H PCM | 135.9 | 137.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE RS | 84.0 | 86.7 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE RS PCM | 97.0 | 100.4 | 3 | 8 | 41.9 | 12 | FAIL |
| DUALOSC RS | 127.1 | 127.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC RS PCM | 111.6 | 112.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PCM | 115.6 | 118.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV SOFT | 91.0 | 92.5 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV HARD | 91.5 | 93.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV FOLD | 91.5 | 93.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TAPE | 93.0 | 94.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TUBE | 92.6 | 94.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV OD | 92.8 | 94.4 | 0 | 8 | 43.2 | 1 | FAIL |
| STACK HEAVY TUBE | 126.4 | 127.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR HEAVY OD | 121.4 | 123.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE HEAVY TUBE | 105.4 | 107.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TUBE | 110.9 | 113.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| FULL | 106.6 | 108.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL HEAVY | 102.4 | 103.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H | 108.1 | 109.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 H | 121.1 | 122.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL H CEL+PP | 116.6 | 118.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H PCM | 88.6 | 90.2 | 0 | 4 | 42.9 | 1 | FAIL |
| FULL SH2 H PCM | 107.7 | 109.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 CEL+PP | 129.7 | 131.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD | 82.1 | 83.6 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD HEAVY | 84.7 | 86.1 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD MULTI H | 87.5 | 89.1 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD SH2 H | 92.6 | 94.1 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD H CEL+PP | 95.0 | 96.6 | 0 | 4 | 43.2 | 12 | FAIL |
| DSPRD MULTI H PCM | 120.9 | 121.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD SH2 CEL+PP | 102.4 | 103.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SEQ+ARP DSPRD CEL | 105.2 | 107.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| IDLE PHASER | 25.9 | 26.5 | 0 | 0 | 60.3 | 4 | pass |
| IDLE PHASER x2 | 29.1 | 29.8 | 0 | 0 | 59.3 | 4 | pass |
| SPREAD PHASER | 113.2 | 115.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL PHSR | 132.9 | 135.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PHSR2 | 110.9 | 113.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE ENSEMBLE | 26.2 | 26.8 | 0 | 0 | 60.0 | 4 | pass |
| IDLE ENSEMBLE x2 | 30.1 | 30.7 | 0 | 0 | 58.3 | 4 | pass |
| IDLE ENS+PHSR | 27.0 | 27.6 | 0 | 0 | 60.0 | 4 | pass |
| IDLE ENS+PHSR x2 | 31.7 | 32.4 | 0 | 0 | 58.2 | 4 | pass |
| IDLE CHORUS | 26.1 | 26.8 | 0 | 0 | 60.3 | 4 | pass |
| IDLE CHORUS x2 | 29.6 | 30.2 | 0 | 0 | 58.3 | 4 | pass |
| SPREAD H CEL ENPH | 135.0 | 138.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME ENPH2 | 114.9 | 117.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL MIX | 130.1 | 132.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP CEL MIX | 113.6 | 116.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING MIX | 98.9 | 100.6 | 45 | 8 | 32.2 | 35 | FAIL |
| IDLE TAPE FLANGER | 27.5 | 28.2 | 0 | 0 | 60.0 | 4 | pass |
| IDLE TAPE FLNG x2 | 32.1 | 32.7 | 0 | 0 | 57.3 | 4 | pass |
| SPREAD H CEL TFLG | 135.8 | 138.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TFLG2 | 114.5 | 117.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE BBD FLANGER | 26.3 | 27.0 | 0 | 0 | 59.9 | 1 | pass |
| IDLE BBD FLNG x2 | 29.9 | 30.6 | 0 | 0 | 58.6 | 4 | pass |
| SPREAD H CEL BBDF | 134.8 | 137.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE PITCH SHIFT | 27.4 | 28.3 | 0 | 0 | 59.6 | 4 | pass |
| PITCH SHIFT HELD | 90.3 | 92.3 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHFT x2 | 32.3 | 33.6 | 0 | 0 | 57.6 | 4 | pass |
| SPREAD H CEL PSHF | 133.9 | 136.3 | 8 | 8 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice per 16-sample block (the voice stages, CTRL and its c.* split); 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 114 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 63 | 15 | 3 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| SINGLE 1V | 1 | 169 | 24 | 21 | 5 | 1 | 12 | 2 | 0 | 68 | 15 | 4 | 12 | 5 | 3 | 7 | 5 | 1 | 25 | 45 |
| SINGLE | 8 | 453 | 162 | 160 | 34 | 6 | 92 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 8 | 31 | 40 |
| SINGLE HEAVY | 8 | 493 | 192 | 162 | 34 | 22 | 80 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 64 | 8 | 39 | 44 |
| DUAL OSC | 6 | 687 | 148 | 410 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 12 | 29 | 18 | 59 | 26 | 7 | 28 | 93 |
| SPREAD | 8 | 582 | 182 | 266 | 138 | 6 | 94 | 16 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 66 | 32 | 10 | 32 | 56 |
| DUAL FILTER | 7 | 530 | 175 | 225 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 14 | 33 | 19 | 41 | 62 | 8 | 27 | 57 |
| STACK | 6 | 659 | 174 | 351 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 16 | 29 | 18 | 57 | 48 | 12 | 27 | 88 |
| BINAURAL | 6 | 548 | 176 | 236 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 4 | 15 | 29 | 18 | 37 | 76 | 8 | 28 | 69 |
| DUAL OSC HEAVY | 6 | 667 | 170 | 362 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 12 | 28 | 17 | 59 | 49 | 7 | 34 | 89 |
| SPREAD HEAVY | 8 | 621 | 212 | 268 | 140 | 22 | 80 | 16 | 0 | 69 | 16 | 4 | 13 | 38 | 22 | 66 | 66 | 8 | 39 | 60 |
| DUAL FLT HEAVY | 7 | 553 | 198 | 216 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 13 | 33 | 21 | 42 | 84 | 8 | 35 | 59 |
| STACK HEAVY | 6 | 620 | 194 | 286 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 15 | 29 | 18 | 58 | 68 | 12 | 32 | 80 |
| BINAURAL HEAVY | 6 | 591 | 206 | 240 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 14 | 29 | 18 | 37 | 105 | 8 | 38 | 74 |
| SINGLE LADDER | 8 | 454 | 162 | 160 | 34 | 6 | 92 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 8 | 32 | 40 |
| SINGLE MS20 | 8 | 427 | 156 | 140 | 34 | 6 | 72 | 16 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 46 | 26 | 8 | 30 | 37 |
| SINGLE SH-2 | 8 | 512 | 162 | 218 | 34 | 6 | 150 | 16 | 0 | 67 | 15 | 4 | 15 | 38 | 22 | 48 | 34 | 8 | 31 | 48 |
| SINGLE FORMANT | 8 | 431 | 154 | 146 | 34 | 6 | 80 | 16 | 0 | 67 | 15 | 4 | 13 | 38 | 22 | 46 | 26 | 8 | 32 | 38 |
| DUAL FLT LADDER | 7 | 530 | 175 | 225 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 33 | 19 | 41 | 63 | 8 | 28 | 57 |
| SEQ+ARP HOME | 8 | 545 | 208 | 168 | 34 | 24 | 80 | 16 | 2 | 72 | 16 | 13 | 14 | 38 | 32 | 48 | 66 | 10 | 52 | 47 |
| SEQ+ARP AMP | 8 | 550 | 210 | 166 | 34 | 24 | 78 | 16 | 0 | 74 | 17 | 14 | 15 | 38 | 34 | 50 | 66 | 8 | 54 | 47 |
| SEQ+ARP SEQ | 8 | 541 | 206 | 168 | 34 | 24 | 80 | 16 | 2 | 72 | 16 | 12 | 14 | 38 | 32 | 48 | 66 | 8 | 51 | 47 |
| SEQ+ARP MOD1 | 8 | 548 | 210 | 168 | 34 | 24 | 80 | 16 | 2 | 71 | 16 | 13 | 17 | 38 | 32 | 48 | 66 | 8 | 51 | 48 |
| LADDER FILTER PG | 8 | 453 | 162 | 160 | 34 | 6 | 92 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 8 | 31 | 40 |
| HEAVY MOD+PP | 8 | 530 | 194 | 164 | 34 | 22 | 80 | 16 | 0 | 103 | 15 | 4 | 14 | 38 | 22 | 46 | 66 | 8 | 36 | 45 |
| HEAVY CELEST+PP | 8 | 533 | 194 | 162 | 32 | 22 | 80 | 16 | 0 | 108 | 15 | 4 | 12 | 38 | 22 | 48 | 66 | 8 | 38 | 44 |
| SEQ+ARP CELEST+PP | 8 | 594 | 210 | 168 | 34 | 22 | 82 | 18 | 2 | 116 | 16 | 15 | 14 | 38 | 34 | 50 | 66 | 10 | 53 | 48 |
| SINGLE MULTI H | 8 | 517 | 200 | 176 | 34 | 22 | 92 | 16 | 0 | 71 | 15 | 4 | 13 | 38 | 22 | 46 | 72 | 8 | 38 | 47 |
| DUALOSC MULTI H | 6 | 685 | 177 | 372 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 4 | 13 | 29 | 18 | 59 | 55 | 7 | 33 | 92 |
| SPREAD MULTI H | 8 | 645 | 222 | 282 | 138 | 22 | 94 | 16 | 0 | 71 | 16 | 4 | 14 | 38 | 22 | 66 | 72 | 8 | 36 | 63 |
| DUALFLT MULTI H | 7 | 593 | 214 | 240 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 14 | 34 | 21 | 42 | 99 | 8 | 34 | 65 |
| STACK MULTI H | 6 | 655 | 203 | 309 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 15 | 29 | 17 | 57 | 79 | 12 | 34 | 86 |
| BINAUR MULTI H | 6 | 628 | 221 | 260 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 5 | 15 | 29 | 18 | 37 | 119 | 7 | 39 | 80 |
| SPREAD H CEL+PP | 8 | 686 | 224 | 282 | 138 | 22 | 92 | 16 | 0 | 110 | 15 | 5 | 13 | 38 | 22 | 66 | 74 | 10 | 37 | 63 |
| DUALOSC H CEL+PP | 6 | 729 | 179 | 372 | 0 | 0 | 0 | 0 | 1 | 110 | 16 | 5 | 13 | 29 | 17 | 60 | 57 | 7 | 33 | 92 |
| SPREAD H TUM+PP | 8 | 673 | 224 | 282 | 140 | 22 | 92 | 16 | 0 | 97 | 15 | 4 | 14 | 38 | 22 | 66 | 74 | 8 | 37 | 63 |
| SEQ+ARP SPRD CEL | 8 | 742 | 238 | 286 | 142 | 22 | 94 | 16 | 2 | 117 | 17 | 15 | 14 | 38 | 34 | 68 | 76 | 10 | 53 | 66 |
| HEAVY SPRING+PP | 8 | 518 | 194 | 164 | 34 | 22 | 80 | 16 | 0 | 91 | 15 | 4 | 13 | 38 | 22 | 48 | 66 | 8 | 37 | 45 |
| HEAVY CEL+BBD PP | 8 | 543 | 196 | 164 | 34 | 22 | 80 | 16 | 0 | 113 | 15 | 5 | 13 | 38 | 22 | 48 | 66 | 10 | 37 | 45 |
| SEQ+ARP SPR+BBD | 8 | 578 | 210 | 168 | 34 | 24 | 80 | 18 | 2 | 100 | 17 | 15 | 14 | 38 | 32 | 48 | 68 | 10 | 52 | 48 |
| STRIKE | 8 | 452 | 162 | 156 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 4 | 14 | 38 | 22 | 48 | 34 | 8 | 32 | 40 |
| STRIKE HEAVY | 8 | 491 | 194 | 158 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 66 | 8 | 37 | 44 |
| SEQ+ARP STRIKE | 8 | 622 | 218 | 182 | 0 | 0 | 0 | 0 | 2 | 119 | 16 | 16 | 14 | 38 | 34 | 50 | 66 | 16 | 55 | 50 |
| SEQ+ARP STRK FILT | 8 | 630 | 218 | 192 | 0 | 0 | 0 | 0 | 2 | 117 | 16 | 16 | 15 | 38 | 34 | 48 | 68 | 16 | 54 | 52 |
| SINGLE SH2 H | 8 | 581 | 204 | 234 | 34 | 22 | 150 | 16 | 0 | 70 | 15 | 5 | 15 | 38 | 22 | 48 | 72 | 8 | 38 | 55 |
| DUALOSC SH2 H | 6 | 729 | 176 | 416 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 4 | 13 | 28 | 17 | 60 | 54 | 7 | 34 | 99 |
| SPREAD SH2 H | 8 | 700 | 218 | 342 | 140 | 22 | 150 | 16 | 2 | 70 | 15 | 5 | 13 | 38 | 22 | 66 | 70 | 8 | 35 | 70 |
| DUALFLT SH2 H | 7 | 690 | 207 | 341 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 15 | 34 | 20 | 42 | 93 | 8 | 35 | 78 |
| STACK SH2 H | 6 | 749 | 204 | 397 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 20 | 29 | 17 | 60 | 76 | 12 | 35 | 100 |
| BINAUR SH2 H | 6 | 713 | 216 | 347 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 16 | 29 | 18 | 37 | 115 | 8 | 40 | 94 |
| STRIKE SH2 H | 8 | 621 | 210 | 262 | 0 | 0 | 0 | 0 | 0 | 74 | 15 | 5 | 16 | 38 | 24 | 48 | 72 | 14 | 39 | 59 |
| SPREAD SH2 CEL+PP | 8 | 746 | 222 | 342 | 140 | 22 | 152 | 16 | 0 | 112 | 15 | 5 | 13 | 38 | 22 | 68 | 72 | 8 | 37 | 70 |
| DUALFLT SH2 CEL | 7 | 736 | 209 | 342 | 0 | 0 | 0 | 0 | 1 | 113 | 15 | 5 | 15 | 33 | 21 | 43 | 93 | 8 | 36 | 79 |
| SEQ+ARP SH2 CEL | 8 | 679 | 220 | 238 | 34 | 22 | 152 | 16 | 2 | 116 | 16 | 16 | 17 | 38 | 34 | 50 | 76 | 8 | 54 | 58 |
| DUALFLT SH2 HP | 7 | 690 | 209 | 340 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 15 | 34 | 20 | 43 | 94 | 8 | 34 | 79 |
| BINAUR SH2 HP | 6 | 714 | 215 | 348 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 16 | 29 | 18 | 37 | 113 | 8 | 41 | 94 |
| SINGLE PCM | 8 | 488 | 180 | 176 | 50 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 64 | 32 | 8 | 31 | 44 |
| DUAL OSC PCM | 6 | 604 | 176 | 293 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 4 | 14 | 29 | 18 | 87 | 26 | 7 | 29 | 78 |
| SPREAD PCM | 8 | 746 | 200 | 414 | 286 | 6 | 94 | 16 | 2 | 69 | 16 | 5 | 12 | 38 | 24 | 84 | 32 | 8 | 28 | 77 |
| DUAL FILTER PCM | 7 | 602 | 196 | 271 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 4 | 15 | 34 | 20 | 60 | 63 | 8 | 28 | 67 |
| STACK PCM | 6 | 703 | 202 | 365 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 17 | 29 | 18 | 85 | 49 | 12 | 27 | 95 |
| BINAURAL PCM | 6 | 654 | 205 | 311 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 15 | 29 | 18 | 52 | 88 | 8 | 29 | 86 |
| DUALOSC MULTI H PCM | 6 | 601 | 207 | 254 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 14 | 29 | 18 | 87 | 56 | 7 | 34 | 77 |
| DUALFLT MULTI H PCM | 7 | 658 | 232 | 284 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 5 | 14 | 34 | 21 | 59 | 99 | 8 | 35 | 74 |
| BINAUR MULTI H PCM | 6 | 736 | 248 | 337 | 0 | 0 | 0 | 0 | 1 | 74 | 16 | 5 | 16 | 30 | 18 | 52 | 131 | 8 | 39 | 98 |
| SINGLE RS | 8 | 454 | 162 | 160 | 34 | 6 | 92 | 16 | 2 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 8 | 30 | 40 |
| SINGLE RS PCM | 8 | 524 | 180 | 208 | 82 | 6 | 92 | 16 | 0 | 71 | 15 | 4 | 14 | 38 | 22 | 64 | 34 | 8 | 32 | 48 |
| DUALOSC RS | 6 | 688 | 146 | 412 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 28 | 17 | 59 | 25 | 7 | 28 | 93 |
| DUALOSC RS PCM | 6 | 604 | 176 | 294 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 14 | 29 | 18 | 87 | 25 | 7 | 29 | 78 |
| SEQ+ARP HOME PCM | 8 | 625 | 230 | 220 | 86 | 24 | 80 | 18 | 2 | 75 | 17 | 15 | 15 | 38 | 34 | 68 | 66 | 10 | 51 | 56 |
| SINGLE DRV SOFT | 8 | 492 | 168 | 190 | 34 | 6 | 92 | 46 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 14 | 32 | 45 |
| SINGLE DRV HARD | 8 | 494 | 168 | 194 | 34 | 6 | 92 | 48 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 14 | 30 | 45 |
| SINGLE DRV FOLD | 8 | 494 | 168 | 196 | 34 | 6 | 92 | 52 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 14 | 30 | 46 |
| SINGLE DRV TAPE | 8 | 503 | 168 | 202 | 34 | 6 | 92 | 58 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 14 | 31 | 46 |
| SINGLE DRV TUBE | 8 | 500 | 168 | 200 | 34 | 6 | 92 | 54 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 14 | 30 | 46 |
| SINGLE DRV OD | 8 | 501 | 168 | 200 | 34 | 6 | 92 | 56 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 14 | 31 | 46 |
| STACK HEAVY TUBE | 6 | 684 | 198 | 346 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 15 | 29 | 18 | 57 | 68 | 17 | 33 | 91 |
| BINAUR HEAVY OD | 6 | 657 | 211 | 299 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 14 | 29 | 17 | 37 | 106 | 12 | 39 | 85 |
| STRIKE HEAVY TUBE | 8 | 570 | 208 | 218 | 0 | 0 | 0 | 0 | 0 | 72 | 16 | 5 | 13 | 38 | 22 | 48 | 66 | 22 | 38 | 53 |
| SEQ+ARP HOME TUBE | 8 | 600 | 218 | 206 | 34 | 24 | 80 | 56 | 0 | 74 | 16 | 16 | 15 | 38 | 34 | 50 | 66 | 16 | 55 | 53 |
| FULL | 4 | 576 | 119 | 327 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 4 | 13 | 19 | 11 | 40 | 37 | 5 | 27 | 112 |
| FULL HEAVY | 4 | 554 | 132 | 288 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 12 | 19 | 11 | 40 | 50 | 5 | 31 | 105 |
| FULL MULTI H | 4 | 584 | 143 | 305 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 13 | 19 | 12 | 42 | 57 | 5 | 31 | 112 |
| FULL SH2 H | 4 | 655 | 148 | 367 | 0 | 0 | 0 | 0 | 1 | 71 | 17 | 6 | 13 | 19 | 12 | 46 | 57 | 7 | 32 | 129 |
| FULL H CEL+PP | 4 | 631 | 146 | 305 | 0 | 0 | 0 | 0 | 0 | 113 | 16 | 5 | 13 | 19 | 12 | 44 | 59 | 5 | 33 | 113 |
| FULL MULTI H PCM | 4 | 479 | 157 | 186 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 4 | 13 | 19 | 11 | 58 | 57 | 4 | 34 | 86 |
| FULL SH2 H PCM | 4 | 582 | 160 | 281 | 0 | 0 | 0 | 0 | 1 | 72 | 16 | 5 | 15 | 20 | 12 | 60 | 56 | 5 | 32 | 110 |
| FULL SH2 CEL+PP | 4 | 702 | 149 | 370 | 0 | 0 | 0 | 0 | 0 | 112 | 17 | 6 | 13 | 19 | 12 | 47 | 57 | 7 | 35 | 130 |
| DSPRD | 4 | 443 | 115 | 203 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 12 | 19 | 11 | 56 | 17 | 4 | 26 | 80 |
| DSPRD HEAVY | 4 | 457 | 131 | 195 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 12 | 19 | 11 | 56 | 34 | 4 | 32 | 82 |
| DSPRD MULTI H | 4 | 472 | 136 | 202 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 4 | 13 | 19 | 11 | 56 | 38 | 4 | 32 | 84 |
| DSPRD SH2 H | 4 | 500 | 135 | 231 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 4 | 13 | 19 | 11 | 56 | 37 | 4 | 33 | 92 |
| DSPRD H CEL+PP | 4 | 513 | 137 | 202 | 0 | 0 | 0 | 0 | 0 | 109 | 15 | 4 | 13 | 19 | 11 | 57 | 39 | 4 | 33 | 85 |
| DSPRD MULTI H PCM | 4 | 654 | 157 | 362 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 5 | 13 | 19 | 12 | 77 | 37 | 5 | 33 | 130 |
| DSPRD SH2 CEL+PP | 4 | 553 | 139 | 233 | 0 | 0 | 0 | 0 | 0 | 113 | 15 | 6 | 14 | 19 | 12 | 58 | 38 | 5 | 33 | 93 |
| SEQ+ARP DSPRD CEL | 4 | 568 | 149 | 206 | 0 | 0 | 0 | 0 | 1 | 118 | 16 | 15 | 14 | 19 | 18 | 60 | 41 | 5 | 49 | 89 |
| IDLE PHASER | 0 | 137 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 85 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE PHASER x2 | 0 | 155 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 103 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD PHASER | 8 | 612 | 184 | 268 | 138 | 6 | 94 | 16 | 0 | 94 | 16 | 4 | 15 | 40 | 24 | 66 | 34 | 8 | 31 | 56 |
| SPREAD H CEL PHSR | 8 | 719 | 224 | 284 | 140 | 22 | 92 | 16 | 0 | 137 | 15 | 5 | 15 | 38 | 24 | 68 | 74 | 8 | 39 | 64 |
| SEQ+ARP HOME PHSR2 | 8 | 600 | 212 | 168 | 36 | 22 | 80 | 16 | 2 | 117 | 17 | 15 | 15 | 38 | 34 | 50 | 66 | 10 | 54 | 48 |
| IDLE ENSEMBLE | 0 | 139 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 87 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENSEMBLE x2 | 0 | 161 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 108 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| IDLE ENS+PHSR | 0 | 144 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 92 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENS+PHSR x2 | 0 | 169 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS | 0 | 139 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| IDLE CHORUS x2 | 0 | 158 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 105 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| SPREAD H CEL ENPH | 8 | 731 | 228 | 282 | 138 | 22 | 92 | 16 | 0 | 146 | 16 | 5 | 14 | 38 | 24 | 68 | 74 | 10 | 40 | 64 |
| SEQ+ARP HOME ENPH2 | 8 | 621 | 214 | 170 | 36 | 24 | 80 | 16 | 2 | 135 | 17 | 16 | 15 | 40 | 34 | 50 | 68 | 10 | 52 | 48 |
| SPREAD H CEL MIX | 8 | 704 | 226 | 282 | 138 | 22 | 94 | 16 | 0 | 127 | 15 | 5 | 13 | 38 | 22 | 68 | 74 | 8 | 36 | 64 |
| SEQ+ARP CEL MIX | 8 | 614 | 212 | 166 | 34 | 22 | 80 | 16 | 2 | 133 | 16 | 16 | 14 | 38 | 34 | 50 | 68 | 10 | 55 | 48 |
| HEAVY SPRING MIX | 8 | 535 | 194 | 164 | 34 | 22 | 80 | 16 | 0 | 108 | 15 | 4 | 13 | 38 | 22 | 48 | 66 | 8 | 37 | 45 |
| IDLE TAPE FLANGER | 0 | 147 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 94 | 15 | 3 | 13 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE TAPE FLNG x2 | 0 | 171 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 118 | 15 | 3 | 13 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL TFLG | 8 | 735 | 228 | 284 | 140 | 22 | 94 | 16 | 0 | 149 | 15 | 5 | 16 | 38 | 22 | 68 | 74 | 8 | 38 | 64 |
| SEQ+ARP HOME TFLG2 | 8 | 619 | 212 | 168 | 36 | 22 | 80 | 16 | 2 | 136 | 16 | 16 | 16 | 38 | 34 | 50 | 66 | 10 | 53 | 48 |
| IDLE BBD FLANGER | 0 | 140 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 87 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE BBD FLNG x2 | 0 | 159 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 106 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL BBDF | 8 | 729 | 228 | 282 | 138 | 22 | 94 | 16 | 0 | 144 | 15 | 6 | 14 | 38 | 24 | 68 | 74 | 8 | 40 | 64 |
| IDLE PITCH SHIFT | 0 | 146 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 93 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| PITCH SHIFT HELD | 8 | 488 | 164 | 162 | 34 | 6 | 92 | 16 | 0 | 98 | 15 | 4 | 14 | 38 | 22 | 48 | 34 | 8 | 31 | 41 |
| IDLE PITCH SHFT x2 | 0 | 173 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 120 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| SPREAD H CEL PSHF | 8 | 725 | 226 | 282 | 138 | 22 | 94 | 16 | 0 | 144 | 16 | 5 | 14 | 38 | 22 | 68 | 76 | 10 | 38 | 64 |

### 2026-11-02 19:50, CARNAC OS PROF (1 core), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.4 | 22.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 31.4 | 32.4 | 0 | 1 | 57.9 | 4 | pass |
| SINGLE | 83.6 | 85.0 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE HEAVY | 90.9 | 92.5 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC | 126.5 | 127.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD | 107.5 | 109.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 97.9 | 99.5 | 0 | 7 | 39.9 | 12 | FAIL |
| STACK | 122.1 | 122.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL | 101.2 | 102.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 122.7 | 123.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 114.8 | 116.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 102.3 | 103.9 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 114.5 | 116.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 109.1 | 110.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 83.6 | 85.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE MS20 | 78.8 | 80.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE SH-2 | 94.3 | 96.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE FORMANT | 79.4 | 80.7 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FLT LADDER | 98.0 | 99.5 | 0 | 7 | 39.2 | 12 | FAIL |
| SEQ+ARP HOME | 100.6 | 104.3 | 27 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 100.7 | 104.2 | 9 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 99.9 | 104.2 | 63 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 101.4 | 105.9 | 27 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 83.6 | 85.1 | 0 | 8 | 42.9 | 1 | FAIL |
| HEAVY MOD+PP | 97.9 | 99.6 | 0 | 8 | 42.4 | 12 | FAIL |
| HEAVY CELEST+PP | 98.4 | 100.3 | 19 | 8 | 32.9 | 24 | FAIL |
| SEQ+ARP CELEST+PP | 109.7 | 112.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MULTI H | 95.4 | 97.0 | 0 | 8 | 43.2 | 12 | FAIL |
| DUALOSC MULTI H | 125.7 | 126.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD MULTI H | 119.0 | 120.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H | 109.8 | 111.5 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK MULTI H | 121.4 | 123.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H | 116.1 | 117.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL+PP | 126.6 | 128.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC H CEL+PP | 134.1 | 135.7 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H TUM+PP | 124.2 | 126.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPRD CEL | 137.2 | 140.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 95.5 | 96.8 | 0 | 8 | 43.2 | 12 | FAIL |
| HEAVY CEL+BBD PP | 100.5 | 101.0 | 17 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPR+BBD | 106.7 | 109.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STRIKE | 83.6 | 85.0 | 0 | 8 | 42.9 | 1 | FAIL |
| STRIKE HEAVY | 90.7 | 92.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP STRIKE | 115.0 | 117.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRK FILT | 116.5 | 119.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE SH2 H | 106.9 | 109.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC SH2 H | 133.9 | 135.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 H | 129.2 | 131.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 H | 127.3 | 129.3 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK SH2 H | 138.2 | 140.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 H | 131.5 | 132.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE SH2 H | 114.2 | 115.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 CEL+PP | 137.6 | 139.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 CEL | 135.9 | 137.6 | 8 | 7 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SH2 CEL | 125.2 | 128.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 HP | 127.5 | 129.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 HP | 131.5 | 133.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE PCM | 90.3 | 92.1 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC PCM | 111.3 | 112.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD PCM | 137.1 | 139.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER PCM | 111.0 | 112.5 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK PCM | 130.2 | 132.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL PCM | 120.9 | 122.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC MULTI H PCM | 111.0 | 112.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H PCM | 121.9 | 123.7 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H PCM | 135.9 | 137.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE RS | 83.8 | 85.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE RS PCM | 96.7 | 99.7 | 0 | 8 | 40.7 | 12 | FAIL |
| DUALOSC RS | 126.4 | 127.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC RS PCM | 111.4 | 112.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PCM | 115.6 | 118.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV SOFT | 90.7 | 92.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV HARD | 91.1 | 92.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV FOLD | 91.2 | 92.7 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TAPE | 92.7 | 94.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TUBE | 92.2 | 93.7 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV OD | 92.5 | 94.0 | 0 | 8 | 43.2 | 1 | FAIL |
| STACK HEAVY TUBE | 126.2 | 127.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR HEAVY OD | 121.3 | 123.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE HEAVY TUBE | 105.2 | 106.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TUBE | 110.6 | 114.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| FULL | 106.3 | 107.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL HEAVY | 102.1 | 103.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H | 106.6 | 107.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 H | 120.3 | 121.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL H CEL+PP | 115.2 | 116.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H PCM | 88.9 | 90.4 | 0 | 4 | 43.2 | 1 | FAIL |
| FULL SH2 H PCM | 107.9 | 109.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 CEL+PP | 128.8 | 130.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD | 82.0 | 83.6 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD HEAVY | 84.6 | 86.2 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD MULTI H | 87.5 | 89.0 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD SH2 H | 92.5 | 93.9 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD H CEL+PP | 95.0 | 96.5 | 0 | 4 | 43.2 | 12 | FAIL |
| DSPRD MULTI H PCM | 119.9 | 120.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD SH2 CEL+PP | 102.4 | 103.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SEQ+ARP DSPRD CEL | 105.1 | 107.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| IDLE PHASER | 25.8 | 26.5 | 0 | 0 | 60.3 | 4 | pass |
| IDLE PHASER x2 | 29.1 | 29.8 | 0 | 0 | 59.6 | 4 | pass |
| SPREAD PHASER | 113.1 | 115.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL PHSR | 132.6 | 134.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PHSR2 | 110.9 | 113.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE ENSEMBLE | 26.1 | 26.7 | 0 | 0 | 59.9 | 4 | pass |
| IDLE ENSEMBLE x2 | 30.0 | 30.5 | 0 | 0 | 58.6 | 4 | pass |
| IDLE ENS+PHSR | 27.0 | 27.6 | 0 | 0 | 59.6 | 4 | pass |
| IDLE ENS+PHSR x2 | 31.6 | 32.2 | 0 | 0 | 57.6 | 4 | pass |
| IDLE CHORUS | 26.1 | 26.6 | 0 | 0 | 60.3 | 4 | pass |
| IDLE CHORUS x2 | 29.6 | 30.2 | 0 | 0 | 58.3 | 4 | pass |
| SPREAD H CEL ENPH | 134.7 | 137.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME ENPH2 | 115.0 | 118.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL MIX | 130.0 | 132.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP CEL MIX | 113.3 | 116.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING MIX | 98.7 | 100.4 | 31 | 8 | 33.2 | 24 | FAIL |
| IDLE TAPE FLANGER | 27.6 | 28.2 | 0 | 0 | 60.3 | 4 | pass |
| IDLE TAPE FLNG x2 | 32.1 | 32.7 | 0 | 0 | 58.0 | 4 | pass |
| SPREAD H CEL TFLG | 135.4 | 137.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TFLG2 | 114.6 | 117.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE BBD FLANGER | 26.3 | 27.0 | 0 | 0 | 59.9 | 4 | pass |
| IDLE BBD FLNG x2 | 29.8 | 30.6 | 0 | 0 | 58.6 | 4 | pass |
| SPREAD H CEL BBDF | 134.4 | 136.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE PITCH SHIFT | 27.3 | 28.2 | 0 | 0 | 59.9 | 4 | pass |
| PITCH SHIFT HELD | 90.2 | 92.2 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHFT x2 | 32.3 | 33.5 | 0 | 0 | 57.6 | 1 | pass |
| SPREAD H CEL PSHF | 133.7 | 135.9 | 8 | 8 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice per 16-sample block (the voice stages, CTRL and its c.* split); 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 113 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 63 | 15 | 3 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SINGLE 1V | 1 | 167 | 23 | 21 | 5 | 0 | 12 | 2 | 0 | 68 | 15 | 4 | 12 | 5 | 3 | 6 | 5 | 1 | 24 | 44 |
| SINGLE | 8 | 452 | 160 | 160 | 34 | 6 | 92 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 8 | 32 | 40 |
| SINGLE HEAVY | 8 | 491 | 192 | 162 | 32 | 22 | 80 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 64 | 8 | 37 | 44 |
| DUAL OSC | 6 | 685 | 148 | 408 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 29 | 17 | 60 | 26 | 7 | 27 | 93 |
| SPREAD | 8 | 581 | 182 | 266 | 140 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 14 | 38 | 22 | 66 | 32 | 8 | 31 | 56 |
| DUAL FILTER | 7 | 530 | 175 | 224 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 14 | 33 | 19 | 41 | 63 | 8 | 29 | 57 |
| STACK | 6 | 661 | 174 | 351 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 16 | 29 | 17 | 58 | 49 | 12 | 29 | 88 |
| BINAURAL | 6 | 547 | 178 | 233 | 0 | 0 | 0 | 0 | 1 | 72 | 16 | 5 | 15 | 30 | 18 | 37 | 75 | 8 | 27 | 69 |
| DUAL OSC HEAVY | 6 | 664 | 170 | 357 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 4 | 12 | 29 | 17 | 59 | 49 | 7 | 35 | 88 |
| SPREAD HEAVY | 8 | 621 | 212 | 270 | 140 | 22 | 80 | 16 | 2 | 70 | 15 | 4 | 13 | 38 | 24 | 64 | 66 | 8 | 35 | 60 |
| DUAL FLT HEAVY | 7 | 553 | 200 | 216 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 33 | 20 | 41 | 86 | 9 | 35 | 60 |
| STACK HEAVY | 6 | 619 | 192 | 288 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 15 | 29 | 18 | 58 | 66 | 12 | 34 | 80 |
| BINAURAL HEAVY | 6 | 590 | 206 | 238 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 14 | 29 | 17 | 36 | 106 | 8 | 40 | 74 |
| SINGLE LADDER | 8 | 452 | 162 | 160 | 34 | 6 | 92 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 8 | 30 | 40 |
| SINGLE MS20 | 8 | 425 | 154 | 140 | 32 | 6 | 72 | 16 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 46 | 26 | 8 | 30 | 37 |
| SINGLE SH-2 | 8 | 510 | 160 | 218 | 34 | 6 | 150 | 16 | 0 | 67 | 15 | 4 | 14 | 38 | 22 | 48 | 32 | 8 | 32 | 47 |
| SINGLE FORMANT | 8 | 428 | 154 | 146 | 32 | 6 | 80 | 16 | 0 | 66 | 15 | 4 | 12 | 38 | 22 | 46 | 26 | 8 | 31 | 38 |
| DUAL FLT LADDER | 7 | 530 | 175 | 224 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 14 | 33 | 19 | 41 | 63 | 8 | 29 | 57 |
| SEQ+ARP HOME | 8 | 543 | 208 | 168 | 34 | 24 | 80 | 16 | 2 | 72 | 16 | 13 | 14 | 38 | 32 | 48 | 66 | 10 | 50 | 47 |
| SEQ+ARP AMP | 8 | 544 | 208 | 166 | 34 | 24 | 80 | 16 | 2 | 72 | 16 | 13 | 14 | 38 | 32 | 48 | 66 | 8 | 53 | 47 |
| SEQ+ARP SEQ | 8 | 540 | 206 | 168 | 34 | 24 | 80 | 16 | 2 | 71 | 16 | 13 | 14 | 38 | 32 | 48 | 66 | 8 | 50 | 47 |
| SEQ+ARP MOD1 | 8 | 548 | 210 | 168 | 34 | 24 | 80 | 16 | 2 | 72 | 16 | 13 | 17 | 38 | 32 | 48 | 66 | 10 | 50 | 48 |
| LADDER FILTER PG | 8 | 451 | 160 | 160 | 34 | 6 | 92 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 8 | 31 | 40 |
| HEAVY MOD+PP | 8 | 529 | 194 | 162 | 34 | 22 | 80 | 16 | 0 | 103 | 15 | 4 | 14 | 38 | 22 | 46 | 66 | 8 | 37 | 44 |
| HEAVY CELEST+PP | 8 | 532 | 192 | 162 | 32 | 22 | 80 | 16 | 0 | 108 | 15 | 4 | 12 | 38 | 22 | 46 | 64 | 8 | 39 | 44 |
| SEQ+ARP CELEST+PP | 8 | 593 | 210 | 168 | 34 | 24 | 80 | 18 | 2 | 115 | 16 | 15 | 14 | 38 | 34 | 50 | 66 | 10 | 53 | 48 |
| SINGLE MULTI H | 8 | 516 | 200 | 176 | 34 | 22 | 92 | 16 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 72 | 8 | 38 | 47 |
| DUALOSC MULTI H | 6 | 680 | 176 | 368 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 29 | 17 | 59 | 55 | 7 | 34 | 91 |
| SPREAD MULTI H | 8 | 644 | 222 | 282 | 138 | 22 | 94 | 16 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 66 | 72 | 8 | 38 | 63 |
| DUALFLT MULTI H | 7 | 594 | 214 | 239 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 14 | 34 | 21 | 42 | 99 | 8 | 36 | 65 |
| STACK MULTI H | 6 | 657 | 203 | 310 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 5 | 16 | 28 | 17 | 58 | 79 | 12 | 35 | 86 |
| BINAUR MULTI H | 6 | 628 | 220 | 261 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 5 | 15 | 30 | 18 | 36 | 118 | 8 | 39 | 80 |
| SPREAD H CEL+PP | 8 | 685 | 222 | 282 | 140 | 22 | 92 | 16 | 2 | 110 | 16 | 4 | 14 | 38 | 22 | 68 | 72 | 8 | 35 | 63 |
| DUALOSC H CEL+PP | 6 | 726 | 179 | 367 | 0 | 0 | 0 | 0 | 1 | 110 | 15 | 5 | 13 | 29 | 17 | 60 | 57 | 7 | 36 | 91 |
| SPREAD H TUM+PP | 8 | 672 | 224 | 280 | 138 | 22 | 92 | 16 | 0 | 97 | 16 | 5 | 14 | 38 | 22 | 68 | 74 | 8 | 36 | 63 |
| SEQ+ARP SPRD CEL | 8 | 743 | 238 | 288 | 142 | 22 | 94 | 16 | 2 | 117 | 16 | 15 | 14 | 38 | 34 | 68 | 76 | 8 | 53 | 66 |
| HEAVY SPRING+PP | 8 | 516 | 194 | 162 | 32 | 22 | 80 | 16 | 2 | 91 | 15 | 4 | 12 | 38 | 22 | 46 | 64 | 8 | 36 | 45 |
| HEAVY CEL+BBD PP | 8 | 543 | 194 | 164 | 34 | 22 | 80 | 16 | 0 | 115 | 15 | 5 | 13 | 38 | 22 | 48 | 66 | 8 | 37 | 45 |
| SEQ+ARP SPR+BBD | 8 | 576 | 212 | 168 | 34 | 24 | 80 | 16 | 2 | 99 | 17 | 15 | 14 | 38 | 34 | 48 | 68 | 10 | 49 | 48 |
| STRIKE | 8 | 451 | 162 | 156 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 4 | 14 | 38 | 22 | 46 | 32 | 8 | 31 | 40 |
| STRIKE HEAVY | 8 | 490 | 194 | 158 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 64 | 8 | 36 | 44 |
| SEQ+ARP STRIKE | 8 | 621 | 218 | 182 | 0 | 0 | 0 | 0 | 2 | 119 | 16 | 16 | 14 | 38 | 34 | 50 | 66 | 16 | 54 | 50 |
| SEQ+ARP STRK FILT | 8 | 630 | 218 | 192 | 0 | 0 | 0 | 0 | 2 | 116 | 16 | 16 | 14 | 38 | 34 | 50 | 68 | 16 | 56 | 52 |
| SINGLE SH2 H | 8 | 578 | 202 | 234 | 34 | 22 | 150 | 16 | 0 | 69 | 15 | 5 | 15 | 38 | 22 | 48 | 72 | 8 | 38 | 54 |
| DUALOSC SH2 H | 6 | 725 | 175 | 412 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 29 | 18 | 59 | 53 | 7 | 36 | 98 |
| SPREAD SH2 H | 8 | 700 | 220 | 340 | 140 | 22 | 150 | 16 | 0 | 69 | 15 | 5 | 14 | 38 | 22 | 66 | 70 | 10 | 37 | 70 |
| DUALFLT SH2 H | 7 | 689 | 208 | 340 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 15 | 34 | 20 | 42 | 93 | 8 | 35 | 78 |
| STACK SH2 H | 6 | 748 | 203 | 397 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 20 | 30 | 18 | 59 | 76 | 12 | 35 | 100 |
| BINAUR SH2 H | 6 | 712 | 215 | 347 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 5 | 16 | 29 | 18 | 37 | 113 | 8 | 41 | 94 |
| STRIKE SH2 H | 8 | 617 | 208 | 264 | 0 | 0 | 0 | 0 | 0 | 72 | 15 | 5 | 15 | 38 | 24 | 48 | 70 | 14 | 38 | 59 |
| SPREAD SH2 CEL+PP | 8 | 745 | 220 | 342 | 138 | 22 | 154 | 16 | 0 | 111 | 15 | 5 | 14 | 38 | 24 | 66 | 70 | 8 | 38 | 70 |
| DUALFLT SH2 CEL | 7 | 735 | 209 | 342 | 0 | 0 | 0 | 0 | 1 | 112 | 15 | 5 | 15 | 34 | 21 | 43 | 93 | 8 | 36 | 79 |
| SEQ+ARP SH2 CEL | 8 | 677 | 218 | 240 | 34 | 22 | 152 | 16 | 2 | 116 | 16 | 16 | 16 | 38 | 34 | 50 | 72 | 10 | 53 | 58 |
| DUALFLT SH2 HP | 7 | 690 | 208 | 341 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 4 | 15 | 33 | 21 | 42 | 94 | 8 | 36 | 79 |
| BINAUR SH2 HP | 6 | 711 | 216 | 346 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 5 | 16 | 29 | 18 | 37 | 115 | 8 | 40 | 94 |
| SINGLE PCM | 8 | 488 | 180 | 176 | 50 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 64 | 32 | 8 | 31 | 44 |
| DUAL OSC PCM | 6 | 602 | 176 | 293 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 4 | 14 | 29 | 18 | 87 | 26 | 7 | 28 | 78 |
| SPREAD PCM | 8 | 742 | 198 | 410 | 284 | 6 | 94 | 16 | 0 | 69 | 15 | 5 | 13 | 38 | 24 | 84 | 32 | 10 | 32 | 76 |
| DUAL FILTER PCM | 7 | 600 | 195 | 270 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 15 | 34 | 20 | 59 | 64 | 8 | 28 | 67 |
| STACK PCM | 6 | 705 | 202 | 366 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 18 | 29 | 18 | 85 | 49 | 12 | 28 | 95 |
| BINAURAL PCM | 6 | 654 | 205 | 312 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 15 | 30 | 18 | 51 | 88 | 8 | 28 | 86 |
| DUALOSC MULTI H PCM | 6 | 600 | 207 | 253 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 14 | 29 | 18 | 87 | 56 | 7 | 33 | 77 |
| DUALFLT MULTI H PCM | 7 | 660 | 232 | 284 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 4 | 15 | 34 | 21 | 60 | 98 | 8 | 37 | 74 |
| BINAUR MULTI H PCM | 6 | 735 | 248 | 337 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 16 | 30 | 18 | 52 | 130 | 8 | 40 | 98 |
| SINGLE RS | 8 | 452 | 162 | 160 | 34 | 6 | 92 | 16 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 8 | 30 | 40 |
| SINGLE RS PCM | 8 | 522 | 180 | 208 | 82 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 14 | 38 | 22 | 64 | 32 | 8 | 32 | 48 |
| DUALOSC RS | 6 | 684 | 147 | 408 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 29 | 17 | 59 | 25 | 7 | 27 | 93 |
| DUALOSC RS PCM | 6 | 602 | 175 | 294 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 4 | 14 | 29 | 18 | 87 | 25 | 7 | 28 | 78 |
| SEQ+ARP HOME PCM | 8 | 625 | 228 | 220 | 88 | 24 | 80 | 16 | 2 | 74 | 16 | 15 | 15 | 38 | 32 | 68 | 66 | 10 | 55 | 56 |
| SINGLE DRV SOFT | 8 | 490 | 168 | 190 | 34 | 6 | 92 | 46 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 14 | 31 | 45 |
| SINGLE DRV HARD | 8 | 492 | 168 | 192 | 34 | 6 | 92 | 48 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 14 | 31 | 45 |
| SINGLE DRV FOLD | 8 | 493 | 168 | 196 | 34 | 6 | 92 | 52 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 14 | 29 | 46 |
| SINGLE DRV TAPE | 8 | 501 | 168 | 200 | 34 | 6 | 92 | 56 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 14 | 31 | 46 |
| SINGLE DRV TUBE | 8 | 498 | 168 | 198 | 34 | 6 | 92 | 54 | 0 | 70 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 14 | 30 | 46 |
| SINGLE DRV OD | 8 | 500 | 168 | 200 | 34 | 6 | 92 | 56 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 14 | 31 | 46 |
| STACK HEAVY TUBE | 6 | 683 | 198 | 346 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 14 | 28 | 17 | 58 | 68 | 17 | 34 | 91 |
| BINAUR HEAVY OD | 6 | 656 | 211 | 298 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 14 | 29 | 17 | 36 | 107 | 13 | 39 | 85 |
| STRIKE HEAVY TUBE | 8 | 569 | 208 | 218 | 0 | 0 | 0 | 0 | 2 | 72 | 15 | 5 | 14 | 38 | 22 | 48 | 66 | 20 | 35 | 54 |
| SEQ+ARP HOME TUBE | 8 | 598 | 216 | 206 | 34 | 24 | 80 | 56 | 2 | 74 | 17 | 16 | 14 | 38 | 32 | 48 | 68 | 16 | 53 | 53 |
| FULL | 4 | 575 | 120 | 325 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 4 | 13 | 20 | 12 | 40 | 37 | 5 | 29 | 111 |
| FULL HEAVY | 4 | 552 | 131 | 285 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 13 | 19 | 12 | 40 | 48 | 5 | 31 | 104 |
| FULL MULTI H | 4 | 576 | 140 | 300 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 5 | 13 | 19 | 12 | 41 | 57 | 5 | 33 | 110 |
| FULL SH2 H | 4 | 651 | 147 | 364 | 0 | 0 | 0 | 0 | 1 | 71 | 17 | 6 | 13 | 20 | 12 | 46 | 56 | 6 | 32 | 128 |
| FULL H CEL+PP | 4 | 623 | 143 | 301 | 0 | 0 | 0 | 0 | 1 | 112 | 15 | 5 | 13 | 19 | 12 | 42 | 59 | 5 | 33 | 111 |
| FULL MULTI H PCM | 4 | 480 | 158 | 186 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 4 | 14 | 20 | 12 | 58 | 57 | 4 | 32 | 86 |
| FULL SH2 H PCM | 4 | 583 | 158 | 283 | 0 | 0 | 0 | 0 | 0 | 71 | 16 | 5 | 15 | 20 | 12 | 61 | 54 | 4 | 35 | 110 |
| FULL SH2 CEL+PP | 4 | 697 | 148 | 365 | 0 | 0 | 0 | 0 | 1 | 112 | 17 | 6 | 13 | 20 | 12 | 47 | 57 | 6 | 35 | 128 |
| DSPRD | 4 | 443 | 115 | 203 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 12 | 19 | 11 | 56 | 17 | 4 | 26 | 80 |
| DSPRD HEAVY | 4 | 457 | 130 | 195 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 12 | 19 | 11 | 56 | 33 | 4 | 32 | 82 |
| DSPRD MULTI H | 4 | 473 | 136 | 203 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 4 | 13 | 19 | 11 | 56 | 38 | 4 | 32 | 85 |
| DSPRD SH2 H | 4 | 500 | 135 | 231 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 4 | 13 | 19 | 11 | 56 | 37 | 4 | 33 | 92 |
| DSPRD H CEL+PP | 4 | 513 | 137 | 202 | 0 | 0 | 0 | 0 | 0 | 109 | 15 | 4 | 13 | 19 | 11 | 57 | 38 | 4 | 33 | 85 |
| DSPRD MULTI H PCM | 4 | 649 | 156 | 359 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 5 | 12 | 19 | 12 | 76 | 37 | 5 | 33 | 129 |
| DSPRD SH2 CEL+PP | 4 | 553 | 139 | 233 | 0 | 0 | 0 | 0 | 1 | 112 | 15 | 5 | 14 | 20 | 12 | 58 | 38 | 5 | 34 | 93 |
| SEQ+ARP DSPRD CEL | 4 | 568 | 150 | 206 | 0 | 0 | 0 | 0 | 1 | 117 | 16 | 15 | 15 | 19 | 18 | 60 | 41 | 5 | 48 | 89 |
| IDLE PHASER | 0 | 137 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 84 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE PHASER x2 | 0 | 155 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 102 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD PHASER | 8 | 612 | 184 | 266 | 140 | 6 | 94 | 16 | 2 | 94 | 15 | 4 | 15 | 38 | 22 | 66 | 34 | 10 | 32 | 56 |
| SPREAD H CEL PHSR | 8 | 717 | 224 | 282 | 138 | 22 | 92 | 16 | 2 | 137 | 15 | 5 | 15 | 38 | 24 | 66 | 74 | 8 | 37 | 64 |
| SEQ+ARP HOME PHSR2 | 8 | 599 | 212 | 166 | 34 | 22 | 80 | 16 | 2 | 118 | 17 | 15 | 16 | 38 | 34 | 48 | 68 | 10 | 53 | 48 |
| IDLE ENSEMBLE | 0 | 139 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 87 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE ENSEMBLE x2 | 0 | 160 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 108 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE ENS+PHSR | 0 | 143 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 91 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENS+PHSR x2 | 0 | 169 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 116 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS | 0 | 139 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS x2 | 0 | 158 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 105 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL ENPH | 8 | 729 | 226 | 282 | 140 | 22 | 92 | 16 | 2 | 146 | 16 | 5 | 15 | 38 | 24 | 68 | 74 | 8 | 37 | 64 |
| SEQ+ARP HOME ENPH2 | 8 | 621 | 212 | 168 | 34 | 24 | 80 | 16 | 2 | 135 | 17 | 17 | 15 | 38 | 34 | 50 | 68 | 10 | 55 | 48 |
| SPREAD H CEL MIX | 8 | 703 | 226 | 280 | 138 | 22 | 92 | 16 | 0 | 126 | 15 | 5 | 14 | 38 | 22 | 68 | 74 | 8 | 37 | 63 |
| SEQ+ARP CEL MIX | 8 | 612 | 210 | 168 | 34 | 24 | 80 | 16 | 2 | 133 | 17 | 15 | 15 | 38 | 34 | 50 | 66 | 10 | 52 | 48 |
| HEAVY SPRING MIX | 8 | 534 | 194 | 162 | 32 | 22 | 80 | 16 | 0 | 107 | 15 | 4 | 13 | 38 | 22 | 48 | 66 | 8 | 39 | 44 |
| IDLE TAPE FLANGER | 0 | 147 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 93 | 15 | 4 | 13 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE TAPE FLNG x2 | 0 | 171 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 4 | 13 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL TFLG | 8 | 733 | 228 | 282 | 140 | 22 | 92 | 16 | 0 | 149 | 15 | 5 | 16 | 38 | 24 | 68 | 76 | 8 | 38 | 64 |
| SEQ+ARP HOME TFLG2 | 8 | 619 | 212 | 168 | 36 | 22 | 80 | 16 | 2 | 136 | 16 | 15 | 16 | 40 | 34 | 50 | 66 | 10 | 54 | 48 |
| IDLE BBD FLANGER | 0 | 140 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 87 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE BBD FLNG x2 | 0 | 159 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 106 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL BBDF | 8 | 727 | 228 | 282 | 138 | 22 | 92 | 16 | 0 | 144 | 16 | 6 | 14 | 38 | 24 | 68 | 74 | 8 | 37 | 64 |
| IDLE PITCH SHIFT | 0 | 145 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 93 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| PITCH SHIFT HELD | 8 | 487 | 164 | 162 | 34 | 6 | 92 | 16 | 0 | 98 | 15 | 4 | 14 | 38 | 22 | 48 | 34 | 8 | 30 | 41 |
| IDLE PITCH SHFT x2 | 0 | 172 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 120 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| SPREAD H CEL PSHF | 8 | 724 | 226 | 282 | 140 | 22 | 94 | 16 | 2 | 143 | 15 | 5 | 14 | 38 | 24 | 68 | 74 | 10 | 37 | 64 |

### 2026-11-02 19:46, CARNAC OS PROF (1 core), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.4 | 22.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 31.1 | 32.1 | 0 | 1 | 58.0 | 4 | pass |
| SINGLE | 82.5 | 83.9 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE HEAVY | 90.2 | 91.8 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC | 116.8 | 117.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD | 103.6 | 105.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 96.9 | 98.2 | 0 | 7 | 42.4 | 12 | FAIL |
| STACK | 120.9 | 121.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL | 98.5 | 100.0 | 10 | 6 | 32.3 | 24 | FAIL |
| DUAL OSC HEAVY | 112.8 | 113.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 110.9 | 112.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 101.5 | 103.1 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 113.8 | 115.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 107.7 | 109.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 82.5 | 84.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE MS20 | 78.1 | 79.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE SH-2 | 93.9 | 95.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE FORMANT | 78.7 | 80.2 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FLT LADDER | 96.9 | 98.4 | 0 | 7 | 42.9 | 12 | FAIL |
| SEQ+ARP HOME | 99.2 | 104.3 | 74 | 8 | 37.5 | 59 | FAIL |
| SEQ+ARP AMP | 99.4 | 104.3 | 85 | 8 | 38.9 | 106 | FAIL |
| SEQ+ARP SEQ | 98.6 | 103.1 | 66 | 8 | 8.1 | 127 | FAIL |
| SEQ+ARP MOD1 | 99.9 | 104.3 | 91 | 8 | 11.3 | 127 | FAIL |
| LADDER FILTER PG | 82.4 | 83.8 | 0 | 8 | 43.2 | 1 | FAIL |
| HEAVY MOD+PP | 97.1 | 98.6 | 0 | 8 | 39.4 | 12 | FAIL |
| HEAVY CELEST+PP | 97.9 | 99.7 | 0 | 8 | 38.2 | 12 | FAIL |
| SEQ+ARP CELEST+PP | 108.8 | 111.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MULTI H | 94.3 | 95.8 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALOSC MULTI H | 115.9 | 116.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD MULTI H | 115.5 | 117.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H | 108.9 | 110.4 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK MULTI H | 120.1 | 121.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H | 114.8 | 116.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL+PP | 123.6 | 125.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC H CEL+PP | 124.5 | 125.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H TUM+PP | 121.4 | 122.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPRD CEL | 133.0 | 135.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 94.8 | 96.4 | 0 | 8 | 43.2 | 1 | FAIL |
| HEAVY CEL+BBD PP | 98.6 | 100.3 | 25 | 8 | 33.5 | 24 | FAIL |
| SEQ+ARP SPR+BBD | 105.7 | 108.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STRIKE | 82.4 | 83.8 | 0 | 8 | 42.9 | 1 | FAIL |
| STRIKE HEAVY | 89.9 | 91.3 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP STRIKE | 114.0 | 116.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRK FILT | 115.5 | 118.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE SH2 H | 106.4 | 108.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC SH2 H | 124.3 | 125.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 H | 125.8 | 127.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 H | 126.7 | 128.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK SH2 H | 137.5 | 139.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 H | 130.4 | 132.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE SH2 H | 113.5 | 114.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 CEL+PP | 134.2 | 136.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 CEL | 135.2 | 137.0 | 8 | 7 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SH2 CEL | 124.8 | 127.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 HP | 126.8 | 128.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 HP | 130.4 | 131.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE PCM | 90.4 | 92.1 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC PCM | 111.3 | 112.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD PCM | 137.1 | 139.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER PCM | 111.2 | 112.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK PCM | 130.4 | 132.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL PCM | 121.0 | 122.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC MULTI H PCM | 111.1 | 112.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H PCM | 122.0 | 123.5 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H PCM | 135.9 | 137.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE RS | 82.6 | 83.9 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE RS PCM | 96.7 | 99.8 | 0 | 8 | 41.1 | 12 | FAIL |
| DUALOSC RS | 116.6 | 117.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC RS PCM | 111.4 | 113.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PCM | 115.6 | 118.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV SOFT | 89.6 | 91.0 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE DRV HARD | 90.0 | 91.4 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV FOLD | 90.1 | 91.5 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TAPE | 91.5 | 92.9 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TUBE | 91.1 | 92.5 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV OD | 91.4 | 92.8 | 0 | 8 | 43.2 | 1 | FAIL |
| STACK HEAVY TUBE | 125.6 | 126.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR HEAVY OD | 119.9 | 121.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE HEAVY TUBE | 104.2 | 106.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TUBE | 109.6 | 112.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| FULL | 98.8 | 100.6 | 33 | 4 | 31.9 | 35 | FAIL |
| FULL HEAVY | 94.3 | 95.7 | 0 | 4 | 43.2 | 1 | FAIL |
| FULL MULTI H | 98.6 | 100.7 | 22 | 4 | 32.3 | 35 | FAIL |
| FULL SH2 H | 113.7 | 114.9 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL H CEL+PP | 108.6 | 109.5 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H PCM | 88.9 | 90.4 | 0 | 4 | 43.2 | 1 | FAIL |
| FULL SH2 H PCM | 108.1 | 109.4 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 CEL+PP | 122.4 | 124.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD | 78.3 | 80.0 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD HEAVY | 82.4 | 83.9 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD MULTI H | 85.1 | 86.6 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD SH2 H | 90.3 | 91.6 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD H CEL+PP | 93.1 | 94.6 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD MULTI H PCM | 120.1 | 121.4 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD SH2 CEL+PP | 98.5 | 100.1 | 12 | 4 | 28.1 | 24 | FAIL |
| SEQ+ARP DSPRD CEL | 102.4 | 104.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| IDLE PHASER | 25.8 | 26.6 | 0 | 0 | 60.0 | 4 | pass |
| IDLE PHASER x2 | 29.1 | 29.8 | 0 | 0 | 59.0 | 4 | pass |
| SPREAD PHASER | 109.1 | 111.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL PHSR | 129.3 | 131.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PHSR2 | 109.9 | 112.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE ENSEMBLE | 26.2 | 26.7 | 0 | 0 | 59.9 | 4 | pass |
| IDLE ENSEMBLE x2 | 30.0 | 30.6 | 0 | 0 | 58.3 | 4 | pass |
| IDLE ENS+PHSR | 27.0 | 27.5 | 0 | 0 | 60.0 | 4 | pass |
| IDLE ENS+PHSR x2 | 31.6 | 32.3 | 0 | 0 | 58.0 | 4 | pass |
| IDLE CHORUS | 26.0 | 26.6 | 0 | 0 | 60.2 | 4 | pass |
| IDLE CHORUS x2 | 29.6 | 30.3 | 0 | 0 | 58.6 | 4 | pass |
| SPREAD H CEL ENPH | 132.0 | 134.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME ENPH2 | 114.0 | 116.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL MIX | 127.0 | 129.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP CEL MIX | 112.7 | 115.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING MIX | 98.1 | 99.8 | 0 | 8 | 38.2 | 12 | FAIL |
| IDLE TAPE FLANGER | 27.5 | 28.2 | 0 | 0 | 60.0 | 4 | pass |
| IDLE TAPE FLNG x2 | 32.1 | 32.7 | 0 | 0 | 57.6 | 4 | pass |
| SPREAD H CEL TFLG | 132.0 | 133.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TFLG2 | 113.6 | 116.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE BBD FLANGER | 26.3 | 27.0 | 0 | 0 | 59.9 | 4 | pass |
| IDLE BBD FLNG x2 | 29.8 | 30.5 | 0 | 0 | 58.6 | 4 | pass |
| SPREAD H CEL BBDF | 131.0 | 133.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE PITCH SHIFT | 27.3 | 28.2 | 0 | 0 | 59.6 | 4 | pass |
| PITCH SHIFT HELD | 89.1 | 91.1 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHFT x2 | 32.3 | 33.5 | 0 | 0 | 57.6 | 1 | pass |
| SPREAD H CEL PSHF | 130.5 | 132.4 | 8 | 8 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice per 16-sample block (the voice stages, CTRL and its c.* split); 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 113 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 63 | 15 | 3 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SINGLE 1V | 1 | 166 | 23 | 21 | 4 | 1 | 12 | 2 | 0 | 67 | 15 | 4 | 12 | 5 | 3 | 6 | 5 | 1 | 24 | 44 |
| SINGLE | 8 | 445 | 160 | 156 | 30 | 6 | 92 | 16 | 0 | 66 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 8 | 32 | 40 |
| SINGLE HEAVY | 8 | 487 | 192 | 160 | 30 | 22 | 80 | 16 | 0 | 67 | 15 | 4 | 12 | 38 | 22 | 46 | 64 | 8 | 37 | 44 |
| DUAL OSC | 6 | 632 | 146 | 355 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 4 | 13 | 29 | 17 | 58 | 25 | 7 | 28 | 84 |
| SPREAD | 8 | 560 | 182 | 246 | 120 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 66 | 34 | 8 | 31 | 54 |
| DUAL FILTER | 7 | 524 | 174 | 221 | 0 | 0 | 0 | 0 | 1 | 67 | 15 | 4 | 13 | 33 | 19 | 41 | 62 | 8 | 29 | 57 |
| STACK | 6 | 654 | 172 | 348 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 16 | 29 | 18 | 56 | 48 | 11 | 27 | 87 |
| BINAURAL | 6 | 532 | 172 | 230 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 15 | 30 | 17 | 35 | 74 | 7 | 26 | 67 |
| DUAL OSC HEAVY | 6 | 610 | 169 | 307 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 12 | 28 | 18 | 57 | 49 | 7 | 34 | 80 |
| SPREAD HEAVY | 8 | 600 | 212 | 248 | 118 | 22 | 78 | 16 | 2 | 69 | 15 | 4 | 13 | 38 | 22 | 64 | 66 | 10 | 37 | 58 |
| DUAL FLT HEAVY | 7 | 549 | 199 | 213 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 34 | 20 | 42 | 85 | 8 | 35 | 59 |
| STACK HEAVY | 6 | 615 | 192 | 287 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 14 | 29 | 17 | 57 | 67 | 12 | 32 | 80 |
| BINAURAL HEAVY | 6 | 582 | 206 | 233 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 14 | 29 | 18 | 36 | 106 | 8 | 38 | 73 |
| SINGLE LADDER | 8 | 446 | 160 | 156 | 30 | 6 | 92 | 16 | 0 | 66 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 8 | 33 | 40 |
| SINGLE MS20 | 8 | 422 | 154 | 138 | 30 | 6 | 72 | 16 | 0 | 68 | 15 | 4 | 12 | 38 | 22 | 46 | 26 | 8 | 31 | 36 |
| SINGLE SH-2 | 8 | 508 | 162 | 216 | 32 | 6 | 150 | 16 | 0 | 66 | 15 | 4 | 14 | 38 | 22 | 46 | 32 | 8 | 31 | 47 |
| SINGLE FORMANT | 8 | 425 | 154 | 144 | 30 | 6 | 80 | 16 | 0 | 66 | 15 | 4 | 12 | 38 | 22 | 46 | 26 | 8 | 30 | 37 |
| DUAL FLT LADDER | 7 | 524 | 174 | 221 | 0 | 0 | 0 | 0 | 1 | 67 | 15 | 4 | 13 | 33 | 19 | 41 | 62 | 8 | 29 | 57 |
| SEQ+ARP HOME | 8 | 536 | 206 | 164 | 32 | 24 | 80 | 16 | 2 | 71 | 16 | 13 | 13 | 38 | 32 | 46 | 66 | 10 | 51 | 46 |
| SEQ+ARP AMP | 8 | 537 | 206 | 164 | 32 | 24 | 80 | 16 | 2 | 71 | 16 | 13 | 13 | 38 | 32 | 48 | 66 | 10 | 52 | 46 |
| SEQ+ARP SEQ | 8 | 533 | 206 | 164 | 32 | 24 | 80 | 16 | 2 | 70 | 16 | 12 | 13 | 38 | 32 | 46 | 66 | 8 | 50 | 46 |
| SEQ+ARP MOD1 | 8 | 540 | 208 | 164 | 30 | 24 | 80 | 16 | 2 | 70 | 16 | 12 | 17 | 38 | 32 | 46 | 66 | 8 | 51 | 47 |
| LADDER FILTER PG | 8 | 445 | 160 | 156 | 30 | 6 | 92 | 16 | 0 | 66 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 8 | 32 | 40 |
| HEAVY MOD+PP | 8 | 525 | 194 | 160 | 30 | 22 | 80 | 16 | 2 | 102 | 15 | 4 | 13 | 38 | 22 | 46 | 66 | 8 | 35 | 44 |
| HEAVY CELEST+PP | 8 | 529 | 192 | 160 | 30 | 22 | 80 | 16 | 2 | 108 | 15 | 4 | 12 | 38 | 22 | 46 | 64 | 8 | 36 | 44 |
| SEQ+ARP CELEST+PP | 8 | 588 | 210 | 164 | 32 | 22 | 80 | 16 | 2 | 114 | 16 | 15 | 14 | 38 | 34 | 48 | 68 | 10 | 53 | 47 |
| SINGLE MULTI H | 8 | 509 | 200 | 172 | 30 | 22 | 92 | 16 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 46 | 72 | 8 | 36 | 46 |
| DUALOSC MULTI H | 6 | 627 | 175 | 316 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 12 | 29 | 17 | 58 | 55 | 7 | 36 | 82 |
| SPREAD MULTI H | 8 | 625 | 222 | 262 | 118 | 22 | 92 | 16 | 0 | 71 | 15 | 4 | 14 | 38 | 22 | 64 | 74 | 10 | 37 | 60 |
| DUALFLT MULTI H | 7 | 589 | 212 | 236 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 4 | 14 | 34 | 20 | 41 | 98 | 8 | 36 | 64 |
| STACK MULTI H | 6 | 650 | 204 | 306 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 15 | 29 | 18 | 57 | 79 | 12 | 34 | 85 |
| BINAUR MULTI H | 6 | 621 | 218 | 254 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 4 | 15 | 30 | 18 | 36 | 117 | 8 | 41 | 79 |
| SPREAD H CEL+PP | 8 | 669 | 224 | 262 | 120 | 22 | 92 | 16 | 0 | 111 | 15 | 5 | 14 | 38 | 22 | 66 | 74 | 8 | 38 | 61 |
| DUALOSC H CEL+PP | 6 | 674 | 178 | 317 | 0 | 0 | 0 | 0 | 1 | 110 | 16 | 5 | 12 | 29 | 18 | 59 | 56 | 7 | 35 | 83 |
| SPREAD H TUM+PP | 8 | 657 | 224 | 262 | 120 | 22 | 92 | 16 | 2 | 99 | 15 | 4 | 14 | 38 | 22 | 66 | 74 | 8 | 37 | 61 |
| SEQ+ARP SPRD CEL | 8 | 720 | 238 | 266 | 120 | 22 | 94 | 16 | 0 | 116 | 17 | 14 | 15 | 38 | 32 | 68 | 76 | 10 | 54 | 63 |
| HEAVY SPRING+PP | 8 | 512 | 192 | 160 | 30 | 22 | 80 | 16 | 0 | 91 | 15 | 4 | 12 | 38 | 22 | 46 | 66 | 8 | 38 | 44 |
| HEAVY CEL+BBD PP | 8 | 533 | 192 | 160 | 30 | 22 | 80 | 16 | 2 | 112 | 15 | 4 | 12 | 38 | 22 | 46 | 64 | 8 | 36 | 44 |
| SEQ+ARP SPR+BBD | 8 | 571 | 210 | 164 | 32 | 24 | 80 | 16 | 2 | 99 | 17 | 15 | 14 | 38 | 32 | 48 | 68 | 10 | 50 | 47 |
| STRIKE | 8 | 445 | 162 | 152 | 0 | 0 | 0 | 0 | 0 | 67 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 8 | 32 | 39 |
| STRIKE HEAVY | 8 | 486 | 192 | 156 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 46 | 64 | 8 | 37 | 44 |
| SEQ+ARP STRIKE | 8 | 616 | 218 | 180 | 0 | 0 | 0 | 0 | 2 | 118 | 16 | 16 | 14 | 38 | 34 | 48 | 68 | 16 | 52 | 50 |
| SEQ+ARP STRK FILT | 8 | 624 | 218 | 190 | 0 | 0 | 0 | 0 | 2 | 115 | 16 | 16 | 14 | 38 | 34 | 48 | 68 | 16 | 53 | 51 |
| SINGLE SH2 H | 8 | 575 | 202 | 232 | 30 | 22 | 152 | 16 | 2 | 68 | 15 | 5 | 15 | 38 | 22 | 48 | 72 | 8 | 36 | 54 |
| DUALOSC SH2 H | 6 | 673 | 176 | 360 | 0 | 0 | 0 | 0 | 1 | 68 | 16 | 5 | 12 | 29 | 18 | 59 | 54 | 7 | 35 | 90 |
| SPREAD SH2 H | 8 | 681 | 218 | 322 | 120 | 22 | 150 | 16 | 2 | 70 | 16 | 5 | 14 | 38 | 22 | 66 | 70 | 8 | 34 | 68 |
| DUALFLT SH2 H | 7 | 685 | 207 | 338 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 15 | 33 | 20 | 42 | 92 | 8 | 34 | 78 |
| STACK SH2 H | 6 | 744 | 203 | 394 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 20 | 30 | 18 | 59 | 76 | 11 | 36 | 100 |
| BINAUR SH2 H | 6 | 706 | 214 | 343 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 15 | 30 | 18 | 37 | 112 | 8 | 42 | 93 |
| STRIKE SH2 H | 8 | 614 | 208 | 260 | 0 | 0 | 0 | 0 | 0 | 71 | 15 | 6 | 15 | 38 | 24 | 48 | 72 | 14 | 39 | 58 |
| SPREAD SH2 CEL+PP | 8 | 726 | 220 | 322 | 120 | 22 | 152 | 16 | 0 | 111 | 15 | 5 | 14 | 38 | 24 | 66 | 70 | 8 | 39 | 68 |
| DUALFLT SH2 CEL | 7 | 732 | 208 | 340 | 0 | 0 | 0 | 0 | 1 | 111 | 15 | 5 | 14 | 33 | 20 | 42 | 94 | 8 | 38 | 78 |
| SEQ+ARP SH2 CEL | 8 | 675 | 220 | 236 | 32 | 24 | 152 | 16 | 0 | 116 | 16 | 15 | 16 | 38 | 34 | 50 | 74 | 8 | 56 | 57 |
| DUALFLT SH2 HP | 7 | 686 | 207 | 338 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 14 | 33 | 20 | 43 | 93 | 8 | 37 | 78 |
| BINAUR SH2 HP | 6 | 705 | 214 | 343 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 15 | 29 | 18 | 36 | 114 | 7 | 41 | 93 |
| SINGLE PCM | 8 | 488 | 180 | 176 | 50 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 13 | 38 | 22 | 64 | 32 | 8 | 31 | 44 |
| DUAL OSC PCM | 6 | 602 | 175 | 294 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 4 | 14 | 29 | 18 | 86 | 25 | 7 | 29 | 78 |
| SPREAD PCM | 8 | 742 | 198 | 412 | 284 | 6 | 94 | 16 | 2 | 69 | 15 | 5 | 13 | 38 | 22 | 82 | 32 | 8 | 28 | 76 |
| DUAL FILTER PCM | 7 | 601 | 195 | 270 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 15 | 33 | 20 | 59 | 64 | 8 | 30 | 67 |
| STACK PCM | 6 | 706 | 202 | 367 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 17 | 29 | 18 | 85 | 48 | 12 | 29 | 95 |
| BINAURAL PCM | 6 | 654 | 204 | 313 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 16 | 30 | 18 | 51 | 89 | 8 | 27 | 86 |
| DUALOSC MULTI H PCM | 6 | 601 | 206 | 253 | 0 | 0 | 0 | 0 | 1 | 72 | 15 | 5 | 14 | 29 | 18 | 87 | 55 | 7 | 35 | 77 |
| DUALFLT MULTI H PCM | 7 | 660 | 234 | 284 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 15 | 34 | 21 | 60 | 99 | 8 | 36 | 74 |
| BINAUR MULTI H PCM | 6 | 735 | 248 | 337 | 0 | 0 | 0 | 0 | 1 | 73 | 15 | 5 | 16 | 30 | 18 | 51 | 131 | 8 | 40 | 98 |
| SINGLE RS | 8 | 446 | 160 | 156 | 30 | 6 | 92 | 16 | 0 | 66 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 8 | 33 | 40 |
| SINGLE RS PCM | 8 | 522 | 180 | 208 | 82 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 14 | 38 | 22 | 64 | 32 | 8 | 32 | 48 |
| DUALOSC RS | 6 | 630 | 147 | 354 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 13 | 29 | 17 | 58 | 26 | 7 | 27 | 84 |
| DUALOSC RS PCM | 6 | 602 | 174 | 294 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 4 | 14 | 29 | 18 | 86 | 25 | 7 | 29 | 78 |
| SEQ+ARP HOME PCM | 8 | 625 | 228 | 220 | 88 | 22 | 80 | 16 | 2 | 75 | 16 | 16 | 15 | 38 | 34 | 68 | 66 | 10 | 53 | 56 |
| SINGLE DRV SOFT | 8 | 484 | 166 | 186 | 30 | 6 | 92 | 46 | 0 | 68 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 14 | 33 | 44 |
| SINGLE DRV HARD | 8 | 487 | 166 | 190 | 30 | 6 | 92 | 48 | 0 | 68 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 14 | 32 | 44 |
| SINGLE DRV FOLD | 8 | 487 | 166 | 192 | 30 | 6 | 92 | 52 | 0 | 66 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 14 | 32 | 45 |
| SINGLE DRV TAPE | 8 | 495 | 166 | 198 | 30 | 6 | 92 | 56 | 0 | 68 | 15 | 4 | 12 | 38 | 22 | 46 | 32 | 14 | 32 | 46 |
| SINGLE DRV TUBE | 8 | 492 | 166 | 194 | 30 | 6 | 92 | 54 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 32 | 14 | 32 | 45 |
| SINGLE DRV OD | 8 | 494 | 166 | 196 | 30 | 6 | 92 | 56 | 0 | 68 | 15 | 4 | 13 | 38 | 22 | 46 | 34 | 14 | 32 | 45 |
| STACK HEAVY TUBE | 6 | 680 | 197 | 345 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 15 | 28 | 18 | 58 | 67 | 17 | 33 | 90 |
| BINAUR HEAVY OD | 6 | 649 | 210 | 294 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 14 | 29 | 18 | 36 | 105 | 13 | 39 | 84 |
| STRIKE HEAVY TUBE | 8 | 563 | 208 | 214 | 0 | 0 | 0 | 0 | 0 | 70 | 16 | 5 | 13 | 38 | 22 | 48 | 64 | 22 | 37 | 53 |
| SEQ+ARP HOME TUBE | 8 | 592 | 218 | 202 | 32 | 22 | 80 | 56 | 2 | 73 | 17 | 15 | 14 | 38 | 34 | 48 | 68 | 16 | 51 | 53 |
| FULL | 4 | 534 | 118 | 290 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 13 | 19 | 11 | 39 | 37 | 4 | 26 | 102 |
| FULL HEAVY | 4 | 510 | 128 | 251 | 0 | 0 | 0 | 0 | 0 | 67 | 15 | 4 | 12 | 19 | 11 | 39 | 48 | 4 | 33 | 95 |
| FULL MULTI H | 4 | 533 | 137 | 264 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 12 | 19 | 11 | 39 | 57 | 4 | 32 | 100 |
| FULL SH2 H | 4 | 615 | 147 | 331 | 0 | 0 | 0 | 0 | 0 | 69 | 16 | 6 | 13 | 20 | 12 | 46 | 56 | 6 | 33 | 120 |
| FULL H CEL+PP | 4 | 587 | 142 | 265 | 0 | 0 | 0 | 0 | 1 | 112 | 16 | 5 | 13 | 20 | 12 | 41 | 59 | 5 | 33 | 102 |
| FULL MULTI H PCM | 4 | 480 | 158 | 186 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 4 | 14 | 20 | 12 | 58 | 57 | 4 | 33 | 86 |
| FULL SH2 H PCM | 4 | 584 | 160 | 281 | 0 | 0 | 0 | 0 | 0 | 72 | 16 | 5 | 16 | 20 | 12 | 61 | 55 | 5 | 34 | 110 |
| FULL SH2 CEL+PP | 4 | 662 | 149 | 332 | 0 | 0 | 0 | 0 | 0 | 111 | 17 | 6 | 13 | 19 | 12 | 47 | 57 | 6 | 34 | 120 |
| DSPRD | 4 | 423 | 114 | 183 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 12 | 19 | 11 | 55 | 18 | 4 | 27 | 74 |
| DSPRD HEAVY | 4 | 445 | 129 | 185 | 0 | 0 | 0 | 0 | 0 | 67 | 15 | 4 | 12 | 19 | 11 | 55 | 33 | 4 | 33 | 78 |
| DSPRD MULTI H | 4 | 459 | 135 | 192 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 13 | 19 | 11 | 55 | 38 | 4 | 32 | 82 |
| DSPRD SH2 H | 4 | 488 | 134 | 220 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 13 | 19 | 11 | 56 | 37 | 4 | 33 | 89 |
| DSPRD H CEL+PP | 4 | 503 | 136 | 192 | 0 | 0 | 0 | 0 | 1 | 109 | 15 | 4 | 13 | 19 | 11 | 56 | 38 | 4 | 33 | 82 |
| DSPRD MULTI H PCM | 4 | 650 | 156 | 360 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 12 | 19 | 12 | 76 | 37 | 5 | 32 | 129 |
| DSPRD SH2 CEL+PP | 4 | 532 | 135 | 221 | 0 | 0 | 0 | 0 | 1 | 109 | 15 | 5 | 13 | 19 | 11 | 56 | 38 | 4 | 33 | 89 |
| SEQ+ARP DSPRD CEL | 4 | 553 | 149 | 194 | 0 | 0 | 0 | 0 | 1 | 116 | 16 | 15 | 14 | 19 | 18 | 59 | 40 | 5 | 48 | 86 |
| IDLE PHASER | 0 | 137 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 85 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE PHASER x2 | 0 | 155 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 102 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD PHASER | 8 | 590 | 182 | 246 | 118 | 6 | 94 | 16 | 2 | 94 | 15 | 5 | 15 | 38 | 24 | 66 | 34 | 8 | 31 | 54 |
| SPREAD H CEL PHSR | 8 | 700 | 226 | 262 | 120 | 22 | 92 | 16 | 2 | 138 | 15 | 5 | 15 | 38 | 22 | 66 | 74 | 8 | 37 | 61 |
| SEQ+ARP HOME PHSR2 | 8 | 594 | 210 | 164 | 30 | 22 | 80 | 16 | 2 | 117 | 17 | 16 | 15 | 38 | 34 | 48 | 68 | 10 | 53 | 47 |
| IDLE ENSEMBLE | 0 | 139 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 87 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE ENSEMBLE x2 | 0 | 160 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 108 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE ENS+PHSR | 0 | 144 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 91 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENS+PHSR x2 | 0 | 169 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 3 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS | 0 | 138 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE CHORUS x2 | 0 | 158 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 105 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL ENPH | 8 | 714 | 228 | 264 | 120 | 22 | 92 | 16 | 2 | 148 | 16 | 5 | 14 | 38 | 22 | 68 | 76 | 10 | 37 | 62 |
| SEQ+ARP HOME ENPH2 | 8 | 616 | 212 | 164 | 32 | 24 | 80 | 16 | 2 | 135 | 17 | 17 | 15 | 38 | 34 | 48 | 68 | 10 | 54 | 47 |
| SPREAD H CEL MIX | 8 | 687 | 224 | 264 | 120 | 22 | 92 | 16 | 0 | 127 | 16 | 4 | 14 | 38 | 22 | 66 | 74 | 8 | 38 | 61 |
| SEQ+ARP CEL MIX | 8 | 609 | 210 | 164 | 30 | 22 | 80 | 16 | 2 | 133 | 17 | 15 | 14 | 38 | 34 | 50 | 66 | 10 | 54 | 47 |
| HEAVY SPRING MIX | 8 | 530 | 194 | 160 | 30 | 22 | 80 | 16 | 0 | 107 | 16 | 4 | 12 | 38 | 22 | 46 | 66 | 8 | 37 | 44 |
| IDLE TAPE FLANGER | 0 | 146 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 93 | 15 | 4 | 13 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE TAPE FLNG x2 | 0 | 171 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 4 | 13 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL TFLG | 8 | 714 | 226 | 266 | 122 | 22 | 92 | 16 | 0 | 149 | 15 | 4 | 16 | 38 | 22 | 66 | 76 | 10 | 38 | 62 |
| SEQ+ARP HOME TFLG2 | 8 | 614 | 212 | 164 | 30 | 22 | 82 | 16 | 2 | 135 | 17 | 16 | 16 | 38 | 34 | 48 | 68 | 10 | 52 | 47 |
| IDLE BBD FLANGER | 0 | 140 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 87 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE BBD FLNG x2 | 0 | 159 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 106 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL BBDF | 8 | 708 | 228 | 262 | 120 | 22 | 92 | 16 | 2 | 144 | 15 | 5 | 14 | 38 | 24 | 68 | 74 | 8 | 38 | 62 |
| IDLE PITCH SHIFT | 0 | 145 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 93 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| PITCH SHIFT HELD | 8 | 481 | 162 | 158 | 30 | 6 | 92 | 16 | 0 | 97 | 15 | 4 | 14 | 38 | 22 | 46 | 34 | 8 | 31 | 40 |
| IDLE PITCH SHFT x2 | 0 | 172 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 120 | 15 | 4 | 12 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| SPREAD H CEL PSHF | 8 | 706 | 226 | 264 | 120 | 22 | 92 | 16 | 0 | 144 | 15 | 5 | 15 | 38 | 24 | 68 | 74 | 8 | 37 | 61 |

### 2026-11-02 19:29, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.5 | 16.0 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 24.3 | 25.1 | 0 | 1 | 60.6 | 4 | pass |
| SINGLE | 45.0 | 46.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 49.3 | 50.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 67.8 | 68.8 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD | 56.3 | 57.3 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 59.5 | 60.6 | 0 | 7 | 55.2 | 8 | pass |
| STACK | 67.2 | 68.5 | 0 | 6 | 46.6 | 9 | pass |
| BINAURAL | 55.8 | 57.0 | 0 | 6 | 57.2 | 7 | pass |
| DUAL OSC HEAVY | 65.8 | 66.8 | 0 | 6 | 50.2 | 8 | pass |
| SPREAD HEAVY | 60.5 | 61.5 | 0 | 8 | 54.2 | 8 | pass |
| DUAL FLT HEAVY | 61.4 | 62.4 | 0 | 7 | 54.2 | 8 | pass |
| STACK HEAVY | 63.5 | 64.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.8 | 61.8 | 0 | 6 | 54.6 | 8 | pass |
| SINGLE LADDER | 45.0 | 46.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 42.7 | 43.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 50.7 | 51.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 42.9 | 43.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.6 | 60.6 | 0 | 7 | 55.9 | 8 | pass |
| SEQ+ARP HOME | 57.5 | 61.5 | 0 | 8 | 56.9 | 9 | pass |
| SEQ+ARP AMP | 57.6 | 61.1 | 0 | 8 | 57.2 | 11 | pass |
| SEQ+ARP SEQ | 56.9 | 60.6 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 57.3 | 62.4 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 45.0 | 45.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 53.6 | 54.6 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.0 | 55.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 62.0 | 65.9 | 0 | 8 | 53.2 | 10 | pass |
| SINGLE MULTI H | 51.8 | 52.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 67.5 | 68.5 | 0 | 6 | 44.5 | 9 | pass |
| SPREAD MULTI H | 63.0 | 64.1 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 66.8 | 67.8 | 0 | 7 | 50.2 | 9 | pass |
| STACK MULTI H | 67.2 | 68.5 | 0 | 6 | 47.2 | 9 | pass |
| BINAUR MULTI H | 64.8 | 65.9 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 67.9 | 69.3 | 0 | 8 | 46.9 | 9 | pass |
| DUALOSC H CEL+PP | 72.4 | 74.2 | 0 | 6 | 43.2 | 1 | FAIL |
| SPREAD H TUM+PP | 66.6 | 67.7 | 0 | 8 | 51.6 | 9 | pass |
| SEQ+ARP SPRD CEL | 75.8 | 80.2 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 51.6 | 52.6 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 54.1 | 55.3 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP SPR+BBD | 60.3 | 63.9 | 0 | 8 | 55.5 | 9 | pass |
| STRIKE | 46.1 | 47.0 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 50.5 | 51.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 66.8 | 71.4 | 0 | 8 | 49.5 | 10 | pass |
| SEQ+ARP STRK FILT | 67.7 | 72.3 | 0 | 8 | 45.6 | 10 | pass |
| SINGLE SH2 H | 57.5 | 58.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 71.3 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD SH2 H | 68.2 | 69.3 | 0 | 8 | 45.9 | 9 | pass |
| DUALFLT SH2 H | 76.4 | 77.8 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 76.2 | 77.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 72.8 | 74.1 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 58.7 | 59.7 | 0 | 8 | 57.3 | 8 | pass |
| SPREAD SH2 CEL+PP | 72.9 | 74.4 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.6 | 82.1 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 69.9 | 74.0 | 0 | 8 | 44.2 | 10 | pass |
| DUALFLT SH2 HP | 76.4 | 78.0 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 72.8 | 74.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 48.7 | 49.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 56.3 | 57.3 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 61.3 | 62.4 | 0 | 8 | 54.5 | 8 | pass |
| DUAL FILTER PCM | 62.9 | 63.9 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 66.9 | 68.1 | 0 | 6 | 48.6 | 9 | pass |
| BINAURAL PCM | 60.8 | 61.8 | 0 | 6 | 55.9 | 8 | pass |
| DUALOSC MULTI H PCM | 55.4 | 56.4 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 70.0 | 71.5 | 0 | 7 | 43.2 | 1 | pass |
| BINAUR MULTI H PCM | 69.7 | 70.9 | 0 | 6 | 42.9 | 1 | pass |
| SINGLE RS | 45.1 | 46.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 52.3 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 68.0 | 69.8 | 0 | 6 | 44.2 | 9 | pass |
| DUALOSC RS PCM | 60.2 | 61.6 | 0 | 6 | 54.2 | 8 | pass |
| SEQ+ARP HOME PCM | 65.4 | 69.4 | 0 | 8 | 50.5 | 10 | pass |
| SINGLE DRV SOFT | 48.9 | 49.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 49.1 | 50.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 49.0 | 49.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TAPE | 49.9 | 50.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 49.6 | 50.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 49.8 | 50.6 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 70.1 | 71.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.1 | 68.3 | 0 | 6 | 45.6 | 9 | pass |
| STRIKE HEAVY TUBE | 54.9 | 55.9 | 0 | 8 | 57.3 | 1 | pass |
| SEQ+ARP HOME TUBE | 63.0 | 66.9 | 0 | 8 | 51.9 | 10 | pass |
| FULL | 58.8 | 59.8 | 0 | 4 | 56.9 | 8 | pass |
| FULL HEAVY | 56.8 | 57.7 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H | 59.2 | 60.2 | 0 | 4 | 56.6 | 8 | pass |
| FULL SH2 H | 64.1 | 65.2 | 0 | 4 | 51.9 | 8 | pass |
| FULL H CEL+PP | 64.3 | 65.6 | 0 | 4 | 52.2 | 8 | pass |
| FULL MULTI H PCM | 51.4 | 52.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 57.3 | 58.4 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 CEL+PP | 69.1 | 70.7 | 0 | 4 | 42.9 | 1 | pass |
| DSPRD | 45.9 | 47.0 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 48.6 | 49.7 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 50.4 | 51.4 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 H | 52.7 | 53.8 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD H CEL+PP | 55.1 | 56.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 55.6 | 56.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 CEL+PP | 57.9 | 59.0 | 0 | 4 | 57.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 63.3 | 66.8 | 0 | 4 | 52.5 | 10 | pass |
| IDLE PHASER | 18.4 | 19.1 | 0 | 0 | 61.3 | 1 | pass |
| IDLE PHASER x2 | 19.8 | 20.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 61.0 | 62.2 | 0 | 8 | 55.9 | 8 | pass |
| SPREAD H CEL PHSR | 71.9 | 73.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 63.9 | 67.7 | 0 | 8 | 51.9 | 10 | pass |
| IDLE ENSEMBLE | 18.5 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 20.1 | 20.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 19.1 | 19.7 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 21.0 | 21.6 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 18.4 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 19.9 | 20.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 74.3 | 76.0 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 66.2 | 70.6 | 0 | 8 | 49.2 | 10 | pass |
| SPREAD H CEL MIX | 69.7 | 71.2 | 0 | 8 | 42.9 | 9 | pass |
| SEQ+ARP CEL MIX | 64.1 | 68.1 | 0 | 8 | 51.6 | 10 | pass |
| HEAVY SPRING MIX | 54.1 | 55.1 | 0 | 8 | 57.6 | 1 | pass |
| IDLE TAPE FLANGER | 19.7 | 20.3 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.6 | 22.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 74.3 | 75.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 65.9 | 69.5 | 0 | 8 | 49.9 | 10 | pass |
| IDLE BBD FLANGER | 18.6 | 19.3 | 0 | 0 | 61.6 | 1 | pass |
| IDLE BBD FLNG x2 | 20.1 | 20.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 73.4 | 74.9 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHIFT | 19.0 | 19.7 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 49.4 | 51.0 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.2 | 22.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 72.6 | 74.6 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-02 19:18, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.3 | 15.8 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 24.6 | 25.5 | 0 | 1 | 60.9 | 4 | pass |
| SINGLE | 46.0 | 47.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 50.2 | 51.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 69.1 | 70.2 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD | 58.5 | 59.4 | 0 | 8 | 57.2 | 8 | pass |
| DUAL FILTER | 60.3 | 61.4 | 0 | 7 | 54.9 | 8 | pass |
| STACK | 68.3 | 69.4 | 0 | 6 | 45.2 | 9 | pass |
| BINAURAL | 56.9 | 58.1 | 0 | 6 | 56.9 | 7 | pass |
| DUAL OSC HEAVY | 66.8 | 67.9 | 0 | 6 | 50.2 | 9 | pass |
| SPREAD HEAVY | 62.6 | 63.6 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 62.3 | 63.5 | 0 | 7 | 52.9 | 8 | pass |
| STACK HEAVY | 64.1 | 65.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 61.7 | 62.9 | 0 | 6 | 53.2 | 8 | pass |
| SINGLE LADDER | 46.0 | 47.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 43.7 | 44.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 51.6 | 52.5 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 43.8 | 44.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.3 | 61.5 | 0 | 7 | 54.5 | 8 | pass |
| SEQ+ARP HOME | 58.7 | 62.3 | 0 | 8 | 56.9 | 9 | pass |
| SEQ+ARP AMP | 58.8 | 62.1 | 0 | 8 | 56.5 | 11 | pass |
| SEQ+ARP SEQ | 58.1 | 62.4 | 0 | 8 | 16.5 | 116 | pass |
| SEQ+ARP MOD1 | 58.7 | 63.6 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 45.9 | 46.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 54.2 | 55.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.7 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 63.2 | 67.2 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE MULTI H | 52.8 | 53.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 68.3 | 69.3 | 0 | 6 | 45.5 | 9 | pass |
| SPREAD MULTI H | 65.2 | 66.1 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 67.3 | 68.5 | 0 | 7 | 46.2 | 9 | pass |
| STACK MULTI H | 68.4 | 69.8 | 0 | 6 | 44.9 | 9 | pass |
| BINAUR MULTI H | 65.9 | 66.9 | 0 | 6 | 50.6 | 8 | pass |
| SPREAD H CEL+PP | 69.2 | 70.8 | 0 | 8 | 43.2 | 1 | pass |
| DUALOSC H CEL+PP | 72.9 | 74.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H TUM+PP | 68.2 | 69.5 | 0 | 8 | 45.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 77.3 | 81.5 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 52.4 | 53.5 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 54.7 | 55.8 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 61.4 | 65.4 | 0 | 8 | 54.2 | 10 | pass |
| STRIKE | 47.3 | 48.3 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 51.6 | 52.6 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 67.9 | 73.0 | 0 | 8 | 46.9 | 10 | pass |
| SEQ+ARP STRK FILT | 69.0 | 73.7 | 0 | 8 | 44.2 | 10 | pass |
| SINGLE SH2 H | 58.3 | 59.3 | 0 | 8 | 57.6 | 8 | pass |
| DUALOSC SH2 H | 71.8 | 73.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD SH2 H | 70.1 | 71.4 | 0 | 8 | 43.2 | 9 | FAIL |
| DUALFLT SH2 H | 78.2 | 79.8 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 76.4 | 77.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 74.8 | 76.5 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.5 | 60.7 | 0 | 8 | 56.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 74.5 | 76.0 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 82.1 | 83.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 70.9 | 75.9 | 0 | 8 | 43.2 | 10 | FAIL |
| DUALFLT SH2 HP | 78.2 | 79.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 74.8 | 76.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 49.1 | 50.2 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 56.7 | 57.9 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 61.5 | 62.6 | 0 | 8 | 52.9 | 8 | pass |
| DUAL FILTER PCM | 62.9 | 63.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 65.8 | 67.0 | 0 | 6 | 50.5 | 8 | pass |
| BINAURAL PCM | 60.2 | 61.2 | 0 | 6 | 55.6 | 8 | pass |
| DUALOSC MULTI H PCM | 55.4 | 56.5 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 70.0 | 71.3 | 0 | 7 | 42.9 | 1 | pass |
| BINAUR MULTI H PCM | 69.1 | 70.4 | 0 | 6 | 43.9 | 9 | pass |
| SINGLE RS | 46.1 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 52.5 | 54.2 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 69.1 | 70.6 | 0 | 6 | 43.5 | 9 | pass |
| DUALOSC RS PCM | 60.3 | 61.9 | 0 | 6 | 54.6 | 8 | pass |
| SEQ+ARP HOME PCM | 65.1 | 69.2 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 49.8 | 50.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 50.1 | 51.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 49.9 | 50.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 50.8 | 51.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 50.6 | 51.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 50.8 | 51.7 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 70.8 | 72.4 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR HEAVY OD | 68.2 | 69.3 | 0 | 6 | 46.2 | 9 | pass |
| STRIKE HEAVY TUBE | 55.9 | 56.8 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 64.0 | 68.5 | 0 | 8 | 51.9 | 10 | pass |
| FULL | 59.5 | 60.6 | 0 | 4 | 56.2 | 8 | pass |
| FULL HEAVY | 57.3 | 58.3 | 0 | 4 | 57.6 | 7 | pass |
| FULL MULTI H | 59.7 | 60.8 | 0 | 4 | 56.9 | 8 | pass |
| FULL SH2 H | 64.6 | 65.7 | 0 | 4 | 51.9 | 8 | pass |
| FULL H CEL+PP | 64.3 | 65.6 | 0 | 4 | 52.2 | 8 | pass |
| FULL MULTI H PCM | 51.6 | 52.7 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 56.9 | 58.1 | 0 | 4 | 57.5 | 7 | pass |
| FULL SH2 CEL+PP | 69.7 | 71.4 | 0 | 4 | 42.9 | 1 | pass |
| DSPRD | 48.3 | 49.2 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.7 | 50.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 51.7 | 52.9 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 53.5 | 54.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 56.1 | 57.6 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD MULTI H PCM | 55.6 | 56.7 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 58.2 | 59.4 | 0 | 4 | 57.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 64.3 | 67.9 | 0 | 4 | 52.2 | 10 | pass |
| IDLE PHASER | 18.3 | 18.8 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 19.7 | 20.5 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 62.4 | 63.5 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 73.6 | 75.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 64.9 | 69.2 | 0 | 8 | 51.9 | 10 | pass |
| IDLE ENSEMBLE | 18.3 | 18.9 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 20.0 | 20.5 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 18.9 | 19.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 20.8 | 21.4 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS | 18.1 | 18.7 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS x2 | 19.7 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 75.4 | 77.0 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 66.9 | 70.9 | 0 | 8 | 48.3 | 10 | pass |
| SPREAD H CEL MIX | 71.3 | 72.9 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 65.3 | 69.7 | 0 | 8 | 51.9 | 10 | pass |
| HEAVY SPRING MIX | 55.3 | 56.4 | 0 | 8 | 57.2 | 1 | pass |
| IDLE TAPE FLANGER | 19.3 | 19.9 | 0 | 0 | 61.3 | 1 | pass |
| IDLE TAPE FLNG x2 | 21.2 | 21.8 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 76.0 | 77.7 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 66.8 | 70.7 | 0 | 8 | 48.6 | 10 | pass |
| IDLE BBD FLANGER | 18.4 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.9 | 20.7 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 75.0 | 76.5 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE PITCH SHIFT | 18.7 | 19.3 | 0 | 0 | 61.3 | 1 | pass |
| PITCH SHIFT HELD | 50.1 | 51.7 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.0 | 21.9 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL PSHF | 74.0 | 75.9 | 0 | 8 | 42.9 | 1 | FAIL |

### 2026-11-02 19:06, CARNAC OS PROF (1 core), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.2 | 21.7 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 34.4 | 35.4 | 0 | 1 | 57.3 | 1 | pass |
| SINGLE | 101.8 | 103.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE HEAVY | 108.9 | 110.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL OSC | 126.5 | 127.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD | 157.9 | 159.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 113.8 | 115.4 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK | 133.9 | 135.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL | 126.3 | 127.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUAL OSC HEAVY | 122.9 | 124.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 165.6 | 166.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 118.6 | 120.3 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 127.2 | 128.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 134.7 | 136.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 102.0 | 103.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MS20 | 95.3 | 96.8 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE SH-2 | 111.1 | 112.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE FORMANT | 96.4 | 97.9 | 0 | 8 | 42.9 | 12 | FAIL |
| DUAL FLT LADDER | 113.6 | 114.4 | 8 | 7 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME | 118.8 | 121.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 118.9 | 122.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 118.0 | 120.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 119.5 | 123.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 101.7 | 104.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY MOD+PP | 115.8 | 116.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY CELEST+PP | 116.7 | 118.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP CELEST+PP | 126.9 | 129.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MULTI H | 113.4 | 115.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC MULTI H | 125.9 | 127.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD MULTI H | 169.3 | 171.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H | 124.3 | 125.9 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK MULTI H | 133.2 | 134.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H | 140.8 | 142.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL+PP | 176.1 | 177.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC H CEL+PP | 133.3 | 134.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H TUM+PP | 173.7 | 175.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPRD CEL | 187.0 | 190.3 | 7 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 113.3 | 114.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY CEL+BBD PP | 118.4 | 119.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPR+BBD | 124.9 | 127.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STRIKE | 107.0 | 109.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STRIKE HEAVY | 114.3 | 115.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRIKE | 132.1 | 135.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRK FILT | 133.8 | 136.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE SH2 H | 122.9 | 125.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC SH2 H | 133.5 | 135.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 H | 178.6 | 180.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 H | 143.8 | 145.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK SH2 H | 147.9 | 149.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 H | 155.4 | 156.7 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE SH2 H | 130.2 | 131.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 CEL+PP | 186.2 | 188.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 CEL | 151.3 | 152.6 | 8 | 7 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SH2 CEL | 140.6 | 143.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 HP | 143.8 | 145.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 HP | 155.7 | 157.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE PCM | 90.3 | 91.9 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC PCM | 110.8 | 111.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD PCM | 137.1 | 139.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER PCM | 110.3 | 112.1 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK PCM | 129.1 | 130.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL PCM | 121.3 | 122.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC MULTI H PCM | 110.6 | 111.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H PCM | 121.0 | 122.9 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H PCM | 135.8 | 137.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE RS | 101.9 | 103.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE RS PCM | 96.3 | 99.5 | 0 | 8 | 42.5 | 12 | FAIL |
| DUALOSC RS | 126.2 | 127.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC RS PCM | 111.1 | 112.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PCM | 115.4 | 118.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV SOFT | 109.1 | 110.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV HARD | 109.6 | 111.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV FOLD | 109.6 | 111.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV TAPE | 110.7 | 112.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV TUBE | 110.2 | 111.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV OD | 110.7 | 112.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STACK HEAVY TUBE | 139.6 | 141.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR HEAVY OD | 148.0 | 149.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE HEAVY TUBE | 123.5 | 124.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TUBE | 128.2 | 131.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| FULL | 106.0 | 107.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL HEAVY | 102.4 | 103.9 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H | 106.0 | 107.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 H | 117.1 | 118.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL H CEL+PP | 114.1 | 115.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H PCM | 88.6 | 90.2 | 0 | 4 | 42.9 | 1 | FAIL |
| FULL SH2 H PCM | 107.0 | 108.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 CEL+PP | 124.7 | 125.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD | 133.6 | 135.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD HEAVY | 112.1 | 113.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD MULTI H | 114.1 | 115.4 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD SH2 H | 118.8 | 119.8 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD H CEL+PP | 121.3 | 122.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD MULTI H PCM | 120.2 | 121.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD SH2 CEL+PP | 126.4 | 127.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SEQ+ARP DSPRD CEL | 130.7 | 133.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| IDLE PHASER | 25.2 | 25.8 | 0 | 0 | 60.2 | 1 | pass |
| IDLE PHASER x2 | 28.5 | 29.0 | 0 | 0 | 58.6 | 4 | pass |
| SPREAD PHASER | 163.3 | 166.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL PHSR | 182.7 | 184.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PHSR2 | 128.5 | 131.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE ENSEMBLE | 25.8 | 26.4 | 0 | 0 | 59.9 | 4 | pass |
| IDLE ENSEMBLE x2 | 29.7 | 30.2 | 0 | 0 | 58.6 | 4 | pass |
| IDLE ENS+PHSR | 26.8 | 27.4 | 0 | 0 | 59.9 | 1 | pass |
| IDLE ENS+PHSR x2 | 31.5 | 32.1 | 0 | 0 | 58.3 | 4 | pass |
| IDLE CHORUS | 26.0 | 26.6 | 0 | 0 | 60.3 | 4 | pass |
| IDLE CHORUS x2 | 29.5 | 30.1 | 0 | 0 | 58.3 | 4 | pass |
| SPREAD H CEL ENPH | 185.4 | 188.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME ENPH2 | 132.5 | 135.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL MIX | 179.6 | 181.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP CEL MIX | 130.3 | 133.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING MIX | 116.8 | 118.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE TAPE FLANGER | 27.2 | 27.8 | 0 | 0 | 60.0 | 4 | pass |
| IDLE TAPE FLNG x2 | 31.7 | 32.3 | 0 | 0 | 58.3 | 4 | pass |
| SPREAD H CEL TFLG | 185.2 | 186.3 | 7 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TFLG2 | 132.3 | 135.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE BBD FLANGER | 25.6 | 26.3 | 0 | 0 | 60.3 | 4 | pass |
| IDLE BBD FLNG x2 | 29.2 | 29.9 | 0 | 0 | 59.6 | 4 | pass |
| SPREAD H CEL BBDF | 182.8 | 185.5 | 7 | 8 | – | – | FAIL (cut by the guard) |
| IDLE PITCH SHIFT | 26.9 | 27.8 | 0 | 0 | 60.0 | 4 | pass |
| PITCH SHIFT HELD | 108.8 | 110.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE PITCH SHFT x2 | 31.9 | 33.1 | 0 | 0 | 57.6 | 1 | pass |
| SPREAD H CEL PSHF | 183.7 | 185.5 | 8 | 8 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice per 16-sample block (the voice stages, CTRL and its c.* split); 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 112 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 63 | 15 | 3 | 9 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SINGLE 1V | 1 | 184 | 27 | 33 | 16 | 0 | 12 | 2 | 0 | 69 | 15 | 4 | 10 | 5 | 3 | 10 | 5 | 1 | 26 | 60 |
| SINGLE | 8 | 550 | 172 | 246 | 118 | 6 | 94 | 16 | 0 | 69 | 16 | 4 | 11 | 36 | 24 | 56 | 34 | 10 | 32 | 52 |
| SINGLE HEAVY | 8 | 589 | 202 | 248 | 118 | 22 | 78 | 16 | 2 | 69 | 16 | 4 | 12 | 38 | 22 | 54 | 66 | 10 | 36 | 56 |
| DUAL OSC | 6 | 685 | 159 | 397 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 11 | 28 | 18 | 71 | 26 | 7 | 29 | 93 |
| SPREAD | 8 | 856 | 190 | 532 | 404 | 6 | 94 | 16 | 0 | 70 | 16 | 5 | 12 | 38 | 24 | 74 | 34 | 8 | 31 | 90 |
| DUAL FILTER | 7 | 616 | 185 | 300 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 11 | 34 | 20 | 48 | 64 | 8 | 29 | 69 |
| STACK | 6 | 725 | 185 | 409 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 4 | 11 | 29 | 18 | 69 | 48 | 12 | 29 | 99 |
| BINAURAL | 6 | 684 | 186 | 364 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 12 | 29 | 18 | 43 | 80 | 8 | 30 | 92 |
| DUAL OSC HEAVY | 6 | 665 | 177 | 352 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 4 | 11 | 28 | 17 | 64 | 51 | 7 | 35 | 88 |
| SPREAD HEAVY | 8 | 898 | 220 | 534 | 404 | 22 | 80 | 16 | 0 | 70 | 17 | 5 | 12 | 38 | 22 | 72 | 66 | 8 | 40 | 94 |
| DUAL FLT HEAVY | 7 | 642 | 209 | 295 | 0 | 0 | 0 | 0 | 1 | 68 | 16 | 5 | 11 | 33 | 20 | 49 | 88 | 8 | 37 | 72 |
| STACK HEAVY | 6 | 689 | 198 | 350 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 13 | 28 | 17 | 63 | 69 | 12 | 35 | 92 |
| BINAURAL HEAVY | 6 | 730 | 218 | 367 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 12 | 28 | 17 | 43 | 112 | 8 | 40 | 98 |
| SINGLE LADDER | 8 | 551 | 172 | 246 | 120 | 6 | 92 | 16 | 0 | 70 | 16 | 4 | 11 | 38 | 24 | 54 | 32 | 8 | 32 | 52 |
| SINGLE MS20 | 8 | 515 | 160 | 224 | 118 | 6 | 72 | 16 | 0 | 69 | 15 | 4 | 10 | 38 | 22 | 54 | 26 | 8 | 33 | 48 |
| SINGLE SH-2 | 8 | 601 | 168 | 302 | 118 | 6 | 150 | 16 | 0 | 68 | 15 | 4 | 11 | 38 | 22 | 54 | 30 | 8 | 33 | 59 |
| SINGLE FORMANT | 8 | 521 | 160 | 232 | 118 | 6 | 80 | 16 | 0 | 68 | 16 | 4 | 10 | 38 | 22 | 54 | 26 | 8 | 31 | 49 |
| DUAL FLT LADDER | 7 | 614 | 185 | 300 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 10 | 34 | 21 | 49 | 63 | 9 | 28 | 69 |
| SEQ+ARP HOME | 8 | 642 | 216 | 254 | 122 | 22 | 80 | 16 | 2 | 73 | 17 | 15 | 13 | 38 | 34 | 56 | 68 | 8 | 52 | 59 |
| SEQ+ARP AMP | 8 | 643 | 216 | 254 | 120 | 22 | 82 | 16 | 2 | 73 | 16 | 16 | 13 | 38 | 34 | 56 | 66 | 8 | 53 | 59 |
| SEQ+ARP SEQ | 8 | 638 | 216 | 252 | 120 | 24 | 80 | 16 | 0 | 72 | 17 | 15 | 13 | 38 | 32 | 56 | 66 | 10 | 53 | 58 |
| SEQ+ARP MOD1 | 8 | 646 | 218 | 252 | 120 | 22 | 80 | 16 | 0 | 73 | 17 | 15 | 17 | 38 | 32 | 56 | 66 | 10 | 54 | 59 |
| LADDER FILTER PG | 8 | 550 | 170 | 248 | 120 | 6 | 94 | 16 | 0 | 69 | 16 | 4 | 11 | 38 | 22 | 56 | 32 | 8 | 32 | 52 |
| HEAVY MOD+PP | 8 | 627 | 204 | 248 | 118 | 22 | 80 | 16 | 0 | 104 | 16 | 5 | 12 | 38 | 24 | 54 | 64 | 10 | 38 | 56 |
| HEAVY CELEST+PP | 8 | 632 | 204 | 248 | 118 | 22 | 78 | 16 | 0 | 109 | 16 | 5 | 12 | 36 | 22 | 54 | 66 | 8 | 38 | 56 |
| SEQ+ARP CELEST+PP | 8 | 686 | 218 | 252 | 120 | 22 | 80 | 16 | 2 | 115 | 17 | 16 | 13 | 38 | 34 | 56 | 68 | 8 | 53 | 59 |
| SINGLE MULTI H | 8 | 613 | 212 | 262 | 120 | 22 | 92 | 16 | 0 | 71 | 15 | 4 | 11 | 38 | 24 | 56 | 72 | 10 | 38 | 59 |
| DUALOSC MULTI H | 6 | 682 | 183 | 362 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 11 | 28 | 18 | 66 | 55 | 7 | 35 | 91 |
| SPREAD MULTI H | 8 | 918 | 228 | 548 | 404 | 22 | 94 | 16 | 0 | 71 | 16 | 4 | 12 | 38 | 22 | 72 | 72 | 10 | 39 | 97 |
| DUALFLT MULTI H | 7 | 673 | 219 | 316 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 11 | 33 | 21 | 50 | 97 | 9 | 35 | 77 |
| STACK MULTI H | 6 | 721 | 210 | 371 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 4 | 12 | 28 | 18 | 64 | 78 | 12 | 36 | 97 |
| BINAUR MULTI H | 6 | 762 | 230 | 387 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 12 | 28 | 18 | 43 | 124 | 8 | 40 | 103 |
| SPREAD H CEL+PP | 8 | 955 | 228 | 546 | 404 | 22 | 92 | 16 | 0 | 110 | 16 | 4 | 12 | 38 | 22 | 72 | 72 | 10 | 39 | 97 |
| DUALOSC H CEL+PP | 6 | 721 | 184 | 360 | 0 | 0 | 0 | 0 | 1 | 110 | 16 | 5 | 11 | 29 | 18 | 66 | 55 | 7 | 34 | 91 |
| SPREAD H TUM+PP | 8 | 941 | 228 | 546 | 404 | 22 | 92 | 16 | 2 | 97 | 16 | 4 | 12 | 38 | 22 | 72 | 72 | 10 | 36 | 97 |
| SEQ+ARP SPRD CEL | 8 | 1013 | 244 | 554 | 408 | 24 | 94 | 16 | 2 | 114 | 17 | 16 | 14 | 38 | 34 | 74 | 74 | 10 | 52 | 100 |
| HEAVY SPRING+PP | 8 | 613 | 204 | 248 | 118 | 22 | 80 | 16 | 0 | 91 | 16 | 4 | 12 | 38 | 22 | 56 | 66 | 8 | 38 | 56 |
| HEAVY CEL+BBD PP | 8 | 641 | 206 | 250 | 118 | 22 | 80 | 16 | 0 | 117 | 15 | 4 | 12 | 38 | 24 | 54 | 68 | 8 | 37 | 57 |
| SEQ+ARP SPR+BBD | 8 | 676 | 218 | 254 | 122 | 22 | 80 | 16 | 2 | 102 | 17 | 16 | 14 | 38 | 32 | 56 | 68 | 10 | 53 | 59 |
| STRIKE | 8 | 578 | 180 | 264 | 0 | 0 | 0 | 0 | 0 | 71 | 16 | 4 | 12 | 38 | 24 | 56 | 34 | 14 | 31 | 56 |
| STRIKE HEAVY | 8 | 618 | 210 | 264 | 0 | 0 | 0 | 0 | 0 | 72 | 16 | 4 | 12 | 36 | 24 | 56 | 66 | 16 | 40 | 59 |
| SEQ+ARP STRIKE | 8 | 714 | 226 | 268 | 0 | 0 | 0 | 0 | 2 | 117 | 17 | 16 | 14 | 38 | 34 | 56 | 68 | 16 | 54 | 62 |
| SEQ+ARP STRK FILT | 8 | 724 | 224 | 280 | 0 | 0 | 0 | 0 | 0 | 115 | 17 | 17 | 14 | 38 | 34 | 56 | 68 | 16 | 57 | 63 |
| SINGLE SH2 H | 8 | 665 | 206 | 320 | 118 | 22 | 150 | 16 | 0 | 69 | 15 | 4 | 11 | 38 | 22 | 54 | 70 | 8 | 40 | 66 |
| DUALOSC SH2 H | 6 | 723 | 181 | 405 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 11 | 28 | 17 | 66 | 53 | 7 | 35 | 98 |
| SPREAD SH2 H | 8 | 968 | 224 | 604 | 404 | 22 | 150 | 16 | 2 | 70 | 16 | 4 | 11 | 38 | 22 | 72 | 70 | 8 | 37 | 104 |
| DUALFLT SH2 H | 7 | 779 | 216 | 421 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 11 | 33 | 21 | 48 | 96 | 8 | 39 | 91 |
| STACK SH2 H | 6 | 801 | 205 | 458 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 4 | 12 | 28 | 17 | 63 | 75 | 12 | 35 | 111 |
| BINAUR SH2 H | 6 | 842 | 225 | 473 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 11 | 29 | 18 | 44 | 118 | 8 | 42 | 116 |
| STRIKE SH2 H | 8 | 705 | 214 | 348 | 0 | 0 | 0 | 0 | 0 | 72 | 15 | 5 | 12 | 38 | 22 | 56 | 70 | 14 | 39 | 70 |
| SPREAD SH2 CEL+PP | 8 | 1010 | 224 | 604 | 402 | 24 | 150 | 16 | 2 | 110 | 16 | 4 | 12 | 38 | 22 | 72 | 70 | 8 | 38 | 104 |
| DUALFLT SH2 CEL | 7 | 820 | 217 | 422 | 0 | 0 | 0 | 0 | 1 | 110 | 16 | 5 | 12 | 33 | 20 | 48 | 97 | 8 | 37 | 91 |
| SEQ+ARP SH2 CEL | 8 | 761 | 222 | 324 | 120 | 24 | 152 | 18 | 2 | 115 | 16 | 15 | 13 | 38 | 34 | 56 | 72 | 10 | 54 | 68 |
| DUALFLT SH2 HP | 7 | 779 | 218 | 420 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 11 | 33 | 20 | 49 | 97 | 8 | 38 | 91 |
| BINAUR SH2 HP | 6 | 843 | 225 | 473 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 12 | 29 | 17 | 43 | 119 | 8 | 40 | 116 |
| SINGLE PCM | 8 | 488 | 180 | 176 | 50 | 6 | 94 | 16 | 0 | 68 | 15 | 4 | 10 | 38 | 22 | 66 | 32 | 10 | 35 | 44 |
| DUAL OSC PCM | 6 | 599 | 177 | 293 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 11 | 28 | 18 | 89 | 25 | 7 | 28 | 78 |
| SPREAD PCM | 8 | 743 | 200 | 412 | 284 | 6 | 94 | 16 | 0 | 69 | 16 | 4 | 11 | 38 | 24 | 84 | 34 | 10 | 31 | 76 |
| DUAL FILTER PCM | 7 | 597 | 195 | 270 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 11 | 34 | 20 | 60 | 63 | 9 | 31 | 67 |
| STACK PCM | 6 | 699 | 202 | 365 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 11 | 29 | 17 | 87 | 48 | 12 | 29 | 95 |
| BINAURAL PCM | 6 | 657 | 209 | 314 | 0 | 0 | 0 | 0 | 1 | 71 | 17 | 5 | 12 | 29 | 18 | 57 | 89 | 8 | 28 | 87 |
| DUALOSC MULTI H PCM | 6 | 597 | 206 | 253 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 11 | 28 | 18 | 89 | 55 | 7 | 34 | 77 |
| DUALFLT MULTI H PCM | 7 | 655 | 231 | 284 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 11 | 33 | 21 | 61 | 97 | 8 | 38 | 74 |
| BINAUR MULTI H PCM | 6 | 735 | 251 | 336 | 0 | 0 | 0 | 0 | 1 | 71 | 17 | 5 | 12 | 29 | 18 | 56 | 132 | 8 | 42 | 98 |
| SINGLE RS | 8 | 551 | 172 | 246 | 120 | 6 | 92 | 16 | 0 | 70 | 16 | 4 | 11 | 38 | 22 | 56 | 32 | 10 | 32 | 52 |
| SINGLE RS PCM | 8 | 520 | 182 | 208 | 80 | 6 | 94 | 16 | 0 | 68 | 15 | 4 | 11 | 38 | 22 | 68 | 32 | 8 | 32 | 49 |
| DUALOSC RS | 6 | 683 | 159 | 396 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 4 | 10 | 28 | 18 | 71 | 25 | 8 | 28 | 93 |
| DUALOSC RS PCM | 6 | 601 | 177 | 292 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 11 | 29 | 18 | 89 | 25 | 7 | 29 | 78 |
| SEQ+ARP HOME PCM | 8 | 624 | 230 | 220 | 86 | 24 | 82 | 16 | 2 | 73 | 17 | 16 | 13 | 38 | 34 | 68 | 66 | 10 | 53 | 56 |
| SINGLE DRV SOFT | 8 | 590 | 178 | 276 | 118 | 6 | 94 | 46 | 2 | 71 | 16 | 4 | 11 | 38 | 24 | 56 | 34 | 16 | 32 | 57 |
| SINGLE DRV HARD | 8 | 593 | 178 | 280 | 120 | 6 | 94 | 50 | 0 | 72 | 16 | 5 | 11 | 38 | 24 | 56 | 34 | 16 | 31 | 57 |
| SINGLE DRV FOLD | 8 | 593 | 178 | 282 | 120 | 6 | 94 | 52 | 2 | 70 | 16 | 4 | 11 | 38 | 24 | 56 | 34 | 16 | 30 | 58 |
| SINGLE DRV TAPE | 8 | 599 | 178 | 288 | 120 | 6 | 94 | 56 | 0 | 70 | 16 | 5 | 11 | 38 | 24 | 56 | 32 | 16 | 31 | 58 |
| SINGLE DRV TUBE | 8 | 596 | 178 | 286 | 118 | 6 | 94 | 54 | 2 | 69 | 16 | 4 | 12 | 36 | 22 | 56 | 34 | 16 | 29 | 58 |
| SINGLE DRV OD | 8 | 599 | 178 | 286 | 118 | 6 | 92 | 56 | 0 | 71 | 16 | 4 | 11 | 36 | 24 | 56 | 34 | 16 | 33 | 58 |
| STACK HEAVY TUBE | 6 | 756 | 204 | 410 | 0 | 0 | 0 | 0 | 1 | 72 | 16 | 5 | 12 | 28 | 18 | 64 | 68 | 17 | 36 | 102 |
| BINAUR HEAVY OD | 6 | 802 | 225 | 426 | 0 | 0 | 0 | 0 | 1 | 73 | 16 | 5 | 12 | 29 | 17 | 44 | 113 | 13 | 44 | 109 |
| STRIKE HEAVY TUBE | 8 | 669 | 218 | 304 | 0 | 0 | 0 | 0 | 0 | 74 | 16 | 5 | 12 | 38 | 22 | 56 | 66 | 22 | 40 | 65 |
| SEQ+ARP HOME TUBE | 8 | 694 | 226 | 292 | 120 | 22 | 80 | 54 | 2 | 74 | 17 | 16 | 13 | 38 | 34 | 56 | 68 | 16 | 54 | 65 |
| FULL | 4 | 574 | 127 | 319 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 5 | 10 | 19 | 12 | 49 | 36 | 5 | 29 | 112 |
| FULL HEAVY | 4 | 554 | 137 | 283 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 5 | 11 | 19 | 12 | 45 | 50 | 5 | 34 | 105 |
| FULL MULTI H | 4 | 573 | 143 | 296 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 5 | 11 | 19 | 12 | 45 | 56 | 5 | 34 | 110 |
| FULL SH2 H | 4 | 634 | 143 | 355 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 5 | 11 | 19 | 12 | 45 | 56 | 5 | 35 | 124 |
| FULL H CEL+PP | 4 | 617 | 145 | 296 | 0 | 0 | 0 | 0 | 1 | 110 | 16 | 5 | 11 | 19 | 12 | 45 | 57 | 5 | 33 | 110 |
| FULL MULTI H PCM | 4 | 479 | 158 | 187 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 6 | 10 | 19 | 12 | 60 | 56 | 5 | 34 | 86 |
| FULL SH2 H PCM | 4 | 578 | 158 | 281 | 0 | 0 | 0 | 0 | 0 | 71 | 15 | 5 | 11 | 19 | 12 | 61 | 55 | 5 | 37 | 110 |
| FULL SH2 CEL+PP | 4 | 675 | 143 | 355 | 0 | 0 | 0 | 0 | 1 | 111 | 15 | 5 | 11 | 19 | 12 | 44 | 56 | 5 | 34 | 125 |
| DSPRD | 4 | 723 | 125 | 469 | 0 | 0 | 0 | 0 | 0 | 70 | 16 | 4 | 11 | 19 | 12 | 64 | 18 | 5 | 28 | 148 |
| DSPRD HEAVY | 4 | 606 | 137 | 331 | 0 | 0 | 0 | 0 | 1 | 70 | 17 | 5 | 12 | 19 | 12 | 60 | 35 | 5 | 33 | 117 |
| DSPRD MULTI H | 4 | 617 | 141 | 338 | 0 | 0 | 0 | 0 | 0 | 71 | 16 | 4 | 12 | 20 | 12 | 61 | 37 | 5 | 35 | 120 |
| DSPRD SH2 H | 4 | 643 | 139 | 365 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 12 | 19 | 12 | 61 | 36 | 5 | 34 | 126 |
| DSPRD H CEL+PP | 4 | 656 | 142 | 336 | 0 | 0 | 0 | 0 | 0 | 111 | 16 | 5 | 13 | 19 | 12 | 61 | 38 | 5 | 33 | 120 |
| DSPRD MULTI H PCM | 4 | 651 | 157 | 359 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 5 | 11 | 19 | 12 | 77 | 38 | 5 | 34 | 129 |
| DSPRD SH2 CEL+PP | 4 | 684 | 140 | 365 | 0 | 0 | 0 | 0 | 0 | 112 | 16 | 5 | 12 | 19 | 12 | 61 | 37 | 5 | 34 | 126 |
| SEQ+ARP DSPRD CEL | 4 | 707 | 153 | 340 | 0 | 0 | 0 | 0 | 1 | 116 | 17 | 16 | 14 | 21 | 18 | 64 | 39 | 5 | 50 | 124 |
| IDLE PHASER | 0 | 134 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 83 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE PHASER x2 | 0 | 152 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 101 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD PHASER | 8 | 885 | 190 | 534 | 406 | 6 | 94 | 16 | 0 | 96 | 16 | 5 | 12 | 38 | 24 | 74 | 34 | 8 | 32 | 90 |
| SPREAD H CEL PHSR | 8 | 990 | 232 | 550 | 406 | 22 | 92 | 16 | 2 | 136 | 16 | 5 | 13 | 38 | 24 | 76 | 74 | 8 | 36 | 98 |
| SEQ+ARP HOME PHSR2 | 8 | 695 | 220 | 254 | 120 | 22 | 80 | 16 | 2 | 118 | 17 | 17 | 14 | 38 | 34 | 58 | 68 | 10 | 53 | 60 |
| IDLE ENSEMBLE | 0 | 137 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENSEMBLE x2 | 0 | 158 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 107 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENS+PHSR | 0 | 143 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 91 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| IDLE ENS+PHSR x2 | 0 | 168 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS | 0 | 138 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 15 | 4 | 11 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS x2 | 0 | 157 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 105 | 15 | 4 | 11 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL ENPH | 8 | 1005 | 234 | 550 | 406 | 22 | 94 | 16 | 2 | 148 | 16 | 5 | 14 | 38 | 22 | 76 | 74 | 10 | 36 | 98 |
| SEQ+ARP HOME ENPH2 | 8 | 717 | 220 | 256 | 120 | 22 | 82 | 16 | 2 | 137 | 17 | 17 | 14 | 38 | 34 | 58 | 68 | 10 | 54 | 60 |
| SPREAD H CEL MIX | 8 | 974 | 230 | 548 | 404 | 22 | 92 | 16 | 0 | 126 | 16 | 4 | 13 | 38 | 24 | 74 | 74 | 8 | 37 | 97 |
| SEQ+ARP CEL MIX | 8 | 705 | 220 | 252 | 120 | 22 | 80 | 16 | 2 | 132 | 16 | 16 | 14 | 38 | 34 | 56 | 68 | 10 | 53 | 59 |
| HEAVY SPRING MIX | 8 | 632 | 204 | 248 | 120 | 22 | 80 | 16 | 2 | 108 | 16 | 4 | 13 | 38 | 22 | 54 | 66 | 8 | 37 | 57 |
| IDLE TAPE FLANGER | 0 | 145 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 93 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| IDLE TAPE FLNG x2 | 0 | 169 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 4 | 11 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL TFLG | 8 | 1004 | 234 | 548 | 404 | 22 | 92 | 16 | 2 | 148 | 16 | 5 | 14 | 38 | 24 | 76 | 74 | 10 | 37 | 98 |
| SEQ+ARP HOME TFLG2 | 8 | 716 | 220 | 254 | 122 | 24 | 82 | 16 | 2 | 135 | 17 | 17 | 15 | 38 | 34 | 58 | 66 | 10 | 56 | 60 |
| IDLE BBD FLANGER | 0 | 136 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 85 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE BBD FLNG x2 | 0 | 156 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 104 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| SPREAD H CEL BBDF | 8 | 991 | 232 | 548 | 404 | 22 | 92 | 16 | 2 | 139 | 16 | 5 | 13 | 38 | 24 | 74 | 74 | 10 | 36 | 98 |
| IDLE PITCH SHIFT | 0 | 143 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 92 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| PITCH SHIFT HELD | 8 | 588 | 174 | 246 | 120 | 6 | 92 | 16 | 0 | 103 | 16 | 5 | 12 | 38 | 24 | 56 | 34 | 10 | 32 | 52 |
| IDLE PITCH SHFT x2 | 0 | 170 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 119 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL PSHF | 8 | 996 | 232 | 548 | 404 | 22 | 94 | 16 | 2 | 144 | 16 | 5 | 13 | 38 | 24 | 74 | 74 | 10 | 36 | 98 |

### 2026-11-02 19:03, CARNAC OS PROF (1 core), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 21.1 | 21.8 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 31.1 | 32.0 | 0 | 1 | 57.6 | 4 | pass |
| SINGLE | 82.2 | 83.5 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE HEAVY | 89.8 | 91.2 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC | 116.4 | 117.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD | 103.5 | 105.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 96.3 | 97.5 | 0 | 7 | 42.9 | 12 | FAIL |
| STACK | 119.3 | 120.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL | 97.6 | 98.8 | 0 | 6 | 41.2 | 12 | FAIL |
| DUAL OSC HEAVY | 113.4 | 114.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 110.9 | 112.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 102.4 | 104.2 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 112.7 | 114.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 107.3 | 108.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 82.2 | 83.4 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE MS20 | 77.1 | 78.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE SH-2 | 92.2 | 93.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE FORMANT | 78.2 | 79.7 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FLT LADDER | 96.3 | 97.5 | 0 | 7 | 42.9 | 12 | FAIL |
| SEQ+ARP HOME | 98.3 | 102.6 | 48 | 8 | 29.8 | 93 | FAIL |
| SEQ+ARP AMP | 98.3 | 102.8 | 51 | 8 | 29.6 | 127 | FAIL |
| SEQ+ARP SEQ | 97.8 | 103.2 | 43 | 8 | 5.2 | 127 | FAIL |
| SEQ+ARP MOD1 | 99.2 | 103.4 | 79 | 8 | 10.2 | 127 | FAIL |
| LADDER FILTER PG | 82.2 | 83.4 | 0 | 8 | 43.2 | 1 | FAIL |
| HEAVY MOD+PP | 96.6 | 98.1 | 0 | 8 | 42.5 | 12 | FAIL |
| HEAVY CELEST+PP | 97.5 | 99.2 | 0 | 8 | 43.0 | 1 | FAIL |
| SEQ+ARP CELEST+PP | 107.9 | 110.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MULTI H | 93.8 | 95.1 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALOSC MULTI H | 115.7 | 116.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD MULTI H | 115.1 | 116.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H | 107.8 | 109.5 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK MULTI H | 118.5 | 120.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H | 112.9 | 114.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL+PP | 122.6 | 124.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC H CEL+PP | 123.8 | 125.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H TUM+PP | 120.3 | 122.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPRD CEL | 132.5 | 135.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 94.4 | 95.7 | 0 | 8 | 43.2 | 1 | FAIL |
| HEAVY CEL+BBD PP | 99.3 | 100.7 | 56 | 8 | 31.0 | 35 | FAIL |
| SEQ+ARP SPR+BBD | 105.8 | 108.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STRIKE | 82.5 | 83.5 | 0 | 8 | 42.9 | 1 | FAIL |
| STRIKE HEAVY | 89.8 | 91.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP STRIKE | 113.1 | 115.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRK FILT | 114.7 | 117.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE SH2 H | 104.6 | 106.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC SH2 H | 124.0 | 125.7 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 H | 125.0 | 126.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 H | 127.8 | 129.7 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK SH2 H | 133.8 | 135.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 H | 128.3 | 129.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE SH2 H | 111.6 | 113.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 CEL+PP | 133.3 | 134.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 CEL | 135.6 | 137.1 | 8 | 7 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SH2 CEL | 122.4 | 125.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 HP | 128.0 | 129.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 HP | 128.3 | 129.7 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE PCM | 90.3 | 91.9 | 0 | 8 | 42.9 | 1 | FAIL |
| DUAL OSC PCM | 111.0 | 112.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD PCM | 137.1 | 139.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER PCM | 110.2 | 112.1 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK PCM | 129.2 | 130.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL PCM | 121.1 | 122.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC MULTI H PCM | 110.6 | 111.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H PCM | 121.0 | 122.7 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H PCM | 135.7 | 137.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE RS | 82.3 | 84.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE RS PCM | 96.2 | 99.3 | 0 | 8 | 42.7 | 12 | FAIL |
| DUALOSC RS | 116.4 | 117.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC RS PCM | 111.0 | 112.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PCM | 115.3 | 118.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV SOFT | 89.4 | 90.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE DRV HARD | 89.8 | 91.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV FOLD | 89.9 | 91.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TAPE | 91.3 | 92.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TUBE | 90.9 | 92.1 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV OD | 91.1 | 92.5 | 0 | 8 | 43.2 | 1 | FAIL |
| STACK HEAVY TUBE | 125.2 | 126.0 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR HEAVY OD | 120.1 | 121.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE HEAVY TUBE | 104.7 | 106.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TUBE | 109.1 | 111.6 | 8 | 8 | – | – | FAIL (cut by the guard) |
| FULL | 97.9 | 99.5 | 0 | 4 | 41.4 | 12 | FAIL |
| FULL HEAVY | 94.5 | 95.9 | 0 | 4 | 43.2 | 1 | FAIL |
| FULL MULTI H | 98.0 | 99.5 | 0 | 4 | 38.6 | 12 | FAIL |
| FULL SH2 H | 110.4 | 110.9 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL H CEL+PP | 107.6 | 108.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H PCM | 88.6 | 90.1 | 0 | 4 | 42.9 | 1 | FAIL |
| FULL SH2 H PCM | 107.1 | 108.1 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 CEL+PP | 118.4 | 119.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD | 78.5 | 80.1 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD HEAVY | 82.7 | 84.1 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD MULTI H | 84.9 | 86.2 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD SH2 H | 90.0 | 91.3 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD H CEL+PP | 92.1 | 93.8 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD MULTI H PCM | 120.1 | 120.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD SH2 CEL+PP | 97.6 | 99.2 | 0 | 4 | 42.9 | 12 | FAIL |
| SEQ+ARP DSPRD CEL | 101.8 | 104.2 | 8 | 4 | – | – | FAIL (cut by the guard) |
| IDLE PHASER | 25.2 | 25.8 | 0 | 0 | 60.0 | 1 | pass |
| IDLE PHASER x2 | 28.5 | 29.1 | 0 | 0 | 59.3 | 4 | pass |
| SPREAD PHASER | 109.3 | 112.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL PHSR | 128.5 | 130.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PHSR2 | 109.2 | 111.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE ENSEMBLE | 25.7 | 26.4 | 0 | 0 | 60.0 | 1 | pass |
| IDLE ENSEMBLE x2 | 29.6 | 30.2 | 0 | 0 | 58.3 | 4 | pass |
| IDLE ENS+PHSR | 26.8 | 27.4 | 0 | 0 | 59.6 | 1 | pass |
| IDLE ENS+PHSR x2 | 31.5 | 32.2 | 0 | 0 | 58.2 | 4 | pass |
| IDLE CHORUS | 25.9 | 26.5 | 0 | 0 | 60.0 | 4 | pass |
| IDLE CHORUS x2 | 29.5 | 30.0 | 0 | 0 | 58.6 | 4 | pass |
| SPREAD H CEL ENPH | 132.2 | 133.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME ENPH2 | 113.4 | 115.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL MIX | 125.7 | 127.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP CEL MIX | 111.8 | 114.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING MIX | 97.4 | 98.8 | 0 | 8 | 40.2 | 12 | FAIL |
| IDLE TAPE FLANGER | 27.2 | 27.7 | 0 | 0 | 60.0 | 4 | pass |
| IDLE TAPE FLNG x2 | 31.7 | 32.3 | 0 | 0 | 58.0 | 4 | pass |
| SPREAD H CEL TFLG | 132.0 | 133.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TFLG2 | 113.1 | 115.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE BBD FLANGER | 25.6 | 26.3 | 0 | 0 | 60.2 | 4 | pass |
| IDLE BBD FLNG x2 | 29.2 | 29.9 | 0 | 0 | 59.3 | 4 | pass |
| SPREAD H CEL BBDF | 129.0 | 131.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE PITCH SHIFT | 26.9 | 27.8 | 0 | 0 | 59.9 | 4 | pass |
| PITCH SHIFT HELD | 88.9 | 90.7 | 0 | 8 | 42.9 | 1 | FAIL |
| IDLE PITCH SHFT x2 | 31.8 | 33.1 | 0 | 0 | 58.0 | 1 | pass |
| SPREAD H CEL PSHF | 130.2 | 132.0 | 8 | 8 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice per 16-sample block (the voice stages, CTRL and its c.* split); 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 112 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 63 | 15 | 3 | 9 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SINGLE 1V | 1 | 166 | 24 | 20 | 4 | 0 | 12 | 2 | 0 | 67 | 15 | 4 | 10 | 5 | 3 | 8 | 5 | 1 | 26 | 44 |
| SINGLE | 8 | 444 | 162 | 154 | 28 | 6 | 92 | 16 | 0 | 67 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 8 | 32 | 40 |
| SINGLE HEAVY | 8 | 485 | 192 | 158 | 28 | 22 | 80 | 16 | 0 | 68 | 15 | 4 | 11 | 38 | 22 | 46 | 64 | 8 | 37 | 44 |
| DUAL OSC | 6 | 629 | 147 | 356 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 10 | 28 | 18 | 59 | 25 | 7 | 28 | 84 |
| SPREAD | 8 | 559 | 182 | 246 | 120 | 6 | 92 | 16 | 0 | 69 | 15 | 4 | 11 | 38 | 22 | 66 | 34 | 8 | 32 | 54 |
| DUAL FILTER | 7 | 521 | 174 | 220 | 0 | 0 | 0 | 0 | 1 | 67 | 15 | 4 | 10 | 33 | 20 | 41 | 62 | 8 | 30 | 56 |
| STACK | 6 | 645 | 172 | 344 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 11 | 28 | 18 | 57 | 48 | 12 | 29 | 86 |
| BINAURAL | 6 | 527 | 173 | 227 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 11 | 28 | 17 | 37 | 74 | 8 | 28 | 67 |
| DUAL OSC HEAVY | 6 | 613 | 170 | 308 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 4 | 11 | 28 | 17 | 59 | 50 | 7 | 34 | 80 |
| SPREAD HEAVY | 8 | 600 | 212 | 250 | 120 | 22 | 80 | 16 | 2 | 69 | 15 | 4 | 11 | 38 | 22 | 64 | 66 | 8 | 37 | 58 |
| DUAL FLT HEAVY | 7 | 554 | 202 | 215 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 4 | 11 | 34 | 20 | 42 | 88 | 8 | 38 | 60 |
| STACK HEAVY | 6 | 610 | 191 | 283 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 11 | 28 | 17 | 56 | 68 | 12 | 34 | 79 |
| BINAURAL HEAVY | 6 | 580 | 206 | 232 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 11 | 29 | 17 | 37 | 106 | 8 | 40 | 73 |
| SINGLE LADDER | 8 | 444 | 162 | 156 | 28 | 6 | 92 | 16 | 0 | 67 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 8 | 30 | 40 |
| SINGLE MS20 | 8 | 416 | 152 | 134 | 28 | 6 | 72 | 16 | 0 | 68 | 15 | 4 | 10 | 38 | 22 | 46 | 26 | 8 | 33 | 36 |
| SINGLE SH-2 | 8 | 498 | 158 | 214 | 28 | 6 | 150 | 16 | 0 | 66 | 15 | 4 | 10 | 36 | 22 | 46 | 30 | 8 | 31 | 46 |
| SINGLE FORMANT | 8 | 422 | 154 | 142 | 28 | 6 | 80 | 16 | 0 | 66 | 15 | 4 | 11 | 38 | 22 | 46 | 26 | 8 | 30 | 37 |
| DUAL FLT LADDER | 7 | 521 | 174 | 220 | 0 | 0 | 0 | 0 | 1 | 67 | 15 | 4 | 10 | 33 | 20 | 41 | 62 | 8 | 30 | 56 |
| SEQ+ARP HOME | 8 | 531 | 204 | 162 | 30 | 22 | 80 | 16 | 2 | 70 | 16 | 13 | 12 | 38 | 32 | 46 | 66 | 8 | 52 | 46 |
| SEQ+ARP AMP | 8 | 531 | 204 | 162 | 30 | 22 | 80 | 16 | 2 | 70 | 16 | 13 | 12 | 38 | 32 | 46 | 66 | 8 | 52 | 46 |
| SEQ+ARP SEQ | 8 | 529 | 204 | 162 | 30 | 22 | 80 | 16 | 2 | 70 | 16 | 12 | 11 | 38 | 32 | 46 | 66 | 8 | 52 | 46 |
| SEQ+ARP MOD1 | 8 | 536 | 206 | 162 | 30 | 22 | 80 | 16 | 2 | 70 | 16 | 12 | 15 | 38 | 32 | 46 | 64 | 8 | 53 | 46 |
| LADDER FILTER PG | 8 | 444 | 162 | 154 | 28 | 6 | 92 | 16 | 0 | 67 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 8 | 32 | 40 |
| HEAVY MOD+PP | 8 | 522 | 192 | 158 | 28 | 22 | 80 | 16 | 0 | 103 | 15 | 4 | 11 | 38 | 22 | 46 | 64 | 8 | 39 | 44 |
| HEAVY CELEST+PP | 8 | 527 | 192 | 158 | 28 | 22 | 80 | 16 | 0 | 109 | 15 | 4 | 11 | 38 | 22 | 46 | 64 | 8 | 38 | 44 |
| SEQ+ARP CELEST+PP | 8 | 583 | 208 | 162 | 30 | 22 | 80 | 16 | 2 | 113 | 16 | 15 | 13 | 38 | 34 | 48 | 66 | 10 | 54 | 46 |
| SINGLE MULTI H | 8 | 507 | 200 | 172 | 28 | 22 | 92 | 16 | 0 | 69 | 15 | 4 | 10 | 38 | 22 | 46 | 72 | 8 | 37 | 46 |
| DUALOSC MULTI H | 6 | 626 | 176 | 316 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 10 | 28 | 18 | 59 | 55 | 7 | 35 | 82 |
| SPREAD MULTI H | 8 | 622 | 222 | 262 | 120 | 22 | 92 | 16 | 2 | 70 | 15 | 4 | 11 | 38 | 24 | 66 | 72 | 8 | 36 | 61 |
| DUALFLT MULTI H | 7 | 583 | 213 | 235 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 4 | 11 | 33 | 21 | 42 | 98 | 8 | 35 | 64 |
| STACK MULTI H | 6 | 642 | 202 | 303 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 4 | 11 | 29 | 17 | 57 | 78 | 12 | 35 | 84 |
| BINAUR MULTI H | 6 | 611 | 218 | 252 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 4 | 11 | 28 | 18 | 37 | 117 | 8 | 40 | 78 |
| SPREAD H CEL+PP | 8 | 663 | 222 | 262 | 120 | 22 | 92 | 16 | 0 | 110 | 16 | 4 | 12 | 38 | 24 | 66 | 72 | 8 | 37 | 60 |
| DUALOSC H CEL+PP | 6 | 670 | 177 | 317 | 0 | 0 | 0 | 0 | 1 | 109 | 15 | 5 | 11 | 29 | 17 | 60 | 55 | 7 | 35 | 82 |
| SPREAD H TUM+PP | 8 | 651 | 220 | 264 | 120 | 22 | 92 | 16 | 2 | 98 | 15 | 4 | 12 | 36 | 24 | 64 | 72 | 8 | 36 | 61 |
| SEQ+ARP SPRD CEL | 8 | 717 | 236 | 266 | 120 | 22 | 94 | 16 | 2 | 115 | 16 | 15 | 13 | 38 | 34 | 66 | 74 | 10 | 54 | 63 |
| HEAVY SPRING+PP | 8 | 510 | 192 | 158 | 28 | 22 | 80 | 16 | 0 | 91 | 15 | 4 | 11 | 38 | 22 | 46 | 66 | 8 | 39 | 44 |
| HEAVY CEL+BBD PP | 8 | 537 | 194 | 160 | 28 | 22 | 80 | 16 | 0 | 116 | 15 | 4 | 11 | 38 | 22 | 46 | 66 | 8 | 37 | 44 |
| SEQ+ARP SPR+BBD | 8 | 572 | 208 | 162 | 30 | 24 | 80 | 16 | 2 | 101 | 16 | 15 | 14 | 38 | 32 | 48 | 68 | 8 | 54 | 46 |
| STRIKE | 8 | 445 | 164 | 152 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 11 | 38 | 22 | 48 | 34 | 10 | 31 | 40 |
| STRIKE HEAVY | 8 | 485 | 194 | 154 | 0 | 0 | 0 | 0 | 0 | 70 | 15 | 4 | 11 | 38 | 22 | 46 | 64 | 8 | 37 | 44 |
| SEQ+ARP STRIKE | 8 | 611 | 216 | 178 | 0 | 0 | 0 | 0 | 2 | 117 | 17 | 15 | 13 | 38 | 32 | 48 | 66 | 16 | 53 | 50 |
| SEQ+ARP STRK FILT | 8 | 620 | 218 | 186 | 0 | 0 | 0 | 0 | 2 | 114 | 16 | 16 | 14 | 38 | 32 | 50 | 68 | 16 | 54 | 51 |
| SINGLE SH2 H | 8 | 566 | 198 | 230 | 28 | 22 | 150 | 16 | 2 | 69 | 15 | 4 | 11 | 38 | 22 | 46 | 70 | 10 | 37 | 54 |
| DUALOSC SH2 H | 6 | 671 | 174 | 361 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 11 | 28 | 17 | 59 | 54 | 7 | 35 | 89 |
| SPREAD SH2 H | 8 | 676 | 216 | 320 | 120 | 22 | 150 | 16 | 2 | 70 | 15 | 4 | 11 | 38 | 22 | 64 | 70 | 8 | 38 | 67 |
| DUALFLT SH2 H | 7 | 692 | 211 | 342 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 5 | 11 | 33 | 21 | 42 | 97 | 8 | 39 | 79 |
| STACK SH2 H | 6 | 724 | 198 | 390 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 11 | 29 | 17 | 57 | 74 | 12 | 35 | 98 |
| BINAUR SH2 H | 6 | 694 | 213 | 338 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 12 | 28 | 18 | 37 | 113 | 8 | 40 | 92 |
| STRIKE SH2 H | 8 | 604 | 206 | 256 | 0 | 0 | 0 | 0 | 0 | 71 | 16 | 4 | 12 | 38 | 22 | 48 | 70 | 14 | 39 | 58 |
| SPREAD SH2 CEL+PP | 8 | 722 | 218 | 322 | 118 | 22 | 152 | 16 | 0 | 111 | 16 | 5 | 12 | 38 | 22 | 64 | 70 | 10 | 38 | 68 |
| DUALFLT SH2 CEL | 7 | 734 | 208 | 347 | 0 | 0 | 0 | 0 | 1 | 109 | 15 | 5 | 11 | 32 | 20 | 40 | 96 | 8 | 38 | 79 |
| SEQ+ARP SH2 CEL | 8 | 662 | 214 | 232 | 30 | 24 | 150 | 16 | 2 | 115 | 16 | 16 | 13 | 38 | 34 | 48 | 72 | 10 | 54 | 56 |
| DUALFLT SH2 HP | 7 | 693 | 209 | 345 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 5 | 11 | 33 | 20 | 41 | 96 | 8 | 39 | 79 |
| BINAUR SH2 HP | 6 | 694 | 215 | 337 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 5 | 11 | 29 | 17 | 38 | 113 | 8 | 41 | 92 |
| SINGLE PCM | 8 | 488 | 180 | 176 | 50 | 6 | 94 | 16 | 0 | 69 | 15 | 4 | 11 | 38 | 22 | 66 | 32 | 10 | 33 | 44 |
| DUAL OSC PCM | 6 | 600 | 177 | 293 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 11 | 28 | 18 | 89 | 25 | 7 | 28 | 78 |
| SPREAD PCM | 8 | 742 | 200 | 412 | 284 | 6 | 92 | 16 | 0 | 68 | 16 | 5 | 11 | 36 | 22 | 84 | 34 | 10 | 30 | 76 |
| DUAL FILTER PCM | 7 | 596 | 195 | 270 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 11 | 32 | 21 | 61 | 62 | 8 | 28 | 67 |
| STACK PCM | 6 | 699 | 203 | 363 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 5 | 12 | 28 | 18 | 87 | 48 | 12 | 28 | 94 |
| BINAURAL PCM | 6 | 655 | 208 | 313 | 0 | 0 | 0 | 0 | 1 | 71 | 17 | 5 | 12 | 29 | 18 | 56 | 89 | 8 | 28 | 87 |
| DUALOSC MULTI H PCM | 6 | 598 | 205 | 254 | 0 | 0 | 0 | 0 | 1 | 71 | 15 | 5 | 11 | 28 | 18 | 89 | 55 | 7 | 36 | 77 |
| DUALFLT MULTI H PCM | 7 | 655 | 230 | 285 | 0 | 0 | 0 | 0 | 1 | 70 | 16 | 5 | 11 | 33 | 20 | 60 | 98 | 9 | 37 | 74 |
| BINAUR MULTI H PCM | 6 | 735 | 251 | 337 | 0 | 0 | 0 | 0 | 1 | 71 | 18 | 5 | 12 | 28 | 18 | 57 | 131 | 8 | 40 | 98 |
| SINGLE RS | 8 | 445 | 162 | 156 | 28 | 6 | 92 | 16 | 0 | 67 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 8 | 31 | 40 |
| SINGLE RS PCM | 8 | 520 | 182 | 208 | 80 | 6 | 94 | 16 | 0 | 68 | 15 | 4 | 11 | 38 | 22 | 68 | 32 | 8 | 32 | 49 |
| DUALOSC RS | 6 | 630 | 146 | 355 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 4 | 11 | 28 | 17 | 59 | 25 | 7 | 28 | 84 |
| DUALOSC RS PCM | 6 | 600 | 177 | 293 | 0 | 0 | 0 | 0 | 1 | 69 | 16 | 5 | 11 | 29 | 18 | 89 | 25 | 7 | 28 | 78 |
| SEQ+ARP HOME PCM | 8 | 624 | 230 | 220 | 86 | 22 | 80 | 16 | 2 | 73 | 17 | 16 | 13 | 38 | 34 | 70 | 66 | 8 | 53 | 56 |
| SINGLE DRV SOFT | 8 | 483 | 168 | 186 | 28 | 6 | 92 | 46 | 0 | 69 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 16 | 31 | 44 |
| SINGLE DRV HARD | 8 | 485 | 168 | 188 | 28 | 6 | 92 | 48 | 0 | 69 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 16 | 31 | 44 |
| SINGLE DRV FOLD | 8 | 486 | 168 | 190 | 28 | 6 | 92 | 52 | 0 | 67 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 16 | 32 | 45 |
| SINGLE DRV TAPE | 8 | 494 | 168 | 196 | 28 | 6 | 92 | 56 | 0 | 69 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 16 | 32 | 46 |
| SINGLE DRV TUBE | 8 | 491 | 168 | 194 | 28 | 6 | 92 | 54 | 0 | 69 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 16 | 31 | 45 |
| SINGLE DRV OD | 8 | 492 | 168 | 194 | 28 | 6 | 92 | 56 | 0 | 69 | 15 | 4 | 10 | 38 | 22 | 46 | 32 | 16 | 32 | 45 |
| STACK HEAVY TUBE | 6 | 677 | 197 | 341 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 5 | 12 | 28 | 17 | 56 | 69 | 17 | 36 | 90 |
| BINAUR HEAVY OD | 6 | 650 | 212 | 291 | 0 | 0 | 0 | 0 | 1 | 72 | 16 | 5 | 12 | 29 | 18 | 36 | 106 | 13 | 41 | 84 |
| STRIKE HEAVY TUBE | 8 | 566 | 208 | 212 | 0 | 0 | 0 | 0 | 2 | 73 | 16 | 5 | 11 | 38 | 22 | 48 | 66 | 22 | 39 | 53 |
| SEQ+ARP HOME TUBE | 8 | 590 | 214 | 202 | 28 | 22 | 80 | 56 | 2 | 74 | 16 | 16 | 13 | 38 | 32 | 48 | 68 | 16 | 53 | 52 |
| FULL | 4 | 530 | 117 | 290 | 0 | 0 | 0 | 0 | 0 | 66 | 15 | 4 | 9 | 19 | 11 | 40 | 36 | 5 | 29 | 102 |
| FULL HEAVY | 4 | 511 | 129 | 252 | 0 | 0 | 0 | 0 | 0 | 66 | 15 | 5 | 10 | 18 | 11 | 39 | 49 | 5 | 34 | 95 |
| FULL MULTI H | 4 | 530 | 136 | 265 | 0 | 0 | 0 | 0 | 0 | 66 | 15 | 4 | 10 | 19 | 11 | 40 | 56 | 5 | 34 | 100 |
| FULL SH2 H | 4 | 597 | 137 | 325 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 5 | 11 | 19 | 12 | 40 | 56 | 5 | 35 | 116 |
| FULL H CEL+PP | 4 | 582 | 141 | 266 | 0 | 0 | 0 | 0 | 0 | 110 | 16 | 5 | 11 | 19 | 12 | 41 | 58 | 5 | 33 | 102 |
| FULL MULTI H PCM | 4 | 478 | 158 | 187 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 6 | 10 | 19 | 11 | 60 | 56 | 5 | 33 | 86 |
| FULL SH2 H PCM | 4 | 579 | 159 | 281 | 0 | 0 | 0 | 0 | 1 | 71 | 16 | 6 | 11 | 19 | 12 | 61 | 56 | 5 | 34 | 110 |
| FULL SH2 CEL+PP | 4 | 641 | 138 | 325 | 0 | 0 | 0 | 0 | 1 | 111 | 15 | 5 | 11 | 19 | 12 | 40 | 57 | 5 | 35 | 116 |
| DSPRD | 4 | 424 | 115 | 184 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 11 | 19 | 11 | 56 | 17 | 5 | 27 | 75 |
| DSPRD HEAVY | 4 | 447 | 129 | 186 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 11 | 19 | 11 | 55 | 33 | 4 | 34 | 79 |
| DSPRD MULTI H | 4 | 459 | 135 | 192 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 11 | 19 | 11 | 56 | 37 | 5 | 34 | 82 |
| DSPRD SH2 H | 4 | 486 | 132 | 221 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 4 | 10 | 19 | 11 | 55 | 36 | 4 | 36 | 88 |
| DSPRD H CEL+PP | 4 | 498 | 134 | 192 | 0 | 0 | 0 | 0 | 0 | 108 | 15 | 4 | 11 | 19 | 11 | 56 | 37 | 5 | 34 | 82 |
| DSPRD MULTI H PCM | 4 | 650 | 156 | 360 | 0 | 0 | 0 | 0 | 0 | 69 | 16 | 5 | 10 | 19 | 12 | 76 | 37 | 5 | 34 | 129 |
| DSPRD SH2 CEL+PP | 4 | 527 | 133 | 222 | 0 | 0 | 0 | 0 | 0 | 108 | 15 | 4 | 11 | 19 | 11 | 55 | 36 | 5 | 34 | 89 |
| SEQ+ARP DSPRD CEL | 4 | 550 | 147 | 195 | 0 | 0 | 0 | 0 | 1 | 115 | 16 | 14 | 13 | 20 | 18 | 58 | 39 | 5 | 49 | 86 |
| IDLE PHASER | 0 | 134 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 83 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE PHASER x2 | 0 | 152 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 101 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD PHASER | 8 | 591 | 184 | 248 | 120 | 6 | 94 | 16 | 2 | 94 | 16 | 4 | 12 | 38 | 24 | 68 | 34 | 8 | 31 | 54 |
| SPREAD H CEL PHSR | 8 | 695 | 226 | 262 | 118 | 22 | 92 | 16 | 0 | 136 | 15 | 5 | 13 | 38 | 24 | 68 | 74 | 10 | 38 | 61 |
| SEQ+ARP HOME PHSR2 | 8 | 590 | 210 | 162 | 30 | 22 | 80 | 16 | 2 | 116 | 17 | 16 | 13 | 38 | 32 | 48 | 66 | 10 | 54 | 47 |
| IDLE ENSEMBLE | 0 | 137 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENSEMBLE x2 | 0 | 158 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 107 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE ENS+PHSR | 0 | 143 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 91 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| IDLE ENS+PHSR x2 | 0 | 168 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS | 0 | 138 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 15 | 4 | 11 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE CHORUS x2 | 0 | 157 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 105 | 15 | 4 | 11 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL ENPH | 8 | 715 | 228 | 264 | 120 | 24 | 92 | 16 | 0 | 150 | 16 | 5 | 14 | 38 | 24 | 68 | 76 | 10 | 38 | 62 |
| SEQ+ARP HOME ENPH2 | 8 | 613 | 212 | 164 | 30 | 24 | 80 | 16 | 2 | 135 | 17 | 16 | 14 | 38 | 32 | 48 | 68 | 10 | 53 | 47 |
| SPREAD H CEL MIX | 8 | 680 | 222 | 262 | 120 | 22 | 92 | 16 | 0 | 125 | 16 | 4 | 12 | 38 | 22 | 66 | 72 | 8 | 39 | 60 |
| SEQ+ARP CEL MIX | 8 | 604 | 208 | 162 | 30 | 24 | 82 | 16 | 2 | 131 | 17 | 16 | 14 | 38 | 34 | 48 | 66 | 10 | 54 | 46 |
| HEAVY SPRING MIX | 8 | 527 | 194 | 158 | 28 | 22 | 80 | 16 | 0 | 107 | 15 | 4 | 12 | 38 | 22 | 46 | 66 | 8 | 37 | 44 |
| IDLE TAPE FLANGER | 0 | 145 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 93 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| IDLE TAPE FLNG x2 | 0 | 169 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 117 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| SPREAD H CEL TFLG | 8 | 714 | 228 | 264 | 122 | 22 | 92 | 16 | 0 | 149 | 15 | 5 | 14 | 38 | 24 | 68 | 74 | 10 | 39 | 62 |
| SEQ+ARP HOME TFLG2 | 8 | 611 | 212 | 162 | 30 | 22 | 82 | 16 | 2 | 133 | 16 | 17 | 14 | 38 | 34 | 50 | 68 | 10 | 55 | 47 |
| IDLE BBD FLANGER | 0 | 136 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 85 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE BBD FLNG x2 | 0 | 156 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 104 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 23 | 0 |
| SPREAD H CEL BBDF | 8 | 698 | 224 | 264 | 118 | 22 | 92 | 16 | 0 | 139 | 15 | 5 | 13 | 38 | 24 | 66 | 72 | 10 | 38 | 61 |
| IDLE PITCH SHIFT | 0 | 143 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 92 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| PITCH SHIFT HELD | 8 | 480 | 162 | 156 | 28 | 6 | 92 | 16 | 0 | 99 | 15 | 4 | 11 | 38 | 22 | 48 | 34 | 8 | 33 | 40 |
| IDLE PITCH SHFT x2 | 0 | 170 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 119 | 15 | 4 | 10 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD H CEL PSHF | 8 | 705 | 226 | 264 | 120 | 22 | 92 | 16 | 0 | 145 | 16 | 5 | 13 | 38 | 24 | 66 | 74 | 10 | 36 | 61 |

### 2026-11-02 18:57, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.0 | 15.4 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 26.2 | 27.1 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 53.0 | 54.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE HEAVY | 57.6 | 58.7 | 0 | 8 | 57.5 | 7 | pass |
| DUAL OSC | 66.5 | 67.7 | 0 | 6 | 51.2 | 9 | pass |
| SPREAD | 80.8 | 82.0 | 0 | 8 | 42.9 | 1 | FAIL |
| DUAL FILTER | 66.4 | 67.3 | 0 | 7 | 49.9 | 8 | pass |
| STACK | 70.9 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAURAL | 66.8 | 67.8 | 0 | 6 | 49.6 | 9 | pass |
| DUAL OSC HEAVY | 65.4 | 66.6 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD HEAVY | 85.3 | 86.4 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FLT HEAVY | 70.6 | 71.9 | 0 | 7 | 43.5 | 9 | FAIL |
| STACK HEAVY | 68.9 | 70.5 | 0 | 6 | 43.9 | 9 | pass |
| BINAURAL HEAVY | 72.9 | 74.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE LADDER | 53.0 | 54.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 50.2 | 51.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 57.7 | 58.7 | 0 | 8 | 57.2 | 7 | pass |
| SINGLE FORMANT | 50.7 | 51.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 66.4 | 67.3 | 0 | 7 | 51.2 | 8 | pass |
| SEQ+ARP HOME | 66.1 | 69.9 | 0 | 8 | 50.9 | 10 | pass |
| SEQ+ARP AMP | 66.2 | 70.2 | 0 | 8 | 50.9 | 12 | pass |
| SEQ+ARP SEQ | 65.6 | 69.7 | 0 | 8 | 12.1 | 127 | pass |
| SEQ+ARP MOD1 | 66.0 | 71.2 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 53.0 | 53.9 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY MOD+PP | 61.7 | 62.8 | 0 | 8 | 52.9 | 8 | pass |
| HEAVY CELEST+PP | 62.5 | 63.6 | 0 | 8 | 52.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 70.6 | 75.0 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE MULTI H | 59.9 | 60.8 | 0 | 8 | 55.8 | 8 | pass |
| DUALOSC MULTI H | 67.0 | 68.1 | 0 | 6 | 47.6 | 9 | pass |
| SPREAD MULTI H | 87.6 | 88.8 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT MULTI H | 73.4 | 74.6 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK MULTI H | 71.7 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR MULTI H | 75.3 | 76.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 91.8 | 93.3 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALOSC H CEL+PP | 71.8 | 73.4 | 0 | 6 | 43.2 | 1 | FAIL |
| SPREAD H TUM+PP | 90.3 | 91.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP SPRD CEL | 101.3 | 103.9 | 11 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 60.5 | 61.6 | 0 | 8 | 54.9 | 8 | pass |
| HEAVY CEL+BBD PP | 63.5 | 64.7 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPR+BBD | 69.9 | 73.6 | 0 | 8 | 43.6 | 10 | pass |
| STRIKE | 54.0 | 55.1 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 58.9 | 59.8 | 0 | 8 | 56.5 | 8 | pass |
| SEQ+ARP STRIKE | 75.3 | 79.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 76.2 | 80.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 64.6 | 65.6 | 0 | 8 | 52.2 | 8 | pass |
| DUALOSC SH2 H | 70.7 | 71.9 | 0 | 6 | 43.2 | 9 | FAIL |
| SPREAD SH2 H | 92.3 | 93.8 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 H | 83.2 | 84.5 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 79.1 | 80.6 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 83.1 | 84.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 65.7 | 66.8 | 0 | 8 | 51.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 96.7 | 98.4 | 0 | 8 | 42.4 | 12 | FAIL |
| DUALFLT SH2 CEL | 88.0 | 89.4 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 76.9 | 81.4 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 83.2 | 84.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 83.1 | 84.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 47.8 | 49.1 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 56.1 | 57.3 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD PCM | 60.8 | 61.8 | 0 | 8 | 56.2 | 8 | pass |
| DUAL FILTER PCM | 61.2 | 62.2 | 0 | 7 | 55.6 | 8 | pass |
| STACK PCM | 65.3 | 66.6 | 0 | 6 | 51.6 | 8 | pass |
| BINAURAL PCM | 59.2 | 60.3 | 0 | 6 | 56.2 | 8 | pass |
| DUALOSC MULTI H PCM | 54.6 | 55.9 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 68.1 | 69.5 | 0 | 7 | 45.2 | 9 | pass |
| BINAUR MULTI H PCM | 67.5 | 68.7 | 0 | 6 | 44.9 | 9 | pass |
| SINGLE RS | 53.1 | 54.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE RS PCM | 51.0 | 53.1 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC RS | 66.5 | 68.0 | 0 | 6 | 50.5 | 9 | pass |
| DUALOSC RS PCM | 59.5 | 61.1 | 0 | 6 | 55.2 | 8 | pass |
| SEQ+ARP HOME PCM | 65.0 | 68.6 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 57.0 | 57.9 | 0 | 8 | 57.2 | 7 | pass |
| SINGLE DRV HARD | 57.2 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| SINGLE DRV FOLD | 57.1 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| SINGLE DRV TAPE | 58.0 | 59.0 | 0 | 8 | 57.2 | 7 | pass |
| SINGLE DRV TUBE | 57.9 | 58.9 | 0 | 8 | 56.9 | 7 | pass |
| SINGLE DRV OD | 57.9 | 58.9 | 0 | 8 | 57.2 | 7 | pass |
| STACK HEAVY TUBE | 77.8 | 79.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 81.2 | 82.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 64.4 | 65.3 | 0 | 8 | 52.2 | 8 | pass |
| SEQ+ARP HOME TUBE | 72.3 | 76.3 | 0 | 8 | 42.9 | 2 | FAIL |
| FULL | 57.2 | 58.2 | 0 | 4 | 57.5 | 7 | pass |
| FULL HEAVY | 56.3 | 57.3 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H | 58.0 | 59.1 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 63.7 | 64.8 | 0 | 4 | 52.2 | 8 | pass |
| FULL H CEL+PP | 62.9 | 64.2 | 0 | 4 | 51.9 | 8 | pass |
| FULL MULTI H PCM | 50.0 | 51.3 | 0 | 4 | 57.3 | 1 | pass |
| FULL SH2 H PCM | 56.0 | 57.2 | 0 | 4 | 57.5 | 7 | pass |
| FULL SH2 CEL+PP | 68.4 | 69.8 | 0 | 4 | 47.2 | 9 | pass |
| DSPRD | 70.9 | 72.0 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD HEAVY | 61.0 | 62.1 | 0 | 4 | 55.2 | 8 | pass |
| DSPRD MULTI H | 62.3 | 63.2 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD SH2 H | 64.3 | 65.5 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD H CEL+PP | 66.5 | 67.7 | 0 | 4 | 50.9 | 9 | pass |
| DSPRD MULTI H PCM | 54.9 | 56.0 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 CEL+PP | 68.7 | 70.1 | 0 | 4 | 44.9 | 9 | pass |
| SEQ+ARP DSPRD CEL | 74.4 | 78.7 | 0 | 4 | 42.9 | 2 | FAIL |
| IDLE PHASER | 17.3 | 17.9 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.7 | 19.3 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 84.9 | 86.0 | 0 | 8 | 42.9 | 1 | FAIL |
| SPREAD H CEL PHSR | 95.8 | 97.4 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 72.5 | 76.8 | 0 | 8 | 42.9 | 2 | FAIL |
| IDLE ENSEMBLE | 17.8 | 18.3 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.4 | 20.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR | 18.4 | 18.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 20.3 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS | 17.5 | 18.1 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 19.1 | 19.7 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ENPH | 98.1 | 100.1 | 6 | 8 | 33.2 | 24 | FAIL |
| SEQ+ARP HOME ENPH2 | 74.6 | 79.3 | 0 | 8 | 43.2 | 2 | FAIL |
| SPREAD H CEL MIX | 93.6 | 95.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP CEL MIX | 72.6 | 77.1 | 0 | 8 | 43.2 | 2 | FAIL |
| HEAVY SPRING MIX | 62.8 | 63.8 | 0 | 8 | 51.9 | 8 | pass |
| IDLE TAPE FLANGER | 18.6 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE TAPE FLNG x2 | 20.5 | 21.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL TFLG | 97.8 | 99.7 | 0 | 8 | 37.7 | 12 | FAIL |
| SEQ+ARP HOME TFLG2 | 74.5 | 78.5 | 0 | 8 | 43.2 | 2 | FAIL |
| IDLE BBD FLANGER | 17.7 | 18.3 | 0 | 0 | 61.3 | 1 | pass |
| IDLE BBD FLNG x2 | 19.2 | 19.8 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL BBDF | 96.6 | 98.1 | 0 | 8 | 42.9 | 12 | FAIL |
| IDLE PITCH SHIFT | 18.1 | 18.7 | 0 | 0 | 61.3 | 1 | pass |
| PITCH SHIFT HELD | 57.8 | 59.6 | 0 | 8 | 57.2 | 8 | pass |
| IDLE PITCH SHFT x2 | 20.4 | 21.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 97.0 | 99.2 | 0 | 8 | 40.2 | 12 | FAIL |

### 2026-11-02 18:40, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.1 | 15.6 | 0 | 0 | 61.9 | 2 | pass |
| SINGLE 1V | 25.7 | 26.6 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 52.0 | 52.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 56.6 | 57.6 | 0 | 8 | 57.2 | 7 | pass |
| DUAL OSC | 68.1 | 69.1 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD | 86.1 | 87.3 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FILTER | 65.3 | 66.4 | 0 | 7 | 51.5 | 8 | pass |
| STACK | 70.8 | 72.0 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAURAL | 65.7 | 66.7 | 0 | 6 | 51.6 | 8 | pass |
| DUAL OSC HEAVY | 66.5 | 67.5 | 0 | 6 | 51.2 | 9 | pass |
| SPREAD HEAVY | 90.4 | 91.7 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FLT HEAVY | 68.5 | 69.6 | 0 | 7 | 46.2 | 9 | pass |
| STACK HEAVY | 68.0 | 69.0 | 0 | 6 | 44.9 | 9 | pass |
| BINAURAL HEAVY | 72.0 | 73.1 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE LADDER | 52.0 | 53.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 49.8 | 51.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 57.2 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| SINGLE FORMANT | 50.0 | 50.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 65.3 | 66.3 | 0 | 7 | 51.9 | 8 | pass |
| SEQ+ARP HOME | 64.3 | 68.3 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 64.5 | 68.4 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 63.8 | 67.4 | 0 | 8 | 12.8 | 127 | pass |
| SEQ+ARP MOD1 | 64.2 | 69.2 | 0 | 8 | 14.9 | 70 | pass |
| LADDER FILTER PG | 52.0 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY MOD+PP | 60.1 | 61.2 | 0 | 8 | 55.2 | 8 | pass |
| HEAVY CELEST+PP | 60.6 | 61.9 | 0 | 8 | 55.6 | 8 | pass |
| SEQ+ARP CELEST+PP | 68.6 | 72.9 | 0 | 8 | 45.2 | 10 | pass |
| SINGLE MULTI H | 58.8 | 59.5 | 0 | 8 | 57.2 | 8 | pass |
| DUALOSC MULTI H | 67.6 | 68.6 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD MULTI H | 92.8 | 93.9 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT MULTI H | 72.4 | 73.5 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK MULTI H | 70.7 | 71.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR MULTI H | 74.3 | 75.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 96.7 | 98.3 | 0 | 8 | 42.4 | 12 | FAIL |
| DUALOSC H CEL+PP | 72.0 | 73.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H TUM+PP | 95.5 | 96.8 | 0 | 8 | 43.2 | 12 | FAIL |
| SEQ+ARP SPRD CEL | 106.1 | 108.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 59.1 | 60.1 | 0 | 8 | 56.2 | 8 | pass |
| HEAVY CEL+BBD PP | 61.3 | 62.4 | 0 | 8 | 53.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 67.8 | 71.9 | 0 | 8 | 46.9 | 10 | pass |
| STRIKE | 53.0 | 53.9 | 0 | 8 | 57.5 | 1 | pass |
| STRIKE HEAVY | 57.6 | 58.6 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP STRIKE | 73.1 | 77.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 74.1 | 78.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 63.7 | 64.7 | 0 | 8 | 52.2 | 8 | pass |
| DUALOSC SH2 H | 71.5 | 72.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD SH2 H | 97.5 | 98.9 | 0 | 8 | 42.7 | 12 | FAIL |
| DUALFLT SH2 H | 81.9 | 83.3 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 78.1 | 79.3 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 81.5 | 82.8 | 0 | 6 | 43.2 | 1 | FAIL |
| STRIKE SH2 H | 64.6 | 65.6 | 0 | 8 | 51.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 102.4 | 103.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 CEL | 86.4 | 87.7 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.4 | 80.0 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 81.9 | 83.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 81.5 | 82.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 47.9 | 48.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 55.8 | 56.9 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD PCM | 60.9 | 61.8 | 0 | 8 | 55.5 | 8 | pass |
| DUAL FILTER PCM | 60.9 | 62.1 | 0 | 7 | 55.9 | 8 | pass |
| STACK PCM | 64.6 | 65.9 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 58.9 | 59.8 | 0 | 6 | 56.9 | 8 | pass |
| DUALOSC MULTI H PCM | 54.8 | 55.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 67.8 | 69.0 | 0 | 7 | 44.5 | 9 | pass |
| BINAUR MULTI H PCM | 67.2 | 68.3 | 0 | 6 | 45.9 | 9 | pass |
| SINGLE RS | 52.1 | 53.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.1 | 53.1 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 68.1 | 69.8 | 0 | 6 | 44.5 | 9 | pass |
| DUALOSC RS PCM | 59.5 | 61.3 | 0 | 6 | 55.9 | 8 | pass |
| SEQ+ARP HOME PCM | 64.8 | 68.6 | 0 | 8 | 51.5 | 10 | pass |
| SINGLE DRV SOFT | 55.8 | 56.7 | 0 | 8 | 57.6 | 7 | pass |
| SINGLE DRV HARD | 56.0 | 57.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 55.9 | 56.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 56.8 | 57.7 | 0 | 8 | 57.6 | 7 | pass |
| SINGLE DRV TUBE | 56.6 | 57.5 | 0 | 8 | 57.6 | 7 | pass |
| SINGLE DRV OD | 56.7 | 57.6 | 0 | 8 | 57.6 | 7 | pass |
| STACK HEAVY TUBE | 76.0 | 77.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 78.8 | 80.0 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 62.3 | 63.4 | 0 | 8 | 52.6 | 8 | pass |
| SEQ+ARP HOME TUBE | 69.3 | 74.1 | 0 | 8 | 44.2 | 10 | pass |
| FULL | 57.8 | 58.8 | 0 | 4 | 57.2 | 7 | pass |
| FULL HEAVY | 56.8 | 57.7 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H | 58.3 | 59.2 | 0 | 4 | 57.2 | 8 | pass |
| FULL SH2 H | 63.5 | 64.6 | 0 | 4 | 52.2 | 8 | pass |
| FULL H CEL+PP | 63.4 | 64.7 | 0 | 4 | 52.2 | 8 | pass |
| FULL MULTI H PCM | 49.9 | 51.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 54.7 | 56.0 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 CEL+PP | 68.3 | 69.8 | 0 | 4 | 46.2 | 9 | pass |
| DSPRD | 75.9 | 77.2 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD HEAVY | 62.7 | 63.8 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD MULTI H | 63.9 | 64.8 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD SH2 H | 66.4 | 67.6 | 0 | 4 | 51.5 | 9 | pass |
| DSPRD H CEL+PP | 68.4 | 70.0 | 0 | 4 | 45.2 | 9 | pass |
| DSPRD MULTI H PCM | 54.8 | 55.8 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 CEL+PP | 70.8 | 72.5 | 0 | 4 | 43.2 | 1 | FAIL |
| SEQ+ARP DSPRD CEL | 76.8 | 80.4 | 0 | 4 | 42.9 | 2 | FAIL |
| IDLE PHASER | 17.4 | 18.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE PHASER x2 | 18.9 | 19.5 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD PHASER | 90.2 | 91.3 | 0 | 8 | 42.9 | 1 | FAIL |
| SPREAD H CEL PHSR | 102.4 | 104.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PHSR2 | 71.0 | 74.9 | 0 | 8 | 43.5 | 10 | FAIL |
| IDLE ENSEMBLE | 17.8 | 18.4 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.4 | 20.0 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR | 18.4 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR x2 | 20.3 | 20.9 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS | 17.7 | 18.3 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS x2 | 19.3 | 19.9 | 0 | 0 | 61.2 | 1 | pass |
| SPREAD H CEL ENPH | 104.4 | 105.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME ENPH2 | 73.1 | 76.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SPREAD H CEL MIX | 98.1 | 100.1 | 5 | 8 | 35.9 | 24 | FAIL |
| SEQ+ARP CEL MIX | 70.2 | 74.2 | 0 | 8 | 44.2 | 10 | FAIL |
| HEAVY SPRING MIX | 61.3 | 62.3 | 0 | 8 | 54.9 | 8 | pass |
| IDLE TAPE FLANGER | 19.0 | 19.6 | 0 | 0 | 61.6 | 1 | pass |
| IDLE TAPE FLNG x2 | 20.9 | 21.5 | 0 | 0 | 61.2 | 1 | pass |
| SPREAD H CEL TFLG | 105.3 | 106.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TFLG2 | 72.6 | 76.0 | 0 | 8 | 43.2 | 11 | FAIL |
| IDLE BBD FLANGER | 17.7 | 18.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.2 | 19.9 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL BBDF | 103.0 | 104.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE PITCH SHIFT | 18.2 | 18.8 | 0 | 0 | 61.6 | 1 | pass |
| PITCH SHIFT HELD | 56.6 | 58.2 | 0 | 8 | 57.3 | 7 | pass |
| IDLE PITCH SHFT x2 | 20.5 | 21.4 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL PSHF | 103.0 | 104.7 | 8 | 8 | – | – | FAIL (cut by the guard) |

### 2026-11-02 18:31, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.1 | 15.6 | 0 | 0 | 62.3 | 2 | pass |
| SINGLE 1V | 23.2 | 24.1 | 0 | 1 | 60.6 | 3 | pass |
| SINGLE | 43.0 | 43.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 47.3 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 61.7 | 62.8 | 0 | 6 | 53.9 | 8 | pass |
| SPREAD | 53.2 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FILTER | 56.2 | 57.1 | 0 | 7 | 57.2 | 7 | pass |
| STACK | 63.1 | 64.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 52.2 | 53.0 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 60.4 | 61.3 | 0 | 6 | 53.5 | 8 | pass |
| SPREAD HEAVY | 57.2 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FLT HEAVY | 58.9 | 59.8 | 0 | 7 | 57.6 | 8 | pass |
| STACK HEAVY | 60.4 | 61.4 | 0 | 6 | 55.2 | 8 | pass |
| BINAURAL HEAVY | 57.8 | 58.7 | 0 | 6 | 57.6 | 7 | pass |
| SINGLE LADDER | 43.0 | 43.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 40.8 | 41.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 48.0 | 48.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 41.0 | 42.0 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 56.2 | 57.0 | 0 | 7 | 57.5 | 7 | pass |
| SEQ+ARP HOME | 55.0 | 59.0 | 0 | 8 | 57.5 | 9 | pass |
| SEQ+ARP AMP | 55.2 | 58.9 | 0 | 8 | 57.2 | 10 | pass |
| SEQ+ARP SEQ | 54.5 | 58.8 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 54.7 | 60.1 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 43.0 | 43.9 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 50.8 | 51.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 51.6 | 52.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 59.8 | 63.9 | 0 | 8 | 56.2 | 9 | pass |
| SINGLE MULTI H | 49.5 | 50.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 61.7 | 62.7 | 0 | 6 | 53.2 | 8 | pass |
| SPREAD MULTI H | 59.8 | 60.6 | 0 | 8 | 55.9 | 8 | pass |
| DUALFLT MULTI H | 62.9 | 63.8 | 0 | 7 | 52.2 | 8 | pass |
| STACK MULTI H | 63.5 | 64.6 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 60.5 | 61.5 | 0 | 6 | 54.2 | 8 | pass |
| SPREAD H CEL+PP | 64.5 | 65.7 | 0 | 8 | 51.9 | 8 | pass |
| DUALOSC H CEL+PP | 66.6 | 67.9 | 0 | 6 | 51.6 | 9 | pass |
| SPREAD H TUM+PP | 63.0 | 64.1 | 0 | 8 | 52.2 | 8 | pass |
| SEQ+ARP SPRD CEL | 72.1 | 75.9 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 49.8 | 50.9 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 52.1 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 58.6 | 62.6 | 0 | 8 | 56.9 | 9 | pass |
| STRIKE | 43.9 | 44.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.3 | 49.2 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP STRIKE | 64.0 | 68.0 | 0 | 8 | 51.5 | 10 | pass |
| SEQ+ARP STRK FILT | 65.0 | 69.4 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE SH2 H | 54.5 | 55.5 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 65.9 | 66.8 | 0 | 6 | 50.9 | 8 | pass |
| SPREAD SH2 H | 64.8 | 65.7 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT SH2 H | 72.4 | 73.7 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 70.4 | 71.6 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAUR SH2 H | 68.1 | 69.1 | 0 | 6 | 45.2 | 9 | pass |
| STRIKE SH2 H | 55.3 | 56.4 | 0 | 8 | 57.6 | 1 | pass |
| SPREAD SH2 CEL+PP | 69.3 | 71.1 | 0 | 8 | 43.2 | 1 | pass |
| DUALFLT SH2 CEL | 76.9 | 78.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 66.4 | 70.3 | 0 | 8 | 49.9 | 10 | pass |
| DUALFLT SH2 HP | 72.4 | 73.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 68.1 | 69.2 | 0 | 6 | 45.9 | 9 | pass |
| SINGLE PCM | 47.8 | 49.0 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.8 | 57.1 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 60.8 | 61.9 | 0 | 8 | 54.9 | 8 | pass |
| DUAL FILTER PCM | 60.9 | 61.9 | 0 | 7 | 56.2 | 8 | pass |
| STACK PCM | 64.6 | 65.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 58.8 | 59.9 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC MULTI H PCM | 54.7 | 55.8 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 67.8 | 69.1 | 0 | 7 | 44.6 | 9 | pass |
| BINAUR MULTI H PCM | 67.2 | 68.5 | 0 | 6 | 46.6 | 9 | pass |
| SINGLE RS | 43.1 | 45.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.1 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 61.8 | 63.5 | 0 | 6 | 54.2 | 8 | pass |
| DUALOSC RS PCM | 59.5 | 61.0 | 0 | 6 | 56.2 | 8 | pass |
| SEQ+ARP HOME PCM | 64.8 | 68.8 | 0 | 8 | 52.2 | 10 | pass |
| SINGLE DRV SOFT | 46.8 | 47.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 47.0 | 47.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 46.8 | 47.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 47.8 | 48.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 47.6 | 48.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 47.7 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 67.5 | 68.7 | 0 | 6 | 44.9 | 9 | pass |
| BINAUR HEAVY OD | 64.4 | 65.3 | 0 | 6 | 52.2 | 8 | pass |
| STRIKE HEAVY TUBE | 52.8 | 53.9 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 60.1 | 63.9 | 0 | 8 | 55.2 | 9 | pass |
| FULL | 53.5 | 54.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 52.0 | 52.9 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 53.8 | 54.8 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 59.1 | 60.2 | 0 | 4 | 56.2 | 8 | pass |
| FULL H CEL+PP | 59.1 | 60.3 | 0 | 4 | 56.2 | 8 | pass |
| FULL MULTI H PCM | 49.8 | 51.0 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 54.7 | 55.9 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 CEL+PP | 64.3 | 65.6 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 42.9 | 43.8 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 45.3 | 46.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 46.6 | 47.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 49.2 | 50.1 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 51.6 | 52.9 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 54.8 | 55.8 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 CEL+PP | 54.2 | 55.7 | 0 | 4 | 57.5 | 1 | pass |
| SEQ+ARP DSPRD CEL | 59.7 | 63.6 | 0 | 4 | 56.5 | 9 | pass |
| IDLE PHASER | 17.4 | 18.1 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.9 | 19.5 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD PHASER | 57.6 | 58.5 | 0 | 8 | 57.2 | 1 | pass |
| SPREAD H CEL PHSR | 68.7 | 70.0 | 0 | 8 | 44.5 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 61.8 | 65.5 | 0 | 8 | 52.9 | 10 | pass |
| IDLE ENSEMBLE | 17.8 | 18.4 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.4 | 20.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 18.4 | 19.1 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 20.3 | 21.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS | 17.7 | 18.3 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS x2 | 19.2 | 19.9 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 70.8 | 72.5 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 63.6 | 67.5 | 0 | 8 | 51.9 | 10 | pass |
| SPREAD H CEL MIX | 66.5 | 67.7 | 0 | 8 | 51.2 | 9 | pass |
| SEQ+ARP CEL MIX | 61.4 | 65.8 | 0 | 8 | 53.9 | 10 | pass |
| HEAVY SPRING MIX | 52.0 | 53.2 | 0 | 8 | 57.2 | 1 | pass |
| IDLE TAPE FLANGER | 19.0 | 19.7 | 0 | 0 | 61.3 | 1 | pass |
| IDLE TAPE FLNG x2 | 20.9 | 21.5 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 71.0 | 72.4 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 63.4 | 67.3 | 0 | 8 | 52.2 | 10 | pass |
| IDLE BBD FLANGER | 17.7 | 18.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.2 | 19.9 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL BBDF | 69.4 | 70.9 | 0 | 8 | 43.2 | 9 | pass |
| IDLE PITCH SHIFT | 18.2 | 18.9 | 0 | 0 | 61.3 | 3 | pass |
| PITCH SHIFT HELD | 47.3 | 48.8 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 20.5 | 21.3 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL PSHF | 69.5 | 71.5 | 0 | 8 | 43.2 | 9 | pass |

### 2026-11-02 18:00, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.2 | 15.7 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 23.0 | 23.8 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 42.8 | 43.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 47.2 | 48.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 56.3 | 57.3 | 0 | 6 | 57.2 | 1 | pass |
| SPREAD | 53.4 | 54.3 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FILTER | 56.7 | 57.7 | 0 | 7 | 57.2 | 7 | pass |
| STACK | 58.5 | 59.6 | 0 | 6 | 57.5 | 8 | pass |
| BINAURAL | 52.1 | 53.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 58.9 | 60.0 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD HEAVY | 57.5 | 58.5 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 58.8 | 59.8 | 0 | 7 | 57.2 | 8 | pass |
| STACK HEAVY | 59.1 | 60.2 | 0 | 6 | 56.2 | 8 | pass |
| BINAURAL HEAVY | 57.1 | 58.0 | 0 | 6 | 57.2 | 1 | pass |
| SINGLE LADDER | 42.8 | 43.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 40.7 | 41.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 48.3 | 49.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.9 | 41.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 56.6 | 57.6 | 0 | 7 | 57.5 | 7 | pass |
| SEQ+ARP HOME | 55.0 | 58.7 | 0 | 8 | 57.2 | 9 | pass |
| SEQ+ARP AMP | 55.3 | 58.6 | 0 | 8 | 57.2 | 10 | pass |
| SEQ+ARP SEQ | 54.5 | 58.1 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 54.9 | 59.5 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 42.8 | 43.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 51.0 | 52.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 51.8 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 60.1 | 63.6 | 0 | 8 | 55.9 | 9 | pass |
| SINGLE MULTI H | 49.4 | 50.2 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.1 | 61.1 | 0 | 6 | 54.9 | 8 | pass |
| SPREAD MULTI H | 60.1 | 61.0 | 0 | 8 | 54.9 | 8 | pass |
| DUALFLT MULTI H | 63.4 | 64.3 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 62.6 | 63.7 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 60.4 | 61.5 | 0 | 6 | 54.9 | 8 | pass |
| SPREAD H CEL+PP | 64.3 | 65.5 | 0 | 8 | 52.2 | 8 | pass |
| DUALOSC H CEL+PP | 64.6 | 66.2 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 62.6 | 63.7 | 0 | 8 | 52.6 | 8 | pass |
| SEQ+ARP SPRD CEL | 72.5 | 76.4 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 49.6 | 50.6 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 52.0 | 53.3 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 58.0 | 61.8 | 0 | 8 | 56.9 | 9 | pass |
| STRIKE | 43.7 | 44.7 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.3 | 49.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 64.8 | 68.8 | 0 | 8 | 51.5 | 10 | pass |
| SEQ+ARP STRK FILT | 65.8 | 70.0 | 0 | 8 | 51.2 | 10 | pass |
| SINGLE SH2 H | 54.8 | 55.8 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC SH2 H | 64.2 | 65.4 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 65.3 | 66.2 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT SH2 H | 72.9 | 74.0 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 69.6 | 70.9 | 0 | 6 | 42.9 | 1 | pass |
| BINAUR SH2 H | 68.0 | 69.2 | 0 | 6 | 45.2 | 9 | pass |
| STRIKE SH2 H | 56.0 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| SPREAD SH2 CEL+PP | 69.9 | 71.4 | 0 | 8 | 42.9 | 9 | pass |
| DUALFLT SH2 CEL | 77.6 | 78.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 68.2 | 72.4 | 0 | 8 | 46.3 | 10 | pass |
| DUALFLT SH2 HP | 72.8 | 74.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 68.0 | 69.1 | 0 | 6 | 44.6 | 9 | pass |
| SINGLE PCM | 47.8 | 49.1 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.7 | 56.9 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD PCM | 61.9 | 62.8 | 0 | 8 | 52.9 | 8 | pass |
| DUAL FILTER PCM | 61.6 | 62.7 | 0 | 7 | 53.9 | 8 | pass |
| STACK PCM | 65.1 | 66.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL PCM | 59.7 | 60.8 | 0 | 6 | 56.6 | 8 | pass |
| DUALOSC MULTI H PCM | 54.4 | 55.5 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 68.4 | 69.6 | 0 | 7 | 46.2 | 9 | pass |
| BINAUR MULTI H PCM | 68.3 | 69.5 | 0 | 6 | 45.6 | 9 | pass |
| SINGLE RS | 42.8 | 44.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.0 | 53.0 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 56.3 | 58.1 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.2 | 61.0 | 0 | 6 | 55.9 | 8 | pass |
| SEQ+ARP HOME PCM | 64.2 | 68.2 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 46.7 | 47.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 46.8 | 48.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 46.6 | 47.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 47.5 | 48.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 47.3 | 48.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 47.4 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 65.2 | 66.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR HEAVY OD | 63.5 | 64.6 | 0 | 6 | 51.9 | 8 | pass |
| STRIKE HEAVY TUBE | 52.5 | 53.6 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 60.2 | 63.9 | 0 | 8 | 56.2 | 9 | pass |
| FULL | 50.2 | 51.3 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 51.1 | 52.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 53.3 | 54.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 58.5 | 59.7 | 0 | 4 | 57.5 | 8 | pass |
| FULL H CEL+PP | 58.0 | 59.4 | 0 | 4 | 57.6 | 8 | pass |
| FULL MULTI H PCM | 50.2 | 51.3 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 56.5 | 57.6 | 0 | 4 | 57.5 | 7 | pass |
| FULL SH2 CEL+PP | 63.4 | 64.7 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 43.2 | 44.3 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 45.6 | 46.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 46.9 | 47.9 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 49.7 | 50.8 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 51.5 | 52.8 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 55.8 | 56.9 | 0 | 4 | 57.5 | 7 | pass |
| DSPRD SH2 CEL+PP | 54.8 | 56.1 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 60.4 | 64.8 | 0 | 4 | 54.5 | 9 | pass |
| IDLE PHASER | 17.9 | 18.5 | 0 | 0 | 61.3 | 1 | pass |
| IDLE PHASER x2 | 19.4 | 20.0 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 58.8 | 59.7 | 0 | 8 | 56.8 | 8 | pass |
| SPREAD H CEL PHSR | 69.0 | 70.4 | 0 | 8 | 43.5 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 61.5 | 65.1 | 0 | 8 | 53.6 | 9 | pass |
| IDLE ENSEMBLE | 18.1 | 18.6 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 19.7 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR | 18.7 | 19.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR x2 | 20.6 | 21.1 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS | 18.1 | 18.6 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 19.7 | 20.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL ENPH | 70.5 | 72.1 | 0 | 8 | 43.2 | 9 | FAIL |
| SEQ+ARP HOME ENPH2 | 63.4 | 67.3 | 0 | 8 | 51.9 | 10 | pass |
| SPREAD H CEL MIX | 66.0 | 67.3 | 0 | 8 | 51.6 | 8 | pass |
| SEQ+ARP CEL MIX | 61.8 | 66.1 | 0 | 8 | 53.6 | 10 | pass |
| HEAVY SPRING MIX | 51.5 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| IDLE TAPE FLANGER | 19.8 | 20.5 | 0 | 0 | 61.3 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.7 | 22.4 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 71.5 | 73.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 63.3 | 67.3 | 0 | 8 | 51.9 | 10 | pass |
| IDLE BBD FLANGER | 18.1 | 18.8 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.6 | 20.2 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 71.2 | 72.4 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE PITCH SHIFT | 18.6 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 48.0 | 49.6 | 0 | 8 | 57.2 | 1 | pass |
| IDLE PITCH SHFT x2 | 20.9 | 21.7 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL PSHF | 70.1 | 72.0 | 0 | 8 | 43.2 | 9 | FAIL |

### 2026-11-02 17:47, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.2 | 15.7 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 22.9 | 23.6 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 42.6 | 43.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 47.1 | 48.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 56.3 | 57.4 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD | 53.5 | 54.6 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FILTER | 56.5 | 57.5 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 58.3 | 59.3 | 0 | 6 | 57.5 | 8 | pass |
| BINAURAL | 52.0 | 53.0 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 59.0 | 60.1 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD HEAVY | 57.6 | 58.5 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FLT HEAVY | 58.7 | 59.8 | 0 | 7 | 57.5 | 8 | pass |
| STACK HEAVY | 59.1 | 60.2 | 0 | 6 | 56.5 | 8 | pass |
| BINAURAL HEAVY | 56.9 | 57.9 | 0 | 6 | 57.6 | 7 | pass |
| SINGLE LADDER | 42.6 | 43.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 40.7 | 41.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 48.0 | 49.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 40.8 | 41.8 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 56.4 | 57.5 | 0 | 7 | 57.5 | 7 | pass |
| SEQ+ARP HOME | 54.7 | 58.4 | 0 | 8 | 57.6 | 9 | pass |
| SEQ+ARP AMP | 54.9 | 58.6 | 0 | 8 | 57.5 | 10 | pass |
| SEQ+ARP SEQ | 54.2 | 57.6 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 54.6 | 59.3 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 42.6 | 43.6 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 51.1 | 52.1 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 51.8 | 53.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 59.8 | 63.3 | 0 | 8 | 56.2 | 9 | pass |
| SINGLE MULTI H | 49.3 | 50.2 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.1 | 61.1 | 0 | 6 | 54.5 | 8 | pass |
| SPREAD MULTI H | 60.2 | 61.1 | 0 | 8 | 55.6 | 8 | pass |
| DUALFLT MULTI H | 63.1 | 64.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 62.6 | 63.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 60.3 | 61.4 | 0 | 6 | 54.9 | 8 | pass |
| SPREAD H CEL+PP | 64.5 | 65.8 | 0 | 8 | 51.9 | 8 | pass |
| DUALOSC H CEL+PP | 64.7 | 66.2 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 62.9 | 64.1 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 72.4 | 76.7 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 49.5 | 50.4 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 52.1 | 53.2 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 57.8 | 61.5 | 0 | 8 | 57.2 | 9 | pass |
| STRIKE | 43.8 | 44.8 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 48.2 | 49.2 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 64.8 | 69.2 | 0 | 8 | 51.2 | 10 | pass |
| SEQ+ARP STRK FILT | 65.8 | 70.3 | 0 | 8 | 50.2 | 10 | pass |
| SINGLE SH2 H | 54.5 | 55.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC SH2 H | 64.3 | 65.3 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD SH2 H | 65.1 | 66.1 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT SH2 H | 72.8 | 74.1 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 69.4 | 70.8 | 0 | 6 | 43.3 | 9 | pass |
| BINAUR SH2 H | 67.7 | 68.8 | 0 | 6 | 43.9 | 9 | pass |
| STRIKE SH2 H | 55.8 | 56.7 | 0 | 8 | 57.6 | 1 | pass |
| SPREAD SH2 CEL+PP | 69.8 | 71.3 | 0 | 8 | 42.9 | 1 | pass |
| DUALFLT SH2 CEL | 77.6 | 79.1 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 67.8 | 72.3 | 0 | 8 | 45.9 | 10 | pass |
| DUALFLT SH2 HP | 72.9 | 74.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 67.7 | 68.8 | 0 | 6 | 43.2 | 9 | pass |
| SINGLE PCM | 47.7 | 49.0 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.5 | 56.7 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD PCM | 61.5 | 62.6 | 0 | 8 | 54.2 | 8 | pass |
| DUAL FILTER PCM | 61.2 | 62.4 | 0 | 7 | 55.9 | 8 | pass |
| STACK PCM | 64.7 | 65.9 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL PCM | 59.3 | 60.3 | 0 | 6 | 56.9 | 8 | pass |
| DUALOSC MULTI H PCM | 54.3 | 55.5 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 67.8 | 69.2 | 0 | 7 | 45.2 | 9 | pass |
| BINAUR MULTI H PCM | 67.7 | 69.0 | 0 | 6 | 45.2 | 9 | pass |
| SINGLE RS | 42.7 | 44.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 50.9 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 56.3 | 58.0 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.0 | 60.7 | 0 | 6 | 56.6 | 8 | pass |
| SEQ+ARP HOME PCM | 63.9 | 67.8 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 46.4 | 47.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 46.6 | 47.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 46.5 | 47.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 47.4 | 48.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 47.2 | 48.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 47.3 | 48.3 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 65.5 | 66.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR HEAVY OD | 63.3 | 64.4 | 0 | 6 | 51.9 | 8 | pass |
| STRIKE HEAVY TUBE | 52.5 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 60.0 | 63.9 | 0 | 8 | 55.2 | 9 | pass |
| FULL | 50.2 | 51.2 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 51.1 | 52.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 53.1 | 54.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 58.5 | 59.6 | 0 | 4 | 57.5 | 8 | pass |
| FULL H CEL+PP | 58.1 | 59.4 | 0 | 4 | 57.6 | 8 | pass |
| FULL MULTI H PCM | 49.7 | 50.6 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 55.4 | 56.4 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 CEL+PP | 63.4 | 64.6 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 43.3 | 44.4 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 45.6 | 46.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 47.0 | 48.0 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 49.8 | 50.9 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 51.6 | 53.0 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 55.4 | 56.6 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 CEL+PP | 54.7 | 56.0 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 60.0 | 63.3 | 0 | 4 | 55.2 | 9 | pass |
| IDLE PHASER | 17.8 | 18.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 19.2 | 19.9 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 58.3 | 59.3 | 0 | 8 | 57.2 | 8 | pass |
| SPREAD H CEL PHSR | 68.9 | 70.3 | 0 | 8 | 44.5 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 61.2 | 65.1 | 0 | 8 | 53.2 | 10 | pass |
| IDLE ENSEMBLE | 18.1 | 18.6 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENSEMBLE x2 | 19.7 | 20.3 | 0 | 0 | 61.2 | 3 | pass |
| IDLE ENS+PHSR | 18.7 | 19.3 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR x2 | 20.6 | 21.2 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS | 18.1 | 18.6 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 19.6 | 20.1 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL ENPH | 70.3 | 72.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 63.2 | 67.1 | 0 | 8 | 52.2 | 10 | pass |
| SPREAD H CEL MIX | 66.1 | 67.3 | 0 | 8 | 51.6 | 9 | pass |
| SEQ+ARP CEL MIX | 61.5 | 65.3 | 0 | 8 | 53.6 | 10 | pass |
| HEAVY SPRING MIX | 51.7 | 52.8 | 0 | 8 | 57.3 | 1 | pass |
| IDLE TAPE FLANGER | 19.6 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.5 | 22.2 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL TFLG | 70.9 | 72.5 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 62.9 | 66.6 | 0 | 8 | 51.9 | 10 | pass |
| IDLE BBD FLANGER | 18.1 | 18.8 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.6 | 20.3 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD H CEL BBDF | 70.6 | 72.0 | 0 | 8 | 43.5 | 9 | FAIL |
| IDLE PITCH SHIFT | 19.4 | 20.5 | 0 | 0 | 61.6 | 3 | pass |
| PITCH SHIFT HELD | 48.4 | 51.4 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 22.5 | 23.9 | 0 | 0 | 60.9 | 3 | pass |
| SPREAD H CEL PSHF | 70.4 | 73.0 | 0 | 8 | 43.5 | 9 | FAIL |

### 2026-11-02 16:15, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.1 | 15.7 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 23.1 | 23.9 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 42.8 | 43.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 47.2 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 56.3 | 57.3 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD | 53.8 | 54.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FILTER | 56.3 | 57.2 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 58.1 | 59.2 | 0 | 6 | 57.2 | 8 | pass |
| BINAURAL | 52.6 | 53.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 59.1 | 60.2 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD HEAVY | 57.7 | 58.7 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FLT HEAVY | 58.6 | 59.6 | 0 | 7 | 57.6 | 8 | pass |
| STACK HEAVY | 58.9 | 60.1 | 0 | 6 | 56.9 | 8 | pass |
| BINAURAL HEAVY | 57.3 | 58.4 | 0 | 6 | 57.6 | 1 | pass |
| SINGLE LADDER | 42.8 | 43.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 40.7 | 41.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 48.1 | 49.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 40.9 | 41.8 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 56.3 | 57.4 | 0 | 7 | 57.5 | 7 | pass |
| SEQ+ARP HOME | 55.0 | 59.0 | 0 | 8 | 57.5 | 9 | pass |
| SEQ+ARP AMP | 55.2 | 58.8 | 0 | 8 | 57.2 | 10 | pass |
| SEQ+ARP SEQ | 54.5 | 58.6 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 55.0 | 60.0 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 42.8 | 43.6 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 51.2 | 52.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 52.0 | 53.2 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 60.2 | 63.8 | 0 | 8 | 55.9 | 9 | pass |
| SINGLE MULTI H | 49.4 | 50.3 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.0 | 61.1 | 0 | 6 | 54.9 | 8 | pass |
| SPREAD MULTI H | 60.4 | 61.4 | 0 | 8 | 54.9 | 8 | pass |
| DUALFLT MULTI H | 63.0 | 64.1 | 0 | 7 | 52.2 | 8 | pass |
| STACK MULTI H | 62.4 | 63.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 60.8 | 62.0 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD H CEL+PP | 64.5 | 65.7 | 0 | 8 | 51.9 | 8 | pass |
| DUALOSC H CEL+PP | 64.8 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 63.1 | 64.3 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 72.4 | 76.2 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 49.3 | 50.4 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 52.3 | 53.4 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 58.5 | 61.8 | 0 | 8 | 56.5 | 9 | pass |
| STRIKE | 44.1 | 45.0 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.5 | 49.6 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 65.1 | 69.1 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP STRK FILT | 65.9 | 70.1 | 0 | 8 | 50.5 | 10 | pass |
| SINGLE SH2 H | 54.6 | 55.6 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 64.1 | 65.2 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 65.1 | 66.1 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT SH2 H | 72.7 | 74.0 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 69.2 | 70.6 | 0 | 6 | 43.6 | 9 | pass |
| BINAUR SH2 H | 68.2 | 69.3 | 0 | 6 | 47.6 | 9 | pass |
| STRIKE SH2 H | 55.8 | 56.8 | 0 | 8 | 57.3 | 1 | pass |
| SPREAD SH2 CEL+PP | 69.6 | 71.5 | 0 | 8 | 42.9 | 1 | pass |
| DUALFLT SH2 CEL | 77.4 | 79.0 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 67.4 | 71.4 | 0 | 8 | 47.2 | 10 | pass |
| DUALFLT SH2 HP | 72.7 | 74.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 68.2 | 69.3 | 0 | 6 | 44.6 | 9 | pass |
| SINGLE PCM | 48.1 | 49.2 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 56.2 | 57.3 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD PCM | 62.2 | 63.4 | 0 | 8 | 52.9 | 8 | pass |
| DUAL FILTER PCM | 61.6 | 62.8 | 0 | 7 | 53.2 | 8 | pass |
| STACK PCM | 65.1 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 59.7 | 60.9 | 0 | 6 | 56.6 | 8 | pass |
| DUALOSC MULTI H PCM | 55.0 | 56.0 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 68.5 | 69.7 | 0 | 7 | 46.2 | 9 | pass |
| BINAUR MULTI H PCM | 68.2 | 69.6 | 0 | 6 | 46.2 | 9 | pass |
| SINGLE RS | 42.9 | 44.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.3 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 56.4 | 58.0 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.6 | 61.1 | 0 | 6 | 56.6 | 8 | pass |
| SEQ+ARP HOME PCM | 64.0 | 67.4 | 0 | 8 | 51.6 | 10 | pass |
| SINGLE DRV SOFT | 46.6 | 47.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 46.8 | 47.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 46.7 | 47.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 47.5 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 47.3 | 48.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 47.4 | 48.3 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 65.5 | 66.9 | 0 | 6 | 51.5 | 8 | pass |
| BINAUR HEAVY OD | 63.8 | 65.0 | 0 | 6 | 51.9 | 8 | pass |
| STRIKE HEAVY TUBE | 52.9 | 54.0 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 61.0 | 64.7 | 0 | 8 | 54.5 | 10 | pass |
| FULL | 49.9 | 50.9 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 51.1 | 52.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 53.1 | 54.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 58.3 | 59.3 | 0 | 4 | 57.2 | 8 | pass |
| FULL H CEL+PP | 57.8 | 59.1 | 0 | 4 | 57.2 | 8 | pass |
| FULL MULTI H PCM | 50.6 | 51.6 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 55.8 | 57.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 CEL+PP | 63.5 | 64.8 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 43.6 | 44.7 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 45.8 | 46.8 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 47.2 | 48.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 49.7 | 50.6 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD H CEL+PP | 51.6 | 52.9 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD MULTI H PCM | 56.5 | 57.7 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 54.5 | 55.9 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 59.9 | 63.1 | 0 | 4 | 56.2 | 9 | pass |
| IDLE PHASER | 17.8 | 18.6 | 0 | 0 | 61.3 | 1 | pass |
| IDLE PHASER x2 | 19.2 | 19.9 | 0 | 0 | 61.3 | 1 | pass |
| SPREAD PHASER | 58.5 | 59.5 | 0 | 8 | 57.2 | 8 | pass |
| SPREAD H CEL PHSR | 69.1 | 70.5 | 0 | 8 | 44.2 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 61.5 | 65.5 | 0 | 8 | 53.9 | 9 | pass |
| IDLE ENSEMBLE | 18.2 | 18.8 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.8 | 20.5 | 0 | 0 | 61.2 | 3 | pass |
| IDLE ENS+PHSR | 18.8 | 19.4 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR x2 | 20.6 | 21.3 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS | 18.0 | 18.5 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS x2 | 19.5 | 20.1 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD H CEL ENPH | 70.3 | 71.7 | 0 | 8 | 43.5 | 9 | FAIL |
| SEQ+ARP HOME ENPH2 | 63.5 | 67.2 | 0 | 8 | 52.2 | 10 | pass |
| SPREAD H CEL MIX | 66.1 | 67.4 | 0 | 8 | 52.2 | 9 | pass |
| SEQ+ARP CEL MIX | 62.1 | 65.8 | 0 | 8 | 53.9 | 10 | pass |
| HEAVY SPRING MIX | 52.0 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| IDLE TAPE FLANGER | 19.5 | 20.3 | 0 | 0 | 61.6 | 3 | pass |
| IDLE TAPE FLNG x2 | 21.4 | 22.0 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 70.8 | 72.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 63.6 | 67.5 | 0 | 8 | 51.9 | 10 | pass |
| IDLE BBD FLANGER | 18.2 | 18.9 | 0 | 0 | 61.6 | 3 | pass |
| IDLE BBD FLNG x2 | 19.7 | 20.4 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL BBDF | 70.6 | 71.9 | 0 | 8 | 43.2 | 1 | FAIL |
| IDLE PITCH SHIFT | 18.9 | 19.8 | 0 | 0 | 61.6 | 1 | pass |
| PITCH SHIFT HELD | 47.3 | 48.8 | 0 | 8 | 57.5 | 1 | pass |
| IDLE PITCH SHFT x2 | 21.5 | 22.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL PSHF | 69.8 | 71.9 | 0 | 8 | 43.2 | 9 | pass |

### 2026-11-02 15:48, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.2 | 15.7 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 23.5 | 24.4 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 43.2 | 44.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE HEAVY | 47.3 | 48.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 57.0 | 58.0 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD | 53.8 | 54.8 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FILTER | 56.4 | 57.4 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 58.6 | 59.7 | 0 | 6 | 57.2 | 8 | pass |
| BINAURAL | 52.2 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.8 | 62.0 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD HEAVY | 57.9 | 58.9 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FLT HEAVY | 58.9 | 60.0 | 0 | 7 | 56.9 | 8 | pass |
| STACK HEAVY | 59.9 | 61.1 | 0 | 6 | 56.2 | 8 | pass |
| BINAURAL HEAVY | 57.5 | 58.5 | 0 | 6 | 57.6 | 1 | pass |
| SINGLE LADDER | 43.2 | 44.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 40.6 | 41.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 48.1 | 49.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 41.1 | 42.1 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 56.4 | 57.4 | 0 | 7 | 57.5 | 7 | pass |
| SEQ+ARP HOME | 54.7 | 58.2 | 0 | 8 | 57.5 | 9 | pass |
| SEQ+ARP AMP | 54.9 | 58.5 | 0 | 8 | 57.2 | 10 | pass |
| SEQ+ARP SEQ | 54.1 | 57.6 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 54.4 | 59.1 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 43.2 | 44.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 51.0 | 52.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 51.7 | 52.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 59.8 | 63.7 | 0 | 8 | 56.2 | 9 | pass |
| SINGLE MULTI H | 49.6 | 50.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.5 | 61.6 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD MULTI H | 60.3 | 61.2 | 0 | 8 | 54.9 | 8 | pass |
| DUALFLT MULTI H | 63.0 | 64.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 62.6 | 63.8 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 60.2 | 61.3 | 0 | 6 | 55.6 | 8 | pass |
| SPREAD H CEL+PP | 64.6 | 65.9 | 0 | 8 | 52.2 | 8 | pass |
| DUALOSC H CEL+PP | 65.0 | 66.7 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 63.0 | 64.1 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 72.1 | 75.8 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 49.7 | 50.7 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 52.4 | 53.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 58.6 | 61.6 | 0 | 8 | 56.9 | 9 | pass |
| STRIKE | 44.2 | 45.1 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.3 | 49.4 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 64.3 | 68.0 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP STRK FILT | 65.1 | 68.9 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE SH2 H | 54.6 | 55.7 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC SH2 H | 64.7 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 65.4 | 66.3 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT SH2 H | 72.5 | 73.7 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 69.8 | 71.2 | 0 | 6 | 42.9 | 1 | pass |
| BINAUR SH2 H | 67.7 | 68.9 | 0 | 6 | 43.9 | 9 | pass |
| STRIKE SH2 H | 55.6 | 56.5 | 0 | 8 | 57.6 | 1 | pass |
| SPREAD SH2 CEL+PP | 70.1 | 71.6 | 0 | 8 | 43.2 | 9 | FAIL |
| DUALFLT SH2 CEL | 77.5 | 79.1 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 67.0 | 71.4 | 0 | 8 | 49.6 | 10 | pass |
| DUALFLT SH2 HP | 72.5 | 73.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 67.8 | 69.0 | 0 | 6 | 44.9 | 9 | pass |
| SINGLE PCM | 48.1 | 49.4 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 56.5 | 57.6 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD PCM | 61.9 | 62.9 | 0 | 8 | 52.9 | 8 | pass |
| DUAL FILTER PCM | 61.6 | 62.8 | 0 | 7 | 53.5 | 8 | pass |
| STACK PCM | 65.5 | 66.8 | 0 | 6 | 51.2 | 8 | pass |
| BINAURAL PCM | 59.4 | 60.4 | 0 | 6 | 56.2 | 8 | pass |
| DUALOSC MULTI H PCM | 55.1 | 56.1 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 68.3 | 69.7 | 0 | 7 | 46.5 | 9 | pass |
| BINAUR MULTI H PCM | 67.6 | 68.9 | 0 | 6 | 44.2 | 9 | pass |
| SINGLE RS | 43.3 | 44.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.3 | 53.4 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 57.0 | 58.6 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.8 | 61.7 | 0 | 6 | 55.9 | 8 | pass |
| SEQ+ARP HOME PCM | 64.4 | 68.1 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 47.0 | 48.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 47.2 | 48.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 47.1 | 48.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TAPE | 48.0 | 49.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 47.8 | 48.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 47.9 | 49.0 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 66.8 | 68.3 | 0 | 6 | 48.9 | 9 | pass |
| BINAUR HEAVY OD | 64.2 | 65.3 | 0 | 6 | 52.2 | 8 | pass |
| STRIKE HEAVY TUBE | 52.8 | 53.9 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 60.0 | 63.0 | 0 | 8 | 56.2 | 9 | pass |
| FULL | 50.3 | 51.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 52.0 | 53.0 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 53.2 | 54.2 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 59.3 | 60.3 | 0 | 4 | 56.9 | 8 | pass |
| FULL H CEL+PP | 58.6 | 59.8 | 0 | 4 | 57.2 | 8 | pass |
| FULL MULTI H PCM | 53.2 | 54.4 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 59.6 | 60.6 | 0 | 4 | 56.5 | 8 | pass |
| FULL SH2 CEL+PP | 64.4 | 65.7 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 43.7 | 44.7 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 46.1 | 47.2 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 47.2 | 48.1 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 50.6 | 51.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 52.1 | 53.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 56.3 | 57.3 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 CEL+PP | 55.1 | 56.3 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 59.6 | 63.7 | 0 | 4 | 55.9 | 9 | pass |
| IDLE PHASER | 17.2 | 17.8 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.6 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 57.1 | 58.2 | 0 | 8 | 57.5 | 7 | pass |
| SPREAD H CEL PHSR | 68.3 | 69.7 | 0 | 8 | 46.2 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 60.7 | 64.0 | 0 | 8 | 54.6 | 9 | pass |
| IDLE ENSEMBLE | 17.8 | 18.4 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.4 | 19.9 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR | 18.4 | 19.0 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR x2 | 20.3 | 21.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS | 17.4 | 18.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE CHORUS x2 | 18.8 | 19.4 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 70.0 | 71.7 | 0 | 8 | 43.5 | 9 | pass |
| SEQ+ARP HOME ENPH2 | 63.1 | 66.5 | 0 | 8 | 51.9 | 10 | pass |
| SPREAD H CEL MIX | 66.2 | 67.4 | 0 | 8 | 51.2 | 8 | pass |
| SEQ+ARP CEL MIX | 61.3 | 65.0 | 0 | 8 | 53.6 | 10 | pass |
| HEAVY SPRING MIX | 51.9 | 53.0 | 0 | 8 | 57.3 | 1 | pass |
| IDLE TAPE FLANGER | 18.4 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE TAPE FLNG x2 | 20.1 | 20.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL TFLG | 71.1 | 72.4 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME TFLG2 | 63.0 | 66.6 | 0 | 8 | 51.9 | 10 | pass |

### 2026-11-02 15:04, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.0 | 15.5 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 23.3 | 24.3 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 43.0 | 44.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 47.4 | 48.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 56.9 | 58.1 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD | 53.2 | 54.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FILTER | 56.2 | 57.2 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 58.7 | 59.7 | 0 | 6 | 57.2 | 8 | pass |
| BINAURAL | 51.9 | 52.9 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 59.5 | 60.6 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD HEAVY | 57.5 | 58.7 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FLT HEAVY | 59.7 | 60.9 | 0 | 7 | 56.5 | 8 | pass |
| STACK HEAVY | 61.3 | 62.5 | 0 | 6 | 52.5 | 8 | pass |
| BINAURAL HEAVY | 57.9 | 59.0 | 0 | 6 | 57.2 | 7 | pass |
| SINGLE LADDER | 43.0 | 44.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 40.8 | 41.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 48.3 | 49.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.9 | 41.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 56.2 | 57.1 | 0 | 7 | 57.2 | 7 | pass |
| SEQ+ARP HOME | 54.9 | 58.3 | 0 | 8 | 57.5 | 9 | pass |
| SEQ+ARP AMP | 55.1 | 58.8 | 0 | 8 | 57.2 | 10 | pass |
| SEQ+ARP SEQ | 54.3 | 58.0 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 54.8 | 60.0 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 42.9 | 44.0 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 51.1 | 52.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 52.2 | 53.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 60.1 | 64.1 | 0 | 8 | 55.5 | 9 | pass |
| SINGLE MULTI H | 49.7 | 50.8 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 61.0 | 62.0 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD MULTI H | 60.0 | 61.1 | 0 | 8 | 55.6 | 8 | pass |
| DUALFLT MULTI H | 63.1 | 64.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 63.1 | 64.3 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 60.3 | 61.4 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD H CEL+PP | 64.8 | 66.0 | 0 | 8 | 51.9 | 8 | pass |
| DUALOSC H CEL+PP | 66.1 | 67.6 | 0 | 6 | 51.2 | 9 | pass |
| SPREAD H TUM+PP | 63.2 | 64.7 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 72.3 | 77.2 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 49.6 | 50.7 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 53.2 | 54.4 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP SPR+BBD | 58.5 | 62.4 | 0 | 8 | 57.2 | 9 | pass |
| STRIKE | 43.7 | 44.7 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.4 | 49.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 64.7 | 69.0 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP STRK FILT | 65.4 | 69.8 | 0 | 8 | 50.9 | 10 | pass |
| SINGLE SH2 H | 55.1 | 56.2 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 65.6 | 66.6 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD SH2 H | 65.5 | 66.7 | 0 | 8 | 51.2 | 8 | pass |
| DUALFLT SH2 H | 73.0 | 74.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 70.7 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 68.1 | 69.1 | 0 | 6 | 45.9 | 9 | pass |
| STRIKE SH2 H | 55.8 | 56.8 | 0 | 8 | 57.6 | 1 | pass |
| SPREAD SH2 CEL+PP | 70.2 | 71.5 | 0 | 8 | 43.2 | 9 | FAIL |
| DUALFLT SH2 CEL | 78.2 | 79.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 67.0 | 70.9 | 0 | 8 | 46.6 | 10 | pass |
| DUALFLT SH2 HP | 73.0 | 74.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 68.1 | 69.3 | 0 | 6 | 45.2 | 9 | pass |
| SINGLE PCM | 47.4 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 56.3 | 57.3 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD PCM | 61.1 | 62.2 | 0 | 8 | 55.9 | 8 | pass |
| DUAL FILTER PCM | 60.5 | 61.6 | 0 | 7 | 54.6 | 8 | pass |
| STACK PCM | 64.7 | 65.9 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 58.3 | 59.4 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC MULTI H PCM | 55.2 | 56.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 67.6 | 68.9 | 0 | 7 | 44.5 | 9 | pass |
| BINAUR MULTI H PCM | 66.9 | 68.2 | 0 | 6 | 46.6 | 9 | pass |
| SINGLE RS | 43.0 | 44.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 50.6 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 57.0 | 58.5 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.3 | 61.0 | 0 | 6 | 55.9 | 8 | pass |
| SEQ+ARP HOME PCM | 64.0 | 67.8 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE DRV SOFT | 46.8 | 47.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 47.0 | 48.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 46.9 | 47.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 47.7 | 48.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 47.5 | 48.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 47.6 | 48.6 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 70.8 | 72.9 | 0 | 6 | 43.5 | 9 | FAIL |
| BINAUR HEAVY OD | 65.3 | 66.4 | 0 | 6 | 51.6 | 8 | pass |
| STRIKE HEAVY TUBE | 53.7 | 54.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 60.9 | 64.6 | 0 | 8 | 54.9 | 9 | pass |
| FULL | 50.4 | 51.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 52.8 | 53.9 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 53.8 | 54.9 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H | 59.6 | 60.5 | 0 | 4 | 56.2 | 8 | pass |
| FULL H CEL+PP | 59.0 | 60.2 | 0 | 4 | 56.9 | 8 | pass |
| FULL MULTI H PCM | 50.4 | 51.5 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 56.6 | 57.6 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 CEL+PP | 65.5 | 66.7 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 43.2 | 44.2 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 45.9 | 46.9 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD MULTI H | 47.0 | 48.0 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 H | 50.1 | 51.3 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD H CEL+PP | 52.0 | 53.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 55.3 | 56.3 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 CEL+PP | 55.1 | 56.2 | 0 | 4 | 57.2 | 1 | pass |
| SEQ+ARP DSPRD CEL | 59.8 | 63.3 | 0 | 4 | 55.9 | 9 | pass |
| IDLE PHASER | 17.1 | 17.6 | 0 | 0 | 61.6 | 1 | pass |
| IDLE PHASER x2 | 18.5 | 19.1 | 0 | 0 | 61.6 | 1 | pass |
| SPREAD PHASER | 56.0 | 57.2 | 0 | 8 | 57.5 | 7 | pass |
| SPREAD H CEL PHSR | 68.6 | 70.2 | 0 | 8 | 43.9 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 60.6 | 64.2 | 0 | 8 | 54.9 | 9 | pass |
| IDLE ENSEMBLE | 17.8 | 18.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 19.4 | 20.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR | 18.4 | 19.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR x2 | 20.3 | 21.0 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 17.1 | 17.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 18.5 | 19.1 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 70.6 | 72.3 | 0 | 8 | 43.2 | 9 | FAIL |
| SEQ+ARP HOME ENPH2 | 63.6 | 67.5 | 0 | 8 | 51.9 | 10 | pass |
| SPREAD H CEL MIX | 66.5 | 67.9 | 0 | 8 | 51.5 | 9 | pass |
| SEQ+ARP CEL MIX | 61.8 | 66.1 | 0 | 8 | 54.2 | 10 | pass |
| HEAVY SPRING MIX | 52.0 | 53.1 | 0 | 8 | 57.3 | 1 | pass |
| IDLE TAPE FLANGER | 18.6 | 19.1 | 0 | 0 | 61.6 | 3 | pass |
| IDLE TAPE FLNG x2 | 20.6 | 21.2 | 0 | 0 | 61.3 | 3 | pass |
| SPREAD H CEL TFLG | 70.8 | 72.3 | 0 | 8 | 43.2 | 9 | FAIL |
| SEQ+ARP HOME TFLG2 | 63.0 | 67.1 | 0 | 8 | 52.5 | 10 | pass |

### 2026-11-02 12:11, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.1 | 15.7 | 0 | 0 | 61.9 | 2 | pass |
| SINGLE 1V | 22.9 | 23.9 | 0 | 1 | 61.3 | 3 | pass |
| SINGLE | 42.6 | 43.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 47.3 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 56.2 | 57.2 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD | 52.8 | 54.0 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FILTER | 55.6 | 56.7 | 0 | 7 | 57.5 | 1 | pass |
| STACK | 58.1 | 59.2 | 0 | 6 | 57.6 | 8 | pass |
| BINAURAL | 51.3 | 52.4 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 59.2 | 60.3 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD HEAVY | 57.4 | 58.6 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FLT HEAVY | 58.7 | 59.8 | 0 | 7 | 57.2 | 8 | pass |
| STACK HEAVY | 59.6 | 60.8 | 0 | 6 | 56.9 | 8 | pass |
| BINAURAL HEAVY | 56.9 | 58.1 | 0 | 6 | 57.2 | 7 | pass |
| SINGLE LADDER | 42.7 | 43.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 40.6 | 41.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 47.5 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.8 | 41.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 55.6 | 56.5 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 54.3 | 57.8 | 0 | 8 | 57.6 | 9 | pass |
| SEQ+ARP AMP | 54.5 | 57.6 | 0 | 8 | 57.5 | 10 | pass |
| SEQ+ARP SEQ | 53.6 | 57.2 | 0 | 8 | 16.5 | 105 | pass |
| SEQ+ARP MOD1 | 54.1 | 58.4 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 42.7 | 43.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 50.4 | 51.5 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 51.7 | 52.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 59.1 | 62.3 | 0 | 8 | 56.2 | 9 | pass |
| SINGLE MULTI H | 49.4 | 50.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.3 | 61.4 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD MULTI H | 59.7 | 60.8 | 0 | 8 | 55.6 | 8 | pass |
| DUALFLT MULTI H | 62.4 | 63.6 | 0 | 7 | 52.5 | 8 | pass |
| STACK MULTI H | 62.5 | 63.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 59.6 | 60.7 | 0 | 6 | 56.6 | 8 | pass |
| SPREAD H CEL+PP | 63.8 | 65.2 | 0 | 8 | 52.2 | 1 | pass |
| DUALOSC H CEL+PP | 64.8 | 66.3 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H TUM+PP | 61.9 | 63.1 | 0 | 8 | 53.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 70.6 | 74.2 | 0 | 8 | 43.9 | 11 | FAIL |
| HEAVY SPRING+PP | 51.3 | 52.6 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 51.9 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 58.2 | 61.4 | 0 | 8 | 56.5 | 9 | pass |
| STRIKE | 43.2 | 44.4 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.2 | 49.4 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 64.0 | 67.7 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP STRK FILT | 65.0 | 68.6 | 0 | 8 | 52.2 | 10 | pass |
| SINGLE SH2 H | 54.1 | 55.1 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 63.9 | 65.0 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 64.6 | 65.7 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT SH2 H | 71.7 | 72.9 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 69.2 | 70.4 | 0 | 6 | 42.9 | 1 | pass |
| BINAUR SH2 H | 66.7 | 67.8 | 0 | 6 | 50.2 | 9 | pass |
| STRIKE SH2 H | 54.8 | 55.7 | 0 | 8 | 57.2 | 1 | pass |
| SPREAD SH2 CEL+PP | 68.7 | 70.0 | 0 | 8 | 44.9 | 9 | pass |
| DUALFLT SH2 CEL | 76.4 | 77.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 65.2 | 69.1 | 0 | 8 | 51.2 | 10 | pass |
| DUALFLT SH2 HP | 71.7 | 72.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 66.7 | 68.0 | 0 | 6 | 47.9 | 9 | pass |
| SINGLE PCM | 47.4 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.6 | 57.0 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD PCM | 62.1 | 63.3 | 0 | 8 | 52.9 | 8 | pass |
| DUAL FILTER PCM | 60.1 | 61.4 | 0 | 7 | 55.6 | 8 | pass |
| STACK PCM | 65.0 | 66.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 58.5 | 59.7 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC MULTI H PCM | 54.9 | 56.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 67.4 | 68.8 | 0 | 7 | 45.2 | 9 | pass |
| BINAUR MULTI H PCM | 67.2 | 68.6 | 0 | 6 | 46.2 | 9 | pass |
| SINGLE RS | 42.7 | 44.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 50.9 | 53.2 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 56.2 | 57.7 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.4 | 61.2 | 0 | 6 | 56.2 | 8 | pass |
| SEQ+ARP HOME PCM | 63.3 | 67.1 | 0 | 8 | 52.2 | 10 | pass |
| SINGLE DRV SOFT | 46.5 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 46.7 | 47.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 46.7 | 47.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 47.5 | 48.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 47.3 | 48.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 47.3 | 48.5 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 66.9 | 68.2 | 0 | 6 | 48.2 | 9 | pass |
| BINAUR HEAVY OD | 63.9 | 65.1 | 0 | 6 | 51.9 | 8 | pass |
| STRIKE HEAVY TUBE | 53.3 | 54.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 59.9 | 63.7 | 0 | 8 | 56.5 | 9 | pass |
| FULL | 49.8 | 50.8 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 51.8 | 53.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 53.4 | 54.5 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H | 58.3 | 59.3 | 0 | 4 | 57.2 | 8 | pass |
| FULL H CEL+PP | 58.3 | 59.6 | 0 | 4 | 57.2 | 8 | pass |
| FULL MULTI H PCM | 50.2 | 51.5 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 53.9 | 55.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 CEL+PP | 63.1 | 64.4 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 42.9 | 43.9 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 45.5 | 46.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 46.7 | 47.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 48.9 | 49.9 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 51.3 | 52.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 55.8 | 56.8 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 CEL+PP | 53.5 | 54.6 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 58.3 | 61.9 | 0 | 4 | 56.5 | 9 | pass |
| IDLE PHASER | 17.0 | 17.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.4 | 19.0 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 55.8 | 57.1 | 0 | 8 | 57.5 | 1 | pass |
| SPREAD H CEL PHSR | 66.9 | 68.4 | 0 | 8 | 47.9 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 59.6 | 63.1 | 0 | 8 | 56.2 | 9 | pass |
| IDLE ENSEMBLE | 17.6 | 18.2 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENSEMBLE x2 | 19.1 | 19.8 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR | 18.1 | 18.9 | 0 | 0 | 61.3 | 1 | pass |
| IDLE ENS+PHSR x2 | 19.9 | 20.7 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 17.3 | 18.0 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 18.8 | 19.5 | 0 | 0 | 61.2 | 3 | pass |
| SPREAD H CEL ENPH | 69.9 | 71.5 | 0 | 8 | 43.2 | 9 | pass |
| SEQ+ARP HOME ENPH2 | 61.6 | 65.4 | 0 | 8 | 54.6 | 10 | pass |

### 2026-11-02 12:03, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 14.9 | 15.4 | 0 | 0 | 62.3 | 2 | pass |
| SINGLE 1V | 22.5 | 23.4 | 0 | 1 | 61.6 | 3 | pass |
| SINGLE | 42.3 | 43.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 47.1 | 48.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 55.8 | 56.8 | 0 | 6 | 57.2 | 1 | pass |
| SPREAD | 52.7 | 54.0 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FILTER | 55.6 | 56.7 | 0 | 7 | 57.5 | 1 | pass |
| STACK | 57.7 | 59.0 | 0 | 6 | 57.5 | 1 | pass |
| BINAURAL | 51.0 | 52.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 58.8 | 59.8 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD HEAVY | 57.3 | 58.4 | 0 | 8 | 57.5 | 1 | pass |
| DUAL FLT HEAVY | 58.8 | 59.9 | 0 | 7 | 57.2 | 8 | pass |
| STACK HEAVY | 59.4 | 60.5 | 0 | 6 | 56.6 | 8 | pass |
| BINAURAL HEAVY | 56.6 | 57.7 | 0 | 6 | 57.2 | 7 | pass |
| SINGLE LADDER | 42.3 | 43.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 40.4 | 41.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 47.4 | 48.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 40.6 | 41.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 55.7 | 56.6 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 63.3 | 66.6 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 63.4 | 66.4 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 62.7 | 66.0 | 0 | 8 | 14.3 | 127 | pass |
| SEQ+ARP MOD1 | 63.3 | 67.4 | 0 | 8 | 17.0 | 70 | pass |
| LADDER FILTER PG | 42.4 | 43.4 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 50.2 | 51.4 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CELEST+PP | 51.8 | 53.1 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.0 | 71.3 | 0 | 8 | 45.2 | 10 | pass |
| SINGLE MULTI H | 48.8 | 49.9 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC MULTI H | 60.1 | 61.2 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD MULTI H | 59.4 | 60.4 | 0 | 8 | 56.6 | 8 | pass |
| DUALFLT MULTI H | 62.5 | 63.6 | 0 | 7 | 53.2 | 8 | pass |
| STACK MULTI H | 61.9 | 63.1 | 0 | 6 | 53.2 | 8 | pass |
| BINAUR MULTI H | 59.5 | 60.6 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD H CEL+PP | 63.7 | 65.2 | 0 | 8 | 51.9 | 8 | pass |
| DUALOSC H CEL+PP | 64.3 | 65.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 61.9 | 63.1 | 0 | 8 | 53.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 80.3 | 83.6 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 51.0 | 52.0 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 51.8 | 52.8 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP SPR+BBD | 67.4 | 70.6 | 0 | 8 | 47.5 | 10 | pass |
| STRIKE | 42.9 | 43.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.3 | 49.4 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 73.1 | 76.7 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 74.1 | 77.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 54.0 | 55.0 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC SH2 H | 63.5 | 64.6 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD SH2 H | 64.5 | 65.6 | 0 | 8 | 51.6 | 8 | pass |
| DUALFLT SH2 H | 71.7 | 72.9 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 68.7 | 70.0 | 0 | 6 | 43.9 | 9 | pass |
| BINAUR SH2 H | 66.5 | 67.6 | 0 | 6 | 51.5 | 9 | pass |
| STRIKE SH2 H | 54.5 | 55.6 | 0 | 8 | 57.2 | 1 | pass |
| SPREAD SH2 CEL+PP | 68.7 | 70.0 | 0 | 8 | 44.2 | 9 | pass |
| DUALFLT SH2 CEL | 76.6 | 78.1 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.2 | 78.2 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 71.7 | 73.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 66.6 | 67.6 | 0 | 6 | 51.9 | 9 | pass |
| SINGLE PCM | 47.1 | 48.4 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.8 | 57.1 | 0 | 6 | 57.5 | 1 | pass |
| SPREAD PCM | 61.4 | 62.5 | 0 | 8 | 54.5 | 8 | pass |
| DUAL FILTER PCM | 60.5 | 61.8 | 0 | 7 | 55.9 | 8 | pass |
| STACK PCM | 64.2 | 65.5 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL PCM | 58.4 | 59.6 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC MULTI H PCM | 55.0 | 56.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 67.5 | 68.9 | 0 | 7 | 45.9 | 9 | pass |
| BINAUR MULTI H PCM | 66.8 | 68.3 | 0 | 6 | 47.6 | 9 | pass |
| SINGLE RS | 42.3 | 44.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 50.6 | 52.8 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 55.9 | 57.7 | 0 | 6 | 57.5 | 1 | pass |
| DUALOSC RS PCM | 59.3 | 61.1 | 0 | 6 | 56.9 | 8 | pass |
| SEQ+ARP HOME PCM | 72.3 | 76.6 | 0 | 8 | 43.2 | 2 | FAIL |
| SINGLE DRV SOFT | 46.1 | 47.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 46.3 | 47.3 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 46.3 | 47.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TAPE | 47.1 | 48.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 46.9 | 47.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 46.9 | 48.0 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 66.8 | 68.3 | 0 | 6 | 49.2 | 9 | pass |
| BINAUR HEAVY OD | 63.5 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| STRIKE HEAVY TUBE | 53.6 | 54.7 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 68.9 | 72.5 | 0 | 8 | 44.2 | 10 | pass |
| FULL | 49.3 | 50.3 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 51.4 | 52.6 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 53.0 | 54.0 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 57.8 | 58.9 | 0 | 4 | 57.5 | 7 | pass |
| FULL H CEL+PP | 57.7 | 58.9 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H PCM | 49.7 | 50.9 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 53.6 | 54.8 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 CEL+PP | 62.6 | 63.7 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 42.4 | 43.5 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 45.3 | 46.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 46.1 | 47.2 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 48.7 | 49.9 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 51.1 | 52.2 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 55.2 | 56.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 CEL+PP | 53.5 | 54.6 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 67.8 | 71.4 | 0 | 4 | 46.2 | 10 | pass |
| IDLE PHASER | 16.8 | 17.5 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.2 | 18.8 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 55.4 | 56.5 | 0 | 8 | 57.2 | 1 | pass |
| SPREAD H CEL PHSR | 66.6 | 68.0 | 0 | 8 | 49.2 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 68.2 | 71.8 | 0 | 8 | 46.2 | 10 | pass |
| IDLE ENSEMBLE | 17.4 | 18.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 18.9 | 19.7 | 0 | 0 | 61.3 | 3 | pass |
| IDLE ENS+PHSR | 17.9 | 18.5 | 0 | 0 | 61.6 | 3 | pass |
| IDLE ENS+PHSR x2 | 19.7 | 20.4 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 17.1 | 17.7 | 0 | 0 | 61.6 | 3 | pass |
| IDLE CHORUS x2 | 18.6 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD H CEL ENPH | 69.6 | 71.2 | 0 | 8 | 43.2 | 9 | pass |
| SEQ+ARP HOME ENPH2 | 70.4 | 74.1 | 0 | 8 | 43.2 | 10 | FAIL |

### 2026-11-02 11:38, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 15.5 | 16.0 | 0 | 0 | 62.0 | 2 | pass |
| SINGLE 1V | 23.4 | 24.3 | 0 | 1 | 61.0 | 3 | pass |
| SINGLE | 43.2 | 44.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 47.8 | 49.0 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 56.8 | 58.0 | 0 | 6 | 57.2 | 7 | pass |
| SPREAD | 53.7 | 54.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FILTER | 56.3 | 57.3 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 58.7 | 59.7 | 0 | 6 | 57.2 | 8 | pass |
| BINAURAL | 51.5 | 52.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 59.8 | 60.9 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD HEAVY | 58.1 | 59.1 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FLT HEAVY | 59.2 | 60.2 | 0 | 7 | 56.6 | 8 | pass |
| STACK HEAVY | 60.0 | 61.1 | 0 | 6 | 55.2 | 8 | pass |
| BINAURAL HEAVY | 57.3 | 58.3 | 0 | 6 | 57.6 | 1 | pass |
| SINGLE LADDER | 43.1 | 44.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 40.9 | 41.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 48.1 | 49.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 41.4 | 42.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 56.2 | 57.3 | 0 | 7 | 57.5 | 1 | pass |
| SEQ+ARP HOME | 64.3 | 67.3 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 64.4 | 67.7 | 0 | 8 | 51.8 | 11 | pass |
| SEQ+ARP SEQ | 63.6 | 66.4 | 0 | 8 | 13.3 | 127 | pass |
| SEQ+ARP MOD1 | 64.2 | 68.8 | 0 | 8 | 15.0 | 70 | pass |
| LADDER FILTER PG | 43.0 | 44.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 50.7 | 51.9 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CELEST+PP | 52.0 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.4 | 71.8 | 0 | 8 | 45.5 | 10 | pass |
| SINGLE MULTI H | 49.6 | 50.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 60.8 | 61.8 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD MULTI H | 60.2 | 61.4 | 0 | 8 | 54.2 | 8 | pass |
| DUALFLT MULTI H | 62.9 | 64.1 | 0 | 7 | 52.5 | 8 | pass |
| STACK MULTI H | 62.6 | 63.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAUR MULTI H | 59.7 | 60.9 | 0 | 6 | 56.9 | 8 | pass |
| SPREAD H CEL+PP | 64.0 | 65.3 | 0 | 8 | 52.2 | 1 | pass |
| DUALOSC H CEL+PP | 64.7 | 66.2 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H TUM+PP | 62.5 | 63.7 | 0 | 8 | 52.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 80.4 | 84.0 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 51.7 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 52.1 | 53.3 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP SPR+BBD | 67.7 | 71.2 | 0 | 8 | 45.2 | 10 | pass |
| STRIKE | 43.7 | 44.7 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 48.7 | 49.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 73.5 | 77.2 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 73.9 | 77.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 54.6 | 55.7 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC SH2 H | 64.2 | 65.4 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD SH2 H | 65.4 | 66.5 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT SH2 H | 72.2 | 73.5 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 69.6 | 71.1 | 0 | 6 | 42.9 | 9 | pass |
| BINAUR SH2 H | 67.2 | 68.6 | 0 | 6 | 46.6 | 8 | pass |
| STRIKE SH2 H | 55.2 | 56.3 | 0 | 8 | 57.6 | 1 | pass |
| SPREAD SH2 CEL+PP | 68.9 | 70.3 | 0 | 8 | 43.6 | 9 | pass |
| DUALFLT SH2 CEL | 76.4 | 78.0 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.2 | 79.0 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 72.2 | 73.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 67.3 | 68.6 | 0 | 6 | 48.2 | 9 | pass |
| SINGLE PCM | 47.6 | 48.8 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 55.7 | 56.8 | 0 | 6 | 57.5 | 7 | pass |
| SPREAD PCM | 61.8 | 63.0 | 0 | 8 | 53.9 | 8 | pass |
| DUAL FILTER PCM | 60.7 | 61.8 | 0 | 7 | 56.6 | 8 | pass |
| STACK PCM | 64.5 | 65.5 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL PCM | 58.1 | 59.2 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC MULTI H PCM | 54.3 | 55.4 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 67.2 | 68.7 | 0 | 7 | 46.5 | 9 | pass |
| BINAUR MULTI H PCM | 66.3 | 67.6 | 0 | 6 | 51.9 | 9 | pass |
| SINGLE RS | 43.2 | 44.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 50.8 | 52.7 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 56.9 | 58.3 | 0 | 6 | 57.5 | 7 | pass |
| DUALOSC RS PCM | 59.3 | 60.9 | 0 | 6 | 56.2 | 8 | pass |
| SEQ+ARP HOME PCM | 73.6 | 77.0 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 46.9 | 48.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 47.1 | 48.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 47.1 | 48.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 48.0 | 49.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 47.8 | 48.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 47.9 | 48.9 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 66.8 | 68.1 | 0 | 6 | 48.5 | 9 | pass |
| BINAUR HEAVY OD | 64.0 | 65.1 | 0 | 6 | 51.9 | 8 | pass |
| STRIKE HEAVY TUBE | 53.4 | 54.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 69.4 | 72.9 | 0 | 8 | 44.2 | 10 | pass |
| FULL | 50.3 | 51.3 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 52.9 | 53.9 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 53.5 | 54.5 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H | 58.4 | 59.4 | 0 | 4 | 57.5 | 8 | pass |
| FULL H CEL+PP | 57.7 | 58.9 | 0 | 4 | 57.6 | 7 | pass |
| FULL MULTI H PCM | 49.7 | 50.9 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 55.9 | 57.3 | 0 | 4 | 57.5 | 7 | pass |
| FULL SH2 CEL+PP | 62.7 | 63.9 | 0 | 4 | 52.2 | 8 | pass |
| DSPRD | 43.4 | 44.6 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 46.2 | 47.2 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 47.0 | 48.1 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 H | 49.5 | 50.6 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD H CEL+PP | 51.1 | 52.3 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD MULTI H PCM | 54.7 | 55.9 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 CEL+PP | 53.5 | 54.7 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 67.6 | 71.2 | 0 | 4 | 46.2 | 10 | pass |
| IDLE PHASER | 18.8 | 19.4 | 0 | 0 | 61.6 | 3 | pass |
| IDLE PHASER x2 | 18.8 | 19.4 | 0 | 0 | 61.6 | 3 | pass |
| SPREAD PHASER | 57.3 | 58.4 | 0 | 8 | 57.5 | 1 | pass |
| SPREAD H CEL PHSR | 67.9 | 69.5 | 0 | 8 | 47.5 | 9 | pass |
| SEQ+ARP HOME PHSR2 | 68.4 | 72.1 | 0 | 8 | 45.2 | 10 | pass |
| IDLE ENSEMBLE | 19.4 | 20.0 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 19.5 | 20.1 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR | 20.5 | 21.2 | 0 | 0 | 61.6 | 1 | pass |
| IDLE ENS+PHSR x2 | 20.5 | 21.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE CHORUS | 19.4 | 19.9 | 0 | 0 | 61.3 | 1 | pass |
| IDLE CHORUS x2 | 19.4 | 20.0 | 0 | 0 | 61.2 | 1 | pass |
| SPREAD H CEL ENPH | 71.5 | 73.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 72.4 | 76.5 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-11-02 11:22, CARNAC OS PROF (1 core), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 26.1 | 26.6 | 0 | 0 | 60.0 | 4 | pass |
| SINGLE 1V | 34.4 | 35.4 | 0 | 1 | 57.6 | 1 | pass |
| SINGLE | 84.8 | 86.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SINGLE HEAVY | 92.5 | 93.9 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL OSC | 109.8 | 110.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD | 106.3 | 107.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER | 98.6 | 100.2 | 18 | 7 | 31.6 | 24 | FAIL |
| STACK | 113.8 | 115.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL | 98.6 | 100.2 | 30 | 6 | 35.9 | 24 | FAIL |
| DUAL OSC HEAVY | 113.8 | 114.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD HEAVY | 113.9 | 115.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FLT HEAVY | 103.6 | 105.2 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK HEAVY | 114.4 | 115.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL HEAVY | 108.8 | 110.4 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE LADDER | 84.8 | 86.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE MS20 | 80.3 | 81.6 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE SH-2 | 94.8 | 96.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE FORMANT | 81.2 | 82.7 | 0 | 8 | 43.2 | 1 | FAIL |
| DUAL FLT LADDER | 98.6 | 100.3 | 19 | 7 | 31.9 | 24 | FAIL |
| SEQ+ARP HOME | 106.5 | 109.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP AMP | 106.9 | 109.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SEQ | 105.8 | 108.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP MOD1 | 107.2 | 111.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| LADDER FILTER PG | 84.8 | 86.1 | 0 | 8 | 42.9 | 1 | FAIL |
| HEAVY MOD+PP | 98.2 | 100.1 | 3 | 8 | 37.7 | 12 | FAIL |
| HEAVY CELEST+PP | 99.6 | 101.2 | 10 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP CELEST+PP | 114.5 | 117.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE MULTI H | 96.3 | 97.6 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALOSC MULTI H | 116.3 | 116.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD MULTI H | 117.7 | 118.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H | 109.9 | 111.6 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK MULTI H | 119.7 | 120.9 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H | 114.2 | 115.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL+PP | 124.9 | 126.3 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC H CEL+PP | 124.0 | 125.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD H TUM+PP | 122.7 | 124.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPRD CEL | 139.2 | 141.8 | 8 | 8 | – | – | FAIL (cut by the guard) |
| HEAVY SPRING+PP | 97.8 | 99.1 | 0 | 8 | 42.2 | 12 | FAIL |
| HEAVY CEL+BBD PP | 101.7 | 102.5 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SPR+BBD | 113.9 | 116.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| STRIKE | 84.1 | 85.3 | 0 | 8 | 42.9 | 1 | FAIL |
| STRIKE HEAVY | 92.0 | 93.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP STRIKE | 120.6 | 123.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP STRK FILT | 123.5 | 125.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE SH2 H | 107.1 | 108.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC SH2 H | 124.0 | 125.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 H | 127.8 | 129.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 H | 127.4 | 128.9 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK SH2 H | 134.8 | 136.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 H | 130.3 | 132.2 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE SH2 H | 113.4 | 114.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD SH2 CEL+PP | 135.0 | 136.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 CEL | 134.7 | 136.4 | 8 | 7 | – | – | FAIL (cut by the guard) |
| SEQ+ARP SH2 CEL | 128.5 | 131.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUALFLT SH2 HP | 127.5 | 129.1 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR SH2 HP | 130.4 | 132.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE PCM | 92.7 | 94.5 | 0 | 8 | 42.9 | 1 | FAIL |
| DUAL OSC PCM | 114.0 | 114.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SPREAD PCM | 141.8 | 143.9 | 8 | 8 | – | – | FAIL (cut by the guard) |
| DUAL FILTER PCM | 112.9 | 114.7 | 8 | 7 | – | – | FAIL (cut by the guard) |
| STACK PCM | 132.3 | 135.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAURAL PCM | 124.5 | 130.3 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC MULTI H PCM | 113.4 | 114.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALFLT MULTI H PCM | 123.1 | 124.8 | 8 | 7 | – | – | FAIL (cut by the guard) |
| BINAUR MULTI H PCM | 139.6 | 144.8 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SINGLE RS | 84.9 | 87.3 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE RS PCM | 98.9 | 102.1 | 43 | 8 | – | – | FAIL (cut by the guard) |
| DUALOSC RS | 109.8 | 111.1 | 8 | 6 | – | – | FAIL (cut by the guard) |
| DUALOSC RS PCM | 114.0 | 115.6 | 8 | 6 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PCM | 122.3 | 125.1 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SINGLE DRV SOFT | 92.0 | 93.5 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV HARD | 92.4 | 93.9 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV FOLD | 92.8 | 94.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TAPE | 94.2 | 95.7 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV TUBE | 93.5 | 95.0 | 0 | 8 | 43.2 | 1 | FAIL |
| SINGLE DRV OD | 93.7 | 95.3 | 0 | 8 | 43.2 | 1 | FAIL |
| STACK HEAVY TUBE | 125.9 | 126.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| BINAUR HEAVY OD | 121.3 | 122.5 | 8 | 6 | – | – | FAIL (cut by the guard) |
| STRIKE HEAVY TUBE | 107.0 | 109.0 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME TUBE | 115.8 | 118.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| FULL | 94.7 | 96.1 | 0 | 4 | 42.9 | 1 | FAIL |
| FULL HEAVY | 97.4 | 98.9 | 0 | 4 | 42.4 | 12 | FAIL |
| FULL MULTI H | 101.7 | 102.6 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 H | 110.8 | 112.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL H CEL+PP | 109.3 | 110.4 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL MULTI H PCM | 91.1 | 93.1 | 0 | 4 | 42.9 | 1 | FAIL |
| FULL SH2 H PCM | 109.2 | 111.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| FULL SH2 CEL+PP | 118.2 | 119.7 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD | 81.2 | 82.6 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD HEAVY | 85.4 | 86.9 | 0 | 4 | 42.9 | 1 | FAIL |
| DSPRD MULTI H | 87.4 | 88.8 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD SH2 H | 92.1 | 93.6 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD H CEL+PP | 94.8 | 96.4 | 0 | 4 | 43.2 | 1 | FAIL |
| DSPRD MULTI H PCM | 123.3 | 124.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| DSPRD SH2 CEL+PP | 100.8 | 102.0 | 8 | 4 | – | – | FAIL (cut by the guard) |
| SEQ+ARP DSPRD CEL | 109.2 | 111.3 | 8 | 4 | – | – | FAIL (cut by the guard) |
| IDLE PHASER | 29.3 | 29.8 | 0 | 0 | 59.0 | 1 | pass |
| IDLE PHASER x2 | 32.4 | 32.9 | 0 | 0 | 57.3 | 1 | pass |
| SPREAD PHASER | 110.2 | 111.7 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SPREAD H CEL PHSR | 129.4 | 131.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME PHSR2 | 114.2 | 116.4 | 8 | 8 | – | – | FAIL (cut by the guard) |
| IDLE ENSEMBLE | 29.9 | 30.6 | 0 | 0 | 58.6 | 4 | pass |
| IDLE ENSEMBLE x2 | 33.4 | 34.0 | 0 | 0 | 57.3 | 1 | pass |
| IDLE ENS+PHSR | 30.9 | 31.5 | 0 | 0 | 57.6 | 4 | pass |
| IDLE ENS+PHSR x2 | 35.2 | 35.9 | 0 | 0 | 57.5 | 1 | pass |
| IDLE CHORUS | 29.9 | 30.6 | 0 | 0 | 58.2 | 4 | pass |
| IDLE CHORUS x2 | 33.5 | 34.1 | 0 | 0 | 57.2 | 1 | pass |
| SPREAD H CEL ENPH | 132.4 | 134.2 | 8 | 8 | – | – | FAIL (cut by the guard) |
| SEQ+ARP HOME ENPH2 | 119.7 | 122.0 | 8 | 8 | – | – | FAIL (cut by the guard) |

Stage profile (24 MHz ticks a sample; one counter read costs 1, taken off once a lap: per voice per 16-sample block (the voice stages, CTRL and its c.* split); 'other' = TOTAL less the rest: clock, sequencers, loops)

| scenario | voices | TOTAL | CTRL | VOICE | OSC | EXTRAS | FILTER | AMP | MIX | FX | CLOCK | SEQ | LTICK | c.LFO | c.ENV | c.OSC | c.FILT | c.AMP | other | per voice (VOICE+CTRL+MIX) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IDLE | 0 | 139 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 66 | 14 | 29 | 9 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| SINGLE 1V | 1 | 184 | 21 | 20 | 3 | 1 | 11 | 2 | 0 | 67 | 14 | 30 | 9 | 5 | 3 | 6 | 4 | 1 | 23 | 41 |
| SINGLE | 8 | 459 | 156 | 152 | 26 | 6 | 92 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 8 | 30 | 38 |
| SINGLE HEAVY | 8 | 500 | 188 | 156 | 26 | 22 | 80 | 16 | 0 | 68 | 14 | 30 | 10 | 38 | 20 | 44 | 64 | 8 | 34 | 43 |
| DUAL OSC | 6 | 594 | 141 | 303 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 10 | 28 | 16 | 55 | 25 | 7 | 26 | 74 |
| SPREAD | 8 | 575 | 176 | 246 | 120 | 6 | 92 | 16 | 0 | 68 | 14 | 30 | 10 | 38 | 22 | 62 | 32 | 8 | 31 | 53 |
| DUAL FILTER | 7 | 533 | 170 | 217 | 0 | 0 | 0 | 0 | 1 | 66 | 14 | 29 | 9 | 33 | 18 | 39 | 61 | 8 | 27 | 55 |
| STACK | 6 | 616 | 168 | 297 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 29 | 11 | 29 | 17 | 54 | 47 | 13 | 27 | 78 |
| BINAURAL | 6 | 533 | 165 | 221 | 0 | 0 | 0 | 0 | 1 | 66 | 14 | 30 | 9 | 28 | 16 | 33 | 71 | 8 | 27 | 64 |
| DUAL OSC HEAVY | 6 | 616 | 166 | 294 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 10 | 28 | 17 | 56 | 49 | 7 | 32 | 77 |
| SPREAD HEAVY | 8 | 616 | 208 | 250 | 120 | 22 | 78 | 16 | 0 | 69 | 14 | 30 | 10 | 38 | 22 | 62 | 64 | 8 | 35 | 57 |
| DUAL FLT HEAVY | 7 | 560 | 195 | 209 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 10 | 34 | 19 | 39 | 85 | 8 | 32 | 58 |
| STACK HEAVY | 6 | 619 | 187 | 276 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 10 | 28 | 17 | 53 | 67 | 12 | 32 | 77 |
| BINAURAL HEAVY | 6 | 589 | 200 | 227 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 10 | 28 | 17 | 34 | 104 | 8 | 38 | 71 |
| SINGLE LADDER | 8 | 459 | 156 | 152 | 26 | 6 | 92 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 8 | 30 | 38 |
| SINGLE MS20 | 8 | 434 | 150 | 132 | 26 | 6 | 72 | 16 | 0 | 69 | 14 | 30 | 10 | 38 | 20 | 44 | 26 | 8 | 29 | 35 |
| SINGLE SH-2 | 8 | 513 | 154 | 210 | 26 | 6 | 150 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 20 | 44 | 28 | 8 | 28 | 46 |
| SINGLE FORMANT | 8 | 439 | 150 | 140 | 26 | 6 | 80 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 20 | 44 | 26 | 8 | 28 | 36 |
| DUAL FLT LADDER | 7 | 534 | 170 | 217 | 0 | 0 | 0 | 0 | 1 | 66 | 14 | 30 | 9 | 33 | 18 | 38 | 61 | 8 | 27 | 55 |
| SEQ+ARP HOME | 8 | 576 | 204 | 160 | 26 | 22 | 80 | 16 | 2 | 70 | 16 | 64 | 12 | 38 | 32 | 46 | 66 | 10 | 48 | 46 |
| SEQ+ARP AMP | 8 | 578 | 204 | 160 | 26 | 24 | 80 | 16 | 2 | 70 | 15 | 65 | 12 | 38 | 32 | 46 | 66 | 10 | 50 | 46 |
| SEQ+ARP SEQ | 8 | 572 | 204 | 158 | 26 | 24 | 80 | 16 | 2 | 69 | 15 | 64 | 11 | 38 | 32 | 44 | 66 | 10 | 49 | 46 |
| SEQ+ARP MOD1 | 8 | 580 | 206 | 158 | 26 | 24 | 78 | 16 | 2 | 69 | 16 | 64 | 15 | 38 | 32 | 44 | 66 | 8 | 50 | 46 |
| LADDER FILTER PG | 8 | 458 | 156 | 152 | 26 | 6 | 92 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 8 | 29 | 38 |
| HEAVY MOD+PP | 8 | 531 | 188 | 156 | 26 | 22 | 80 | 16 | 0 | 99 | 14 | 30 | 10 | 38 | 20 | 44 | 64 | 8 | 34 | 43 |
| HEAVY CELEST+PP | 8 | 539 | 188 | 156 | 26 | 22 | 78 | 16 | 0 | 106 | 14 | 30 | 10 | 36 | 20 | 44 | 64 | 8 | 35 | 43 |
| SEQ+ARP CELEST+PP | 8 | 620 | 204 | 160 | 26 | 24 | 82 | 16 | 2 | 109 | 15 | 65 | 12 | 38 | 32 | 46 | 66 | 10 | 53 | 46 |
| SINGLE MULTI H | 8 | 521 | 194 | 168 | 26 | 22 | 92 | 16 | 0 | 69 | 14 | 30 | 10 | 38 | 20 | 44 | 70 | 8 | 36 | 45 |
| DUALOSC MULTI H | 6 | 629 | 171 | 304 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 29 | 17 | 55 | 54 | 7 | 31 | 79 |
| SPREAD MULTI H | 8 | 637 | 216 | 262 | 120 | 22 | 92 | 16 | 2 | 68 | 15 | 30 | 10 | 38 | 22 | 62 | 70 | 8 | 34 | 60 |
| DUALFLT MULTI H | 7 | 595 | 207 | 231 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 33 | 20 | 39 | 97 | 8 | 34 | 63 |
| STACK MULTI H | 6 | 648 | 197 | 294 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 10 | 29 | 16 | 54 | 77 | 12 | 33 | 82 |
| BINAUR MULTI H | 6 | 618 | 211 | 246 | 0 | 0 | 0 | 0 | 1 | 67 | 14 | 30 | 10 | 29 | 17 | 34 | 114 | 8 | 39 | 76 |
| SPREAD H CEL+PP | 8 | 676 | 216 | 262 | 120 | 22 | 92 | 16 | 0 | 107 | 15 | 30 | 11 | 38 | 22 | 62 | 72 | 10 | 35 | 60 |
| DUALOSC H CEL+PP | 6 | 671 | 172 | 304 | 0 | 0 | 0 | 0 | 1 | 107 | 15 | 30 | 10 | 28 | 17 | 56 | 55 | 7 | 32 | 80 |
| SPREAD H TUM+PP | 8 | 664 | 216 | 260 | 118 | 22 | 92 | 16 | 2 | 95 | 14 | 30 | 11 | 38 | 22 | 64 | 72 | 10 | 36 | 60 |
| SEQ+ARP SPRD CEL | 8 | 754 | 230 | 266 | 120 | 24 | 92 | 16 | 2 | 111 | 16 | 65 | 12 | 38 | 32 | 66 | 72 | 10 | 52 | 62 |
| HEAVY SPRING+PP | 8 | 529 | 188 | 156 | 26 | 22 | 80 | 16 | 0 | 94 | 14 | 30 | 10 | 38 | 20 | 44 | 64 | 8 | 37 | 43 |
| HEAVY CEL+BBD PP | 8 | 550 | 190 | 156 | 26 | 22 | 78 | 16 | 0 | 114 | 15 | 30 | 10 | 36 | 22 | 44 | 64 | 10 | 35 | 43 |
| SEQ+ARP SPR+BBD | 8 | 616 | 206 | 160 | 28 | 24 | 80 | 16 | 2 | 104 | 16 | 65 | 11 | 38 | 32 | 46 | 66 | 10 | 52 | 46 |
| STRIKE | 8 | 454 | 156 | 148 | 0 | 0 | 0 | 0 | 0 | 67 | 14 | 30 | 10 | 36 | 20 | 44 | 32 | 8 | 29 | 38 |
| STRIKE HEAVY | 8 | 497 | 188 | 150 | 0 | 0 | 0 | 0 | 0 | 69 | 14 | 30 | 10 | 36 | 20 | 44 | 64 | 8 | 36 | 42 |
| SEQ+ARP STRIKE | 8 | 652 | 216 | 176 | 0 | 0 | 0 | 0 | 2 | 114 | 15 | 67 | 12 | 36 | 32 | 46 | 68 | 18 | 50 | 49 |
| SEQ+ARP STRK FILT | 8 | 668 | 216 | 190 | 0 | 0 | 0 | 0 | 2 | 112 | 17 | 67 | 12 | 38 | 32 | 48 | 68 | 16 | 52 | 51 |
| SINGLE SH2 H | 8 | 579 | 194 | 226 | 26 | 22 | 150 | 16 | 2 | 68 | 14 | 30 | 10 | 38 | 22 | 44 | 68 | 8 | 35 | 53 |
| DUALOSC SH2 H | 6 | 671 | 169 | 346 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 28 | 17 | 56 | 51 | 7 | 33 | 86 |
| SPREAD SH2 H | 8 | 692 | 212 | 320 | 120 | 22 | 150 | 16 | 0 | 69 | 14 | 31 | 10 | 38 | 22 | 62 | 68 | 8 | 36 | 66 |
| DUALFLT SH2 H | 7 | 690 | 197 | 336 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 33 | 19 | 39 | 88 | 8 | 34 | 76 |
| STACK SH2 H | 6 | 730 | 193 | 381 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 28 | 17 | 54 | 72 | 12 | 33 | 96 |
| BINAUR SH2 H | 6 | 706 | 207 | 334 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 32 | 10 | 29 | 17 | 35 | 109 | 8 | 39 | 90 |
| STRIKE SH2 H | 8 | 614 | 202 | 252 | 0 | 0 | 0 | 0 | 2 | 69 | 15 | 30 | 10 | 38 | 22 | 46 | 68 | 16 | 34 | 57 |
| SPREAD SH2 CEL+PP | 8 | 731 | 210 | 320 | 120 | 22 | 150 | 16 | 2 | 107 | 15 | 31 | 11 | 36 | 22 | 62 | 68 | 10 | 35 | 66 |
| DUALFLT SH2 CEL | 7 | 730 | 201 | 333 | 0 | 0 | 0 | 0 | 1 | 107 | 15 | 30 | 10 | 33 | 19 | 39 | 91 | 8 | 33 | 76 |
| SEQ+ARP SH2 CEL | 8 | 696 | 210 | 230 | 26 | 22 | 152 | 16 | 2 | 110 | 15 | 65 | 12 | 38 | 32 | 48 | 68 | 10 | 52 | 55 |
| DUALFLT SH2 HP | 7 | 690 | 202 | 331 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 33 | 20 | 40 | 91 | 8 | 34 | 76 |
| BINAUR SH2 HP | 6 | 706 | 208 | 335 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 32 | 10 | 29 | 17 | 35 | 110 | 8 | 37 | 91 |
| SINGLE PCM | 8 | 501 | 174 | 174 | 48 | 6 | 92 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 20 | 62 | 32 | 8 | 32 | 44 |
| DUAL OSC PCM | 6 | 617 | 172 | 292 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 30 | 10 | 29 | 17 | 85 | 25 | 7 | 28 | 78 |
| SPREAD PCM | 8 | 768 | 196 | 416 | 288 | 6 | 92 | 16 | 2 | 69 | 14 | 30 | 13 | 38 | 22 | 82 | 32 | 10 | 28 | 77 |
| DUAL FILTER PCM | 7 | 611 | 191 | 268 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 11 | 33 | 20 | 58 | 62 | 8 | 27 | 66 |
| STACK PCM | 6 | 716 | 199 | 363 | 0 | 0 | 0 | 0 | 1 | 69 | 14 | 32 | 11 | 29 | 17 | 84 | 48 | 12 | 27 | 94 |
| BINAURAL PCM | 6 | 674 | 204 | 314 | 0 | 0 | 0 | 0 | 1 | 69 | 14 | 35 | 11 | 29 | 18 | 52 | 88 | 8 | 26 | 86 |
| DUALOSC MULTI H PCM | 6 | 613 | 204 | 251 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 30 | 11 | 29 | 17 | 87 | 54 | 7 | 31 | 76 |
| DUALFLT MULTI H PCM | 7 | 667 | 225 | 283 | 0 | 0 | 0 | 0 | 1 | 68 | 15 | 30 | 11 | 33 | 20 | 58 | 96 | 8 | 34 | 73 |
| BINAUR MULTI H PCM | 6 | 756 | 248 | 339 | 0 | 0 | 0 | 0 | 1 | 69 | 14 | 35 | 10 | 28 | 18 | 53 | 131 | 8 | 40 | 98 |
| SINGLE RS | 8 | 459 | 156 | 152 | 26 | 6 | 92 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 10 | 30 | 38 |
| SINGLE RS PCM | 8 | 535 | 176 | 208 | 82 | 6 | 92 | 16 | 0 | 67 | 14 | 30 | 10 | 38 | 22 | 64 | 32 | 8 | 30 | 48 |
| DUALOSC RS | 6 | 594 | 141 | 303 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 28 | 17 | 56 | 24 | 7 | 27 | 74 |
| DUALOSC RS PCM | 6 | 617 | 175 | 288 | 0 | 0 | 0 | 0 | 1 | 70 | 14 | 30 | 11 | 29 | 17 | 88 | 25 | 7 | 28 | 77 |
| SEQ+ARP HOME PCM | 8 | 662 | 226 | 218 | 86 | 24 | 80 | 16 | 0 | 70 | 16 | 65 | 12 | 38 | 32 | 66 | 66 | 10 | 55 | 56 |
| SINGLE DRV SOFT | 8 | 497 | 162 | 182 | 26 | 6 | 92 | 46 | 0 | 69 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 16 | 30 | 43 |
| SINGLE DRV HARD | 8 | 499 | 162 | 184 | 26 | 6 | 92 | 48 | 0 | 69 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 16 | 30 | 43 |
| SINGLE DRV FOLD | 8 | 502 | 162 | 188 | 26 | 6 | 92 | 52 | 0 | 67 | 15 | 30 | 10 | 38 | 20 | 44 | 32 | 16 | 30 | 44 |
| SINGLE DRV TAPE | 8 | 509 | 162 | 194 | 26 | 6 | 92 | 58 | 0 | 69 | 15 | 30 | 10 | 38 | 20 | 44 | 32 | 16 | 29 | 44 |
| SINGLE DRV TUBE | 8 | 506 | 162 | 190 | 26 | 6 | 92 | 54 | 0 | 69 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 16 | 31 | 44 |
| SINGLE DRV OD | 8 | 507 | 162 | 192 | 26 | 6 | 92 | 56 | 0 | 69 | 14 | 30 | 10 | 38 | 20 | 44 | 32 | 16 | 30 | 44 |
| STACK HEAVY TUBE | 6 | 682 | 194 | 331 | 0 | 0 | 0 | 0 | 1 | 68 | 14 | 30 | 10 | 28 | 17 | 54 | 68 | 17 | 34 | 88 |
| BINAUR HEAVY OD | 6 | 656 | 205 | 287 | 0 | 0 | 0 | 0 | 1 | 70 | 15 | 31 | 10 | 28 | 17 | 35 | 103 | 13 | 37 | 82 |
| STRIKE HEAVY TUBE | 8 | 579 | 206 | 210 | 0 | 0 | 0 | 0 | 0 | 72 | 15 | 30 | 10 | 36 | 22 | 44 | 66 | 22 | 36 | 52 |
| SEQ+ARP HOME TUBE | 8 | 626 | 212 | 198 | 26 | 24 | 80 | 56 | 0 | 71 | 16 | 65 | 12 | 38 | 32 | 46 | 66 | 16 | 52 | 51 |
| FULL | 4 | 512 | 112 | 254 | 0 | 0 | 0 | 0 | 0 | 67 | 14 | 30 | 9 | 19 | 10 | 37 | 35 | 5 | 26 | 92 |
| FULL HEAVY | 4 | 527 | 128 | 246 | 0 | 0 | 0 | 0 | 0 | 67 | 14 | 30 | 9 | 19 | 10 | 37 | 51 | 5 | 33 | 94 |
| FULL MULTI H | 4 | 550 | 136 | 258 | 0 | 0 | 0 | 0 | 0 | 68 | 15 | 30 | 10 | 19 | 11 | 38 | 57 | 5 | 33 | 98 |
| FULL SH2 H | 4 | 600 | 132 | 314 | 0 | 0 | 0 | 0 | 0 | 68 | 14 | 30 | 10 | 19 | 11 | 37 | 52 | 5 | 32 | 112 |
| FULL H CEL+PP | 4 | 591 | 137 | 258 | 0 | 0 | 0 | 0 | 0 | 108 | 14 | 30 | 11 | 19 | 11 | 38 | 57 | 5 | 33 | 99 |
| FULL MULTI H PCM | 4 | 492 | 153 | 185 | 0 | 0 | 0 | 0 | 0 | 68 | 14 | 30 | 10 | 19 | 10 | 56 | 56 | 5 | 32 | 84 |
| FULL SH2 H PCM | 4 | 590 | 154 | 280 | 0 | 0 | 0 | 0 | 1 | 69 | 15 | 31 | 10 | 19 | 11 | 59 | 53 | 5 | 30 | 109 |
| FULL SH2 CEL+PP | 4 | 640 | 132 | 313 | 0 | 0 | 0 | 0 | 0 | 107 | 14 | 30 | 10 | 19 | 11 | 38 | 52 | 5 | 34 | 111 |
| DSPRD | 4 | 438 | 109 | 183 | 0 | 0 | 0 | 0 | 1 | 67 | 14 | 30 | 9 | 19 | 10 | 52 | 16 | 5 | 25 | 73 |
| DSPRD HEAVY | 4 | 462 | 126 | 185 | 0 | 0 | 0 | 0 | 0 | 67 | 14 | 30 | 9 | 19 | 10 | 53 | 32 | 5 | 31 | 78 |
| DSPRD MULTI H | 4 | 472 | 129 | 191 | 0 | 0 | 0 | 0 | 0 | 67 | 14 | 30 | 10 | 19 | 10 | 53 | 36 | 5 | 31 | 80 |
| DSPRD SH2 H | 4 | 498 | 127 | 220 | 0 | 0 | 0 | 0 | 0 | 67 | 14 | 30 | 9 | 19 | 11 | 53 | 34 | 5 | 31 | 87 |
| DSPRD H CEL+PP | 4 | 513 | 130 | 192 | 0 | 0 | 0 | 0 | 1 | 106 | 14 | 30 | 10 | 19 | 10 | 53 | 36 | 5 | 30 | 81 |
| DSPRD MULTI H PCM | 4 | 667 | 152 | 359 | 0 | 0 | 0 | 0 | 0 | 69 | 15 | 31 | 11 | 19 | 11 | 73 | 37 | 5 | 30 | 128 |
| DSPRD SH2 CEL+PP | 4 | 545 | 131 | 220 | 0 | 0 | 0 | 0 | 1 | 107 | 15 | 30 | 10 | 19 | 11 | 54 | 35 | 5 | 31 | 88 |
| SEQ+ARP DSPRD CEL | 4 | 590 | 144 | 195 | 0 | 0 | 0 | 0 | 1 | 111 | 15 | 65 | 12 | 19 | 17 | 57 | 39 | 5 | 47 | 85 |
| IDLE PHASER | 0 | 157 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 83 | 14 | 29 | 9 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| IDLE PHASER x2 | 0 | 174 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 100 | 14 | 29 | 9 | 0 | 0 | 0 | 0 | 0 | 22 | 0 |
| SPREAD PHASER | 8 | 596 | 178 | 246 | 120 | 6 | 92 | 16 | 0 | 88 | 14 | 30 | 11 | 38 | 22 | 62 | 34 | 10 | 29 | 53 |
| SPREAD H CEL PHSR | 8 | 701 | 216 | 262 | 120 | 22 | 92 | 16 | 0 | 128 | 14 | 31 | 12 | 38 | 22 | 64 | 72 | 10 | 38 | 60 |
| SEQ+ARP HOME PHSR2 | 8 | 618 | 206 | 160 | 26 | 24 | 80 | 16 | 2 | 107 | 16 | 65 | 12 | 38 | 32 | 46 | 66 | 10 | 50 | 46 |
| IDLE ENSEMBLE | 0 | 159 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 14 | 29 | 9 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE ENSEMBLE x2 | 0 | 179 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 105 | 14 | 30 | 9 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE ENS+PHSR | 0 | 165 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 91 | 14 | 30 | 9 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE ENS+PHSR x2 | 0 | 188 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 114 | 14 | 30 | 9 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE CHORUS | 0 | 160 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 86 | 14 | 30 | 9 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| IDLE CHORUS x2 | 0 | 179 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 105 | 14 | 30 | 9 | 0 | 0 | 0 | 0 | 0 | 21 | 0 |
| SPREAD H CEL ENPH | 8 | 717 | 220 | 264 | 120 | 22 | 92 | 16 | 2 | 139 | 15 | 31 | 13 | 38 | 22 | 64 | 72 | 10 | 33 | 61 |
| SEQ+ARP HOME ENPH2 | 8 | 648 | 208 | 160 | 26 | 24 | 80 | 16 | 2 | 130 | 16 | 67 | 13 | 38 | 32 | 46 | 68 | 10 | 52 | 46 |

### 2026-11-02 11:17, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 20.4 | 20.8 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 28.5 | 29.4 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 48.3 | 49.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.4 | 53.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 61.7 | 62.9 | 0 | 6 | 52.9 | 8 | pass |
| SPREAD | 58.8 | 59.7 | 0 | 8 | 57.2 | 8 | pass |
| DUAL FILTER | 61.7 | 62.9 | 0 | 7 | 52.6 | 8 | pass |
| STACK | 63.8 | 65.0 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 56.6 | 57.9 | 0 | 6 | 57.6 | 7 | pass |
| DUAL OSC HEAVY | 64.4 | 65.4 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD HEAVY | 62.6 | 63.7 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FLT HEAVY | 63.9 | 65.1 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 64.8 | 66.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 62.1 | 63.3 | 0 | 6 | 54.2 | 8 | pass |
| SINGLE LADDER | 48.3 | 49.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 45.8 | 47.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 53.0 | 54.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 46.2 | 47.2 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 61.7 | 63.0 | 0 | 7 | 53.5 | 8 | pass |
| SEQ+ARP HOME | 64.2 | 67.7 | 0 | 8 | 51.5 | 10 | pass |
| SEQ+ARP AMP | 64.3 | 67.4 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 63.5 | 66.6 | 0 | 8 | 13.6 | 127 | pass |
| SEQ+ARP MOD1 | 64.1 | 68.0 | 0 | 8 | 15.5 | 70 | pass |
| LADDER FILTER PG | 48.2 | 49.2 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.6 | 56.6 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.7 | 57.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.3 | 72.1 | 0 | 8 | 45.6 | 10 | pass |
| SINGLE MULTI H | 54.8 | 55.8 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 65.3 | 66.4 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD MULTI H | 65.2 | 66.2 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 68.1 | 69.5 | 0 | 7 | 45.6 | 9 | pass |
| STACK MULTI H | 67.8 | 69.3 | 0 | 6 | 45.9 | 9 | pass |
| BINAUR MULTI H | 64.9 | 66.2 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 68.8 | 70.6 | 0 | 8 | 43.6 | 9 | pass |
| DUALOSC H CEL+PP | 69.2 | 70.9 | 0 | 6 | 42.9 | 1 | pass |
| SPREAD H TUM+PP | 67.4 | 68.8 | 0 | 8 | 46.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 80.6 | 84.3 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 56.2 | 57.2 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CEL+BBD PP | 56.8 | 57.9 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 67.8 | 70.8 | 0 | 8 | 46.5 | 10 | pass |
| STRIKE | 48.8 | 49.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.5 | 54.6 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 73.7 | 77.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 74.5 | 78.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 59.5 | 60.6 | 0 | 8 | 55.9 | 8 | pass |
| DUALOSC SH2 H | 68.8 | 70.0 | 0 | 6 | 44.9 | 9 | pass |
| SPREAD SH2 H | 69.7 | 71.0 | 0 | 8 | 42.9 | 1 | pass |
| DUALFLT SH2 H | 77.1 | 78.6 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.4 | 76.0 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 72.1 | 73.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 60.1 | 61.2 | 0 | 8 | 55.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 73.7 | 75.2 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 81.1 | 82.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.3 | 78.7 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 77.2 | 78.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 72.1 | 73.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 52.7 | 54.0 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 60.8 | 61.9 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD PCM | 66.6 | 67.9 | 0 | 8 | 51.2 | 9 | pass |
| DUAL FILTER PCM | 66.2 | 67.6 | 0 | 7 | 51.5 | 8 | pass |
| STACK PCM | 69.6 | 71.3 | 0 | 6 | 43.2 | 9 | pass |
| BINAURAL PCM | 63.2 | 64.5 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 59.4 | 60.5 | 0 | 6 | 56.6 | 8 | pass |
| DUALFLT MULTI H PCM | 72.6 | 74.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.5 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 48.3 | 49.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.9 | 57.7 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 61.8 | 63.2 | 0 | 6 | 53.9 | 8 | pass |
| DUALOSC RS PCM | 64.3 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 73.6 | 77.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 52.0 | 53.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 52.2 | 53.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 52.2 | 53.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 53.1 | 54.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 52.9 | 54.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 53.0 | 54.1 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 71.9 | 73.6 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 68.6 | 70.1 | 0 | 6 | 45.2 | 9 | pass |
| STRIKE HEAVY TUBE | 58.8 | 60.0 | 0 | 8 | 57.2 | 8 | pass |
| SEQ+ARP HOME TUBE | 69.6 | 73.7 | 0 | 8 | 43.5 | 10 | pass |
| FULL | 55.5 | 56.6 | 0 | 4 | 57.5 | 1 | pass |
| FULL HEAVY | 59.5 | 61.0 | 0 | 4 | 56.5 | 8 | pass |
| FULL MULTI H | 59.5 | 60.7 | 0 | 4 | 56.6 | 8 | pass |
| FULL SH2 H | 64.0 | 65.1 | 0 | 4 | 51.9 | 8 | pass |
| FULL H CEL+PP | 63.5 | 64.8 | 0 | 4 | 51.9 | 8 | pass |
| FULL MULTI H PCM | 54.5 | 55.7 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 60.9 | 62.0 | 0 | 4 | 54.5 | 8 | pass |
| FULL SH2 CEL+PP | 68.1 | 69.4 | 0 | 4 | 45.6 | 9 | pass |
| DSPRD | 48.3 | 49.4 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 50.7 | 51.7 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 51.9 | 52.9 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 54.1 | 55.1 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 55.9 | 57.1 | 0 | 4 | 57.5 | 7 | pass |
| DSPRD MULTI H PCM | 60.0 | 61.3 | 0 | 4 | 55.2 | 8 | pass |
| DSPRD SH2 CEL+PP | 58.2 | 59.3 | 0 | 4 | 57.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 67.2 | 70.7 | 0 | 4 | 47.9 | 10 | pass |
| IDLE PHASER | 23.7 | 24.2 | 0 | 0 | 61.0 | 3 | pass |
| IDLE PHASER x2 | 23.7 | 24.3 | 0 | 0 | 61.0 | 3 | pass |
| SPREAD PHASER | 62.5 | 63.7 | 0 | 8 | 52.8 | 8 | pass |
| SPREAD H CEL PHSR | 72.8 | 74.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 68.3 | 72.1 | 0 | 8 | 44.6 | 10 | pass |
| IDLE ENSEMBLE | 24.4 | 25.0 | 0 | 0 | 61.0 | 3 | pass |
| IDLE ENSEMBLE x2 | 24.5 | 25.2 | 0 | 0 | 60.9 | 4 | pass |
| IDLE ENS+PHSR | 25.4 | 26.1 | 0 | 0 | 60.0 | 1 | pass |
| IDLE ENS+PHSR x2 | 25.5 | 26.2 | 0 | 0 | 60.3 | 1 | pass |
| IDLE CHORUS | 24.4 | 25.0 | 0 | 0 | 61.0 | 3 | pass |
| IDLE CHORUS x2 | 24.5 | 25.1 | 0 | 0 | 60.9 | 3 | pass |
| SPREAD H CEL ENPH | 76.8 | 78.6 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 72.2 | 76.4 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-11-02 11:03, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.4 | 19.9 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 27.5 | 28.5 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 47.3 | 48.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.4 | 52.6 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.5 | 61.6 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD | 57.9 | 58.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FILTER | 60.4 | 61.5 | 0 | 7 | 55.9 | 1 | pass |
| STACK | 63.0 | 64.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL | 55.7 | 56.9 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.0 | 64.1 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD HEAVY | 61.6 | 62.7 | 0 | 8 | 53.9 | 8 | pass |
| DUAL FLT HEAVY | 62.7 | 63.8 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.7 | 64.9 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 62.3 | 0 | 6 | 54.9 | 8 | pass |
| SINGLE LADDER | 47.3 | 48.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.8 | 45.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 52.0 | 53.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.2 | 46.2 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.4 | 61.5 | 0 | 7 | 56.2 | 1 | pass |
| SEQ+ARP HOME | 62.9 | 66.3 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 63.0 | 66.5 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 62.3 | 66.1 | 0 | 8 | 15.2 | 125 | pass |
| SEQ+ARP MOD1 | 62.8 | 66.9 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 47.2 | 48.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.6 | 55.7 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CELEST+PP | 55.6 | 56.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.1 | 70.3 | 0 | 8 | 49.2 | 10 | pass |
| SINGLE MULTI H | 53.7 | 54.7 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 64.2 | 65.4 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD MULTI H | 64.2 | 65.3 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 66.8 | 68.0 | 0 | 7 | 50.2 | 9 | pass |
| STACK MULTI H | 66.8 | 68.1 | 0 | 6 | 49.6 | 9 | pass |
| BINAUR MULTI H | 63.8 | 65.1 | 0 | 6 | 51.9 | 1 | pass |
| SPREAD H CEL+PP | 67.9 | 69.5 | 0 | 8 | 47.2 | 9 | pass |
| DUALOSC H CEL+PP | 68.2 | 69.8 | 0 | 6 | 46.9 | 9 | pass |
| SPREAD H TUM+PP | 66.5 | 67.7 | 0 | 8 | 51.6 | 9 | pass |
| SEQ+ARP SPRD CEL | 79.6 | 83.9 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.9 | 56.0 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.8 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 66.6 | 69.4 | 0 | 8 | 49.6 | 10 | pass |
| STRIKE | 47.7 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.5 | 53.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.8 | 75.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.6 | 76.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.3 | 59.3 | 0 | 8 | 57.3 | 8 | pass |
| DUALOSC SH2 H | 67.7 | 68.9 | 0 | 6 | 45.2 | 9 | pass |
| SPREAD SH2 H | 68.6 | 69.8 | 0 | 8 | 46.2 | 9 | pass |
| DUALFLT SH2 H | 75.9 | 77.3 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.5 | 74.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 70.9 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.1 | 60.2 | 0 | 8 | 56.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 72.6 | 74.1 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.1 | 81.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.2 | 77.3 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 76.0 | 77.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.9 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.6 | 52.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.3 | 60.3 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD PCM | 65.4 | 66.7 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 64.8 | 66.0 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 68.7 | 70.2 | 0 | 6 | 46.6 | 9 | pass |
| BINAURAL PCM | 62.2 | 63.4 | 0 | 6 | 52.6 | 8 | pass |
| DUALOSC MULTI H PCM | 57.9 | 59.0 | 0 | 6 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 71.1 | 72.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.5 | 72.1 | 0 | 6 | 43.6 | 9 | FAIL |
| SINGLE RS | 47.3 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.7 | 56.6 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.6 | 62.2 | 0 | 6 | 55.5 | 8 | pass |
| DUALOSC RS PCM | 62.7 | 64.2 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 71.4 | 75.2 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.0 | 52.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.2 | 52.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 51.1 | 52.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 52.0 | 53.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 51.8 | 52.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 51.9 | 52.9 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 70.2 | 71.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.4 | 68.9 | 0 | 6 | 46.2 | 9 | pass |
| STRIKE HEAVY TUBE | 57.1 | 58.2 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 68.0 | 71.8 | 0 | 8 | 46.6 | 10 | pass |
| FULL | 54.1 | 55.1 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 57.1 | 58.3 | 0 | 4 | 57.6 | 7 | pass |
| FULL MULTI H | 57.6 | 58.9 | 0 | 4 | 57.6 | 7 | pass |
| FULL SH2 H | 62.7 | 63.7 | 0 | 4 | 52.2 | 8 | pass |
| FULL H CEL+PP | 61.8 | 63.1 | 0 | 4 | 54.5 | 8 | pass |
| FULL MULTI H PCM | 53.3 | 54.5 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 59.4 | 60.6 | 0 | 4 | 56.5 | 8 | pass |
| FULL SH2 CEL+PP | 66.9 | 68.1 | 0 | 4 | 46.9 | 9 | pass |
| DSPRD | 47.2 | 48.3 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.9 | 50.9 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 50.9 | 51.9 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 H | 53.1 | 54.1 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD H CEL+PP | 54.8 | 56.1 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD MULTI H PCM | 58.8 | 59.9 | 0 | 4 | 57.2 | 8 | pass |
| DSPRD SH2 CEL+PP | 57.3 | 58.5 | 0 | 4 | 57.6 | 1 | pass |
| SEQ+ARP DSPRD CEL | 66.4 | 70.4 | 0 | 4 | 49.9 | 10 | pass |
| IDLE PHASER | 22.7 | 23.2 | 0 | 0 | 61.3 | 3 | pass |
| IDLE PHASER x2 | 25.8 | 26.4 | 0 | 0 | 59.9 | 1 | pass |
| SPREAD PHASER | 61.6 | 63.0 | 0 | 8 | 54.2 | 8 | pass |
| SPREAD H CEL PHSR | 71.9 | 73.7 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 70.1 | 73.3 | 0 | 8 | 43.6 | 10 | FAIL |
| IDLE ENSEMBLE | 23.3 | 23.9 | 0 | 0 | 60.6 | 1 | pass |
| IDLE ENSEMBLE x2 | 26.8 | 27.5 | 0 | 0 | 60.3 | 4 | pass |
| IDLE ENS+PHSR | 24.4 | 25.1 | 0 | 0 | 61.0 | 3 | pass |
| IDLE ENS+PHSR x2 | 28.7 | 29.5 | 0 | 0 | 59.0 | 4 | pass |
| IDLE CHORUS | 23.4 | 24.0 | 0 | 0 | 61.0 | 3 | pass |
| IDLE CHORUS x2 | 26.9 | 27.5 | 0 | 0 | 59.6 | 4 | pass |
| SPREAD H CEL ENPH | 75.4 | 77.2 | 0 | 8 | 43.2 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 75.5 | 79.1 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-11-02 10:21, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.6 | 20.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 27.6 | 28.5 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 47.4 | 48.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.6 | 52.8 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.9 | 61.9 | 0 | 6 | 56.2 | 8 | pass |
| SPREAD | 57.6 | 58.6 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 60.7 | 61.8 | 0 | 7 | 55.6 | 8 | pass |
| STACK | 63.0 | 64.0 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 57.1 | 58.1 | 0 | 6 | 57.6 | 7 | pass |
| DUAL OSC HEAVY | 62.9 | 64.0 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD HEAVY | 61.7 | 62.8 | 0 | 8 | 53.2 | 8 | pass |
| DUAL FLT HEAVY | 62.7 | 63.7 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.6 | 64.7 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 61.6 | 62.6 | 0 | 6 | 52.9 | 8 | pass |
| SINGLE LADDER | 47.3 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.8 | 45.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 51.9 | 52.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.3 | 46.3 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.7 | 61.9 | 0 | 7 | 55.2 | 8 | pass |
| SEQ+ARP HOME | 64.3 | 67.6 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 64.5 | 68.3 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 63.8 | 66.7 | 0 | 8 | 12.9 | 127 | pass |
| SEQ+ARP MOD1 | 64.2 | 68.1 | 0 | 8 | 15.3 | 70 | pass |
| LADDER FILTER PG | 47.4 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.5 | 55.6 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CELEST+PP | 55.8 | 56.9 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.9 | 73.0 | 0 | 8 | 45.6 | 10 | pass |
| SINGLE MULTI H | 53.9 | 55.0 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC MULTI H | 64.7 | 65.7 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD MULTI H | 64.1 | 65.2 | 0 | 8 | 51.9 | 1 | pass |
| DUALFLT MULTI H | 67.1 | 68.3 | 0 | 7 | 46.9 | 9 | pass |
| STACK MULTI H | 67.1 | 68.2 | 0 | 6 | 45.2 | 9 | pass |
| BINAUR MULTI H | 65.3 | 66.4 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 67.7 | 69.1 | 0 | 8 | 45.2 | 1 | pass |
| DUALOSC H CEL+PP | 68.8 | 70.4 | 0 | 6 | 44.6 | 9 | pass |
| SPREAD H TUM+PP | 66.3 | 67.5 | 0 | 8 | 52.2 | 8 | pass |
| SEQ+ARP SPRD CEL | 80.3 | 83.8 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.4 | 56.4 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.5 | 57.6 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP SPR+BBD | 67.8 | 70.8 | 0 | 8 | 45.9 | 10 | pass |
| STRIKE | 48.2 | 49.4 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.4 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 72.7 | 76.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 73.3 | 77.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.6 | 59.5 | 0 | 8 | 57.5 | 8 | pass |
| DUALOSC SH2 H | 68.0 | 69.3 | 0 | 6 | 44.9 | 9 | pass |
| SPREAD SH2 H | 68.7 | 69.9 | 0 | 8 | 46.2 | 9 | pass |
| DUALFLT SH2 H | 76.1 | 77.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.4 | 75.6 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 72.3 | 73.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.4 | 60.4 | 0 | 8 | 56.3 | 8 | pass |
| SPREAD SH2 CEL+PP | 72.8 | 74.3 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.4 | 81.8 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.6 | 79.4 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 76.1 | 77.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 72.3 | 73.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.7 | 52.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.4 | 60.5 | 0 | 6 | 56.5 | 8 | pass |
| SPREAD PCM | 65.8 | 67.0 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 65.0 | 66.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 69.2 | 70.7 | 0 | 6 | 43.9 | 9 | pass |
| BINAURAL PCM | 64.7 | 65.8 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 58.3 | 59.4 | 0 | 6 | 57.6 | 8 | pass |
| DUALFLT MULTI H PCM | 71.6 | 72.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 73.3 | 74.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.4 | 49.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.6 | 57.6 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 61.0 | 62.5 | 0 | 6 | 55.5 | 8 | pass |
| DUALOSC RS PCM | 63.7 | 65.4 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 74.1 | 77.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.3 | 52.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.5 | 52.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 51.4 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.3 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.2 | 53.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 52.3 | 53.3 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 70.5 | 72.1 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAUR HEAVY OD | 68.1 | 69.3 | 0 | 6 | 45.9 | 9 | pass |
| STRIKE HEAVY TUBE | 57.4 | 58.6 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 71.9 | 76.0 | 0 | 8 | 43.2 | 10 | FAIL |
| FULL | 54.3 | 55.4 | 0 | 4 | 57.5 | 1 | pass |
| FULL HEAVY | 57.4 | 58.7 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H | 57.4 | 58.6 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 62.7 | 63.8 | 0 | 4 | 52.2 | 8 | pass |
| FULL H CEL+PP | 61.8 | 63.0 | 0 | 4 | 53.2 | 8 | pass |
| FULL MULTI H PCM | 53.5 | 54.7 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 58.8 | 59.9 | 0 | 4 | 56.9 | 8 | pass |
| FULL SH2 CEL+PP | 67.5 | 69.0 | 0 | 4 | 44.9 | 9 | pass |
| DSPRD | 47.3 | 48.4 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.6 | 50.7 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 51.0 | 52.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 53.1 | 54.2 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 54.8 | 56.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 59.5 | 60.5 | 0 | 4 | 56.9 | 8 | pass |
| DSPRD SH2 CEL+PP | 57.7 | 58.9 | 0 | 4 | 57.6 | 7 | pass |
| SEQ+ARP DSPRD CEL | 66.8 | 70.6 | 0 | 4 | 49.9 | 10 | pass |
| IDLE PHASER | 23.2 | 23.8 | 0 | 0 | 61.0 | 3 | pass |
| IDLE PHASER x2 | 26.7 | 27.3 | 0 | 0 | 59.9 | 1 | pass |
| SPREAD PHASER | 62.0 | 63.1 | 0 | 8 | 53.5 | 8 | pass |
| SPREAD H CEL PHSR | 72.3 | 74.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 72.9 | 76.1 | 0 | 8 | 42.9 | 2 | FAIL |
| IDLE ENSEMBLE | 23.6 | 24.3 | 0 | 0 | 61.0 | 3 | pass |
| IDLE ENSEMBLE x2 | 27.2 | 27.9 | 0 | 0 | 60.0 | 4 | pass |
| IDLE ENS+PHSR | 24.5 | 25.4 | 0 | 0 | 60.6 | 4 | pass |
| IDLE ENS+PHSR x2 | 29.0 | 29.8 | 0 | 0 | 60.0 | 4 | pass |
| IDLE CHORUS | 23.5 | 24.2 | 0 | 0 | 61.0 | 3 | pass |
| IDLE CHORUS x2 | 27.3 | 27.9 | 0 | 0 | 60.0 | 4 | pass |
| SPREAD H CEL ENPH | 75.5 | 77.1 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME ENPH2 | 77.2 | 81.5 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-11-01 17:51, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.5 | 20.0 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 27.3 | 28.3 | 0 | 1 | 59.9 | 4 | pass |
| SINGLE | 47.1 | 48.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.5 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.3 | 61.3 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD | 57.3 | 58.4 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 60.2 | 61.2 | 0 | 7 | 55.2 | 8 | pass |
| STACK | 63.4 | 64.4 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL | 56.3 | 57.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.8 | 63.9 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD HEAVY | 61.5 | 62.6 | 0 | 8 | 53.9 | 8 | pass |
| DUAL FLT HEAVY | 62.8 | 63.8 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 63.7 | 64.8 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 61.4 | 62.4 | 0 | 6 | 56.2 | 8 | pass |
| SINGLE LADDER | 47.1 | 48.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.9 | 45.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 52.0 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 45.1 | 46.1 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.2 | 61.3 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 64.1 | 67.4 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 64.2 | 67.5 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 63.4 | 66.5 | 0 | 8 | 13.3 | 127 | pass |
| SEQ+ARP MOD1 | 64.0 | 68.5 | 0 | 8 | 15.9 | 70 | pass |
| LADDER FILTER PG | 47.0 | 48.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.5 | 55.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.8 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 68.7 | 72.4 | 0 | 8 | 45.2 | 10 | pass |
| SINGLE MULTI H | 53.7 | 54.9 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.0 | 65.2 | 0 | 6 | 52.2 | 1 | pass |
| SPREAD MULTI H | 63.8 | 65.1 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 66.8 | 67.9 | 0 | 7 | 50.9 | 9 | pass |
| STACK MULTI H | 67.4 | 68.7 | 0 | 6 | 46.6 | 9 | pass |
| BINAUR MULTI H | 64.4 | 65.6 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 67.9 | 69.2 | 0 | 8 | 46.2 | 9 | pass |
| DUALOSC H CEL+PP | 68.4 | 70.2 | 0 | 6 | 45.6 | 9 | pass |
| SPREAD H TUM+PP | 66.5 | 67.7 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 80.7 | 84.4 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.0 | 56.0 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.4 | 57.6 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP SPR+BBD | 67.5 | 71.1 | 0 | 8 | 47.5 | 10 | pass |
| STRIKE | 47.8 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.2 | 53.3 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 72.4 | 75.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 73.2 | 77.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.7 | 59.7 | 0 | 8 | 57.5 | 8 | pass |
| DUALOSC SH2 H | 68.0 | 69.2 | 0 | 6 | 45.9 | 9 | pass |
| SPREAD SH2 H | 68.7 | 70.0 | 0 | 8 | 45.6 | 9 | pass |
| DUALFLT SH2 H | 76.2 | 77.5 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.6 | 75.9 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 72.3 | 73.5 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.4 | 60.4 | 0 | 8 | 56.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 73.0 | 74.6 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.4 | 82.1 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.0 | 78.4 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 76.2 | 77.5 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 72.3 | 73.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.7 | 53.2 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.7 | 60.8 | 0 | 6 | 56.5 | 8 | pass |
| SPREAD PCM | 65.5 | 66.7 | 0 | 8 | 51.5 | 8 | pass |
| DUAL FILTER PCM | 64.8 | 66.0 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 69.7 | 71.1 | 0 | 6 | 42.9 | 1 | pass |
| BINAURAL PCM | 64.2 | 65.3 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 58.5 | 59.6 | 0 | 6 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 71.5 | 72.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 72.6 | 73.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.1 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.1 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 60.4 | 62.2 | 0 | 6 | 55.9 | 8 | pass |
| DUALOSC RS PCM | 63.1 | 64.9 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 74.2 | 78.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.9 | 52.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.1 | 52.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 51.0 | 52.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.9 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 51.7 | 52.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 51.8 | 52.9 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 70.9 | 72.5 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAUR HEAVY OD | 67.8 | 69.0 | 0 | 6 | 44.9 | 9 | pass |
| STRIKE HEAVY TUBE | 56.9 | 58.0 | 0 | 8 | 57.3 | 7 | pass |
| SEQ+ARP HOME TUBE | 72.3 | 76.4 | 0 | 8 | 42.9 | 2 | FAIL |
| FULL | 53.9 | 55.0 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 57.6 | 58.8 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 57.5 | 58.7 | 0 | 4 | 57.6 | 7 | pass |
| FULL SH2 H | 62.1 | 63.2 | 0 | 4 | 53.2 | 8 | pass |
| FULL H CEL+PP | 62.2 | 63.5 | 0 | 4 | 53.2 | 8 | pass |
| FULL MULTI H PCM | 54.4 | 56.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 60.2 | 61.5 | 0 | 4 | 56.2 | 8 | pass |
| FULL SH2 CEL+PP | 67.1 | 68.4 | 0 | 4 | 46.9 | 9 | pass |
| DSPRD | 47.0 | 48.0 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.6 | 50.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 50.7 | 51.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 53.3 | 54.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 55.2 | 56.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 59.0 | 60.1 | 0 | 4 | 56.5 | 8 | pass |
| DSPRD SH2 CEL+PP | 57.8 | 59.0 | 0 | 4 | 57.2 | 7 | pass |
| SEQ+ARP DSPRD CEL | 67.5 | 71.4 | 0 | 4 | 46.9 | 10 | pass |
| IDLE PHASER | 22.7 | 23.3 | 0 | 0 | 61.3 | 3 | pass |
| IDLE PHASER x2 | 25.9 | 26.6 | 0 | 0 | 60.2 | 4 | pass |
| SPREAD PHASER | 61.0 | 62.0 | 0 | 8 | 56.2 | 8 | pass |
| SPREAD H CEL PHSR | 71.3 | 72.9 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 71.1 | 75.4 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-11-01 17:27, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.3 | 19.8 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.9 | 27.9 | 0 | 1 | 60.2 | 4 | pass |
| SINGLE | 46.7 | 47.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.3 | 52.5 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.4 | 61.5 | 0 | 6 | 55.5 | 8 | pass |
| SPREAD | 57.2 | 58.1 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 60.0 | 61.1 | 0 | 7 | 56.6 | 8 | pass |
| STACK | 62.6 | 63.7 | 0 | 6 | 52.6 | 8 | pass |
| BINAURAL | 56.5 | 57.5 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.0 | 64.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD HEAVY | 61.4 | 62.3 | 0 | 8 | 55.2 | 8 | pass |
| DUAL FLT HEAVY | 62.6 | 63.8 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 63.7 | 64.9 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.5 | 62.5 | 0 | 6 | 53.9 | 8 | pass |
| SINGLE LADDER | 46.7 | 47.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.6 | 45.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 51.8 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.0 | 45.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.1 | 61.0 | 0 | 7 | 56.2 | 8 | pass |
| SEQ+ARP HOME | 62.4 | 65.8 | 0 | 8 | 52.5 | 10 | pass |
| SEQ+ARP AMP | 62.5 | 65.8 | 0 | 8 | 52.5 | 11 | pass |
| SEQ+ARP SEQ | 61.9 | 65.4 | 0 | 8 | 15.5 | 119 | pass |
| SEQ+ARP MOD1 | 62.4 | 67.0 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.6 | 47.6 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.2 | 55.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.9 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.1 | 70.6 | 0 | 8 | 47.2 | 10 | pass |
| SINGLE MULTI H | 53.2 | 54.2 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.2 | 65.4 | 0 | 6 | 52.2 | 1 | pass |
| SPREAD MULTI H | 63.7 | 64.8 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 66.6 | 67.7 | 0 | 7 | 52.2 | 9 | pass |
| STACK MULTI H | 66.8 | 67.9 | 0 | 6 | 51.6 | 9 | pass |
| BINAUR MULTI H | 64.8 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 67.7 | 69.2 | 0 | 8 | 45.2 | 1 | pass |
| DUALOSC H CEL+PP | 68.6 | 70.1 | 0 | 6 | 45.9 | 9 | pass |
| SPREAD H TUM+PP | 66.2 | 67.5 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 80.0 | 84.7 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.6 | 55.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 56.5 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 66.2 | 69.6 | 0 | 8 | 49.6 | 10 | pass |
| STRIKE | 47.3 | 48.3 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.4 | 53.6 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 73.6 | 77.5 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.8 | 76.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.3 | 59.4 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC SH2 H | 67.8 | 69.0 | 0 | 6 | 45.6 | 1 | pass |
| SPREAD SH2 H | 68.5 | 69.8 | 0 | 8 | 47.2 | 9 | pass |
| DUALFLT SH2 H | 76.0 | 77.4 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.6 | 74.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.6 | 72.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.0 | 60.1 | 0 | 8 | 57.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 72.6 | 74.1 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.2 | 81.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 73.7 | 77.7 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 76.0 | 77.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.5 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.8 | 53.0 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 60.7 | 61.7 | 0 | 6 | 54.5 | 8 | pass |
| SPREAD PCM | 65.5 | 66.5 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 65.2 | 66.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 70.4 | 71.7 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAURAL PCM | 66.7 | 67.9 | 0 | 6 | 49.6 | 9 | pass |
| DUALOSC MULTI H PCM | 59.7 | 60.8 | 0 | 6 | 56.6 | 8 | pass |
| DUALFLT MULTI H PCM | 72.0 | 73.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 75.1 | 76.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.7 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.2 | 57.3 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.5 | 62.2 | 0 | 6 | 56.2 | 8 | pass |
| DUALOSC RS PCM | 64.1 | 65.8 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 71.3 | 75.6 | 0 | 8 | 43.2 | 11 | FAIL |
| SINGLE DRV SOFT | 50.5 | 51.5 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 50.7 | 51.6 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 50.6 | 51.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.5 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 51.3 | 52.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.4 | 52.4 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 71.5 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 68.3 | 69.6 | 0 | 6 | 44.9 | 9 | pass |
| STRIKE HEAVY TUBE | 58.5 | 59.7 | 0 | 8 | 57.6 | 8 | pass |
| SEQ+ARP HOME TUBE | 69.5 | 73.1 | 0 | 8 | 43.9 | 10 | pass |
| FULL | 53.7 | 54.8 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 56.1 | 57.2 | 0 | 4 | 57.5 | 7 | pass |
| FULL MULTI H | 57.5 | 58.5 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 62.6 | 63.8 | 0 | 4 | 52.2 | 8 | pass |
| FULL H CEL+PP | 62.2 | 63.3 | 0 | 4 | 53.6 | 8 | pass |
| FULL MULTI H PCM | 54.4 | 55.6 | 0 | 4 | 57.6 | 1 | pass |
| FULL SH2 H PCM | 58.5 | 59.6 | 0 | 4 | 57.2 | 8 | pass |
| FULL SH2 CEL+PP | 67.1 | 68.5 | 0 | 4 | 45.2 | 9 | pass |
| DSPRD | 47.0 | 48.1 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 49.5 | 50.8 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 50.8 | 51.9 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 53.2 | 54.5 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 55.2 | 56.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 59.5 | 60.8 | 0 | 4 | 56.5 | 8 | pass |
| DSPRD SH2 CEL+PP | 57.8 | 59.1 | 0 | 4 | 57.2 | 8 | pass |
| SEQ+ARP DSPRD CEL | 67.4 | 71.1 | 0 | 4 | 47.9 | 10 | pass |
| IDLE PHASER | 26.1 | 26.8 | 0 | 0 | 60.3 | 4 | pass |
| IDLE PHASER x2 | 32.7 | 33.4 | 0 | 0 | 57.2 | 1 | pass |
| SPREAD PHASER | 64.4 | 65.5 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD H CEL PHSR | 74.9 | 76.4 | 0 | 8 | 42.9 | 1 | FAIL |
| SEQ+ARP HOME PHSR2 | 76.0 | 79.4 | 0 | 8 | 43.2 | 2 | FAIL |

### 2026-10-29 10:35, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.6 | 19.0 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 26.2 | 27.2 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 46.7 | 47.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.2 | 52.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 59.9 | 60.8 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD | 57.1 | 58.1 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 60.3 | 61.4 | 0 | 7 | 55.6 | 8 | pass |
| STACK | 62.7 | 63.8 | 0 | 6 | 52.5 | 8 | pass |
| BINAURAL | 55.6 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 62.3 | 63.4 | 0 | 6 | 52.9 | 8 | pass |
| SPREAD HEAVY | 61.5 | 62.4 | 0 | 8 | 55.2 | 8 | pass |
| DUAL FLT HEAVY | 62.6 | 63.6 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 63.3 | 64.5 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 62.2 | 0 | 6 | 55.9 | 8 | pass |
| SINGLE LADDER | 46.6 | 47.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.4 | 45.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 51.9 | 52.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.9 | 45.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.3 | 61.3 | 0 | 7 | 55.9 | 8 | pass |
| SEQ+ARP HOME | 62.6 | 65.4 | 0 | 8 | 52.5 | 10 | pass |
| SEQ+ARP AMP | 62.6 | 66.1 | 0 | 8 | 52.5 | 11 | pass |
| SEQ+ARP SEQ | 62.1 | 65.9 | 0 | 8 | 15.2 | 125 | pass |
| SEQ+ARP MOD1 | 62.5 | 67.2 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.6 | 47.4 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 54.1 | 55.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.6 | 56.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.3 | 70.9 | 0 | 8 | 47.9 | 10 | pass |
| SINGLE MULTI H | 53.2 | 54.1 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 63.7 | 64.7 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD MULTI H | 63.6 | 64.7 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 66.9 | 68.1 | 0 | 7 | 50.2 | 9 | pass |
| STACK MULTI H | 66.5 | 67.7 | 0 | 6 | 51.6 | 8 | pass |
| BINAUR MULTI H | 63.7 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 67.7 | 69.3 | 0 | 8 | 45.2 | 1 | pass |
| DUALOSC H CEL+PP | 68.0 | 69.6 | 0 | 6 | 45.2 | 9 | pass |
| SPREAD H TUM+PP | 66.2 | 67.4 | 0 | 8 | 51.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 79.8 | 83.8 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.3 | 55.3 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.8 | 56.8 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.9 | 69.1 | 0 | 8 | 51.2 | 10 | pass |
| STRIKE | 47.2 | 48.1 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 51.9 | 52.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.8 | 75.0 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.2 | 75.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.4 | 59.4 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC SH2 H | 67.9 | 69.1 | 0 | 6 | 43.5 | 9 | pass |
| SPREAD SH2 H | 68.7 | 69.9 | 0 | 8 | 45.2 | 9 | pass |
| DUALFLT SH2 H | 76.9 | 78.1 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.9 | 75.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.2 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 58.9 | 59.8 | 0 | 8 | 57.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 72.9 | 74.4 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 81.2 | 82.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.3 | 78.8 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 76.9 | 78.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.2 | 72.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.1 | 52.4 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 58.9 | 60.0 | 0 | 6 | 57.5 | 8 | pass |
| SPREAD PCM | 64.1 | 65.2 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 64.7 | 66.1 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 68.9 | 70.3 | 0 | 6 | 43.9 | 9 | pass |
| BINAURAL PCM | 64.1 | 65.5 | 0 | 6 | 52.2 | 8 | pass |
| DUALOSC MULTI H PCM | 57.5 | 58.5 | 0 | 6 | 57.6 | 7 | pass |
| DUALFLT MULTI H PCM | 71.0 | 72.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 72.0 | 73.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.7 | 48.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.1 | 56.0 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 59.9 | 61.6 | 0 | 6 | 55.9 | 8 | pass |
| DUALOSC RS PCM | 62.2 | 63.7 | 0 | 6 | 54.2 | 8 | pass |
| SEQ+ARP HOME PCM | 74.2 | 77.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.8 | 51.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.0 | 51.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 50.8 | 51.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 51.7 | 52.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 51.4 | 52.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 51.6 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 69.3 | 70.8 | 0 | 6 | 43.5 | 9 | pass |
| BINAUR HEAVY OD | 68.0 | 69.1 | 0 | 6 | 43.6 | 9 | pass |
| STRIKE HEAVY TUBE | 56.4 | 57.5 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 67.9 | 71.5 | 0 | 8 | 47.2 | 10 | pass |
| FULL | 53.3 | 54.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 54.3 | 55.4 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 56.3 | 57.4 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 H | 62.5 | 63.5 | 0 | 4 | 52.5 | 8 | pass |
| FULL H CEL+PP | 61.1 | 62.3 | 0 | 4 | 55.6 | 8 | pass |
| FULL MULTI H PCM | 52.5 | 53.6 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 58.7 | 59.9 | 0 | 4 | 57.2 | 8 | pass |
| FULL SH2 CEL+PP | 66.8 | 68.0 | 0 | 4 | 49.9 | 8 | pass |
| DSPRD | 46.6 | 47.6 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.3 | 50.3 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD MULTI H | 50.2 | 51.4 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 H | 53.0 | 54.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 54.6 | 55.7 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 57.3 | 58.5 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 57.4 | 58.6 | 0 | 4 | 57.2 | 7 | pass |
| SEQ+ARP DSPRD CEL | 66.5 | 69.8 | 0 | 4 | 50.2 | 10 | pass |

### 2026-10-29 10:07, CARNAC OS 0.9.6 (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.7 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.2 | 27.0 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 46.6 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.2 | 52.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.1 | 61.0 | 0 | 6 | 55.9 | 8 | pass |
| SPREAD | 56.9 | 57.8 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 59.7 | 60.7 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 62.4 | 63.5 | 0 | 6 | 52.9 | 8 | pass |
| BINAURAL | 55.5 | 56.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 62.9 | 63.9 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD HEAVY | 61.6 | 62.6 | 0 | 8 | 53.9 | 8 | pass |
| DUAL FLT HEAVY | 62.5 | 63.5 | 0 | 7 | 53.2 | 8 | pass |
| STACK HEAVY | 63.4 | 64.5 | 0 | 6 | 52.6 | 8 | pass |
| BINAURAL HEAVY | 60.7 | 61.7 | 0 | 6 | 54.9 | 1 | pass |
| SINGLE LADDER | 46.6 | 47.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.5 | 45.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 51.7 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.8 | 45.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.7 | 60.8 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 61.9 | 64.7 | 0 | 8 | 53.2 | 9 | pass |
| SEQ+ARP AMP | 62.0 | 64.8 | 0 | 8 | 52.5 | 11 | pass |
| SEQ+ARP SEQ | 61.5 | 64.7 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 62.0 | 65.9 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.4 | 47.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.1 | 55.1 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.9 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.1 | 70.2 | 0 | 8 | 47.6 | 10 | pass |
| SINGLE MULTI H | 53.2 | 54.1 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 64.0 | 65.2 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD MULTI H | 63.6 | 64.6 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 66.2 | 67.3 | 0 | 7 | 50.9 | 8 | pass |
| STACK MULTI H | 66.7 | 67.7 | 0 | 6 | 50.9 | 9 | pass |
| BINAUR MULTI H | 63.6 | 64.7 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 68.0 | 69.4 | 0 | 8 | 46.2 | 1 | pass |
| DUALOSC H CEL+PP | 68.2 | 70.0 | 0 | 6 | 45.9 | 9 | pass |
| SPREAD H TUM+PP | 66.5 | 67.7 | 0 | 8 | 51.6 | 8 | pass |
| SEQ+ARP SPRD CEL | 79.8 | 84.3 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.5 | 55.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 55.6 | 56.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.7 | 67.5 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 47.3 | 48.3 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 51.9 | 52.9 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP STRIKE | 70.8 | 74.5 | 0 | 8 | 43.5 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.3 | 75.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.6 | 59.7 | 0 | 8 | 57.3 | 8 | pass |
| DUALOSC SH2 H | 68.1 | 69.2 | 0 | 6 | 44.6 | 1 | pass |
| SPREAD SH2 H | 69.0 | 70.3 | 0 | 8 | 44.2 | 9 | pass |
| DUALFLT SH2 H | 76.0 | 77.3 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.8 | 75.1 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.0 | 72.2 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.4 | 60.3 | 0 | 8 | 56.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 73.7 | 75.3 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.4 | 81.7 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.5 | 78.2 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 76.0 | 77.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.0 | 72.3 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 50.8 | 51.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 59.0 | 60.2 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD PCM | 63.9 | 65.2 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 64.2 | 65.3 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 68.5 | 69.7 | 0 | 6 | 47.6 | 9 | pass |
| BINAURAL PCM | 61.6 | 63.0 | 0 | 6 | 52.6 | 8 | pass |
| DUALOSC MULTI H PCM | 57.7 | 58.8 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 70.7 | 72.0 | 0 | 7 | 43.2 | 9 | FAIL |
| BINAUR MULTI H PCM | 69.6 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| SINGLE RS | 46.6 | 48.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 53.9 | 55.9 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.2 | 61.9 | 0 | 6 | 56.2 | 8 | pass |
| DUALOSC RS PCM | 62.1 | 63.9 | 0 | 6 | 53.2 | 8 | pass |
| SEQ+ARP HOME PCM | 71.0 | 74.4 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE DRV SOFT | 50.5 | 51.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 50.7 | 51.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 50.6 | 51.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.5 | 52.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 51.2 | 52.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.3 | 52.3 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 69.6 | 70.8 | 0 | 6 | 43.2 | 9 | pass |
| BINAUR HEAVY OD | 67.2 | 68.3 | 0 | 6 | 47.6 | 9 | pass |
| STRIKE HEAVY TUBE | 56.5 | 57.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 67.1 | 69.9 | 0 | 8 | 47.2 | 10 | pass |
| FULL | 53.4 | 54.3 | 0 | 4 | 57.5 | 1 | pass |
| FULL HEAVY | 54.8 | 55.8 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 56.4 | 57.6 | 0 | 4 | 57.5 | 7 | pass |
| FULL SH2 H | 61.8 | 63.0 | 0 | 4 | 53.2 | 8 | pass |
| FULL H CEL+PP | 60.8 | 62.2 | 0 | 4 | 56.6 | 8 | pass |
| FULL MULTI H PCM | 52.6 | 53.8 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 58.0 | 59.3 | 0 | 4 | 57.2 | 8 | pass |
| FULL SH2 CEL+PP | 66.0 | 67.2 | 0 | 4 | 51.9 | 8 | pass |
| DSPRD | 46.4 | 47.3 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.4 | 50.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H | 50.2 | 51.1 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 H | 53.4 | 54.3 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 54.6 | 55.7 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 57.2 | 58.3 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 57.8 | 59.0 | 0 | 4 | 57.2 | 7 | pass |
| SEQ+ARP DSPRD CEL | 66.2 | 69.5 | 0 | 4 | 51.2 | 10 | pass |

### 2026-10-27 12:19, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.7 | 19.1 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.5 | 27.3 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 46.9 | 47.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE HEAVY | 51.4 | 52.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 61.2 | 62.4 | 0 | 6 | 54.9 | 8 | pass |
| SPREAD | 57.2 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FILTER | 59.9 | 60.8 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 62.4 | 63.4 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 55.5 | 56.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 64.1 | 65.0 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD HEAVY | 61.5 | 62.4 | 0 | 8 | 54.6 | 8 | pass |
| DUAL FLT HEAVY | 62.5 | 63.4 | 0 | 7 | 52.6 | 8 | pass |
| STACK HEAVY | 64.6 | 65.7 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.9 | 61.9 | 0 | 6 | 55.2 | 8 | pass |
| SINGLE LADDER | 46.9 | 47.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.6 | 45.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 51.9 | 52.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.9 | 45.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.9 | 60.7 | 0 | 7 | 56.5 | 8 | pass |
| SEQ+ARP HOME | 62.2 | 65.0 | 0 | 8 | 52.9 | 9 | pass |
| SEQ+ARP AMP | 62.4 | 65.4 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 61.7 | 64.8 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 62.2 | 66.3 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.9 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.4 | 55.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.8 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.2 | 70.2 | 0 | 8 | 46.5 | 10 | pass |
| SINGLE MULTI H | 53.5 | 54.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 65.2 | 66.7 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD MULTI H | 63.8 | 64.8 | 0 | 8 | 51.9 | 8 | pass |
| DUALFLT MULTI H | 66.4 | 67.4 | 0 | 7 | 50.5 | 8 | pass |
| STACK MULTI H | 67.4 | 68.5 | 0 | 6 | 44.2 | 8 | pass |
| BINAUR MULTI H | 63.6 | 64.7 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 67.9 | 69.2 | 0 | 8 | 44.6 | 9 | pass |
| DUALOSC H CEL+PP | 69.1 | 70.9 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD H TUM+PP | 66.6 | 67.6 | 0 | 8 | 50.6 | 8 | pass |
| SEQ+ARP SPRD CEL | 79.8 | 83.2 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.6 | 55.4 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 55.7 | 56.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.2 | 68.0 | 0 | 8 | 51.5 | 10 | pass |
| STRIKE | 47.3 | 48.2 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.1 | 52.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.0 | 74.6 | 0 | 8 | 43.5 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.4 | 74.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 59.0 | 59.9 | 0 | 8 | 57.3 | 8 | pass |
| DUALOSC SH2 H | 69.1 | 70.6 | 0 | 6 | 43.9 | 9 | pass |
| SPREAD SH2 H | 69.2 | 70.3 | 0 | 8 | 43.9 | 9 | pass |
| DUALFLT SH2 H | 76.1 | 77.1 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.9 | 76.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.5 | 72.6 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.6 | 60.4 | 0 | 8 | 54.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 73.5 | 74.7 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.5 | 81.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.5 | 77.8 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 76.1 | 77.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.5 | 72.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.1 | 52.3 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 58.9 | 59.9 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD PCM | 64.0 | 65.0 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 64.1 | 65.2 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 68.0 | 69.3 | 0 | 6 | 45.2 | 9 | pass |
| BINAURAL PCM | 61.5 | 62.7 | 0 | 6 | 54.6 | 8 | pass |
| DUALOSC MULTI H PCM | 57.7 | 58.8 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 70.7 | 72.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 69.6 | 70.9 | 0 | 6 | 42.9 | 1 | pass |
| SINGLE RS | 46.9 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.0 | 56.0 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 61.3 | 62.8 | 0 | 6 | 55.2 | 8 | pass |
| DUALOSC RS PCM | 62.5 | 64.2 | 0 | 6 | 53.2 | 8 | pass |
| SEQ+ARP HOME PCM | 73.1 | 76.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.8 | 51.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 51.0 | 52.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 50.9 | 51.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TAPE | 51.8 | 52.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 51.6 | 52.5 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 51.7 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 70.8 | 72.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.3 | 68.3 | 0 | 6 | 46.6 | 8 | pass |
| STRIKE HEAVY TUBE | 56.5 | 57.4 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 67.8 | 70.9 | 0 | 8 | 45.2 | 10 | pass |
| FULL | 54.4 | 55.5 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 55.8 | 56.7 | 0 | 4 | 57.2 | 7 | pass |
| FULL MULTI H | 57.4 | 58.6 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 62.8 | 63.8 | 0 | 4 | 51.9 | 8 | pass |
| FULL H CEL+PP | 61.3 | 62.7 | 0 | 4 | 55.6 | 8 | pass |
| FULL MULTI H PCM | 52.2 | 53.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 57.7 | 58.7 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 CEL+PP | 66.6 | 68.1 | 0 | 4 | 50.5 | 9 | pass |
| DSPRD | 46.7 | 47.7 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.4 | 50.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 50.5 | 51.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 53.5 | 54.6 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 54.8 | 55.9 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 57.5 | 58.5 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 57.8 | 59.0 | 0 | 4 | 57.2 | 1 | pass |
| SEQ+ARP DSPRD CEL | 66.1 | 68.9 | 0 | 4 | 51.9 | 10 | pass |

### 2026-10-27 11:29, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.7 | 19.1 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 26.5 | 27.4 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 46.9 | 47.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.4 | 52.3 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 61.1 | 62.2 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD | 57.1 | 58.0 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FILTER | 59.9 | 60.8 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 62.3 | 63.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 55.5 | 56.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 64.0 | 65.0 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD HEAVY | 61.5 | 62.4 | 0 | 8 | 55.5 | 8 | pass |
| DUAL FLT HEAVY | 62.4 | 63.4 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 64.5 | 65.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 60.9 | 61.9 | 0 | 6 | 54.9 | 8 | pass |
| SINGLE LADDER | 46.9 | 47.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.6 | 45.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 51.9 | 52.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.9 | 45.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.9 | 60.8 | 0 | 7 | 56.2 | 8 | pass |
| SEQ+ARP HOME | 62.1 | 65.0 | 0 | 8 | 51.9 | 9 | pass |
| SEQ+ARP AMP | 62.3 | 65.3 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.5 | 64.8 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 62.2 | 66.3 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.9 | 47.7 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.4 | 55.4 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.9 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.2 | 70.3 | 0 | 8 | 46.9 | 10 | pass |
| SINGLE MULTI H | 53.5 | 54.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 65.2 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD MULTI H | 63.8 | 64.8 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 66.4 | 67.4 | 0 | 7 | 50.5 | 8 | pass |
| STACK MULTI H | 67.2 | 68.5 | 0 | 6 | 45.2 | 9 | pass |
| BINAUR MULTI H | 63.6 | 64.6 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 68.0 | 69.2 | 0 | 8 | 43.9 | 9 | pass |
| DUALOSC H CEL+PP | 69.2 | 70.6 | 0 | 6 | 43.2 | 9 | pass |
| SPREAD H TUM+PP | 66.6 | 67.8 | 0 | 8 | 50.2 | 8 | pass |
| SEQ+ARP SPRD CEL | 79.8 | 83.1 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.6 | 55.4 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 55.8 | 56.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.1 | 67.9 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 47.3 | 48.2 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.1 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.0 | 74.2 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.3 | 74.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 59.0 | 59.9 | 0 | 8 | 57.5 | 8 | pass |
| DUALOSC SH2 H | 69.1 | 70.3 | 0 | 6 | 44.2 | 9 | pass |
| SPREAD SH2 H | 69.3 | 70.3 | 0 | 8 | 44.6 | 9 | pass |
| DUALFLT SH2 H | 76.1 | 77.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.7 | 75.8 | 0 | 6 | 42.8 | 1 | FAIL |
| BINAUR SH2 H | 71.5 | 72.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.6 | 60.5 | 0 | 8 | 55.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 73.4 | 74.7 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 80.6 | 82.0 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.5 | 77.6 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 76.1 | 77.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.5 | 72.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 50.9 | 51.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 58.9 | 60.0 | 0 | 6 | 57.2 | 8 | pass |
| SPREAD PCM | 64.1 | 65.2 | 0 | 8 | 51.9 | 8 | pass |
| DUAL FILTER PCM | 64.2 | 65.2 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 68.0 | 69.4 | 0 | 6 | 44.9 | 9 | pass |
| BINAURAL PCM | 61.6 | 62.9 | 0 | 6 | 52.9 | 8 | pass |
| DUALOSC MULTI H PCM | 57.7 | 58.7 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 70.7 | 72.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 69.8 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| SINGLE RS | 46.9 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.0 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 61.0 | 62.5 | 0 | 6 | 54.5 | 8 | pass |
| DUALOSC RS PCM | 62.7 | 64.3 | 0 | 6 | 52.5 | 8 | pass |
| SEQ+ARP HOME PCM | 72.6 | 75.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.8 | 51.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 51.0 | 51.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 50.9 | 51.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TAPE | 51.8 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 51.5 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 51.6 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 70.6 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.3 | 68.3 | 0 | 6 | 45.9 | 8 | pass |
| STRIKE HEAVY TUBE | 56.5 | 57.3 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 67.7 | 71.2 | 0 | 8 | 46.6 | 10 | pass |
| FULL | 54.3 | 55.5 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 55.8 | 56.7 | 0 | 4 | 57.5 | 1 | pass |
| FULL MULTI H | 57.4 | 58.7 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 62.7 | 63.9 | 0 | 4 | 52.2 | 8 | pass |
| FULL H CEL+PP | 61.3 | 62.5 | 0 | 4 | 54.9 | 8 | pass |
| FULL MULTI H PCM | 52.3 | 53.4 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 57.9 | 58.9 | 0 | 4 | 57.5 | 1 | pass |
| FULL SH2 CEL+PP | 66.6 | 68.1 | 0 | 4 | 50.5 | 9 | pass |
| DSPRD | 46.7 | 47.5 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD HEAVY | 49.5 | 50.4 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H | 50.5 | 51.5 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H | 53.7 | 54.7 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD H CEL+PP | 54.8 | 56.1 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD MULTI H PCM | 57.5 | 58.5 | 0 | 4 | 57.2 | 7 | pass |
| DSPRD SH2 CEL+PP | 57.8 | 59.0 | 0 | 4 | 57.2 | 1 | pass |
| SEQ+ARP DSPRD CEL | 66.0 | 69.2 | 0 | 4 | 51.5 | 10 | pass |

### 2026-10-26 21:38, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.3 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.5 | 27.4 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 46.9 | 47.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE HEAVY | 51.4 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.5 | 61.6 | 0 | 6 | 55.2 | 8 | pass |
| SPREAD | 57.4 | 58.4 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 60.3 | 61.3 | 0 | 7 | 55.2 | 8 | pass |
| STACK | 62.6 | 63.6 | 0 | 6 | 52.5 | 8 | pass |
| BINAURAL | 55.7 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.0 | 64.1 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD HEAVY | 61.7 | 62.7 | 0 | 8 | 53.6 | 8 | pass |
| DUAL FLT HEAVY | 62.7 | 63.7 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 64.0 | 65.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 62.0 | 0 | 6 | 55.9 | 8 | pass |
| SINGLE LADDER | 46.9 | 47.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.7 | 45.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 52.0 | 52.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 45.0 | 45.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.3 | 61.3 | 0 | 7 | 56.2 | 8 | pass |
| SEQ+ARP HOME | 62.1 | 65.2 | 0 | 8 | 52.9 | 9 | pass |
| SEQ+ARP AMP | 62.3 | 64.8 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.6 | 64.9 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 62.1 | 66.5 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.9 | 47.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 54.4 | 55.5 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.9 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 66.9 | 69.9 | 0 | 8 | 48.6 | 10 | pass |
| SINGLE MULTI H | 53.4 | 54.4 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.4 | 65.6 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD MULTI H | 64.0 | 65.0 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 66.8 | 68.0 | 0 | 7 | 50.2 | 8 | pass |
| STACK MULTI H | 66.8 | 67.9 | 0 | 6 | 50.9 | 9 | pass |
| BINAUR MULTI H | 63.7 | 64.9 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 68.7 | 70.2 | 0 | 8 | 44.9 | 9 | pass |
| DUALOSC H CEL+PP | 68.5 | 70.0 | 0 | 6 | 45.9 | 9 | pass |
| SPREAD H TUM+PP | 67.2 | 68.7 | 0 | 8 | 45.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 80.6 | 84.5 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.6 | 55.5 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.8 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.9 | 68.1 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 47.5 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 52.1 | 53.0 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 70.4 | 74.1 | 0 | 8 | 42.9 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.2 | 74.2 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE SH2 H | 58.7 | 59.6 | 0 | 8 | 57.3 | 7 | pass |
| DUALOSC SH2 H | 68.3 | 69.4 | 0 | 6 | 43.9 | 9 | pass |
| SPREAD SH2 H | 69.5 | 70.7 | 0 | 8 | 42.9 | 1 | pass |
| DUALFLT SH2 H | 76.9 | 78.1 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.9 | 75.1 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.4 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.3 | 60.3 | 0 | 8 | 56.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 74.5 | 75.9 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 81.9 | 83.2 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.8 | 78.2 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 76.8 | 78.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.4 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.8 | 53.2 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.5 | 60.6 | 0 | 6 | 56.5 | 8 | pass |
| SPREAD PCM | 65.0 | 66.2 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 65.7 | 67.2 | 0 | 7 | 51.2 | 8 | pass |
| STACK PCM | 69.5 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| BINAURAL PCM | 62.0 | 63.2 | 0 | 6 | 53.9 | 8 | pass |
| DUALOSC MULTI H PCM | 58.3 | 59.2 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 72.1 | 73.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.3 | 71.7 | 0 | 6 | 42.9 | 9 | FAIL |
| SINGLE RS | 46.9 | 48.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.2 | 57.2 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 60.6 | 62.0 | 0 | 6 | 53.9 | 8 | pass |
| DUALOSC RS PCM | 62.9 | 64.7 | 0 | 6 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 74.5 | 77.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.0 | 52.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.2 | 52.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 51.1 | 52.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.0 | 53.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 51.8 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.8 | 52.8 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 71.5 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.6 | 68.7 | 0 | 6 | 44.6 | 9 | pass |
| STRIKE HEAVY TUBE | 56.7 | 57.6 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 68.6 | 72.0 | 0 | 8 | 44.5 | 10 | pass |
| FULL | 54.1 | 55.0 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 55.3 | 56.3 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 57.1 | 58.1 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 62.3 | 63.3 | 0 | 4 | 51.9 | 8 | pass |
| FULL H CEL+PP | 61.3 | 62.7 | 0 | 4 | 54.9 | 8 | pass |
| FULL MULTI H PCM | 54.0 | 55.0 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 59.4 | 60.5 | 0 | 4 | 56.5 | 8 | pass |
| FULL SH2 CEL+PP | 66.9 | 68.3 | 0 | 4 | 47.9 | 9 | pass |
| DSPRD | 47.1 | 48.1 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD HEAVY | 49.7 | 50.7 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD MULTI H | 50.8 | 51.9 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 H | 53.9 | 55.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD H CEL+PP | 55.5 | 56.8 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD MULTI H PCM | 58.4 | 59.4 | 0 | 4 | 57.5 | 8 | pass |
| DSPRD SH2 CEL+PP | 58.7 | 59.7 | 0 | 4 | 57.6 | 8 | pass |
| SEQ+ARP DSPRD CEL | 66.8 | 70.1 | 0 | 4 | 48.6 | 10 | pass |

### 2026-10-26 20:51, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.3 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.5 | 27.4 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 46.9 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.3 | 52.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.6 | 61.7 | 0 | 6 | 54.2 | 8 | pass |
| SPREAD | 57.4 | 58.4 | 0 | 8 | 57.6 | 7 | pass |
| DUAL FILTER | 60.3 | 61.4 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 62.6 | 63.7 | 0 | 6 | 52.9 | 8 | pass |
| BINAURAL | 55.6 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.0 | 64.1 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD HEAVY | 61.7 | 62.7 | 0 | 8 | 53.2 | 8 | pass |
| DUAL FLT HEAVY | 62.7 | 63.6 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 64.0 | 65.0 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 62.0 | 0 | 6 | 54.5 | 8 | pass |
| SINGLE LADDER | 46.9 | 47.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.6 | 45.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 52.1 | 52.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.0 | 45.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.3 | 61.4 | 0 | 7 | 55.2 | 8 | pass |
| SEQ+ARP HOME | 62.1 | 65.4 | 0 | 8 | 53.5 | 10 | pass |
| SEQ+ARP AMP | 62.2 | 65.3 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.6 | 64.9 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 62.1 | 66.6 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.9 | 47.9 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.4 | 55.5 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.9 | 57.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 66.9 | 70.2 | 0 | 8 | 48.2 | 10 | pass |
| SINGLE MULTI H | 53.4 | 54.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 64.4 | 65.6 | 0 | 6 | 52.2 | 1 | pass |
| SPREAD MULTI H | 64.0 | 65.1 | 0 | 8 | 52.2 | 8 | pass |
| DUALFLT MULTI H | 66.8 | 68.0 | 0 | 7 | 50.9 | 8 | pass |
| STACK MULTI H | 66.8 | 67.9 | 0 | 6 | 51.6 | 8 | pass |
| BINAUR MULTI H | 63.7 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 68.7 | 70.2 | 0 | 8 | 44.9 | 9 | pass |
| DUALOSC H CEL+PP | 68.5 | 70.0 | 0 | 6 | 44.9 | 9 | pass |
| SPREAD H TUM+PP | 67.2 | 68.6 | 0 | 8 | 47.2 | 9 | pass |
| SEQ+ARP SPRD CEL | 80.6 | 83.8 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.6 | 55.4 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.7 | 56.8 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.9 | 68.0 | 0 | 8 | 51.5 | 10 | pass |
| STRIKE | 47.5 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.1 | 53.1 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP STRIKE | 70.4 | 73.9 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.2 | 75.2 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.8 | 59.7 | 0 | 8 | 57.5 | 8 | pass |
| DUALOSC SH2 H | 68.3 | 69.4 | 0 | 6 | 45.6 | 9 | pass |
| SPREAD SH2 H | 69.5 | 70.7 | 0 | 8 | 42.9 | 1 | pass |
| DUALFLT SH2 H | 76.8 | 78.0 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.9 | 75.1 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.4 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.4 | 60.3 | 0 | 8 | 56.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 74.5 | 75.9 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 81.9 | 83.3 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.8 | 78.5 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 76.8 | 78.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.4 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.8 | 53.0 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 59.6 | 60.7 | 0 | 6 | 56.5 | 8 | pass |
| SPREAD PCM | 65.0 | 66.2 | 0 | 8 | 52.2 | 8 | pass |
| DUAL FILTER PCM | 65.7 | 67.1 | 0 | 7 | 50.9 | 8 | pass |
| STACK PCM | 69.5 | 70.9 | 0 | 6 | 42.9 | 1 | pass |
| BINAURAL PCM | 61.9 | 63.1 | 0 | 6 | 52.6 | 8 | pass |
| DUALOSC MULTI H PCM | 58.2 | 59.3 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 72.2 | 73.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.3 | 71.7 | 0 | 6 | 43.2 | 9 | FAIL |
| SINGLE RS | 47.0 | 48.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.2 | 57.5 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.6 | 62.0 | 0 | 6 | 54.5 | 8 | pass |
| DUALOSC RS PCM | 63.0 | 64.8 | 0 | 6 | 52.2 | 8 | pass |
| SEQ+ARP HOME PCM | 74.6 | 77.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.0 | 52.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.2 | 52.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 51.1 | 52.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.0 | 53.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 51.7 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.8 | 52.8 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 71.5 | 72.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.6 | 68.7 | 0 | 6 | 44.6 | 9 | pass |
| STRIKE HEAVY TUBE | 56.7 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 68.6 | 71.8 | 0 | 8 | 45.2 | 10 | pass |
| FULL | 54.1 | 55.1 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 55.3 | 56.3 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 57.2 | 58.2 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 62.3 | 63.4 | 0 | 4 | 52.2 | 8 | pass |
| FULL H CEL+PP | 61.3 | 62.5 | 0 | 4 | 53.6 | 8 | pass |
| FULL MULTI H PCM | 54.0 | 55.2 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 59.3 | 60.5 | 0 | 4 | 56.5 | 8 | pass |
| FULL SH2 CEL+PP | 66.9 | 68.2 | 0 | 4 | 47.9 | 9 | pass |
| DSPRD | 60.5 | 61.5 | 0 | 6 | 54.9 | 8 | pass |
| DSPRD HEAVY | 63.8 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| DSPRD MULTI H | 65.4 | 66.4 | 0 | 6 | 51.6 | 8 | pass |
| DSPRD SH2 H | 69.6 | 70.8 | 0 | 6 | 42.9 | 1 | pass |
| DSPRD H CEL+PP | 69.8 | 71.5 | 0 | 6 | 43.2 | 1 | pass |
| DSPRD MULTI H PCM | 76.1 | 77.3 | 0 | 6 | 42.9 | 1 | FAIL |
| DSPRD SH2 CEL+PP | 74.3 | 75.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SEQ+ARP DSPRD CEL | 95.6 | 99.5 | 0 | 6 | 26.5 | 47 | FAIL |

### 2026-10-26 20:26, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.2 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 26.7 | 27.5 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 47.0 | 47.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.4 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.8 | 61.9 | 0 | 5 | 55.5 | 1 | pass |
| SPREAD | 57.5 | 58.5 | 0 | 8 | 57.2 | 7 | pass |
| DUAL FILTER | 60.3 | 61.3 | 0 | 7 | 55.5 | 8 | pass |
| STACK | 62.8 | 63.8 | 0 | 6 | 52.9 | 8 | pass |
| BINAURAL | 55.7 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.1 | 64.2 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD HEAVY | 61.7 | 62.7 | 0 | 8 | 52.6 | 8 | pass |
| DUAL FLT HEAVY | 62.7 | 63.6 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 64.2 | 65.3 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 62.1 | 0 | 6 | 55.6 | 8 | pass |
| SINGLE LADDER | 47.0 | 47.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.7 | 45.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 52.1 | 52.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.0 | 45.9 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.3 | 61.4 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 62.1 | 65.1 | 0 | 8 | 52.9 | 10 | pass |
| SEQ+ARP AMP | 62.3 | 65.0 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.6 | 64.9 | 0 | 8 | 15.4 | 119 | pass |
| SEQ+ARP MOD1 | 62.1 | 66.4 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.9 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.5 | 55.6 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.8 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 66.9 | 70.0 | 0 | 8 | 47.9 | 10 | pass |
| SINGLE MULTI H | 53.6 | 54.6 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC MULTI H | 64.8 | 65.9 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 64.1 | 65.2 | 0 | 8 | 52.2 | 1 | pass |
| DUALFLT MULTI H | 66.8 | 68.0 | 0 | 7 | 50.5 | 8 | pass |
| STACK MULTI H | 67.2 | 68.3 | 0 | 6 | 46.6 | 8 | pass |
| BINAUR MULTI H | 63.8 | 64.9 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 68.6 | 70.0 | 0 | 8 | 46.2 | 9 | pass |
| DUALOSC H CEL+PP | 68.6 | 70.1 | 0 | 5 | 45.9 | 9 | pass |
| SPREAD H TUM+PP | 67.1 | 68.4 | 0 | 8 | 47.9 | 9 | pass |
| SEQ+ARP SPRD CEL | 80.4 | 84.1 | 0 | 8 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.5 | 55.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 55.8 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.1 | 68.1 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 47.7 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.3 | 53.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.5 | 74.3 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 71.3 | 74.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.9 | 59.8 | 0 | 8 | 57.5 | 8 | pass |
| DUALOSC SH2 H | 68.4 | 69.5 | 0 | 5 | 44.9 | 9 | pass |
| SPREAD SH2 H | 69.6 | 70.8 | 0 | 8 | 42.9 | 1 | pass |
| DUALFLT SH2 H | 76.7 | 77.8 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.1 | 75.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.4 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.6 | 60.7 | 0 | 8 | 56.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 74.3 | 75.6 | 0 | 8 | 42.9 | 1 | FAIL |
| DUALFLT SH2 CEL | 81.7 | 82.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.9 | 78.1 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 76.8 | 78.0 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.5 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.4 | 52.6 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 59.2 | 60.3 | 0 | 5 | 56.5 | 8 | pass |
| SPREAD PCM | 64.4 | 65.5 | 0 | 8 | 51.9 | 1 | pass |
| DUAL FILTER PCM | 65.6 | 67.1 | 0 | 7 | 51.6 | 8 | pass |
| STACK PCM | 70.4 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAURAL PCM | 62.1 | 63.2 | 0 | 6 | 52.9 | 8 | pass |
| DUALOSC MULTI H PCM | 57.8 | 58.9 | 0 | 5 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 72.0 | 73.6 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.4 | 71.7 | 0 | 6 | 43.2 | 9 | FAIL |
| SINGLE RS | 47.1 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.8 | 56.8 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 60.8 | 62.3 | 0 | 5 | 54.9 | 8 | pass |
| DUALOSC RS PCM | 62.5 | 64.4 | 0 | 5 | 52.2 | 8 | pass |
| SEQ+ARP HOME PCM | 74.6 | 77.9 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.9 | 51.8 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.1 | 52.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 51.0 | 52.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.9 | 52.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 51.7 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.8 | 52.9 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 72.4 | 74.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.5 | 68.7 | 0 | 6 | 43.9 | 9 | pass |
| STRIKE HEAVY TUBE | 56.7 | 57.6 | 0 | 8 | 57.6 | 7 | pass |
| SEQ+ARP HOME TUBE | 68.2 | 71.8 | 0 | 8 | 45.2 | 10 | pass |
| FULL | 55.4 | 56.4 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 56.2 | 57.3 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 58.3 | 59.4 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 63.2 | 64.2 | 0 | 4 | 51.9 | 8 | pass |
| FULL H CEL+PP | 62.5 | 63.6 | 0 | 4 | 52.9 | 8 | pass |
| FULL MULTI H PCM | 54.0 | 55.3 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 59.7 | 60.8 | 0 | 4 | 56.5 | 8 | pass |
| FULL SH2 CEL+PP | 67.9 | 69.2 | 0 | 4 | 45.2 | 9 | pass |
| DSPRD | 60.5 | 61.6 | 0 | 6 | 55.6 | 8 | pass |
| DSPRD HEAVY | 63.8 | 64.9 | 0 | 6 | 51.9 | 8 | pass |
| DSPRD MULTI H | 65.5 | 66.5 | 0 | 6 | 51.6 | 8 | pass |
| DSPRD SH2 H | 69.7 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| DSPRD H CEL+PP | 69.8 | 71.6 | 0 | 6 | 43.2 | 9 | pass |
| DSPRD MULTI H PCM | 75.7 | 77.0 | 0 | 6 | 42.9 | 1 | FAIL |
| DSPRD SH2 CEL+PP | 74.2 | 75.8 | 0 | 6 | 42.9 | 1 | FAIL |
| DSPRD 4V | 47.2 | 48.2 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD MULTI H 4V | 51.0 | 52.0 | 0 | 4 | 57.6 | 1 | pass |
| DSPRD SH2 H 4V | 53.9 | 55.0 | 0 | 4 | 57.5 | 1 | pass |
| DSPRD SH2 CEL 4V | 58.6 | 59.9 | 0 | 4 | 57.5 | 8 | pass |

### 2026-10-26 20:10, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.1 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 26.7 | 27.5 | 0 | 1 | 60.3 | 1 | pass |
| SINGLE | 47.0 | 48.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.4 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.8 | 61.9 | 0 | 5 | 55.5 | 1 | pass |
| SPREAD | 48.1 | 49.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 60.3 | 61.4 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 62.8 | 63.8 | 0 | 6 | 53.2 | 8 | pass |
| BINAURAL | 55.7 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.1 | 64.1 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 51.6 | 52.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.7 | 63.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 64.2 | 65.2 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.2 | 62.1 | 0 | 6 | 55.6 | 8 | pass |
| SINGLE LADDER | 47.0 | 47.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 44.7 | 45.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 52.1 | 52.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 45.0 | 45.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.3 | 61.3 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 62.1 | 65.0 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 62.3 | 65.2 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.5 | 64.9 | 0 | 8 | 15.5 | 118 | pass |
| SEQ+ARP MOD1 | 62.1 | 66.3 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.9 | 47.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 54.6 | 55.6 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.8 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 66.9 | 69.8 | 0 | 8 | 48.2 | 10 | pass |
| SINGLE MULTI H | 53.5 | 54.5 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.7 | 65.8 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 53.1 | 54.2 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 66.8 | 67.9 | 0 | 7 | 49.9 | 8 | pass |
| STACK MULTI H | 67.2 | 68.2 | 0 | 6 | 46.9 | 8 | pass |
| BINAUR MULTI H | 63.8 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 58.1 | 59.5 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.6 | 70.1 | 0 | 5 | 45.6 | 9 | pass |
| SPREAD H TUM+PP | 56.6 | 57.8 | 0 | 6 | 57.6 | 7 | pass |
| SEQ+ARP SPRD CEL | 80.2 | 83.3 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.6 | 55.5 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.8 | 56.9 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.1 | 68.1 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 47.7 | 48.6 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 52.3 | 53.2 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 70.5 | 74.3 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.3 | 75.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.9 | 59.9 | 0 | 8 | 57.3 | 8 | pass |
| DUALOSC SH2 H | 68.4 | 69.5 | 0 | 5 | 45.5 | 9 | pass |
| SPREAD SH2 H | 57.5 | 58.4 | 0 | 6 | 57.6 | 7 | pass |
| DUALFLT SH2 H | 76.7 | 77.9 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.1 | 75.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.4 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.6 | 60.7 | 0 | 8 | 55.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.2 | 63.4 | 0 | 6 | 52.5 | 8 | pass |
| DUALFLT SH2 CEL | 81.7 | 83.2 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.9 | 78.6 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 76.7 | 77.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.5 | 72.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.4 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 59.2 | 60.3 | 0 | 5 | 56.9 | 8 | pass |
| SPREAD PCM | 53.4 | 54.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 65.7 | 67.1 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 70.4 | 71.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAURAL PCM | 62.1 | 63.2 | 0 | 6 | 52.6 | 8 | pass |
| DUALOSC MULTI H PCM | 57.8 | 58.9 | 0 | 5 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 72.0 | 73.5 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.3 | 71.6 | 0 | 6 | 42.9 | 9 | FAIL |
| SINGLE RS | 47.0 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.7 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 60.8 | 62.3 | 0 | 5 | 55.2 | 8 | pass |
| DUALOSC RS PCM | 62.5 | 64.4 | 0 | 5 | 52.9 | 8 | pass |
| SEQ+ARP HOME PCM | 74.7 | 77.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.9 | 52.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.2 | 52.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 51.0 | 52.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.0 | 53.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 51.7 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.8 | 52.8 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 72.4 | 74.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.6 | 68.7 | 0 | 6 | 43.9 | 9 | pass |
| STRIKE HEAVY TUBE | 56.7 | 57.7 | 0 | 8 | 57.6 | 7 | pass |
| SEQ+ARP HOME TUBE | 68.3 | 71.5 | 0 | 8 | 45.6 | 10 | pass |
| FULL | 55.4 | 56.3 | 0 | 4 | 57.3 | 1 | pass |
| FULL HEAVY | 56.2 | 57.3 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H | 58.3 | 59.3 | 0 | 4 | 57.2 | 7 | pass |
| FULL SH2 H | 63.1 | 64.3 | 0 | 4 | 51.9 | 8 | pass |
| FULL H CEL+PP | 62.4 | 63.8 | 0 | 4 | 52.2 | 8 | pass |
| FULL MULTI H PCM | 54.0 | 55.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 59.7 | 60.9 | 0 | 4 | 56.5 | 8 | pass |
| FULL SH2 CEL+PP | 67.8 | 69.1 | 0 | 4 | 45.2 | 9 | pass |
| DSPRD | 60.6 | 61.7 | 0 | 6 | 55.2 | 8 | pass |
| DSPRD HEAVY | 63.8 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| DSPRD MULTI H | 65.6 | 66.6 | 0 | 6 | 51.9 | 8 | pass |
| DSPRD SH2 H | 69.6 | 70.9 | 0 | 6 | 42.9 | 1 | pass |
| DSPRD H CEL+PP | 69.8 | 71.6 | 0 | 6 | 42.9 | 1 | pass |
| DSPRD MULTI H PCM | 75.7 | 76.9 | 0 | 6 | 42.9 | 1 | FAIL |
| DSPRD SH2 CEL+PP | 74.3 | 75.7 | 0 | 6 | 42.9 | 1 | FAIL |
| DSPRD 4V | 47.2 | 48.1 | 0 | 4 | 57.3 | 1 | pass |
| DSPRD MULTI H 4V | 51.0 | 52.0 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 H 4V | 53.9 | 55.0 | 0 | 4 | 57.2 | 1 | pass |
| DSPRD SH2 CEL 4V | 58.6 | 59.8 | 0 | 4 | 57.2 | 8 | pass |

### 2026-10-26 19:35, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.2 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 26.7 | 27.5 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 46.8 | 47.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.3 | 52.3 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.8 | 61.7 | 0 | 5 | 55.9 | 1 | pass |
| SPREAD | 48.6 | 49.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 60.5 | 61.5 | 0 | 7 | 55.5 | 8 | pass |
| STACK | 62.7 | 63.7 | 0 | 6 | 52.9 | 8 | pass |
| BINAURAL | 55.7 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 62.9 | 63.8 | 0 | 5 | 52.5 | 8 | pass |
| SPREAD HEAVY | 52.1 | 53.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 62.6 | 63.4 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 63.8 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 62.0 | 0 | 6 | 55.2 | 8 | pass |
| SINGLE LADDER | 46.8 | 47.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.4 | 45.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 51.9 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 44.9 | 45.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.5 | 61.5 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 62.4 | 65.3 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 62.6 | 65.8 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 61.9 | 65.1 | 0 | 8 | 15.5 | 118 | pass |
| SEQ+ARP MOD1 | 62.5 | 66.5 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.8 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.3 | 55.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.8 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.3 | 70.3 | 0 | 8 | 48.5 | 10 | pass |
| SINGLE MULTI H | 53.4 | 54.4 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.7 | 65.7 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 53.5 | 54.6 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 66.9 | 68.0 | 0 | 7 | 49.2 | 9 | pass |
| STACK MULTI H | 66.9 | 68.3 | 0 | 6 | 47.6 | 9 | pass |
| BINAUR MULTI H | 63.8 | 64.8 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 58.8 | 60.1 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.4 | 69.8 | 0 | 5 | 46.2 | 9 | pass |
| SPREAD H TUM+PP | 57.4 | 58.6 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.5 | 84.5 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.5 | 55.3 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.6 | 56.7 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.3 | 68.3 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 47.6 | 48.6 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.2 | 53.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 70.5 | 74.1 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.2 | 75.0 | 0 | 8 | 42.9 | 10 | FAIL |
| SINGLE SH2 H | 58.8 | 59.8 | 0 | 8 | 57.3 | 8 | pass |
| DUALOSC SH2 H | 68.1 | 69.2 | 0 | 5 | 45.2 | 9 | pass |
| SPREAD SH2 H | 58.0 | 58.9 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT SH2 H | 77.1 | 78.3 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.8 | 74.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.4 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.5 | 60.6 | 0 | 8 | 56.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.8 | 63.9 | 0 | 6 | 52.2 | 8 | pass |
| DUALFLT SH2 CEL | 81.9 | 83.3 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.3 | 78.6 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 77.1 | 78.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.4 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 52.4 | 53.6 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 60.4 | 61.7 | 0 | 5 | 56.2 | 8 | pass |
| SPREAD PCM | 54.9 | 56.0 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 65.3 | 66.3 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 70.0 | 71.2 | 0 | 6 | 42.9 | 1 | pass |
| BINAURAL PCM | 62.1 | 63.3 | 0 | 6 | 52.9 | 8 | pass |
| DUALOSC MULTI H PCM | 59.0 | 60.3 | 0 | 5 | 56.9 | 8 | pass |
| DUALFLT MULTI H PCM | 72.0 | 73.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.5 | 71.8 | 0 | 6 | 42.9 | 9 | FAIL |
| SINGLE RS | 46.9 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 56.0 | 57.9 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC RS | 60.8 | 62.2 | 0 | 5 | 54.5 | 8 | pass |
| DUALOSC RS PCM | 63.9 | 65.5 | 0 | 5 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 75.5 | 78.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.3 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.5 | 52.6 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 51.4 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.3 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.1 | 53.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.3 | 53.3 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 72.3 | 73.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.8 | 68.8 | 0 | 6 | 44.9 | 9 | pass |
| STRIKE HEAVY TUBE | 57.1 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 69.8 | 73.0 | 0 | 8 | 43.9 | 10 | pass |
| FULL | 54.3 | 55.2 | 0 | 4 | 57.6 | 1 | pass |
| FULL HEAVY | 55.2 | 56.1 | 0 | 4 | 57.6 | 1 | pass |
| FULL MULTI H | 57.3 | 58.2 | 0 | 4 | 57.6 | 7 | pass |
| FULL SH2 H | 62.1 | 63.0 | 0 | 4 | 52.5 | 8 | pass |
| FULL H CEL+PP | 61.3 | 62.4 | 0 | 4 | 55.6 | 8 | pass |
| FULL MULTI H PCM | 53.9 | 54.9 | 0 | 4 | 57.2 | 1 | pass |
| FULL SH2 H PCM | 59.7 | 60.9 | 0 | 4 | 55.9 | 8 | pass |
| FULL SH2 CEL+PP | 66.7 | 68.1 | 0 | 4 | 50.9 | 8 | pass |

### 2026-10-26 19:24, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.2 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 26.8 | 27.6 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 46.9 | 47.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.3 | 52.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.8 | 61.8 | 0 | 5 | 56.2 | 8 | pass |
| SPREAD | 48.7 | 49.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 60.5 | 61.5 | 0 | 7 | 55.5 | 8 | pass |
| STACK | 62.8 | 63.8 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 55.6 | 56.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 63.0 | 64.1 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 52.1 | 53.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 62.6 | 63.5 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 64.1 | 65.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 61.9 | 0 | 6 | 55.2 | 8 | pass |
| SINGLE LADDER | 47.0 | 48.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.5 | 45.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 51.9 | 52.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 45.0 | 45.8 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.5 | 61.5 | 0 | 7 | 55.2 | 8 | pass |
| SEQ+ARP HOME | 62.5 | 65.7 | 0 | 8 | 52.5 | 10 | pass |
| SEQ+ARP AMP | 62.6 | 66.0 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 62.0 | 65.2 | 0 | 8 | 15.5 | 119 | pass |
| SEQ+ARP MOD1 | 62.5 | 66.3 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.8 | 47.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.4 | 55.4 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.8 | 57.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 67.2 | 70.4 | 0 | 8 | 47.5 | 10 | pass |
| SINGLE MULTI H | 53.4 | 54.4 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.7 | 65.8 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 53.6 | 54.6 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 66.9 | 68.0 | 0 | 7 | 48.2 | 8 | pass |
| STACK MULTI H | 67.1 | 68.3 | 0 | 6 | 46.9 | 9 | pass |
| BINAUR MULTI H | 63.8 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 58.6 | 59.8 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.6 | 69.9 | 0 | 5 | 46.2 | 9 | pass |
| SPREAD H TUM+PP | 57.2 | 58.4 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 82.2 | 85.7 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 54.5 | 55.4 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.6 | 56.8 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.4 | 68.4 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 47.6 | 48.6 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 52.1 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 70.6 | 73.7 | 0 | 8 | 43.5 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.3 | 74.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.6 | 59.7 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC SH2 H | 68.3 | 69.5 | 0 | 5 | 45.9 | 9 | pass |
| SPREAD SH2 H | 57.9 | 59.0 | 0 | 6 | 57.3 | 1 | pass |
| DUALFLT SH2 H | 76.9 | 78.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.2 | 75.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.3 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.4 | 60.4 | 0 | 8 | 56.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.6 | 63.8 | 0 | 6 | 52.2 | 8 | pass |
| DUALFLT SH2 CEL | 81.5 | 82.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.3 | 78.7 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 76.9 | 78.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.4 | 72.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 53.5 | 54.8 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 61.3 | 62.6 | 0 | 5 | 54.5 | 8 | pass |
| SPREAD PCM | 55.9 | 57.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 65.4 | 66.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 70.7 | 72.2 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAURAL PCM | 61.9 | 63.0 | 0 | 6 | 53.2 | 8 | pass |
| DUALOSC MULTI H PCM | 59.9 | 61.1 | 0 | 5 | 55.9 | 8 | pass |
| DUALFLT MULTI H PCM | 72.0 | 73.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.3 | 71.5 | 0 | 6 | 43.2 | 9 | FAIL |
| SINGLE RS | 47.0 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 57.1 | 58.9 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC RS | 60.9 | 62.4 | 0 | 5 | 55.9 | 8 | pass |
| DUALOSC RS PCM | 64.7 | 66.6 | 0 | 5 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 76.6 | 80.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.4 | 52.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV HARD | 51.6 | 52.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 51.5 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 52.4 | 53.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 52.2 | 53.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 52.2 | 53.3 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 72.4 | 74.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.6 | 68.8 | 0 | 6 | 44.3 | 9 | pass |
| STRIKE HEAVY TUBE | 57.0 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 69.7 | 72.9 | 0 | 8 | 43.6 | 10 | pass |
| FULL | 70.7 | 72.1 | 0 | 6 | 43.2 | 9 | FAIL |
| FULL HEAVY | 71.6 | 72.9 | 0 | 6 | 42.9 | 1 | FAIL |
| FULL MULTI H | 74.6 | 76.1 | 0 | 6 | 42.9 | 1 | FAIL |
| FULL SH2 H | 82.2 | 83.3 | 0 | 6 | 42.9 | 1 | FAIL |
| FULL H CEL+PP | 78.6 | 80.1 | 0 | 6 | 42.9 | 1 | FAIL |
| FULL MULTI H PCM | 69.2 | 70.6 | 0 | 6 | 43.5 | 9 | pass |
| FULL 4V | 54.2 | 55.1 | 0 | 4 | 57.2 | 1 | pass |
| FULL MULTI H 4V | 57.2 | 58.1 | 0 | 4 | 57.5 | 7 | pass |
| FULL SH2 H 4V | 62.3 | 63.3 | 0 | 4 | 52.9 | 8 | pass |
| FULL H CEL+PP 4V | 61.2 | 62.3 | 0 | 4 | 55.6 | 8 | pass |
| FULL SH2 H PCM 4V | 59.2 | 60.3 | 0 | 4 | 55.9 | 8 | pass |

### 2026-10-26 18:55, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 19.6 | 20.0 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 27.5 | 28.3 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 47.9 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 52.0 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.6 | 61.5 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD | 49.0 | 49.9 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 61.6 | 62.6 | 0 | 7 | 54.2 | 8 | pass |
| STACK | 63.5 | 64.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 56.2 | 57.2 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.1 | 64.0 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 52.1 | 53.0 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.1 | 63.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 64.3 | 65.3 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 61.6 | 62.7 | 0 | 6 | 54.2 | 8 | pass |
| SINGLE LADDER | 48.0 | 48.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 45.4 | 46.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 53.0 | 54.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.8 | 46.6 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 61.6 | 62.6 | 0 | 7 | 55.2 | 8 | pass |
| SEQ+ARP HOME | 62.5 | 65.2 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 62.5 | 65.2 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 62.1 | 65.1 | 0 | 8 | 15.5 | 119 | pass |
| SEQ+ARP MOD1 | 62.5 | 66.6 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 47.9 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.5 | 56.3 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.8 | 57.7 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 66.9 | 70.0 | 0 | 8 | 47.9 | 10 | pass |
| SINGLE MULTI H | 54.9 | 55.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.4 | 65.3 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 54.4 | 55.2 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 68.1 | 69.3 | 0 | 7 | 44.5 | 9 | pass |
| STACK MULTI H | 67.6 | 68.9 | 0 | 6 | 45.2 | 9 | pass |
| BINAUR MULTI H | 64.3 | 65.4 | 0 | 6 | 51.9 | 1 | pass |
| SPREAD H CEL+PP | 59.6 | 60.8 | 0 | 6 | 56.6 | 8 | pass |
| DUALOSC H CEL+PP | 68.9 | 70.2 | 0 | 5 | 43.9 | 9 | pass |
| SPREAD H TUM+PP | 58.0 | 59.1 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 83.5 | 87.4 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 55.5 | 56.3 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.5 | 57.4 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 65.5 | 68.2 | 0 | 8 | 51.5 | 10 | pass |
| STRIKE | 48.3 | 49.2 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 52.7 | 53.5 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 73.1 | 76.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.8 | 76.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 59.5 | 60.5 | 0 | 8 | 56.2 | 8 | pass |
| DUALOSC SH2 H | 68.2 | 69.4 | 0 | 5 | 44.6 | 9 | pass |
| SPREAD SH2 H | 57.6 | 58.6 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT SH2 H | 77.9 | 79.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 74.8 | 76.1 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.6 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 60.0 | 61.0 | 0 | 8 | 56.3 | 8 | pass |
| SPREAD SH2 CEL+PP | 62.5 | 63.6 | 0 | 6 | 52.9 | 8 | pass |
| DUALFLT SH2 CEL | 82.2 | 83.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.4 | 77.9 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 77.9 | 79.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.7 | 72.9 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 52.4 | 53.6 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 60.1 | 61.1 | 0 | 5 | 55.5 | 8 | pass |
| SPREAD PCM | 56.1 | 57.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 66.5 | 67.8 | 0 | 7 | 50.9 | 9 | pass |
| STACK PCM | 70.0 | 71.5 | 0 | 6 | 42.9 | 1 | pass |
| BINAURAL PCM | 65.5 | 67.2 | 0 | 6 | 51.6 | 8 | pass |
| DUALOSC MULTI H PCM | 59.1 | 60.3 | 0 | 5 | 56.2 | 8 | pass |
| DUALFLT MULTI H PCM | 73.0 | 74.5 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 73.4 | 75.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 48.0 | 49.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 56.0 | 57.8 | 0 | 8 | 57.5 | 7 | pass |
| DUALOSC RS | 60.7 | 62.1 | 0 | 5 | 54.5 | 8 | pass |
| DUALOSC RS PCM | 63.5 | 65.3 | 0 | 5 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 71.0 | 74.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 52.0 | 52.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 52.2 | 53.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 52.1 | 52.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 53.0 | 54.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.8 | 53.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 52.9 | 53.8 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 70.7 | 71.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 68.0 | 69.1 | 0 | 6 | 44.9 | 1 | pass |
| STRIKE HEAVY TUBE | 57.6 | 58.4 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 67.6 | 70.8 | 0 | 8 | 46.5 | 10 | pass |

### 2026-10-26 17:33, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.3 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 26.8 | 27.6 | 0 | 1 | 59.9 | 4 | pass |
| SINGLE | 47.1 | 48.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 51.7 | 52.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.8 | 61.8 | 0 | 5 | 56.5 | 8 | pass |
| SPREAD | 48.6 | 49.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 60.4 | 61.4 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 62.9 | 63.8 | 0 | 6 | 52.5 | 8 | pass |
| BINAURAL | 55.7 | 56.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.0 | 63.8 | 0 | 5 | 52.5 | 8 | pass |
| SPREAD HEAVY | 53.7 | 54.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 62.8 | 63.7 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 63.6 | 64.6 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.1 | 62.0 | 0 | 6 | 55.2 | 8 | pass |
| SINGLE LADDER | 47.1 | 48.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE MS20 | 44.8 | 45.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 52.2 | 53.1 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 45.2 | 45.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 60.4 | 61.3 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 65.8 | 69.0 | 0 | 8 | 50.9 | 10 | pass |
| SEQ+ARP AMP | 65.9 | 69.3 | 0 | 8 | 50.2 | 12 | pass |
| SEQ+ARP SEQ | 65.1 | 68.8 | 0 | 8 | 12.1 | 127 | pass |
| SEQ+ARP MOD1 | 65.4 | 69.7 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 47.1 | 47.9 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 54.7 | 55.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.0 | 57.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 70.5 | 73.8 | 0 | 8 | 43.5 | 10 | FAIL |
| SINGLE MULTI H | 53.9 | 54.8 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 64.9 | 65.9 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 55.8 | 56.9 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 66.9 | 67.9 | 0 | 7 | 49.5 | 9 | pass |
| STACK MULTI H | 67.0 | 68.2 | 0 | 6 | 49.9 | 9 | pass |
| BINAUR MULTI H | 63.7 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 60.4 | 61.7 | 0 | 6 | 55.6 | 8 | pass |
| DUALOSC H CEL+PP | 68.8 | 70.1 | 0 | 5 | 44.6 | 9 | pass |
| SPREAD H TUM+PP | 59.0 | 60.2 | 0 | 6 | 57.2 | 8 | pass |
| SEQ+ARP SPRD CEL | 85.2 | 88.9 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 54.8 | 55.7 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 56.1 | 57.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 68.5 | 72.2 | 0 | 8 | 44.2 | 10 | pass |
| STRIKE | 47.7 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.3 | 53.3 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.9 | 75.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 73.4 | 76.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 59.1 | 60.1 | 0 | 8 | 57.5 | 8 | pass |
| DUALOSC SH2 H | 68.1 | 69.3 | 0 | 5 | 44.2 | 9 | pass |
| SPREAD SH2 H | 59.8 | 61.0 | 0 | 6 | 55.9 | 8 | pass |
| DUALFLT SH2 H | 76.6 | 77.8 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.7 | 75.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.4 | 72.6 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 59.5 | 60.5 | 0 | 8 | 55.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 64.2 | 65.6 | 0 | 6 | 51.9 | 1 | pass |
| DUALFLT SH2 CEL | 80.9 | 82.4 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 77.7 | 80.7 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 76.6 | 77.8 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 71.3 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 52.2 | 53.4 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.9 | 61.0 | 0 | 5 | 56.5 | 8 | pass |
| SPREAD PCM | 54.2 | 55.3 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 65.0 | 66.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 69.2 | 70.4 | 0 | 6 | 43.2 | 9 | pass |
| BINAURAL PCM | 62.4 | 63.5 | 0 | 6 | 53.2 | 8 | pass |
| DUALOSC MULTI H PCM | 61.8 | 63.3 | 0 | 5 | 53.2 | 8 | pass |
| DUALFLT MULTI H PCM | 72.2 | 73.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.4 | 71.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.2 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.6 | 57.7 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.8 | 62.2 | 0 | 5 | 55.2 | 8 | pass |
| DUALOSC RS PCM | 63.3 | 65.0 | 0 | 5 | 52.2 | 8 | pass |
| SEQ+ARP HOME PCM | 78.0 | 81.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.5 | 52.5 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.7 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 51.6 | 52.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.5 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 52.3 | 53.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.4 | 53.3 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 72.6 | 73.9 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 67.3 | 68.4 | 0 | 6 | 45.9 | 9 | pass |
| STRIKE HEAVY TUBE | 58.5 | 59.6 | 0 | 8 | 57.6 | 7 | pass |
| SEQ+ARP HOME TUBE | 73.9 | 77.2 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-26 17:27, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.9 | 19.4 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 26.8 | 27.7 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 47.2 | 48.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.9 | 53.0 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.6 | 61.7 | 0 | 5 | 54.5 | 8 | pass |
| SPREAD | 48.2 | 49.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 60.2 | 61.1 | 0 | 7 | 55.2 | 8 | pass |
| STACK | 63.2 | 64.1 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 55.5 | 56.4 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.3 | 64.3 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD HEAVY | 52.1 | 53.0 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 62.7 | 63.6 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 64.4 | 65.7 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.3 | 62.3 | 0 | 6 | 54.9 | 8 | pass |
| SINGLE LADDER | 47.2 | 48.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.8 | 45.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 52.6 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.2 | 46.1 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.2 | 61.0 | 0 | 7 | 55.9 | 8 | pass |
| SEQ+ARP HOME | 63.3 | 66.1 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 63.4 | 66.8 | 0 | 8 | 51.9 | 11 | pass |
| SEQ+ARP SEQ | 62.8 | 66.3 | 0 | 8 | 14.8 | 127 | pass |
| SEQ+ARP MOD1 | 63.3 | 67.4 | 0 | 8 | 17.1 | 70 | pass |
| LADDER FILTER PG | 47.1 | 48.1 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY MOD+PP | 54.9 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 56.4 | 57.4 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 67.8 | 71.1 | 0 | 8 | 46.5 | 10 | pass |
| SINGLE MULTI H | 54.0 | 54.9 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC MULTI H | 64.8 | 65.8 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 53.6 | 54.7 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 66.8 | 67.8 | 0 | 7 | 50.2 | 8 | pass |
| STACK MULTI H | 67.9 | 69.0 | 0 | 6 | 43.2 | 9 | pass |
| BINAUR MULTI H | 63.8 | 64.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 58.3 | 59.7 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.7 | 70.3 | 0 | 5 | 45.9 | 9 | pass |
| SPREAD H TUM+PP | 56.9 | 58.1 | 0 | 6 | 57.6 | 7 | pass |
| SEQ+ARP SPRD CEL | 80.5 | 83.9 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.0 | 55.8 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 56.1 | 57.0 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 66.0 | 69.0 | 0 | 8 | 50.5 | 10 | pass |
| STRIKE | 47.8 | 48.7 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 52.6 | 53.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.4 | 75.2 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.9 | 75.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 60.1 | 61.3 | 0 | 8 | 56.3 | 8 | pass |
| DUALOSC SH2 H | 69.1 | 70.4 | 0 | 5 | 44.5 | 9 | pass |
| SPREAD SH2 H | 58.8 | 59.9 | 0 | 6 | 57.2 | 8 | pass |
| DUALFLT SH2 H | 77.2 | 78.3 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 75.2 | 76.4 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.6 | 72.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 60.8 | 61.7 | 0 | 8 | 54.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.1 | 64.4 | 0 | 6 | 52.2 | 8 | pass |
| DUALFLT SH2 CEL | 82.1 | 83.3 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 77.4 | 81.0 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 77.2 | 78.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.6 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.3 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC PCM | 59.1 | 60.1 | 0 | 5 | 56.9 | 8 | pass |
| SPREAD PCM | 53.4 | 54.4 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 64.5 | 65.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 68.7 | 70.0 | 0 | 6 | 44.9 | 9 | pass |
| BINAURAL PCM | 63.2 | 64.6 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 58.3 | 59.4 | 0 | 5 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 71.0 | 72.2 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.4 | 72.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.2 | 48.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.7 | 56.6 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.7 | 62.1 | 0 | 5 | 54.5 | 8 | pass |
| DUALOSC RS PCM | 62.4 | 63.9 | 0 | 5 | 52.2 | 8 | pass |
| SEQ+ARP HOME PCM | 74.2 | 77.2 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.1 | 52.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 51.3 | 52.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 51.2 | 52.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TAPE | 52.1 | 53.1 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 51.9 | 52.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 52.0 | 53.0 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 71.2 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 69.6 | 70.9 | 0 | 6 | 42.9 | 1 | pass |
| STRIKE HEAVY TUBE | 57.0 | 57.8 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 68.7 | 72.0 | 0 | 8 | 45.2 | 10 | pass |

### 2026-10-26 17:14, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.9 | 19.3 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 27.1 | 27.9 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 47.5 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 52.9 | 54.0 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.6 | 61.5 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD | 48.4 | 49.4 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 60.4 | 61.4 | 0 | 7 | 54.5 | 8 | pass |
| STACK | 63.1 | 64.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL | 55.7 | 56.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 63.3 | 64.3 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 53.1 | 54.2 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 63.3 | 64.3 | 0 | 7 | 52.2 | 8 | pass |
| STACK HEAVY | 64.6 | 65.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.3 | 62.2 | 0 | 6 | 56.2 | 8 | pass |
| SINGLE LADDER | 47.5 | 48.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 45.2 | 46.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 53.6 | 54.5 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.6 | 46.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.4 | 61.4 | 0 | 7 | 54.2 | 8 | pass |
| SEQ+ARP HOME | 64.3 | 66.7 | 0 | 8 | 51.9 | 10 | pass |
| SEQ+ARP AMP | 64.5 | 67.2 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 63.9 | 67.0 | 0 | 8 | 12.5 | 127 | pass |
| SEQ+ARP MOD1 | 64.1 | 68.0 | 0 | 8 | 14.4 | 70 | pass |
| LADDER FILTER PG | 47.5 | 48.4 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.8 | 56.9 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CELEST+PP | 57.2 | 58.4 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP CELEST+PP | 68.8 | 71.4 | 0 | 8 | 44.9 | 10 | pass |
| SINGLE MULTI H | 54.9 | 55.9 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC MULTI H | 64.8 | 65.8 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 54.4 | 55.4 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 67.1 | 68.2 | 0 | 7 | 46.5 | 8 | pass |
| STACK MULTI H | 67.9 | 69.1 | 0 | 6 | 43.9 | 9 | pass |
| BINAUR MULTI H | 63.9 | 65.0 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 59.1 | 60.3 | 0 | 6 | 56.9 | 8 | pass |
| DUALOSC H CEL+PP | 68.6 | 70.3 | 0 | 5 | 46.2 | 9 | pass |
| SPREAD H TUM+PP | 57.7 | 58.8 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.7 | 85.1 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 55.9 | 56.8 | 0 | 8 | 57.5 | 7 | pass |
| HEAVY CEL+BBD PP | 57.0 | 58.1 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP SPR+BBD | 66.9 | 69.3 | 0 | 8 | 47.9 | 10 | pass |
| STRIKE | 48.0 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.5 | 54.4 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.2 | 74.7 | 0 | 8 | 43.2 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.6 | 74.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 63.0 | 64.3 | 0 | 8 | 52.2 | 8 | pass |
| DUALOSC SH2 H | 70.2 | 71.4 | 0 | 5 | 43.2 | 9 | FAIL |
| SPREAD SH2 H | 61.4 | 62.5 | 0 | 6 | 54.6 | 8 | pass |
| DUALFLT SH2 H | 78.5 | 79.6 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 75.9 | 77.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.7 | 72.6 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 63.6 | 64.7 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 65.5 | 66.7 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT SH2 CEL | 83.3 | 84.6 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 78.9 | 82.0 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 78.6 | 79.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 71.7 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.5 | 52.7 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.4 | 60.5 | 0 | 5 | 56.9 | 8 | pass |
| SPREAD PCM | 53.8 | 54.8 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 64.9 | 65.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 69.0 | 70.2 | 0 | 6 | 44.2 | 9 | pass |
| BINAURAL PCM | 63.9 | 65.1 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 58.9 | 60.0 | 0 | 5 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 71.5 | 72.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.8 | 73.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.6 | 49.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.0 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 60.6 | 61.9 | 0 | 5 | 55.2 | 8 | pass |
| DUALOSC RS PCM | 62.7 | 64.4 | 0 | 5 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 76.0 | 79.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.5 | 52.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.7 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 51.6 | 52.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 52.5 | 53.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 52.2 | 53.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 52.3 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 70.9 | 72.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 69.7 | 70.8 | 0 | 6 | 42.9 | 1 | pass |
| STRIKE HEAVY TUBE | 58.0 | 59.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 69.9 | 72.8 | 0 | 8 | 43.2 | 10 | pass |

### 2026-10-26 17:07, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.9 | 19.2 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 26.4 | 27.2 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 46.3 | 47.1 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 50.6 | 51.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 59.6 | 60.5 | 0 | 5 | 55.5 | 8 | pass |
| SPREAD | 47.4 | 48.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 59.2 | 60.1 | 0 | 7 | 57.2 | 8 | pass |
| STACK | 62.3 | 63.2 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 54.9 | 55.8 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 61.6 | 62.6 | 0 | 5 | 53.2 | 8 | pass |
| SPREAD HEAVY | 50.8 | 51.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 61.5 | 62.3 | 0 | 7 | 55.5 | 8 | pass |
| STACK HEAVY | 62.8 | 63.8 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.3 | 61.2 | 0 | 6 | 55.6 | 8 | pass |
| SINGLE LADDER | 46.3 | 47.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.1 | 44.9 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE SH-2 | 51.6 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 44.5 | 45.3 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 59.2 | 60.1 | 0 | 7 | 57.2 | 8 | pass |
| SEQ+ARP HOME | 61.2 | 64.0 | 0 | 8 | 54.5 | 9 | pass |
| SEQ+ARP AMP | 61.3 | 64.3 | 0 | 8 | 53.9 | 11 | pass |
| SEQ+ARP SEQ | 60.8 | 63.0 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 61.1 | 65.3 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.3 | 47.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 53.6 | 54.5 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.9 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 65.8 | 68.9 | 0 | 8 | 51.2 | 10 | pass |
| SINGLE MULTI H | 52.7 | 53.5 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 63.3 | 64.3 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 52.1 | 52.9 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 65.5 | 66.5 | 0 | 7 | 51.5 | 8 | pass |
| STACK MULTI H | 65.9 | 66.8 | 0 | 6 | 49.2 | 8 | pass |
| BINAUR MULTI H | 62.8 | 63.8 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 56.6 | 57.7 | 0 | 6 | 57.6 | 7 | pass |
| DUALOSC H CEL+PP | 67.1 | 68.3 | 0 | 5 | 47.2 | 9 | pass |
| SPREAD H TUM+PP | 55.3 | 56.4 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.9 | 81.4 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.0 | 54.8 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 54.7 | 55.6 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.1 | 66.5 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 46.9 | 47.7 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 51.2 | 52.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 69.6 | 72.7 | 0 | 8 | 44.2 | 10 | pass |
| SEQ+ARP STRK FILT | 70.3 | 73.9 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE SH2 H | 58.2 | 59.0 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 67.2 | 68.2 | 0 | 5 | 46.9 | 9 | pass |
| SPREAD SH2 H | 56.5 | 57.4 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT SH2 H | 75.4 | 76.5 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 73.2 | 74.4 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 70.8 | 71.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 58.5 | 59.3 | 0 | 8 | 57.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 60.5 | 61.6 | 0 | 6 | 54.9 | 8 | pass |
| DUALFLT SH2 CEL | 80.3 | 81.7 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 74.9 | 78.9 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 75.4 | 76.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.8 | 71.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 50.6 | 51.7 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.0 | 59.9 | 0 | 5 | 56.9 | 8 | pass |
| SPREAD PCM | 52.7 | 53.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 63.8 | 64.8 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 69.0 | 70.2 | 0 | 6 | 43.9 | 9 | pass |
| BINAURAL PCM | 61.4 | 62.4 | 0 | 6 | 54.9 | 8 | pass |
| DUALOSC MULTI H PCM | 57.8 | 58.8 | 0 | 5 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 70.2 | 71.5 | 0 | 7 | 42.9 | 9 | FAIL |
| BINAUR MULTI H PCM | 69.6 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| SINGLE RS | 46.4 | 47.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.2 | 56.3 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 59.7 | 60.9 | 0 | 5 | 55.9 | 8 | pass |
| DUALOSC RS PCM | 62.0 | 63.8 | 0 | 5 | 53.5 | 8 | pass |
| SEQ+ARP HOME PCM | 73.8 | 76.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.2 | 51.0 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV HARD | 50.5 | 51.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 50.3 | 51.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.2 | 52.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 51.0 | 51.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.1 | 51.9 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 69.0 | 70.3 | 0 | 6 | 44.5 | 9 | pass |
| BINAUR HEAVY OD | 67.0 | 68.2 | 0 | 6 | 47.3 | 9 | pass |
| STRIKE HEAVY TUBE | 56.0 | 56.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP HOME TUBE | 66.4 | 69.5 | 0 | 8 | 49.2 | 10 | pass |

### 2026-10-26 17:00, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.9 | 19.3 | 0 | 0 | 61.3 | 3 | pass |
| SINGLE 1V | 27.1 | 27.9 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 47.5 | 48.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE HEAVY | 52.9 | 54.0 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.6 | 61.5 | 0 | 5 | 55.2 | 8 | pass |
| SPREAD | 48.4 | 49.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 60.5 | 61.5 | 0 | 7 | 54.9 | 8 | pass |
| STACK | 63.1 | 64.1 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 55.7 | 56.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.3 | 64.3 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 53.1 | 54.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 63.3 | 64.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK HEAVY | 64.5 | 65.5 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 61.3 | 62.3 | 0 | 6 | 55.9 | 8 | pass |
| SINGLE LADDER | 47.5 | 48.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 45.2 | 46.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 53.6 | 54.5 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 45.6 | 46.4 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 60.4 | 61.4 | 0 | 7 | 53.5 | 8 | pass |
| SEQ+ARP HOME | 64.3 | 66.6 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 64.4 | 67.0 | 0 | 8 | 52.2 | 11 | pass |
| SEQ+ARP SEQ | 63.9 | 67.2 | 0 | 8 | 12.9 | 127 | pass |
| SEQ+ARP MOD1 | 64.0 | 67.6 | 0 | 8 | 14.5 | 70 | pass |
| LADDER FILTER PG | 47.5 | 48.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 55.8 | 57.0 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CELEST+PP | 57.2 | 58.3 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP CELEST+PP | 68.8 | 71.7 | 0 | 8 | 44.2 | 10 | pass |
| SINGLE MULTI H | 54.9 | 55.9 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 64.8 | 65.8 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 54.5 | 55.7 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 67.1 | 68.2 | 0 | 7 | 47.2 | 8 | pass |
| STACK MULTI H | 67.9 | 69.1 | 0 | 6 | 44.6 | 9 | pass |
| BINAUR MULTI H | 63.8 | 64.9 | 0 | 6 | 52.2 | 8 | pass |
| SPREAD H CEL+PP | 59.1 | 60.5 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.6 | 70.1 | 0 | 5 | 45.9 | 9 | pass |
| SPREAD H TUM+PP | 57.6 | 58.8 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 81.7 | 84.6 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 55.8 | 56.7 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 57.0 | 58.1 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP SPR+BBD | 66.8 | 69.3 | 0 | 8 | 46.9 | 10 | pass |
| STRIKE | 48.0 | 48.9 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 53.5 | 54.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 71.2 | 75.0 | 0 | 8 | 43.5 | 10 | FAIL |
| SEQ+ARP STRK FILT | 71.7 | 74.7 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 63.1 | 64.3 | 0 | 8 | 52.2 | 8 | pass |
| DUALOSC SH2 H | 70.1 | 71.4 | 0 | 5 | 42.9 | 1 | FAIL |
| SPREAD SH2 H | 61.4 | 62.5 | 0 | 6 | 55.9 | 8 | pass |
| DUALFLT SH2 H | 78.6 | 79.9 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 75.9 | 77.1 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 71.7 | 72.7 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 63.6 | 64.8 | 0 | 8 | 52.2 | 8 | pass |
| SPREAD SH2 CEL+PP | 65.5 | 66.9 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT SH2 CEL | 83.3 | 84.4 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 78.9 | 82.2 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 78.6 | 79.8 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 71.7 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.5 | 52.8 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.4 | 60.4 | 0 | 5 | 56.2 | 8 | pass |
| SPREAD PCM | 53.8 | 54.9 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 64.9 | 65.9 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 69.0 | 70.2 | 0 | 6 | 44.6 | 9 | pass |
| BINAURAL PCM | 63.9 | 65.0 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 58.9 | 59.9 | 0 | 5 | 57.2 | 8 | pass |
| DUALFLT MULTI H PCM | 71.5 | 72.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.9 | 73.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 47.5 | 49.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.0 | 57.1 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.7 | 61.9 | 0 | 5 | 54.9 | 8 | pass |
| DUALOSC RS PCM | 62.7 | 64.4 | 0 | 5 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 76.0 | 79.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 51.5 | 52.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV HARD | 51.7 | 52.7 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV FOLD | 51.6 | 52.6 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TAPE | 52.4 | 53.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV TUBE | 52.2 | 53.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE DRV OD | 52.4 | 53.3 | 0 | 8 | 57.2 | 1 | pass |
| STACK HEAVY TUBE | 70.9 | 72.2 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR HEAVY OD | 69.7 | 71.0 | 0 | 6 | 42.9 | 9 | pass |
| STRIKE HEAVY TUBE | 58.0 | 59.0 | 0 | 8 | 57.6 | 7 | pass |
| SEQ+ARP HOME TUBE | 69.9 | 73.0 | 0 | 8 | 43.2 | 10 | pass |

### 2026-10-26 15:04, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.9 | 19.3 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 26.9 | 27.7 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 46.8 | 47.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 51.0 | 52.2 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 60.1 | 60.9 | 0 | 5 | 56.2 | 8 | pass |
| SPREAD | 47.9 | 48.8 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 59.7 | 60.8 | 0 | 7 | 56.5 | 8 | pass |
| STACK | 62.7 | 63.6 | 0 | 6 | 52.5 | 8 | pass |
| BINAURAL | 55.4 | 56.3 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 62.1 | 63.1 | 0 | 5 | 52.9 | 8 | pass |
| SPREAD HEAVY | 51.2 | 52.2 | 0 | 6 | 57.3 | 1 | pass |
| DUAL FLT HEAVY | 61.8 | 62.9 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 63.3 | 64.3 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.7 | 61.7 | 0 | 6 | 54.6 | 8 | pass |
| SINGLE LADDER | 46.8 | 47.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 44.4 | 45.3 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 51.9 | 52.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.9 | 45.9 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.7 | 60.6 | 0 | 7 | 56.5 | 8 | pass |
| SEQ+ARP HOME | 61.7 | 64.6 | 0 | 8 | 53.9 | 9 | pass |
| SEQ+ARP AMP | 61.7 | 64.6 | 0 | 8 | 52.9 | 11 | pass |
| SEQ+ARP SEQ | 61.0 | 64.2 | 0 | 8 | 23.6 | 105 | pass |
| SEQ+ARP MOD1 | 61.4 | 65.5 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.8 | 47.7 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 53.9 | 55.0 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.0 | 56.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 66.0 | 68.8 | 0 | 8 | 50.2 | 10 | pass |
| SINGLE MULTI H | 53.0 | 53.8 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC MULTI H | 63.7 | 64.5 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 52.6 | 53.6 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 65.8 | 66.9 | 0 | 7 | 50.5 | 8 | pass |
| STACK MULTI H | 66.3 | 67.3 | 0 | 6 | 51.2 | 8 | pass |
| BINAUR MULTI H | 63.2 | 64.1 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 56.8 | 57.8 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 67.2 | 68.3 | 0 | 5 | 45.2 | 8 | pass |
| SPREAD H TUM+PP | 55.2 | 56.1 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 78.0 | 81.9 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 54.5 | 55.3 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 55.1 | 56.1 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.9 | 67.7 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 47.2 | 48.1 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 51.8 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.3 | 74.4 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.1 | 75.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.2 | 59.0 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC SH2 H | 67.4 | 68.4 | 0 | 5 | 45.2 | 9 | pass |
| SPREAD SH2 H | 56.5 | 57.4 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT SH2 H | 75.6 | 76.7 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 73.7 | 74.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 70.8 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 58.6 | 59.4 | 0 | 8 | 57.3 | 8 | pass |
| SPREAD SH2 CEL+PP | 60.8 | 62.0 | 0 | 6 | 55.5 | 8 | pass |
| DUALFLT SH2 CEL | 80.1 | 81.3 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 73.8 | 77.5 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 75.6 | 76.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.8 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.4 | 52.6 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.5 | 60.5 | 0 | 5 | 55.9 | 8 | pass |
| SPREAD PCM | 53.1 | 54.3 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 64.5 | 65.6 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 69.5 | 70.7 | 0 | 6 | 42.9 | 1 | pass |
| BINAURAL PCM | 63.8 | 65.2 | 0 | 6 | 52.2 | 8 | pass |
| DUALOSC MULTI H PCM | 57.9 | 59.0 | 0 | 5 | 57.2 | 1 | pass |
| DUALFLT MULTI H PCM | 70.6 | 71.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.4 | 73.2 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.8 | 48.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 55.1 | 56.9 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.2 | 61.6 | 0 | 5 | 55.5 | 8 | pass |
| DUALOSC RS PCM | 62.7 | 64.3 | 0 | 5 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 76.0 | 79.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.8 | 51.8 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 51.0 | 52.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 50.8 | 52.0 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.8 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 51.5 | 52.5 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 51.6 | 52.6 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 70.4 | 71.8 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR HEAVY OD | 70.3 | 71.6 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE HEAVY TUBE | 58.4 | 59.4 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP HOME TUBE | 68.5 | 71.7 | 0 | 8 | 44.2 | 10 | pass |

### 2026-10-26 12:52, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.8 | 19.2 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 26.7 | 27.4 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 46.7 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 50.9 | 52.0 | 0 | 8 | 57.5 | 1 | pass |
| DUAL OSC | 60.1 | 61.0 | 0 | 5 | 56.2 | 8 | pass |
| SPREAD | 47.8 | 48.6 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 59.8 | 60.8 | 0 | 7 | 56.2 | 8 | pass |
| STACK | 62.8 | 63.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL | 55.3 | 56.2 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 62.1 | 63.1 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD HEAVY | 51.1 | 52.0 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 61.9 | 63.0 | 0 | 7 | 52.9 | 8 | pass |
| STACK HEAVY | 63.0 | 64.0 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.6 | 61.5 | 0 | 6 | 54.6 | 8 | pass |
| SINGLE LADDER | 46.6 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 44.3 | 45.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 51.8 | 52.8 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 44.7 | 45.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 59.8 | 60.8 | 0 | 7 | 55.5 | 8 | pass |
| SEQ+ARP HOME | 61.6 | 64.8 | 0 | 8 | 53.5 | 9 | pass |
| SEQ+ARP AMP | 61.7 | 64.7 | 0 | 8 | 53.5 | 11 | pass |
| SEQ+ARP SEQ | 60.9 | 64.3 | 0 | 8 | 23.6 | 105 | pass |
| SEQ+ARP MOD1 | 61.4 | 65.8 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.6 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 53.9 | 54.8 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.9 | 55.9 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 65.9 | 69.5 | 0 | 8 | 49.9 | 10 | pass |
| SINGLE MULTI H | 52.9 | 53.7 | 0 | 8 | 57.3 | 1 | pass |
| DUALOSC MULTI H | 63.7 | 64.6 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 52.5 | 53.5 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 66.0 | 67.0 | 0 | 7 | 51.5 | 8 | pass |
| STACK MULTI H | 66.1 | 67.0 | 0 | 6 | 50.6 | 8 | pass |
| BINAUR MULTI H | 63.1 | 64.0 | 0 | 6 | 51.9 | 8 | pass |
| SPREAD H CEL+PP | 56.6 | 57.8 | 0 | 6 | 57.2 | 7 | pass |
| DUALOSC H CEL+PP | 67.1 | 68.3 | 0 | 5 | 45.9 | 9 | pass |
| SPREAD H TUM+PP | 55.1 | 56.1 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 78.0 | 81.1 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 54.3 | 55.3 | 0 | 8 | 57.5 | 1 | pass |
| HEAVY CEL+BBD PP | 54.9 | 56.0 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.8 | 67.2 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 47.2 | 48.1 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 51.7 | 52.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP STRIKE | 71.1 | 74.6 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 72.0 | 75.3 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 58.1 | 58.9 | 0 | 8 | 57.2 | 7 | pass |
| DUALOSC SH2 H | 67.4 | 68.4 | 0 | 5 | 44.9 | 8 | pass |
| SPREAD SH2 H | 56.4 | 57.3 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT SH2 H | 75.6 | 76.7 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 73.4 | 74.5 | 0 | 6 | 43.2 | 1 | FAIL |
| BINAUR SH2 H | 70.8 | 72.0 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 58.6 | 59.4 | 0 | 8 | 57.6 | 7 | pass |
| SPREAD SH2 CEL+PP | 60.7 | 61.8 | 0 | 6 | 55.6 | 8 | pass |
| DUALFLT SH2 CEL | 80.2 | 81.4 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 73.7 | 77.5 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 75.7 | 76.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.7 | 71.9 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 51.1 | 52.3 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 59.4 | 60.5 | 0 | 5 | 55.9 | 8 | pass |
| SPREAD PCM | 53.0 | 54.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 64.4 | 65.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 69.4 | 70.5 | 0 | 6 | 43.2 | 1 | pass |
| BINAURAL PCM | 63.3 | 64.2 | 0 | 6 | 52.2 | 8 | pass |
| DUALOSC MULTI H PCM | 57.8 | 58.8 | 0 | 5 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 70.6 | 71.9 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 71.0 | 72.3 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.7 | 48.3 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.5 | 56.6 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 60.2 | 61.5 | 0 | 5 | 54.9 | 8 | pass |
| DUALOSC RS PCM | 62.6 | 64.2 | 0 | 5 | 51.9 | 8 | pass |
| SEQ+ARP HOME PCM | 75.2 | 79.0 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.7 | 51.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 50.9 | 51.8 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 50.8 | 51.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.7 | 52.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TUBE | 51.5 | 52.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 51.6 | 52.5 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 69.8 | 71.2 | 0 | 6 | 42.9 | 1 | pass |
| BINAUR HEAVY OD | 69.8 | 71.2 | 0 | 6 | 42.9 | 1 | pass |
| STRIKE HEAVY TUBE | 58.2 | 59.2 | 0 | 8 | 56.9 | 7 | pass |
| SEQ+ARP HOME TUBE | 68.3 | 71.8 | 0 | 8 | 45.2 | 10 | pass |

### 2026-10-26 12:35, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.7 | 19.2 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 26.4 | 27.2 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 46.3 | 47.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 50.5 | 51.5 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 59.5 | 60.4 | 0 | 5 | 56.2 | 8 | pass |
| SPREAD | 47.5 | 48.4 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 59.5 | 60.5 | 0 | 7 | 55.9 | 8 | pass |
| STACK | 62.2 | 63.2 | 0 | 6 | 52.9 | 8 | pass |
| BINAURAL | 55.0 | 56.0 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 61.7 | 62.6 | 0 | 5 | 53.2 | 8 | pass |
| SPREAD HEAVY | 50.7 | 51.5 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 61.8 | 62.8 | 0 | 7 | 52.5 | 8 | pass |
| STACK HEAVY | 62.7 | 63.6 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 60.0 | 61.0 | 0 | 6 | 55.9 | 8 | pass |
| SINGLE LADDER | 46.3 | 47.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE MS20 | 44.0 | 44.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 51.4 | 52.3 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 44.5 | 45.3 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 59.5 | 60.5 | 0 | 7 | 56.5 | 8 | pass |
| SEQ+ARP HOME | 60.9 | 63.7 | 0 | 8 | 54.2 | 9 | pass |
| SEQ+ARP AMP | 61.0 | 64.1 | 0 | 8 | 54.5 | 11 | pass |
| SEQ+ARP SEQ | 60.2 | 64.0 | 0 | 8 | 24.1 | 104 | pass |
| SEQ+ARP MOD1 | 60.7 | 65.0 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 46.2 | 47.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 53.6 | 54.7 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 55.1 | 56.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 65.9 | 69.1 | 0 | 8 | 50.5 | 10 | pass |
| SINGLE MULTI H | 52.5 | 53.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 63.2 | 64.2 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 52.2 | 53.1 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 65.7 | 66.8 | 0 | 7 | 51.2 | 8 | pass |
| STACK MULTI H | 65.7 | 66.8 | 0 | 6 | 51.6 | 8 | pass |
| BINAUR MULTI H | 62.7 | 63.7 | 0 | 6 | 52.6 | 8 | pass |
| SPREAD H CEL+PP | 56.9 | 58.1 | 0 | 6 | 57.6 | 7 | pass |
| DUALOSC H CEL+PP | 67.3 | 68.6 | 0 | 5 | 45.2 | 9 | pass |
| SPREAD H TUM+PP | 55.3 | 56.3 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.8 | 81.1 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 53.9 | 54.8 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 55.1 | 56.3 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 64.2 | 67.1 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 46.7 | 47.6 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 51.2 | 52.0 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 69.8 | 73.5 | 0 | 8 | 43.2 | 9 | pass |
| SEQ+ARP STRK FILT | 70.6 | 74.0 | 0 | 8 | 43.2 | 2 | FAIL |
| SINGLE SH2 H | 57.7 | 58.7 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 67.0 | 68.0 | 0 | 5 | 49.2 | 8 | pass |
| SPREAD SH2 H | 56.1 | 57.0 | 0 | 6 | 57.6 | 7 | pass |
| DUALFLT SH2 H | 75.5 | 76.7 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 72.8 | 74.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 70.3 | 71.5 | 0 | 6 | 43.2 | 9 | FAIL |
| STRIKE SH2 H | 58.2 | 59.2 | 0 | 8 | 57.6 | 7 | pass |
| SPREAD SH2 CEL+PP | 60.8 | 61.9 | 0 | 6 | 55.9 | 8 | pass |
| DUALFLT SH2 CEL | 80.1 | 81.4 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 72.8 | 76.8 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 75.5 | 76.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 70.3 | 71.5 | 0 | 6 | 43.2 | 9 | FAIL |
| SINGLE PCM | 50.7 | 51.6 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 58.9 | 59.8 | 0 | 5 | 57.2 | 8 | pass |
| SPREAD PCM | 52.9 | 53.8 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 64.2 | 65.2 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 69.0 | 70.1 | 0 | 6 | 43.6 | 9 | pass |
| BINAURAL PCM | 63.5 | 64.8 | 0 | 6 | 52.2 | 8 | pass |
| DUALOSC MULTI H PCM | 57.3 | 58.3 | 0 | 5 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 70.4 | 71.6 | 0 | 7 | 42.9 | 9 | FAIL |
| BINAUR MULTI H PCM | 71.1 | 72.3 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 46.3 | 47.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 54.2 | 56.2 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 59.6 | 61.1 | 0 | 5 | 56.2 | 8 | pass |
| DUALOSC RS PCM | 61.8 | 63.4 | 0 | 5 | 53.6 | 8 | pass |
| SEQ+ARP HOME PCM | 72.6 | 76.0 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 50.3 | 51.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE DRV HARD | 50.5 | 51.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV FOLD | 50.3 | 51.2 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 51.3 | 52.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 51.1 | 51.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV OD | 51.1 | 52.0 | 0 | 8 | 57.5 | 1 | pass |
| STACK HEAVY TUBE | 70.7 | 72.4 | 0 | 6 | 43.2 | 9 | FAIL |
| BINAUR HEAVY OD | 68.3 | 69.8 | 0 | 6 | 45.3 | 9 | pass |
| STRIKE HEAVY TUBE | 56.0 | 56.9 | 0 | 8 | 57.6 | 7 | pass |
| SEQ+ARP HOME TUBE | 67.9 | 71.2 | 0 | 8 | 45.5 | 10 | pass |

### 2026-10-25 12:30, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.2 | 18.5 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 25.9 | 26.6 | 0 | 1 | 60.0 | 4 | pass |
| SINGLE | 45.8 | 46.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 50.0 | 50.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 58.9 | 59.8 | 0 | 5 | 57.2 | 8 | pass |
| SPREAD | 46.9 | 47.8 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 58.6 | 59.4 | 0 | 7 | 57.2 | 7 | pass |
| STACK | 60.9 | 61.8 | 0 | 6 | 54.9 | 8 | pass |
| BINAURAL | 54.2 | 55.0 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.8 | 61.6 | 0 | 5 | 54.9 | 8 | pass |
| SPREAD HEAVY | 49.9 | 50.7 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 60.6 | 61.4 | 0 | 7 | 53.9 | 8 | pass |
| STACK HEAVY | 61.6 | 62.6 | 0 | 6 | 53.2 | 8 | pass |
| BINAURAL HEAVY | 59.4 | 60.4 | 0 | 6 | 56.2 | 8 | pass |
| SINGLE LADDER | 45.8 | 46.4 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 43.2 | 43.9 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 50.6 | 51.5 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 43.7 | 44.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 58.7 | 59.5 | 0 | 7 | 57.2 | 7 | pass |
| SEQ+ARP HOME | 60.0 | 62.9 | 0 | 8 | 55.9 | 9 | pass |
| SEQ+ARP AMP | 60.2 | 62.4 | 0 | 8 | 55.5 | 11 | pass |
| SEQ+ARP SEQ | 59.6 | 61.7 | 0 | 8 | 15.5 | 116 | pass |
| SEQ+ARP MOD1 | 60.1 | 64.4 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 45.8 | 46.5 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 53.4 | 54.2 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.8 | 55.7 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 65.3 | 68.7 | 0 | 8 | 52.2 | 10 | pass |
| SINGLE MULTI H | 52.0 | 52.7 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 62.4 | 63.4 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 51.6 | 52.4 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 64.9 | 65.7 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 64.6 | 65.5 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 62.3 | 63.4 | 0 | 6 | 52.9 | 8 | pass |
| SPREAD H CEL+PP | 56.7 | 57.8 | 0 | 6 | 57.6 | 7 | pass |
| DUALOSC H CEL+PP | 67.0 | 68.2 | 0 | 5 | 48.2 | 8 | pass |
| SPREAD H TUM+PP | 54.7 | 55.8 | 0 | 6 | 57.6 | 1 | pass |
| SEQ+ARP SPRD CEL | 77.4 | 80.7 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 53.6 | 54.5 | 0 | 8 | 57.6 | 1 | pass |
| HEAVY CEL+BBD PP | 54.8 | 55.7 | 0 | 8 | 57.5 | 1 | pass |
| SEQ+ARP SPR+BBD | 63.7 | 66.7 | 0 | 8 | 51.9 | 10 | pass |
| STRIKE | 46.5 | 47.3 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 50.5 | 51.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 69.9 | 73.3 | 0 | 8 | 43.9 | 10 | pass |
| SEQ+ARP STRK FILT | 71.1 | 74.8 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE SH2 H | 56.9 | 57.7 | 0 | 8 | 57.3 | 7 | pass |
| DUALOSC SH2 H | 65.9 | 66.9 | 0 | 5 | 50.9 | 8 | pass |
| SPREAD SH2 H | 55.2 | 56.0 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT SH2 H | 74.3 | 75.2 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 71.7 | 72.8 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 69.4 | 70.5 | 0 | 6 | 42.9 | 1 | pass |
| STRIKE SH2 H | 57.3 | 58.2 | 0 | 8 | 57.2 | 7 | pass |
| SPREAD SH2 CEL+PP | 59.9 | 61.1 | 0 | 6 | 55.6 | 8 | pass |
| DUALFLT SH2 CEL | 79.3 | 80.5 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 72.1 | 76.0 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 74.3 | 75.3 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 69.4 | 70.6 | 0 | 6 | 43.2 | 9 | pass |
| SINGLE PCM | 50.6 | 52.1 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 58.1 | 59.7 | 0 | 5 | 57.2 | 8 | pass |
| SPREAD PCM | 52.2 | 53.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 65.6 | 66.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 67.8 | 69.4 | 0 | 6 | 44.6 | 9 | pass |
| BINAURAL PCM | 61.5 | 63.0 | 0 | 6 | 53.9 | 8 | pass |
| DUALOSC MULTI H PCM | 56.7 | 58.1 | 0 | 5 | 57.2 | 7 | pass |
| DUALFLT MULTI H PCM | 71.7 | 73.1 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 69.4 | 71.1 | 0 | 6 | 43.2 | 9 | pass |
| SINGLE RS | 45.8 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 53.9 | 56.5 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 58.9 | 60.4 | 0 | 5 | 57.2 | 8 | pass |
| DUALOSC RS PCM | 61.7 | 63.7 | 0 | 5 | 53.2 | 8 | pass |
| SEQ+ARP HOME PCM | 72.7 | 75.8 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE DRV SOFT | 49.8 | 50.5 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV HARD | 50.0 | 50.7 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV FOLD | 50.2 | 50.9 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE DRV TAPE | 50.7 | 51.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV TUBE | 50.5 | 51.2 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE DRV OD | 50.5 | 51.3 | 0 | 8 | 57.6 | 1 | pass |
| STACK HEAVY TUBE | 68.1 | 69.4 | 0 | 6 | 44.9 | 9 | pass |
| BINAUR HEAVY OD | 66.8 | 68.0 | 0 | 6 | 50.2 | 8 | pass |
| STRIKE HEAVY TUBE | 55.6 | 56.4 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP HOME TUBE | 66.1 | 69.2 | 0 | 8 | 50.9 | 10 | pass |

### 2026-10-25 10:14, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.1 | 18.5 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 25.6 | 26.3 | 0 | 1 | 60.3 | 4 | pass |
| SINGLE | 45.5 | 46.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE HEAVY | 49.8 | 50.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC | 58.9 | 59.7 | 0 | 5 | 57.5 | 7 | pass |
| SPREAD | 46.6 | 47.4 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 58.5 | 59.3 | 0 | 7 | 57.2 | 1 | pass |
| STACK | 61.4 | 62.3 | 0 | 6 | 55.6 | 8 | pass |
| BINAURAL | 54.2 | 55.2 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 60.7 | 61.7 | 0 | 5 | 53.9 | 8 | pass |
| SPREAD HEAVY | 49.8 | 50.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 60.7 | 61.7 | 0 | 7 | 56.2 | 8 | pass |
| STACK HEAVY | 61.9 | 62.8 | 0 | 6 | 52.2 | 8 | pass |
| BINAURAL HEAVY | 59.7 | 60.6 | 0 | 6 | 55.6 | 8 | pass |
| SINGLE LADDER | 45.5 | 46.2 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 43.2 | 44.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 50.3 | 51.0 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 43.6 | 44.3 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 58.6 | 59.4 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 59.6 | 62.4 | 0 | 8 | 56.6 | 9 | pass |
| SEQ+ARP AMP | 59.7 | 62.6 | 0 | 8 | 56.2 | 11 | pass |
| SEQ+ARP SEQ | 59.0 | 62.1 | 0 | 8 | 16.3 | 116 | pass |
| SEQ+ARP MOD1 | 59.5 | 63.8 | 0 | 8 | 17.2 | 58 | pass |
| LADDER FILTER PG | 45.5 | 46.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 53.0 | 54.0 | 0 | 8 | 57.2 | 1 | pass |
| HEAVY CELEST+PP | 54.4 | 55.4 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP CELEST+PP | 64.8 | 68.1 | 0 | 8 | 51.9 | 10 | pass |
| SINGLE MULTI H | 51.7 | 52.5 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC MULTI H | 62.4 | 63.3 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD MULTI H | 51.3 | 52.1 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT MULTI H | 64.8 | 65.6 | 0 | 7 | 51.9 | 8 | pass |
| STACK MULTI H | 64.8 | 65.8 | 0 | 6 | 52.2 | 8 | pass |
| BINAUR MULTI H | 62.4 | 63.5 | 0 | 6 | 52.5 | 8 | pass |
| SPREAD H CEL+PP | 56.5 | 57.5 | 0 | 6 | 57.6 | 7 | pass |
| DUALOSC H CEL+PP | 67.0 | 68.3 | 0 | 5 | 49.6 | 8 | pass |
| SPREAD H TUM+PP | 54.4 | 55.4 | 0 | 6 | 57.2 | 1 | pass |
| SEQ+ARP SPRD CEL | 76.9 | 81.0 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 53.4 | 54.3 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY CEL+BBD PP | 54.5 | 55.5 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP SPR+BBD | 63.1 | 66.1 | 0 | 8 | 52.2 | 10 | pass |
| STRIKE | 46.2 | 47.0 | 0 | 8 | 57.6 | 1 | pass |
| STRIKE HEAVY | 50.2 | 51.1 | 0 | 8 | 57.6 | 1 | pass |
| SEQ+ARP STRIKE | 68.7 | 71.7 | 0 | 8 | 45.2 | 10 | pass |
| SEQ+ARP STRK FILT | 69.0 | 72.9 | 0 | 8 | 43.9 | 10 | pass |
| SINGLE SH2 H | 56.6 | 57.4 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC SH2 H | 65.9 | 66.9 | 0 | 5 | 50.9 | 8 | pass |
| SPREAD SH2 H | 55.1 | 55.9 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT SH2 H | 74.4 | 75.4 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 71.9 | 73.0 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 69.6 | 70.6 | 0 | 6 | 43.3 | 9 | pass |
| STRIKE SH2 H | 56.9 | 57.8 | 0 | 8 | 57.2 | 7 | pass |
| SPREAD SH2 CEL+PP | 60.2 | 61.3 | 0 | 6 | 56.2 | 8 | pass |
| DUALFLT SH2 CEL | 79.5 | 80.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 71.7 | 75.5 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 74.4 | 75.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 69.6 | 70.6 | 0 | 6 | 43.2 | 9 | pass |
| SINGLE PCM | 49.8 | 50.9 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 57.1 | 58.1 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD PCM | 52.2 | 53.1 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 64.4 | 65.5 | 0 | 7 | 52.2 | 8 | pass |
| STACK PCM | 67.1 | 68.1 | 0 | 6 | 48.9 | 9 | pass |
| BINAURAL PCM | 62.1 | 63.2 | 0 | 6 | 53.2 | 8 | pass |
| DUALOSC MULTI H PCM | 55.5 | 56.6 | 0 | 5 | 57.6 | 1 | pass |
| DUALFLT MULTI H PCM | 70.5 | 71.8 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 70.0 | 71.3 | 0 | 6 | 42.9 | 1 | pass |
| SINGLE RS | 45.5 | 47.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 53.4 | 55.4 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 59.0 | 60.7 | 0 | 5 | 57.2 | 8 | pass |
| DUALOSC RS PCM | 60.6 | 62.4 | 0 | 5 | 55.2 | 8 | pass |
| SEQ+ARP HOME PCM | 68.8 | 72.5 | 0 | 8 | 45.6 | 10 | pass |

### 2026-10-25 10:08, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.6 | 18.9 | 0 | 0 | 61.6 | 1 | pass |
| SINGLE 1V | 26.1 | 27.0 | 0 | 1 | 60.0 | 1 | pass |
| SINGLE | 45.9 | 46.7 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 57.1 | 58.0 | 0 | 8 | 57.5 | 7 | pass |
| DUAL OSC | 59.6 | 60.4 | 0 | 5 | 55.9 | 8 | pass |
| SPREAD | 47.1 | 47.9 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 58.7 | 59.5 | 0 | 7 | 57.2 | 7 | pass |
| STACK | 61.6 | 62.5 | 0 | 6 | 55.9 | 8 | pass |
| BINAURAL | 54.7 | 55.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 65.1 | 66.1 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 55.2 | 56.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FLT HEAVY | 67.8 | 68.9 | 0 | 7 | 43.9 | 9 | pass |
| STACK HEAVY | 67.2 | 68.2 | 0 | 6 | 46.9 | 9 | pass |
| BINAURAL HEAVY | 71.2 | 72.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE LADDER | 45.9 | 46.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 43.6 | 44.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 50.7 | 51.4 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 43.9 | 44.7 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 58.9 | 59.7 | 0 | 7 | 57.2 | 8 | pass |
| SEQ+ARP HOME | 66.8 | 70.0 | 0 | 8 | 48.9 | 10 | pass |
| SEQ+ARP AMP | 66.8 | 70.1 | 0 | 8 | 49.6 | 12 | pass |
| SEQ+ARP SEQ | 66.2 | 69.7 | 0 | 8 | 12.1 | 127 | pass |
| SEQ+ARP MOD1 | 66.7 | 71.1 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 45.9 | 46.6 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 60.5 | 61.5 | 0 | 8 | 56.2 | 8 | pass |
| HEAVY CELEST+PP | 61.7 | 62.7 | 0 | 8 | 54.6 | 8 | pass |
| SEQ+ARP CELEST+PP | 72.2 | 75.3 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE MULTI H | 58.9 | 59.9 | 0 | 8 | 57.3 | 8 | pass |
| DUALOSC MULTI H | 66.6 | 67.5 | 0 | 5 | 48.9 | 8 | pass |
| SPREAD MULTI H | 56.8 | 57.6 | 0 | 6 | 57.2 | 7 | pass |
| DUALFLT MULTI H | 71.6 | 72.6 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK MULTI H | 69.9 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| BINAUR MULTI H | 72.5 | 73.6 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 61.8 | 63.0 | 0 | 6 | 53.9 | 8 | pass |
| DUALOSC H CEL+PP | 71.3 | 72.5 | 0 | 5 | 42.9 | 1 | FAIL |
| SPREAD H TUM+PP | 59.8 | 60.7 | 0 | 6 | 56.2 | 8 | pass |
| SEQ+ARP SPRD CEL | 84.5 | 87.8 | 0 | 6 | 42.9 | 12 | FAIL |
| HEAVY SPRING+PP | 60.7 | 61.6 | 0 | 8 | 55.5 | 8 | pass |
| HEAVY CEL+BBD PP | 61.8 | 62.8 | 0 | 8 | 53.5 | 8 | pass |
| SEQ+ARP SPR+BBD | 70.3 | 73.2 | 0 | 8 | 43.6 | 10 | FAIL |
| STRIKE | 46.7 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 57.6 | 58.6 | 0 | 8 | 57.2 | 7 | pass |
| SEQ+ARP STRIKE | 76.2 | 79.9 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 76.6 | 80.1 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 63.7 | 64.8 | 0 | 8 | 51.9 | 8 | pass |
| DUALOSC SH2 H | 70.0 | 71.0 | 0 | 5 | 42.9 | 1 | pass |
| SPREAD SH2 H | 60.5 | 61.3 | 0 | 6 | 56.6 | 8 | pass |
| DUALFLT SH2 H | 81.6 | 82.7 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 77.2 | 78.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 79.8 | 80.9 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 64.2 | 65.3 | 0 | 8 | 51.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 65.5 | 66.5 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT SH2 CEL | 86.8 | 87.9 | 0 | 7 | 43.2 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 79.1 | 83.0 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 81.6 | 82.6 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 79.8 | 80.8 | 0 | 6 | 43.2 | 1 | FAIL |
| SINGLE PCM | 50.2 | 51.4 | 0 | 8 | 57.2 | 1 | pass |
| DUAL OSC PCM | 57.5 | 58.5 | 0 | 5 | 57.5 | 7 | pass |
| SPREAD PCM | 52.7 | 53.6 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 63.4 | 64.5 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 67.4 | 68.7 | 0 | 6 | 45.2 | 9 | pass |
| BINAURAL PCM | 62.7 | 63.9 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 61.1 | 62.2 | 0 | 5 | 55.6 | 8 | pass |
| DUALFLT MULTI H PCM | 77.3 | 78.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 82.5 | 84.1 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 45.9 | 47.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE RS PCM | 53.7 | 55.8 | 0 | 8 | 57.5 | 1 | pass |
| DUALOSC RS | 59.7 | 61.1 | 0 | 5 | 55.9 | 8 | pass |
| DUALOSC RS PCM | 61.0 | 62.8 | 0 | 5 | 55.5 | 8 | pass |
| SEQ+ARP HOME PCM | 78.9 | 82.4 | 0 | 8 | 42.9 | 2 | FAIL |

### 2026-10-25 00:16, CARNAC OS 0.9.3 (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 16.6 | 17.0 | 0 | 0 | 61.6 | 3 | pass |
| SINGLE 1V | 23.7 | 24.4 | 0 | 1 | 60.9 | 3 | pass |
| SINGLE | 43.4 | 44.1 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 54.9 | 55.6 | 0 | 8 | 57.6 | 1 | pass |
| DUAL OSC | 56.9 | 57.9 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD | 44.7 | 45.5 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER | 56.7 | 57.7 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 59.2 | 60.2 | 0 | 6 | 57.2 | 8 | pass |
| BINAURAL | 52.1 | 52.9 | 0 | 6 | 57.6 | 1 | pass |
| DUAL OSC HEAVY | 63.0 | 63.9 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 53.1 | 54.0 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 65.9 | 66.9 | 0 | 7 | 49.9 | 8 | pass |
| STACK HEAVY | 65.3 | 66.3 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL HEAVY | 67.2 | 68.2 | 0 | 6 | 46.9 | 9 | pass |
| SINGLE LADDER | 43.3 | 44.0 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 41.4 | 42.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE SH-2 | 48.4 | 49.2 | 0 | 8 | 57.2 | 1 | pass |
| SINGLE FORMANT | 41.7 | 42.6 | 0 | 8 | 57.2 | 1 | pass |
| DUAL FLT LADDER | 56.7 | 57.5 | 0 | 7 | 57.2 | 1 | pass |
| SEQ+ARP HOME | 64.6 | 67.3 | 0 | 8 | 52.2 | 10 | pass |
| SEQ+ARP AMP | 64.7 | 67.5 | 0 | 8 | 51.8 | 11 | pass |
| SEQ+ARP SEQ | 64.0 | 66.9 | 0 | 8 | 12.8 | 127 | pass |
| SEQ+ARP MOD1 | 64.6 | 68.3 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 43.3 | 44.1 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 57.9 | 58.9 | 0 | 8 | 56.9 | 7 | pass |
| HEAVY CELEST+PP | 59.7 | 60.7 | 0 | 8 | 55.9 | 8 | pass |
| SEQ+ARP CELEST+PP | 69.6 | 72.6 | 0 | 8 | 44.2 | 10 | pass |
| SINGLE MULTI H | 56.4 | 57.4 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC MULTI H | 64.2 | 65.3 | 0 | 5 | 52.2 | 8 | pass |
| SPREAD MULTI H | 54.4 | 55.3 | 0 | 6 | 57.6 | 1 | pass |
| DUALFLT MULTI H | 69.7 | 70.7 | 0 | 7 | 43.2 | 1 | pass |
| STACK MULTI H | 67.5 | 68.6 | 0 | 6 | 45.9 | 9 | pass |
| BINAUR MULTI H | 69.8 | 70.8 | 0 | 6 | 42.9 | 1 | pass |
| SPREAD H CEL+PP | 59.1 | 60.1 | 0 | 6 | 57.2 | 8 | pass |
| DUALOSC H CEL+PP | 68.9 | 70.1 | 0 | 5 | 44.5 | 9 | pass |
| SPREAD H TUM+PP | 56.8 | 57.8 | 0 | 6 | 57.2 | 7 | pass |
| SEQ+ARP SPRD CEL | 81.2 | 84.8 | 0 | 6 | 43.2 | 2 | FAIL |
| HEAVY SPRING+PP | 58.7 | 59.6 | 0 | 8 | 57.5 | 8 | pass |
| HEAVY CEL+BBD PP | 59.8 | 60.9 | 0 | 8 | 54.9 | 8 | pass |
| SEQ+ARP SPR+BBD | 68.3 | 70.9 | 0 | 8 | 44.5 | 10 | pass |
| STRIKE | 44.3 | 45.3 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 55.3 | 56.2 | 0 | 8 | 57.2 | 1 | pass |
| SEQ+ARP STRIKE | 73.8 | 77.2 | 0 | 8 | 43.2 | 2 | FAIL |
| SEQ+ARP STRK FILT | 74.7 | 77.7 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 61.6 | 62.7 | 0 | 8 | 55.8 | 8 | pass |
| DUALOSC SH2 H | 67.9 | 69.0 | 0 | 5 | 43.6 | 9 | pass |
| SPREAD SH2 H | 58.3 | 59.2 | 0 | 6 | 57.2 | 1 | pass |
| DUALFLT SH2 H | 79.5 | 80.6 | 0 | 7 | 43.2 | 1 | FAIL |
| STACK SH2 H | 75.0 | 76.1 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 77.3 | 78.4 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 62.0 | 63.3 | 0 | 8 | 53.6 | 8 | pass |
| SPREAD SH2 CEL+PP | 63.1 | 64.1 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT SH2 CEL | 84.3 | 85.6 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 75.9 | 79.4 | 0 | 8 | 42.9 | 2 | FAIL |
| DUALFLT SH2 HP | 79.5 | 80.6 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR SH2 HP | 77.4 | 78.4 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 47.7 | 48.8 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 55.2 | 56.2 | 0 | 5 | 57.5 | 1 | pass |
| SPREAD PCM | 50.3 | 51.1 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER PCM | 61.2 | 62.1 | 0 | 7 | 54.9 | 8 | pass |
| STACK PCM | 65.2 | 66.1 | 0 | 6 | 51.9 | 8 | pass |
| BINAURAL PCM | 59.8 | 60.7 | 0 | 6 | 55.2 | 8 | pass |
| DUALOSC MULTI H PCM | 58.8 | 59.8 | 0 | 5 | 57.6 | 8 | pass |
| DUALFLT MULTI H PCM | 74.3 | 75.4 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 77.5 | 78.7 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 43.4 | 44.9 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE RS PCM | 51.0 | 52.9 | 0 | 8 | 57.2 | 1 | pass |
| DUALOSC RS | 56.9 | 58.5 | 0 | 5 | 57.2 | 7 | pass |
| DUALOSC RS PCM | 58.8 | 60.4 | 0 | 5 | 57.2 | 8 | pass |
| SEQ+ARP HOME PCM | 72.9 | 76.3 | 0 | 8 | 43.2 | 2 | FAIL |

### 2026-10-25 00:10, CARNAC OS DEV (2 cores), done

| scenario | avg % | peak % | late | voices | UI fps | slowest UI frame ms | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| IDLE | 18.6 | 18.9 | 0 | 0 | 61.3 | 1 | pass |
| SINGLE 1V | 26.1 | 26.9 | 0 | 1 | 60.3 | 1 | pass |
| SINGLE | 45.9 | 46.6 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE HEAVY | 57.1 | 58.0 | 0 | 8 | 57.5 | 7 | pass |
| DUAL OSC | 59.6 | 60.4 | 0 | 5 | 56.2 | 8 | pass |
| SPREAD | 47.1 | 48.0 | 0 | 6 | 57.6 | 1 | pass |
| DUAL FILTER | 58.8 | 59.5 | 0 | 7 | 57.5 | 7 | pass |
| STACK | 61.6 | 62.5 | 0 | 6 | 54.2 | 8 | pass |
| BINAURAL | 54.7 | 55.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL OSC HEAVY | 65.1 | 66.1 | 0 | 5 | 51.9 | 8 | pass |
| SPREAD HEAVY | 55.2 | 56.0 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FLT HEAVY | 67.7 | 68.9 | 0 | 7 | 44.9 | 9 | pass |
| STACK HEAVY | 67.2 | 68.3 | 0 | 6 | 49.2 | 9 | pass |
| BINAURAL HEAVY | 71.2 | 72.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE LADDER | 45.8 | 46.6 | 0 | 8 | 57.3 | 1 | pass |
| SINGLE MS20 | 43.6 | 44.4 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE SH-2 | 50.7 | 51.4 | 0 | 8 | 57.5 | 1 | pass |
| SINGLE FORMANT | 43.9 | 44.7 | 0 | 8 | 57.6 | 1 | pass |
| DUAL FLT LADDER | 58.8 | 59.7 | 0 | 7 | 57.2 | 7 | pass |
| SEQ+ARP HOME | 66.8 | 69.7 | 0 | 8 | 50.8 | 10 | pass |
| SEQ+ARP AMP | 66.8 | 70.2 | 0 | 8 | 48.9 | 12 | pass |
| SEQ+ARP SEQ | 66.2 | 69.5 | 0 | 8 | 12.1 | 127 | pass |
| SEQ+ARP MOD1 | 66.7 | 71.1 | 0 | 8 | 14.3 | 70 | pass |
| LADDER FILTER PG | 45.9 | 46.6 | 0 | 8 | 57.3 | 1 | pass |
| HEAVY MOD+PP | 60.5 | 61.7 | 0 | 8 | 54.9 | 8 | pass |
| HEAVY CELEST+PP | 61.8 | 62.9 | 0 | 8 | 53.2 | 8 | pass |
| SEQ+ARP CELEST+PP | 72.1 | 75.9 | 0 | 8 | 43.2 | 10 | FAIL |
| SINGLE MULTI H | 58.9 | 60.0 | 0 | 8 | 57.2 | 8 | pass |
| DUALOSC MULTI H | 66.6 | 67.6 | 0 | 5 | 50.2 | 8 | pass |
| SPREAD MULTI H | 56.7 | 57.7 | 0 | 6 | 57.6 | 7 | pass |
| DUALFLT MULTI H | 71.6 | 72.6 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK MULTI H | 69.8 | 71.0 | 0 | 6 | 42.9 | 1 | pass |
| BINAUR MULTI H | 72.5 | 73.5 | 0 | 6 | 42.9 | 1 | FAIL |
| SPREAD H CEL+PP | 61.8 | 62.9 | 0 | 6 | 54.2 | 8 | pass |
| DUALOSC H CEL+PP | 71.4 | 72.7 | 0 | 5 | 42.9 | 1 | FAIL |
| SPREAD H TUM+PP | 59.8 | 60.9 | 0 | 6 | 54.9 | 8 | pass |
| SEQ+ARP SPRD CEL | 84.5 | 87.9 | 0 | 6 | 42.9 | 2 | FAIL |
| HEAVY SPRING+PP | 60.7 | 61.7 | 0 | 8 | 54.5 | 8 | pass |
| HEAVY CEL+BBD PP | 61.8 | 62.9 | 0 | 8 | 53.2 | 8 | pass |
| SEQ+ARP SPR+BBD | 70.3 | 73.6 | 0 | 8 | 42.9 | 10 | FAIL |
| STRIKE | 46.7 | 47.5 | 0 | 8 | 57.3 | 1 | pass |
| STRIKE HEAVY | 57.6 | 58.5 | 0 | 8 | 57.5 | 7 | pass |
| SEQ+ARP STRIKE | 76.3 | 79.6 | 0 | 8 | 42.9 | 2 | FAIL |
| SEQ+ARP STRK FILT | 76.6 | 80.5 | 0 | 8 | 42.9 | 2 | FAIL |
| SINGLE SH2 H | 63.8 | 64.7 | 0 | 8 | 51.9 | 8 | pass |
| DUALOSC SH2 H | 70.0 | 71.2 | 0 | 5 | 42.9 | 1 | pass |
| SPREAD SH2 H | 60.5 | 61.4 | 0 | 6 | 55.2 | 8 | pass |
| DUALFLT SH2 H | 81.6 | 82.6 | 0 | 7 | 42.9 | 1 | FAIL |
| STACK SH2 H | 77.2 | 78.3 | 0 | 6 | 42.9 | 1 | FAIL |
| BINAUR SH2 H | 79.8 | 80.8 | 0 | 6 | 42.9 | 1 | FAIL |
| STRIKE SH2 H | 64.2 | 65.3 | 0 | 8 | 51.9 | 8 | pass |
| SPREAD SH2 CEL+PP | 65.4 | 66.6 | 0 | 6 | 51.9 | 8 | pass |
| DUALFLT SH2 CEL | 86.8 | 87.9 | 0 | 7 | 42.9 | 1 | FAIL |
| SEQ+ARP SH2 CEL | 79.1 | 82.9 | 0 | 8 | 43.2 | 2 | FAIL |
| DUALFLT SH2 HP | 81.6 | 82.6 | 0 | 7 | 43.2 | 1 | FAIL |
| BINAUR SH2 HP | 79.8 | 80.8 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE PCM | 50.2 | 51.4 | 0 | 8 | 57.3 | 1 | pass |
| DUAL OSC PCM | 57.5 | 58.5 | 0 | 5 | 57.2 | 7 | pass |
| SPREAD PCM | 52.8 | 53.7 | 0 | 6 | 57.2 | 1 | pass |
| DUAL FILTER PCM | 63.4 | 64.4 | 0 | 7 | 51.9 | 8 | pass |
| STACK PCM | 67.4 | 68.8 | 0 | 6 | 45.9 | 9 | pass |
| BINAURAL PCM | 62.8 | 63.9 | 0 | 6 | 51.9 | 8 | pass |
| DUALOSC MULTI H PCM | 61.1 | 62.2 | 0 | 5 | 55.9 | 8 | pass |
| DUALFLT MULTI H PCM | 77.4 | 78.7 | 0 | 7 | 42.9 | 1 | FAIL |
| BINAUR MULTI H PCM | 82.5 | 84.1 | 0 | 6 | 42.9 | 1 | FAIL |
| SINGLE RS | 45.9 | 47.3 | 0 | 8 | 57.6 | 1 | pass |
| SINGLE RS PCM | 53.7 | 55.6 | 0 | 8 | 57.6 | 1 | pass |
| DUALOSC RS | 59.7 | 61.0 | 0 | 5 | 55.5 | 8 | pass |
| DUALOSC RS PCM | 61.0 | 62.9 | 0 | 5 | 55.2 | 8 | pass |
| SEQ+ARP HOME PCM | 78.9 | 82.2 | 0 | 8 | 42.9 | 2 | FAIL |

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

- [ ] **Shared noise generator** (2026-10-25, KEPT: build/fm1/synth_tightmidi2.fwsc = tight MIDI + shared noise, byte-identical to synth_sharednoise1 (sha256 7fb4490e2f28044d...), preflight OK, INSTALLED; run_tests 22/22, render golden rewritten). One noise generator per ear for the whole synth (oscmixer.c noise_shared_fill, once a render pass) instead of one per voice; each voice still blends the colours by its own colour morph and level. **Measured 2026-10-25, back-to-back PERF TEST against tightmidi1 (both runs below): 51 -> 63 of 70 pass, late 0.** Noisy (HEAVY / MULTI H / SEQ+ARP) rows -4.4 to -11.5 pts (SINGLE HEAVY 57.1 -> 49.8, BINAURAL HEAVY 71.2 -> 59.7, SEQ+ARP HOME 66.8 -> 59.6, SEQ+ARP STRIKE 76.2 -> 68.7); rows without noise about -0.4. Host: 8 voices of white noise cost the same either way (+5.7 ns/sample); white->pink 17.0 -> 11.3, pink->brown 28.3 -> 11.3. Noise-off renders bit-identical; 35 noisy render hashes changed (golden rewritten 2026-10-25 on the user's go). To do: listen to listening/noise_ab (A_pervoice vs B_shared: pad chord, wind chord, BINAURAL chord, NEWGRANGE with noise 8, HYPOGEUM, SKARA BRAE, CURSUS WIND, BONE FLUTE), chords with noise on the panel (fallback: reinstall synth_tightmidi1).
- [ ] Tight MIDI in + MIDI clock lock (2026-10-24, build/fm1/synth_tightmidi1.fwsc sha256 4381a7d4..., built, preflight OK, INSTALLED 2026-10-25 00:05; run_tests 22/22, rebuild reproducible). **(2) measured 2026-10-25, back-to-back A/B against carnac-0.9.3 (same source without this change): +2.3 pts median on every row (mean +2.5, range +1.8..+6.0; IDLE 16.6 -> 18.6, SINGLE x8 43.4 -> 45.9, SEQ+ARP rows +2.7, worst SEQ+ARP HOME PCM +6.0 and BINAUR MULTI H PCM +5.0); 57 -> 51 of 70 pass; no late halves in any row. That is ~5x the estimate and a flat cost even at idle, so it is not per-voice: suspect ep1_rx / uart_midi_poll in TIMER5 during the render, or the per-frame ext_q check in render_pass. Profile before keeping it.** (1) partly: enumerates, INFO and installs over USB OK; USB audio / CDC / web installer not yet checked. Was: MIDI read once per 512-frame half, pulses and notes on an 11.6 ms grid (1/16 steps up to ~6 ms off, tempo 10-30 % wobble). Now: TIMER5 stamps each MIDI-in packet (midi_stamp) and reads USB ep1_rx + TRS during the render (midi_in_render); midi_drain plays them at their frame in the next half (synth_midi_at): constant 23.2 ms in to sound; midi_clock.c locks to the pulses (DLL 1/8, 1/64, one beat averaged after Start / a jump). Host: test_midi_clock (pulses within 0.32-0.38 ms stamped, 1.40 ms on the old grid; counts kept through ramps, jumps, Stop/Continue, a silent sender, bursts). On device: (1) USB still fine: enumerates, web installer INFO, USB audio, CDC, an install over USB while notes play; (2) PERF TEST SINGLE x8 / SEQ+ARP rows vs synth_rites3 (in-render ep1_rx at 2 kHz, est. < 0.5 pt); (3) record ARP 1/16 + incoming notes against the source over USB and TRS, CLOCK SOURCE = MIDI: onset spread (expect < 1 ms) and a steady 23 ms offset; (4) DAW Start/Stop/Continue, tempo ramps.

- [x] **AMP DRIVE rework** (2026-10-25, INSTALLED 12:28 as build/fm1/synth_drive2.fwsc (= synth.fwsc, sha256 b2559a75...), with the peer sessions' seq/arp names chunk and settings v3 on board. **Measured 12:30: drive costs ~0.5-0.6 pt per voice: SINGLE x8 45.8 -> 49.8 (SOFT) .. 50.7 (TAPE); STACK HEAVY TUBE 68.1 (+6.5), BINAUR HEAVY OD 66.8 (+7.4), STRIKE HEAVY TUBE 55.6 (+5.1), SEQ+ARP HOME TUBE 66.1 (+6.1); all 10 drive rows pass, late 0. Undriven rows median +0.3 vs 10:14; SEQ+ARP HOME PCM 68.8 -> 72.7 and SEQ+ARP STRK FILT 69.0 -> 71.1 now fail (no drive in them: likely peer changes, unprofiled). 71/80.** Still to do: listen on the panel; PERF TEST rows SINGLE DRV SOFT..OD, STACK HEAVY TUBE, BINAUR HEAVY OD, STRIKE HEAVY TUBE, SEQ+ARP HOME TUBE added; amplifier.h amp_drive_step, drive_tables.h from src/host/gen_drive_tables.c). New models TUBE and OVERDRIVE (enum 4, 5: stored patches unchanged), every model smooth (no hard corner), gain exponential per model (SOFT/HARD/TUBE +30 dB, TAPE +24, FOLD +16, OVERDRIVE +40), makeup holding a -6 dBFS 110 Hz saw at its RMS; TAPE / TUBE / OVERDRIVE carry two floats of state per path. Final shape: amp_drive_block (one loop per model, one copy) on the block in place after the amp level (level_drive); the flash had no room for per-model inlined loops. Host, 8 held SINGLE voices at DRIVE 80 (render_drive_ab, ~5.7 ns timer steps): SOFT +0, HARD/FOLD/TAPE +5.7, OVERDRIVE +11.3, TUBE +22.7 ns/sample over 221 (old SOFT was +28). On device: PERF TEST SINGLE x8 and BINAURAL/STACK heavy rows with DRIVE 80 on each model vs DRIVE 0 (expect < 1 pt; TUBE and OVERDRIVE the worst); if a model costs more, specialise or drop ops. Listen: build/listen_drive (A_old vs B_new; 00_clean_* = DRIVE 0). Factory perfs with a driven layer come out 1-5 dB quieter than before (the old drive added up to +7 dB): FLINT, CAIRN DRUMS, SILBURY BASS, SOLSTICE FIRE, WOODHENGE, ANCESTOR the most.

- [ ] **MONO layer gives its voices to the other layer** (2026-10-25, in source, NOT built or installed; PresetStructures.h layer_voice_limit, voice_allocator.c pool_range). In DUAL / SPLIT a MONO RETRIG / LEGATO layer takes 1 voice and the other gets floor(c_other * (c_mono - 1) / c_mono) (SINGLE + SINGLE 1 + 7, mono DUAL OSC + SINGLE 1 + 6, mono SINGLE + STACK 1 + 5, both mono 1 + 1); layer 2 starts earlier in the pool when it has more than half. SPLIT's layer 2 has its own VOICE MODE / UNISON DETUNE now. Budget by the linear model (a cap-c voice costs at most 1/c of the budget), not measured. Host: voice_arch checks 1 + 7 both ways round in DUAL and SPLIT; the 49 render hashes unchanged. On device: PERF TEST-style worst case: mono BINAURAL (cap 6) + 5 STACK voices held, and mono DUAL OSC + 6 SINGLE HEAVY, under 70 %; SEQ+ARP with a mono SEQ layer and a 7-voice ARP layer.

- [ ] **Sequence and arp names** (2026-10-25, INSTALLED as build/fm1/synth_seqnames1.fwsc sha256 e4341ac0..., run_tests 23/23, preflight OK; the tree's other uncommitted work (AMP drive rework, MONO voice split) is in this build too; image 580,700 of 581,564 B app slot; preset_store_fm1.c chunk 1 = the names table, 0xA2000..0xA3FFF, kind 4, PS_NAMES_VER 1). The seq / arp chunks keep their layout, so saved sequences and arps load unchanged; an older firmware ignores chunk 1. Host: test_preset_store (names round-trip, a same-name re-save writes nothing extra, factory names stay unless saved, another version left out), ui_arch (knob 4 edits the sequence's / arp's name). On device: saved seqs / arps still load after the install; SAVE on SEQ, rename, save, power cycle: the preset list (Preset Encoder on SEQ) shows the name; same on ARP; a performance load shows its linked sequence's name on the SAVE page; a renamed SAVE costs one extra erase (listen for a longer audio gap than a plain re-save).

- [ ] **GLOBAL MIDI / clock settings kept across power cycles** (2026-10-25, in source, NOT built or installed; synth_panel.c settings record v3, set_inst / settings_apply). MIDI CH LAYER1/2, CUSTOM CC1/2, BEND RANGE and CLOCK SOURCE now belong to the instrument: saved in the CRNC record (its spare bytes, still 16 B) and put back onto every performance load and the power-on NEWGRANGE DAWN. A stacked performance (both layers on one channel, the factory DUAL ones) stays stacked on the layer-1 channel; on one, MIDI CH LAYER1 carries layer 2 along. INT TEMPO and BASE VELOCITY stay per performance. A v2 record loads (tune / curve / clock out / theme kept; the new ones at defaults). Host: x0x_unity_check OK only (no host test covers synth_panel.c). On device: set CLOCK SOURCE MIDI and MIDI CH LAYER1 5, wait 3 s, power cycle: both kept and the arp follows the DAW clock; load a DUAL factory perf: both layers on ch 5; load a SINGL perf: layer 1 on 5, layer 2 on your LAYER2 channel; PERF TEST, then check the settings are unchanged.

- [ ] **Wavetable bank 0 = Foundations, the old banks moved to 1..16** (2026-10-26, built as build/fm1/synth_found2.fwsc sha256 b36f3b49... (adds PCM_ID, editor command 104: an area's held image hcrc), run_tests 24/24, preflight OK, image 577,712 B; INSTALLED 2026-10-26 with `fm1_perf.py install --yes`: areas 0 and 1 already up to date, area 2 written, a second sync all up to date, INFO lists area 2 with 1 entry, STATS after: late 2 (the upload's erases), load 17 % (the linked wavetable image lost its 4,064-byte header padding, so ~4 KB of app slot came back); the build also carries the tree's uncommitted menu-rows and SEQ step-block work). Bank 0's image (src/dsp/foundations_bank.bin, 16,416 B, gen_foundations.py) lives in the preset store's free chunks 14..16 (0xBC000..0xC1FFF), uploaded as area 2: `python src/hardware/pcm_store_upload.py upload src/dsp/foundations_bank.bin --area 2`. `fm1_perf.py install` now runs `pcm_store_upload.py sync` after the firmware (writes areas 0..2 only where PCM_ID's hcrc differs); the web installer does the same (test_installer 10/10 incl. a same-length Foundations update). Saved and factory patches keep their wave codes, so every wavetable patch now plays the next bank down (user OK'd: "don't worry about presets"). Host: test_pcm_store (area 2 upload, cut-offs, flipped bit, PCM image / other bank refused, reboots, PCM areas untouched), render hashes rewritten after proving them identical with the old mapping. On device: before the upload the OSC WAVE list starts with Foundations playing a sine; the upload's info lists area 2 with 1 entry "Foundations"; after it, Foundations plays TRI..SAW+SQUARE on SCAN and survives a power cycle; the saved presets and PCM waves still load; a second `sync` writes nothing (all "up to date"); an upload's erases give the usual short audio gap.

- [ ] **DELAY HP** (2026-10-26; v2 INSTALLED as build/fm1/synth_dlyhp2.fwsc sha256 799c2bbd..., image 578,308 B: range widened on the user's "too narrow" to 1..127 = ~30 Hz..1 kHz exponential (float one-pole, flushed a block), runaway compensation per type (tape 200000, BBD 550000 x coefficient, top cap 3.0 via a third Q15 term), EFFECTS rows reordered delay first then reverb; PERF TEST 67/80, late 0, delay rows within +-0.6 of v1, STACK HEAVY TUBE 69.8 pass / SEQ+ARP STRIKE 71.1 fail (both borderline). v1: sha256 fc020d2a..., preflight OK, image 578,064 B (+352 over found2, ~3.5 KB app slot left), waves all up to date; PERF TEST 12:35: 67/80, late 0, every row ~+0.2..0.9 over the 2026-10-25 synth_drive2 run incl. IDLE +0.5 and the delay rows +0.2..0.6 (found2 was never PERF tested, so the step is not attributable; the scenarios run DELAY HP 0 = one branch a sample); 4 rows sitting at 68-69.4 crossed 70: BINAUR SH2 H / SH2 HP 70.3, BINAUR MULTI H PCM 71.1, STACK HEAVY TUBE 70.7; fx.c delay_fb scoop_c, EFFECTS row DELAY HP = delay_pingpong bits 1..7, no record version bump). A one-pole high-pass in the delay feedback only, 0 = off (bit-exact: fx_hash HEAD lines unchanged), 127 = ~165 Hz; the runaway top rises +amount x 50 so the howl onset stays (host: tape ~112, BBD ~116 -> ~120 at 127). Host: fx_hash 12 new hp lines, ui_arch (PING PONG and DELAY HP keep each other's bits), A/B WAVs build/listen_dub_scoop/. Cost: one branch + one multiply a sample when on (unmeasured). On device: old saved performances load with DELAY HP 0 and PING PONG as saved; DELAY HP 127 at FEEDBACK 100 thins the repeats, first echo keeps its bass; FEEDBACK 120+ still howls on TAPE and BBD; PERF TEST unchanged; XIP headroom after the build.

- [ ] **DELAY TIME max 1.0 s** (2026-10-26, INSTALLED as build/fm1/synth_dlytime1.fwsc sha256 aea79bf1..., preflight OK, image 578,308 B; fx.c delay_len = 64 + DELAY TIME x 347 (DELAY_TIME_STEP; was x 512, 1.47 s); factory_presets.c unsynced dtime rescaled x 512/347 (every factory time within 4 ms; ANCESTOR synced, unchanged); fx_hash delay_time 40 -> 59 (still past TAPE_GAP_MAX), golden rewritten; run_tests 24/24; no PERF TEST (no per-sample change). User saved perfs with SYNC off play ~0.68x their old time (no migration). On device: DELAY TIME fully right = ~1 s; NEWGRANGE DAWN echo timing unchanged by ear.
- [ ] **Listen: BBD without the grit, 2026-10-26 (synth_bbdclean1.fwsc, installed).** User: "the bbd delay sounds bad and has too much digital grit/aliasing. Sounds bitcrushed." Cause: one MN3008's 2048 buckets clocked at ~2 kHz for a 1 s echo, held-step reads, filters fixed at ~2.7 kHz above the clock's Nyquist. Now 8192 stages (four chips; full rate to 186 ms, ~8 kHz clock at 1 s), interpolated bucket reads, filters held below ~0.35 x the clock. Host, junk around a sine echo (old -> new): 1.2 kHz at DELAY TIME 127 -1.5 -> -30 dB, 500 Hz -18 -> -40, 200 Hz -20 -> -49. Howl onset kept (DELAY HP 127 now howls from FEEDBACK 120, was 127). PERF TEST 15:04: HEAVY CEL+BBD PP 54.9 -> 55.1, SEQ+ARP SPR+BBD 64.8 -> 64.9; 65/80 (borderline rows, not BBD: STACK HEAVY TUBE 69.8 -> 70.4). A/B: build/listen_bbd_grit/{old,new}_bbd_saw_time{70,127}.wav. Check: long BBD echoes warm, no bitcrush; DEGRADE still darkens.
- [ ] **Listen: DELAY TIME glide 4x faster, 2026-10-26 (synth_dlyslew1.fwsc, installed).** User: "the first delay changes but then the feedbacked delays don't maintain the same delay length". Cause: TAPE_SLEW 12 (tau 93 ms, ~0.5 s to settle): a note soon after a DELAY TIME turn got its first echo at an in-between length (30 -> 100, note 100 ms later: 753 ms then 788 ms repeats). Now TAPE_SLEW 10 (tau 23 ms): every echo within ~2 ms of the new length 50 ms after the turn, tape and BBD. All 36 fx hashes changed (the heads glide in from 0 at start); no per-sample cost. Check: turning DELAY TIME while it howls still bends smoothly, no zipper per knob step.
- [ ] **VEL > LEVEL, LFO DEPTH MOD, cutoff MOD AMOUNT 0..127; patches v10** (2026-10-26, built as build/fm1/synth_vellvl1.fwsc sha256 5f8f48ac..., preflight OK, INSTALLED 2026-10-26 ~17:00 (fallback: synth_brackets1.fwsc). **PERF TEST A/B on the device (back-to-back, runs in PERF TEST runs below): synth_brackets1 69/80, synth_vellvl1 65/80 twice (repeat within 0.1 pt); median +1.2 pts, SINGLE x8 46.3 -> 47.5, SINGLE HEAVY 50.6 -> 52.9, SH-2 heavy rows +3..+5 (SINGLE SH2 H 58.2 -> 63.0), SEQ+ARP +3; no late halves. Newly over 70: SEQ+ARP STRIKE 71.2, DUALOSC SH2 H 70.2, BINAUR MULTI H PCM 71.8, STACK HEAVY TUBE 70.9. The scenarios set none of the new amounts (every gain exact unity), and the new control-rate work estimates under 0.5 pt, so most of the step is unexplained: suspect the DSP code's placement in XIP (I-cache), not profiled yet.** image 579,564 B of 581,564 (~2 KB app slot left), RAM .data+.bss 71,948 B, pool 278,400 B; run_tests 24/24). FREC_VER_ENGINE 10 / FREC_VER_PERF 10, stored patch still 173 B (the envelopes' 8 flags packed into a byte, DEPTH MOD SRC in SPD MOD SRC's high nibble), so every chunk stays where it was; v5..v9 patches and v6..v9 performances migrate at load (engine_from_v9: ENV1 VEL > LEVEL 111, the cutoff amount doubled from its centre, a negative one 0); a chunk is written as v10 only when one of its slots is saved, so an older firmware still reads the unsaved ones. Render hashes rewritten after proving them identical with v9's ENV1 velocity maths (the only change: ENV1 at 111 instead of the old table, and ENV1 as a mod source now carries velocity). Host: test_flash_record (v10 round trip, each envelope flag alone, v9 -> v10, v8 / v7 -> v10), test_preset_store (v9 patch chunk, v9 performance chunk, save into them), test_voice_arch (VEL > LEVEL 0 / 127, ENV2's gain, DEPTH MOD on the wheel, an LFO refused as depth source). Cost: 3 velocity gains + up to 3 source reads a voice tick (control rate), unmeasured. On device: saved patches and performances load and sound as before (play soft and loud); ENV1 VEL > LEVEL 0 = level ignores velocity, 127 = soft notes near silent; LFO2 on pitch with DEPTH MOD SRC modwh, AMT 127: no vibrato with the wheel down, full with it up; save a patch, power cycle, still there; PERF TEST unchanged.
- [ ] **SH-2 / PERF TEST step after synth_vellvl1 = code placement, not the change** (2026-10-26, device runs in "PERF TEST runs" below). Same DSP source, five builds (avg %: SINGLE x8 / SINGLE SH2 H / DUALFLT SH2 H / pass count): brackets1 (old source) 46.3 / 58.2 / 75.4 / 69; vellvl1 47.5 / 63.0 / 78.6 / 65 (repeat within 0.1); DSP functions 4-aligned (synth_align4a) 47.2 / 60.1 / 77.2 / 66; 8-aligned 47.1 / 59.1 / 76.6 / 64 (but SPREAD SH2 H, STACK HEAVY TUBE, SEQ+ARP HOME +1..2 worse); the filter's block loops in RAM (.c1_text, synth_ramflt1, user OK'd) 47.9 / 59.5 / 77.9 / 66 (IDLE +0.7, BINAUR MULTI H PCM 73.4). Cause: the DSP runs from XIP through the I-cache; vellvl1's UI code grew 1,030 B (2 mod 4), moving every DSP function (the filter functions are byte-identical in size) and flipping each instruction's 4-byte fetch phase; past that, placement reshuffles rows by +-2 either way (cache sets). RAM fetch is no faster than a cache hit, and the whole DSP no longer fits the 64 KiB RAM window (2.7 KB over), so the RAM move was dropped. Kept: build_fm1.py --dsp-align, default 4 (LLVM -align-all-functions; clang 4 ignores -falign-functions), +108 B. INSTALLED synth_align4a.fwsc (sha256 11b785c3..., = a default build of the tree). Real SH-2 savings need fewer saturators (changes the sound: user's ear). Lesson: compare PERF TEST rows only between builds of the same layout, and expect +-2 on heavy rows from unrelated code changes.
- [ ] **DUAL SPREAD cap 6 -> 4** (2026-10-26, user's choice after SEQ+ARP DSPRD CEL 95.6 % at 6; INSTALLED build/fm1/synth_dspread4.fwsc). Device at 4: DSPRD 47.1, HEAVY 49.7, MULTI H 50.8, SH2 H 53.9, H CEL+PP 55.5, MULTI H PCM 58.4, SH2 CEL+PP 58.7, SEQ+ARP DSPRD CEL 66.8 / peak 70.1: all pass. Only the 6 DUAL SPREAD render hashes rewritten.
- [ ] **SINGLE replaced by SPREAD; DUAL OSC cap 5 -> 6** (2026-10-26, INSTALLED build/fm1/synth_spreadsingle1.fwsc, image 580,640 B). SINGLE left the ARCH list (row starts at DUAL OSC); a patch that says SINGLE (stored: engine_store_unpack_v; factory: factory_voice) loads as SPREAD with SPREAD and SPREAD MOD at 0 = the same DSP path (varch_single_to_spread). No record version change: new patches never say SINGLE, older firmware reads SPREAD. OSCMIXER keeps SPREAD's knobs (SPREAD / NOISE / SUB LEVEL / SPREAD MOD); NOISE COLOUR and SUB OCTAVE are rows. test_flash_record: SINGLE -> SPREAD at 0, SPREAD keeps its spread. PERF rows named SINGLE still set arch 0 directly (same DSP). **DUAL OSC at 6 on device: every row within 0.2 of cap 5 (DUAL OSC 60.6, HEAVY 63.0, MULTI H 64.4, H CEL+PP 68.5, SH2 H 68.3, PCM 59.6, RS PCM 63.0): passes. New row SEQ+ARP DSPRD CEL 95.6 / peak 99.5 (no late halves, but above the 88 % shed: the overload guard would drop voices there; SEQ+ARP SPRD CEL is 80).** On device: an old SINGLE patch/perf loads as SPREAD and sounds the same; ARCH knob can't reach SINGLE; DUAL OSC 6-note chord.
- [ ] **SPREAD cap 6 -> 8** (2026-10-26, INSTALLED build/fm1/synth_spread8.fwsc). Device PERF TEST at 8 (4 a core): SPREAD 57.5, HEAVY 61.7, MULTI H 64.1, H CEL+PP 68.6, H TUM+PP 67.1, SH2 H 69.6, PCM 64.4 pass; SPREAD SH2 CEL+PP 74.3 FAIL (peak 75.6, no late); SEQ+ARP SPRD CEL 80.4 (was 80.2 at 6: fails either way, the sequencer row). Same failure profile as DUAL SPREAD at 6. Per voice on a core: SPREAD ~9.8 pts, DUAL SPREAD ~14.2 (the host said they were close). Only the 6 SPREAD render hashes changed (rewritten). On device: 8-note SPREAD chord, DUAL/SPLIT splits now follow 8.
- [ ] **DUAL SPREAD architecture** (2026-10-26, VARCH_DUAL_SPREAD = 8, name DUAL SPRD; UiRow.archs widened to uint16 (struct stays 16 B); INSTALLED build/fm1/synth_dspread1.fwsc, image 580,692 B (~0.9 KB app slot left)). OSC1 and OSC2 each as SPREAD's 4 detuned playheads (one SPREAD knob + SPREAD MOD for both), by OSC MIX, no cross-mod -> FILTER1. OSCMIXER knobs OSC MIX / SPREAD / SUB / NOISE. Existing render hashes bit-exact. Host 1.44x SINGLE per voice. **Device PERF TEST at cap 6 (3 a core): DSPRD 60.6, HEAVY 63.8, MULTI H 65.6, SH2 H 69.6, H CEL+PP 69.8 pass; MULTI H PCM 75.7 and SH2 CEL+PP 74.3 FAIL (peaks 76.9 / 75.7, no late halves, under the 88 % shed). At 4 (2 a core) all pass, worst SH2 CEL 58.6. Cap 6 in source, user to choose 6 vs 4.** On device: OSC MIX sweep, SPREAD 0 -> 99 detune -> chords, OSC2 a different bank, save + power cycle.
- [ ] **FULL architecture = DUAL OSC into DUAL FILTER** (2026-10-26, VARCH_FULL = 7, name FULL; INSTALLED build/fm1/synth_full2.fwsc sha256 1c4f8ac9..., image 580,088 B (~1.5 KB app slot left)). OSC1 + cross-modulated OSC2 by OSC MIX (DUAL OSC's OSCMIXER knobs and rows) -> FILTER1 / FILTER2 serial .. parallel with BALANCE / ROUTING (DUAL FILTER's FILTER MIXER); OSC2, FILTER2 and FILTER MIXER pages show. No patch format change (a new arch value; an older firmware plays it as SINGLE). Existing render hashes bit-exact (dual_osc_block shared with DUAL OSC). Host: 1.95x SINGLE per voice at 6, 2.10x at 4. **Device PERF TEST: cap 6 (3 a core) fails, FULL 70.7, HEAVY 71.6, MULTI H 74.6, SH2 H 82.2, H CEL+PP 78.6 (synth_full1); cap 4 (2 a core) passes all 8 FULL rows: FULL 54.3, MULTI H 57.3, SH2 H 62.1, H CEL+PP 61.3, SH2 CEL+PP 66.7 (synth_full2). Cap 5 would still put 3 voices on one core. ~17.7 pts per voice on a core (DUAL OSC ~13.7, SINGLE ~7.1).** On device: pick FULL on HOME, OSC MIX and CROSS-MOD sweep, ROUTING serial -> parallel, BALANCE; 5th note steals; DUAL/SPLIT with FULL on one layer (2+2: the smaller cap halved; a MONO layer 1+3); save, power cycle, still FULL.
- [x] **Waves-missing start-up screen** (2026-10-29, build/fm1-wavewarn/synth.fwsc, image 564,092 B, INSTALLED). **Device 2026-10-29: area 1 cleared + power cycle -> empty field + WAVES MISSING (user saw it); sync -> power cycle -> normal stones, no warning (so the embedded hcrcs match the shipped images). OUT OF DATE not shown on device (needs a different valid image).** At power-on pcm_boot compares each area (0, 1, 2 = Foundations) with the header CRCs build_fm1.py read from src/dsp (PCM_WANT_HCRC0..2): an empty area -> the splash is redrawn with no stones and WAVES MISSING / RUN THE INSTALLER for 3 s; a different image -> WAVES OUT OF DATE. On device: normal boot after a sync shows the stones (no delay); `pcm_store_upload.py clear --area 1` + power cycle -> empty field + WAVES MISSING; `sync` -> normal again.
- [ ] **Per-layer mix bus + MFX STONE PHASER** (2026-11-01, in source, NOT built for the FM1 or installed). Voices write their layer's dry pair (2 multiply-adds a sample instead of 4; STACK/BINAURAL 4 instead of 8); synth_runtime.c sums each layer, runs its MFX, then takes the delay/reverb sends from the layer's sum. Render hashes rewritten after proving the change is summing order only: 2,914 of 10.2 M samples differ, max 4 LSB (-102.6 dB); with MFX OFF the output is bit-identical to the bus change alone. STONE PHASER (src/dsp/mfx.c, test_mfx: within 0.012 dB of the circuit's own bilinear transfer function). Host cost: 59 ns/sample a layer (stereo), +25 % on 8 held SPREAD pad voices (249 -> 312 ns). Not set by any patch yet (mfx_params in RAM only). On device: PERF TEST before/after the bus change alone (expect equal or a little better; compare builds of the same layout, +-2 on heavy rows); then a PERF TEST row with STONE PHASER on one layer and on both (DUAL), to set its real cost; listen for zipper on the sends while turning DELAY/REVERB SEND (now stepped per 128-frame pass, was per voice tick).
- [ ] **MFX ENSEMBLE** (2026-11-01, in source, NOT built for the FM1). The Solina string ensemble's chorus (docs/effect schematics): chorus + tremolo oscillators, three 120-degree phases (the third the inverter's -(C1 + C2)), three BBD lines sharing BULLROARER's two 512-sample brigades (no extra RAM), all wet; knobs AGE / RATE / SHIMMER / DEPTH, row WIDTH. Host cost 15.9 ns/sample a layer vs STONE PHASER 27.2: expect ~2 pts a layer (phaser measured 3.2..3.7). No PERF TEST row yet (all 16 flag bits taken: needs a wider flags field or a row swap). On device: its cost on one and both layers; hiss at AGE 127 on a quiet pad (should duck to nothing in silence); no zipper when turning AGE (the filters and delay step per 16 frames).
- [x] **MFX costs on the FM1** (2026-11-02, build/fm1/synth_mfx3.fwsc sha256 bba4ef72..., commit 8aeb1d9; one build, so no layout noise between rows). Idle (IDLE 19.6): STONE PHASER +3.6 one layer / +7.1 both; ENSEMBLE +4.0 / +7.6; CHORUS +3.9 / +7.7; ENS+PHASER +4.9 / +9.4. Under load the same effect costs more (the voices' code and the effect's compete for the XIP I-cache): SPREAD H CEL+PP 67.7 -> +PHASER 72.3 (+4.6), +ENS+PHASER 75.5 (+7.8); SEQ+ARP HOME 64.3 -> both layers PHASER 72.9 (+8.6), ENS+PHASER 77.2 (+12.9, peak 81.5). Half-rate brigades did not make ENSEMBLE / CHORUS cheaper than the phaser (int16 line reads and conversions dominate). Headroom to the 88 % voice-shed guard (peak): plain patches (peaks <= 65) 23+ pts, any MFX on both layers fine; heavy MULTI / PCM / CEL rows (66..75) 13..22, both layers fine; SH-2 HEAVY on two-filter archs and SEQ+ARP with CELESTIAL (76..84) 4..12: one MFX at most, ENS+PHASER on both would shed. No late halves anywhere.
- [x] **MFX trimmed + layer 2's on core 1** (2026-11-02, in source, uncommitted). (1) mfx.c: the half-rate brigades are floats (a union with BULLROARER's int16 line, same RAM), read heads in 16.16 fixed point, each model's knob maths (exp2, tans, divides) worked out only when a glided knob moves (the glide snaps its last 1/500 of a step), STONE PHASER's loop with the chain's input divider folded in and no gain ramps at rest. Output against the old code: -78 dB or better (the int16 rounding gone; the snap moves an LFO's phase by a hair over time), BULLROARER bit-identical. Device (build/fm1/synth_mfxopt1.fwsc, same build A/B): STONE PHASER 3.6 -> 3.3 a layer; ENSEMBLE / CHORUS / ENS+PHASER only ~0.1-0.3 better (the conversions were not their cost). (2) synth_runtime.c: with an MFX on both layers, layer 2's runs on core 1 while core 0 runs layer 1's (mfx_job; the voices' fallback if core 1 fails; layer sums and MfxState on cache lines of their own). Render hashes: 6 MFX scenarios added (61 old unchanged), --core2 / --core2-fail identical. Device (build/fm1/synth_mfxopt2.fwsc, IDLE 20.4): one layer / both: PHASER +3.3 / +3.3, ENSEMBLE +4.0 / +4.1, ENS+PHASER +5.0 / +5.1, CHORUS +4.0 / +4.1; SEQ+ARP HOME 64.2 -> PHSR2 68.3 (was +8.6, now +4.1), ENPH2 72.2 (was +12.9, now +8.0). Rows without MFX read ~1 point higher than synth_mfxopt1 (IDLE too, which runs none of the change: the I-cache layout). Still open: one MFX alone (SINGLE, or one layer) stays on core 0: splitting its two sides across the cores would take about half of it back.
- [x] **Sequencers skipped at rest** (2026-11-02, in source, uncommitted; FM1 runs build/fm1/synth_seqquiet1.fwsc). The device stage profile (CARNAC OS PROF run 11:22, one core) showed the MAQ + ARP frame calls costing 29 of a sample's 544 ticks even stopped (their idle fast paths: ~60 loads and ~10 calls a frame), on core 0 while core 1 waits. Now maq_frame / carp_frame report an idle frame; after one, the render skips them (synth_runtime.c seq_quiet) until a MIDI event in the pass, and runs the last frame skipped before the event or at the pass's end (maq_frame_late: the same writes at the same clock). test_seq_idle (state + output every block, against the SYNTH_SEQ_NO_IDLE build) and all 67 render hashes unchanged. Device vs synth_mfxopt2: every row without the sequencers -5.0 points (median of 94; -4.3 .. -6.6), IDLE 20.4 -> 15.5, SINGLE HEAVY 52.4 -> 47.8, DUALFLT SH2 CEL 81.1 -> 76.4; SEQ+ARP rows unchanged (playing, never idle); 99 of 109 pass (was 91), no late halves. Next for the SEQ+ARP rows: the same while playing (skip to the next pulse or due frame), SEQ was 64 ticks there.
- [x] **One MFX on both cores** (2026-11-02, build/fm1/synth_mfxsplit1.fwsc). mfx.c split into a plan (core 0: knob glides, LFOs, the shared read heads, settings; MfxStep a control step) and one run a side (MfxSide, own cache lines): with one MFX on, its right side runs on core 1 (synth_runtime.c mfx_job arg | 2). STONE PHASER / BULLROARER bit-identical; ENSEMBLE / CHORUS: each side its own hiss noise and (ENSEMBLE) compander follower, the clocks' wander its own draws (with no wander: within -57 dB, the hiss's level). Render-hash MFX scenarios now with both layers sounding (they were silent on layer 2) + ENSEMBLE alone; --core2 / --core2-fail identical. Device (IDLE 14.9): one layer PHASER +1.9 (was +3.3), ENSEMBLE +2.5 (+3.9), ENS+PHASER +3.0 (+5.0), CHORUS +2.2 (+3.9); both layers ~+3.3..4.8 as before; no late halves.
- [x] **Sequencers skipped while playing** (2026-11-02, build/fm1/synth_seqplay1.fwsc). Between clock pulses a playing MAQ / ARP acts only on what is due (strikes, strum notes, releases, cells: absolute frames); maq_next_due / carp_next_due give the earliest, and the render skips their frames until it, the next pulse (the clock still runs every frame) or an event. test_seq_idle gained a stamped-clock phase (synth_midi_at, as TIGHT MIDI; 100 -> 137 BPM, Start, Stop) with both playing: output and state bit-identical to the every-frame build at blocks 7 / 32 / 128, so every note goes out on the same frame. Device: every SEQ+ARP row -9.0 (median; -8.6 .. -9.7), SEQ+ARP HOME 63.3 -> 54.3, SPRD CEL 80.3 -> 70.6, others unchanged; 105 of 109 pass, no late halves. Not measured on the device: an external clock (no device timing tool; the host proof above stands for it).
- [ ] **Stereo DLY > REV + LOOP TONE wear** (2026-11-03, src/dsp/fx.c, in source, NOT built for the FM1). DLY > REV runs each side on its own (mean into the reverb send, half the difference into the tank's halves / the two springs via rv_sring); away from LOOP TONE 64 the loop wears (darker: per-side time wander +-0.55 ms, lopsided saturation, heat darkening; thinner: even saturation, a little wander). 64 + PING PONG off = bit-identical (fx_hash 36/36, fx_cross numbers unchanged). Host cost with both cross-feeds at 127 (MOD, PING PONG): the loop 6.8 -> 13.6 ns/sample at 64, ~18-20 worn (the whole FX ~48 ns); loop off unchanged (+~3 ops/sample in the tank for the side, core 1). RAM ~4.7 KB .bss (3 x 1 KB wander lines, 1 KB side ring, 0.5 KB side buffer). On device: PERF TEST row with MIX fully engaged at LOOP TONE 64 / 20 / 110 vs synth before (MIX alone measured +1.7..2.4 pts), check .bss / pool fit and XIP size, listen at the knob's ends.
- [ ] **Stereo BBD + tape PING PONG heads** (2026-11-03, src/dsp/fx.c, in source, NOT built for the FM1). Tape PING PONG: the near head its own wow/flutter (WOW_INC2 0.565 Hz, FLUTTER_INC2 5.86 Hz). BBD: two lines, a track each in the tape's halves (BBD_TRACK 8192, BBD_N 8192 -> 8064: no RAM), shared clock, own wobble/filters/compander/saturation/feedback; PING PONG = true ping pong (L -> R x1.06, R -> L x FEEDBACK; howl within 1-2 dB of before at 110..127); REV > DLY stereo for the BBD (dl_side_in). fx_hash: 18 rows rewritten (tape pp + all BBD), 18 unchanged; render hashes unchanged. Known, pre-existing: the BBD settles onto a stuck DC of up to ~60 LSB after a tail (old and new). Host cost (TOMB base, coarse timer): BBD +11 ns/sample full rate, +14 slowed; BBD pp ~0 full rate (old ran slowed), +8 slowed; BBD + loop 39.7 -> 56.7; tape unchanged. On device: PERF TEST row with the BBD (pp off/on, TIME 20/90) vs before; XIP size (bbd_delay_block grew); listen to listening/delay_stereo on the FM1.
