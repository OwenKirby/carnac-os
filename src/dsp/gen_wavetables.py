#!/usr/bin/env python3
"""Generate the wavetables for the Mvave FM1 synth.

The bank: "Stone Circle", 14 banks of 8 morph frames x 512 samples, signed 16-bit Q15. Its theme is druidic and
neolithic: standing stones, rituals and weathering. Every bank's morph is one physical or ritual process, frame 1 the
stone before it begins and frame 8 the stone after it ends, so a scan sweep tells that story. Four groups
(docs/wavetables.md has the full design notes):

  Lithic Foundations    0 Menhir  1 Barrow  2 Trilithon  3 Ley Line              deep, heavy, mossy: drones and bass
  Celestial Alignments  4 Solstice  5 Moonstone  6 Star Chart                    bright, metallic, resonant
  Arcane Rituals        7 Druid Chant  8 Overtone Rite  9 Votive Choir  10 Bone Flute   vocal, formant, breath
  Time & Decay          11 Erosion  12 Frost Shatter  13 Dust                    aliased, noisy, fractured

Where they live (spec 6.5). 14 banks x 16 KiB do not fit beside the code in the FM1's 568 KiB app slot, so the
bank data sits in Felucca's store, flash 0xA0000..0xDBFFF (the three 80 KiB user sample slots), as one image: a
32-byte header in a 4 KiB header sector, then the data. The installer only writes the app area, so the image is
uploaded over USB-MIDI (src/hardware/wt_store_upload.py) and read in place through the plain XIP window
(src/hardware/wt_store_fm1.c). The app keeps only the small tables: names, mip offsets, the sub-oscillator's
square, and the sine that is the sub's SINE and what every bank plays while the store holds no valid image.

Method
  Tonal frames are built in the frequency domain: harmonics 1..255 of a 512-sample cycle, inverse-FFT'd. That is
  band-limited and exactly periodic, so there is no loop seam. Harmonic 256 (Nyquist) stays empty. Shapes that are
  easier to draw in time (the trilithon silhouette, CZ-style resonance, FM) are drawn 8x oversampled over one
  cycle and cut to harmonics 1..255, so they are band-limited the same way. The Time & Decay banks that should
  alias (Erosion's bit and rate reduction, Frost Shatter's folding) are drawn at 512 samples and keep what folds
  back: the grit is part of the cycle, and it stays periodic.
  Noise is drawn once per bank (the seed is per bank, not per frame), so a noisy bank morphs smoothly instead of
  reshuffling between frames.
  Each bank uses one gain across its 8 frames, so morphing does not change level.
  The result is DC-free and quantised with TPDF dither.

  Anti-aliasing (mip levels). A 512-sample cycle read at pitch f puts harmonic k at k*f, so
  harmonics above Nyquist fold back. Each frame is stored at WT_LEVELS band-limited levels: level L is
  the same cycle at LEVEL_LEN samples, keeping harmonics up to LEVEL_HARM. Level 0 is
  the full cycle; the oscillator picks the richest level whose top harmonic stays below Nyquist for its
  pitch (oscillators.c, wt_level_for_freq).

Usage
  python gen_wavetables.py                      write, beside this script: wavetables.c (the app's tables),
                                                wavetables_image.bin (the store image, to upload) and
                                                wavetables_image.c (the same image for host and browser builds)
  python gen_wavetables.py --preview DIR        also write one audition WAV per bank
  python gen_wavetables.py --preview DIR --preview-hz 110
                                                audition at another pitch
The noisy banks (Dust, the breath in Bone Flute, Ley Line's moss) loop at the played pitch, so they sound pitched;
they work best low. Preview them at 110 Hz or below.
"""
import argparse
import struct
import wave
import zlib
from pathlib import Path

import numpy as np

