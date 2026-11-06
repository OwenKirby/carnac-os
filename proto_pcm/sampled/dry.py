"""Sampled PCM waves from the new source WAVs (2026-11-06): de-reverb and loop tools.

The sources (docs/PCM waves/new waves to consider, main folder, never committed) play C2/C3/C4 (or C2..C6) notes,
each held ~2.3 s then left to ring out in a hall (late decay ~-20..-25 dB/s). The user wants NO reverb in our waves
and the detail + movement of the samples kept. So:

  1. the reverb is MEASURED per octave band from each note's own release (late decay slope = the hall's T60, the
     slope line extrapolated back to the note-off = the hall's steady share of the held note);
  2. a late-reverb estimator (recursive, per bin: P_r[t] = a P_r[t-1] + c P_d[t-D], with a from the slope and c
     from the steady share) predicts the reverb in every STFT bin; what stands above it is the dry note. Gains are
     smoothed over time and frequency (no musical noise) and floored;
  3. loops are cut from the dry note itself (the real movement: chorus, vibrato, glints) with a crossfade into the
     material just before the loop start, so the handoff from the attack is the sample itself, unbroken.

Notes come out mono (mid), pitch-corrected to the MIDI root, at 44.1 kHz, as (y, nt) pairs for finish_pair_float."""
import pathlib, wave, numpy as np

SRC = pathlib.Path(__file__).resolve().parents[2].parent / "Claude Synth" / "docs" / "PCM waves" / "new waves to consider"
SR = 44100
BANDS = np.array([125, 250, 500, 1000, 2000, 4000, 8000, 16000], float)


def load(rel):
    w = wave.open(str(SRC / rel)); sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
    assert sw == 2, rel
    x = np.frombuffer(w.readframes(n), np.int16).astype(np.float64).reshape(-1, ch) / 32768.0
    return sr, x


