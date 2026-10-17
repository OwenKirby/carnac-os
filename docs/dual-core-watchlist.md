# Dual-core on the FM-1: build check and watch list

Checked 2026-10-14 against [dual-core-plan.md](dual-core-plan.md); fixes the same day. Status per item: **done**,
**open** (needs the device or the Felucca glue), or **watch** (fine now, keep it that way).

## How it was checked

The JieLi pi32v2 toolchain and Docker are not installed on this PC, so this is **not** a real FM-1 build. Stand-in:
emsdk clang 24, `--target=i686-unknown-none-elf -ffreestanding -fno-builtin -Os -DFELUCCA_ID`.

1. Each firmware source on its own, with `-Wall -Wextra -Wshadow`.
2. **Felucca's own `firmware/src/felucca.c`** (the real unity file) with every synth source `#include`d before
   `editor.c`, as the glue will do: src/dsp (wavetable_attach.c, not wavetables_image.c), UIStateMachine.c,
   ui_seq_draw.c, ui_arp_draw.c, ui_page_draw.c, and src/hardware/wt_store_fm1.c after `editor_menu.c`. Its generated
   headers come from Felucca's tools/gen_*.py (the font and icon tools ran without libraqm, which only matters for
   glyph shapes). The front end must pass with no errors; the LLVM IR must call nothing undefined except what the
   .S files provide. Only the backend can't run (x86 rejects pi32v2 inline asm like `csync`).

Result after the fixes: 0 errors, no warnings in synth files, and the only undefined symbols are `isr_alnk0` and
`isr_timer5` (both in hal/fm1_isr.S). No float. The `__udivdi3` calls (fx.c, midi_clock.c) are fine: Felucca's
phys_dsp.c does the same 64-bit divides and links without a runtime library.

## A. Build blockers (one core or two)

1. **done** `abs()` (envelopes.c) replaced by a two-sided compare; Felucca links no libc.
2. **done** `<string.h>` / `<stdlib.h>`: src/hardware/synth_libc.h declares Felucca's `memset` / `memcpy` /
   `memcmp` (with its `unsigned` length) under `FELUCCA_ID`, the real header elsewhere. Nothing in src/ outside
   src/host may call any other libc function.
3. **done** Collisions inside src/: one `clamp_q15` (int32) and `clampi` in src/dsp/dsp_clamp.h; synth_runtime.c's
   int64 one is `clamp_q15_64` (merging it would have truncated the mix sums); `perf_mode` -> `rt_perf_mode` in
   synth_runtime.c.
4. **done** Collisions with Felucca: `voice_start` -> `rt_voice_start`; `PAGES` / `cur_page` / `preset_step` /
   `arp_chord_off` -> `UI_PAGES` / `ui_cur_page` / `ui_preset_step` / `ui_arp_chord_off`; fx.c's `mulq15`,
   `REV_COMB`, `REV_AP`, `rev_comb`, `rev_ap` -> `sfx_*` / `SFX_*` (Felucca `#define`s `rev_ap`). `arp_chord_off`
   was only found by the real unity compile, which also covers typedefs, enum constants and macros.
5. **done** `global_init` had no FM-1 version (global.c used fopen). It now boots on factory defaults under
   `FELUCCA_ID`, like the browser. **open:** storing it in Felucca's globals flash is glue work.
6. **done** src/hardware/audio_hal.h (host-only) `#error`s under `FELUCCA_ID`: its `HALF_WORDS` would redefine
   Felucca's.
7. **watch** New src/ files: prefix file statics (`sfx_`, `rt_`, `ui_`, `pu_`, `carp_`...) and re-run the unity
   compile. Felucca has many short static names (`voice_start`, `cur_page`, `arp_*`).

## B. Two-core safety in the voice path

