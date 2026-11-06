"""Whole-note PCM waves (JD-800 style): each instrument is rendered as one continuous note, an attack section that
runs into a steady state that repeats exactly every n samples. The loop is one period of that steady state, so the
attack hands over to it seamlessly (the firmware starts the loop where the attack ends). Two notes per wave: one
that settles into loop A, one into loop B; SCAN picks the nearer at note-on.

Everything that lives on in the loop (partials, chorus, vibrato, breath noise) is periodic over n by construction
(frequencies on the loop's grid, modulations whole cycles, noise built from the grid's own bins); everything that
belongs to the attack alone (scoops, entries, chiffs, blats, strikes) is shaped to be exactly gone at the handoff."""
import sys, pathlib, numpy as np
sys.path.insert(0, sys.argv[1]); OUT = pathlib.Path(sys.argv[2]); OUT.mkdir(parents=True, exist_ok=True)
import gen_pcm_waves as gp, gen_attacks as ga
SR = gp.SR
TOP = gp.ENS_TOP


def ss(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3.0 - 2.0 * x)


class Note:
    """The timeline of one note: attack of T samples, then a loop of n; t = s since note-on, m = loop index."""
    def __init__(self, sr, n, T_s):
        self.sr, self.n = sr, n
        self.T = int(round(T_s * sr))
        self.Ts = self.T / sr
        self.N = self.T + n
        i = np.arange(self.N)
        self.t = i / sr
        self.tau = (i - self.T) / sr          # s since the handoff (negative in the attack)
        self.m = (i - self.T) % n             # loop index

    def ramp(self, t0, t1):                   # 0 -> 1 between t0 and t1 s; must end before the handoff
        assert t1 <= self.Ts + 1e-9
        return ss((self.t - t0) / max(t1 - t0, 1e-6))

    def gone(self, w=0.04):                   # 1, then to 0 exactly at the handoff over the last w s
        return ss((self.Ts - self.t) / w)

    def scoop(self, f, cents, tau):
        """Phase offset (rad) of a pitch that starts `cents` off and glides onto f; 0 from the handoff on."""
        r = (2.0 ** (cents / 1200.0 * np.exp(-self.t / tau)) - 1.0) * self.gone(0.05)
        d = 2 * np.pi * f * r / self.sr
        return -np.cumsum(d[::-1])[::-1] + d   # minus the frequency excess still to come

    def noise(self, rng, shape):             # one-off noise (the attack's own), shaped by shape(f)
        f = np.fft.rfftfreq(self.N, 1.0 / self.sr)
        y = np.fft.irfft(np.fft.rfft(rng.standard_normal(self.N)) * shape(f), n=self.N)
        return y / (np.abs(y).max() + 1e-12)


class Grid:
    """Loop grid shared by a pair: A's n at sr_a fixes df (Hz); B renders the same Hz at its own rate."""
    def __init__(self, sr_a, f_target, L, sr_b=None):
        self.n_a, self.f0 = gp.loop_len(sr_a, f_target, L)
        self.df = sr_a / self.n_a
        self.sr_a = sr_a
        self.n_b = int(round(self.n_a * (sr_b or sr_a) / sr_a))
        self.sr_b = self.n_b * self.df         # exact; stored rounded to Hz (as resample_loop does today)

    def periodic_noise(self, coef, note, shape):
        """Noise on the grid's bins (the same random coefficients at A's and B's rate): periodic over n."""
        nb = note.n // 2 + 1
        f = np.arange(nb) * self.df
        c = np.zeros(nb, complex)
        k = min(nb, len(coef))
        c[:k] = coef[:k]
        c *= shape(f) * (f < 0.45 * note.sr)
        y = np.fft.irfft(c, n=note.n)
        return y[note.m] / (np.abs(y).max() + 1e-12)


def harm_sum(phase, amps_k, ph, f, sr):
    """sum_k amps_k[k-1](t) sin(k phase + ph_k), partials above 0.45 sr dropped; amps_k entries scalar or arrays."""
    y = 0.0
    for k, (a, p) in enumerate(zip(amps_k, ph), start=1):
        if k * f >= 0.45 * sr:
            break
        y = y + a * np.sin(k * phase + p)
    return y


