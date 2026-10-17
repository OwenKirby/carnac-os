# Carnac OS

A polyphonic synthesizer firmware for the M-VAVE FM-1, by OrbVorgis.

Carnac OS replaces the FM-1's firmware with a wavetable synth themed on Carnac's standing stones and the
Neolithic: 15 morphing wavetable banks, seven voice architectures (including STRIKE, with 20 synthesised
Neolithic attack sounds), four filter types, three modulation slots per patch, spring, tomb and tank
reverbs, tape and bucket-brigade delays, a step sequencer with scales, an arpeggiator, two layers with
splits and zones, and presets saved in the instrument's flash. It runs on both of the FM-1's cores.

> **Carnac OS is in beta (0.9.x).** Installing custom firmware is at your own risk. M-VAVE's own
> firmware can always be put back (see *Recovery* below).

## Install

Download the `.fwsc` package from [Releases](../../releases), plug in the FM-1 over USB, then:

```
python x0x-firmware/tools/fm1_install.py carnac-0.9.0.fwsc
```

Saved presets live in their own flash area, so they survive firmware updates.

## Recovery

- A build that crashes twice in a row at start-up boots into safe mode, with USB up, so you can install
  again.
- To put M-VAVE's stock firmware back on a unit that doesn't answer USB, see
  [docs/fm1-recovery.md](docs/fm1-recovery.md) (hold OCT- and OCT+ together for 5 s to enter the chip's
  update mode).

## Build from source

```
git clone --recursive https://github.com/<you>/carnac-os.git
```

- Host tests (gcc and Python with Pillow): `sh src/host/run_tests.sh`
- Firmware (Linux / WSL, with the JieLi toolchain and AC79 SDK described in
  [x0x-firmware/BUILDING.md](x0x-firmware/BUILDING.md); X0X's tree needs LF line endings):
  `python3 src/hardware/build_fm1.py --x0x x0x-firmware --release 0.9.0`
- Browser emulator (Emscripten): `sh web/emu/build.sh`

The `x0x-firmware` submodule is pinned to the exact X0X commit the release was built with.

## Licence

GPL-3.0-only. See [LICENSE](LICENSE), and [LICENSING.md](LICENSING.md) for where each part comes from and
the credits to [X0X](https://github.com/charlesvestal/fm1-x0x) and
[Felucca](https://github.com/hugelton/Felucca), on whose platform Carnac runs.

Carnac OS is independent firmware, not affiliated with or endorsed by M-VAVE.
