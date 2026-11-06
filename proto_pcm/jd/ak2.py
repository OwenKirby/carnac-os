import numpy as np
x = np.load("x.npy"); sr = 44100; st = np.load("starts.npy")
def hp_env(a, fc, h=0.002):
    X = np.fft.rfft(a); f = np.fft.rfftfreq(len(a), 1 / sr); X[f < fc] = 0; y = np.fft.irfft(X, len(a))
    n = int(h * sr); return np.array([np.abs(y[j:j + n]).max() for j in range(0, len(y) - n, n)])
for w, fc, span in ((107, 2500, (0.0, 0.8)), (108, 1800, (0.0, 0.78))):
    t0 = st[w - 1]; a = x[int((t0 + span[0]) * sr):int((t0 + span[1]) * sr)]
    e = hp_env(a, fc); ed = 20 * np.log10(e / e.max() + 1e-6)
    pk = [i for i in range(3, len(e) - 3) if e[i] == e[i - 3:i + 4].max() and ed[i] > -14 and e[i] > 1.6 * e[max(0, i - 8):i - 2].min()]
    iv = np.diff(pk) * 2
    print(f"wave {w}: {len(pk)} knocks in {span[1]-span[0]:.2f}s = {len(pk)/(span[1]-span[0]):.1f}/s; intervals ms "
          f"median {np.median(iv):.0f}, 10-90% {np.percentile(iv,10):.0f}-{np.percentile(iv,90):.0f}")
    # mean knock decay: average envelope after each knock (dB vs knock peak)
    tr = np.array([ed[p:p + 30] - ed[p] for p in pk if p + 30 < len(ed)])
    print("  mean knock decay (2 ms steps):", " ".join(f"{v:.0f}" for v in tr.mean(0)[::3]))
# chime ring time: the long release at the file's end
t = len(x) / sr; a = x[int((t - 3.4) * sr):]
S = []
n = 4096
for j in range(0, len(a) - n, n // 2):
    S.append(np.abs(np.fft.rfft(a[j:j + n] * np.hanning(n))))
S = 20 * np.log10(np.array(S) + 1e-9); f = np.fft.rfftfreq(n, 1 / sr)
lv = S.max(1); print("release level per 46 ms:", " ".join(f"{v - lv.max():.0f}" for v in lv[::2]))
for fr in (2093, 2453, 2698, 3401, 4363):
    i = np.argmin(abs(f - fr)); tr = S[:, i - 1:i + 2].max(1)
    print(f"  {fr} Hz:", " ".join(f"{v - tr.max():.0f}" for v in tr[::2]))
