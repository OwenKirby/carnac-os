import sys, numpy as np
sys.argv = ["proto.py", sys.argv[1], "x"]
g = {}; exec(open("proto.py").read().split("TUNES =")[0], g)
gp, ga = g["gp"], g["ga"]
mu = gp.build_samples()
ga.mulaw_roundtrip = lambda x: x; gp.ga.mulaw_roundtrip = lambda x: x
clean = gp.build_samples()
for name in ["PAN BREATHY", "CHOIR OOH", "HORNS SOFT", "CAIRN SPIRIT"]:
    a = g["Split"](mu[name]).air(); c = g["Split"](clean[name]).air()
    err = mu[name]["data"] - clean[name]["data"]
    print(f"{name:13s} air rms {np.sqrt(np.mean(a**2)):.4f}  clean air {np.sqrt(np.mean(c**2)):.4f}  mu-law error {np.sqrt(np.mean(err**2)):.4f}")
