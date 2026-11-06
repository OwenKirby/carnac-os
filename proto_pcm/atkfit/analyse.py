import sys, numpy as np
sys.path.insert(0, sys.argv[1])
import gen_pcm_waves as gp, gen_attacks as ga
S = gp.build_samples()
FW = {t[0]: t for t in ga.build_all()}
def cent(x, sr):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / sr)
    return (X * f).sum() / X.sum()
def harm_share(x, sr, f0):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2; f = np.fft.rfftfreq(len(x), 1 / sr)
    h = f / f0; near = (np.abs(h - np.round(h)) < 0.04) & (h > 0.5)
    return X[near].sum() / X.sum()
def rms(x): return np.sqrt(np.mean(x ** 2))
print(f"{'wave':13s} {'attack':12s} {'aroot':>5s} {'lroot':>5s} {'ms':>4s} {'cA':>5s} {'cOOH/A':>6s} {'cB':>5s} {'harmA':>5s} {'harmL':>5s} {'pk/rmsL dB':>9s}")
for name, la, lb, brate, boff, atk, lvl in gp.WAVES:
    if atk is None: continue
    if isinstance(atk, tuple):
        _, _, root, sr, x = FW[atk[1]]; aname = atk[1] + "*"
        root = 60 if root is None else root
    else:
        x, sr, root = S[atk]["data"], S[atk]["sr"], S[atk]["root"]; aname = atk
    A, B = S[la], S[lb]
    f0 = gp.note_hz(A["root"]) * 2 ** ((root - A["root"]) / 12)   # loop's fundamental at the attack's root
    body = x[int(0.01 * sr):]
    print(f"{name:13s} {aname:12s} {root:5d} {A['root']:5d} {1000*len(x)/sr:4.0f} {cent(x,sr):5.0f} {cent(A['data'],A['sr']):6.0f} {cent(B['data'],B['sr']):5.0f}"
          f" {harm_share(body,sr,gp.note_hz(root)):5.2f} {harm_share(A['data'],A['sr'],gp.note_hz(A['root'])):5.2f}"
          f" {20*np.log10(np.abs(x).max()*lvl/127/rms(A['data'])):9.1f}")
