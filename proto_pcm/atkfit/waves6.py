"""Round 7: every wave of the list at the lowest rate its spectrum allows (energy above 0.45 x the rate under -35 dB,
the cut resample_loop makes; never above today's), attack and loop together so the seam holds. Renders A -> 50 % -> B
for each, now vs lower; prints the sizes."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "waves5.py").read().split("if __name__ == \"__main__\":")[0])
RATES = [6000, 7000, 8000, 9000, 10000, 11025, 12000, 13000, 14000, 16000]


def finish_pair_float(pair, b_db=0.0):
    """finish_pair without the mu-law (rate changes happen before the 8 bits)"""
    (ya, na), (yb, nb) = pair
    ga_ = 1.0 / np.sqrt(np.mean(ya[na.T:] ** 2))
    gb_ = 1.0 / np.sqrt(np.mean(yb[nb.T:] ** 2)) * 10 ** (b_db / 20)
    k = gp.PEAK / max(np.abs(ya).max() * ga_, np.abs(yb).max() * gb_)
    return [dict(atk=y[:nt.T] * g * k, loop=y[nt.T:] * g * k, sr=nt.sr, n=nt.n)
            for y, nt, g in ((ya, na, ga_), (yb, nb, gb_))]


def lowest(s):
    x = np.concatenate([s["atk"], s["loop"]])
    X = np.abs(np.fft.rfft(x)) ** 2; f = np.fft.rfftfreq(len(x), 1 / s["sr"]); tot = X.sum()
    for r in RATES:
        if r >= s["sr"] - 1:
            return s["sr"]
        if X[f > 0.45 * r].sum() / tot < 10 ** (-35 / 10):
            return r
    return s["sr"]


def rerate(s, target):
    if target >= s["sr"] - 1:
        return dict(s)
    loop, _ = gp.resample_loop(s["loop"], s["sr"], target)
    ratio = len(loop) / len(s["loop"])
    z = np.concatenate([s["atk"], s["loop"], s["loop"]])
    n, n2 = len(z), 2 * len(z)
    m2 = int(round(n2 * ratio))
    spec = np.fft.rfft(z, n=n2)[:m2 // 2 + 1]
    fr = np.arange(len(spec)) / (len(spec) - 1)
    spec = spec * np.where(fr < 0.9, 1.0, 0.5 * (1.0 + np.cos(np.pi * (fr - 0.9) / 0.1)))
    zr = np.fft.irfft(spec, n=m2) * (m2 / n2)
    T = int(round(len(s["atk"]) * ratio))
    return dict(atk=zr[:T], loop=loop, sr=s["sr"] * ratio, n=len(loop))


def mu(s):
    pk = max(np.abs(s["atk"]).max(), np.abs(s["loop"]).max())
    g = min(1.0, gp.PEAK / pk)
    return dict(s, atk=ga.mulaw_roundtrip(s["atk"] * g), loop=ga.mulaw_roundtrip(s["loop"] * g))


def seam(s):
    x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
    d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
    return max(d[len(s["atk"]) - 1], d[len(s["atk"]) + len(s["loop"]) - 1]) / typ


def abmid(wv_or_play, root, tune, gate, env):
    """the phrase at SCAN 0, 0.5 and 1, a second's gap between"""
    out = []
    for scan in (0.0, 0.5, 1.0):
        parts = []
        for m in tune:
            n_out = int((gate + 0.6) * SR)
            e = gp.adsr(n_out, env[0], env[1], env[2], int(gate * SR), env[3])
            parts.append(wv_or_play(m, n_out, scan) * e)
        hop = int((gate + 0.1) * SR)
        o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
        for i, xx in enumerate(parts):
            o[i * hop:i * hop + len(xx)] += xx
        out += [o, np.zeros(SR)]
    return np.concatenate(out)


SUST = (0.002, 0.3, 1.0, 0.25)
DEC = (0.001, 0.22, 0.0, 0.15)
WAVES_L = [  # name, model, root, B dB, shrink (drums), tune, gate, env
    ("CHOIR", choir, 57, 2.0, None, [57, 60, 64, 62], 1.2, SUST),
    ("PAN FLUTE", pan_flute, 72, 1.0, None, [74, 76, 79, 81, 76], 0.6, SUST),
    ("HORNS", horns, 48, 3.0, None, [43, 48, 50, 48], 0.9, SUST),
    ("SINGING STN", singing_stone2, 57, 2.0, None, [50, 57, 53], 1.4, SUST),
    ("RAINSTICK", rainstick, 60, 4.0, None, [60, 60], 2.0, SUST),
    ("WIND CHIMES", wind_chimes, 60, 0.0, None, [60, 67], 2.2, SUST),
    ("LORE", lore_long, 60, 2.0, None, [60, 55], 2.6, SUST),
    ("ANGKLUNG", angklung_long, 60, 0.0, None, [60, 64, 67, 72, 67, 64], 0.45, SUST)]
