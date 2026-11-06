"""Candidate wavetable banks that do what the VCO (a shape through one low-pass) cannot: PETROGLYPH, PASSAGE TOMB,
AURORA. Each scored against the VCO (as wt_review) and against the kept banks, and swept to a WAV."""
import sys, pathlib, wave
import numpy as np
DSP, OUT = sys.argv[1], pathlib.Path(sys.argv[2])
sys.path.insert(0, DSP)
import gen_wavetables as gw
K, KI, KSAFE, H, N = gw.K, gw.KI, gw.KSAFE, gw.H, gw.N
SR = 44100


def petroglyph(t, seed):
    """Marks cut in the stone: each frame a different sparse set of harmonics, all at one gentle slope."""
    f = gw.frame_index(t)
    primes = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61]
    fib = [1, 2, 3, 5, 8, 13, 21, 34, 55, 89]
    sets = [lambda k: (k & (k - 1)) == 0,                       # octaves 1 2 4 8 16 ...
            lambda k: k % 2 == 1,                               # odd
            lambda k: k % 3 == 1,                               # 1 4 7 10 ...
            lambda k: (k % 6 == 1) | (k % 6 == 5),              # free of 2 and 3: 1 5 7 11 13 ...
            lambda k: np.isin(k, fib),                          # Fibonacci
            lambda k: k % 4 == 1,                               # 1 5 9 13 ...
            lambda k: np.isin(k, [n * n for n in range(1, 16)]),  # squares
            lambda k: (k % 5 == 1) | (k % 5 == 2)]              # pairs: 1 2 6 7 11 12 ...
    mask = sets[f](KI) & (KI > 0) & (KI <= 96)
    return gw.tonal(mask * KSAFE ** -0.6, gw.schroeder())


def passage_tomb(t, seed):
    """A bright tone sounded in the tomb: three resonances of the stone passage, high and close together, slide down
    and spread as the passage opens into the chamber, and the chamber's low boom swells under them."""
    src = np.where(KI > 0, KSAFE ** -0.5, 0.0)
    peaks = [(28 - 22 * t, 0.10), (40 - 26 * t, 0.10), (55 - 25 * t, 0.12)]
    res = 0.03 + sum(np.exp(-0.5 * (np.log2(KSAFE / c) / w) ** 2) for c, w in peaks)
    boom = 2.0 * t * np.exp(-0.5 * (np.log2(KSAFE / 2.0) / 0.25) ** 2)
    return gw.tonal(src * (res + boom) * (KI > 0), gw.schroeder())


def aurora(t, seed):
    """Curtains of light: a bright spectrum rippled in log-frequency (a frozen phaser of many notches), the ripples
    tightening and drifting frame by frame, a sparkle of high harmonics glinting in the later frames."""
    rng = np.random.default_rng(seed)
    base = np.where(KI > 0, KSAFE ** -0.7, 0.0) * (KI <= 120)
    dens = 1.5 + 4.5 * t                                         # ripples per octave
    ripple = 0.5 + 0.5 * np.cos(2 * np.pi * dens * np.log2(KSAFE) + 2 * np.pi * 0.7 * t)
    sparkle = (t > 0.4) * (t - 0.4) * 0.6 * ((rng.uniform(size=H + 1) > 0.8) & (KI >= 24) & (KI <= 96))
    return gw.tonal(base * (0.08 + ripple ** 2) + sparkle * KSAFE ** -0.3, gw.schroeder())


CANDS = [("Petroglyph", petroglyph), ("Passage Tomb", passage_tomb), ("Aurora", aurora)]

# ---- scoring (wt_review's VCO + one low-pass test, and the nearest kept bank)
Hn = 64; k = np.arange(1, Hn + 1)
def db(m): m = np.asarray(m, float); return 20 * np.log10(m / m.max() + 1e-9)
V = {"tri": np.where(k % 2 == 1, 1 / k ** 2, 1e-6), "saw": 1 / k}
for w in np.arange(0.05, 0.51, 0.025): V[f"p{w:.2f}"] = np.abs(np.sin(np.pi * k * w)) / k + 1e-6
for a in (0.25, 0.5, 0.75): V[f"sawsq{a}"] = np.abs((1 - a) / k + a * np.where(k % 2 == 1, 1 / k, 0)) + 1e-6
F = []
for fc in np.geomspace(1, 80, 40):
    for Q in (0.5, 0.7, 1.0, 2.0, 4.0, 8.0):
        for n in (1, 2):
            r = k / fc; F.append(20 * np.log10(1 / np.sqrt((1 - r ** 2) ** 2 + (r / Q) ** 2) ** n + 1e-12))
