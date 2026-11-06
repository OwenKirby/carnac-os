import sys; sys.path.insert(0, ".")
import numpy as np; from png import write_png
x = np.load("x.npy"); sr = 44100; db = np.load("db.npy")
st = list(np.load("starts.npy"))
if len(st) == 106:
    for a, b in [(230.8, 236.7), (278.1, 284.0)]:          # one wave hidden in each long gap: the biggest rise
        i0, i1 = int((a + 2.3) / 0.005), int((b - 0.4) / 0.005)
        r = db[i0 + 6:i1] - db[i0:i1 - 6]
        st.append((i0 + 6 + np.argmax(r)) * 0.005 - 0.02)
st = np.array(sorted(st)); np.save("starts.npy", st); print(len(st), "waves")
FONT = {"0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111",
        "4": "101101111001001", "5": "111100111001111", "6": "111100111101111", "7": "111001001001001",
        "8": "111101111101111", "9": "111101111001111"}


def text(img, s, y, x0, col):
    for c in s:
        g = np.array([int(v) for v in FONT[c]]).reshape(5, 3)
        for yy in range(5):
            for xx in range(3):
                if g[yy, xx]:
                    img[y + 2 * yy:y + 2 * yy + 2, x0 + 2 * xx:x0 + 2 * xx + 2] = col
        x0 += 8


def tile(t0, dur=0.8, W=96, H=110, nfft=1024, fmin=60, fmax=12000):
    a = int(t0 * sr); seg = x[a:a + int(dur * sr) + nfft]
    hop = int(dur * sr / W); win = np.hanning(nfft)
    fr = np.stack([seg[i * hop:i * hop + nfft] * win for i in range(W)])
    S = 20 * np.log10(np.abs(np.fft.rfft(fr, axis=1)) + 1e-7); f = np.fft.rfftfreq(nfft, 1 / sr)
    fy = fmin * (fmax / fmin) ** (np.arange(H)[::-1] / (H - 1))
    im = np.stack([np.interp(fy, f, s) for s in S], 1); ref = im.max()
    return np.clip((im - ref + 70) / 70, 0, 1) ** 1.3 * 255


cols = 12; rows = int(np.ceil(len(st) / cols)); TW, TH = 100, 126
img = np.zeros((rows * TH, cols * TW, 3), np.uint8)
for i, t in enumerate(st):
    r, c = divmod(i, cols); g = tile(t - 0.02)
    img[r * TH + 14:r * TH + 14 + 110, c * TW:c * TW + 96] = g[..., None]
    text(img, str(i + 1), r * TH + 2, c * TW + 2, [255, 200, 0])
write_png("grid.png", img)


def flat(s):
    P = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2 + 1e-12; P = P[5:len(P) // 2]
    return np.exp(np.mean(np.log(P))) / np.mean(P)


def cent(s):
    P = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2; f = np.fft.rfftfreq(len(s), 1 / sr)
    return (P * f).sum() / P.sum()


rows_ = []
for i, t in enumerate(st):
    a = int((t - 0.03) * sr); seg = x[a:a + int(0.78 * sr)]
    hop = int(0.005 * sr)
    e = np.array([np.sqrt(np.mean(seg[j:j + hop] ** 2)) for j in range(0, len(seg) - hop, hop)])
    pk = e.argmax(); on = np.argmax(e > e.max() * 0.1)
    t90 = (np.argmax(e > e.max() * 0.9) - on) * 5
    sus = 20 * np.log10(np.mean(e[110:140]) / e.max() + 1e-9)
    s0 = seg[on * hop:on * hop + 2048]; s1 = seg[int(0.45 * sr):int(0.45 * sr) + 4096]
    rows_.append((i + 1, t, t90, pk * 5 - on * 5, sus, flat(s0), flat(s1), cent(s0), cent(s1)))
np.save("feats.npy", np.array(rows_))
print(" #   time  rise90 peak@ sus@0.6  flat0 flat.5  cent0  cent.5")
for r in rows_:
    print(f"{r[0]:3d} {r[1]:6.1f} {r[2]:5.0f} {r[3]:5.0f} {r[4]:7.1f}  {r[5]:.3f} {r[6]:.3f} {r[7]:6.0f} {r[8]:6.0f}")
