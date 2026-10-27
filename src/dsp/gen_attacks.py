#!/usr/bin/env python3
"""Neolithic attack samples for the LA-style architecture (SINGLE + a one-shot PCM attack partial).

Every sound is synthesised here from physical recipes (no recordings, no sampled ROMs), so the set is ours to ship:
  modes(...)        a struck body: decaying sinusoids at the body's mode frequencies (stone, wood, bone, skin)
  band_noise(...)   noise shaped in the frequency domain: breath, sparks, contact grit
  pulse_train(...)  a band-limited buzz with a moving pitch: lips on a horn

Each attack is one short sound that ends in silence (a cosine fade), meant to be layered over a wavetable voice that
carries the sustain, the way the D-50 put a PCM attack in front of a synthesised partial. Pitched attacks name the
note their fundamental sits on (ROOT), so key tracking can transpose them; unpitched ones play at their own rate.

Usage
  python gen_attacks.py                    write attacks.c beside this script (the firmware's tables) and
                                           listening/attacks/: one WAV per attack at its own rate, a reel of
                                           all of them, and LA demos (attack + a Carnac wavetable bank)
  python gen_attacks.py --out DIR          write somewhere else
"""
import argparse
import wave
from pathlib import Path

import numpy as np

import gen_wavetables as gw

SEED_BASE = 7000
PEAK = 0.89                     # -1 dBFS: the attack's LEVEL control scales it down in the voice
FADE = 0.15                     # the last 15 % of every attack fades to zero on a half cosine


def note_hz(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


# ------------------------------------------------------------------ building blocks --- #

def t_axis(sr, dur):
    return np.arange(int(round(sr * dur))) / sr


def modes(sr, dur, freqs, amps, decays, rng, onset=0.0004, detune=0.0):
    """Sum of exponentially decaying sines (a struck body). onset softens the strike (s); detune scatters the
    modes by up to that fraction so two takes of the same stone differ."""
    t = t_axis(sr, dur)
    y = np.zeros_like(t)
    for f, a, d in zip(freqs, amps, decays):
        f = f * (1.0 + detune * rng.uniform(-1.0, 1.0))
        if f >= 0.45 * sr:
            continue
        y += a * np.exp(-t / d) * np.sin(2.0 * np.pi * f * t + rng.uniform(0.0, 2.0 * np.pi))
    return y * (1.0 - np.exp(-t / onset))


def band_noise(sr, n, rng, centre, octaves, lo=20.0, hi=None):
    """White noise shaped by a Gaussian band on a log-frequency axis (centre Hz, width in octaves), normalised."""
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / sr)
    lf = np.log2(np.maximum(f, 1.0) / centre)
    gain = np.exp(-0.5 * (lf / (octaves / 2.0)) ** 2)
    gain[f < lo] = 0.0
    if hi is not None:
        gain[f > hi] = 0.0
    x = np.fft.irfft(spec * gain, n=n)
    return x / (np.abs(x).max() + 1e-12)


def env_ad(t, attack, decay):
    """Linear rise over attack (s), then exponential decay with time constant decay (s)."""
    return np.where(t < attack, t / max(attack, 1e-6), np.exp(-(t - attack) / decay))


def osc(sr, f_of_t, harm_amps, rng):
    """A tone with a moving pitch: f_of_t is an array of Hz; harm_amps[k-1] is harmonic k's level."""
    phase = 2.0 * np.pi * np.cumsum(f_of_t) / sr
    y = np.zeros_like(f_of_t)
    for k, a in enumerate(harm_amps, start=1):
        y += a * np.sin(k * phase + rng.uniform(0.0, 2.0 * np.pi)) * (k * f_of_t < 0.45 * sr)
    return y


def pulse_train(sr, f_of_t, bright_of_t, rng, harmonics=40):
    """Lip buzz: harmonics at 1/k^slope where slope = 2 - bright (0 dark .. 1 brassy), each muted above Nyquist."""
    phase = 2.0 * np.pi * np.cumsum(f_of_t) / sr
    slope = 2.0 - bright_of_t
    y = np.zeros_like(f_of_t)
    for k in range(1, harmonics + 1):
        y += np.sin(k * phase) * k ** -slope * (k * f_of_t < 0.45 * sr)
    return y


def hits(sr, n, times, amps, make):
    """Place copies of make(rng-free index) at times (s) with amps: rattles, rasps, rebounds, crackle."""
    y = np.zeros(n)
    for i, (t0, a) in enumerate(zip(times, amps)):
        g = make(i)
        s = int(t0 * sr)
        if s >= n:
            continue
        m = min(len(g), n - s)
        y[s:s + m] += a * g[:m]
    return y


