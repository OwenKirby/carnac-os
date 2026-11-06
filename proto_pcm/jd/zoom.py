import sys; sys.path.insert(0, ".")
import numpy as np; from png import write_png
import feats_helpers as fh
x = fh.x; sr = 44100; st = np.load("starts.npy")
sel = [int(v) for v in sys.argv[1].split(",")]
cols = 6; TW, TH = 200, 196
img = np.zeros((int(np.ceil(len(sel) / cols)) * TH, cols * TW, 3), np.uint8)
for i, w in enumerate(sel):
    t = st[w - 1]; a = int((t - 0.05) * sr); seg = x[a:a + int(1.0 * sr)]
    h = 220; e = np.array([np.abs(seg[j:j + h]).max() for j in range(0, len(seg) - h, h)])
    on = np.argmax(e > e.max() * 0.15) * h / sr
    g = fh.tile(t - 0.05 + on - 0.02, dur=0.40, W=196, H=180, nfft=512, fmin=80, fmax=14000)
    r, c = divmod(i, cols)
    img[r * TH + 14:r * TH + 194, c * TW:c * TW + 196] = g[..., None]
    for ms in (50, 100, 200, 300):
        img[r * TH + 14:r * TH + 20, c * TW + int((ms + 20) / 400 * 196)] = [255, 60, 60]
    fh.text(img, str(w), r * TH + 2, c * TW + 2, [255, 200, 0])
write_png(sys.argv[2], img)