N = 512                       # samples per cycle
H = N // 2                    # usable harmonics are 1..H-1; bin H (Nyquist) stays empty
K = np.arange(H + 1, dtype=float)
KI = K.astype(int)
KSAFE = np.maximum(K, 1.0)    # 1/k without dividing by zero at k = 0
FRAMES = 8
PEAK = 0.92                   # full-scale fraction after normalisation: headroom for the voice mix
SINE_PEAK = 4926 / 32767      # wavetable_sine (the sub's SINE, the no-image fallback): about -16 dB, as it always was
OS = 8                        # oversampling for shapes drawn in time
REF_HZ = 110.0                # formant frequencies are placed against this reference pitch
SR = 44100
SEED_BASE = 1000              # bank b uses seed SEED_BASE + b, so every run is identical
DITHER_SEED = 0x46554C        # "FUL"
DITHER_BANKS = 14             # banks 0..13 share the first dither stream; later banks use DITHER_SEED + 1 (main)
WT_LEVELS = 7                 # mip levels, each one cycle: level L has LEVEL_LEN[L] samples
LEVEL_LEN = [512, 256, 128, 64, 32, 16, 16]                     # level 6 is 16 samples so its interpolation stays clean
LEVEL_HARM = [256, 128, 64, 32, 16, 8, 4]                       # the highest harmonic a level keeps (its Nyquist, or 4 at the top)
LEVEL_START = [sum(LEVEL_LEN[:L]) for L in range(WT_LEVELS)]   # 0, 512, 768, ... (wavetables.h)
FRAME_LEN = sum(LEVEL_LEN)                                      # 1024 samples per frame, every level
SINE_PHASE = np.full(H + 1, -np.pi / 2)                         # cos(k x - pi/2) = sin(k x)


# ----------------------------------------------------------------- synthesis --- #

def from_spectrum(spec):
    """Complex harmonic spectrum (index = harmonic number) -> one periodic cycle."""
    s = np.zeros(H + 1, dtype=complex)
    s[1:H] = spec[1:H]
    return np.fft.irfft(s, n=N)


def tonal(amp, phase=None):
    """Real harmonic amplitudes with optional phases."""
    ph = np.zeros(H + 1) if phase is None else phase
    return from_spectrum(amp * np.exp(1j * ph))


def schroeder():
    """Low-crest phases for dense spectra: loud without clipping."""
    return np.pi * K * (K - 1) / (H - 1)


def noise_spec(rng, amp):
    """Random complex harmonics shaped by amp: breath, air, grit."""
    return amp * (rng.standard_normal(H + 1) + 1j * rng.standard_normal(H + 1)) / np.sqrt(2)


def cycle_spec(x):
    """The harmonic spectrum of one cycle drawn at any length: the coefficients from_spectrum turns back into it."""
    return np.fft.rfft(x)[:H + 1] * (N / len(x))


def drawn(fn):
    """A shape drawn in time, fn(phase 0..1), sampled OS x and cut to harmonics 1..255: band-limited, no aliasing."""
    return from_spectrum(cycle_spec(fn(np.arange(N * OS) / (N * OS))))


def aliased(x):
    """A 512-sample cycle kept with what folded back when it was drawn (only DC and Nyquist go)."""
    return from_spectrum(cycle_spec(x))


def ramp(t, a, b):
    """0 below a, 1 above b, linear between."""
    return float(np.clip((t - a) / (b - a), 0.0, 1.0))


def frame_index(t):
    """The frame number 0..7 of a morph position."""
    return int(round(t * (FRAMES - 1)))


# ------------------------------------------------------------------ formants --- #

# Peterson & Barney style average formants (Hz).
VOWELS = {
    "male":   {"a": (730, 1090, 2440), "e": (530, 1840, 2480), "i": (270, 2290, 3010),
               "o": (570, 840, 2410), "u": (300, 870, 2240), "m": (250, 1000, 2200)},
    "female": {"a": (1030, 1370, 2850), "e": (580, 2000, 2800), "i": (320, 2800, 3400),
               "o": (590, 920, 2710), "u": (370, 950, 2670), "m": (290, 1150, 2600)},
}


def vowel_path(voice, path, pos):
    """Formants at pos (0..1) along a path of vowels, e.g. "muoaei"."""
    table = VOWELS[voice]
    x = min(max(pos, 0.0), 1.0) * (len(path) - 1)
    i = min(int(x), len(path) - 2)
    f = x - i
    a, b = table[path[i]], table[path[i + 1]]
    return tuple(p + (q - p) * f for p, q in zip(a, b))


def formant_amp(formants, weights=(1.0, 0.6, 0.3), tilt=0.3):
    """Harmonic amplitudes from three resonances, with harmonic k at k * REF_HZ."""
    hz = K * REF_HZ
    amp = np.zeros(H + 1)
    for f, w in zip(formants, weights):
        bw = 60.0 + 0.08 * f                      # resonances widen with frequency
        amp += w / (1.0 + ((hz - f) / bw) ** 2)
    amp *= KSAFE ** -tilt
    amp[0] = 0.0
    return amp