def finish(x, sr):
    """High-pass at 20 Hz (no DC, no sub rumble), fade the tail to silence, normalise to PEAK."""
    n = len(x)
    spec = np.fft.rfft(x, n=2 * n)                      # zero-padded so the filter does not wrap the tail
    f = np.fft.rfftfreq(2 * n, 1.0 / sr)
    x = np.fft.irfft(spec * (f / np.sqrt(f * f + 20.0 ** 2)), n=2 * n)[:n]
    k = int(n * FADE)
    x[n - k:] *= 0.5 * (1.0 + np.cos(np.pi * np.arange(k) / k))
    return x / (np.abs(x).max() + 1e-12) * PEAK


# ------------------------------------------------------------------------- the set --- #
# Each recipe returns (samples, sample_rate). Pitched ones are tuned so the fundamental sits on their ROOT.

def bone_chiff(rng):
    """Bone flute, tongued: a hard "t" (a 2 ms bright click of air), a bright 3.2 kHz breath that is gone in
    20 ms, a loud overblown octave squeak, and the tone locking on in 10 ms after a quick 60-cent scoop.
    Sharp and short: for leads (Bone Flute, Overtone Rite)."""
    sr, dur, f0 = 32000, 0.11, note_hz(72)
    t = t_axis(sr, dur)
    tongue = band_noise(sr, len(t), rng, 5500, 1.6, lo=1500) * env_ad(t, 0.0003, 0.0018)
    breath = band_noise(sr, len(t), rng, 3200, 1.0) * env_ad(t, 0.0015, 0.016)
    scoop = f0 * 2.0 ** (-0.60 / 12.0 * np.exp(-t / 0.008))
    tone = osc(sr, scoop, [1.0, 0.22, 0.08], rng) * np.clip(t / 0.010, 0, 1) * np.exp(-t / 0.055)
    squeak = osc(sr, scoop * 2.0, [1.0], rng) * env_ad(t, 0.0015, 0.014)
    return tongue * 0.9 + breath * 0.7 + tone * 0.6 + squeak * 0.6, sr


def reed_chiff(rng):
    """A reed pipe, untongued (reeds do not survive, so this is a guess at the oldest flutes' cousins): the breath fades in over 25 ms, wide and dark (1 kHz), pulses at the
    pitch as the jet slowly finds the pipe (60 ms), and keeps blowing under the tone to the end. Soft and airy:
    for pads (Hearth, Votive Choir)."""
    sr, dur, f0 = 32000, 0.13, note_hz(67)
    t = t_axis(sr, dur)
    air = band_noise(sr, len(t), rng, 1000, 2.8)
    lock = np.clip(t / 0.06, 0, 1) ** 1.5
    pulsed = air * (1.0 + 0.9 * lock * np.sin(2.0 * np.pi * f0 * t)) * env_ad(t, 0.025, 0.11)
    tone = osc(sr, np.full_like(t, f0), [1.0, 0.06, 0.12], rng) * lock * np.exp(-t / 0.09)
    return pulsed * 0.85 + tone * 0.45, sr


def horn_blat(rng):
    """Aurochs horn: lips buzzing into a cow horn, pitch rising three semitones onto the note as the buzz
    brightens, a growl of uneven lip cycles in the first 40 ms."""
    sr, dur, f0 = 22050, 0.14, note_hz(48)
    t = t_axis(sr, dur)
    pitch = f0 * 2.0 ** (-3.0 / 12.0 * np.exp(-t / 0.025))
    growl = 1.0 + 0.04 * np.exp(-t / 0.03) * rng.standard_normal(len(t)).cumsum() / np.sqrt(np.arange(1, len(t) + 1))
    bright = 0.2 + 0.8 * (1.0 - np.exp(-t / 0.035))
    buzz = pulse_train(sr, pitch * growl, bright, rng) * env_ad(t, 0.010, 0.08)
    spit = band_noise(sr, len(t), rng, 900, 2.0) * env_ad(t, 0.002, 0.015)
    return buzz + spit * 0.4, sr


def flint_strike(rng):
    """Flint on flint: a hard click, glassy high modes that die in milliseconds, and a spray of sparks."""
    sr, dur = 32000, 0.06
    t = t_axis(sr, dur)
    body = modes(sr, dur, [3150, 4820, 6170, 7930, 9480, 11800], [1.0, 0.8, 0.7, 0.5, 0.4, 0.3],
                 [0.010, 0.008, 0.006, 0.005, 0.004, 0.003], rng, onset=0.00005, detune=0.02)
    sparks = band_noise(sr, len(t), rng, 7000, 1.5, lo=3000) * env_ad(t, 0.0005, 0.018)
    click = np.zeros_like(t); click[:3] = [1.0, -0.6, 0.2]
    return body + sparks * 0.5 + click * 0.9, sr


