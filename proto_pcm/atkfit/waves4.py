"""Round 5: the drum family shrunk (0.2 s strikes, ~0.12 s ring loops, 11-12 kHz, no twin beat) and MOONSONG /
CAIRN SPIRIT at lower rates (8-bit mu-law, as stored)."""
import sys, pathlib
here = pathlib.Path(__file__).parent
_src = open(here / "waves3.py").read().split('MODELS = {"LORE LONG"')[0]
exec(_src)
_struck = struck
SHRINK = {}


def struck(sr, root, L, partials, strike, ring_twin=0.3, onset=0.0012, sag=(0.0, 0.03), T_s=0.30, seed=7, phs=None):
    """struck() with the shrink settings in SHRINK: rate cap, ring loop length, strike length, no twin"""
    return _struck(min(sr, SHRINK["sr"]), root, SHRINK["L"], partials, strike, 0.0, onset, sag, SHRINK["T"], seed, phs)


def log_drum2(rng):
    R = [1.0, 2.04, 3.12, 4.6, 6.3]
    phs = rng.uniform(0, 2 * np.pi, len(R) + 1)
    a = struck(11025, 55, 0, [(r, p, g, d) for r, p, g, d in zip(R, [1.0, .45, .2, .1, .05], [.30, .05, 0, 0, 0],
                                                                [.11, .05, .03, .02, .012])],
               [(400, 1.5, 0.5, 0.001, 0.010)], phs=phs, seed=7)
    b = struck(11025, 55, 0, [(r, p, g, d) for r, p, g, d in zip(R, [1.0, .65, .45, .3, .2], [.30, .10, .04, 0, 0],
                                                                [.10, .05, .035, .022, .014])],
               [(700, 1.5, 0.5, 0.001, 0.010), (3000, 2.0, 0.5, 0.0002, 0.0025)], phs=phs, seed=8, onset=0.0006)
    return [a, b]


def musical_bow2(rng):
    """MUSICAL BOW at 12 kHz on a short ring (as waves3, B's mouth formant inlined)"""
    sr, root = SHRINK["sr_bright"], 55
    n, f0 = gp.loop_len(sr, gp.note_hz(root), SHRINK["L"])
    K = int(0.45 * sr / f0)
    phs = rng.uniform(0, 2 * np.pi, K + 2)
    pa = [(k * (1 + 0.0004 * k * k), 1.0 / k ** 0.8, 0.30 / k ** 2.2, 0.35 / k ** 0.6) for k in range(1, K)]
    a = _struck(sr, root, SHRINK["L"], pa, [(4500, 1.5, 0.4, 0.0002, 0.0015), (700, 1.2, 0.2, 0.0005, 0.01)],
                0.0, 0.0003, (0.0, 0.03), SHRINK["T"], 29, phs)
    nt = Note(sr, n, SHRINK["T"])
    df = sr / n
    y = np.zeros(nt.N)
    fc = 600.0 * 3.0 ** np.clip(nt.t / 0.07, 0, 1) * (1 - 0.35 * nt.ramp(0.08, 0.19))
    for j, (r, peak, ring, dec) in enumerate(pa):
        f = round(f0 * r / df) * df
        if f >= 0.45 * sr:
            continue
        mouth = 0.15 + np.exp(-0.5 * (np.log2(f / fc) / 0.35) ** 2)
        y += (1.6 * ring + (peak - 1.6 * ring) * np.exp(-nt.t / dec) * nt.gone(0.08)) * mouth * np.sin(2 * np.pi * f * nt.tau + phs[j])
    y *= 1.0 - np.exp(-nt.t / 0.0003)
    y += 0.3 * nt.noise(np.random.default_rng(30), lambda f: ga_band(f, 3500, 1.5)) * ga.env_ad(nt.t, 0.0002, 0.0015)
    return [a, (y, nt)]


DRUMS = [  # name, model, root, ring loop s, rate cap
    ("LOG DRUM", log_drum2, 55, 0.12, 11025), ("FRAME DRUM", frame_drum, 45, 0.12, 11025),
    ("POT DRUM", pot_drum, 43, 0.12, 11025), ("LITHOPHONE", lithophone, 69, 0.10, 11025),
    ("MENHIR", menhir, 36, 0.15, 11025), ("MUSICAL BOW", musical_bow2, 55, 0.12, 12000)]

