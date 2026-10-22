# PCM wave store (shelved 2026-10-21)

PCM-style waveforms (single cycles or looped samples) can be stored in flash outside the firmware and uploaded over USB-MIDI. This prototype built the storage and the upload path, then was shelved: the question was only whether it is possible. **Nothing plays the waves yet.**

## Why outside the firmware

The app slot (581,564 B) holds the 15 wavetable banks (249,856 B) and the 20 attacks (71,739 B), leaving about 37 KB. Dropping a filter flavour frees only about 1 KB, because all the filter code together is about 4 KB.

## Where the waves would go

| Area | Flash | Size | Was |
|---|---|---|---|
| 0 | 0x97000–0x9FFFF | 36 KiB | X0X's sound / pattern / song objects (Carnac never uses them) |
| 1 | 0xCE000–0xDBFFF | 56 KiB | Preset store chunks 23–24 (empty) + 40 KiB unused after them |

- The preset store shrinks from 25 to 23 chunks (`PS_NCHUNK`). No bank's chunks move, so saved presets survive.
- Left alone: the panel calibration (0xDC000), the update loader's staging area (0xE0000 and up), preset chunks 1 and 14–16 (room for the presets to grow), and 8 KiB at 0xDE000 (spare).
- 92 KiB holds about 46 single-cycle waves at 16-bit (2 KB each), about 90 as 8-bit mu-law, or about 4 s of looped mu-law at 22 kHz.

## What was built

All 18 host tests passed and the FM1 build passed its checks (image 544,532 B). Nothing was installed or committed.

- **Format** (`src/dsp/pcm_store.h` / `.c`):
  - A 32-byte header ("CRPW", with a CRC of everything after it) is written last, so an upload that's cut off leaves the area empty.
  - Then one 36-byte entry per wave (at most 64 per area): name (12 characters), 16-bit or mu-law (the attacks' codec), LOOP / CYCLE flags, root note, cycles in the loop, rate, offset, length, loop points.
  - Every entry is range-checked before use. Waves are read in place through XIP.
- **Firmware** (`src/hardware/pcm_store_fm1.c`): SysEx commands 100 BEGIN, 101 WRITE, 102 END, 103 INFO. Both areas are loaded at boot after the presets. Each flash erase silences the audio for tens of ms, so uploading is a studio job.
- **PC tool** (`src/hardware/pcm_store_upload.py`):
  - `build out.bin [--cycle N] [--ulaw] *.wav`: WAV `smpl` chunks give the loop points and root note; `--demo` makes test waves.
  - `upload out.bin --area A`, `info`, `clear --area A`, and `syx` (writes the frames to a file, for the tests).
- **Test** (`src/host/test_pcm_store.c`, in `run_tests.sh`): runs on a simulated flash and covers uploads, reboots, cut-off uploads, damaged data, bad entries, and that nothing is written outside an area. Mutation-checked.

## To bring it back

The code is in `docs/shelved/pcm-wave-store.patch`. It was made against commit 8e3f5b5. From the repo root:

```sh
git apply docs/shelved/pcm-wave-store.patch
sh src/host/run_tests.sh
```

The same changes are also in a local git stash, "PCM wave store prototype (2026-10-21, shelved)".

If the patch no longer applies cleanly, the conflicts will probably be in `src/hardware/preset_store_fm1.c` (the `PS_NCHUNK` line and its header comment) and `src/hardware/synth_main_fm1.c` (four small hooks). The five new files apply as they are.

## Still to do

1. **Playback:** an oscillator mode for the waves. CYCLE waves can use the wavetable oscillator's phase path; looped samples need their rate and root note for pitch.
2. **Choosing a wave:** a way to pick one on the panel.
3. **A patch byte for the choice:** after the SH-2 filter work uses the last two spare bytes for DRIVE (format v9), this means either packing some on/off settings into single bits or another format change with a migration. Playing a wave must not cost polyphony.
4. **Device check:** saved presets still load; both areas read empty at first (area 0 may hold old X0X data); the demo waves upload and survive a power cycle; `clear` works; boot time with a full area 1 stays normal.
5. **Before installing:** this changes the flash layout, so run the preflight and keep the rescue kit ready.
6. **Manual:** add a section once the waves can be played from the panel.
