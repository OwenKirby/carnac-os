#!/usr/bin/env python3
"""Carnac's PCM waves: the samples and wave table of the PCM store (src/dsp/pcm_store.h, version 2), and auditions.

The oscillator's wave list runs: the wavetable banks, then these PCM waves, then the 20 firmware attacks as one-shots
(src/dsp/pcm_osc.h). A PCM wave is an optional attack on top of loop A, and an optional loop B that SCAN crossfades
to; the oscillator reads at most two samples at once (during the attack: the attack and the nearer loop), the same as
a wavetable's two frames, so a PCM wave costs no voices.

  CHIMES     WOOD: a hand brushing the chimes (the attack), then the wind (the loop). B is the same
             loop again, read 6 % faster from another point: SCAN 0 the calm loop, 1 twice the strikes (density).
  LORE       small cane chimes after listening/inspiration/lore_inspo.wav: SCAN 0 stirring, 1 a gust (twice the
             knocks). It took SHELL CHIMES' place, wave 66.
  RAINSTICK  (CLAY CHIMES' place, 65) a trickle of seeds -> the stick upended; SEED RATTLE in front.
  SINGING STN a wet stone slab rubbed (the 2026-10-26 prototype) -> rubbed hard; LITHOPHONE in front.
  CAIRN SPIRIT (73) a 90s spirit pad: dark choir + pan flutes on a stone drone -> bright choir, halo, glass shimmer.
  PAN FLUTE  a stopped pipe blown softly, breathy -> hard, the odd harmonics up; BONE CHIFF
             in front.
  ENSEMBLES  CHOIR (a breath, then a hummed "ooh" -> a ringing open "aah") and HORNS (the firmware's HORN BLAT,
             then a gentle round section -> overblown brassy). A and B are the same players with the same vibrato
             and drift, only their tone differs, so the crossfade is one section changing how it plays.
  MOONSONG   (STRINGS' place, 67; STRINGS removed 2026-10-27) voices humming in a passage tomb on A2, the chamber's
             note -> overtone singers whistling a chord of harmonics above the hum, starlight twinkling over it.
  CHOIR+WOOD the choir's "aah" crossfading to the wood chimes: Universe on one oscillator (SCAN = the balance).

Every sound is synthesised here, with the recipes of the STRIKE attacks (gen_attacks.py), so the sets sound like one
family and are ours to ship. The loops cannot click: each is exactly periodic. A chime loop is drawn on a circle (a
ring running past the end carries on from the start); an ensemble loop is a whole number of samples and of the tone's
cycles, and every frequency in it (partials, vibrato, chorus, the bow and breath noise, the drift) is a whole number
of cycles in it. Each loop's wrap is checked and printed.

Pitch: a sample plays at its rate at its ROOT; higher notes play it faster. Sample playback has no band-limited
levels, so the ensembles are cut at 5 kHz (two octaves up stays below the engine's Nyquist); the chimes keep their
top, which aliases a little at the highest notes, as a JD-800 or an M1 does.

Usage
  python gen_pcm_waves.py                  the store images (pcm_area0.bin, pcm_area1.bin, beside this script, and
                                           src/host/pcm_images_host.c for the host builds), the firmware's own area
                                           (pcm_area_fw.c, beside this script) and listening/pcm/
  python gen_pcm_waves.py --images-only    the images, no WAVs
  python gen_pcm_waves.py --out DIR        the WAVs somewhere else
"""
import argparse
import struct
import zlib
from pathlib import Path

import numpy as np

import gen_attacks as ga

SR = 44100                      # the engine's rate (oscillators.c), and the WAVs'
PEAK = 0.89                     # -1 dBFS, as the attacks
SEED_BASE = 9000
ENS_TOP = 5000.0                # ensembles: no partial above this, at the root
note_hz = ga.note_hz

# the store (pcm_store.h)
MAGIC, VERSION = 0x57505243, 2
HDR_SIZE, ENTRY_SIZE, NAME_LEN, MAX_ENTRIES = 32, 36, 12, 63
AREAS = (0x9000, 0xE000, 0xA000)    # area 0: 36 KiB, area 1: 56 KiB of store flash (pcm_store_fm1.c), uploaded; area
                                    # 2 (2026-10-27): in the firmware itself (pcm_area_fw.c), at most 40 KiB of the app
FW_AREA = 2
K_LOOP, K_ONESHOT, K_WAVE = 1, 2, 3
REF_NONE = 0xFF
FW_ATTACKS = [a[0] for a in ga.ATTACK_DEFS]


# ------------------------------------------------------------------ building blocks --- #

def loop_len(sr, f0, approx_s):
    """A loop of whole cycles in whole samples: N samples holding k cycles of f = k * sr / N (within a fraction of a
    cent of f0). Returns N and that f; every other frequency in the loop is snapped to sr / N."""
    k = max(1, int(round(f0 * approx_s)))
    n = int(round(k * sr / f0))
    return n, k * sr / n


def wander(n, rng, harmonics=4):
    """A smooth random wobble, exactly periodic over n samples (harmonics 1.. of 1 / n), peak 1."""
    t = np.arange(n) / n
    y = np.zeros(n)
    for h in range(1, harmonics + 1):
        y += rng.standard_normal() / h * np.sin(2.0 * np.pi * h * t + rng.uniform(0, 2 * np.pi))
    return y / (np.abs(y).max() + 1e-12)


def formant(f, bands, floor=0.02):
    """Resonance bumps: sum of g / sqrt(1 + ((f - F) / B)^2) over (F, B, g)."""
    y = np.full_like(np.asarray(f, dtype=float), floor)
    for F, B, g in bands:
        y = y + g / np.sqrt(1.0 + ((f - F) / B) ** 2)
    return y


def peak_norm(x):
    return x / (np.abs(x).max() + 1e-12) * PEAK


def fade_tail(x, frac=0.25):
    """A one-shot ends in silence: the last frac fades on a half cosine."""
    k = max(2, int(len(x) * frac))
    x = x.copy()
    x[-k:] *= 0.5 * (1.0 + np.cos(np.pi * np.arange(k) / k))
    return x


# ------------------------------------------------------------------------ chimes --- #

def circular_hits(n, sr, events):
    """Hits on a circle n samples round: a tail past the end carries on from the start (exactly periodic)."""
    out = np.zeros(n)
    for onset, amp, snd in events:
        idx = (int(onset * sr) + np.arange(len(snd))) % n
        np.add.at(out, idx, amp * snd)
    return out


def gusts(L, rng, rate, calm=0.65):
    """Strike times over a loop of L s: a Poisson stream whose rate follows a gentle periodic gust (in a loop under
    a second long a strong gust repeats as a pulse at the loop rate)."""
    g = 0.5 + 0.5 * wander(1024, rng, 3)
    cand = np.sort(rng.uniform(0.0, L, rng.poisson(L * rate)))
    keep = rng.uniform(size=len(cand)) < calm + (1.0 - calm) * g[(cand / L * 1024).astype(int) % 1024]
    return cand[keep]