for name, fn, root, L, cap in [d for d in DRUMS if d[0] != "MUSICAL BOW"] + [("SHAKER", shaker, 60, 0.20, 14000)]:
    t = [60, 60, 62, 60] if name == "SHAKER" else ([root + i for i in (0, 7, 3, 12)] if root >= 48 else
                                                   [root + 12 + i for i in (0, 7, 3, 12)])
    WAVES_L.append((name, fn, root, 0.0, (cap, L), t, 0.42 if name != "SHAKER" else 0.24, DEC))

if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    tot_now = tot_low = 0
    print(f"{'wave':12s} {'A now':>6s} {'A low':>6s} {'B now':>6s} {'B low':>6s}   KB now -> lower   seams")
    for name, fn, root, bdb, shr, tune, gate, env in WAVES_L:
        if shr:
            SHRINK.update(sr=shr[0], sr_bright=shr[0], L=shr[1], T=0.20)
        wf = finish_pair_float(fn(np.random.default_rng(4242)), bdb)
        now = [mu(s) for s in wf]
        low = [mu(rerate(s, lowest(s))) for s in wf]
        kb_now = sum(len(s["atk"]) + len(s["loop"]) + 1 for s in now) / 1024
        kb_low = sum(len(s["atk"]) + len(s["loop"]) + 1 for s in low) / 1024
        tot_now += kb_now; tot_low += kb_low
        print(f"{name:12s} {now[0]['sr']:6.0f} {low[0]['sr']:6.0f} {now[1]['sr']:6.0f} {low[1]['sr']:6.0f}   "
              f"{kb_now:5.1f} -> {kb_low:5.1f}   {max(seam(s) for s in low):.2f}")
        fn_ = name.lower().replace(" ", "_")
        a = abmid(lambda m, n, sc: play(now, root, m, n, sc), root, tune, gate, env)
        changed = any(abs(l["sr"] - s["sr"]) > 1 for l, s in zip(low, now))
        if changed:
            b = abmid(lambda m, n, sc: play(low, root, m, n, sc), root, tune, gate, env)
            pk = max(np.abs(a).max(), np.abs(b).max()) / 0.8
            ga.write_wav(OUT / f"{fn_}_1_now.wav", a / pk, SR)
            ga.write_wav(OUT / f"{fn_}_2_lower.wav", b / pk, SR)
        else:
            ga.write_wav(OUT / f"{fn_}.wav", a / np.abs(a).max() * 0.8, SR)
    print(f"total {tot_now:.1f} KB -> {tot_low:.1f} KB (attacks as prototyped; MOONSONG / CAIRN SPIRIT not included)")
    # MOONSONG and CAIRN SPIRIT (today's waves, no handoff) at their lower rates, A -> 50 % -> B
    mu_ = ga.mulaw_roundtrip
    ga.mulaw_roundtrip = lambda x: x
    RAW = gp.build_samples(rerate=False)
    ga.mulaw_roundtrip = mu_
    NOW = gp.build_samples()
    LOW = dict(NOW)
    for nm, r in {"MOON HUM": 6000, "MOON HALO": 8000, "CAIRN SPIRIT": 7000, "CAIRN HALO": 12000}.items():
        x, sr2 = gp.resample_loop(RAW[nm]["data"], RAW[nm]["sr"], r)
        LOW[nm] = dict(NOW[nm], data=ga.mulaw_roundtrip(x / np.abs(x).max() * gp.PEAK), sr=sr2)
    W = {w[0]: w for w in gp.WAVES}
    for name, root, tune, gate in (("MOONSONG", 45, [45, 52, 57], 2.4), ("CAIRN SPIRIT", 57, [57, 60, 64], 2.0)):
        outs = []
        for S_ in (NOW, LOW):
            rng = np.random.default_rng(3)
            outs.append(abmid(lambda m, n, sc: gp.play_wave(S_, W[name], m, n, np.full(n, sc), rng), root, tune, gate,
                              (0.1, 0.3, 1.0, 0.3)))
        pk = max(np.abs(o).max() for o in outs) / 0.8
        fn_ = name.lower().replace(" ", "_")
        ga.write_wav(OUT / f"{fn_}_1_now.wav", outs[0] / pk, SR)
        ga.write_wav(OUT / f"{fn_}_2_lower.wav", outs[1] / pk, SR)
    print("MOONSONG + CAIRN SPIRIT: 49.9 -> 35.3 KB")
