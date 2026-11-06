import sys; sys.path.insert(0, ".")
import numpy as np; from png import *
x = np.load("x.npy"); sr = 44100
for i, t0 in enumerate(range(0, 330, 30)):
    write_png(f"ov{i:02d}.png", spec(x, sr, t0, min(t0 + 30, len(x) / sr), px_s=40, H=220))