def chime_loop(rng, sr, L, pieces, rate, collide):
    """The wind on a set of chimes: random pieces struck, often knocking a neighbour a few ms later."""
    ev = []
    for t0 in gusts(L, rng, rate):
        j = rng.integers(len(pieces))
        ev.append((t0, rng.uniform(0.25, 1.0), pieces[j]()))
        if rng.uniform() < collide:
            k = (j + rng.choice([-1, 1])) % len(pieces)
            ev.append((t0 + rng.uniform(0.002, 0.007), rng.uniform(0.2, 0.6), pieces[k]()))
    return peak_norm(circular_hits(int(round(L * sr)), sr, ev))


def sweep(rng, sr, dur, pieces, span, order=1):
    """A hand brushed across the chimes: every piece in turn over `span` s (a little uneven), then a few knocking
    back; the rings fade to silence by `dur`."""
    n = int(dur * sr)
    seq = list(range(len(pieces)))[::order]
    times = np.cumsum(rng.uniform(0.6, 1.4, len(seq))) / len(seq) * span
    ev = [(t, rng.uniform(0.7, 1.0), pieces[j]()) for t, j in zip(times - times[0] + 0.002, seq)]
    for _ in range(3):
        ev.append((rng.uniform(span * 0.6, span * 1.4), rng.uniform(0.2, 0.45), pieces[rng.integers(len(pieces))]()))
    y = np.zeros(n)
    for t0, a, snd in ev:
        s = int(t0 * sr)
        m = min(len(snd), n - s)
        if m > 0:
            y[s:s + m] += a * snd[:m]
    return peak_norm(fade_tail(y, 0.4))


def wood_piece(sr, rng, modes_fd, knock, dur):
    f, a, d = zip(*modes_fd)
    body = ga.modes(sr, dur, list(f), list(a), list(d), rng, onset=0.0005, detune=0.01)
    t = ga.t_axis(sr, dur)
    return body + knock[0] * ga.band_noise(sr, len(t), rng, knock[1], 1.8, hi=0.45 * sr) * ga.env_ad(t, 0.0004, 0.004)


def wood_pieces(sr, rng):
    """Hollow wooden tubes (cane or elder): each knock wakes the air inside, a round "tonk" of about 0.1 s over a
    short ring of the tube wall. Six unrelated lengths: no key."""
    def tube(f):
        wall = f * rng.uniform(2.6, 3.2)
        return lambda: wood_piece(sr, rng, [(f, 1.0, rng.uniform(0.09, 0.14)), (f * 2.03, 0.22, 0.04),
                                            (wall, 0.45, 0.03), (wall * 2.76, 0.15, 0.012)], (0.5, 1800.0), 0.4)
    return [tube(f) for f in [430.0, 515.0, 590.0, 700.0, 815.0, 960.0]]


# --------------------------------------------------------------------- ensembles --- #

def ensemble(sr, root, L, rng, players, render_player):
    """A section: players [(chorus step, vibrato cycles in the loop, vibrato cents)], each its own drift and level
    wobble, all exactly periodic over N samples. render_player(fv, phase, k_top) -> the player's tone for one layer;
    returns the layers' sums."""
    n, f0 = loop_len(sr, note_hz(root), L)
    df = sr / n
    t = np.arange(n) / sr
    out = None
    for step, vcyc, vcents in players:
        fv = f0 + step * df
        vr = vcyc * df
        vd = fv * (2.0 ** (vcents / 1200.0) - 1.0)
        phase = 2 * np.pi * fv * t + (vd / vr * np.sin(2 * np.pi * vr * t + rng.uniform(0, 2 * np.pi)) if vr else 0.0) \
            + 0.5 * wander(n, rng, 3)
        amp = 1.0 + 0.10 * wander(n, rng, 3)
        k_top = int(min(ENS_TOP, 0.45 * sr) / fv)
        layers = render_player(fv, phase, k_top, n, rng)
        layers = [amp * y for y in layers]
        out = layers if out is None else [o + y for o, y in zip(out, layers)]
    return out, n, f0


def harmonics(phase, amps, ph):
    y = np.zeros_like(phase)
    for k, (a, p) in enumerate(zip(amps, ph), start=1):
        if a:
            y += a * np.sin(k * phase + p)
    return y


def choir(rng):
    """Seven voices around A3, the same singers in both. OOH: lips nearly closed, sung softly, a dark hummed "oo"
    (the top falls steeply, hardly any breath). AAH: the mouth wide open and sung out, the singer's ring at 2.5..3.4
    kHz (the formant that carries over an orchestra), more breath. 2026-10-27: pulled further apart, the user could
    barely hear SCAN."""
    sr, root = 16000, 57
    ooh = [(300, 50, 1.0), (870, 80, 0.2), (2240, 140, 0.03)]
    aah = [(730, 90, 1.0), (1090, 110, 0.85), (2440, 160, 0.5), (2900, 300, 0.75), (3400, 300, 0.35)]

    def player(fv, phase, k_top, n, rng):
        k = np.arange(1, k_top + 1)
        ph = rng.uniform(0, 2 * np.pi, len(k))
        breath = ga.band_noise(sr, n, rng, 2600, 1.4, hi=ENS_TOP) * (0.7 + 0.3 * np.sin(phase))
        return [harmonics(phase, k ** -1.8 * formant(k * fv, ooh, floor=0.01), ph) + 0.008 * breath,
                harmonics(phase, k ** -0.7 * formant(k * fv, aah, floor=0.05), ph) + 0.07 * breath]
    players = [(-1, 2, 12), (-1, 3, 8), (0, 2, 14), (0, 3, 6), (0, 2, 10), (1, 3, 9), (1, 2, 13)]
    (o, a), n, f0 = ensemble(sr, root, 0.48, rng, players, player)
    o, a = pair_gain(o, a, 2.0)
    return o, a, sr, root


def horns(rng):
    """Four cow horns, lips buzzing. SOFT: blown gently, round and dark, almost a flugelhorn (the buzz's slope steep,
    no growl). BRASSY: blown as hard as they go, the harmonics up to the bell's formant and a blare above it, the lips
    growling and the tone overdriven at the bell (each player's wave bent, so it stays periodic). 2026-10-27: pulled
    further apart, the user could barely hear SCAN."""
    sr, root = 16000, 48

    def player(fv, phase, k_top, n, rng):
        k = np.arange(1, k_top + 1)
        ph = rng.uniform(0, 2 * np.pi, len(k))
        bell = formant(k * fv, [(1200, 600, 1.4), (2500, 700, 0.7)], floor=1.0)
        growl = 0.25 * wander(n, rng, 8)
        soft = harmonics(phase, k ** -2.4 * formant(k * fv, [(600, 300, 0.3)], floor=1.0), ph)
        brassy = harmonics(phase + growl, k ** -0.75 * bell, ph)
        brassy = np.tanh(2.5 * brassy / np.abs(brassy).max())
        return [soft, brassy]
    players = [(-1, 0, 0), (0, 0, 0), (0, 0, 0), (1, 0, 0)]
    (s, b), n, f0 = ensemble(sr, root, 0.48, rng, players, player)
    s, b = pair_gain(s, b, 3.0)
    return s, b, sr, root