def formant(f, bands, floor):
    return gp.formant(np.asarray(f, float), bands, floor=floor)


# ----------------------------------------------------------------------------------------------- choir --- #

def choir(rng):
    """Seven singers. Each comes in on their own breath (a soft "h" in the vowel's own colour, not a chiff), a hair
    apart from the others (0..70 ms), sliding up onto the note from a little under, the vowel opening from closed
    (dark) to its full colour over ~0.2 s, sung straight at first and the vibrato blooming after. A: ooh, B: aah."""
    g = Grid(11025, gp.note_hz(57), 0.48, 13000)
    ooh = [(300, 50, 1.0), (870, 80, 0.2), (2240, 140, 0.03)]
    aah = [(730, 90, 1.0), (1090, 110, 0.85), (2440, 160, 0.5), (2900, 300, 0.75), (3400, 300, 0.35)]
    players = [(-1, 2, 12), (-1, 3, 8), (0, 2, 14), (0, 3, 6), (0, 2, 10), (1, 3, 9), (1, 2, 13)]
    P = []
    for step, vcyc, vc in players:
        P.append(dict(step=step, vcyc=vcyc, vc=vc, vph=rng.uniform(0, 2 * np.pi), wph=rng.uniform(0, 2 * np.pi, 6),
                      ph=rng.uniform(0, 2 * np.pi, 40), coef=rng.standard_normal(4000) + 1j * rng.standard_normal(4000),
                      enter=rng.uniform(0.0, 0.07), rise=rng.uniform(0.07, 0.12), cents=-rng.uniform(35, 90),
                      open=rng.uniform(0.16, 0.24), seed=rng.integers(1 << 30)))
    T_s = 0.40
    out = []
    for vowel, sr, n, slope, flo, br in [(ooh, g.sr_a, g.n_a, -1.8, 0.01, 0.008), (aah, g.sr_b, g.n_b, -0.7, 0.05, 0.07)]:
        nt = Note(sr, n, T_s)
        y = 0.0
        for p in P:
            fv = g.f0 + p["step"] * g.df
            vr = p["vcyc"] * g.df
            vd = fv * (2 ** (p["vc"] / 1200) - 1)
            u = nt.m / n
            bloom = nt.ramp(0.12, 0.36)                      # vibrato blooms after the entry
            wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
            amp_w = 1 + 0.10 * sum(np.sin(2 * np.pi * h * u + p["wph"][h + 2]) / h for h in range(1, 4)) / 1.83
            phase = 2 * np.pi * fv * nt.tau + bloom * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
                + 0.5 * wan + nt.scoop(fv, p["cents"], 0.045)
            e0, e1 = p["enter"], p["enter"] + p["rise"]
            on = nt.ramp(e0, e1)
            opened = nt.ramp(e0, e0 + p["open"])             # the vowel opening: the top comes in last
            k_top = int(min(TOP, 0.45 * sr) / fv)
            amps = []
            for k in range(1, k_top + 1):
                a = k ** slope * formant(k * fv, vowel, flo)
                amps.append(a * (1.0 - (1.0 - opened) * (1.0 - min(1.0, 1.6 / k))))
            voice = harm_sum(phase, amps, p["ph"], fv, sr)
            breath_loop = g.periodic_noise(p["coef"], nt, lambda f: ga_band(f, 2600, 1.4)) * (0.7 + 0.3 * np.sin(phase))
            prng = np.random.default_rng(p["seed"])
            h = nt.noise(prng, lambda f: formant(f, vowel, 0.05) * (f > 150) * (f < TOP))
            h_env = ss((nt.t - (e0 - 0.035)) / 0.03) * np.exp(-np.maximum(nt.t - e0, 0) / 0.05) * nt.gone(0.08)
            y = y + amp_w * (on * voice + br * on * breath_loop + (0.05 if vowel is ooh else 0.10) * h_env * h)
        out.append((y, nt))
    return out


