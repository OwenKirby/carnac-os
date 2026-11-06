import sys, runpy, numpy as np
sys.argv = ["proto.py", sys.argv[1], sys.argv[2]]
g = {}
src = open("proto.py").read().split("TUNES =")[0]
exec(src, g)
gp = g["gp"]
for w in gp.WAVES:
    if w[0] not in g["RECIPES"]: continue
    for (x, end, s) in g["build"](w):
        d = s["data"]; prev = g["rd"](d, np.array([end - 1.0]))[0]
        print(w[0], f"last {x[-1]:+.3f} loop {prev:+.3f} rms {np.sqrt(np.mean(d**2)):.3f}")
