"""Round 3: ANGKLUNG, WIND CHIMES (wood -> slate), LORE refined, SINGING STN with a gentler B. Whole notes as in
notes.py: an attack section, then a loop that repeats exactly; the seam continuous."""
import sys
src = open(__file__.replace("waves2.py", "notes.py")).read().split("TUNES = {")[0]
exec(src)


def tiled_events(L, events, T, dur_of):
    """The loop's events (t, amp, sound) placed on the timeline from -T s up to the handoff, as they were before it."""
    out = []
    j = 1
    while True:
        base = -j * L
        if base + L <= -T:
            break
        for t, a, snd in events:
            u = base + t
            if -T <= u < 0:
                out.append((u, a, snd))
        j += 1
    return out


def render_events(sr, N, T, evs, gain=lambda u: 1.0):
    y = np.zeros(N)
    for u, a, snd in evs:
        s = T + int(round(u * sr))
        m = min(len(snd), N - s)
        if m > 0 and s >= 0:
            y[s:s + m] += gain(u) * a * snd[:m]
    return y


# --------------------------------------------------------------------------------------------- angklung --- #

def angklung(rng):
    """Bamboo angklung: two tubes an octave apart in a frame, shaken. Each shake knocks each tube against the frame
    at both ends of its swing (a strong and a weak knock), so the tubes rattle ~15 times a second each, a little out
    of step: a dry bamboo clack, then the tube's round tone ringing ~0.15 s. The first jerk of the frame knocks both
    tubes at once, hard, and the rattle settles into the shake. A: one player shaking gently. B: shaken hard (faster,
    brighter clacks, the tube's 3rd mode up) and a second angklung joining, a few cents apart."""
    sr, root = 16000, 60
    f0 = gp.note_hz(root)

    def knock(f, hard, prng):
        dur = 0.32
        t = ga.t_axis(sr, dur)
        ring = prng.uniform(0.11, 0.17)
        body = ga.modes(sr, dur, [f, f * 1.004, f * 3.02, f * 5.1], [1.0, 0.35, 0.10 + 0.25 * hard, 0.03 + 0.08 * hard],
                        [ring, ring * 0.8, ring * 0.3, ring * 0.15], prng, onset=0.0006)
        clack = ga.band_noise(sr, len(t), prng, 1800 + 1600 * hard, 1.6, hi=0.45 * sr) * ga.env_ad(t, 0.0003, 0.002 + 0.001 * hard)
        return body / np.abs(body).max() + (0.35 + 0.35 * hard) * clack

    def player(prng, shake_hz, cycles, hard, detune_c):
        L = cycles / shake_hz
        ev = []
        for tube, ratio, lvl, lag in ((0, 1.0, 1.0, 0.0), (1, 2.0 * 2 ** (3 / 1200), 0.7, prng.uniform(0.012, 0.025))):
            f = f0 * ratio * 2 ** (detune_c / 1200)
            for c in range(cycles):
                for end, acc in ((0.0, 1.0), (0.5, 0.55)):
                    t = (c + end) / shake_hz + lag + prng.normal(0, 0.004)
                    ev.append((t % L, lvl * acc * 10 ** (prng.uniform(-3, 0) / 20), knock(f, hard, prng)))
        return L, ev

    out = []
    for hard, shake, cycles, second, seed in ((0.0, 7.0, 4, False, 11), (1.0, 8.75, 5, True, 12)):
        prng = np.random.default_rng(seed)
        L, ev = player(prng, shake, cycles, hard, 0.0)
        if second:
            _, ev2 = player(prng, shake, cycles, hard, 7.0)
            ev += [(t, a * 0.7, s) for t, a, s in ev2]
        n = int(round(L * sr))
        loop = gp.circular_hits(n, sr, ev)
        nt = Note(sr, n, 0.42)
        # note-on lands on the strongest knock of the shake: the jerk
        first = min(ev, key=lambda e: e[0])[0]
        shift = (first + L - (nt.Ts % L)) % L
        ev_s = [((t - shift) % L, a, s) for t, a, s in ev]
        loop = gp.circular_hits(n, sr, ev_s)
        # the loop's knocks before the handoff, on the loop's own sample grid (as circular_hits places them)
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


