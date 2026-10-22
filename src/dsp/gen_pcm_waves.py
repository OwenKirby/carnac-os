#!/usr/bin/env python3
"""Carnac's PCM waves: the samples and wave table of the PCM store (src/dsp/pcm_store.h, version 2), and auditions.

The oscillator's wave list runs: the wavetable banks, then these PCM waves, then the 20 firmware attacks as one-shots
(src/dsp/pcm_osc.h). A PCM wave is an optional attack on top of loop A, and an optional loop B that SCAN crossfades
to; the oscillator reads at most two samples at once (during the attack: the attack and the nearer loop), the same as
a wavetable's two frames, so a PCM wave costs no voices.

  CHIMES     WOOD / CLAY / SHELL: a hand brushing the chimes (the attack), then the wind (the loop). B is the same
             loop again, read 6 % faster from another point: SCAN 0 the calm loop, 1 twice the strikes (density).
  ENSEMBLES  STRINGS (a rosin scrape, then soft -> bright bowing), CHOIR (a breath, then "ooh" -> "aah"), HORNS
             (the firmware's HORN BLAT, then a soft section -> brassy). A and B are the same players with the same
             vibrato and drift, only their tone differs, so the crossfade is one section changing how it plays.
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
                                           src/host/pcm_images_host.c for the host builds) and listening/pcm/
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
AREAS = (0x9000, 0xE000)        # area 0: 36 KiB, area 1: 56 KiB (pcm_store_fm1.c)
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


def chime(sr, rng, f, ratios, amps, decay, dur, scatter=0.03):
    r = np.array(ratios) * (1.0 + scatter * rng.standard_normal(len(ratios)))
    r[0] = 1.0
    decays = decay / (1.0 + 0.35 * np.arange(len(ratios)))
    body = ga.modes(sr, dur, list(f * r), list(amps), list(decays), rng, onset=0.0001)
    t = ga.t_axis(sr, dur)
    return body + 0.2 * ga.band_noise(sr, len(t), rng, min(5000, 0.4 * sr), 1.5) * ga.env_ad(t, 0.0001, 0.001)


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


def clay_pieces(sr, rng):
    """Fired clay discs (the Neolithic's own material) on cords: an earthy "tink", a short ring with a little more
    body than wood, the top damped by the porous clay."""
    def disc(f):
        r = [1.0, 1.58 * rng.uniform(0.97, 1.03), 2.29 * rng.uniform(0.97, 1.03), 3.1 * rng.uniform(0.97, 1.03)]
        return lambda: wood_piece(sr, rng, [(f * r[0], 1.0, rng.uniform(0.10, 0.16)), (f * r[1], 0.6, 0.06),
                                            (f * r[2], 0.35, 0.03), (f * r[3], 0.15, 0.015)], (0.4, 1500.0), 0.45)
    return [disc(f) for f in [610.0, 745.0, 880.0, 1040.0, 1230.0, 1390.0]]


def shell_pieces(sr, rng):
    """Strings of cockle shells: bright, short, clattering rings, and a larger shell's hollow tok."""
    def shell():
        return chime(sr, rng, rng.uniform(2600, 7000), [1.0, 1.37, 1.91, 2.6], [1.0, 0.8, 0.6, 0.4],
                     rng.uniform(0.03, 0.07), 0.2, 0.08)

    def big():
        return chime(sr, rng, rng.uniform(1200, 1800), [1.0, 1.62, 2.4], [1.0, 0.7, 0.4], 0.07, 0.25, 0.05)
    return [shell] * 7 + [big]


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


def strings(rng):
    """Bowed gut strings, six players: a bowed string's sawtooth with each player's bow-position notch, a body's
    resonances. SOFT bows lightly over the fingerboard (the top falls fast, little rosin); BRIGHT digs in (the top
    and the body's upper resonances come up, more bow hiss)."""
    sr, root = 16000, 60
    body_soft = [(280, 70, 1.0), (460, 100, 0.6), (1000, 250, 0.35), (2600, 500, 0.12)]
    body_bright = [(280, 70, 1.0), (460, 100, 0.6), (1000, 250, 0.45), (2600, 500, 0.6), (3500, 600, 0.35)]

    def player(fv, phase, k_top, n, rng):
        k = np.arange(1, k_top + 1)
        notch = np.abs(np.sin(k * np.pi * rng.uniform(0.09, 0.14)))
        ph = rng.uniform(0, 2 * np.pi, len(k))
        hiss = ga.band_noise(sr, n, rng, 3000, 1.5, hi=ENS_TOP) * (0.6 + 0.4 * np.sin(phase))
        soft = harmonics(phase, notch / k ** 1.5 * formant(k * fv, body_soft, 0.3), ph) + 0.02 * hiss
        bright = harmonics(phase, notch / k ** 0.95 * formant(k * fv, body_bright, 0.3), ph) + 0.05 * hiss
        return [soft, bright]
    players = [(-1, 2, 10), (-1, 3, 7), (0, 2, 12), (0, 3, 6), (1, 2, 9), (1, 3, 11)]
    (soft, bright), n, f0 = ensemble(sr, root, 0.48, rng, players, player)
    g = PEAK / max(np.abs(soft).max(), np.abs(bright).max())
    return soft * g * 1.25, bright * g, sr, root            # soft a little up: the two about as loud


def choir(rng):
    """Seven voices around A3, the same singers in both: "ooh" (lips rounded) and "aah" (open). Each its own
    vibrato and drift, one or two chorus steps apart, a little breath."""
    sr, root = 16000, 57
    ooh = [(300, 60, 1.0), (870, 90, 0.45), (2240, 140, 0.15)]
    aah = [(730, 80, 1.0), (1090, 100, 0.55), (2440, 140, 0.25), (3000, 250, 0.15)]

    def player(fv, phase, k_top, n, rng):
        k = np.arange(1, k_top + 1)
        ph = rng.uniform(0, 2 * np.pi, len(k))
        breath = ga.band_noise(sr, n, rng, 2600, 1.4, hi=ENS_TOP) * (0.7 + 0.3 * np.sin(phase))
        return [harmonics(phase, k ** -1.1 * formant(k * fv, ooh), ph) + 0.03 * breath,
                harmonics(phase, k ** -1.1 * formant(k * fv, aah), ph) + 0.03 * breath]
    players = [(-1, 2, 12), (-1, 3, 8), (0, 2, 14), (0, 3, 6), (0, 2, 10), (1, 3, 9), (1, 2, 13)]
    (o, a), n, f0 = ensemble(sr, root, 0.48, rng, players, player)
    g = PEAK / max(np.abs(o).max(), np.abs(a).max())
    return o * g, a * g, sr, root


def horns(rng):
    """Four cow horns, lips buzzing: SOFT a round, dark section (the buzz's slope steep), BRASSY blown hard (the
    harmonics up to the bell's formant, a growl of uneven lip cycles)."""
    sr, root = 16000, 48

    def player(fv, phase, k_top, n, rng):
        k = np.arange(1, k_top + 1)
        ph = rng.uniform(0, 2 * np.pi, len(k))
        bell = formant(k * fv, [(1200, 500, 0.8)], floor=1.0)
        growl = 0.12 * wander(n, rng, 8)
        soft = harmonics(phase, k ** -1.65 * bell, ph)
        brassy = harmonics(phase + growl, k ** -1.05 * bell, ph)
        return [soft, brassy]
    players = [(-1, 0, 0), (0, 0, 0), (0, 0, 0), (1, 0, 0)]
    (s, b), n, f0 = ensemble(sr, root, 0.48, rng, players, player)
    g = PEAK / max(np.abs(s).max(), np.abs(b).max())
    return s * g * 1.4, b * g, sr, root


def rosin(rng):
    """The bow catching the string: rosin noise in uneven stick-slip pulses, gone in about a tenth of a second."""
    sr, dur = 16000, 0.13
    t = ga.t_axis(sr, dur)
    pulses = np.zeros_like(t)
    tt = 0.0
    while tt < dur:
        pulses[int(tt * sr)] = rng.uniform(0.5, 1.0)
        tt += 1.0 / rng.uniform(180, 380)
    grit = np.convolve(pulses, np.exp(-np.arange(int(0.002 * sr)) / (0.0004 * sr)), "same")
    y = grit * ga.band_noise(sr, len(t), rng, 2800, 1.6) + 0.5 * ga.band_noise(sr, len(t), rng, 1800, 2.0)
    return peak_norm(fade_tail(y * ga.env_ad(t, 0.004, 0.05), 0.3)), sr, 60


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


# --------------------------------------------------------------------------- set --- #
# Samples: (name, kind, recipe -> (samples, rate, root)). Waves: (name, loop A, loop B, B rate, B offset, attack,
# attack level); an attack is a sample name or ("fw", firmware attack name). New waves go at the end: patches store
# the wave's number.

def build_samples():
    out = {}

    def add(name, kind, x, sr, root):
        assert len(name) <= NAME_LEN and name not in out
        out[name] = dict(name=name, kind=kind, data=ga.mulaw_roundtrip(x), sr=sr, root=root)

    for i, (stem, pieces_of, sr, L, rate, collide, span) in enumerate([
            ("WOOD", wood_pieces, 16000, 0.55, 10.0, 0.5, 0.16),
            ("CLAY", clay_pieces, 16000, 0.55, 9.0, 0.45, 0.18),
            ("SHELL", shell_pieces, 22050, 0.40, 26.0, 0.6, 0.10)]):
        rng = np.random.default_rng(SEED_BASE + i)
        pieces = pieces_of(sr, rng)
        add(f"{stem} LOOP", K_LOOP, chime_loop(rng, sr, L, pieces, rate, collide), sr, 60)
        add(f"{stem} SWEEP", K_ONESHOT, sweep(rng, sr, 0.25 if stem != "SHELL" else 0.18, pieces, span), sr, 60)
    for i, (stem, fn) in enumerate([("STRINGS", strings), ("CHOIR", choir), ("HORNS", horns)]):
        a, b, sr, root = fn(np.random.default_rng(SEED_BASE + 10 + i))
        names = {"STRINGS": ("STRINGS SOFT", "STRINGS BRT"), "CHOIR": ("CHOIR OOH", "CHOIR AAH"),
                 "HORNS": ("HORNS SOFT", "HORNS BRASSY")}[stem]
        add(names[0], K_LOOP, a, sr, root)
        add(names[1], K_LOOP, b, sr, root)
    for i, (name, fn) in enumerate([("ROSIN", rosin), ("BREATH", breath)]):
        x, sr, root = fn(np.random.default_rng(SEED_BASE + 20 + i))
        add(name, K_ONESHOT, x, sr, root)
    return out


WAVES = [
    ("WOOD CHIMES", "WOOD LOOP", "WOOD LOOP", 1.06, 0.37, "WOOD SWEEP", 110),
    ("CLAY CHIMES", "CLAY LOOP", "CLAY LOOP", 1.06, 0.41, "CLAY SWEEP", 110),
    ("SHELL CHIMES", "SHELL LOOP", "SHELL LOOP", 1.06, 0.33, "SHELL SWEEP", 100),
    ("STRINGS", "STRINGS SOFT", "STRINGS BRT", 1.0, 0.0, "ROSIN", 70),
    ("CHOIR", "CHOIR OOH", "CHOIR AAH", 1.0, 0.0, "BREATH", 60),
    ("HORNS", "HORNS SOFT", "HORNS BRASSY", 1.0, 0.0, ("fw", "HORN BLAT"), 90),
    ("CHOIR+WOOD", "CHOIR AAH", "WOOD LOOP", 1.0, 0.0, None, 0),
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
    used = [HDR_SIZE + ENTRY_SIZE * len(WAVES), HDR_SIZE]
    where = {}
    for s in sorted(samples.values(), key=lambda s: -len(sample_bytes(s))):
        need = len(sample_bytes(s)) + ENTRY_SIZE
        for a, cap in enumerate(AREAS):
            if used[a] + need <= cap:
                used[a] += need
                where[s["name"]] = a
                break
        else:
            raise SystemExit(f"{s['name']} does not fit: areas {used[0]} / {AREAS[0]}, {used[1]} / {AREAS[1]}")
    return where, used


def make_images(samples, where):
    """One image per area. Area 0: the waves, then its samples; area 1: its samples. -> [bytes, bytes], refs"""
    order = [[s for s in samples.values() if where[s["name"]] == a] for a in range(2)]
    refs = {}
    for a in range(2):
        first = len(WAVES) if a == 0 else 0
        for i, s in enumerate(order[a]):
            refs[s["name"]] = (a << 6) | (first + i)
    images = []
    for a in range(2):
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
    for name in ["WOOD CHIMES", "CLAY CHIMES", "SHELL CHIMES"]:
        parts = [(2.6 * k, note(samples, W[name], m, 2.3, (0.002, 1, 1, 0.5), 0.3, rng))
                 for k, m in enumerate([55, 60, 67])]
        parts.append((8.0, note(samples, W[name], 60, 8.0, (0.002, 1, 1, 0.8), rise, rng)))
        out.append((f"{name.lower().replace(' ', '_')}", f"{name}: brushed at 55 60 67, then held with SCAN rising "
                    "(calm -> windy)", reverb(mixdown(parts), 2.5, 0.3)))
    for name, chord, env in [("STRINGS", [48, 55, 60, 64], (0.25, 1, 1, 0.8)),
                             ("CHOIR", [45, 52, 57, 60], (0.4, 1, 1, 1.0)),
                             ("HORNS", [41, 48, 53, 57], (0.03, 1, 1, 0.4))]:
        parts = [(0.0, note(samples, W[name], m, 3.0, env, 0.0, rng)) for m in chord]
        parts += [(4.5, note(samples, W[name], m, 3.0, env, 1.0, rng)) for m in chord]
        parts += [(9.0, note(samples, W[name], m, 6.0, env, rise, rng)) for m in chord]
        out.append((name.lower(), f"{name}: a chord at SCAN 0, again at 1, then held with SCAN rising",
                    reverb(mixdown(parts), 2.5, 0.3)))
    parts = [(0.0, note(samples, W["CHOIR+WOOD"], m, 7.0, (1.2, 1, 1, 2.5), 0.5, rng)) for m in [48, 55, 60, 63]]
    out.append(("choir_plus_wood", "CHOIR+WOOD at SCAN 0.5: Universe on one oscillator", reverb(mixdown(parts), 4.0, 0.5)))
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
    for a, img in enumerate(images):
        (here / f"pcm_area{a}.bin").write_bytes(img)
    write_host_c(here.parent / "host" / "pcm_images_host.c", images)
    total = sum(len(sample_bytes(s)) for s in samples.values())
    print(f"{'name':<13} {'kind':<8} {'rate':>5} {'root':>4} {'ms':>4} {'bytes':>6} area  check")
    for s in samples.values():
        print(f"{s['name']:<13} {'loop' if s['kind'] == K_LOOP else 'one-shot':<8} {s['sr']:5d} {s['root']:4d} "
              f"{1000 * len(s['data']) / s['sr']:4.0f} {len(sample_bytes(s)):6d} {where[s['name']]:4d}  {wrap_report(s)}")
    print(f"{total} B of samples; area 0 {len(images[0])} / {AREAS[0]} B, area 1 {len(images[1])} / {AREAS[1]} B "
          f"({sum(AREAS) - len(images[0]) - len(images[1])} B spare); waves: " + ", ".join(w[0] for w in WAVES))
    print(f"wrote {here / 'pcm_area0.bin'}, {here / 'pcm_area1.bin'} and src/host/pcm_images_host.c")
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
