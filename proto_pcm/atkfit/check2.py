import sys, numpy as np
sys.argv = ["notes.py", sys.argv[1], sys.argv[2]]
g = {}; exec(open("notes.py").read().split("TUNES = {")[0], g)
for name in ["pan_flute", "choir", "horns", "singing_stone", "rainstick", "log_drum"]:
    for s, tag in zip(g["finish_pair"](g[name](np.random.default_rng(4242))), "AB"):
        x = np.concatenate([s["atk"], s["loop"], s["loop"][:4]])
        d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
        T, n = len(s["atk"]), len(s["loop"])
        print(f"{name:14s}{tag} seam jump {d[T-1]/typ:4.2f}  wrap jump {d[T+n-1]/typ:4.2f}  (x 99th pct step)  "
              f"atk peak {np.abs(s['atk']).max():.2f} loop rms {np.sqrt(np.mean(s['loop']**2)):.3f}")