def ga_band(f, centre, octaves):
    return np.exp(-0.5 * (np.log2(np.maximum(f, 1.0) / centre) / (octaves / 2.0)) ** 2)


# ------------------------------------------------------------------------------------------- pan flute --- #

def pan_flute(rng):
    """A stopped pipe blown across. The breath comes first, a soft rush of air at the pipe's edge (no tongue, no
    click), the pipe catching it a moment later: the air turns pitched, the tone grows out of it, flat and rising
    onto the note as the blowing firms up, and a brief overblown whistle an octave and a fifth up (a stopped pipe's
    next mode) flickers as it locks. A: blown softly, B: blown hard (quicker, more edge, a stronger whistle)."""
    g = Grid(11025, gp.note_hz(72), 0.48)
    ph = rng.uniform(0, 2 * np.pi, 40)
    coef = rng.standard_normal(4000) + 1j * rng.standard_normal(4000)
    vph, wph = rng.uniform(0, 2 * np.pi), rng.uniform(0, 2 * np.pi, 6)
    soft = [1.0, 0.05, 0.10, 0.02, 0.03, 0.01, 0.01]
    full = [1.0, 0.10, 0.65, 0.06, 0.38, 0.04, 0.2, 0.02, 0.1, 0.01, 0.05]
    out = []
    for amps0, air_l, rush, catch, cents, whistle, seed in [(soft, 0.30, 0.55, 0.045, -30, 0.25, 1),
                                                            (full, 0.10, 0.45, 0.025, -45, 0.55, 2)]:
        nt = Note(g.sr_a, g.n_a, 0.30)
        fv = g.f0
        u = nt.m / nt.n
        vr, vd = 2 * g.df, fv * (2 ** (9 / 1200) - 1)
        wan = sum(np.sin(2 * np.pi * h * u + wph[h - 1]) / h for h in range(1, 4)) / 1.83
        phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.10, 0.28) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + vph) \
            + 0.5 * wan + nt.scoop(fv, cents, 0.035)
        amps = []
        for k, a in enumerate(amps0, start=1):
            env = nt.ramp(catch * 0.4, catch * 0.4 + 0.07) if k == 1 else nt.ramp(catch, catch + 0.10)
            amps.append(a * env)
        tone = harm_sum(phase, amps, ph, fv, nt.sr)
        wh = whistle * np.exp(-np.maximum(nt.t - catch * 0.5, 0) / 0.03) * ss((nt.t - catch * 0.3) / 0.015) \
            * nt.gone(0.1) * np.sin(3 * phase + ph[2])
        pitched = lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 35) ** 2) for h in (1, 2, 3)) * 0.6
                             + ga_band(f, 2500, 2.0)) * (f < TOP)
        air = g.periodic_noise(coef, nt, pitched) * (0.6 + 0.4 * np.sin(phase))
        edge = nt.noise(np.random.default_rng(seed), lambda f: ga_band(f, 2800, 2.4) * (f > 300))
        edge_env = ss(nt.t / 0.012) * np.exp(-nt.t / 0.06) * nt.gone(0.08)
        y = tone + wh + air_l * nt.ramp(0.0, catch + 0.08) * air + rush * edge_env * edge
        out.append((y, nt))
    return out


# ----------------------------------------------------------------------------------------------- horns --- #