BREATH_SHAPE = np.where((KI >= 3) & (KI <= 120), 0.3 / np.sqrt(KSAFE), 0.0)


def voiced(formants, seed, breath=0.0, detune=0.0, weights=(1.0, 0.6, 0.3), tilt=0.3):
    """One voiced frame. detune blends in a copy with every formant shifted (a second singer)."""
    rng = np.random.default_rng(seed)
    amp = formant_amp(formants, weights, tilt)
    if detune:
        d = 1.0 + detune
        amp = 0.5 * (amp + formant_amp(tuple(f * d for f in formants), weights, tilt))
    spec = amp * np.exp(1j * schroeder())
    if breath:
        spec = spec + noise_spec(rng, breath * BREATH_SHAPE)
    return from_spectrum(spec)


# ------------------------------------------- Group 1: Lithic Foundations (0-3) --- #

def menhir(t, seed):
    """The standing stone raised from the earth. Frame 1 is a pure sine (the buried stone); each frame lifts it
    higher and uncovers more harmonics, in sine phase so the cycle grows into a saw: the stone's lean. The
    fundamental grows with it, so it stays heavy as it brightens."""
    c = 64.0 * t ** 2                              # how many harmonics the stone has cleared
    amp = np.zeros(H + 1)
    if c > 0.0:
        amp = np.exp(-(K - 1.0) / c) / KSAFE
        amp[0] = 0.0
    amp[1] = 1.0 + 0.5 * t
    return tonal(amp, SINE_PHASE)


def barrow(t, seed):
    """The long barrow, an earth mound over a stone chamber. Odd harmonics only: 1/k^2 in sine phase draws
    rounded mounds, and they sharpen to 1/k (a square) as the barrow is dug open. A resonance (the chamber's boom)
    swells and climbs from harmonic 3 to 7, moss damps the top, and once the chamber is breached (frame 5 on)
    even harmonics seep in."""
    odd = KI % 2 == 1
    amp = np.where(odd, KSAFE ** -(2.0 - t), 0.0)
    amp /= 1.0 + (K / (10.0 + 30.0 * t)) ** 2      # moss and earth
    amp *= 1.0 + 4.0 * t * np.exp(-((K - (3.0 + 4.0 * t)) / 1.2) ** 2)
    amp += np.where(~odd & (KI > 0), 0.3 * ramp(t, 0.5, 1.0) * KSAFE ** -1.5, 0.0)
    return tonal(amp, SINE_PHASE)


def trilithon(t, seed):
    """Two uprights and a lintel. The cycle is drawn as the stones' silhouette: frame 1 is one solid block (a
    square); a gap opens in the block's middle and is cut deeper, until frame 8 is two narrow uprights over a
    trench: a double pulse that hollows the tone toward its octave. A fixed low-pass keeps the edges weathered."""
    g = 0.36 * t                                    # width of the gap between the uprights
    depth = 1.0 - 1.8 * t                           # how far the gap is cut, from flush to below the ground

    def silhouette(ph):
        x = np.where(ph < 0.5, 1.0, -1.0)
        return np.where(np.abs(ph - 0.25) < g / 2, depth, x)

    spec = cycle_spec(silhouette(np.arange(N * OS) / (N * OS)))
    return from_spectrum(spec / (1.0 + (K / 48.0) ** 2))


LEY_STACK = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32]     # octaves and fifths: a drone that never clashes


def ley_line(t, seed):
    """The earth current between the stones: a drone of octaves and fifths. Frame 1 is the root and its octave;
    each frame wakes more of the stack (fifth, double octave, ... the 32nd harmonic), and a low mossy hiss
    rises under it. The phases are fixed per partial, so frames only add, never shuffle."""
    rng = np.random.default_rng(seed)
    phase = rng.uniform(0.0, 2.0 * np.pi, H + 1)
    lit = 2.0 + 8.0 * t
    amp = np.zeros(H + 1)
    for i, k in enumerate(LEY_STACK):
        amp[k] = np.clip(lit - i, 0.0, 1.0) / k ** 0.7
    moss = noise_spec(rng, 0.05 * t * np.where((KI >= 1) & (KI <= 40), 1.0 / KSAFE, 0.0))
    return from_spectrum(amp * np.exp(1j * phase) + moss)


# ------------------------------------------ Group 2: Celestial Alignments (4-6) --- #