8. **done** `noise_seed` was one global RNG for every voice. Each `NoiseState` (per voice, per BINAURAL ear) now has
   its own seed, set once at boot (`noise_seed_init`, generator = 2 x voice + ear). This changed the noise once
   (same colour and level, other random numbers: about 0.2 dB on the test's RMS); everything else stayed
   bit-identical.
9. **done** `mod_tick` was one global pass counter for the mod-slot cache. Now `voice->mod_pass` per voice and
   `layer_mod_pass` for the layer pass (core 0 only). Bit-identical on one core.
10. **done** Block-major render (plan step 2): voices render in runs of at most 8 frames, cut before every event,
    clock pulse and layer tick; voices ask the clock about their own frame (`midi_clock_phase_at`). Bit-identical
    over 43 scenarios (src/host/test_render_hash.c). This also answers the old items "layer state read by the
    voices" and "master_midi read live": nothing they read changes during a run.
11. **watch** Partial sums stay unclamped: each core sums into its own `MixBus` (int64), and `clamp_q15_64` runs
    once, on the total, before the FX and at the output.
12. **watch** Anything new a voice reads must either hold still between runs, or end the run when it changes
    (call `voices_up_to` first, as `synth_midi_3` and the layer tick do). Anything a voice writes must be its own.
    test_render_hash catches a mistake only if a scenario exercises it: add one for each new feature.

## C. Rules for the core-1 hand-off (at deploy)

13. **open** Core 1 renders only inside the audio interrupt window. Felucca renders in the ALNK0 interrupt
    (audio.c), one 128-frame half at a time, and MIDI arrives through `midi_in_q`. Core 0 starts core 1 on a run and
    waits for it **before leaving the interrupt**, so the main loop (UI knob writes, `patch_load`, saves) never
    runs while core 1 reads `engine_state`, `perf_state` or `poly_bank`.
14. **open** Hand-off rate: one per run, so 16 or more per half. If the sync is dear, lengthen the runs (plan,
    "As built").
15. **open** Voice ownership: `rt_voice_start`, `note_off`, `enforce_voice_limits` and stealing all run on core 0,
    between runs, while core 1 is idle. Keep it that way.
16. **open** Core-1 stack: deepest frames on i686 -Os are `voice_control_tick` 156 B, `update_poly_lfo_engines`
    60 B, then `slot_output` 40 B <-> `resolve_source` up to 3 deep. Roughly 450 B on x86; pi32v2 frames may be
    larger. X0X overflowed at 256 B and needed 828 B. Give core 1 at least 1 KB with a canary.
17. **open** `.ram_text` is only 24 KB (app.ld), shared with Felucca's flash-write routines. Core 1's idle loop must
    live there, and build.py rejects calls out of `.ram_text`.

## D. Measure on the device

18. **open** I-cache pressure: the voice path is ~15 KB (i686 -Os); two cores share 32 KB with the FX, the
    sequencers and Felucca. Compare `cpu_pct` with the same architecture on both layers vs two different ones.
19. **open** Wavetable XIP contention: 8 wavetable voices vs 8 basic-wave voices, two cores.
20. **open** Core-1 bad reads (X0X: some units): a start-up self-test render on both cores, else one core.
21. **open** Flash-write parking for every writer: saves, autosave, globals, the wavetable upload (SysEx 96..99)
    and OTA. Test each while notes are held.
22. **open** Load balance: core 0 also runs events, FX (~1/3 of a SINGLE voice) and the limiter. Start 4 + 4.
23. **open** Block-major overhead on one core: ~3–5 % on the host (docs/fm1-measure-checklist.md).

## E. Small things

24. **done** `web_lr` and `synth_web_render` / `synth_web_buffer` are left out of the FM-1 build.
25. **watch** RAM: `MixBus` is 4 KB of .bss per core (8 KB with two), `patch_bank` 22,272 B in .pool. Both fit.
26. **watch** Keep 64-bit divides out of `render_voice_run` (core 1's loop).

## Re-running the checks

- Host: build and run src/host/test_render_hash.c (bit-identical), test_voice_arch, test_maq_seq, test_arp_seq,
  test_ui_arch, test_flash_record (build lines at the top of each file).
- Browser: web/emu/build.sh.
- FM-1 stand-in: `sh src/host/fm1_unity_check.sh` (the unity compile in "How it was checked"; it generates Felucca's
  headers itself on the first run).
