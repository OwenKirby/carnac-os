#!/usr/bin/env python3
"""Generate "Foundations": the analog oscillator's own waves as one wavetable bank, drawn from the circuit.

The shapes are the Roland SH-2's (service notes, 1979): VCO-1 TRIANGLE / SAW / SQUARE / PULSE with PULSE WIDTH
50 % .. MIN, and the two VCOs mixed. Frames, in SCAN order:

  1 TRIANGLE   2 SAW   3 SQUARE   4 PULSE 35 %   5 PULSE 20 %   6 PULSE 10 %   7 PULSE MIN   8 SAW + SQUARE

Every frame comes out of one simulated saw core, the way the VCO makes them, so the waves keep their analog
phase relations and SCAN crossfades never cancel:
  core     a capacitor charged by the exponential converter's current: the ramp bends slightly (transistor
           Early effect); a reset that takes a few microseconds, undershoots and recovers.
  saw      the core, buffered.
  pulse    a comparator on the core: high from the PULSE WIDTH threshold until the reset; the switch rises
           slower than it falls; SQUARE sits at 49 %, not 50 (part tolerance: a trace of even harmonics).
  triangle the core full-wave rectified: the diodes round the valley, the reset leaves a small notch at the
           peak, the two halves differ by a few percent.
  out      a 4558 op-amp: slew limit (1.7 V/us on a 10 V swing) and a mild roll-off; the output coupling
           capacitor tilts the flat tops; a hint of output-stage asymmetry. SAW + SQUARE goes through the
           mixer's soft saturation.
Edge times are set at EDGE_HZ (a wavetable holds one shape for every pitch; a real VCO's edges take the same
microseconds at every pitch, above the audio band, so they are drawn at the lowest pitch) and the coupling tilt
at COUPLE_HZ, the bass register where it shows. The analog character lives in the low harmonics: the bent ramp,
the reset dip, the tilt, the duty and half mismatches.

What a table cannot hold, and where Carnac gets it instead: pitch drift and tolerance (the ANALOG parameter),
noise (the noise generator), the sub-oscillator (SUB). SCAN between pulse frames crossfades widths rather than
moving the edge, so four frames go to pulses to keep the steps small.

The bank is drawn at 8x per cycle in time and cut to harmonics 1..255 like every other bank (gen_wavetables.py),
in the same mip levels and quantisation. It is bank 0 (gen_wavetables.py STORE_BANKS): the app slot has no room, so
it is an image of its own (wavetables.h WT_BANK_*) in the store flash, uploaded over USB-MIDI:

  python src/hardware/pcm_store_upload.py upload src/dsp/foundations_bank.bin --area 2

Usage
  python gen_foundations.py [--out DIR]      writes src/dsp/foundations_bank.bin (the image to upload),
                                             src/host/foundations_image_host.c (the same, for host and browser
                                             builds) and, in DIR (default listening/foundations), the WAVs
"""
import struct
import zlib
import argparse
from pathlib import Path

import numpy as np

import gen_wavetables as gw

NAME = "Foundations"
FINE = gw.N * gw.OS            # simulation points per cycle
EDGE_HZ = 55.0                 # pitch at which the circuit's microsecond edges are drawn: the bottom of the range,
                               # so they stay above the audio band as on the real VCO (at 440 the saw lost 8.6 dB
                               # at harmonic 128 and every lower note sounded dull)
COUPLE_HZ = 110.0              # pitch at which the coupling capacitor's tilt is drawn
DITHER_SLOT = 16               # its dither stream: the one gen_wavetables.py's rule gives a 17th image bank
BANK_MAGIC, BANK_VERSION = 0x42575243, 1   # wavetables.h WT_BANK_MAGIC "CRWB", WT_BANK_VERSION
PH = np.arange(FINE) / FINE


def us(t):
    """Microseconds -> fraction of a cycle at EDGE_HZ."""
    return t * 1e-6 * EDGE_HZ


def last_cycle(step, x, cycles):
    """Run a stateful filter over x repeated until it settles; the final cycle (periodic steady state)."""
    y = step(np.tile(x, cycles))
    return y[-len(x):]


# ----------------------------------------------------------------- circuit --- #

RESET = us(3.0)                # capacitor discharge
UNDERSHOOT = 0.035             # the reset overshoots below the ramp's start ...
RECOVER = us(2.5)              # ... and the ramp climbs out of it
BEND = 0.22                    # ramp curvature


