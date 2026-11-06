import sys; sys.path.insert(0, ".")
import numpy as np, feats_helpers as fh
from png import write_png
x = fh.x; sr = 44100; st = np.load("starts.npy"); on = np.load("on.npy") * 0.005
for w in (107, 108):
    t0 = st[w - 1]; t1 = st[w] if w < 108 else len(x) / sr
    seg = x[int(t0 * sr):int(t1 * sr)]
    print(f"== wave {w}: {t0:.2f}..{t1:.2f} s, onsets:", " ".join(f"{o - t0:.2f}" for o in on if t0 - 0.05 < o < t1))
    # first note: up to the next onset
    nx = [o for o in on if t0 + 0.15 < o < t1]; end = (nx[0] - t0) if nx else 0.8
    a = seg[:int(end * sr)]
    h = int(0.002 * sr); e = np.array([np.sqrt(np.mean(a[j:j + h] ** 2)) for j in range(0, len(a) - h, h)])
    e_db = 20 * np.log10(e / e.max() + 1e-6)
    print(" first note", f"{end:.2f}s; env 10ms:", " ".join(f"{v:.0f}" for v in e_db[::5][:60]))
    ee = e - np.convolve(e, np.ones(40) / 40, "same")
    ac = np.correlate(ee, ee, "full")[len(ee) - 1:]; ac /= ac[0]
    lags = np.arange(len(ac)) * 0.002
    m = (lags > 0.02) & (lags < 0.3)
    k = np.argmax(ac[m]); print(f" tremolo: period {lags[m][k]*1000:.0f} ms ({1/lags[m][k]:.1f} Hz), corr {ac[m][k]:.2f}")
    S = np.abs(np.fft.rfft(a * np.hanning(len(a)), 1 << 17)); f = np.fft.rfftfreq(1 << 17, 1 / sr)
    Sd = 20 * np.log10(S / S.max() + 1e-9)
    pk = [i for i in range(2, len(S) - 2) if S[i] == S[i - 2:i + 3].max() and Sd[i] > -35 and f[i] > 80]
    pk = sorted(pk, key=lambda i: -S[i])[:16]
    print(" partials (Hz, dB, ratio to C4 261.6):", ", ".join(f"{f[i]:.0f}/{Sd[i]:.0f}/{f[i]/261.63:.2f}" for i in sorted(pk)))
    # decay of each top partial over the note: band energy in 20 ms frames
    for i in sorted(pk)[:8]:
        fr = f[i]; n = int(0.02 * sr); tt = np.arange(n) / sr
        en = [abs(np.sum(a[j:j + n] * np.exp(-2j * np.pi * fr * tt))) for j in range(0, len(a) - n, n)]
        en = 20 * np.log10(np.array(en) / max(en) + 1e-6)
        print(f"  {fr:6.0f} Hz:", " ".join(f"{v:.0f}" for v in en[:30]))