if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    reel = []
    tot = 0
    print("DRUMS (strike 0.2 s + ring loop, A and B each)")
    for name, fn, root, L, cap in DRUMS:
        SHRINK.update(sr=cap, sr_bright=cap, L=L, T=0.20)
        wv = finish_pair(fn(np.random.default_rng(4242)), 0.0)
        size = 0
        for s, tag in zip(wv, "AB"):
            x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
            d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
            T, n = len(s["atk"]), len(s["loop"])
            size += T + n + 1
        tot += size
        print(f"  {name:12s} {wv[0]['sr']:6.0f} Hz  strike {len(wv[0]['atk'])} B, ring {len(wv[0]['loop'])} B "
              f"({len(wv[0]['loop']) / wv[0]['sr']:.2f} s)  -> {size / 1024:.1f} KB with A and B")
        tune = [root + i for i in (0, 7, 3, 12, 7, 0, -5, 0)] if root >= 48 else [root + 12 + i for i in (0, 7, 3, 12, 7, 0, -5, 0)]
        fn_ = name.lower().replace(" ", "_")
        for scan, tag in ((0.0, "A"), (1.0, "B")):
            for etag, env, gate in (("_decay", (0.001, 0.22, 0.0, 0.15), 0.42), ("_ring", (0.001, 0.3, 1.0, 0.3), 0.55)):
                parts = []
                for m in tune:
                    n_out = int((gate + 0.6) * SR)
                    e = gp.adsr(n_out, env[0], env[1], env[2], int(gate * SR), env[3])
                    parts.append(play(wv, root, m, n_out, scan) * e)
                hop = int((gate + 0.1) * SR)
                o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
                for i, xx in enumerate(parts):
                    o[i * hop:i * hop + len(xx)] += xx
                o = o / np.abs(o).max() * 0.8
                ga.write_wav(OUT / f"drum_{fn_}_scan{tag}{etag}.wav", o, SR)
                if etag == "_decay":
                    reel += [o, np.zeros(int(0.4 * SR))]
    ga.write_wav(OUT / "drums_reel_all.wav", np.concatenate(reel), SR)
    print(f"  all six drums: {tot / 1024:.1f} KB")

    # MOONSONG and CAIRN SPIRIT at lower rates
    mu = ga.mulaw_roundtrip
    ga.mulaw_roundtrip = lambda x: x
    RAW = gp.build_samples(rerate=False)
    ga.mulaw_roundtrip = mu
    NOW = gp.build_samples()
    LOW = dict(NOW)
    TARGET = {"MOON HUM": 6000, "MOON HALO": 8000, "CAIRN SPIRIT": 7000, "CAIRN HALO": 12000}
    print("LOWER RATES")
    for nm, r in TARGET.items():
        raw = RAW[nm]
        x, sr2 = gp.resample_loop(raw["data"], raw["sr"], r)
        x = ga.mulaw_roundtrip(x / np.abs(x).max() * gp.PEAK)
        LOW[nm] = dict(NOW[nm], data=x, sr=sr2)
        print(f"  {nm:12s} {NOW[nm]['sr']:6d} Hz {len(NOW[nm]['data']):6d} B  ->  {sr2:6d} Hz {len(x):6d} B")
    W = {w[0]: w for w in gp.WAVES}
    for name, root, tune, gate in (("MOONSONG", 45, [45, 52, 57, 52], 2.4), ("CAIRN SPIRIT", 57, [57, 60, 64, 62], 2.0)):
        for scan, tag in ((0.0, "A"), (1.0, "B")):
            outs = []
            for S_ in (NOW, LOW):
                rng = np.random.default_rng(3)
                hop = int((gate + 0.1) * SR); parts = []
                for m in tune:
                    n_out = int((gate + 0.6) * SR)
                    parts.append(gp.play_wave(S_, W[name], m, n_out, np.full(n_out, scan), rng) *
                                 gp.adsr(n_out, 0.1, 0.3, 1.0, int(gate * SR), 0.3))
                o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
                for i, xx in enumerate(parts):
                    o[i * hop:i * hop + len(xx)] += xx
                outs.append(o)
            pk = max(np.abs(o).max() for o in outs) / 0.8
            fn_ = name.lower().replace(" ", "_")
            ga.write_wav(OUT / f"rate_{fn_}_scan{tag}_1_now.wav", outs[0] / pk, SR)
            ga.write_wav(OUT / f"rate_{fn_}_scan{tag}_2_lower.wav", outs[1] / pk, SR)