def breath(rng):
    """The singers' breath before the note: an open "h" with the "aah" vowel's colour, swelling and gone."""
    sr, dur = 16000, 0.15
    t = ga.t_axis(sr, dur)
    n = len(t)
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / sr)
    shape = formant(f, [(730, 120, 1.0), (1090, 150, 0.7), (2440, 300, 0.4)], floor=0.05) * (f > 200) * (f < ENS_TOP)
    y = np.fft.irfft(spec * shape, n=n)
    return peak_norm(fade_tail(y * ga.env_ad(t, 0.02, 0.06), 0.4)), sr, 57


def lore(rng):
    """Small cane chimes in a breeze, after listening/inspiration/lore_inspo.wav: tubes tuned to its pitches (3, 3 1/3,
    4, 4.4, 5 1/4, 6, 7 and 10 1/2 times the root, as its 1010 / 1347 / 1770 / 2360 / 3540 Hz over 337), tapped
    softly (2..9 ms onsets), mostly short wooden rings, a tube often knocking its neighbour a few ms later. A is the
    chimes stirring (~16 knocks a second, as the inspiration); B is a gust (~34, faster, shorter), a little louder:
    SCAN brings the wind up."""
    sr, root = 16000, 60
    n, f0 = loop_len(sr, note_hz(root), 0.5)
    tubes = [(3, 0.8), (10 / 3, 0.5), (4, 1.0), (4.4, 0.4), (5.25, 0.8), (6, 0.4), (7, 0.6), (10.5, 0.5)]
    w = np.array([g for _, g in tubes]) / sum(g for _, g in tubes)

    def tube(ratio, soft, ring):
        """A hollow cane tube: a soft 'tonk' at its pitch (a pair 0.3 % apart, so it shimmers), a wall mode at 2.76x,
        a breath of knock."""
        f = f0 * ratio * (1 + rng.uniform(-0.002, 0.002))
        dur = min(0.5, 6 * ring + 0.02)
        fs, amps, decs = [f, f * 1.003, f * 2.76], [1.0, rng.uniform(0.3, 0.6), 0.25], [ring, ring * 0.8, ring * 0.25]
        keep = [i for i, x in enumerate(fs) if x < 0.45 * sr]
        y = ga.modes(sr, dur, [fs[i] for i in keep], [amps[i] for i in keep], [decs[i] for i in keep], rng, onset=soft)
        t = ga.t_axis(sr, dur)
        knock = ga.band_noise(sr, len(t), rng, min(2.2 * f, 6000), 1.4, hi=0.45 * sr) * ga.env_ad(t, soft * 0.5, 0.003)
        return y / np.abs(y).max() + 0.12 * knock / np.abs(knock).max()

    def chimes(rate, soft, short, long_p, cluster, long=(0.08, 0.2), min_gap=0.006):
        ev, t = [], rng.uniform(0, 0.02)
        while t < n / sr:
            j = rng.choice(len(tubes), p=w)
            ring = rng.uniform(*long) if rng.uniform() < long_p else rng.uniform(*short)
            ev.append((t, 10 ** (rng.uniform(-10, 0) / 20), tube(tubes[j][0], rng.uniform(*soft), ring)))
            if rng.uniform() < cluster:
                k = (j + rng.choice([-1, 1])) % len(tubes)
                ev.append((t + rng.uniform(0.004, 0.02), 10 ** (rng.uniform(-14, -4) / 20),
                           tube(tubes[k][0], rng.uniform(*soft), rng.uniform(*short))))
            t += max(min_gap, rng.exponential(1.0 / rate))
        y = circular_hits(n, sr, ev)
        return y / np.sqrt(np.mean(y ** 2))
    still = chimes(16, (0.002, 0.007), (0.012, 0.035), 0.25, 0.5)
    gust = chimes(34, (0.003, 0.008), (0.008, 0.02), 0.15, 0.7) * 1.25    # +2 dB: the wind is louder
    g = PEAK / max(np.abs(still).max(), np.abs(gust).max())
    return still * g, gust * g, sr, root


# ----------------------------------------------------------------- stone, seeds, pipe --- #

def periodic_noise(n, sr, rng, gain):
    """Noise over n samples shaped by gain(f): random phases on the loop's own bins, so it is periodic by construction."""
    f = np.fft.rfftfreq(n, 1.0 / sr)
    y = np.fft.irfft(np.fft.rfft(rng.standard_normal(n)) * gain(f), n=n)
    return y / (np.abs(y).max() + 1e-12)


def logband(f, centre, octaves):
    return np.exp(-0.5 * (np.log2(np.maximum(f, 1.0) / centre) / (octaves / 2)) ** 2)


def pair_gain(a, b, b_db=0.0):
    """A and B RMS-matched (B b_db louder) under one gain, the louder peak at PEAK: SCAN changes the sound, not
    mostly its level."""
    a = a / np.sqrt(np.mean(a ** 2))
    b = b / np.sqrt(np.mean(b ** 2)) * 10 ** (b_db / 20)
    k = PEAK / max(np.abs(a).max(), np.abs(b).max())
    return a * k, b * k


def singing_stone(rng):
    """A wet lithophone slab rubbed (the 2026-10-26 prototype, kept as it was heard: listening/pcm_proto). Its bar
    modes (1, 2.76, 5.40, 8.93, 13.3) sustained by stick-slip, each a steady partial with a twin one loop step up
    (a slow beat) and a narrow band of roughness, the grit pulsing once a cycle. A: stroked. B: rubbed hard: the upper
    modes up, the roughness wide and loud, the stick-slip catching hard: SCAN presses the hand into the stone.
    4096 samples at 11025 (0.37 s), on a grid that puts the fundamental at 220.7 Hz."""
    sr, root, n = 11025, 57, 4096
    step = sr / n
    f0 = round(220.0 / step) * step
    t = np.arange(n)
    kf = round(f0 / step)

    def stone(gains, rough_w, rough_lvl, grit_lvl, grit_pow):
        x = np.zeros(n)
        for r, g in zip(ratios, gains):
            k = round(f0 * r / step)
            if k * step >= sr * 0.45:
                continue
            x += g * np.sin(2 * np.pi * k * t / n + rng.uniform(0, 2 * np.pi))
            x += 0.5 * g * np.sin(2 * np.pi * (k + 1) * t / n + rng.uniform(0, 2 * np.pi))
        bands = [(f0 * r, rough_w * r ** 0.5, g) for r, g in zip(ratios, gains)]
        f = np.fft.rfftfreq(n, 1.0 / sr)
        rough = np.fft.irfft(formant(f, bands, floor=0.0) * (f < sr * 0.45) * np.exp(2j * np.pi * rng.uniform(size=len(f))), n=n)
        x = x / np.abs(x).max() + rough_lvl * rough / np.abs(rough).max()
        return x * (1.0 + grit_lvl * np.maximum(0, np.sin(2 * np.pi * kf * t / n)) ** grit_pow)
    ratios = [1.0, 2.76, 5.40, 8.93, 13.3]
    a = stone([1.0, 0.55, 0.3, 0.16, 0.08], 10.0, 0.6, 0.25, 6)
    b = stone([1.0, 0.8, 0.6, 0.45, 0.3], 28.0, 1.4, 0.8, 3)
    a, b = pair_gain(a, b, 2.0)
    return a, b, sr, root