def stone_knock(rng):
    """Hand stone on a boulder: a dull knock, mid modes scattered like a real rock's, a low thud under it."""
    sr, dur = 22050, 0.09
    t = t_axis(sr, dur)
    body = modes(sr, dur, [610, 1130, 1690, 2420, 3290, 4300], [1.0, 0.7, 0.55, 0.4, 0.25, 0.15],
                 [0.024, 0.017, 0.012, 0.009, 0.007, 0.005], rng, onset=0.0003, detune=0.04)
    thud = modes(sr, dur, [140], [1.0], [0.022], rng, onset=0.001)
    grit = band_noise(sr, len(t), rng, 2500, 2.5) * env_ad(t, 0.0003, 0.004)
    return body + thud * 0.8 + grit * 0.5, sr


def menhir_thud(rng):
    """A maul swung into a standing stone: low, heavy modes (the stone is metres tall) under a short crack."""
    sr, dur = 16000, 0.20
    t = t_axis(sr, dur)
    body = modes(sr, dur, [68, 113, 176, 251, 337, 452, 590], [1.0, 0.8, 0.6, 0.45, 0.35, 0.25, 0.15],
                 [0.075, 0.06, 0.045, 0.035, 0.028, 0.02, 0.015], rng, onset=0.002, detune=0.03)
    crack = band_noise(sr, len(t), rng, 1200, 2.0, hi=6000) * env_ad(t, 0.0005, 0.012)
    return body + crack * 0.6, sr


def lithophone(rng):
    """A ringing rock: a struck slab or cave stalagmite (Palaeolithic cave formations bear percussion marks: natural
    lithophones), a clear pitch over stone's stretched overtones."""
    sr, dur, f0 = 32000, 0.20, note_hz(72)
    t = t_axis(sr, dur)
    ratios = [1.0, 2.31, 4.07, 5.93, 8.62, 11.3]
    body = modes(sr, dur, [f0 * r for r in ratios], [1.0, 0.55, 0.35, 0.22, 0.12, 0.07],
                 [0.12, 0.075, 0.045, 0.03, 0.02, 0.014], rng, onset=0.0002)
    tick = band_noise(sr, len(t), rng, 4000, 2.0) * env_ad(t, 0.0002, 0.002)
    return body + tick * 0.3, sr


def pebble_rattle(rng):
    """A handful of beach pebbles dropped on stone: a dozen tiny clicks, clustered early, each its own pebble."""
    sr, dur = 32000, 0.12
    n = int(sr * dur)
    times = np.sort(np.concatenate([[0.0], rng.exponential(0.024, 13)]))
    amps = np.exp(-times / 0.04) * rng.uniform(0.4, 1.0, len(times))
    amps[0] = 1.0

    def pebble(i):
        f = rng.uniform(2200, 6800)
        return modes(sr, 0.012, [f, f * rng.uniform(1.4, 1.9)], [1.0, 0.5],
                     [rng.uniform(0.002, 0.005), 0.0015], rng, onset=0.00005)
    return hits(sr, n, times, amps, pebble), sr


def wood_block(rng):
    """A hollowed oak block hit with a stick: one clear tuned mode, a wooden second at 2.76x, a dry click."""
    sr, dur, f0 = 32000, 0.09, note_hz(76)
    t = t_axis(sr, dur)
    body = modes(sr, dur, [f0, f0 * 2.76, f0 * 4.35, f0 * 6.1], [1.0, 0.35, 0.15, 0.08],
                 [0.035, 0.015, 0.008, 0.005], rng, onset=0.0002)
    click = band_noise(sr, len(t), rng, 3500, 2.0) * env_ad(t, 0.0002, 0.0025)
    return body + click * 0.6, sr


def log_drum(rng):
    """A slit log drum: a hollowed trunk's tongue struck with a padded beater, round and woody."""
    sr, dur, f0 = 16000, 0.16, note_hz(55)
    t = t_axis(sr, dur)
    body = modes(sr, dur, [f0, f0 * 2.04, f0 * 3.12, f0 * 4.6, f0 * 6.3], [1.0, 0.45, 0.2, 0.1, 0.05],
                 [0.085, 0.045, 0.028, 0.018, 0.01], rng, onset=0.0012)
    thump = band_noise(sr, len(t), rng, 400, 1.5) * env_ad(t, 0.001, 0.010)
    return body + thump * 0.5, sr