# ------------------------------------------------------------------------------------------ wind chimes --- #

def wind_chimes(rng, ramp_b=(0.05, 0.33), gone_b=0.12):
    """A hand drawn across the chimes, then the wind keeps them going. A: the hollow wood tubes (today's WOOD
    CHIMES pieces, dry "tonks"). B: slate shards, thin stone plates that ring long and bright (plate modes up to
    ~7 kHz, a second or more), so the strikes pile up into a shimmering bed with new ones glinting on top (after the
    JD-800's chimes, in stone). SCAN: wood -> slate."""
    sr = 16000
    out = []
    # A: the wood
    prng = np.random.default_rng(gp.SEED_BASE)
    pieces = gp.wood_pieces(sr, prng)
    L = 0.55
    loop = gp.chime_loop(prng, sr, L, pieces, 10.0, 0.5)
    sweep = gp.sweep(prng, sr, 0.30, pieces, 0.16)
    nt = Note(sr, len(loop), 0.30)
    y = loop[nt.m] * nt.ramp(0.10, 0.28) * 0.8
    m_ = min(len(sweep), nt.T)
    y[:m_] += sweep[:m_] * nt.gone(0.08)[:m_]
    out.append((y, nt))
    # B: slate
    prng = np.random.default_rng(gp.SEED_BASE + 77)
    ratios = [1.0, 1.47, 1.81, 2.65, 2.91, 3.7]

    def shard(f):
        def make():
            dur = 1.6
            fs = [f * r * (1 + prng.uniform(-0.01, 0.01)) for r in ratios]
            keep = [i for i, x in enumerate(fs) if x < 0.45 * sr]
            dec = [prng.uniform(0.9, 1.4) / ratios[i] ** 0.6 for i in keep]
            amp = [1.0, 0.6, 0.5, 0.35, 0.3, 0.2]
            yy = ga.modes(sr, dur, [fs[i] for i in keep], [amp[i] for i in keep], dec, prng, onset=0.0004, detune=0.0)
            t = ga.t_axis(sr, dur)
            tick = ga.band_noise(sr, len(t), prng, 4500, 1.5, hi=0.45 * sr) * ga.env_ad(t, 0.0002, 0.0015)
            return yy / np.abs(yy).max() * (1 - np.exp(-t / 0.0004)) + 0.25 * tick
        return make
    shards = [shard(f) for f in (1046, 1175, 1397, 1568, 1760, 2093, 2349)]
    Lb = 0.6
    loopb = gp.chime_loop(prng, sr, Lb, shards, 6.0, 0.4)
    sweepb = gp.sweep(prng, sr, 0.35, shards, 0.18)
    ntb = Note(sr, len(loopb), 0.35)
    yb = loopb[ntb.m] * ntb.ramp(*ramp_b) * 0.9
    yb[:ntb.T] += sweepb[:ntb.T] * ntb.gone(gone_b)[:ntb.T]
    out.append((yb, ntb))
    return out


# -------------------------------------------------------------------------------------------------- lore --- #

