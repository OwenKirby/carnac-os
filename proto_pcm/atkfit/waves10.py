"""Round 8 drums: WOOD BLOCK, KALIMBA, LOG MARIMBA, TONGUE DRUM, as the drum family (a strike ringing into a short
loop, the amp envelope sets the length; SCAN = how it is played). Rendered A -> 50 % -> B."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "waves5.py").read().split('if __name__ == "__main__":')[0])
T_DRUM = 0.22


def wood_block(rng):
    """A hollowed block on a stand. A: a soft mallet, round 'tok' with a short ring. B: a hard stick, a dry click and
    the block's upper modes cracking."""
    R = [1.0, 2.76, 4.35, 6.1]
    phs = rng.uniform(0, 6.3, len(R) + 1)
    a = _struck(11025, 72, 0.12, [(1.0, 1.0, 0.10, 0.06), (2.76, 0.30, 0.0, 0.02), (4.35, 0.10, 0.0, 0.01),
                                 (6.1, 0.05, 0.0, 0.006)], [(1500, 2.0, 0.3, 0.0005, 0.004)], 0.0, 0.0008,
                (0.0, 0.03), T_DRUM, 61, phs)
    b = _struck(11025, 72, 0.12, [(1.0, 1.0, 0.08, 0.04), (2.76, 0.6, 0.02, 0.018), (4.35, 0.35, 0.0, 0.009),
                                 (6.1, 0.2, 0.0, 0.005)], [(3500, 2.0, 0.8, 0.0002, 0.0025)], 0.0, 0.0002,
                (0.0, 0.03), T_DRUM, 62, phs)
    return [a, b]


def log_marimba(rng):
    """A wooden bar over a gourd resonator (the marimba's 1 : 4 : 10 tuning), the resonator holding the fundamental.
    A: yarn mallets, round and long. B: hard mallets, the 4th and 10th partials bright."""
    R = [1.0, 3.93, 9.94]
    phs = rng.uniform(0, 6.3, len(R) + 1)
    a = _struck(11025, 60, 0.12, [(1.0, 1.0, 0.30, 0.35), (3.93, 0.25, 0.03, 0.08), (9.94, 0.05, 0.0, 0.02)],
                [(500, 1.5, 0.25, 0.002, 0.008)], 0.0, 0.002, (0.0, 0.03), T_DRUM, 63, phs)
    b = _struck(11025, 60, 0.12, [(1.0, 1.0, 0.26, 0.30), (3.93, 0.6, 0.08, 0.07), (9.94, 0.3, 0.02, 0.02)],
                [(2500, 1.8, 0.45, 0.0003, 0.003)], 0.0, 0.0004, (0.0, 0.03), T_DRUM, 64, phs)
    return [a, b]


def tongue_drum(rng):
    """A tuned tongue drum, the new-age one: each tongue tuned so its octave and twelfth ring with it, a long soft
    sustain in a hollow body. A: the soft pad of a palm. B: fingertips, the octave forward and a little ping."""
    R = [1.0, 2.0, 3.0, 4.5]
    phs = rng.uniform(0, 6.3, len(R) + 1)
    a = _struck(11025, 62, 0.12, [(1.0, 1.0, 0.40, 0.5), (2.0, 0.35, 0.10, 0.3), (3.0, 0.12, 0.03, 0.2),
                                 (4.5, 0.05, 0.0, 0.05)], [(300, 1.5, 0.3, 0.003, 0.012)], 0.0, 0.004,
                (0.0, 0.03), T_DRUM, 65, phs)
    b = _struck(11025, 62, 0.12, [(1.0, 1.0, 0.36, 0.45), (2.0, 0.7, 0.18, 0.3), (3.0, 0.3, 0.06, 0.2),
                                 (4.5, 0.15, 0.0, 0.05)], [(2000, 1.6, 0.35, 0.0005, 0.004)], 0.0, 0.0012,
                (0.0, 0.03), T_DRUM, 66, phs)
    return [a, b]


