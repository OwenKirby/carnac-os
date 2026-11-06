"""Round 4: LORE and ANGKLUNG with ~1.2 s loops at lower rates, and the struck family (attack + ringing loop, the amp
envelope sets the length): FRAME DRUM, POT DRUM, LITHOPHONE, MENHIR, MUSICAL BOW. SCAN = how it is played."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "notes.py").read().split("TUNES = {")[0])
w2 = open(here / "waves2.py").read()
exec(w2.split("TUNES = {\n    \"ANGKLUNG\"")[0].split("exec(src)")[1])


# ------------------------------------------------------------------------------- the long textures --- #

def angklung_long(rng, sr=12000, cycles=(8, 10)):
    """ANGKLUNG as before, on a loop of 8 (A) / 10 (B) shakes: ~1.14 s before the knock pattern repeats."""
    root = 60
    f0 = gp.note_hz(root)

    def knock(f, hard, prng):
        dur = 0.32
        t = ga.t_axis(sr, dur)
        ring = prng.uniform(0.11, 0.17)
        fs = [f, f * 1.004, f * 3.02, f * 5.1]
        am = [1.0, 0.35, 0.10 + 0.25 * hard, 0.03 + 0.08 * hard]
        dc = [ring, ring * 0.8, ring * 0.3, ring * 0.15]
        keep = [i for i, x in enumerate(fs) if x < 0.45 * sr]
        body = ga.modes(sr, dur, [fs[i] for i in keep], [am[i] for i in keep], [dc[i] for i in keep], prng, onset=0.0006)
        clack = ga.band_noise(sr, len(t), prng, 1800 + 1200 * hard, 1.6, hi=0.45 * sr) * ga.env_ad(t, 0.0003, 0.002 + 0.001 * hard)
        return body / np.abs(body).max() + (0.35 + 0.35 * hard) * clack

    def player(prng, shake_hz, cyc, hard, detune_c):
        L = cyc / shake_hz
        ev = []
        for ratio, lvl, lag in ((1.0, 1.0, 0.0), (2.0 * 2 ** (3 / 1200), 0.7, prng.uniform(0.012, 0.025))):
            f = f0 * ratio * 2 ** (detune_c / 1200)
            for c in range(cyc):
                for end, acc in ((0.0, 1.0), (0.5, 0.55)):
                    t = (c + end) / shake_hz + lag + prng.normal(0, 0.005)
                    ev.append((t % L, lvl * acc * 10 ** (prng.uniform(-3.5, 0) / 20), knock(f, hard, prng)))
        return L, ev

    out = []
    for hard, shake, cyc, second, seed in ((0.0, 7.0, cycles[0], False, 11), (1.0, 8.75, cycles[1], True, 12)):
        prng = np.random.default_rng(seed)
        L, ev = player(prng, shake, cyc, hard, 0.0)
        if second:
            _, ev2 = player(prng, shake, cyc, hard, 7.0)
            ev += [(t, a * 0.7, s) for t, a, s in ev2]
        n = int(round(L * sr))
        nt = Note(sr, n, 0.42)
        first = min(ev, key=lambda e: e[0])[0]
        shift = (first + L - (nt.Ts % L)) % L
        ev_s = [((t - shift) % L, a, s) for t, a, s in ev]
        loop = gp.circular_hits(n, sr, ev_s)
        y = np.zeros(nt.N + sr)
        for t, a, snd in ev_s:
            for j in range(1, 4):
                p = nt.T + int(t * sr) - j * n
                if 0 <= p < nt.T:
                    u = (p - nt.T) / sr
                    gain = 1.0 + 0.9 * np.exp(-(u + nt.Ts) / 0.05) * (u < -0.34)
                    y[p:p + len(snd)] += gain * a * snd
        y = y[:nt.N]
        y[nt.T:] = loop
        jerk = nt.noise(prng, lambda f: ga_band(f, 1500 + 1000 * hard, 2.0)) * ga.env_ad(nt.t, 0.0005, 0.012) * nt.gone(0.2)
        y += (0.6 + 0.4 * hard) * jerk * np.abs(loop).max()
        out.append((y, nt))
    return out


def lore_long(rng, srs=(12000, 10000), L=1.2, ramp=(0.12, 0.33), breeze=(0.25, 0.12), wood=0.0):
    """The refined LORE on ~1.2 s loops, A at 12 kHz and B at 10 kHz (what their spectra allow). wood (0..1,
    2026-11-05): hollow wooden tubes, the knock louder and rounder, the rings shorter, the metallic twin down."""
    root = 60
    out = []
    for (rate, soft, short, long_p, cluster, gain), sr, seed in zip(
            ((16, (0.002, 0.007), (0.02, 0.05), 0.4, 0.4, 1.0), (30, (0.003, 0.008), (0.015, 0.035), 0.3, 0.45, 1.25)),
            srs, (gp.SEED_BASE + 30, gp.SEED_BASE + 31)):
        prng = np.random.default_rng(seed)
        n, f0 = gp.loop_len(sr, gp.note_hz(root), L)
        tubes = [(3, 0.8), (10 / 3, 0.5), (4, 1.0), (4.4, 0.4), (5.25, 0.8), (6, 0.4), (7, 0.6), (10.5, 0.5)]
        w = np.array([g for _, g in tubes]) / sum(g for _, g in tubes)

        def tube(ratio, sft, ring):
            f = f0 * ratio * (1 + prng.uniform(-0.002, 0.002))
            ring = ring * (1 - 0.45 * wood)
            dur = min(0.6, 6 * ring + 0.02)
            fs, amps, decs = [f, f * 1.003, f * 2.0, f * 2.76], [1.0, prng.uniform(0.3, 0.6) * (1 - 0.7 * wood), 0.12, 0.15 * (1 - 0.6 * wood)], \
                [ring, ring * 0.8, ring * 0.5, ring * 0.25]
            keep = [i for i, x in enumerate(fs) if x < 0.45 * sr]
            y = ga.modes(sr, dur, [fs[i] for i in keep], [amps[i] for i in keep], [decs[i] for i in keep], prng, onset=sft)
            t = ga.t_axis(sr, dur)
            knock = ga.band_noise(sr, len(t), prng, min((2.2 - 0.6 * wood) * f, 0.4 * sr), 1.4, hi=0.45 * sr) \
                * ga.env_ad(t, sft * 0.5, 0.003 + 0.006 * wood)
            return y / np.abs(y).max() + (0.06 + 0.34 * wood) * knock / np.abs(knock).max()

        def stream(T_len, r, long_pp, cl, t0=0.0):
            ev, t = [], t0
            while t < T_len:
                j = prng.choice(len(tubes), p=w)
                ring = prng.uniform(0.10, 0.24) if prng.uniform() < long_pp else prng.uniform(*short)
                ev.append((t, 10 ** (prng.uniform(-10, 0) / 20), tube(tubes[j][0], prng.uniform(*soft), ring)))
                if prng.uniform() < cl:
                    k = (j + prng.choice([-1, 1])) % len(tubes)
                    ev.append((t + prng.uniform(0.006, 0.02), 10 ** (prng.uniform(-14, -5) / 20),
                               tube(tubes[k][0], prng.uniform(*soft), prng.uniform(*short))))
                t += max(0.008, prng.exponential(1.0 / r))
            return ev
        loop = gp.circular_hits(n, sr, stream(n / sr, rate, long_p, cluster))
        nt = Note(sr, n, 0.35)
        y = loop[nt.m] * nt.ramp(*ramp)
        b = render_events(sr, nt.N + sr, 0, stream(breeze[0], rate * 1.4, 0.6, cluster * 0.6, t0=0.003))[:nt.N]             * nt.gone(breeze[1])
        out.append((gain * (y + b), nt))
    return out


# ------------------------------------------------------------------------------------- struck family --- #

def struck(sr, root, L, partials, strike, ring_twin=0.3, onset=0.0012, sag=(0.0, 0.03), T_s=0.30, seed=7, phs=None):
    """One struck note: partials [(ratio, peak, ring, decay)] on the loop's grid, each from its strike level to its
    ring level (the ring is the loop: the amp envelope decides how long it sounds), an optional pitch sag at the
    strike, the strike's noises [(centre Hz, octaves, level, attack s, decay s)]."""
    n, f0 = gp.loop_len(sr, gp.note_hz(root), L)
    df = sr / n
    nt = Note(sr, n, T_s)
    prng = np.random.default_rng(seed)
    phs = phs if phs is not None else prng.uniform(0, 2 * np.pi, len(partials) + 1)
    y = np.zeros(nt.N)
    rg = globals().get("RING_GAIN", 1.0)             # (2026-11-05) the ring louder, for held notes: set per wave
    partials = [(r, peak, min(ring * rg, 0.9 * peak) if ring else 0.0, dec) for r, peak, ring, dec in partials]
    for j, (r, peak, ring, dec) in enumerate(partials):
        k = round(f0 * r / df)
        if k * df >= 0.45 * sr:
            continue
        f = k * df
        ph = 2 * np.pi * f * nt.tau + phs[j] + (nt.scoop(f, sag[0], sag[1]) if sag[0] else 0.0)
        a = ring + (peak - ring) * np.exp(-nt.t / dec) * nt.gone(0.1)
        y += a * np.sin(ph)
    if ring_twin and partials[0][2] > 0:                 # a slow beat in the ring: a twin of the fundamental
        k = round(f0 / df) + 1
        y += ring_twin * partials[0][2] * np.sin(2 * np.pi * k * df * nt.tau + phs[-1])
    y *= 1.0 - np.exp(-nt.t / onset)
    for c, octv, lvl, at, dc in strike:
        y += lvl * nt.noise(prng, lambda f: ga_band(f, c, octv) * (f < 0.45 * sr)) * ga.env_ad(nt.t, at, dc) * nt.gone(0.1)
    return y, nt