def lore(rng):
    """LORE refined: the same cane chimes, tuned as before, but rounder: more of the knocks ring (the tubes' tone, not
    their clatter), the knock noise softer, and in the gust fewer collisions so it stays musical. A breeze arriving
    is its attack: a few tubes stirred, then the chimes settle into the loop."""
    sr, root = 16000, 60
    n, f0 = gp.loop_len(sr, gp.note_hz(root), 0.5)
    prng = np.random.default_rng(gp.SEED_BASE + 30)
    tubes = [(3, 0.8), (10 / 3, 0.5), (4, 1.0), (4.4, 0.4), (5.25, 0.8), (6, 0.4), (7, 0.6), (10.5, 0.5)]
    w = np.array([g for _, g in tubes]) / sum(g for _, g in tubes)

    def tube(ratio, soft, ring):
        f = f0 * ratio * (1 + prng.uniform(-0.002, 0.002))
        dur = min(0.6, 6 * ring + 0.02)
        fs, amps, decs = [f, f * 1.003, f * 2.0, f * 2.76], [1.0, prng.uniform(0.3, 0.6), 0.12, 0.15], \
            [ring, ring * 0.8, ring * 0.5, ring * 0.25]
        keep = [i for i, x in enumerate(fs) if x < 0.45 * sr]
        y = ga.modes(sr, dur, [fs[i] for i in keep], [amps[i] for i in keep], [decs[i] for i in keep], prng, onset=soft)
        t = ga.t_axis(sr, dur)
        knock = ga.band_noise(sr, len(t), prng, min(2.2 * f, 6000), 1.4, hi=0.45 * sr) * ga.env_ad(t, soft * 0.5, 0.003)
        return y / np.abs(y).max() + 0.06 * knock / np.abs(knock).max()

    def stream(T_len, rate_of, soft, short, long_p, cluster, long=(0.10, 0.24), min_gap=0.008, t0=0.0):
        ev, t = [], t0
        while t < T_len:
            j = prng.choice(len(tubes), p=w)
            ring = prng.uniform(*long) if prng.uniform() < long_p else prng.uniform(*short)
            ev.append((t, 10 ** (prng.uniform(-10, 0) / 20), tube(tubes[j][0], prng.uniform(*soft), ring)))
            if prng.uniform() < cluster:
                k = (j + prng.choice([-1, 1])) % len(tubes)
                ev.append((t + prng.uniform(0.006, 0.02), 10 ** (prng.uniform(-14, -5) / 20),
                           tube(tubes[k][0], prng.uniform(*soft), prng.uniform(*short))))
            t += max(min_gap, prng.exponential(1.0 / rate_of(t)))
        return ev

    out = []
    for rate, soft, short, long_p, cluster, gain in ((16, (0.002, 0.007), (0.02, 0.05), 0.4, 0.4, 1.0),
                                                     (30, (0.003, 0.008), (0.015, 0.035), 0.3, 0.45, 1.25)):
        ev = stream(n / sr, lambda t: rate, soft, short, long_p, cluster)
        loop = gp.circular_hits(n, sr, ev)
        nt = Note(sr, n, 0.35)
        y = loop[nt.m] * nt.ramp(0.12, 0.33)
        breeze = stream(0.25, lambda t: rate * 1.4, soft, short, 0.6, cluster * 0.6, t0=0.003)
        b = render_events(sr, nt.N + sr, 0, breeze)[:nt.N] * nt.gone(0.12)
        out.append((gain * (y + b), nt))
    return out


# ------------------------------------------------------------------------------------- singing stone --- #