def solstice(t, seed):
    """Sunrise through the heel stone. A CZ-style resonance: a sine at r times the pitch, restarted every cycle
    under a falling window, so it is a resonant peak at harmonic r with no seam. r climbs from 1 (almost a sine,
    the sun below the horizon) to 16 (a narrow, ringing beam) exponentially, so each frame is an equal step."""
    r = 16.0 ** t
    return drawn(lambda ph: (1.0 - ph) * np.sin(2.0 * np.pi * r * ph))


def moonstone(t, seed):
    """A struck stone under the moon: an FM bell. The strike is carrier 2 modulated at 7 (ratio 1:3.5, the DX bell)
    over a hum tone at the fundamental, so the bell keeps its pitch. Frame 1 is the hum and a clean strike; the
    index rises to 7 by frame 8, spreading clangorous sidebands (2, 5, 9, 12, 16 ...) like moonlight on wet stone."""
    index = 7.0 * t ** 1.3

    def bell(ph):
        x = 2.0 * np.pi * ph
        return 0.5 * np.sin(x) + np.sin(2.0 * x + index * np.sin(7.0 * x))
    return drawn(bell)


STAR_PRIMES = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89]


def star_chart(t, seed):
    """Dusk to full night over the circle. The fundamental plus prime-numbered harmonics only: primes share no
    pattern, so together they ring metallic, almost inharmonic. Frame 1 lights only the octave; the stars come out
    one by one, low to high, until frame 8 is the whole prime field up to 89, glittering."""
    rng = np.random.default_rng(seed)
    phase = rng.uniform(0.0, 2.0 * np.pi, H + 1)
    lit = 1.0 + (len(STAR_PRIMES) - 1) * t ** 1.2
    amp = np.zeros(H + 1)
    amp[1] = 1.0
    for i, p in enumerate(STAR_PRIMES):
        amp[p] = np.clip(lit - i, 0.0, 1.0) * 0.8 / p ** 0.35
    return tonal(amp, phase)


# ----------------------------------------------- Group 3: Arcane Rituals (7-10) --- #

def druid_chant(t, seed):
    """A male chant, from a closed-mouth hum (m) through u, o, a and e to a bright, keening i: the invocation
    opening up. Formants glide along that path, so any scan position is a vowel between two of them."""
    return voiced(vowel_path("male", "muoaei", t), seed, breath=0.03)


OVERTONE_STEPS = [6, 7, 8, 9, 10, 12, 14, 16]     # the throat singer's melody, harmonic numbers


def overtone_rite(t, seed):
    """Throat singing (khoomei): a low male drone on an 'o' with one harmonic whistling above it. Each frame moves the
    whistle to the next harmonic of a rising melody (6, 7, 8, 9, 10, 12, 14, 16), so a stepped scan plays the
    tune and a smooth one crossfades between overtones, as a real singer does."""
    h = OVERTONE_STEPS[frame_index(t)]
    amp = 0.35 * formant_amp(VOWELS["male"]["o"], tilt=0.4)
    amp += 1.6 * np.exp(-((K - h) / 0.45) ** 2) + 0.25 * np.exp(-((K - h) / 1.5) ** 2)
    return tonal(amp, schroeder())


def votive_choir(t, seed):
    """A procession of women's voices at dawn: two singers a little apart, from a soft 'oo' through 'o' to an open
    'aah'. More voices join (the spread widens) and they exhale more as they go, so frame 8 is open and airy."""
    return voiced(vowel_path("female", "uoa", t), seed, breath=0.06 + 0.3 * t ** 2, detune=0.02 + 0.03 * t)


def bone_flute(t, seed):
    """A neolithic bone flute brought to life. Frame 1 is mostly breath across the hole with the tone just catching;
    the tone firms up to a clean, nearly pure flute by frame 4, then the player overblows: the octave takes over and
    an edgy third harmonic and fresh breath push it to a shriek by frame 8. The breath noise is fixed per bank."""
    rng = np.random.default_rng(seed)
    over = ramp(t, 0.55, 1.0)
    body = np.clip(0.25 + 1.5 * t, 0.0, 1.0)
    amp = np.zeros(H + 1)
    amp[1] = body * (1.0 - 0.55 * over)
    amp[2] = body * (0.1 + 0.9 * over)
    amp[3] = body * (0.05 + 0.3 * over)
    amp[4:12] = body * 0.02 * np.exp(-(K[4:12] - 4.0) / 3.0) * (1.0 + 3.0 * over)
    breath = (0.5 * (1.0 - t) ** 2 + 0.05 + 0.15 * over) * np.where((KI >= 2) & (KI <= 80), 1.0 / np.sqrt(KSAFE), 0.0)
    return from_spectrum(amp + noise_spec(rng, breath))