def saw_core():
    """The integrator's voltage 0..1 over one cycle: ramp, then reset."""
    up = 1.0 - RESET
    r = np.minimum(PH / up, 1.0)
    v = (1.0 - np.exp(-BEND * r)) / (1.0 - np.exp(-BEND))
    fall = np.clip((PH - up) / RESET, 0.0, 1.0)
    fall = fall * fall * (3.0 - 2.0 * fall)                 # the discharge: fast in the middle, eased at both ends
    v = np.where(PH < up, v, 1.0 - (1.0 + UNDERSHOOT) * fall)
    return v - UNDERSHOOT * np.exp(-PH / RECOVER) * (PH < up)


def switch(x, rise_us=1.4, fall_us=0.5):
    """A transistor switch's output: one pole, slower going up than coming down."""
    dt = 1.0 / (FINE * EDGE_HZ)
    a_up = 1.0 - np.exp(-dt / (rise_us * 1e-6))
    a_dn = 1.0 - np.exp(-dt / (fall_us * 1e-6))

    def run(sig):
        out = np.empty_like(sig)
        y = sig[0]
        for i, s in enumerate(sig):
            y += (s - y) * (a_up if s > y else a_dn)
            out[i] = y
        return out
    return last_cycle(run, x, 2)


def opamp(x, slew_v_us=1.7, swing_v=10.0, pole_khz=28.0):
    """The 4558 buffer: slew limit on the +-1 signal (2 units = swing_v), then a one-pole roll-off."""
    dt = 1.0 / (FINE * EDGE_HZ)
    step_max = slew_v_us * 1e6 * dt * 2.0 / swing_v
    a = 1.0 - np.exp(-2.0 * np.pi * pole_khz * 1e3 * dt)

    def run(sig):
        out = np.empty_like(sig)
        s = sig[0]
        y = sig[0]
        for i, v in enumerate(sig):
            s += min(max(v - s, -step_max), step_max)
            y += (s - y) * a
            out[i] = y
        return out
    return last_cycle(run, x, 2)


def coupling(x, fc_hz=5.0):
    """The output capacitor: a one-pole high-pass, drawn at COUPLE_HZ, in its periodic steady state."""
    dt = 1.0 / (FINE * COUPLE_HZ)
    rc = 1.0 / (2.0 * np.pi * fc_hz)
    a = rc / (rc + dt)
    x = x - x.mean()
    # steady state of y[n] = a (y[n-1] + x[n] - x[n-1]) over a periodic input: solve for the start value
    out = np.empty_like(x)
    y, prev = 0.0, x[-1]
    for i, v in enumerate(x):
        y = a * (y + v - prev)
        prev = v
        out[i] = y
    y_end = out[-1]
    decay = a ** np.arange(1, FINE + 1)
    y0 = y_end / (1.0 - a ** FINE)            # the start value that the cycle returns to
    return out + y0 * decay


def output_stage(x):
    """The rest of the path: a hint of asymmetry, the op-amp, the coupling capacitor."""
    return coupling(opamp(x + 0.025 * x * x))


def threshold_for(width):
    """The core voltage at which the comparator must switch so the pulse is high for `width` of the cycle."""
    core = saw_core()
    return np.interp(1.0 - width, PH, np.maximum.accumulate(core))


def pulse_raw(width):
    """The comparator: high while the core is above the threshold (the reset ends it)."""
    core = saw_core()
    return switch(np.where(core > threshold_for(width), 1.0, -1.0))


def saw_raw():
    return 2.0 * saw_core() - 1.0


def triangle_raw():
    """Full-wave rectified core: diode rounding at the valley, the halves a few percent apart, inverted to sit
    like the panel's symbol."""
    s = 2.0 * saw_core() - 1.0
    gain = np.where(s > 0.0, 1.0, 0.965)
    tri = np.sqrt((s * gain) ** 2 + 0.035 ** 2)
    return 1.0 - 2.0 * (tri - tri.min()) / (tri.max() - tri.min())


def mixer(x, drive=1.25):
    return np.tanh(drive * x) / np.tanh(drive)


