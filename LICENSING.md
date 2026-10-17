# Carnac OS licensing

Carnac OS is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text
in `LICENSE`). Copyright (C) 2026 OrbVorgis.

Carnac runs on the platform of [X0X](https://github.com/charlesvestal/fm1-x0x), which is GPL-3.0-only and
itself a fork of [Felucca](https://github.com/hugelton/Felucca). Firmware built from this tree is a derived
work of both, so it keeps their licence: if you distribute Carnac OS, or firmware derived from it, you must
give your recipients its complete corresponding source under the same licence.

Selling Carnac OS is allowed by the GPL. Buyers receive the same rights as everyone else: the source, and
the freedom to change and share it.

## Where the code comes from

| What | Origin | Licence |
| --- | --- | --- |
| `x0x-firmware/` (git submodule, pinned): boot, loader, HAL, USB, LCD, flash, packaging and web installer | [X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, a fork of [Felucca](https://github.com/hugelton/Felucca), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments | GPL-3.0-only; see `x0x-firmware/LICENSING.md` for its own third-party parts |
| `src/hardware/synth_main_fm1.c`: boot, safe mode, crash screen, audio ISR and main loop | adapted from X0X's `firmware/src/app/main_fm1.c`, itself adapted from Felucca's `main.c` / `audio.c` | GPL-3.0-only |
| Everything else in `src/`, `web/`, `docs/`: the synth engine, effects, sequencer, arpeggiator, user interface, screens, wavetables, attack samples and factory presets | Carnac OS (OrbVorgis) | GPL-3.0-only |

The wavetables (`src/dsp/gen_wavetables.py`) and the attack samples (`src/dsp/gen_attacks.py`) are
synthesised by those scripts; no sampled or third-party audio is in this tree or in the firmware.

## Third-party material used at build time

| What | Licence | Where |
| --- | --- | --- |
| Barlow Semi Condensed (The Barlow Project Authors) and Terminus (Dimitar Toshkov Zhekov) fonts, from X0X's tree | SIL OFL 1.1 | `x0x-firmware/assets/fonts/` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`, read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Trademarks

"Carnac OS" and the Carnac start-up artwork identify this firmware and its author; please give a modified
version a different name. "X0X" is Charles Vestal's project; "Felucca" and "Hügelton Instruments" are names
of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their respective owners. Roland and other
instrument names are trademarks of their owners, used only to describe what a feature is inspired by.
Carnac OS is independent firmware; it is not affiliated with, endorsed by or supported by any of them.

## Radio

Carnac OS never enables the Bluetooth / Wi-Fi radio of the hardware.
