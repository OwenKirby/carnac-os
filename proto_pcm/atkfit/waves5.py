"""Round 6: SHAKER replaces MUSICAL BOW. One shake as the strike, the seeds still settling as the loop (quiet: the amp
envelope decides how long it rustles). A: a gourd of seeds, fine soft grains. B: a clay rattle of pebbles, fewer
harder clicks over the clay's own ring. Then the reel of the drum list."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "waves4.py").read().split("if __name__ == \"__main__\":")[0])


def shaker(rng, sr=14000, L=0.20, T_s=0.20):
    def seed_grain(prng):
        m = int(sr * 0.003)
        f = prng.uniform(3000, 6500)
        k = np.arange(m) / sr
        return np.sin(2 * np.pi * f * k + prng.uniform(0, 6.3)) * np.exp(-k / prng.uniform(0.0003, 0.0007))

    def pebble_click(prng):
        m = int(sr * 0.012)
        k = np.arange(m) / sr
        f1 = prng.uniform(2000, 5500)
        body = 0.5 * np.sin(2 * np.pi * 1150 * k) * np.exp(-k / 0.004)          # the clay pot answering
        return (np.sin(2 * np.pi * f1 * k) * np.exp(-k / 0.0012) + 0.5 * np.sin(2 * np.pi * f1 * 1.7 * k) *
                np.exp(-k / 0.0008) + body)

    out = []
    for grain, n_shake, swell, rate_loop, lvl_loop, seed in ((seed_grain, 260, (2.0, 0.018), 120, 0.16, 41),
                                                              (pebble_click, 45, (1.6, 0.022), 22, 0.22, 42)):
        prng = np.random.default_rng(seed)
        n = int(round(L * sr))
        # the loop: seeds still rolling, sparse, quiet
        times = np.sort(prng.uniform(0, L, prng.poisson(rate_loop * L)))
        ev = [(t, lvl_loop * prng.uniform(0.2, 1.0), grain(prng)) for t in times]
        loop = gp.circular_hits(n, sr, ev)
        nt = Note(sr, n, T_s)
        y = loop[nt.m] * nt.ramp(0.06, T_s - 0.01)
        # the shake: a burst of grains swelling in and stopping hard as the hand reverses, all gone by the handoff
        shake = np.zeros(nt.N + sr)
        ts = np.sort(prng.gamma(swell[0], swell[1], n_shake))
        for t in ts[ts < T_s - 0.02]:
            s = int(t * sr)
            g = grain(prng)
            shake[s:s + len(g)] += prng.uniform(0.3, 1.0) * g
        y += shake[:nt.N] * nt.gone(0.06)
        out.append((y, nt))
    return out


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    LIST = [d for d in DRUMS if d[0] != "MUSICAL BOW"] + [("SHAKER", shaker, 60, 0.20, 14000)]
    reel, tot = [], 0
    print("THE DRUM LIST (strike + loop, A and B each)")
    for name, fn, root, L, cap in LIST:
        SHRINK.update(sr=cap, sr_bright=cap, L=L, T=0.20)
        wv = finish_pair(fn(np.random.default_rng(4242)), 0.0)
        size = sum(len(s["atk"]) + len(s["loop"]) + 1 for s in wv)
        seams = []
        for s in wv:
            x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
            d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
            seams.append(max(d[len(s["atk"]) - 1], d[len(s["atk"]) + len(s["loop"]) - 1]) / typ)
        tot += size
        print(f"  {name:11s} {wv[0]['sr']:6.0f} Hz  strike {len(wv[0]['atk'])} B, loop {len(wv[0]['loop'])} B  "
              f"{size / 1024:4.1f} KB  (worst seam {max(seams):.2f} x a big step)")
        tune = [root + i for i in (0, 7, 3, 12, 7, 0, -5, 0)] if root >= 48 else [root + 12 + i for i in (0, 7, 3, 12, 7, 0, -5, 0)]
        if name == "SHAKER":
            tune = [60, 60, 62, 60, 60, 60, 62, 60]
        fn_ = name.lower().replace(" ", "_")
        for scan, tag in ((0.0, "A"), (1.0, "B")):
            for etag, env, gate in (("_decay", (0.001, 0.22, 0.0, 0.15), 0.42), ("_ring", (0.001, 0.3, 1.0, 0.3), 0.55)):
                if name == "SHAKER" and etag == "_decay":
                    env, gate = (0.001, 0.12, 0.0, 0.08), 0.24
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
    print(f"  the list: {tot / 1024:.1f} KB")