SQUARE_WIDTH = 0.49
PULSE_MIN = 0.045
FRAME_DEFS = [
    ("TRIANGLE", lambda: output_stage(triangle_raw())),
    ("SAW", lambda: output_stage(saw_raw())),
    ("SQUARE", lambda: output_stage(pulse_raw(SQUARE_WIDTH))),
    ("PULSE 35", lambda: output_stage(pulse_raw(0.35))),
    ("PULSE 20", lambda: output_stage(pulse_raw(0.20))),
    ("PULSE 10", lambda: output_stage(pulse_raw(0.10))),
    ("PULSE MIN", lambda: output_stage(pulse_raw(PULSE_MIN))),
    ("SAW+SQUARE", lambda: output_stage(mixer(0.55 * saw_raw() + 0.55 * pulse_raw(SQUARE_WIDTH)))),
]

# The textbook shapes, for the A/B files only.
IDEAL = {
    "TRIANGLE": lambda p: 1.0 - 4.0 * np.abs(p - 0.5),
    "SAW": lambda p: 2.0 * p - 1.0,
    "SQUARE": lambda p: np.where(p >= 0.5, 1.0, -1.0),
    "PULSE 10": lambda p: np.where(p >= 0.9, 1.0, -1.0),
}


def band_limit(fine):
    """A cycle drawn at FINE points -> 512 samples with harmonics 1..255, as gen_wavetables.drawn does."""
    return gw.from_spectrum(gw.cycle_spec(fine))


# ------------------------------------------------------------------- build --- #

def build():
    frames = np.stack([band_limit(fn()) for _, fn in FRAME_DEFS])
    frames = frames - frames.mean(axis=1, keepdims=True)
    return frames / np.abs(frames).max() * gw.PEAK              # one gain for the bank, like build_bank


def quantised_levels(frames):
    """(8, 1024) int16: each frame's mip levels one after another, the store image's layout."""
    levels = gw.trimmed_levels(frames[None])
    rng = np.random.default_rng(gw.DITHER_SEED + 1 + (DITHER_SLOT - gw.DITHER_BANKS))
    return np.concatenate([gw.quantise(level, rng) for level in levels], axis=-1)[0]


def bank_image(table):
    """wavetables.h's bank image: wt_bank_hdr_t, then the data."""
    data = np.ascontiguousarray(table, dtype="<i2").tobytes()
    hdr = struct.pack("<IHHIIIII", BANK_MAGIC, BANK_VERSION, 1, 32 + len(data), zlib.crc32(data),
                      gw.set_id([NAME]), gw.FRAMES, gw.FRAME_LEN)
    hdr += struct.pack("<I", zlib.crc32(hdr))
    assert len(hdr) == 32 and gw.STORE_BANKS[0] == NAME
    return hdr + data


def write_host_c(path, image):
    out = ["/* Generated by src/dsp/gen_foundations.py: do not edit. Bank 0's image (wavetables.h WT_BANK_*), for host",
           " * and browser builds only: the FM1 reads it from the store flash it was uploaded to (pcm_store_fm1.c). */",
           "#include <stdint.h>", "",
           f"const uint32_t foundations_image_host_len = {len(image)}u;",
           f"const uint8_t foundations_image_host[{len(image)}] __attribute__((aligned(4))) = {{"]
    out += ["    " + ",".join(str(b) for b in image[i:i + 32]) + "," for i in range(0, len(image), 32)]
    out.append("};")
    path.write_bytes(("\n".join(out) + "\n").encode("ascii"))


def read(table, frame_pos, hz):
    """Play table (8, 1024) like the oscillator: the mip level for the pitch, linear in phase and frame."""
    n = len(frame_pos)
    hz = np.broadcast_to(np.asarray(hz, dtype=float), (n,))
    out = np.zeros(n)
    phase = np.cumsum(hz / gw.SR) % 1.0
    f0 = np.clip(np.floor(frame_pos).astype(int), 0, gw.FRAMES - 2)
    ff = frame_pos - f0
    levels = np.full(n, gw.WT_LEVELS - 1)
    for L in reversed(range(gw.WT_LEVELS - 1)):
        levels[gw.LEVEL_HARM[L] * hz < gw.SR / 2] = L
    for L in range(gw.WT_LEVELS):
        sel = levels == L
        if not sel.any():
            continue
        ln, st = gw.LEVEL_LEN[L], gw.LEVEL_START[L]
        x = phase[sel] * ln
        i0 = np.floor(x).astype(int) % ln
        fr = x - np.floor(x)
        i1 = (i0 + 1) % ln
        lv = table[:, st:st + ln].astype(float)
        a = lv[f0[sel], i0] * (1 - fr) + lv[f0[sel], i1] * fr
        b = lv[f0[sel] + 1, i0] * (1 - fr) + lv[f0[sel] + 1, i1] * fr
        out[sel] = a * (1 - ff[sel]) + b * ff[sel]
    return out