def singing_stone2(rng):
    """SINGING STN reworked: B is the hand pressed harder, which makes the stone SING more, not hiss: the upper
    modes ring up (and beat slowly against their twins), the roughness stays a narrow band around each mode, and
    the stick-slip grit barely deepens. A as before. The strike is the slab's own modes, the rub building under."""
    sr, n = 11025, 4096
    step = sr / n
    f0 = round(220.0 / step) * step
    kf = round(f0 / step)
    ratios = [1.0, 2.76, 5.40, 8.93, 13.3]
    phs = rng.uniform(0, 2 * np.pi, (len(ratios), 2))
    coef = np.exp(2j * np.pi * rng.uniform(size=n // 2 + 1))
    out = []
    for gains, twin, rough_w, rough_lvl, grit_lvl, grit_pow, strike, seed in [
            ([1.0, 0.55, 0.3, 0.16, 0.08], 0.5, 10.0, 0.6, 0.25, 6, [1.6, 1.4, 1.2, 1.0, 0.8], 3),
            ([1.0, 0.85, 0.7, 0.5, 0.3], 0.7, 12.0, 0.55, 0.32, 5, [2.0, 2.0, 1.8, 1.6, 1.4], 4)]:
        nt = Note(sr, n, 0.50)
        t = nt.tau * sr
        sines = np.zeros(nt.N)
        strike_part = np.zeros(nt.N)
        for j, (r, gn) in enumerate(zip(ratios, gains)):
            k = round(f0 * r / step)
            if k * step >= sr * 0.45:
                continue
            s1 = np.sin(2 * np.pi * k * t / n + phs[j, 0])
            s2 = np.sin(2 * np.pi * (k + 1) * t / n + phs[j, 1])
            sines += gn * (s1 + twin * s2)
            strike_part += strike[j] * gn * np.exp(-nt.t / (0.30 / r ** 0.7)) * s1
        sn = np.abs(sines[nt.T:]).max()
        bands = [(f0 * r, rough_w * r ** 0.5, gn) for r, gn in zip(ratios, gains)]
        f = np.fft.rfftfreq(n, 1.0 / sr)
        rough = np.fft.irfft(gp.formant(f, bands, floor=0.0) * (f < sr * 0.45) * coef, n=n)
        rough = rough[nt.m] / np.abs(rough).max()
        x = (nt.ramp(0.04, 0.42) * sines + strike_part * nt.gone(0.15)) / sn + rough_lvl * nt.ramp(0.02, 0.40) * rough
        grit = 1.0 + grit_lvl * nt.ramp(0.08, 0.45) * np.maximum(0, np.sin(2 * np.pi * kf * t / n)) ** grit_pow
        tick = nt.noise(np.random.default_rng(seed), lambda f: ga_band(f, 3500, 2.0)) * np.exp(-nt.t / 0.002)
        out.append((x * grit + 0.8 * tick, nt))
    return out


TUNES = {
    "ANGKLUNG": (60, [60, 64, 67, 72, 67, 64, 62, 60], 0.45, None),
    "WIND CHIMES": (60, [60, 67, 60], 1.6, "WOOD CHIMES"),
    "LORE": (60, [60, 55, 60], 1.6, "LORE"),
    "SINGING STN": (57, [50, 57, 53, 62, 57, 50, 45, 50], 0.9, "SINGING STN"),
}
MODELS = {"ANGKLUNG": angklung, "WIND CHIMES": wind_chimes, "LORE": lore, "SINGING STN": singing_stone2}
B_DB = {"LORE": 2.0, "SINGING STN": 2.0}
OLD = {w[0]: w for w in gp.WAVES}
S = gp.build_samples()
for name, fn in MODELS.items():
    w = finish_pair(fn(np.random.default_rng(4242)), B_DB.get(name, 0.0))
    root, tune, gate, old_name = TUNES[name]
    for s, tag in zip(w, "AB"):
        x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
        d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
        T, n = len(s["atk"]), len(s["loop"])
        print(f"{name:12s} {tag}: attack {T:5d} B, loop {n:5d} B at {s['sr']:.0f} Hz; seam {d[T-1]/typ:.2f}, "
              f"wrap {d[T+n-1]/typ:.2f} (x a big step)")
    for scan, tag in ((0.0, "A"), (1.0, "B")):
        rng = np.random.default_rng(3)
        new, old = [], []
        for m in tune:
            n_out = int((gate + 0.6) * SR)
            e = gp.adsr(n_out, 0.002, 0.3, 1.0, int(gate * SR), 0.25)
            new.append(play(w, root, m, n_out, scan) * e)
            if old_name:
                old.append(gp.play_wave(S, OLD[old_name], m, n_out, np.full(n_out, scan), rng) * e)
        hop = int((gate + 0.12) * SR)

        def seq(parts):
            o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
            for i, xx in enumerate(parts):
                o[i * hop:i * hop + len(xx)] += xx
            return o
        b = seq(new)
        a = seq(old) if old else None
        pk = max(np.abs(b).max(), np.abs(a).max() if a is not None else 0) / 0.8
        fn_ = name.lower().replace(" ", "_")
        if a is not None:
            ga.write_wav(OUT / f"{fn_}_scan{tag}_1_now.wav", a / pk, SR)
        ga.write_wav(OUT / f"{fn_}_scan{tag}_2_note.wav", b / pk, SR)