def bone_clack(rng):
    """Two rib bones clacked in the hand: a bright clack and the rebound 13 ms later."""
    sr, dur = 32000, 0.06
    n = int(sr * dur)

    def clack(i):
        return modes(sr, 0.05, [1870, 2740, 4110, 5620, 7300], [1.0, 0.8, 0.5, 0.3, 0.2],
                     [0.012, 0.009, 0.006, 0.004, 0.003], rng, onset=0.00008, detune=0.01)
    return hits(sr, n, [0.0, 0.013], [1.0, 0.45], clack), sr


def frame_drum(rng):
    """A hide stretched on a bent-wood hoop: membrane modes (Bessel ratios), the pitch sagging 4 % as the skin
    relaxes after the hit, a palm thump."""
    sr, dur, f0 = 16000, 0.16, note_hz(45)
    t = t_axis(sr, dur)
    ratios = [1.0, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16, 3.50]
    sag = 1.0 + 0.04 * np.exp(-t / 0.03)
    y = np.zeros_like(t)
    for r, a, d in zip(ratios, [1.0, 0.6, 0.45, 0.4, 0.3, 0.25, 0.2, 0.15],
                       [0.09, 0.065, 0.05, 0.045, 0.038, 0.032, 0.027, 0.024]):
        ph = 2.0 * np.pi * np.cumsum(f0 * r * sag) / sr + rng.uniform(0, 2 * np.pi)
        y += a * np.exp(-t / d) * np.sin(ph)
    y *= 1.0 - np.exp(-t / 0.0008)
    palm = band_noise(sr, len(t), rng, 250, 1.5) * env_ad(t, 0.001, 0.015)
    return y + palm * 0.7, sr


def antler_rasp(rng):
    """A notched antler scraped with a bone: a quick ratchet of clicks, speeding up, each a scratch of grit."""
    sr, dur = 32000, 0.14
    n = int(sr * dur)
    times, t0, gap = [], 0.0, 0.012
    while t0 < 0.11:
        times.append(t0)
        t0 += gap * rng.uniform(0.85, 1.15)
        gap *= 0.93
    amps = 0.6 + 0.4 * rng.uniform(size=len(times))

    def notch(i):
        m = int(sr * 0.006)
        g = band_noise(sr, m, rng, rng.uniform(2500, 4000), 1.2)
        return g * np.exp(-np.arange(m) / (sr * 0.0015))
    return hits(sr, n, times, amps, notch), sr


def seed_rattle(rng):
    """A dried gourd of seeds shaken once: a swarm of tiny grains, swelling in then pouring out."""
    sr, dur = 32000, 0.12
    n = int(sr * dur)
    times = np.sort(rng.gamma(2.0, 0.017, 220))
    amps = rng.uniform(0.2, 1.0, len(times))

    def grain(i):
        m = int(sr * 0.002)
        f = rng.uniform(3500, 9000)
        k = np.arange(m) / sr
        return np.sin(2 * np.pi * f * k) * np.exp(-k / 0.0005)
    return hits(sr, n, times, amps, grain), sr


def hearth_crackle(rng):
    """Green wood in the hearth: one loud pop, a few sharp snaps after it, over the low breath of the fire."""
    sr, dur = 32000, 0.13
    n = int(sr * dur)
    times = np.concatenate([[0.0], np.sort(rng.uniform(0.015, 0.10, 6))])
    amps = np.concatenate([[1.0], rng.uniform(0.15, 0.5, 6)])

    def snap(i):
        m = int(sr * 0.008)
        g = band_noise(sr, m, rng, rng.uniform(1500, 5000), 2.5)
        return g * np.exp(-np.arange(m) / (sr * rng.uniform(0.0006, 0.0018)))
    y = hits(sr, n, times, amps, snap)
    t = np.arange(n) / sr
    roar = band_noise(sr, n, rng, 300, 2.0) * env_ad(t, 0.008, 0.05)
    return y + roar * 0.25, sr


def cave_clap(rng):
    """Two hands clapped in a cave mouth: the clap's three quick flesh bursts, then the rock answering
    at 19, 33 and 52 ms, each echo duller."""
    sr, dur = 32000, 0.14
    n = int(sr * dur)

    def burst(i):
        m = int(sr * 0.012)
        g = band_noise(sr, m, rng, 1400, 1.4)
        return g * np.exp(-np.arange(m) / (sr * 0.003))
    clap = hits(sr, n, [0.0, 0.006, 0.011], [0.7, 0.85, 1.0], burst)
    y = clap.copy()
    for delay, gain, centre in [(0.019, 0.45, 1100), (0.033, 0.30, 800), (0.052, 0.18, 600)]:
        s = int(delay * sr)
        echo = np.zeros(n)
        echo[s:] = clap[: n - s]
        spec = np.fft.rfft(echo)
        f = np.fft.rfftfreq(n, 1 / sr)
        echo = np.fft.irfft(spec / np.sqrt(1 + (f / centre) ** 2), n=n)
        y += gain * echo / (np.abs(echo).max() + 1e-12)
    return y, sr