def env(n, fade=0.01):
    k = int(gw.SR * fade)
    e = np.ones(n)
    e[:k] = np.linspace(0, 1, k)
    e[-k:] = np.linspace(1, 0, k)
    return e


def held(table, frame, hz, seconds):
    n = int(gw.SR * seconds)
    return read(table, np.full(n, float(frame)), hz) * env(n)


def out_wav(path, x, gain=0.7):
    gw.write_wav(path, np.clip(np.rint(x * gain), -32768, 32767))


def ideal_table(fn, match):
    """The textbook shape at the RMS of `match` (the Foundations frame it is compared with): louder sounds
    brighter, so the A/B is level-matched."""
    one = band_limit(fn(np.arange(FINE) / FINE))
    one = one / np.sqrt(np.mean(one ** 2)) * np.sqrt(np.mean(match ** 2))
    frames = np.repeat(one[None], gw.FRAMES, axis=0)
    return quantised_levels(frames)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    root = Path(__file__).resolve().parents[2]
    ap.add_argument("--out", type=Path, default=root / "listening" / "foundations")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    frames = build()
    table = quantised_levels(frames)
    image = bank_image(table)
    (root / "src" / "dsp" / "foundations_bank.bin").write_bytes(image)
    write_host_c(root / "src" / "host" / "foundations_image_host.c", image)
    names = [name for name, _ in FRAME_DEFS]

    gap = np.zeros(int(gw.SR * 0.15))
    for hz, tag in ((55.0, "A1"), (110.0, "A2"), (440.0, "A4")):
        out_wav(args.out / f"01_frames_{tag}.wav", np.concatenate([np.concatenate([held(table, f, hz, 1.6), gap])
                                                                  for f in range(gw.FRAMES)]))
    n = int(gw.SR * 10)
    out_wav(args.out / "02_scan_sweep_A2.wav", read(table, np.linspace(0, gw.FRAMES - 1, n), 110.0) * env(n))
    t = np.arange(n) / gw.SR
    pwm = 2.0 + 4.0 * (0.5 - 0.5 * np.cos(2 * np.pi * 0.4 * t))          # SQUARE .. PULSE MIN and back
    out_wav(args.out / "03_pwm_scan_lfo_A2.wav", read(table, pwm, 110.0) * env(n))

    for name, fn in IDEAL.items():
        i = names.index(name)
        ideal = ideal_table(fn, frames[i])
        tag = name.replace(" ", "").lower()
        out_wav(args.out / f"04_ab_{tag}_ideal_then_foundations.wav",
                np.concatenate([held(ideal, 0, 110.0, 2.5), gap, gap, held(table, i, 110.0, 2.5)]))

    # Two saws with the kind of drift and tolerance the ANALOG parameter adds (a sketch of it, not its code).
    rng = np.random.default_rng(7)
    walk = lambda: np.convolve(rng.standard_normal(n), np.ones(8000) / 8000, "same") * 60.0   # slow, +-few cents
    cents1, cents2 = 3.0 + walk(), -3.0 + walk()
    saws = (read(table, np.full(n, 1.0), 110.0 * 2 ** (cents1 / 1200)) +
            read(table, np.full(n, 1.0), 110.0 * 2 ** (cents2 / 1200))) * 0.5
    out_wav(args.out / "05_two_saws_drifting_A2.wav", saws * env(n))

    print(f"wrote src/dsp/foundations_bank.bin ({len(image)} B: {gw.FRAMES} frames x {gw.FRAME_LEN} int16), "
          f"src/host/foundations_image_host.c and the WAVs in {args.out}")
    for f, name in enumerate(names):
        mag = np.abs(np.fft.rfft(frames[f]))[1:9]
        rel = 20 * np.log10(np.maximum(mag / mag[0], 1e-9))
        rms = np.sqrt(np.mean(frames[f] ** 2))
        print(f"  {f + 1} {name:<11} rms {20 * np.log10(rms):6.1f} dBFS  H2..H8 dB " +
              " ".join(f"{v:6.1f}" for v in rel[1:]))


if __name__ == "__main__":
    main()
