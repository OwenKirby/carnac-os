import sys, pathlib, numpy as np
sys.path.insert(0, sys.argv[1]); sys.path.insert(0, str(pathlib.Path(__file__).parent))
OUT = pathlib.Path(sys.argv[2]); OUT.mkdir(parents=True, exist_ok=True)
import gen_pcm_waves as gp, gen_attacks as ga
import brr

mu = ga.mulaw_roundtrip
ga.mulaw_roundtrip = lambda x: x                      # the float sources, before the store's 8 bits
SRC = gp.build_samples(rerate=True)                   # at today's store rates
RAW = gp.build_samples(rerate=False)                  # at the recipes' rates (for lower-rate candidates)
ga.mulaw_roundtrip = mu
RATES = [6000, 7000, 8000, 9000, 10000, 11025, 12000, 13000, 14000, 16000]


def snr(ref, d):
    return 10 * np.log10(np.sum(ref ** 2) / max(np.sum((d - ref) ** 2), 1e-20))


def seg_snr10(ref, d, n=256):
    """the worst tenth of the audible 256-sample stretches: where hiss shows (quiet tails, sparse ticks)"""
    pk = np.abs(ref).max(); v = []
    for j in range(0, len(ref) - n, n):
        r = ref[j:j + n]
        if np.sqrt(np.mean(r ** 2)) > pk * 10 ** (-50 / 20):
            v.append(snr(r, d[j:j + n]))
    return np.percentile(v, 10) if v else float("nan")


def lowest_rate(x, sr, loop):
    """the lowest candidate rate whose cut loses < -35 dB of the energy (what resample_loop's roll-off removes),
    never above the rate it has now"""
    X = np.abs(np.fft.rfft(x)) ** 2; f = np.fft.rfftfreq(len(x), 1 / sr); tot = X.sum()
    for r in RATES:
        if r >= sr:
            return sr
        if X[f > 0.45 * r].sum() / tot < 10 ** (-35 / 10):
            return r
    return sr


def to_rate(x, sr, r, loop):
    if r >= sr:
        return x, sr
    return gp.resample_loop(x, sr, r) if loop else (ga.resample(x, sr, r), r)


rows = []
LOW, B4 = {}, {}
print(f"{'sample':14s} {'rate':>6s} {'mu SNR':>6s} {'4b SNR':>6s} {'4b worst10%':>11s} | {'low rate':>8s} {'4b SNR':>6s} "
      f"{'worst10%':>8s} | bytes mu / 4b / 4b-low")
tot = np.zeros(3)
for name, s in SRC.items():
    loop = s["kind"] == gp.K_LOOP
    x = s["data"] / np.abs(s["data"]).max() * gp.PEAK; sr = s["sr"]
    xm = mu(x)
    x16, sr16 = brr.pad16_loop(x, sr) if loop else (brr.pad16_once(x), sr)
    d4, b4 = brr.encode(x16, loop)
    raw = RAW[name]
    r = lowest_rate(raw["data"], raw["sr"], loop)
    r = min(r, sr)
    xl, srl = to_rate(raw["data"], raw["sr"], r, loop)
    xl = xl / np.abs(xl).max() * gp.PEAK
    xl16, srl16 = brr.pad16_loop(xl, srl) if loop else (brr.pad16_once(xl), srl)
    dl, bl = brr.encode(xl16, loop)
    B4[name] = dict(s, data=d4, sr=sr16)
    LOW[name] = dict(s, data=dl, sr=srl16)
    print(f"{name:14s} {sr:6.0f} {snr(x, xm):6.1f} {snr(x16, d4):6.1f} {seg_snr10(x16, d4):11.1f} | {srl16:8.0f} "
          f"{snr(xl16, dl):6.1f} {seg_snr10(xl16, dl):8.1f} | {len(x):6d} / {b4:6d} / {bl:6d}")
    tot += [len(x), b4, bl]