def string_modes(sr, t, f0, pluck_at, decay0, rng, sharp_cents=25.0, gains=None, damp=0.35):
    """A plucked string as its harmonics: level sin(k pi p) / k^2 for a pluck at fraction p of its length (the
    pluck point's notch), each decaying faster the higher it is, the pitch starting sharp (a hard pluck stretches
    the string) and settling. gains(k, fk, t) may shape harmonic k over time (a resonator)."""
    bend = 2.0 ** (sharp_cents / 1200.0 * np.exp(-t / 0.012))
    phase = 2.0 * np.pi * np.cumsum(f0 * bend) / sr
    y = np.zeros_like(t)
    for k in range(1, 40):
        fk = k * f0 * (1.0 + 0.0004 * k * k)              # a gut or sinew string is a little stiff: stretched partials
        if fk >= 0.45 * sr:
            break
        a = abs(np.sin(k * np.pi * pluck_at)) / k ** 1.3
        d = decay0 / (1.0 + damp * (k - 1))
        g = 1.0 if gains is None else gains(k, fk, t)
        y += a * g * np.exp(-t / d) * np.sin(k * phase * (1.0 + 0.0004 * k * k) + rng.uniform(0, 2 * np.pi))
    return y


def bow_pluck(rng):
    """The hunter's bow as an instrument: a sinew string plucked near its end, bright and twanging, over the
    knock of the bow stave. (The musical bow: the San still play their hunting bows; a Trois-Freres engraving
    may show one, ~13,000 BC.)"""
    sr, dur, f0 = 32000, 0.15, note_hz(55)
    t = t_axis(sr, dur)
    string = string_modes(sr, t, f0, 0.12, 0.09, rng, sharp_cents=30.0)
    stave = modes(sr, dur, [310, 720, 1350], [1.0, 0.5, 0.3], [0.015, 0.009, 0.005], rng, onset=0.0002)
    snap = band_noise(sr, len(t), rng, 4500, 1.5, lo=1500) * env_ad(t, 0.0002, 0.0015)
    return string + stave * 0.25 + snap * 0.5, sr


def mouth_bow(rng):
    """The bow's string held against the open mouth: the mouth is its resonator, and opening it sweeps a formant
    up through the harmonics (600 Hz to 1.8 kHz in 70 ms), the twanging "wah" of the mouth bow."""
    sr, dur, f0 = 32000, 0.18, note_hz(50)
    t = t_axis(sr, dur)
    fc = 600.0 * 3.0 ** np.clip(t / 0.07, 0, 1)

    def mouth(k, fk, tt):
        return 0.05 + np.exp(-0.5 * (np.log2(fk / fc) / 0.30) ** 2)
    string = string_modes(sr, t, f0, 0.2, 0.12, rng, sharp_cents=15.0, gains=mouth, damp=0.06)
    snap = band_noise(sr, len(t), rng, 3500, 1.5, lo=1200) * env_ad(t, 0.0002, 0.0015)
    return string + snap * 0.35, sr


def pot_drum(rng):
    """A clay pot drum (Neolithic potters made ceramic drums, their hides long rotted away): a palm on the skin,
    the air in the pot booming (its pitch sagging into the note as the skin settles), the clay shell ringing."""
    sr, dur, f0 = 16000, 0.18, note_hz(43)
    t = t_axis(sr, dur)
    sag = 1.0 + 0.30 * np.exp(-t / 0.025)
    boom = np.sin(2.0 * np.pi * np.cumsum(f0 * sag) / sr) * env_ad(t, 0.002, 0.07)
    shell = modes(sr, dur, [820, 1310, 1960, 2730], [1.0, 0.7, 0.45, 0.3], [0.02, 0.015, 0.01, 0.007], rng,
                  onset=0.0003, detune=0.03)
    palm = band_noise(sr, len(t), rng, 600, 2.0, hi=5000) * env_ad(t, 0.0008, 0.008)
    return boom + shell * 0.3 + palm * 0.5, sr