def rainstick(rng):
    """A cactus rainstick tilted. TRICKLE: a few seeds at a time ticking down the thorns, sparse and bright. POUR:
    the stick upended, hundreds of seeds a second, some striking the wall lower, over the rush of the stream."""
    sr, root = 14000, 60
    n = int(0.45 * sr)

    def ticks(rate, lo_share, gust):
        swell = 0.5 + 0.5 * wander(1024, rng, 2)
        times = np.sort(rng.uniform(0, n / sr, rng.poisson(rate * n / sr)))
        keep = rng.uniform(size=len(times)) < (1 - gust) + gust * swell[(times / (n / sr) * 1024).astype(int) % 1024]
        ev = []
        for tt in times[keep]:
            m = int(sr * 0.005)
            low = rng.uniform() < lo_share
            tk = ga.band_noise(sr, m, rng, rng.uniform(1300, 2400) if low else rng.uniform(2800, 5000), 1.0,
                               hi=0.45 * sr)
            tk *= np.exp(-np.arange(m) / (sr * rng.uniform(0.0004, 0.0013)))
            ev.append((tt, 10 ** (rng.uniform(-18, 0) / 20), tk))
        y = circular_hits(n, sr, ev)
        return y / (np.abs(y).max() + 1e-12)
    a = ticks(70, 0.1, 0.6)
    b = ticks(650, 0.35, 0.3) + 0.25 * periodic_noise(n, sr, rng, lambda f: logband(f, 3500, 1.8))
    a, b = pair_gain(a, b, 4.0)
    return a, b, sr, root


def pan_flute(rng):
    """A pan flute pipe (stopped: the odd harmonics lead). BREATHY: blown softly, nearly a pure tone in a lot of
    breath, the breath pulsing with the tone. FULL: blown hard, the 3rd and 5th harmonics up, an edge, less air."""
    sr, root = 12000, 72

    def player(fv, phase, k_top, n, rng):
        k = np.arange(1, k_top + 1)
        ph = rng.uniform(0, 2 * np.pi, len(k))
        soft = np.array([1.0, 0.05, 0.10, 0.02, 0.03, 0.01, 0.01] + [0.0] * 64)[:len(k)]
        full = np.array([1.0, 0.10, 0.65, 0.06, 0.38, 0.04, 0.2, 0.02, 0.1, 0.01, 0.05] + [0.0] * 64)[:len(k)]
        air = periodic_noise(n, sr, rng, lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 35) ** 2) for h in (1, 2, 3))
                                                    * 0.6 + logband(f, 2500, 2.0)) * (f < ENS_TOP))
        pulse = 0.6 + 0.4 * np.sin(phase)
        return [harmonics(phase, soft, ph) + 0.30 * air * pulse,
                harmonics(phase, full, ph) + 0.10 * air * pulse]
    (a, b), n, f0 = ensemble(sr, root, 0.48, rng, [(0, 2, 9)], player)
    a, b = pair_gain(a, b, 1.0)
    return a, b, sr, root


# ------------------------------------------------------------------------ moonsong --- #

def glide(n, targets, hold=0.7):
    """A path through targets, one equal share of the loop each, holding `hold` of it and easing (raised cosine) to
    the next in the rest, the last back to the first: exactly periodic over n samples."""
    m = len(targets)
    u = np.arange(n) / n * m
    i = np.floor(u).astype(int) % m
    x = np.clip((u - np.floor(u) - hold) / (1.0 - hold), 0.0, 1.0)
    w = 0.5 - 0.5 * np.cos(np.pi * x)
    t = np.asarray(targets, dtype=float)
    return t[i] * (1.0 - w) + t[(i + 1) % m] * w


def moonsong(rng):
    """Voices humming inside a passage tomb under the moon. The loops sit on A2, 110 Hz: Newgrange's chamber
    resonates there, and the stone answers the singers. HUM: men in unison and women an octave up, lips closed (a
    nasal "mm", the top falling away), the chamber's deep fundamental swelling and settling once a loop. HALO: the same
    hum, quieter, and three overtone singers above it, their tongues shaping a narrow formant that picks out one
    harmonic as a whistle: they glide through an A major chord of overtones (6, 8, 9, 10, 12, 16), each at its own
    pace, and above them the 18th..32nd harmonics twinkle faintly. A is a short loop (1 s), B a long one (2 s) so its
    overtone melody turns slowly; both exactly periodic, every glide and twinkle a whole number of turns in its loop."""
    sr, root = 16000, 45
    hum_sp = [(250, 70, 1.0), (2600, 250, 0.05)]

    def chant(L, singers, halo):
        n, f0 = loop_len(sr, note_hz(root), L)
        df = sr / n
        t = np.arange(n) / sr
        u = np.arange(n) / n
        y = np.zeros(n)
        for mult, step, vcyc, vcents, lvl in singers:            # the hum: (octave, chorus step, vibrato, level)
            fv = mult * f0 + step * df
            vr, vd = vcyc * df, fv * (2.0 ** (vcents / 1200.0) - 1.0)
            phase = 2 * np.pi * fv * t + vd / vr * np.sin(2 * np.pi * vr * t + rng.uniform(0, 2 * np.pi)) \
                + 0.4 * wander(n, rng, 3)
            k = np.arange(1, int(ENS_TOP / fv) + 1)
            notch = 1.0 - 0.8 * np.exp(-0.5 * ((k * fv - 1000) / 250) ** 2)      # closed lips: the hum's anti-formant
            amps = k ** -1.7 * formant(k * fv, hum_sp, floor=0.01) * notch
            y += lvl * (1.0 + 0.12 * wander(n, rng, 3)) * harmonics(phase, amps, rng.uniform(0, 2 * np.pi, len(k)))
        # the chamber: its resonance on the fundamental, swelling once a loop
        y += 0.35 * (0.65 + 0.35 * np.sin(2 * np.pi * u + rng.uniform(0, 2 * np.pi))) * np.sin(2 * np.pi * f0 * t)
        y += 0.02 * periodic_noise(n, sr, rng, lambda f: logband(f, 700, 1.5))     # the singers' air
        if not halo:
            return y, n
        y *= 0.55
        # overtone singers: (harmonic path, its turns in the loop, chorus step, level)
        for path, turns, step, lvl in [([8, 8, 10, 12, 10, 9], 1, 0, 1.0),
                                       ([12, 12, 16, 12], 2, 1, 0.55),
                                       ([6, 8, 6, 9], 1, -1, 0.7)]:
            fv = f0 + step * df
            phase = 2 * np.pi * fv * t + 0.25 * wander(n, rng, 3)
            c = glide(n, path * turns)
            swell = 0.75 + 0.25 * np.sin(2 * np.pi * u * turns + rng.uniform(0, 2 * np.pi))
            for k in range(4, 20):
                a = np.exp(-0.5 * ((k - c) / 0.3) ** 2)
                if a.max() > 1e-3:
                    y += 0.35 * lvl * swell * a * np.sin(k * phase + rng.uniform(0, 2 * np.pi))
        for k in range(18, 33):                                  # starlight: each high harmonic its own glint
            tw = (0.5 + 0.5 * np.sin(2 * np.pi * rng.integers(1, 4) * u + rng.uniform(0, 2 * np.pi))) ** 4
            y += 0.03 * tw * np.sin(2 * np.pi * k * f0 * t + rng.uniform(0, 2 * np.pi))
        return y, n

    men, women = [(1, -1, 5, 6, 1.0), (1, 0, 4, 8, 1.0), (1, 0, 6, 5, 0.9), (1, 1, 5, 7, 1.0)], \
        [(2, -1, 5, 9, 0.5), (2, 1, 6, 7, 0.5), (2, 0, 4, 10, 0.45)]
    hum, _ = chant(1.0, men + women, False)
    halo, _ = chant(2.0, men + women, True)
    hum, halo = pair_gain(hum, halo, 2.0)
    return hum, halo, sr, root