# ------------------------------------------------- Group 4: Time & Decay (11-13) --- #

EROSION_BITS = [None, 6, 5, 4, 4, 3, 3, 2]        # None: the fresh-cut stone, not quantised
EROSION_HOLD = [1, 2, 4, 4, 8, 8, 16, 32]         # sample-and-hold, in samples of the 512-sample cycle


def erosion(t, seed):
    """Centuries of weather on a carved face. Frame 1 is a fresh, sharp saw, band-limited. Each later frame loses
    bits (6 down to 2) and sample rate (holding 2 to 32 samples), drawn at 512 samples so the steps alias like an
    early sampler's: the stone's grain shows through, until frame 8 is a few blocky terraces."""
    i = frame_index(t)
    x = tonal(np.where(KI > 0, 1.0 / KSAFE, 0.0), SINE_PHASE)
    x = x / np.abs(x).max()
    hold = EROSION_HOLD[i]
    x = x[(np.arange(N) // hold) * hold]
    if EROSION_BITS[i] is not None:
        q = 2 ** (EROSION_BITS[i] - 1)
        x = np.round(x * q) / q
    return aliased(x)


def fold(x):
    """Triangle wavefolder: x itself on -1..1, reflected back at the rails beyond."""
    return 1.0 - 4.0 * np.abs((x + 1.0) / 4.0 % 1.0 - 0.5)


def frost_shatter(t, seed):
    """Freeze-thaw splits the stone. Frame 1 is a smooth sine (unbroken ice); the drive into a wavefolder rises to 8
    and a growing offset makes the folds lopsided (even harmonics: the crack). Drawn at 512 samples, so the fold
    corners alias. From frame 6, shards break off: short runs of the cycle chipped out to silence, more each
    frame."""
    rng = np.random.default_rng(seed)
    shards = [(rng.integers(N), rng.integers(2, 7)) for _ in range(12)]   # drawn once: frames only add more
    ph = np.arange(N) / N
    x = fold((1.0 + 7.0 * t ** 1.5) * np.sin(2.0 * np.pi * ph) + 0.4 * t)
    for pos, length in shards[:int(12 * ramp(t, 0.5, 1.0) ** 2)]:
        run = (pos + np.arange(length)) % N
        x[run] = 0.0
    return aliased(x)


def dust(t, seed):
    """The monolith returns to the earth. Frame 1 is a solid, warm tone (1/k^1.5, sine phase). Frame by frame its
    harmonics are jittered and their phases scattered, the tone thins and a dusty noise rises in its place, until
    frame 8 is only drifting dust: shaped noise with one slow gust per cycle, its peaks worn down by a soft
    saturator. Every random draw is fixed per bank."""
    rng = np.random.default_rng(seed)
    scatter = rng.uniform(-np.pi, np.pi, H + 1)
    jitter = rng.uniform(0.0, 2.0, H + 1)
    noise = noise_spec(rng, np.ones(H + 1))
    tone = np.where((KI > 0) & (KI <= 40), KSAFE ** -1.5, 0.0) * (1.0 + t * (jitter - 1.0))
    tone = from_spectrum(tone * np.exp(1j * (SINE_PHASE + t * scatter)))
    centre = 6.0 + 30.0 * t
    noise = from_spectrum(noise * KSAFE ** -0.5 * (0.15 + np.exp(-0.5 * ((K - centre) / (0.8 * centre)) ** 2)))
    m = t ** 2                                      # how much has turned to dust: equal power, so the level holds
    x = np.sqrt(1.0 - m) * tone / np.std(tone) + np.sqrt(m) * noise / np.std(noise)
    gust = 1.0 + 0.6 * t * np.cos(2.0 * np.pi * np.arange(N) / N)
    return aliased(2.0 * np.tanh(0.5 * x * gust))   # grains: the noise's tall peaks worn down


def hearth(t, seed):
    """The fire at the centre of the roundhouse (the 32-bank set's Warm Drone, unchanged). Frame 1 is banked
    embers: harmonics up to 12, falling as 1/k^1.6. Frame by frame the fire catches: the slope eases to 1/k^0.8
    and the harmonics reach 52. Schroeder phases keep the dense top end smooth, so it sits as a pad."""
    amp = KSAFE ** -(0.8 + 0.8 * (1.0 - t)) * (K <= 12 + 40 * t)
    return tonal(amp, schroeder())


# ------------------------------------------------------------------- build --- #

# Bank-number order. Changing a name changes the set id (wavetables.c), so the firmware refuses an image built
# from another list until the matching one is uploaded. Names go to the screen and the USB info reply (20 chars).
BANK_DEFS = [
    ("Menhir", menhir), ("Barrow", barrow), ("Trilithon", trilithon), ("Ley Line", ley_line),
    ("Solstice", solstice), ("Moonstone", moonstone), ("Star Chart", star_chart),
    ("Druid Chant", druid_chant), ("Overtone Rite", overtone_rite), ("Votive Choir", votive_choir),
    ("Bone Flute", bone_flute),
    ("Erosion", erosion), ("Frost Shatter", frost_shatter), ("Dust", dust),
    ("Hearth", hearth),                     # new banks go at the end: saved patches keep their bank numbers
]
assert all(len(name) <= 20 for name, _ in BANK_DEFS)

# The store image (wavetables.h WT_STORE_*): header sector, then len(BANK_DEFS) x FRAMES x FRAME_LEN int16 little-endian.
STORE_BASE = 0xA0000
STORE_SIZE = 0x48000          # wavetables.h WT_IMAGE_MAX: the image is linked into the app (build_fm1 checks the slot)
STORE_DATA_OFF = 0x1000
STORE_MAGIC = 0x42545746      # "FWTB"
STORE_VERSION = 1


def build_bank(index, fn):
    """Eight frames at one gain: the common peak keeps morph levels consistent."""
    frames = []
    for f in range(FRAMES):
        frame = np.asarray(fn(f / (FRAMES - 1), SEED_BASE + index), dtype=float)
        assert frame.shape == (N,), f"{fn.__name__} returned {frame.shape}"
        frames.append(frame - frame.mean())       # DC-free
    frames = np.stack(frames)
    return frames / np.abs(frames).max() * PEAK


def mip_levels(frames):
    """The band-limited levels of (BANKS, FRAMES, N) frames, one array per level, each (BANKS, FRAMES, LEVEL_LEN[L]).
    A level is the same periodic cycle sampled at LEVEL_LEN[L] points, keeping the cycle's harmonics up to
    LEVEL_HARM[L]: a truncated spectrum, scaled by LEVEL_LEN[L] / N so the sampled values agree with level 0."""
    spec = np.fft.rfft(frames, axis=-1)
    levels = []
    for n, top in zip(LEVEL_LEN, LEVEL_HARM):
        keep = np.zeros(spec.shape[:-1] + (n // 2 + 1,), dtype=complex)
        keep[..., :top + 1] = spec[..., :top + 1] * (n / N)   # DC is already zero; harmonic `top` is kept
        levels.append(np.fft.irfft(keep, n=n, axis=-1))
    return levels


def trimmed_levels(frames):
    """mip_levels, with each level above 0 trimmed to the headroom where truncating harmonics overshoots (Gibbs,
    at sharp edges), per bank so the morph frames stay level with each other. Levels are never boosted."""
    levels = mip_levels(frames)
    for L in range(1, WT_LEVELS):
        peak = np.abs(levels[L]).max(axis=(1, 2), keepdims=True)
        levels[L] = levels[L] * np.minimum(1.0, PEAK / peak)
    return levels


def sub_square():
    """The sub-oscillator's SQUARE: odd harmonics at 1/k in sine phase, high for the first half cycle, at PEAK."""
    amp = np.where(KI % 2 == 1, 1.0 / KSAFE, 0.0)
    cycle = tonal(amp, SINE_PHASE)
    cycle = cycle - cycle.mean()
    return cycle / np.abs(cycle).max() * PEAK


def sub_sine():
    """wavetable_sine: a sine at SINE_PEAK, the level the sub's SINE and the no-image fallback were tuned at."""
    return SINE_PEAK * np.sin(2.0 * np.pi * np.arange(N) / N)


def quantise(frames, rng):
    """Q15 with TPDF dither: +-1 LSB triangular noise decorrelates the rounding error."""
    lsb = frames * 32767.0
    tpdf = rng.uniform(-0.5, 0.5, lsb.shape) + rng.uniform(-0.5, 0.5, lsb.shape)
    return np.clip(np.rint(lsb + tpdf), -32768, 32767).astype(np.int16)


def one_frame(cycle, rng):
    """A single (N,) cycle as one frame at every level, the layout of wavetable_sine and wavetable_sub_square."""
    return np.concatenate([quantise(level, rng) for level in trimmed_levels(cycle[None, None, :])], axis=-1)[0, 0]


def centroid(frame):
    mag = np.abs(np.fft.rfft(frame))[1:H]
    return float((np.arange(1, H) * mag).sum() / max(mag.sum(), 1e-12))


def set_id(names):
    """CRC-32 of the bank names, one per line: the firmware attaches only an image of its own bank list."""
    return zlib.crc32("\n".join(names).encode("ascii")) & 0xFFFFFFFF


def frame_rows(out, frame):
    for row in range(0, FRAME_LEN, 16):
        for level, start in enumerate(LEVEL_START):
            if start == row and level > 0:
                out.append(f"    /* level {level}: {LEVEL_LEN[level]} samples */")
        out.append("    " + ", ".join(str(int(v)) for v in frame[row:row + 16]) + ",")


def write_c(path, names, sine, square):
    """The app-resident part: everything but the bank data."""
    out = [
        "/* Generated by src/dsp/gen_wavetables.py. Do not edit: change the generator and rerun it.",
        " * The bank data is not here: it is the store image (wavetables_image.bin, wavetable_attach.c). */",
        '#include "wavetables.h"',
        "",
        f'_Static_assert(NUM_BANKS == {len(names)}, "wavetables.h NUM_BANKS != gen_wavetables.py BANK_DEFS");',
        "",
        "const uint16_t wavetable_level_start[WT_LEVELS] = {" + ", ".join(str(s) for s in LEVEL_START) + "};",
        "",
        f"const uint32_t wavetable_set_id = 0x{set_id(names):08X}u;   /* CRC-32 of the names, one per line */",
        "",
        "const char *const wavetable_bank_names[NUM_BANKS] = {",
    ]
    out += [f'    "{name}",' for name in names]
    out += [
        "};",
        "",
        f"/* A band-limited sine at every level, laid out as one bank frame (peak ~{int(round(SINE_PEAK * 32767))}, "
        "about -16 dB): the sub-oscillator's",
        " * SINE, and what every bank plays while the store holds no valid image. */",
        "const int16_t wavetable_sine[WT_FRAME_SAMPLES] = {",
    ]
    frame_rows(out, sine)
    out += [
        "};",
        "",
        "/* The sub-oscillator's SQUARE (oscmixer.c): a band-limited square at every level, laid out as one bank frame. */",
        "const int16_t wavetable_sub_square[WT_FRAME_SAMPLES] = {",
    ]
    frame_rows(out, square)
    out.append("};")
    path.write_text("\n".join(out) + "\n", encoding="ascii")


def store_image(table, names):
    """Header sector + data, exactly as it sits in flash at STORE_BASE (wavetables.h wt_store_hdr_t)."""
    data = np.ascontiguousarray(table, dtype="<i2").tobytes()
    assert STORE_DATA_OFF + len(data) <= STORE_SIZE, f"{len(names)} banks do not fit the store"
    hdr = struct.pack("<IHBBHBBIIII", STORE_MAGIC, STORE_VERSION, len(names), FRAMES, FRAME_LEN, WT_LEVELS, 0,
                      STORE_DATA_OFF, len(data), zlib.crc32(data) & 0xFFFFFFFF, set_id(names))
    hdr += struct.pack("<I", zlib.crc32(hdr) & 0xFFFFFFFF)
    assert len(hdr) == 32
    return hdr + b"\xFF" * (STORE_DATA_OFF - len(hdr)) + data


def write_image_c(path, image, names):
    """The wavetable image as a C array, linked into every build (the FM1's too, since 2026-10-16)."""
    out = [
        "/* Generated by src/dsp/gen_wavetables.py. Do not edit: change the generator and rerun it.",
        " * The wavetable image (wavetables_image.bin), linked into every build: wavetable_attach.c starts on it. On the",
        " * FM1 it sits in the firmware (read in place through XIP) since 2026-10-16; before, it was uploaded to the store",
        f" * flash at 0x{STORE_BASE:05X}, which now holds the saved presets.",
        " * Banks: " + ", ".join(f"{b} {n}" for b, n in enumerate(names)) + ". */",
        '#include "wavetables.h"',
        "",
        f"_Alignas(4) const uint8_t wavetable_image_host[{len(image)}] = {{",
        "    " + ", ".join(str(b) for b in image[:32]) + ",",
        f"    [{STORE_DATA_OFF}] =   /* (the rest of the header sector is never read: zero here, erased in flash) */",
    ]
    for i in range(STORE_DATA_OFF, len(image), 32):
        out.append("    " + ",".join(str(b) for b in image[i:i + 32]) + ",")
    out += ["};"]
    path.write_text("\n".join(out) + "\n", encoding="ascii")


def write_wav(path, samples):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(np.asarray(samples, dtype="<i2").tobytes())


def render_preview(bank, hz, seconds=8.0):
    """One bank swept through its 8 frames at hz, linearly interpolated in time and frame."""
    frames = bank.astype(float)
    n = int(SR * seconds)
    sweep = np.arange(n) / n * (FRAMES - 1)
    f0 = np.clip(np.floor(sweep).astype(int), 0, FRAMES - 2)
    ff = sweep - f0
    phase = np.cumsum(np.full(n, hz * N / SR)) % N
    i0 = np.floor(phase).astype(int)
    fr = phase - i0
    i1 = (i0 + 1) % N
    a = frames[f0, i0] * (1 - fr) + frames[f0, i1] * fr
    b = frames[f0 + 1, i0] * (1 - fr) + frames[f0 + 1, i1] * fr
    audio = a * (1 - ff) + b * ff
    return np.clip(np.rint(audio * 0.8), -32768, 32767)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=Path(__file__).with_name("wavetables.c"))
    ap.add_argument("--preview", type=Path, help="folder for one audition WAV per bank")
    ap.add_argument("--preview-hz", type=float, default=110.0)
    args = ap.parse_args()

    names = [name for name, _ in BANK_DEFS]
    frames = np.stack([build_bank(b, fn) for b, (_, fn) in enumerate(BANK_DEFS)])
    # The first DITHER_BANKS banks draw their dither in one stream, as they always have; banks added later draw from
    # their own, so adding a bank leaves every existing table (and the render hashes) bit-identical.
    levels = trimmed_levels(frames)
    rng = np.random.default_rng(DITHER_SEED)
    table = np.concatenate([quantise(level[:DITHER_BANKS], rng) for level in levels], axis=-1)
    square = one_frame(sub_square(), rng)
    sine = one_frame(sub_sine(), rng)
    if len(names) > DITHER_BANKS:
        rng_added = np.random.default_rng(DITHER_SEED + 1)
        added = np.concatenate([quantise(level[DITHER_BANKS:], rng_added) for level in levels], axis=-1)
        table = np.concatenate([table, added], axis=0)

    image = store_image(table, names)
    write_c(args.out, names, sine, square)
    image_bin = args.out.with_name("wavetables_image.bin")
    image_bin.write_bytes(image)
    write_image_c(args.out.with_name("wavetables_image.c"), image, names)
    full = table[:, :, :N]                            # level 0: the full cycle, for the report and previews

    print(f"wrote {args.out} (app tables), {image_bin} ({len(image)} B store image: {len(names)} banks, "
          f"{FRAMES}x{FRAME_LEN} int16 each in {WT_LEVELS} mip levels, set id 0x{set_id(names):08X}) "
          f"and wavetables_image.c")
    for b, name in enumerate(names):
        peak = int(np.abs(full[b]).max())
        rms = [float(np.sqrt(np.mean(full[b, f].astype(float) ** 2))) for f in (0, FRAMES - 1)]
        c0 = centroid(full[b, 0].astype(float))
        c7 = centroid(full[b, FRAMES - 1].astype(float))
        print(f"  {b:2d} {name:<14} peak {peak:5d}  rms {rms[0]:6.0f} -> {rms[1]:6.0f}  "
              f"centroid {c0:6.1f} -> {c7:6.1f}")

    if args.preview:
        args.preview.mkdir(parents=True, exist_ok=True)
        for b, name in enumerate(names):
            write_wav(args.preview / f"{b:02d} {name}.wav", render_preview(full[b], args.preview_hz))
        print(f"previews in {args.preview}")


if __name__ == "__main__":
    main()
