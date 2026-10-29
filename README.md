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

The easy way: open the [web installer](https://owenkirby.github.io/carnac-os/install/) in Chrome or Edge, with
the FM-1 connected by USB.

Carnac OS is free. If you enjoy it, you can [tip me on Ko-fi](https://ko-fi.com/orbvorgis).

From the command line: download the `.fwsc` package from [Releases](../../releases), plug in the FM-1 over USB, then:

```
python x0x-firmware/tools/fm1_install.py carnac-0.9.0.fwsc
python src/hardware/pcm_store_upload.py sync
```

The `.fwsc` holds the firmware only. The PCM waves (CHOIR, MOONSONG, PAN FLUTE...) and the Foundations
wavetable bank live in their own flash areas and are uploaded by the second command, from a checkout of
this repo at the same release tag (`git checkout v0.9.0`; it needs `pip install mido python-rtmidi`). The
web installer does both steps for you. If you install the `.fwsc` with any other tool, the FM-1 starts
with an empty field and no standing stones on its start-up screen, and the words WAVES MISSING (or WAVES
OUT OF DATE): run the web installer or the `sync` command once to fix it. Until then Foundations plays a
sine and the PCM waves are silent.

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
- Website with the web installer (GitHub Pages): `python3 web/site/make_site.py PACKAGE VERSION build/site`,
  tested with `python3 web/site/test_installer.py build/site` (headless Edge or Chrome, simulated FM-1);
  `sh web/site/publish_site.sh VERSION` pushes it to the `gh-pages` branch

The `x0x-firmware` submodule is pinned to the exact X0X commit the release was built with.

## Licence

GPL-3.0-only. See [LICENSE](LICENSE), and [LICENSING.md](LICENSING.md) for where each part comes from and
the credits to [X0X](https://github.com/charlesvestal/fm1-x0x) and
[Felucca](https://github.com/hugelton/Felucca), on whose platform Carnac runs.

Carnac OS is independent firmware, not affiliated with or endorsed by M-VAVE.