print(f"PCM samples total: mu-law {tot[0]:.0f} B, 4-bit {tot[1]:.0f} B ({tot[1]/tot[0]:.2f}), "
      f"4-bit at lower rates {tot[2]:.0f} B ({tot[2]/tot[0]:.2f})")

# the attacks (transients: the worst case for 4 bits), as stored today
print("\nfirmware attacks (sharp transients):")
at = np.zeros(2)
for name, grp, root, sr, x in ga.build_all():
    x16 = brr.pad16_once(x)
    d4, b4 = brr.encode(x16, False)
    print(f"  {name:12s} {sr:6d}  4b SNR {snr(x16, d4):5.1f}  worst10% {seg_snr10(x16, d4):5.1f}  {len(x):5d} -> {b4:5d} B")
    at += [len(x), b4]
print(f"  attacks: {at[0]:.0f} -> {at[1]:.0f} B")

# demos: today vs 4-bit vs 4-bit at the lower rates
W = {w[0]: w for w in gp.WAVES}
TUNE = {"CHOIR": (57, [57, 60, 64, 62, 57], 0.9), "HORNS": (48, [43, 48, 50, 48, 43], 0.8),
        "PAN FLUTE": (72, [74, 76, 79, 81, 76], 0.6), "MOONSONG": (45, [45, 52, 45], 2.0),
        "RAINSTICK": (60, [60, 60], 2.0), "WOOD CHIMES": (60, [60, 67], 2.0), "LORE": (60, [60, 55], 2.0),
        "SINGING STN": (57, [50, 57, 53], 1.2)}
SR = gp.SR


def phrase(samples, wave, tune, gate, scan):
    rng = np.random.default_rng(3)
    hop = int((gate + 0.1) * SR); parts = []
    for m in tune:
        n = int((gate + 0.5) * SR)
        parts.append(gp.play_wave(samples, wave, m, n, np.full(n, scan), rng) *
                     gp.adsr(n, 0.005, 0.3, 1.0, int(gate * SR), 0.2))
    o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
    for i, p in enumerate(parts):
        o[i * hop:i * hop + len(p)] += p
    return o


NOW = gp.build_samples()
for name, (root, tune, gate) in TUNE.items():
    for scan, tag in ((0.0, "A"), (1.0, "B")):
        vs = [phrase(S_, W[name], tune, gate, scan) for S_ in (NOW, B4, LOW)]
        pk = max(np.abs(v).max() for v in vs) / 0.8
        fn = name.lower().replace(" ", "_")
        for v, lab in zip(vs, ("1_mulaw", "2_4bit", "3_4bit_lowrate")):
            ga.write_wav(OUT / f"{fn}_scan{tag}_{lab}.wav", v / pk, SR)

# SCAN reuse: B as the same loop played faster (no second loop stored)
REUSE = {"LORE": ("LORE", "LORE STILL", "LORE STILL", 2.0, 0.37, None, 0),
         "RAINSTICK": ("RAINSTICK", "RAIN TRICKLE", "RAIN TRICKLE", 2.0, 0.37, ("fw", "SEED RATTLE"), 90),
         "WOOD CHIMES": ("WOOD CHIMES", "WOOD LOOP", "WOOD LOOP", 2.0, 0.37, "WOOD SWEEP", 110)}
for name, wv in REUSE.items():
    root, tune, gate = TUNE[name]
    for scan, tag in ((0.5, "mid"), (1.0, "B")):
        vs = [phrase(NOW, W[name], tune, gate, scan), phrase(NOW, wv, tune, gate, scan)]
        pk = max(np.abs(v).max() for v in vs) / 0.8
        fn = name.lower().replace(" ", "_")
        ga.write_wav(OUT / f"reuse_{fn}_scan{tag}_1_two_loops.wav", vs[0] / pk, SR)
        ga.write_wav(OUT / f"reuse_{fn}_scan{tag}_2_one_loop_x2.wav", vs[1] / pk, SR)
print("demos written")