def frame_drum(rng):
    """A hide on a bent-wood hoop (the membrane's Bessel modes). A: the palm in the centre, a deep boom, the skin
    sagging 4 % in pitch as it relaxes; the boom rings on. B: fingers at the rim: the fundamental quiet, the
    overtone ring forward, a sharper slap."""
    R = [1.0, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16, 3.50]
    dec = [0.09, 0.065, 0.05, 0.045, 0.038, 0.032, 0.027, 0.024]
    phs = rng.uniform(0, 2 * np.pi, len(R) + 1)
    a = struck(11025, 45, 0.30, [(r, p, g, d) for r, p, g, d in zip(R, [1.0, .6, .45, .4, .3, .25, .2, .15],
                                                                  [.30, .06, .02, 0, 0, 0, 0, 0], dec)],
               [(250, 1.5, 0.7, 0.001, 0.015)], sag=(68, 0.03), phs=phs, seed=21)
    b = struck(11025, 45, 0.30, [(r, p, g, d) for r, p, g, d in zip(R, [0.4, .8, .7, .65, .5, .45, .35, .3],
                                                                  [.08, .14, .10, .07, .03, .02, 0, 0], dec)],
               [(2200, 1.6, 0.6, 0.0003, 0.004), (400, 1.5, 0.3, 0.001, 0.008)], sag=(40, 0.02), phs=phs, seed=22,
               onset=0.0005)
    return [a, b]