def conch(rng):
    """A conch blown as a trumpet (the Marsoulas shell, ~18,000 years old, still sounds near C, C# and D): lips
    buzzing into the shell's spiral, a hollow nasal tone with formants at 1.1 and 2.6 kHz, rough with breath."""
    sr, dur, f0 = 22050, 0.15, note_hz(61)
    t = t_axis(sr, dur)
    pitch = f0 * 2.0 ** (-0.7 / 12.0 * np.exp(-t / 0.02))
    phase = 2.0 * np.pi * np.cumsum(pitch) / sr
    y = np.zeros_like(t)
    for k in range(1, 30):
        fk = k * f0
        if fk >= 0.45 * sr:
            break
        form = np.exp(-0.5 * (np.log2(fk / 1100.0) / 0.4) ** 2) + 0.6 * np.exp(-0.5 * (np.log2(fk / 2600.0) / 0.3) ** 2)
        y += (0.25 / k + form) * np.sin(k * phase + rng.uniform(0, 2 * np.pi))
    y *= env_ad(t, 0.018, 0.10)
    rasp = band_noise(sr, len(t), rng, 1800, 1.5) * env_ad(t, 0.004, 0.03)
    return y + rasp * 0.35, sr


# (name on screen, group, recipe, root MIDI note or None for unpitched). New attacks go at the end: patches
# store the attack's number.
ATTACK_DEFS = [
    ("BONE CHIFF", "Breath", bone_chiff, 72),
    ("REED CHIFF", "Breath", reed_chiff, 67),
    ("HORN BLAT", "Breath", horn_blat, 48),
    ("FLINT", "Stone", flint_strike, None),
    ("STONE KNOCK", "Stone", stone_knock, None),
    ("MENHIR THUD", "Stone", menhir_thud, None),
    ("LITHOPHONE", "Stone", lithophone, 72),
    ("PEBBLES", "Stone", pebble_rattle, None),
    ("WOOD BLOCK", "Wood & Bone", wood_block, 76),
    ("LOG DRUM", "Wood & Bone", log_drum, 55),
    ("BONE CLACK", "Wood & Bone", bone_clack, None),
    ("ANTLER RASP", "Wood & Bone", antler_rasp, None),
    ("FRAME DRUM", "Hide & Fire", frame_drum, 45),
    ("SEED RATTLE", "Hide & Fire", seed_rattle, None),
    ("CRACKLE", "Hide & Fire", hearth_crackle, None),
    ("CAVE CLAP", "Hide & Fire", cave_clap, None),
    ("BOW PLUCK", "String", bow_pluck, 55),
    ("MOUTH BOW", "String", mouth_bow, 50),
    ("POT DRUM", "Hide & Fire", pot_drum, 43),
    ("CONCH", "Breath", conch, 61),
]
assert all(len(name) <= 12 for name, *_ in ATTACK_DEFS)


MU = 255.0


def mulaw_encode(x):
    """8-bit mu-law (mu 255): -127..127 (the byte the firmware stores, as int8; -128 is never used)."""
    y = np.sign(x) * np.log1p(MU * np.abs(x)) / np.log1p(MU)
    return np.clip(np.rint(y * 127.0), -127, 127).astype(int)


def mulaw_decode(q):
    q = np.asarray(q, dtype=float) / 127.0
    return np.sign(q) * np.expm1(np.abs(q) * np.log1p(MU)) / MU


def mulaw_roundtrip(x):
    """8-bit mu-law (mu 255) encode then decode: the WAVs play exactly what the firmware tables will hold."""
    return mulaw_decode(mulaw_encode(x))


# The rate each attack is stored at, where it is lower than the rate its recipe draws at (2026-10-27): about 2.2x the
# band holding 99.9 % of its energy, never under 11025. Saves ~21 KB of app flash; the voice reads any rate (its
# AttackInfo rate), so only the bytes change. The bright ones (FLINT, PEBBLES, BONE CLACK, ANTLER RASP, SEED RATTLE,
# CRACKLE) keep 32 kHz; HORN BLAT and CONCH sit higher than their band says, because at that rate the voice's linear
# interpolation images their rasp up as grit (+5..9 dB above the cut; BONE CLACK did so even at 22050).
STORE_SR = {
    "BONE CHIFF": 24000, "REED CHIFF": 27000, "HORN BLAT": 16000, "STONE KNOCK": 11025, "MENHIR THUD": 11025,
    "LITHOPHONE": 11025, "WOOD BLOCK": 15000, "LOG DRUM": 11025,
    "FRAME DRUM": 11025, "CAVE CLAP": 15000, "BOW PLUCK": 16000, "MOUTH BOW": 19000, "POT DRUM": 11025,
    "CONCH": 16000,
}