def horns(rng):
    """Four cow horns. The blat is the horn's signature and stays: each player's lips buzz in a hair apart, the
    pitch snapping up from a few semitones under, a burst of brassy rasp and lip growl right at the front, then
    settling into the section. A settles soft and round, B is blown hard into the brassy loop (a harder blat)."""
    g = Grid(11025, gp.note_hz(48), 0.48, 13000)
    P = [dict(step=s, ph=rng.uniform(0, 2 * np.pi, 80), wph=rng.uniform(0, 2 * np.pi, 20),
              enter=rng.uniform(0, 0.03), cents=-rng.uniform(220, 330), seed=rng.integers(1 << 30))
         for s in (-1, 0, 0, 1)]
    out = []
    for kind, sr, n in [("soft", g.sr_a, g.n_a), ("brassy", g.sr_b, g.n_b)]:
        nt = Note(sr, n, 0.32)
        u = nt.m / n
        y = 0.0
        for p in P:
            fv = g.f0 + p["step"] * g.df
            prng = np.random.default_rng(p["seed"])
            wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
            growl = 0.25 * sum(np.sin(2 * np.pi * h * u + p["wph"][h + 5]) / h for h in range(1, 9)) / 2.72
            jit = nt.noise(prng, lambda f: (f < 60) * 1.0) * 1.2 * np.exp(-nt.t / 0.035) * nt.gone(0.1)
            e0 = p["enter"]
            phase = 2 * np.pi * fv * nt.tau + 0.5 * wan + nt.scoop(fv, p["cents"] * (1.3 if kind == "brassy" else 1.0),
                                                               0.022) + jit
            k_top = int(min(TOP, 0.45 * sr) / fv)
            k = np.arange(1, k_top + 1)
            bell = formant(k * fv, [(1200, 600, 1.4), (2500, 700, 0.7)], 1.0)
            blat = np.exp(-np.maximum(nt.t - e0, 0) / 0.05) * ss((nt.t - e0) / 0.012) * nt.gone(0.1)
            on = nt.ramp(e0, e0 + 0.02)
            if kind == "soft":
                steady = k ** -2.4 * formant(k * fv, [(600, 300, 0.3)], 1.0)
                rasp = k ** -1.0 * bell
                s = harm_sum(phase, list(steady), p["ph"], fv, sr)
                b = harm_sum(phase + growl, list(rasp), p["ph"], fv, sr)
                ref = np.abs(b).max()
                v = on * s + 0.85 * blat * np.tanh(2.0 * b / ref) * np.abs(s).max()
            else:
                steady = k ** -0.75 * bell
                b = harm_sum(phase + growl, list(steady), p["ph"], fv, sr)
                ref = np.abs(b[nt.T:]).max()
                drive = 2.5 + 3.0 * blat
                v = on * np.tanh(drive * b / ref) * (1.0 + 0.4 * blat)
            spit = nt.noise(prng, lambda f: ga_band(f, 900, 2.0)) * ss((nt.t - e0) / 0.002) \
                * np.exp(-np.maximum(nt.t - e0, 0) / 0.015) * nt.gone(0.1)
            y = y + v + 0.25 * spit * np.abs(v[nt.T:]).max()
        out.append((y, nt))
    return out


# ------------------------------------------------------------------------------------------- stone ------ #