def pot_drum(rng):
    """A clay pot drum: a palm on the skin, the air in the pot booming (its pitch sagging into the note), the clay
    shell ringing short. A: the mouth open, the boom ringing on. B: a palm over the mouth: the boom closed and
    nasal (its 2nd and 3rd partials up), a slap."""
    R = [1.0, 2.0, 3.0, 820 / 98, 1310 / 98, 1960 / 98]
    phs = rng.uniform(0, 2 * np.pi, len(R) + 1)
    a = struck(11025, 43, 0.30, [(1.0, 1.0, 0.35, 0.10), (2.0, 0.25, 0.05, 0.05), (3.0, 0.1, 0.0, 0.03),
                                 (R[3], 0.5, 0.0, 0.02), (R[4], 0.35, 0.0, 0.015), (R[5], 0.25, 0.0, 0.01)],
               [(300, 1.5, 0.5, 0.001, 0.012)], sag=(450, 0.025), phs=phs, seed=23, onset=0.002)
    b = struck(11025, 43, 0.30, [(1.0, 0.7, 0.22, 0.08), (2.0, 0.5, 0.12, 0.06), (3.0, 0.35, 0.08, 0.04),
                                 (R[3], 0.3, 0.0, 0.015), (R[4], 0.2, 0.0, 0.01), (R[5], 0.1, 0.0, 0.008)],
               [(1500, 1.5, 0.5, 0.0005, 0.005), (300, 1.5, 0.3, 0.001, 0.01)], sag=(250, 0.02), phs=phs, seed=24,
               onset=0.001)
    return [a, b]


