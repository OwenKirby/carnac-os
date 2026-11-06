import numpy as np
on = np.load("on.npy") * 0.005; db = np.load("db.npy")
st = []
for t in on:
    if not st or t - st[-1] >= 2.2: st.append(t)
sm = np.convolve(db, np.ones(20) / 20, "same")
extra = []
for a, b in zip(st, st[1:] + [len(db) * 0.005]):
    if b - a > 4.3:
        i0, i1 = int((a + 2.3) / 0.005), int((b - 0.5) / 0.005)
        j = i0 + np.argmin(sm[i0:i1])
        k = j + np.argmax(db[j:i1] > db[j] + 15) if (db[j:i1] > db[j] + 15).any() else None
        if k: extra.append(k * 0.005)
st = sorted(st + extra)
np.save("starts.npy", np.array(st))
print(len(st), len(extra), np.round(np.percentile(np.diff(st), [0, 5, 50, 95, 100]), 2))
print(" ".join(f"{t:.1f}" for t in st))
