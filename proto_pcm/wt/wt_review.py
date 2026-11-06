import sys, numpy as np, pathlib
D = pathlib.Path(sys.argv[1])
NAMES = ["Foundations", "Hearth", "Barrow", "Trilithon", "Ley Line", "Solstice", "Moonstone", "Star Chart",
         "Druid Chant", "Overtone Rite", "Votive Choir", "Bone Flute", "Erosion", "Frost Shatter", "Dust"]
img = np.frombuffer((D / "wavetables_image.bin").read_bytes()[32:], "<i2").reshape(14, 8, 1024)[:, :, :512] / 32768
fnd = np.frombuffer((D / "foundations_bank.bin").read_bytes()[32:], "<i2").reshape(1, 8, 1024)[:, :, :512] / 32768
B = np.concatenate([fnd, img])                                  # 15 banks x 8 frames x 512 (the full-band level)
H = 96
spec = np.abs(np.fft.rfft(B, axis=2))[:, :, 1:H + 1]
db = 20 * np.log10(spec / spec.max(axis=2, keepdims=True) + 1e-6).clip(-60, 0)
def dist(a, b): return np.sqrt(np.mean((a - b) ** 2))         # dB RMS over harmonics 1..96
noise = []
for b in range(15):                                             # noisy banks: energy between harmonics can't be seen
    pass
print(f"{'bank':14s} {'SCAN range':>10s} {'step':>5s} {'sine-ish':>8s} {'nearest other bank (frame dist)':>34s} {'unique':>6s} {'centroid h':>12s}")
rows = []
for b in range(15):
    fr = db[b]
    rng_ = dist(fr[0], fr[7]); step = np.mean([dist(fr[i], fr[i + 1]) for i in range(7)])
    p = spec[b] ** 2; sine = int(np.sum(p[:, 0] / p.sum(1) > 0.9))
    cen = (p * np.arange(1, H + 1)).sum(1) / p.sum(1)
    mins = []
    near = {}
    for i in range(8):
        best = (1e9, None)
        for c in range(15):
            if c == b: continue
            for j in range(8):
                d = dist(fr[i], db[c, j])
                if d < best[0]: best = (d, c)
        mins.append(best[0]); near[best[1]] = near.get(best[1], 0) + 1
    nb = max(near, key=near.get)
    rows.append((b, rng_, step, sine, np.mean(mins), nb))
    print(f"{NAMES[b]:14s} {rng_:10.1f} {step:5.1f} {sine:8d} {NAMES[nb] + f' ({np.mean(mins):.1f} dB, {near[nb]}/8)':>34s} {np.mean(mins):6.1f} {cen.min():5.1f}-{cen.max():5.1f}")
# identical / near-identical frame pairs across the whole set
pairs = []
for b in range(15):
    for i in range(8):
        for c in range(b, 15):
            for j in range(8):
                if (c, j) <= (b, i): continue
                d = dist(db[b, i], db[c, j])
                if d < 3.0: pairs.append((d, f"{NAMES[b]} {i+1}", f"{NAMES[c]} {j+1}"))
print("\nnear-duplicate frames (< 3 dB):", len(pairs))
for d, a, b in sorted(pairs)[:25]: print(f"  {d:4.1f} dB  {a}  ~  {b}")
np.save(D.parent.parent.joinpath("x.npy") if False else "wt/B.npy", B)