def lithophone(rng):
    """A ringing slab, the SINGING STN's stone (its modes 1, 2.76, 5.40, 8.93): struck it rings, rubbed it sings. A:
    tapped, the fundamental ringing; B: struck hard, the upper modes ringing too."""
    R = [1.0, 2.76, 5.40, 8.93]
    phs = rng.uniform(0, 2 * np.pi, len(R) + 1)
    a = struck(16000, 69, 0.25, [(1.0, 1.0, 0.35, 0.25), (2.76, 0.5, 0.06, 0.12), (5.40, 0.3, 0.0, 0.05),
                                 (8.93, 0.15, 0.0, 0.025)], [(4000, 2.0, 0.25, 0.0002, 0.002)], phs=phs, seed=25,
               onset=0.0003, ring_twin=0.2)
    b = struck(16000, 69, 0.25, [(1.0, 1.0, 0.30, 0.25), (2.76, 0.8, 0.14, 0.15), (5.40, 0.6, 0.07, 0.08),
                                 (8.93, 0.4, 0.03, 0.04)], [(4500, 2.0, 0.5, 0.0001, 0.0015)], phs=phs, seed=26,
               onset=0.0002, ring_twin=0.2)
    return [a, b]


def menhir(rng):
    """A maul swung into a standing stone: a crack, heavy low modes, and the stone's deep hum left ringing. A: heard
    close (the crack and the stone's body); B: heard from afar across the field (no crack, the hum darker, the low
    modes blurred)."""
    R = [1.0, 1.66, 2.59, 3.69, 4.96, 6.65, 8.68]
    phs = rng.uniform(0, 2 * np.pi, len(R) + 1)
    a = struck(11025, 36, 0.35, [(r, p, g, d) for r, p, g, d in zip(R, [1.0, .8, .6, .45, .35, .25, .15],
                                                                  [.30, .10, .03, 0, 0, 0, 0],
                                                                  [.15, .10, .07, .05, .04, .03, .02])],
               [(1200, 2.0, 0.6, 0.0005, 0.012), (200, 1.5, 0.5, 0.002, 0.03)], phs=phs, seed=27, onset=0.002,
               T_s=0.40)
    b = struck(11025, 36, 0.35, [(r, p, g, d) for r, p, g, d in zip(R, [1.0, .6, .3, .15, .08, .04, .02],
                                                                  [.32, .08, .01, 0, 0, 0, 0],
                                                                  [.20, .12, .08, .05, .04, .03, .02])],
               [(300, 2.0, 0.4, 0.01, 0.06)], phs=phs, seed=28, onset=0.012, T_s=0.40)
    return [a, b]


