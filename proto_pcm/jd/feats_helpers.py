import numpy as np
x = np.load('x.npy'); sr = 44100
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