def cairn_spirit(rng):
    """A 90s-PCM 'spirit' pad (2026-10-27, wave 73, after SAMHAIN and BULLROARER were dropped): every idea the user
    liked in one. SPIRIT (A): six singers on a warm, dark "oh", breathy pan flutes woven through them, a soft stone
    drone on the fundamental swelling once a loop. HALO (B): the same singers opening to a bright "aah" with the
    singer's ring and more air, an octave-and-fifth halo rising above them, and a glassy shimmer of high harmonics
    twinkling, the D-50 / M1 bell-pad idea. Exactly periodic: every partial on the loop's grid, every swell and
    twinkle a whole number of turns."""
    sr, root = 16000, 57
    oh = [(450, 80, 1.0), (850, 100, 0.45), (2400, 150, 0.08)]
    aah = [(730, 90, 1.0), (1090, 110, 0.7), (2440, 160, 0.4), (2900, 300, 0.55), (3400, 300, 0.25)]

    def player(fv, phase, k_top, n, rng):
        k = np.arange(1, k_top + 1)
        ph = rng.uniform(0, 2 * np.pi, len(k))
        air = periodic_noise(n, sr, rng, lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 30) ** 2) for h in (1, 2, 3))
                                                    + 0.5 * logband(f, 2500, 2.0)) * (f < ENS_TOP))
        pan_amps = np.array([1.0, 0.06, 0.25, 0.03, 0.08, 0.02, 0.03] + [0.0] * 128)[:len(k)]
        pan = harmonics(phase, pan_amps, ph[::-1])
        pulse = 0.6 + 0.4 * np.sin(phase)
        spirit = harmonics(phase, k ** -1.4 * formant(k * fv, oh, floor=0.01), ph) + 0.45 * pan + 0.12 * air * pulse
        halo = harmonics(phase, k ** -0.85 * formant(k * fv, aah, floor=0.04), ph) + 0.2 * pan + 0.1 * air * pulse
        return [spirit, halo]
    players = [(-1, 2, 12), (-1, 3, 8), (0, 2, 14), (0, 3, 6), (1, 3, 9), (1, 2, 13)]
    (a, b), n, f0 = ensemble(sr, root, 0.7, rng, players, player)
    u = np.arange(n) / n
    t = np.arange(n) / sr

    def swell(turns, depth):
        return 1.0 - depth + depth * np.sin(2 * np.pi * turns * u + rng.uniform(0, 2 * np.pi))

    lvl = np.sqrt(np.mean(a ** 2))
    a = a + 1.6 * lvl * swell(1, 0.3) * np.sin(2 * np.pi * f0 * t)                    # the stone drone
    lvl = np.sqrt(np.mean(b ** 2))
    for k, g in [(2, 0.9), (3, 0.7), (4, 0.4), (6, 0.3)]:                             # octave-and-fifth halo
        b = b + g * lvl * swell(1, 0.35) * np.sin(2 * np.pi * k * f0 * t + rng.uniform(0, 2 * np.pi))
    for k in [8, 10, 12, 15, 16, 20, 24]:                                            # glass shimmer
        tw = (0.5 + 0.5 * np.sin(2 * np.pi * rng.integers(1, 4) * u + rng.uniform(0, 2 * np.pi))) ** 2
        b = b + 0.35 * lvl * tw * np.sin(2 * np.pi * k * f0 * t + rng.uniform(0, 2 * np.pi))
    a, b = pair_gain(a, b, 1.5)
    return a, b, sr, root


# --------------------------------------------------------------------------- set --- #
# Samples: (name, kind, recipe -> (samples, rate, root)). Waves: (name, loop A, loop B, B rate, B offset, attack,
# attack level); an attack is a sample name or ("fw", firmware attack name). New waves go at the end: patches store
# the wave's number.

# The rate each sample is stored at, where lower than the rate its recipe draws at (2026-10-27, as the attacks'
# STORE_SR): about 2.2x the band holding 99.9 % of its energy, checked against the images the voice's linear
# interpolation adds above the new Nyquist (CHOIR AAH, HORNS BRASSY, BREATH and LORE sit higher for that).
STORE_SR = {
    "WOOD LOOP": 11025, "WOOD SWEEP": 11025, "CHOIR OOH": 11025,
    "CHOIR AAH": 13000, "HORNS SOFT": 11025, "HORNS BRASSY": 13000, "BREATH": 13000, "LORE STILL": 14000,
    "LORE GUST": 14000, "PAN BREATHY": 11025, "PAN FULL": 11025, "CAIRN SPIRIT": 11025, "CAIRN HALO": 13000, "MOON HUM": 11025, "MOON HALO": 11025,
}