def resample(x, sr, new_sr):
    """Band-limited rate change of a one-shot: zero-padded FFT (no wrap), a short cosine roll-off below the new
    Nyquist so nothing rings at the cut."""
    n, n2 = len(x), 2 * len(x)
    m2 = int(round(n2 * new_sr / sr))
    spec = np.fft.rfft(x, n=n2)[:m2 // 2 + 1]
    f = np.arange(len(spec)) / (len(spec) - 1)              # 0 .. the new Nyquist
    spec = spec * np.where(f < 0.9, 1.0, 0.5 * (1.0 + np.cos(np.pi * (f - 0.9) / 0.1)))
    return np.fft.irfft(spec, n=m2)[:int(round(n * new_sr / sr))] * (m2 / n2)


def build_all(rerate=True):
    out = []
    for i, (name, group, fn, root) in enumerate(ATTACK_DEFS):
        x, sr = fn(np.random.default_rng(SEED_BASE + i))
        x = finish(np.asarray(x, dtype=float), sr)
        if rerate and STORE_SR.get(name, sr) < sr:
            x, sr = resample(x, sr, STORE_SR[name]), STORE_SR[name]
            x = x / (np.abs(x).max() + 1e-12) * PEAK
        out.append((name, group, root, sr, mulaw_roundtrip(x)))
    return out


# --------------------------------------------------------------------- audition --- #

OUT_SR = 44100


def write_wav(path, x, sr):
    pcm = np.clip(np.rint(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def play_at(x, sr, ratio, n_out):
    """Read x from the start at ratio x (sr / OUT_SR), linear interpolation, as the voice will: silence after."""
    pos = np.arange(n_out) * ratio * sr / OUT_SR
    i = np.floor(pos).astype(int)
    fr = pos - i
    ok = i + 1 < len(x)
    y = np.zeros(n_out)
    y[ok] = x[i[ok]] * (1 - fr[ok]) + x[i[ok] + 1] * fr[ok]
    return y


def wavetable_voice(cycle, hz, n, attack, release_at):
    """One wavetable frame held at hz with a simple AR envelope (the sustain partial of the demo)."""
    ph = (np.arange(n) * hz * len(cycle) / OUT_SR) % len(cycle)
    i = np.floor(ph).astype(int)
    fr = ph - i
    y = cycle[i] * (1 - fr) + cycle[(i + 1) % len(cycle)] * fr
    t = np.arange(n) / OUT_SR
    env = np.clip(t / attack, 0, 1) * np.where(t < release_at, 1.0, np.exp(-(t - release_at) / 0.25))
    return y * env


def bank_cycle(bank_name, scan):
    """Level 0 of a Carnac wavetable bank at scan 0..1, peak 1."""
    for b, (name, fn) in enumerate(gw.BANK_DEFS):
        if name == bank_name:
            c = fn(scan, gw.SEED_BASE + b)
            c = c - c.mean()
            return c / np.abs(c).max()
    raise KeyError(bank_name)


# (file, attack, wavetable bank, scan, notes as MIDI, attack level, sustain fade-in s)
DEMOS = [
    ("la_bone_chiff_bone_flute", "BONE CHIFF", "Bone Flute", 0.35, [72, 76, 79, 84], 0.8, 0.05),
    ("la_reed_chiff_hearth", "REED CHIFF", "Hearth", 0.3, [60, 64, 67, 72], 0.7, 0.06),
    ("la_lithophone_moonstone", "LITHOPHONE", "Moonstone", 0.25, [60, 67, 72, 79], 0.8, 0.03),
    ("la_wood_block_menhir", "WOOD BLOCK", "Menhir", 0.4, [64, 67, 71, 76], 0.9, 0.02),
    ("la_frame_drum_ley_line", "FRAME DRUM", "Ley Line", 0.5, [36, 43, 48], 0.9, 0.04),
    ("la_horn_blat_barrow", "HORN BLAT", "Barrow", 0.6, [41, 48, 53], 0.7, 0.05),
    ("la_stone_knock_hearth", "STONE KNOCK", "Hearth", 0.6, [48, 55, 60, 64], 0.9, 0.03),
    ("la_flint_votive_choir", "FLINT", "Votive Choir", 0.3, [60, 64, 67, 71], 0.8, 0.08),
    ("la_bow_pluck_menhir", "BOW PLUCK", "Menhir", 0.5, [48, 55, 60, 67], 0.9, 0.04),
    ("la_mouth_bow_trilithon", "MOUTH BOW", "Trilithon", 0.4, [45, 50, 52, 57], 0.9, 0.05),
    ("la_pot_drum_ley_line", "POT DRUM", "Ley Line", 0.4, [36, 41, 43], 0.9, 0.04),
    ("la_conch_druid_chant", "CONCH", "Druid Chant", 0.5, [55, 60, 61, 62], 0.7, 0.05),
]


def render_demo(atk, sr, root, cycle, notes, level, fade_in):
    note_len, gap = 0.75, 0.15
    seg = int((note_len + gap + 0.4) * OUT_SR)
    out = np.zeros(int(len(notes) * (note_len + gap) * OUT_SR) + seg)
    for k, m in enumerate(notes):
        ratio = note_hz(m) / note_hz(root) if root is not None else 1.0
        a = play_at(atk, sr, ratio, seg)
        s = wavetable_voice(cycle, note_hz(m), seg, fade_in, note_len)
        start = int(k * (note_len + gap) * OUT_SR)
        out[start:start + seg] += level * a + 0.45 * s
    return out / np.abs(out).max() * PEAK


ENGINE_SR = 44100.0            # the oscillators' pitch reference (oscillators.c): the attacks step against the same


def write_c(c_path, attacks):
    """attacks.c: the samples as mu-law bytes, one table, and what the voice needs to play each (attacks.h)."""
    names = [a[0] for a in attacks]
    data, info, off = [], [], 0
    for name, group, root, sr, x in attacks:
        q = mulaw_encode(x)                               # x already round-tripped: encoding again is exact
        data += [int(v) & 0xFF for v in q]
        info.append((off, len(q), int(round(sr / ENGINE_SR * 65536.0)), 255 if root is None else root, name, sr))
        off += len(q)
    dec = mulaw_decode(np.array([(i if i < 128 else i - 256) for i in range(256)])) * 32768.0
    dec[128] = 0.0                                        # byte 0x80 (-128) is never written
    out = [
        "/* Generated by gen_attacks.py: do not edit. The STRIKE architecture's attack samples (attacks.h): 8-bit mu-law,",
        f" * {len(names)} sounds, {off} bytes. */",
        '#include "attacks.h"',
        "",
        f'_Static_assert(ATK_COUNT == {len(names)}, "attacks.h ATK_COUNT != gen_attacks.py ATTACK_DEFS");',
        "",
        "const char *const attack_names[ATK_COUNT] = {",
    ]
    out += [f'    "{n}",' for n in names]
    out += ["};", "", "/* {offset, length, rate (Q16, samples of the sound per engine sample at 44.1 kHz), root note or ATK_UNPITCHED} */",
            "const AttackInfo attack_info[ATK_COUNT] = {"]
    out += [f"    {{{o}u, {n}u, {r}u, {root}u}},   /* {nm}, {sr} Hz */" for o, n, r, root, nm, sr in info]
    out += ["};", "", "/* mu-law byte -> sample, Q15 units (32768 = full scale) */", "const float attack_mulaw[256] = {"]
    for i in range(0, 256, 8):
        out.append("    " + " ".join(f"{v:.2f}f," for v in dec[i:i + 8]))
    out += ["};", "", f"const uint8_t attack_data[{off}] = {{"]
    for i in range(0, off, 24):
        out.append("    " + ",".join(f"{v}" for v in data[i:i + 24]) + ",")
    out += ["};"]
    c_path.write_text("\n".join(out) + "\n", encoding="ascii")
    return off


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parents[2] / "listening" / "attacks")
    ap.add_argument("--c", type=Path, default=Path(__file__).with_name("attacks.c"), help="the firmware tables")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    attacks = build_all()
    reel, total16 = [], 0
    print(f"{'#':>2} {'name':<12} {'group':<12} {'root':>4} {'rate':>6} {'ms':>4} {"mu-law B":>8}")
    for i, (name, group, root, sr, x) in enumerate(attacks):
        write_wav(args.out / f"{i:02d} {name.title()}.wav", x, sr)
        reel += [play_at(x, sr, 1.0, int(len(x) * OUT_SR / sr) + 1), np.zeros(int(0.45 * OUT_SR))]
        total16 += 2 * len(x)
        rname = "-" if root is None else str(root)
        print(f"{i:2d} {name:<12} {group:<12} {rname:>4} {sr:6d} {1000 * len(x) / sr:4.0f} {len(x):8d}")
    write_wav(args.out / "_reel all attacks.wav", np.concatenate(reel), OUT_SR)
    print(f"total {total16 // 2} B as 8-bit mu-law ({total16 / 2048:.1f} KiB; the WAVs are that, decoded), "
          f"{total16} B if kept int16")

    by_name = {a[0]: a for a in attacks}
    for fname, aname, bank, scan, notes, level, fade_in in DEMOS:
        _, _, root, sr, x = by_name[aname]
        demo = render_demo(x, sr, root, bank_cycle(bank, scan), notes, level, fade_in)
        write_wav(args.out / f"{fname}.wav", demo, OUT_SR)
    nbytes = write_c(args.c, attacks)
    print(f"wrote {args.c} ({nbytes} B of samples)")
    print(f"wrote {len(attacks)} attacks, the reel and {len(DEMOS)} LA demos to {args.out}")


if __name__ == "__main__":
    main()