def kalimba(rng, sr=12000):
    """A thumb piano: a metal tine plucked over a wooden box (the tine's modes 1 and 6.27, the box answering), a
    click of the thumb. A: dry, the tine ringing clean. B: the rattle: a buzz of loose rings on the tine, riding each
    cycle, ringing on with the tine."""
    R = [1.0, 6.27, 17.55]
    phs = rng.uniform(0, 6.3, len(R) + 1)
    parts = [(1.0, 1.0, 0.30, 0.45), (6.27, 0.35, 0.02, 0.06), (17.55, 0.08, 0.0, 0.015)]
    strike = [(3500, 1.6, 0.4, 0.0002, 0.002), (700, 1.2, 0.2, 0.001, 0.01)]
    a = _struck(sr, 72, 0.12, parts, strike, 0.0, 0.0003, (0.0, 0.03), T_DRUM, 67, phs)
    yb, nt = _struck(sr, 72, 0.12, parts, strike, 0.0, 0.0003, (0.0, 0.03), T_DRUM, 68, phs)
    n, f0 = gp.loop_len(sr, gp.note_hz(72), 0.12)
    noise = gp.periodic_noise(n, sr, np.random.default_rng(69), lambda f: ga_band(f, 3000, 1.6) * (f > 800) * (f < 0.45 * sr))
    pulse = np.maximum(0, np.sin(2 * np.pi * f0 * nt.tau)) ** 8                        # the rings hit each cycle
    buzz_env = 0.22 + (0.8 - 0.22) * np.exp(-nt.t / 0.15) * nt.gone(0.1)
    yb = yb + buzz_env * noise[nt.m] * pulse * 0.8 * (1 - np.exp(-nt.t / 0.002))
    return [a, (yb, nt)]


NEW = [("WOOD BLOCK", wood_block, 72), ("KALIMBA", kalimba, 72), ("LOG MARIMBA", log_marimba, 60),
       ("TONGUE DRUM", tongue_drum, 62)]

if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    reel, tot = [], 0
    for name, fn, root in NEW:
        w = finish_pair(fn(np.random.default_rng(4242)), 0.0)
        size = 0; seams = []
        for s in w:
            x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
            d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
            T, n = len(s["atk"]), len(s["loop"])
            size += T + n + 1
            seams.append(max(d[T - 1], d[T + n - 1]) / typ)
        tot += size
        print(f"{name:12s} {w[0]['sr']:6.0f} Hz  strike {len(w[0]['atk'])} B + ring {len(w[0]['loop'])} B  "
              f"{size / 1024:4.1f} KB  (worst seam {max(seams):.2f})")
        tune = [root + i for i in (0, 4, 7, 12, 7, 4, 2, 0)]
        fn_ = name.lower().replace(" ", "_")
        for etag, env, gate in (("_decay", (0.001, 0.35, 0.0, 0.2), 0.35), ("_ring", (0.001, 0.3, 1.0, 0.4), 0.5)):
            out = []
            for scan in (0.0, 0.5, 1.0):
                parts = []
                for m in tune:
                    n_out = int((gate + 0.8) * SR)
                    e = gp.adsr(n_out, env[0], env[1], env[2], int(gate * SR), env[3])
                    parts.append(play(w, root, m, n_out, scan) * e)
                hop = int((gate + 0.08) * SR)
                o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
                for i, xx in enumerate(parts):
                    o[i * hop:i * hop + len(xx)] += xx
                out += [o, np.zeros(int(0.6 * SR))]
            o = np.concatenate(out); o = o / np.abs(o).max() * 0.8
            ga.write_wav(OUT / f"drum_{fn_}{etag}_A_mid_B.wav", o, SR)
            if etag == "_decay":
                reel += [o, np.zeros(int(0.5 * SR))]
    ga.write_wav(OUT / "new_drums_reel.wav", np.concatenate(reel), SR)
    print(f"four new drums: {tot / 1024:.1f} KB")