def resample_loop(x, sr, new_sr):
    """A loop to a lower rate: its spectrum cut (a short cosine roll-off below the new Nyquist), so it stays exactly
    periodic and every partial on its grid stays in tune. -> (samples, the rate they hold, to the nearest Hz)"""
    n = len(x)
    m = int(round(n * new_sr / sr))
    spec = np.fft.rfft(x)[:m // 2 + 1]
    f = np.arange(len(spec)) / (m // 2)
    spec = spec * np.where(f < 0.9, 1.0, 0.5 * (1.0 + np.cos(np.pi * np.minimum(f - 0.9, 0.1) / 0.1)))
    return np.fft.irfft(spec, n=m) * (m / n), int(round(sr * m / n))


def build_samples(rerate=True):
    out = {}

    def add(name, kind, x, sr, root):
        assert len(name) <= NAME_LEN and name not in out
        if rerate and STORE_SR.get(name, sr) < sr:
            x, sr = resample_loop(x, sr, STORE_SR[name]) if kind == K_LOOP else \
                (ga.resample(x, sr, STORE_SR[name]), STORE_SR[name])
            x = x / np.abs(x).max() * PEAK
        out[name] = dict(name=name, kind=kind, data=ga.mulaw_roundtrip(x), sr=sr, root=root)

    for i, (stem, pieces_of, sr, L, rate, collide, span) in enumerate([
            ("WOOD", wood_pieces, 16000, 0.55, 10.0, 0.5, 0.16)]):
        rng = np.random.default_rng(SEED_BASE + i)
        pieces = pieces_of(sr, rng)
        add(f"{stem} LOOP", K_LOOP, chime_loop(rng, sr, L, pieces, rate, collide), sr, 60)
        add(f"{stem} SWEEP", K_ONESHOT, sweep(rng, sr, 0.25, pieces, span), sr, 60)
    for i, (stem, fn) in enumerate([("CHOIR", choir), ("HORNS", horns)], start=1):
        a, b, sr, root = fn(np.random.default_rng(SEED_BASE + 10 + i))
        names = {"CHOIR": ("CHOIR OOH", "CHOIR AAH"), "HORNS": ("HORNS SOFT", "HORNS BRASSY")}[stem]
        add(names[0], K_LOOP, a, sr, root)
        add(names[1], K_LOOP, b, sr, root)
    for i, (name, fn) in enumerate([("BREATH", breath)], start=1):
        x, sr, root = fn(np.random.default_rng(SEED_BASE + 20 + i))
        add(name, K_ONESHOT, x, sr, root)
    a, b, sr, root = lore(np.random.default_rng(SEED_BASE + 30))
    add("LORE STILL", K_LOOP, a, sr, root)
    add("LORE GUST", K_LOOP, b, sr, root)
    for i, (stem, fn, tags) in enumerate([("STONE", singing_stone, ("SOFT", "HARD")),
                                          ("RAIN", rainstick, ("TRICKLE", "POUR")),
                                          ("PAN", pan_flute, ("BREATHY", "FULL"))]):
        a, b, sr, root = fn(np.random.default_rng(9101 if stem == "STONE" else SEED_BASE + 40 + i))
        add(f"{stem} {tags[0]}", K_LOOP, a, sr, root)
        add(f"{stem} {tags[1]}", K_LOOP, b, sr, root)
    a, b, sr, root = moonsong(np.random.default_rng(SEED_BASE + 50))
    add("MOON HUM", K_LOOP, a, sr, root)
    add("MOON HALO", K_LOOP, b, sr, root)
    a, b, sr, root = cairn_spirit(np.random.default_rng(SEED_BASE + 80))
    add("CAIRN SPIRIT", K_LOOP, a, sr, root)
    add("CAIRN HALO", K_LOOP, b, sr, root)
    return out


WAVES = [
    ("WOOD CHIMES", "WOOD LOOP", "WOOD LOOP", 1.06, 0.37, "WOOD SWEEP", 110),
    ("RAINSTICK", "RAIN TRICKLE", "RAIN POUR", 1.0, 0.0, ("fw", "SEED RATTLE"), 90),   # was CLAY CHIMES
    ("LORE", "LORE STILL", "LORE GUST", 1.0, 0.0, None, 0),             # was SHELL CHIMES (patches keep 66)
    ("MOONSONG", "MOON HUM", "MOON HALO", 1.0, 0.0, None, 0),         # was STRINGS (removed 2026-10-27)
    ("CHOIR", "CHOIR OOH", "CHOIR AAH", 1.0, 0.0, "BREATH", 60),
    ("HORNS", "HORNS SOFT", "HORNS BRASSY", 1.0, 0.0, ("fw", "HORN BLAT"), 90),
    ("CHOIR+WOOD", "CHOIR AAH", "WOOD LOOP", 1.0, 0.0, None, 0),
    ("SINGING STN", "STONE SOFT", "STONE HARD", 1.0, 0.0, ("fw", "LITHOPHONE"), 60),
    ("PAN FLUTE", "PAN BREATHY", "PAN FULL", 1.0, 0.0, ("fw", "BONE CHIFF"), 80),
    ("CAIRN SPIRIT", "CAIRN SPIRIT", "CAIRN HALO", 1.0, 0.0, "BREATH", 40),   # was BULLROARER, then SAMHAIN
]
assert all(len(w[0]) <= NAME_LEN for w in WAVES)


# ------------------------------------------------------------------------ images --- #

def sample_bytes(s):
    q = [int(v) & 0xFF for v in ga.mulaw_encode(s["data"])]
    if s["kind"] == K_LOOP:
        q.append(q[0])                                       # the guard: the first byte again
    return bytes(q)


def pack_samples(samples):
    """First fit, largest first, into the areas; area 0 also carries the wave entries. -> {name: area}"""
    used = [HDR_SIZE + ENTRY_SIZE * len(WAVES), HDR_SIZE, HDR_SIZE]
    where = {}
    for s in sorted(samples.values(), key=lambda s: -len(sample_bytes(s))):
        need = len(sample_bytes(s)) + ENTRY_SIZE
        for a, cap in enumerate(AREAS):
            if used[a] + need <= cap:
                used[a] += need
                where[s["name"]] = a
                break
        else:
            raise SystemExit(f"{s['name']} does not fit: areas " + ", ".join(f"{u} / {c}" for u, c in zip(used, AREAS)))
    return where, used


def make_images(samples, where):
    """One image per area. Area 0: the waves, then its samples; area 1: its samples. -> [bytes, bytes], refs"""
    order = [[s for s in samples.values() if where[s["name"]] == a] for a in range(len(AREAS))]
    refs = {}
    for a in range(len(AREAS)):
        first = len(WAVES) if a == 0 else 0
        for i, s in enumerate(order[a]):
            refs[s["name"]] = (a << 6) | (first + i)
    images = []
    for a in range(len(AREAS)):
        entries, blobs = [], []
        count = (len(WAVES) if a == 0 else 0) + len(order[a])
        assert count <= MAX_ENTRIES
        off = HDR_SIZE + ENTRY_SIZE * count
        if a == 0:
            for name, la, lb, brate, boff, atk, lvl in WAVES:
                if atk is None:
                    atk_ref = atk_fw = REF_NONE
                elif isinstance(atk, tuple):
                    atk_ref, atk_fw = REF_NONE, FW_ATTACKS.index(atk[1])
                else:
                    atk_ref, atk_fw = refs[atk], REF_NONE
                entries.append(struct.pack("<12sBBBB BBBBBBHHH8x", name.encode().ljust(NAME_LEN, b"\0"),
                                           K_WAVE, 0, 0, 0, refs[la], refs[lb] if lb else REF_NONE, atk_ref, atk_fw,
                                           lvl, 0, int(round(brate * 4096)), int(round(boff * 65536)) & 0xFFFF, 0))
        for s in order[a]:
            blob = sample_bytes(s)
            entries.append(struct.pack("<12sBBBBIIIII", s["name"].encode().ljust(NAME_LEN, b"\0"), s["kind"],
                                       s["root"], 0, 0, s["sr"], off, len(s["data"]), 0, 0))
            blobs.append(blob)
            off += len(blob)
        body = b"".join(entries) + b"".join(blobs)
        assert all(len(e) == ENTRY_SIZE for e in entries)
        h = struct.pack("<IHHII12x", MAGIC, VERSION, count, HDR_SIZE + len(body), zlib.crc32(body))
        img = h + struct.pack("<I", zlib.crc32(h)) + body
        assert len(img) <= AREAS[a], f"area {a}: {len(img)} > {AREAS[a]}"
        images.append(img)
    return images, refs


def write_host_c(path, images):
    """The images as C arrays for the host builds (tests, renders): pcm_store_attach on them as on flash."""
    out = ["/* Generated by src/dsp/gen_pcm_waves.py: do not edit. The PCM store's two images (pcm_store.h), for host",
           " * builds only: the FM1 reads them from the flash they were uploaded to (pcm_store_fm1.c). */",
           "#include <stdint.h>", ""]
    for a, img in enumerate(images):
        out.append(f"const uint32_t pcm_image_host_len{a} = {len(img)}u;")
        out.append(f"const uint8_t pcm_image_host{a}[{len(img)}] __attribute__((aligned(4))) = {{")
        for i in range(0, len(img), 24):
            out.append("    " + ",".join(str(b) for b in img[i:i + 24]) + ",")
        out.append("};")
    path.write_text("\n".join(out) + "\n", encoding="ascii")


def write_fw_c(path, img):
    """The firmware's own area (pcm_store.h PCM_AREA_FW) as a C array, linked into every build."""
    out = ["/* Generated by src/dsp/gen_pcm_waves.py: do not edit. The PCM store's firmware area (pcm_store.h",
           " * PCM_AREA_FW): samples that live in the app itself, read in place (XIP on the FM1); pcm_store.c attaches",
           " * it with the first area attached. */",
           "#include <stdint.h>", "",
           f"const uint32_t pcm_image_fw_len = {len(img)}u;",
           f"_Alignas(4) const uint8_t pcm_image_fw[{len(img)}] = {{"]
    for i in range(0, len(img), 24):
        out.append("    " + ",".join(str(b) for b in img[i:i + 24]) + ",")
    out.append("};")
    path.write_text("\n".join(out) + "\n", encoding="ascii")


def wrap_report(s):
    """LOOP: how many steps inside the loop are bigger than the step across the wrap (0 = the wrap is the
    biggest step of all, a click). ONESHOT: how quiet its end is."""
    x = s["data"]
    if s["kind"] == K_LOOP:
        wrap = abs(x[0] - x[-1])
        return f"wrap: {int((np.abs(np.diff(x)) > wrap).sum())} steps bigger"
    return f"ends at {20 * np.log10(np.abs(x[-8:]).max() / np.abs(x).max() + 1e-9):.0f} dB"


# ------------------------------------------------------------------ audition (host) --- #
# A stand-in for src/dsp/pcm_osc.c, to hear the waves before the firmware plays them: loop A and B at their own
# speeds, SCAN crossfading them; during the attack, the attack and the nearer loop; an amp envelope, no filter.

def read_loop(x, sr, note, root, n, rate_mul, start):
    step = sr / SR * 2.0 ** ((note - root) / 12.0) * rate_mul
    pos = (start + np.arange(n) * step) % len(x)
    i = pos.astype(int)
    fr = pos - i
    return x[i] * (1 - fr) + x[(i + 1) % len(x)] * fr


def read_once(x, sr, note, root, n):
    step = sr / SR * 2.0 ** ((note - root) / 12.0)
    pos = np.arange(n) * step
    ok = pos < len(x) - 1
    i = pos[ok].astype(int)
    fr = pos[ok] - i
    y = np.zeros(n)
    y[ok] = x[i] * (1 - fr) + x[i + 1] * fr
    return y, int(ok.sum())


def play_wave(samples, wave, note, n, scan, rng):
    name, la, lb, brate, boff, atk, lvl = wave
    A, B = samples[la], samples[lb]
    start = rng.uniform(0, len(A["data"]))                  # free-running: the loop is wherever it is
    a = read_loop(A["data"], A["sr"], note, A["root"], n, 1.0, start)
    b = read_loop(B["data"], B["sr"], note, B["root"], n, brate,
                  (start / len(A["data"]) + boff) % 1.0 * len(B["data"]))
    s = np.clip(np.broadcast_to(scan, (n,)), 0.0, 1.0)
    y = a * (1 - s) + b * s
    if atk is not None:
        if isinstance(atk, tuple):
            _, _, root, sr, x = {t[0]: t for t in ga.build_all()}[atk[1]]
            root = 60 if root is None else root
        else:
            x, sr, root = samples[atk]["data"], samples[atk]["sr"], samples[atk]["root"]
        k, na = read_once(x, sr, note, root, n)
        near = np.where(s < 0.5, a, b)                       # two reads during the attack: the nearer loop
        y[:na] = near[:na] + k[:na] * (lvl / 127.0)
    return y


def adsr(n, a, d, s, gate_n, r):
    t = np.arange(n) / SR
    e = np.where(t < a, t / max(a, 1e-6), s + (1.0 - s) * np.exp(-(t - a) / max(d, 1e-6)))
    if gate_n < n:
        e[gate_n:] = e[gate_n] * np.exp(-(t[gate_n:] - t[gate_n]) / r)
    return e


def note(samples, wave, m, gate, env, scan, rng):
    n = int((gate + 5 * env[3]) * SR)
    t = np.arange(n) / SR
    sc = scan(t) if callable(scan) else np.full(n, float(scan))
    return play_wave(samples, wave, m, n, sc, rng) * adsr(n, *env[:3], int(gate * SR), env[3])


def mixdown(parts):
    n = max(int(st * SR) + len(y) for st, y in parts)
    out = np.zeros(n)
    for st, y in parts:
        out[int(st * SR):int(st * SR) + len(y)] += y
    return out


def reverb(x, t60, wet, seed=5):
    rng = np.random.default_rng(seed)
    n_ir = int(t60 * 1.1 * SR)
    t = np.arange(n_ir) / SR
    ir = sum(ga.band_noise(SR, n_ir, rng, c, o) * np.exp(-6.91 * t / (t60 * k))
             for c, o, k in [(300, 2.5, 1.0), (1500, 2.0, 0.7), (5000, 2.0, 0.4)])
    ir = np.concatenate([np.zeros(int(0.018 * SR)), ir * (1.0 - np.exp(-t / 0.01))])
    nfft = 1 << int(np.ceil(np.log2(len(x) + len(ir))))
    w = np.fft.irfft(np.fft.rfft(x, nfft) * np.fft.rfft(ir, nfft), nfft)[:len(x) + len(ir)]
    xp = np.concatenate([x, np.zeros(len(ir))])
    w *= np.sqrt(np.mean(xp ** 2) / (np.mean(w ** 2) + 1e-20))
    return xp * (1.0 - 0.5 * wet) + w * wet


def demos(samples):
    rng = np.random.default_rng(1)
    W = {w[0]: w for w in WAVES}
    rise = lambda t: np.clip((t - 0.5) / 6.0, 0.0, 1.0)
    out = []
    for name in ["WOOD CHIMES"]:
        parts = [(2.6 * k, note(samples, W[name], m, 2.3, (0.002, 1, 1, 0.5), 0.3, rng))
                 for k, m in enumerate([55, 60, 67])]
        parts.append((8.0, note(samples, W[name], 60, 8.0, (0.002, 1, 1, 0.8), rise, rng)))
        out.append((f"{name.lower().replace(' ', '_')}", f"{name}: brushed at 55 60 67, then held with SCAN rising "
                    "(calm -> windy)", reverb(mixdown(parts), 2.5, 0.3)))
    for name, chord, env in [("CHOIR", [45, 52, 57, 60], (0.4, 1, 1, 1.0)),
                             ("HORNS", [41, 48, 53, 57], (0.03, 1, 1, 0.4))]:
        parts = [(0.0, note(samples, W[name], m, 3.0, env, 0.0, rng)) for m in chord]
        parts += [(4.5, note(samples, W[name], m, 3.0, env, 1.0, rng)) for m in chord]
        parts += [(9.0, note(samples, W[name], m, 6.0, env, rise, rng)) for m in chord]
        out.append((name.lower(), f"{name}: a chord at SCAN 0, again at 1, then held with SCAN rising",
                    reverb(mixdown(parts), 2.5, 0.3)))
    parts = [(0.0, note(samples, W["CHOIR+WOOD"], m, 7.0, (1.2, 1, 1, 2.5), 0.5, rng)) for m in [48, 55, 60, 63]]
    out.append(("choir_plus_wood", "CHOIR+WOOD at SCAN 0.5: Universe on one oscillator", reverb(mixdown(parts), 4.0, 0.5)))
    parts = [(0.0, note(samples, W["MOONSONG"], m, 5.0, (1.0, 1, 1, 2.0), 0.0, rng)) for m in [45, 52, 57, 61]]
    parts += [(6.5, note(samples, W["MOONSONG"], m, 5.0, (1.0, 1, 1, 2.0), 1.0, rng)) for m in [45, 52, 57, 61]]
    parts += [(13.0, note(samples, W["MOONSONG"], m, 10.0, (1.5, 1, 1, 3.0), rise, rng)) for m in [50, 57, 62, 65]]
    out.append(("moonsong", "MOONSONG: an A chord humming (SCAN 0), the same with the halo (1), then D minor with SCAN "
                "rising", reverb(mixdown(parts), 4.5, 0.45)))
    parts = [(0.0, note(samples, W["LORE"], 64, 4.0, (0.3, 1, 1, 1.5), 0.0, rng))]
    parts += [(5.5, note(samples, W["LORE"], m, 4.0, (0.3, 1, 1, 1.5), 1.0, rng)) for m in [64]]
    parts += [(11.0, note(samples, W["LORE"], m, 8.0, (0.8, 1, 1, 2.5), rise, rng)) for m in [52, 59, 64, 67]]
    out.append(("lore", "LORE: E4 at SCAN 0 (the inspiration's register), at SCAN 1 (a gust), then a "
                 "chord with SCAN rising", reverb(mixdown(parts), 3.0, 0.35)))
    for name, phrase, chord, env in [("RAINSTICK", [60], [60], (0.02, 1, 1, 0.6)),
                                     ("SINGING STN", [57, 60, 64], [45, 52, 57], (0.05, 1, 1, 0.8)),
                                     ("PAN FLUTE", [72, 74, 76, 79], [72, 79], (0.03, 1, 1, 0.3)),
                                     ("CAIRN SPIRIT", [57, 60, 64], [45, 52, 57, 60, 64], (0.5, 1, 1, 1.5))]:
        parts, t = [], 0.0
        for scan in (0.0, 1.0):
            for m in phrase:
                parts.append((t, note(samples, W[name], m, 0.9, env, scan, rng)))
                t += 1.0
            t += 0.6
        sweep_ = lambda tt: np.interp(tt, [0, 1.5, 4.5, 6, 8], [0, 0, 1, 1, 0])
        parts += [(t, note(samples, W[name], m, 8.0, env, sweep_, rng)) for m in chord]
        out.append((name.lower().replace(' ', '_'), f"{name}: notes at SCAN 0, the same at 1, then held with SCAN "
                    "0 -> 1 -> 0", reverb(mixdown(parts), 2.5, 0.3)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[2] / "listening" / "pcm")
    ap.add_argument("--images-only", action="store_true")
    args = ap.parse_args()
    here = Path(__file__).resolve().parent

    samples = build_samples()
    where, used = pack_samples(samples)
    images, refs = make_images(samples, where)
    for a, img in enumerate(images[:FW_AREA]):
        (here / f"pcm_area{a}.bin").write_bytes(img)
    write_host_c(here.parent / "host" / "pcm_images_host.c", images[:FW_AREA])
    write_fw_c(here / "pcm_area_fw.c", images[FW_AREA])
    total = sum(len(sample_bytes(s)) for s in samples.values())
    print(f"{'name':<13} {'kind':<8} {'rate':>5} {'root':>4} {'ms':>4} {'bytes':>6} area  check")
    for s in samples.values():
        print(f"{s['name']:<13} {'loop' if s['kind'] == K_LOOP else 'one-shot':<8} {s['sr']:5d} {s['root']:4d} "
              f"{1000 * len(s['data']) / s['sr']:4.0f} {len(sample_bytes(s)):6d} {where[s['name']]:4d}  {wrap_report(s)}")
    print(f"{total} B of samples; area 0 {len(images[0])} / {AREAS[0]} B, area 1 {len(images[1])} / {AREAS[1]} B "
          f"({AREAS[0] + AREAS[1] - len(images[0]) - len(images[1])} B spare), firmware area {len(images[2])} / "
          f"{AREAS[2]} B; waves: " + ", ".join(w[0] for w in WAVES))
    print(f"wrote {here / 'pcm_area0.bin'}, {here / 'pcm_area1.bin'}, src/host/pcm_images_host.c and "
          f"{here / 'pcm_area_fw.c'}")
    if args.images_only:
        return
    args.out.mkdir(parents=True, exist_ok=True)
    for f in args.out.glob("*.wav"):
        f.unlink()
    for fname, what, y in demos(samples):
        ga.write_wav(args.out / f"demo_{fname}.wav", peak_norm(y), SR)
        print(f"demo_{fname}.wav: {what}")
    print(f"wrote the demos to {args.out}")


if __name__ == "__main__":
    main()