def singing_stone(rng):
    """The wet slab struck with a hand stone, then rubbed: the strike rings the slab's own modes (the ones the rub
    sustains, so it is one stone), the highs dying first, while the stick-slip grit and roughness build under it
    until the rub holds the note. A: a soft knock into a stroke; B: a hard knock into a hard rub."""
    sr, n = 11025, 4096
    step = sr / n
    f0 = round(220.0 / step) * step
    kf = round(f0 / step)
    ratios = [1.0, 2.76, 5.40, 8.93, 13.3]
    phs = rng.uniform(0, 2 * np.pi, (len(ratios), 2))
    coef = np.exp(2j * np.pi * rng.uniform(size=n // 2 + 1))
    out = []
    for gains, rough_w, rough_lvl, grit_lvl, grit_pow, strike, seed in [
            ([1.0, 0.55, 0.3, 0.16, 0.08], 10.0, 0.6, 0.25, 6, [1.6, 1.4, 1.2, 1.0, 0.8], 3),
            ([1.0, 0.8, 0.6, 0.45, 0.3], 28.0, 1.4, 0.8, 3, [2.2, 2.4, 2.4, 2.2, 2.0], 4)]:
        nt = Note(sr, n, 0.50)
        t = nt.tau * sr                                       # samples since the handoff (the loop's own clock)
        sines = np.zeros(nt.N)
        strike_part = np.zeros(nt.N)
        for j, (r, gn) in enumerate(zip(ratios, gains)):
            k = round(f0 * r / step)
            if k * step >= sr * 0.45:
                continue
            s1 = np.sin(2 * np.pi * k * t / n + phs[j, 0])
            s2 = np.sin(2 * np.pi * (k + 1) * t / n + phs[j, 1])
            sines += gn * (s1 + 0.5 * s2)
            dec = 0.30 / r ** 0.7
            strike_part += strike[j] * gn * np.exp(-nt.t / dec) * s1
        sn = np.abs(sines[nt.T:]).max()
        bands = [(f0 * r, rough_w * r ** 0.5, gn) for r, gn in zip(ratios, gains)]
        f = np.fft.rfftfreq(n, 1.0 / sr)
        rough = np.fft.irfft(gp.formant(f, bands, floor=0.0) * (f < sr * 0.45) * coef, n=n)
        rough = rough[nt.m] / np.abs(rough).max()
        rub = nt.ramp(0.04, 0.42)
        x = (rub * sines + strike_part * nt.gone(0.15)) / sn + rough_lvl * nt.ramp(0.02, 0.40) * rough
        grit = 1.0 + grit_lvl * nt.ramp(0.08, 0.45) * np.maximum(0, np.sin(2 * np.pi * kf * t / n)) ** grit_pow
        tick = nt.noise(np.random.default_rng(seed), lambda f: ga_band(f, 3500, 2.0)) * np.exp(-nt.t / 0.002)
        out.append((x * grit + 0.8 * tick, nt))
    return out


# ------------------------------------------------------------------------------------------- rainstick --- #

def rainstick(rng):
    """The stick turned over: the seeds that were waiting at the end let go together, a rush that thins out into the
    steady fall of the loop. A: tipped gently into the trickle; B: upended into the pour."""
    sr = 14000
    n = int(0.45 * sr)
    out = []

    def tick_snd(prng, low):
        m = int(sr * 0.005)
        tk = ga.band_noise(sr, m, prng, prng.uniform(1300, 2400) if low else prng.uniform(2800, 5000), 1.0, hi=0.45 * sr)
        return tk * np.exp(-np.arange(m) / (sr * prng.uniform(0.0004, 0.0013)))

    def loop_ticks(prng, rate, lo_share, gust):
        swell = 0.5 + 0.5 * gp.wander(1024, prng, 2)
        times = np.sort(prng.uniform(0, n / sr, prng.poisson(rate * n / sr)))
        keep = prng.uniform(size=len(times)) < (1 - gust) + gust * swell[(times / (n / sr) * 1024).astype(int) % 1024]
        ev = [(tt, 10 ** (prng.uniform(-18, 0) / 20), tick_snd(prng, prng.uniform() < lo_share)) for tt in times[keep]]
        y = gp.circular_hits(n, sr, ev)
        return y / (np.abs(y).max() + 1e-12)

    for rate, lo_share, gust, wash, surge, seed in [(70, 0.1, 0.6, 0.0, 450, 5), (650, 0.35, 0.3, 0.25, 2400, 6)]:
        prng = np.random.default_rng(seed)
        loop = loop_ticks(prng, rate, lo_share, gust)
        if wash:
            loop = loop + wash * gp.periodic_noise(n, sr, prng, lambda f: gp.logband(f, 3500, 1.8))
        nt = Note(sr, n, 0.45)
        y = loop[nt.m] * nt.ramp(0.05, 0.40)
        # the rush: density falling from `surge` a second, every grain gone before the handoff
        T_end = nt.Ts - 0.01
        tt = np.sort(prng.uniform(0.0, T_end, prng.poisson(surge * 0.15)))
        keep = prng.uniform(size=len(tt)) < np.exp(-tt / 0.12) * ss(tt / 0.02 + 0.3)
        for t0 in tt[keep]:
            snd = tick_snd(prng, prng.uniform() < lo_share + 0.15)
            s = int(t0 * sr)
            mlen = min(len(snd), nt.T - s)
            y[s:s + mlen] += 10 ** (prng.uniform(-14, 0) / 20) * snd[:mlen] * (0.8 if wash else 0.35)
        whoosh = nt.noise(prng, lambda f: ga_band(f, 3000, 2.0)) * ss(nt.t / 0.03) * np.exp(-nt.t / 0.12) * nt.gone(0.1)
        out.append((y + (0.12 if wash else 0.05) * whoosh, nt))
    return out


# ------------------------------------------------------------------------------------------- log drum --- #

def log_drum(rng):
    """The slit drum as a wave: the beater's strike wakes the trunk's modes, the upper ones dying in tens of ms, the
    tongue's own note ringing on as the sustain (the loop: the amp envelope decides how long it rings). A: a padded
    beater, round; B: a hard stick, more bite and a brighter ring."""
    sr = 11025
    n, f0 = gp.loop_len(sr, gp.note_hz(55), 0.30)
    df = sr / n
    ratios = [1.0, 2.04, 3.12, 4.6, 6.3]
    phs = rng.uniform(0, 2 * np.pi, len(ratios) + 1)
    out = []
    for peak, ring, decay, thump_c, click, seed in [
            ([1.0, 0.45, 0.2, 0.1, 0.05], [0.30, 0.05, 0.0, 0.0, 0.0], [0.11, 0.05, 0.03, 0.02, 0.012], 400, 0.0, 7),
            ([1.0, 0.65, 0.45, 0.3, 0.2], [0.30, 0.10, 0.04, 0.0, 0.0], [0.10, 0.05, 0.035, 0.022, 0.014], 700, 0.5, 8)]:
        nt = Note(sr, n, 0.35)
        y = np.zeros(nt.N)
        for j, r in enumerate(ratios):
            k = round(f0 * r / df)
            if k * df >= 0.45 * sr:
                continue
            s = np.sin(2 * np.pi * k * df * nt.tau + phs[j])
            a = ring[j] + (peak[j] - ring[j]) * np.exp(-nt.t / decay[j]) * nt.gone(0.12)
            y += a * s
        kb = round(f0 / df) + 1                                # the tongue's twin: a slow woody beat in the ring
        y += 0.35 * ring[0] * np.sin(2 * np.pi * kb * df * nt.tau + phs[-1])
        y *= 1.0 - np.exp(-nt.t / 0.0012)
        prng = np.random.default_rng(seed)
        th = nt.noise(prng, lambda f: ga_band(f, thump_c, 1.5)) * ga.env_ad(nt.t, 0.001, 0.010) * nt.gone(0.1)
        ck = nt.noise(prng, lambda f: ga_band(f, 3000, 2.0)) * ga.env_ad(nt.t, 0.0002, 0.0025)
        out.append((y + 0.5 * th + click * ck, nt))
    return out


# ------------------------------------------------------------------------------------------- render ----- #

def finish_pair(pair, b_db=0.0):
    """Split each note into attack and loop, the pair at one gain (loops RMS-matched as pair_gain does), the
    loudest sample at PEAK, then 8-bit mu-law as stored."""
    (ya, na), (yb, nb) = pair
    la, lb = ya[na.T:], yb[nb.T:]
    ga_ = 1.0 / np.sqrt(np.mean(la ** 2))
    gb_ = 1.0 / np.sqrt(np.mean(lb ** 2)) * 10 ** (b_db / 20)
    k = gp.PEAK / max(np.abs(ya).max() * ga_, np.abs(yb).max() * gb_)
    res = []
    for y, nt, g in ((ya, na, ga_), (yb, nb, gb_)):
        q = ga.mulaw_roundtrip(y * g * k)
        res.append(dict(atk=q[:nt.T], loop=q[nt.T:], sr=nt.sr, n=nt.n))
    return res


def play(w, root, note, n_out, scan):
    """The proposed firmware: the attack nearer SCAN alone, then the loops from index 0, SCAN's crossfade. Each loop
    runs at its own rate from the handoff (2026-11-05 fix: the loops were kept in step by fraction of the nearer
    loop's length, which read a loop of another length at the wrong speed, a ghost note under SCAN's middle)."""
    near = w[0] if scan < 0.5 else w[1]
    step = near["sr"] / SR * 2 ** ((note - root) / 12)
    pos = np.arange(n_out) * step
    T = len(near["atk"])
    y = np.zeros(n_out)
    ia = pos < T - 1
    i = pos[ia].astype(int); fr = pos[ia] - i
    y[ia] = near["atk"][i] * (1 - fr) + near["atk"][i + 1] * fr
    for wgt, s in ((1 - scan, w[0]), (scan, w[1])):
        if wgt == 0:
            continue
        lp = ((pos[~ia] - T) / near["sr"] * s["sr"]) % s["n"]
        j = lp.astype(int); fr2 = lp - j
        y[~ia] += wgt * (s["loop"][j % s["n"]] * (1 - fr2) + s["loop"][(j + 1) % s["n"]] * fr2)
    return y


TUNES = {
    "PAN FLUTE": (72, [74, 76, 79, 81, 79, 76, 74, 69], 0.55),
    "CHOIR": (57, [57, 60, 64, 62, 57, 55, 57, 64], 0.8),
    "HORNS": (48, [43, 48, 50, 48, 43, 38, 43, 48], 0.6),
    "SINGING STN": (57, [50, 57, 53, 62, 57, 50, 45, 50], 0.9),
    "RAINSTICK": (60, [60, 60, 55, 60], 1.4),
    "LOG DRUM": (55, [43, 50, 55, 50, 48, 43, 50, 55], 0.35),
}
MODELS = {"PAN FLUTE": pan_flute, "CHOIR": choir, "HORNS": horns, "SINGING STN": singing_stone,
          "RAINSTICK": rainstick, "LOG DRUM": log_drum}
B_DB = {"PAN FLUTE": 1.0, "CHOIR": 2.0, "HORNS": 3.0, "SINGING STN": 2.0, "RAINSTICK": 4.0}
OLD = {w[0]: w for w in gp.WAVES}
S = gp.build_samples()

total = 0
for name, fn in MODELS.items():
    w = finish_pair(fn(np.random.default_rng(4242)), B_DB.get(name, 0.0))
    root, tune, gate = TUNES[name]
    for s, tag in zip(w, "AB"):
        last, first = s["atk"][-1], s["loop"][0]
        total += len(s["atk"]) + len(s["loop"])
        print(f"{name:12s} {tag}: attack {len(s['atk']):5d} B, loop {len(s['loop']):5d} B at {s['sr']:.0f} Hz; "
              f"seam {last:+.3f} -> {first:+.3f}")
    for scan, tag in ((0.0, "A"), (1.0, "B")):
        rng = np.random.default_rng(3)
        envs = [("", (0.002, 0.3, 1.0, 0.18))]
        if name == "LOG DRUM":                                  # the ring at full, then the envelope's decay
            envs = [("_ring", (0.001, 0.3, 1.0, 0.25)), ("_decay", (0.001, 0.25, 0.0, 0.15))]
        for etag, env in envs:
            new, old = [], []
            for m in tune:
                n_out = int((gate + 0.6) * SR)
                e = gp.adsr(n_out, env[0], env[1], env[2], int(gate * SR), env[3])
                new.append(play(w, root, m, n_out, scan) * e)
                if name in OLD:
                    old.append(gp.play_wave(S, OLD[name], m, n_out, np.full(n_out, scan), rng) * e)
                else:
                    _, _, r0, sr0, x0 = {t[0]: t for t in ga.build_all()}["LOG DRUM"]
                    k0, _ = gp.read_once(x0, sr0, m, r0, n_out)
                    old.append(k0 * e)
            def seq(parts):
                hop = int((gate + 0.12) * SR)
                o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
                for i, x in enumerate(parts):
                    o[i * hop:i * hop + len(x)] += x
                return o
            a, b = seq(old), seq(new)
            pk = max(np.abs(a).max(), np.abs(b).max()) / 0.8
            fn_ = name.lower().replace(" ", "_")
            ga.write_wav(OUT / f"{fn_}_scan{tag}{etag}_1_now.wav", a / pk, SR)
            ga.write_wav(OUT / f"{fn_}_scan{tag}{etag}_2_note.wav", b / pk, SR)
print(f"total {total} B for these six waves (attacks + loops)")