F = np.stack(F); cand = np.stack([db(v) for v in V.values()]); FLOOR = -45
def vco_err(frame):
    fd = np.maximum(db(np.abs(np.fft.rfft(frame))[1:Hn + 1]), FLOOR); best = 1e9
    for c in cand:
        tt = c[None, :] + F; tt -= tt.max(1, keepdims=True); tt = np.maximum(tt, FLOOR)
        m = (fd > FLOOR)[None, :] | (tt > FLOOR); d = np.where(m, fd[None, :] - tt, 0.0)
        best = min(best, (np.sqrt((d ** 2).sum(1) / m.sum(1))).min())
    return best
img = np.frombuffer(open(pathlib.Path(DSP) / "wavetables_image.bin", "rb").read()[32:], "<i2").reshape(6, 8, 1024)[:, :, :512] / 32768
KEPT = ["Ley Line", "Moonstone", "Star Chart", "Overtone Halo", "Frost Shatter", "Dust"]
def hdb(x): s = np.abs(np.fft.rfft(x))[1:97]; return (20 * np.log10(s / s.max() + 1e-6)).clip(-60, 0)
kept = [[hdb(img[b, f]) for f in range(8)] for b in range(6)]


def sweep_wav(frames, path):
    def note(f0, dur):
        n = int(dur * SR); X = np.fft.rfft(frames, axis=1); X[:, int(0.45 * SR / f0) + 1:] = 0; fr = np.fft.irfft(X, N, axis=1)
        ph = (np.arange(n) * f0 * N / SR) % N; i0 = ph.astype(int); a = ph - i0
        u = np.arange(n) / n; s = np.where(u < 0.5, u * 2, 2 - u * 2) * 7; kk = np.minimum(s.astype(int), 6); w = s - kk
        r = lambda j: fr[j, i0] * (1 - a) + fr[j, (i0 + 1) % N] * a
        y = np.zeros(n)
        for j in range(7):
            m = kk == j; y[m] = (r(j) * (1 - w) + r(j + 1) * w)[m]
        return y * np.minimum(1, np.arange(n) / 882) * np.minimum(1, (n - np.arange(n)) / 4410)
    x = sum(note(440 * 2 ** ((m - 69) / 12), 8.0) for m in (48, 55, 64)) / 3
    x = np.concatenate([x, np.zeros(SR // 2), note(65.41, 6.0)]); x = x / np.abs(x).max() * 0.8
    wv = wave.open(str(path), "wb"); wv.setnchannels(1); wv.setsampwidth(2); wv.setframerate(SR)
    wv.writeframes((x * 32767).astype("<i2").tobytes()); wv.close()


OUT.mkdir(parents=True, exist_ok=True)
for i, (name, fn) in enumerate(CANDS):
    frames = np.stack([np.asarray(fn(f / 7, 5000 + i), float) for f in range(8)])
    frames = frames - frames.mean(1, keepdims=True); frames = frames / np.abs(frames).max() * 0.92
    errs = [vco_err(fr) for fr in frames]
    near = []
    for fr in frames:
        d = min((np.sqrt(np.mean((hdb(fr) - kf) ** 2)), KEPT[b]) for b in range(6) for kf in kept[b])
        near.append(d)
    rng_ = np.sqrt(np.mean((hdb(frames[0]) - hdb(frames[7])) ** 2))
    steps = [np.sqrt(np.mean((hdb(frames[j]) - hdb(frames[j + 1])) ** 2)) for j in range(7)]
    nb = min(near)
    print(f"{name:13s} VCO-reproducible {sum(e < 5 for e in errs)}/8 (errors {' '.join(f'{e:.0f}' for e in errs)} dB); "
          f"SCAN range {rng_:.0f} dB, steps {min(steps):.0f}..{max(steps):.0f}; nearest kept bank {nb[1]} at {nb[0]:.0f} dB")
    sweep_wav(frames, OUT / f"new_{name.lower().replace(' ', '_')}.wav")