def resample(x, ratio):
    """FFT resample by `ratio` (new length = len * ratio), band-limited, for whole notes (a few s)."""
    n = len(x); m = int(round(n * ratio))
    X = np.fft.rfft(x)
    k = min(len(X), m // 2 + 1)
    Y = np.zeros(m // 2 + 1, complex); Y[:k] = X[:k]
    if ratio < 1:                                    # cos taper on the top 5 % of the kept band
        t = np.linspace(0, 1, k); Y[:k] *= np.where(t < 0.95, 1, 0.5 + 0.5 * np.cos(np.pi * (t - 0.95) / 0.05))
    return np.fft.irfft(Y, m) * (m / n)


def env_db(x, hop):
    e = np.sqrt(np.convolve(x * x, np.ones(hop) / hop, "same") + 1e-14)[::hop]
    return 20 * np.log10(e)


def get_note(rel, k, per=None):
    """Note k of a file: mid channel at 44.1 kHz from 50 ms before its onset; the onset index and the note-off."""
    sr, x = load(rel)
    dur = len(x) / sr                                    # 21.6 s = 3 notes x 7.2, 28.8 s = 3 x 9.6, 50.4 s = 7 x 7.2
    per = per or (9.6 if abs(dur - 28.8) < 0.5 else 7.2)
    if dur < 6:
        per = dur
    a = int(k * per * sr); b = min(len(x), int((k + 1) * per * sr))
    m = x[a:b].mean(1)
    if sr != SR:
        m = resample(m, SR / sr)
    hop = int(0.005 * SR); d = env_db(m, hop)
    on = max(0, int(np.argmax(d > d.max() - 40)) * hop - int(0.05 * SR))
    m = m[on:]
    d = env_db(m, hop); t = np.arange(len(d)) * hop / SR
    sus = np.median(d[(t > 0.5) & (t < 2.0)])
    below = np.where((d < sus - 12) & (t > 1.0))[0]                     # well into the release
    if not len(below):                                                   # held to the end of the file (no release)
        return m, int(0.05 * SR), len(m) - hop
    below = below[0]
    up = np.where(d[:below] > sus - 3)[0]
    off = up[-1] * hop if len(up) else below * hop                       # last moment at the held level
    return m, int(0.05 * SR), off


def stft(x, N=2048, H=256):
    w = np.hanning(N)
    xp = np.concatenate([np.zeros(N), x, np.zeros(N)])
    fr = [np.fft.rfft(xp[i:i + N] * w) for i in range(0, len(xp) - N, H)]
    return np.array(fr)


def istft(X, n, N=2048, H=256):
    w = np.hanning(N); y = np.zeros(len(X) * H + N); ws = np.zeros_like(y)
    for i, F in enumerate(X):
        y[i * H:i * H + N] += np.fft.irfft(F, N) * w; ws[i * H:i * H + N] += w * w
    y /= np.maximum(ws, 1e-6)
    return y[N:N + n]


def band_power(P, f):
    """octave-band power per frame (P = |X|^2 frames x bins)"""
    out = []
    for c in BANDS:
        sel = (f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))
        out.append(P[:, sel].sum(1) + 1e-20)
    return 10 * np.log10(np.array(out).T)                  # frames x bands, dB


def reverb_profile(m, off, N=2048, H=256, verbose=""):
    """Per band: the late decay (dB/s) after the note-off, and the hall's steady level relative to the held note
    (the decay line extrapolated back to the note-off), as a reverb/direct power ratio."""
    X = stft(m, N, H); f = np.fft.rfftfreq(N, 1 / SR)
    B = band_power(np.abs(X) ** 2, f)
    hs = H / SR; t = (np.arange(len(B)) * H - N) / SR       # frame centres (the stft pads N in front)
    t0 = off / SR
    slope, ratio = [], []
    for b in range(len(BANDS)):
        e = np.convolve(B[:, b], np.ones(8) / 8, "same")
        held = np.median(e[(t > t0 - 0.8) & (t < t0 - 0.05)])
        floor = np.percentile(e[t > t0], 5)
        late = (t > t0 + 0.05) & (e < held - 12) & (e > max(held - 45, floor + 8))
        if late.sum() < 10:
            slope.append(-25.0); ratio.append(0.3); continue
        k, c = np.polyfit(t[late], e[late], 1)
        k = min(k, -6.0)                                     # never slower than 6 dB/s
        r0 = (k * t0 + c) - held                             # hall's level at the note-off vs the held note, dB
        p = 10 ** (min(r0, -1.0) / 10)                       # reverb share of the held power
        slope.append(k); ratio.append(min(p / (1 - p), 3.0))
    if verbose:
        print(f"   {verbose}: decay " + " ".join(f"{s:4.0f}" for s in slope) + " dB/s | r/d " +
              " ".join(f"{10 * np.log10(r):+4.0f}" for r in ratio) + " dB")
    return np.array(slope), np.array(ratio)


def dereverb(m, prof, beta=1.0, gmin_db=-24.0, D=3, N=2048, H=256):
    """Recursive late-reverb estimate per bin, subtracted in power; smoothed, floored gains."""
    slope, ratio = prof
    X = stft(m, N, H); P = np.abs(X) ** 2; f = np.fft.rfftfreq(N, 1 / SR)
    lf = np.log2(np.clip(f, BANDS[0], BANDS[-1]))
    k = np.interp(lf, np.log2(BANDS), slope)                # dB/s per bin
    r = np.interp(lf, np.log2(BANDS), ratio)
    a = 10 ** (k * (H / SR) / 10)                            # power decay per frame
    c = r * (1 - a)                                          # steady state: P_r = r P_d
    Pr = np.zeros(P.shape[1]); Pd_hist = [np.zeros(P.shape[1])] * D
    G = np.zeros_like(P); gmin = 10 ** (gmin_db / 10)
    sm = np.ones(5) / 5
    for i in range(len(P)):
        Pr = a * Pr + c * Pd_hist[-D]
        Pd = np.maximum(P[i] - beta * Pr, gmin * P[i])
        g = Pd / (P[i] + 1e-20)
        G[i] = g
        Pd_s = np.convolve(Pd, sm, "same")                   # reverb of a vibrato'd partial spreads to its neighbours
        Pd_hist = Pd_hist[1:] + [Pd_s]
    # smooth the gains: 3 frames in time, 3 bins in frequency (no musical noise), amplitude domain
    G = np.sqrt(G)
    from numpy.lib.stride_tricks import sliding_window_view as sw
    Gp = np.pad(G, ((1, 1), (1, 1)), mode="edge")
    G = sw(Gp, (3, 3)).mean(axis=(2, 3))
    return istft(X * G, len(m), N, H)


def f0_of(y, lo=40, hi=1500):
    N = 1 << 17; seg = y[:min(len(y), 2 * SR)]
    W = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), N)); f = np.fft.rfftfreq(N, 1 / SR)
    h = W.copy()
    for q in (2, 3, 4):
        h[:len(W) // q] *= W[::q][:len(W) // q]
    sel = (f > lo) & (f < hi)
    f0 = f[sel][np.argmax(h[sel])]
    # refine on the harmonic with most energy among H1..H4 (parabolic peak)
    best = None
    for q in (1, 2, 3, 4):
        i = int(round(q * f0 * N / SR)); j = i - 6 + int(np.argmax(W[i - 6:i + 7]))
        if best is None or W[j] > best[0]:
            a_, b_, c_ = np.log(W[j - 1:j + 2] + 1e-12); d = 0.5 * (a_ - c_) / (a_ - 2 * b_ + c_)
            best = (W[j], (j + d) * SR / N / q)
    return best[1]


def to_root(y, root, f0=None):
    """resample so the note's f0 is exactly MIDI `root`"""
    want = 440.0 * 2 ** ((root - 69) / 12)
    f0 = f0 or f0_of(y, want / 1.4, want * 1.4)
    cents = 1200 * np.log2(f0 / want)
    assert abs(cents) < 60, (f0, want, cents)
    return resample(y, f0 / want), cents


def flatten(y, start, end, win=0.25, keep=0.0):
    """divide out the slow level trend (dB-linear fit over [start,end)) from start on; keep = dB/s left in"""
    hop = int(0.01 * SR); d = env_db(y[start:end], hop); t = np.arange(len(d)) * 0.01
    k, c = np.polyfit(t, d, 1)
    tt = (np.arange(len(y) - start)) / SR
    g = 10 ** (-(k - keep) * tt / 20)
    out = y.copy(); out[start:] *= g
    return out, k


def even(y, start, depth=0.6, win=0.15, ramp=0.1):
    """tame slow swells from `start` on: divide by the (win-smoothed) level ^ depth, relative to its median; the
    correction fades in over `ramp` s before start, so the attack stays as it was. Fast movement is untouched."""
    h = int(0.01 * SR); n = len(y)
    e = np.sqrt(np.convolve(y * y, np.ones(int(win * SR)) / int(win * SR), "same") + 1e-12)
    med = np.median(e[start:])
    g = (e / med) ** (-depth)
    r0 = max(0, start - int(ramp * SR))
    w = np.clip((np.arange(n) - r0) / max(1, start - r0), 0, 1)
    g = 1 + w * (g - 1)
    g = np.convolve(g, np.ones(h) / h, "same")
    return y * g


SEARCH = 0.12                 # the loop length search window, s (bank.py's fit sets it tight)


def make_loop(y, T, L, xf, search=None, f0=None):
    """Loop n samples from T (~L s, n chosen near L to best match the crossfaded regions), crossfading the loop's
    last xf samples into y[T-xf:T] so the wrap continues exactly as the note did into y[T]."""
    search = SEARCH if search is None else search
    n0 = int(L * SR); best = None
    a = y[T - xf:T]
    step = 1 if f0 is None else 1
    for n in range(n0 - int(search * SR), n0 + int(search * SR), step):
        b = y[T + n - xf:T + n]
        rho = np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-12)
        if best is None or rho > best[0]:
            best = (rho, n)
    rho, n = best
    loop = y[T:T + n].copy()
    t = np.linspace(0, 1, xf, endpoint=False)
    # crossfade gains for correlation rho: g_in^2 + g_out^2 + 2 rho g_in g_out = 1
    gi = np.sin(0.5 * np.pi * t); go = np.cos(0.5 * np.pi * t)
    norm = np.sqrt(gi ** 2 + go ** 2 + 2 * max(rho, 0) * gi * go)
    loop[n - xf:] = (go * y[T + n - xf:T + n] + gi * a) / norm
    return loop, n, rho


class NoteT:
    """what finish_pair_float needs: T (attack samples), n (loop samples), sr"""
    def __init__(self, T, n, sr=SR):
        self.T, self.n, self.sr = T, n, sr


def whole(y, T, loop):
    return np.concatenate([y[:T], loop]), NoteT(T, len(loop))
