import numpy as np
x = np.load("x.npy"); sr = 44100; on = np.load("on.npy"); db = np.load("db.npy")
def f0(seg):
    seg = seg - seg.mean(); n = len(seg)
    X = np.fft.rfft(seg * np.hanning(n), 4 * n); ac = np.fft.irfft(np.abs(X) ** 2)[: n]
    lo, hi = int(sr / 1000), int(sr / 50)
    i = lo + np.argmax(ac[lo:hi]); return sr / i, ac[i] / ac[0]
def midi(f): return 69 + 12 * np.log2(f / 440)
out = []
for o in on:
    t = o * 0.005; a = int((t + 0.08) * sr)
    f, c = f0(x[a:a + 4096])
    out.append((t, midi(f), c, db[o:o + 40].max()))
np.save("pitch.npy", np.array(out))
for t, m, c, l in out[:60]: print(f"{t:7.2f} {m:6.1f} {c:4.2f} {l:6.1f}")