def musical_bow(rng):
    """The hunter's bow as an instrument: a sinew string plucked near its end (harmonics a little sharp, as a stiff
    string's), the stave's knock, the string ringing on. A: plucked, bright then mellowing. B: the string held to
    the open mouth: the mouth's formant sweeps up through the harmonics (the "wah") and stays, a nasal twang."""
    sr, root, L = 16000, 55, 0.30
    n, f0 = gp.loop_len(sr, gp.note_hz(root), L)
    K = int(0.45 * sr / f0)
    phs = rng.uniform(0, 2 * np.pi, K + 2)
    pa = [(k * (1 + 0.0004 * k * k), 1.0 / k ** 0.8, 0.30 / k ** 2.2, 0.35 / k ** 0.6) for k in range(1, K)]
    a = struck(sr, root, L, pa, [(4500, 1.5, 0.4, 0.0002, 0.0015), (700, 1.2, 0.2, 0.0005, 0.01)], phs=phs,
               seed=29, onset=0.0003, ring_twin=0.0)
    # B: the mouth formant: a time-varying gain per harmonic is not in struck(); build it here
    nt = Note(sr, n, 0.30)
    df = sr / n
    y = np.zeros(nt.N)
    fc = 600.0 * 3.0 ** np.clip(nt.t / 0.08, 0, 1) * (1 - 0.35 * nt.ramp(0.10, 0.28))   # opens, settles at ~1.2 kHz
    for j, (r, peak, ring, dec) in enumerate(pa):
        kk = round(f0 * r / df)
        f = kk * df
        if f >= 0.45 * sr:
            continue
        mouth = 0.15 + np.exp(-0.5 * (np.log2(f / fc) / 0.35) ** 2)   # settles with fc, before the handoff
        a_t = (1.6 * ring + (peak - 1.6 * ring) * np.exp(-nt.t / dec) * nt.gone(0.1)) * mouth
        y += a_t * np.sin(2 * np.pi * f * nt.tau + phs[j])
    y *= 1.0 - np.exp(-nt.t / 0.0003)
    y += 0.3 * nt.noise(np.random.default_rng(30), lambda f: ga_band(f, 3500, 1.5)) * ga.env_ad(nt.t, 0.0002, 0.0015)
    return [a, (y, nt)]


# ------------------------------------------------------------------------------------------- render --- #
MODELS = {"LORE LONG": (lore_long, 60, 2.0), "ANGKLUNG LONG": (angklung_long, 60, 0.0),
          "FRAME DRUM": (frame_drum, 45, 0.0), "POT DRUM": (pot_drum, 43, 0.0), "LITHOPHONE": (lithophone, 69, 0.0),
          "MENHIR": (menhir, 36, 0.0), "MUSICAL BOW": (musical_bow, 55, 0.0)}
if __name__ == "__main__":
    tot = 0
    for name, (fn, root, bdb) in MODELS.items():
        wv = finish_pair(fn(np.random.default_rng(4242)), bdb)
        for s, tag in zip(wv, "AB"):
            x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
            d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
            T, n = len(s["atk"]), len(s["loop"])
            tot += T + n
            print(f"{name:13s} {tag}: attack {T:5d} B + loop {n:5d} B ({n / s['sr']:.2f} s) at {s['sr']:.0f} Hz; "
                  f"seam {d[T-1]/typ:.2f}, wrap {d[T+n-1]/typ:.2f}")
        fn_ = name.lower().replace(" ", "_")
        if name in ("LORE LONG", "ANGKLUNG LONG"):
            tune, gate = ([60, 55, 60], 2.6) if name == "LORE LONG" else ([60, 64, 67, 72, 67, 64, 62, 60], 0.45)
            envs = [("", (0.002, 0.3, 1.0, 0.25))]
        else:
            tune = [root - 12 + i for i in (12, 19, 15, 24, 19, 12, 7, 12)] if root < 60 else [root + i for i in (0, 7, 3, 12, 7, 0, -5, 0)]
            gate = 0.55
            envs = [("_ring", (0.001, 0.3, 1.0, 0.3)), ("_decay", (0.001, 0.22, 0.0, 0.15))]
        for scan, tag in ((0.0, "A"), (1.0, "B")):
            for etag, env in envs:
                parts = []
                for m in tune:
                    n_out = int((gate + 0.6) * SR)
                    e = gp.adsr(n_out, env[0], env[1], env[2], int(gate * SR), env[3])
                    parts.append(play(wv, root, m, n_out, scan) * e)
                hop = int((gate + 0.12) * SR)
                o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
                for i, xx in enumerate(parts):
                    o[i * hop:i * hop + len(xx)] += xx
                ga.write_wav(OUT / f"{fn_}_scan{tag}{etag}.wav", o / np.abs(o).max() * 0.8, SR)
    print(f"total {tot} B")
