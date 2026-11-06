import sys, wave, numpy as np
w = wave.open(sys.argv[1]); sr = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(float) / 32768
np.save("x.npy", x)
hop = int(0.005 * sr)
e = np.sqrt(np.add.reduceat(x[: len(x) // hop * hop] ** 2, np.arange(0, len(x) // hop * hop, hop)) / hop)
db = 20 * np.log10(e + 1e-6)
np.save("db.npy", db)
# onsets: rise of > 12 dB over 20 ms from a quiet-ish floor
on = []
for i in range(4, len(db)):
    if db[i] - db[i - 4] > 12 and db[i] > -40 and (not on or i - on[-1] > 16):
        on.append(i)
on = np.array(on)
print(len(on), "onsets; clip samples:", int((np.abs(x) > 0.999).sum()))
gaps = np.diff(on) * 0.005
print("gap hist (s):", np.histogram(gaps, bins=[0, .1, .2, .3, .5, .8, 1.2, 2, 4, 10])[0])
np.save("on.npy", on)
print(" ".join(f"{o*0.005:.2f}" for o in on[:80]))
