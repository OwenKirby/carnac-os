"""The definitive PCM list (2026-11-05): every wave built from its round, the new ones (HARP from JD 57, VOX>CHOIR,
the two merged wooden clouds), each side at the lowest rate a threshold allows, the total against the budget.

  python definitive.py <src/dsp> <out dir> [--thr -30] [--render]

The rate rule (waves6): the lowest rate whose cut (energy above 0.45 x rate) is under `thr` dB of the side's energy.
--render writes each wave A -> 50 % -> B at the chosen rates (and at -35 dB, today's rule, when they differ)."""
import sys, pathlib
here = pathlib.Path(__file__).parent
THR = float(sys.argv[sys.argv.index("--thr") + 1]) if "--thr" in sys.argv else -30.0
RENDER = "--render" in sys.argv
sys.argv = sys.argv[:3]
for _f in ("waves6.py", "waves10.py", "waves9b.py", "waves13.py"):
    exec(compile(open(here / _f).read().split('\nif __name__ == "__main__":')[0], str(here / _f), "exec"))
RATES = [6000, 7000, 8000, 9000, 10000, 11025, 12000, 13000, 14000, 16000]
BUDGET_KB = 226.0


def rate_for(s, thr):
    x = np.concatenate([s["atk"], s["loop"]])
    X = np.abs(np.fft.rfft(x)) ** 2; f = np.fft.rfftfreq(len(x), 1 / s["sr"]); tot = X.sum()
    for r in RATES:
        if r >= s["sr"] - 1:
            return s["sr"]
        if X[f > 0.45 * r].sum() / tot < 10 ** (thr / 10):
            return r
    return s["sr"]


# ------------------------------------------------------------------------------------------------ HARP --- #

HARP_A_DB = [0, -25, -9, -38, -25, -20, -15, -22, -30, -20, -37, -27, -45, -38, -55, -34, -45, -52, -60, -65]
HARP_A_DEC = [-6, -15, -28, -40, -20, -23, -36, -90, -52, -30, -40, -73, -110, -96, -100, -139, -110, -110, -140, -150]


def harp(rng, sr=12000, T_s=0.30):
    """A: the nylon-strung harp after JD-800 wave 57 (measured: harmonics by number as HARP_A_DB, the decays per
    harmonic HARP_A_DEC dB/s, no inharmonicity, steady pitch, a 2..8 kHz snap gone in ~25 ms). B: a wire-strung harp
    (the clarsach; NOT from the JD, which has none): plucked with the nail near the end, bright, the highs ringing
    long, and a slow shimmer of the doubled courses on the ring. The loop is the ring: the amp envelope decays it."""
    out = []
    ph = rng.uniform(0, 2 * np.pi, 64)
    for side in "AB":
        L = 0.12 if side == "A" else 0.50
        n, f0 = gp.loop_len(sr, gp.note_hz(60), L)
        df = sr / n
        nt = Note(sr, n, T_s)
        t = np.minimum(nt.t, nt.Ts)
        y = 0.0
        K = 20 if side == "A" else 26
        for k in range(1, K + 1):
            if k * f0 >= 0.45 * sr:
                break
            if side == "A":
                a, d = 10 ** (HARP_A_DB[k - 1] / 20), HARP_A_DEC[k - 1]
            else:
                a = k ** -0.75 * (0.25 + abs(np.sin(np.pi * k * 0.11))) / 1.25
                d = -3.0 - 4.0 * k
            g_end = 10 ** (d * nt.Ts / 20)
            env = a * (g_end + (1 - g_end) * (10 ** (d * t / 20) - g_end) / (1 - g_end))
            y = y + env * np.sin(2 * np.pi * k * f0 * nt.tau + ph[k])
            if side == "B" and 2 <= k <= 8:                    # the doubled course, a hair apart: a 2 Hz shimmer
                y = y + 0.35 * env * np.sin(2 * np.pi * (k * f0 + df) * nt.tau + ph[k + 30])
        y = y * (1 - np.exp(-nt.t / 0.0008))
        prng = np.random.default_rng(57 if side == "A" else 58)
        lo, hi, dec, lvl = (2000, 8000, 0.008, 0.10) if side == "A" else (3500, 0.45 * sr, 0.006, 0.16)
        snap = nt.noise(prng, lambda f: ((f > lo) & (f < hi)) * 1.0) * ga.env_ad(nt.t, 0.0003, dec) * nt.gone(0.1)
        out.append((y + lvl * np.abs(y).max() * snap, nt))
    return out


# ------------------------------------------------------------------------------------------- VOX > CHOIR --- #

def vox_choir(rng):
    """A: the male voice after JD wave 45 (waves9b solo_voice), alone; B: the seven-singer 'aah' choir (notes.choir)
    on the same grid, so SCAN is the others joining the singer"""
    ch = choir(rng)
    g = Grid(11025, gp.note_hz(57), 0.48, 13000)
    nt = Note(g.sr_a, g.n_a, 0.40)
    return [(solo_voice(g, nt, g.sr_a), nt), ch[1]]


VOWELS = {                                                   # male formants (Hz, bandwidth Hz): textbook values
    "M": [(260, 60), (950, 250), (2250, 300)],              # lips closed, the hum before the vowel
    "OH": [(450, 70), (800, 80), (2830, 120), (3300, 200)],
    "AH": [(730, 90), (1090, 110), (2440, 120), (2900, 130), (3400, 220)],
}


def vowel_gain(f, name):
    """the vowel's cascade of resonances at f (1 at 0 Hz)"""
    f = np.asarray(f, float)
    g = np.ones_like(f)
    for F, B in VOWELS[name]:
        g = g / np.sqrt((1 - (f / F) ** 2) ** 2 + (f * B / F ** 2) ** 2)
    return g


def male_vox(rng, L=0.48, T_s=0.35, sr_a=11025, sr_b=12000):
    """VOX (2026-11-05 redesign, user: one male voice OH -> AH, unlike SOLSTICE's choir). A: a monk's dark,
    intimate OH, a straight tone (only a slow drift), breath under it; its attack the lips opening, mm -> OH, in a
    soft 120 ms rise. B: the open AH, chest voice (a flatter tilt) and the singer's ring near 2.8 kHz, a slow vibrato
    (2 loop bins, 4.2 Hz, +-18 c) growing in over the attack; its attack a quick glottal onset scooping up 40 c."""
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    out = []
    for side, sr, n in (("A", g.sr_a, g.n_a), ("B", g.sr_b, g.n_b)):
        r = np.random.default_rng(57 if side == "A" else 58)
        nt = Note(sr, n, T_s)
        fv = g.f0
        u = nt.m / nt.n
        drift = sum(np.sin(2 * np.pi * h * u + r.uniform(0, 6.3)) / h for h in (1, 2)) / 1.5
        phase = 2 * np.pi * fv * nt.tau + (fv * (2 ** (6 / 1200) - 1) / g.df) * drift * 0.5
        if side == "B":
            vr = 2 * g.df
            vd = fv * (2 ** (18 / 1200) - 1)
            phase = phase + nt.ramp(0.12, nt.Ts) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + r.uniform(0, 6.3)) \
                + nt.scoop(fv, -40, 0.03)
        K = int(0.45 * sr / fv)
        k = np.arange(1, K + 1)
        tilt = k ** (-1.15 if side == "A" else -0.8)
        if side == "A":
            oh, mm = tilt * vowel_gain(k * fv, "OH"), tilt * vowel_gain(k * fv, "M") * (k <= 6)
            oh, mm = oh / oh.max(), mm / mm.max() * 0.8
            w = nt.ramp(0.05, 0.20)                                  # the lips open: mm -> OH
            amps = [mm[i] * (1 - w) + oh[i] * w for i in range(K)]
        else:
            ah = tilt * vowel_gain(k * fv, "AH")
            ah = ah / ah.max()
            amps = list(ah)
        voice = harm_sum(phase, amps, r.uniform(0, 6.3, K + 1), fv, sr)
        voice = voice / np.std(voice[nt.T:])
        vname = "OH" if side == "A" else "AH"
        coef = r.standard_normal(20000) + 1j * r.standard_normal(20000)
        breath = g.periodic_noise(coef, nt, lambda f: vowel_gain(f, vname) / vowel_gain(0, vname)
                                  * np.clip((f - 400) / 1500, 0.05, 1.0) * (f < 0.45 * sr))
        breath = breath / np.std(breath[nt.T:])
        if side == "A":
            on = nt.ramp(0.0, 0.12)
            y = on * (voice + 0.28 * breath)
        else:
            on = nt.ramp(0.0, 0.035)
            puff = nt.noise(r, lambda f: ga_band(f, 1800, 2.0) * (f < 0.45 * sr)) * ga.env_ad(nt.t, 0.002, 0.03) \
                * nt.gone(0.1)
            y = on * (voice + 0.12 * breath) + 0.5 * puff
        out.append((y, nt))
    return out


def shaker_rain(rng, sr=14000, LB=0.30, T_s=0.20):
    """SHAKER (2026-11-05): A the seed shaker as before; B (the user: the pebble B "bit into an apple") a RAIN of
    seeds through a stick, held it is the RAINSTICK: dense (~600 grains/s), high and soft, every grain about as loud
    as the next (one loud grain repeating every loop was RAINSTICK's tick), no pot body; the level breathes slowly
    across the loop as the seeds bunch. Its attack is the stick tipped: the fall thickening in, the loop under it
    from the first moment (no gap)."""
    a = shaker(rng)[0]
    prng = np.random.default_rng(4343)
    n = int(round(LB * sr))

    def seed(pr):
        m = int(sr * 0.004)
        k = np.arange(m) / sr
        f = pr.uniform(2600, 6200)
        return (np.sin(2 * np.pi * f * k + pr.uniform(0, 6.3)) + 0.4 * np.sin(2 * np.pi * f * 1.53 * k)) \
            * np.exp(-k / pr.uniform(0.0004, 0.0009))
    times = np.sort(prng.uniform(0, LB, prng.poisson(600 * LB)))
    ev = [(t, prng.uniform(0.55, 1.0) * (0.8 + 0.2 * np.sin(2 * np.pi * t / LB)), seed(prng)) for t in times]
    loop = gp.circular_hits(n, sr, ev)
    nt = Note(sr, n, T_s)
    y = loop[nt.m] * (0.35 + 0.65 * nt.ramp(0.0, 0.15))
    extra = np.zeros(nt.N + sr)                                  # the first rush as the stick tips
    for t in np.sort(prng.gamma(2.0, 0.03, 90)):
        if t < T_s - 0.03:
            s0 = int(t * sr); gg = seed(prng)
            extra[s0:s0 + len(gg)] += prng.uniform(0.4, 0.9) * gg
    y = y + 0.6 * extra[:nt.N] * nt.gone(0.08)
    return [a, (y, nt)]


def k_loud(x, sr):
    """loudness (dB) of x by the LUFS K-weighting: a high shelf +4 dB above ~1.7 kHz, a high-pass at ~60 Hz"""
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / sr)
    w = (1 + (10 ** (4 / 20) - 1) / (1 + (1700 / np.maximum(f, 1)) ** 2)) * (f > 60)
    return 10 * np.log10(np.mean(np.fft.irfft(X * w, n=len(x)) ** 2) + 1e-20)


def rainstick2(rng, sr_a=10000, sr_b=14000, LA=0.9, LB=0.30, T_s=0.20):
    """SHAKER as a RAINSTICK (2026-11-05, user: A was quieter than B; make them even, and a dramatic SCAN, sparse/low
    -> busy/high). A TRICKLE: the stick held nearly level, a few big seeds tumbling down the bamboo, often in pairs
    or threes, each a hollow wooden knock (two modes 0.8..2.4 kHz) with the tube answering low; ~30 grains/s on a
    0.9 s loop (long, so the sparse pattern does not repeat audibly), the bunching breathing across it. Its attack:
    the stick tipped, a handful of seeds letting go together. B RAIN: the stick tipped steep, a dense even fall of
    fine high seeds (~900/s, 3..7 kHz). Both loops sound from the first moment; held, each is a rainstick, and in
    the middle the trickle knocks through the rain. The loops are matched in K-weighted loudness (see L_)."""
    out = []
    # A: the trickle
    prng = np.random.default_rng(5151)
    n = int(round(LA * sr_a))

    def knock(pr, big=1.0):
        m = int(sr_a * 0.03)
        k = np.arange(m) / sr_a
        f1 = pr.uniform(800, 2400) / big ** 0.3
        body = 0.22 * np.sin(2 * np.pi * pr.uniform(480, 560) * k) * np.exp(-k / 0.010)    # the tube answering
        return (np.sin(2 * np.pi * f1 * k + pr.uniform(0, 6.3)) * np.exp(-k / pr.uniform(0.0015, 0.003))
                + 0.45 * np.sin(2 * np.pi * f1 * pr.uniform(1.9, 2.6) * k) * np.exp(-k / 0.0009) + body) \
            * (1 - np.exp(-k / 0.0002))
    ev = []
    for t in np.sort(prng.uniform(0, LA, prng.poisson(14 * LA))):        # tumbles: 1..3 seeds a hair apart
        lv = prng.uniform(0.35, 1.0) * (0.75 + 0.25 * np.sin(2 * np.pi * t / LA + 0.7))
        for j in range(prng.choice([1, 2, 2, 3])):
            ev.append(((t + j * prng.uniform(0.008, 0.03)) % LA, lv * prng.uniform(0.5, 1.0), knock(prng)))
    loop = gp.circular_hits(n, sr_a, ev)
    nt = Note(sr_a, n, T_s)
    y = loop[nt.m] * (0.5 + 0.5 * nt.ramp(0.0, 0.12))
    extra = np.zeros(nt.N + sr_a)                                  # the stick tipped: a handful let go
    for t in np.sort(prng.gamma(2.0, 0.025, 14)):
        if t < T_s - 0.04:
            s0 = int(t * sr_a); gg = knock(prng, 1.4)
            extra[s0:s0 + len(gg)] += prng.uniform(0.5, 1.0) * gg
    y = y + 0.45 * extra[:nt.N] * nt.gone(0.06)
    out.append((y, nt))
    # B: the rain
    prng = np.random.default_rng(4343)
    n = int(round(LB * sr_b))

    def seed(pr):
        m = int(sr_b * 0.004)
        k = np.arange(m) / sr_b
        f = pr.uniform(3000, 7000)
        return (np.sin(2 * np.pi * f * k + pr.uniform(0, 6.3)) + 0.4 * np.sin(2 * np.pi * f * 1.53 * k)) \
            * np.exp(-k / pr.uniform(0.0003, 0.0007))
    times = np.sort(prng.uniform(0, LB, prng.poisson(900 * LB)))
    ev = [(t, prng.uniform(0.55, 1.0) * (0.85 + 0.15 * np.sin(2 * np.pi * t / LB)), seed(prng)) for t in times]
    loop = gp.circular_hits(n, sr_b, ev)
    nt = Note(sr_b, n, T_s)
    y = loop[nt.m] * (0.4 + 0.6 * nt.ramp(0.0, 0.12))
    extra = np.zeros(nt.N + sr_b)                                  # the first rush as the stick tips
    for t in np.sort(prng.gamma(2.0, 0.03, 120)):
        if t < T_s - 0.03:
            s0 = int(t * sr_b); gg = seed(prng)
            extra[s0:s0 + len(gg)] += prng.uniform(0.4, 0.9) * gg
    y = y + 0.9 * extra[:nt.N] * nt.gone(0.08)
    out.append((y, nt))
    return out


# -------------------------------------------------------------------------------------- the two clouds --- #

def lore_slate(rng):
    """LORE: A the refined cane chimes (LORE STILL, 1.2 s), B the slate shards (WIND CHIMES' stone side).
    2026-11-05 the gap fixed: the chimes' loop sounds from the first moment (it used to fade in 0.12..0.33 s while
    the breeze faded out, a hole between them); the breeze/hand sweep is a gust on top, its strikes in the first
    0.15 s left to ring down before the handoff."""
    return [lore_long(rng, ramp=(0.0, 0.06), breeze=(0.15, 0.15))[0],
            wind_chimes(rng, ramp_b=(0.0, 0.06), gone_b=0.18)[1]]


def rain_angklung(rng):
    """RAINSTICK: A the trickle of seeds, B the angklung shaken (one player, the 8-shake loop)"""
    return [rainstick(rng)[0], angklung_long(rng)[0]]


# ------------------------------------------------------------------------------------------- STAR DUST --- #

def star_dust2(rng, sr=10000, L=0.8, T_s=0.30):
    """STAR DUST as a whole-note wave with a TONE under the glints, so it stands alone (user, 2026-11-05), and a
    dramatic SCAN. A STARLIGHT: a soft round glow (the fundamental, a twin a loop-bin above beating slowly, a faint
    octave), sparse glints, a slow glissando of glints as its attack. B STARFALL: a bright glassy chord of harmonics,
    each with a beating twin (shimmer), air, dense falling glints, its attack a burst of sparkle settling into the
    fall. The loops sound from the first moment (no gap); the tone swells in over the attack."""
    out = []
    for side in "AB":
        drng = np.random.default_rng(4321 if side == "A" else 8765)
        n, f0 = gp.loop_len(sr, gp.note_hz(DUST_ROOT), L)
        df = sr / n; k0 = int(round(f0 / df))
        nt = Note(sr, n, T_s)
        ph = drng.uniform(0, 2 * np.pi, 32)
        if side == "A":
            parts = [(1, 1.0, 0.55), (2, 0.16, 0.3), (3, 0.04, 0.0)]          # (harmonic, level, twin level)
        else:
            parts = [(1, 0.55, 0.4), (2, 0.6, 0.5), (3, 0.42, 0.5), (4, 0.34, 0.5), (5, 0.18, 0.5), (6, 0.2, 0.5),
                     (8, 0.12, 0.5), (10, 0.07, 0.5)]
        tone = 0.0
        for j, (h, lv, tw) in enumerate(parts):
            k = h * k0
            if (k + 1) * df >= 0.45 * sr:
                break
            tone = tone + lv * np.sin(2 * np.pi * k * df * nt.tau + ph[j])
            if tw:                                                       # the twin: a beat of df (1.25 Hz)
                tone = tone + lv * tw * np.sin(2 * np.pi * (k + 1) * df * nt.tau + ph[j + 16])
        tone /= np.sqrt(np.mean(tone[nt.T:] ** 2))
        swell = 0.35 + 0.65 * nt.ramp(0.0, 0.25) if side == "A" else 0.55 + 0.45 * nt.ramp(0.0, 0.15)

        H = [6, 8, 10, 12, 15, 16, 20] if side == "A" else [8, 10, 12, 15, 16, 18, 20]
        rate, d_rng, cascade = (8.0, (0.15, 0.5), False) if side == "A" else (22.0, (0.25, 0.6), True)
        ev = []
        for tt in np.sort(drng.uniform(0, L, drng.poisson(rate * L))):
            h = H[drng.integers(len(H))]
            a = 10 ** (drng.uniform(-18, 0) / 20)
            f = round(h * f0 / df) * df
            if f >= 0.45 * sr:
                continue
            ev.append((tt, a, glint(f, sr, drng, drng.uniform(*d_rng))))
            if cascade and drng.uniform() < 0.35:                        # a falling star: glints stepping down
                for i2, h2 in enumerate([x for x in H if x < h][-3:][::-1]):
                    ev.append(((tt + 0.055 * (i2 + 1)) % L, a * 0.75 ** (i2 + 1),
                               glint(round(h2 * f0 / df) * df, sr, drng, drng.uniform(*d_rng))))
        gl = gp.circular_hits(n, sr, ev)
        gl /= np.sqrt(np.mean(gl ** 2)) + 1e-12
        nb = n // 2 + 1; fq = np.arange(nb) * df
        c = (drng.standard_normal(nb) + 1j * drng.standard_normal(nb)) * ga_band(fq, 3200, 1.6) * (fq > 1200) \
            * (fq < 0.45 * sr)
        air = np.fft.irfft(c, n=n); air /= np.sqrt(np.mean(air ** 2)) + 1e-12
        g_gl, g_air = (0.45, 0.10) if side == "A" else (0.55, 0.22)
        y = tone * swell + (g_gl * gl + g_air * air)[nt.m] * nt.ramp(0.0, 0.04)
        # the attack's own glints, gone by the handoff
        at = np.zeros(nt.N + sr)
        if side == "A":                                                  # a slow glissando up the harmonics
            for i, h in enumerate([4, 5, 6, 8, 10, 12, 15, 16, 20]):
                if h * f0 >= 0.45 * sr:
                    break
                s0 = int((0.005 + 0.024 * i) * sr); gg = glint(h * f0, sr, drng, 0.18)
                at[s0:s0 + len(gg)] += (0.5 + 0.3 * i / 8) * gg
        else:                                                            # a burst of sparkle, thinning out
            for _ in range(26):
                tt = drng.exponential(0.05)
                if tt > 0.2:
                    continue
                h = [10, 12, 15, 16, 18, 20][drng.integers(6)]
                if h * f0 >= 0.45 * sr:
                    continue
                s0 = int(tt * sr); gg = glint(h * f0, sr, drng, drng.uniform(0.06, 0.16))
                at[s0:s0 + len(gg)] += 10 ** (drng.uniform(-8, 0) / 20) * gg
            at[:nt.N] += 0.5 * nt.noise(drng, lambda f: ga_band(f, 4000, 1.5) * (f < 0.45 * sr)) \
                * ga.env_ad(nt.t, 0.002, 0.06)
        y = y + 0.9 * at[:nt.N] * nt.gone(0.1)
        out.append((y, nt))
    return out


# ---------------------------------------------------------------------------------------------- VOICES --- #

WHISPER_VOWELS = [[(330, 80, 1.0), (900, 120, 0.3), (2400, 200, 0.1)],         # ooh
                  [(500, 90, 1.0), (900, 100, 0.6), (2700, 200, 0.15)],        # oh
                  [(850, 110, 1.0), (1250, 120, 0.8), (2800, 200, 0.35)],      # aah
                  [(600, 90, 1.0), (2000, 150, 0.6), (2900, 200, 0.3)]]        # eh


# measured from the user's JD-800 recording (2026-11-05; numbers only, no audio): harmonics by number (dB vs H1,
# sustain) and breath per band (dB of noise against the harmonics in that band)
JD43_H = [0, -15, -36, -45, -41, -55, -50, -58, -48, -49, -48, -52, -40, -36, -39, -34, -32, -34, -37, -36, -35, -40,
          -42, -44, -48, -51, -54, -61, -67, -71]
JD43_BREATH = [(0, 1000, -7), (1000, 2000, -10), (2000, 5000, -4), (5000, 10000, -4)]
JD45_H = [0, -34, -20, -37, -44, -47, -31, -50, -41, -55, -56, -55, -55, -55, -51, -47, -42, -26, -31, -27, -29, -30,
          -30, -35, -35, -38, -43, -44, -46, -52]
JD45_BREATH = [(0, 1000, -4), (1000, 2000, -3), (2000, 5000, -10), (5000, 10000, -7)]


BREATH_TRIM = -10.0     # the JD's 'noise' bins hold its drifting harmonics' skirts too: matched to its 98 % tonal


def jd_voice(g, nt, sr, H_db, breath, drift_c, vib, swell, rise, seed):
    """one voice to the JD's numbers: harmonics by number (as a sample, they move with the pitch), a slow drift of
    drift_c cents, an optional vibrato (bins, cents), the level swelling slowly (swell dB, 1 loop bin), breath per
    band at the measured level against the harmonics, the rise over `rise` s"""
    r = np.random.default_rng(seed)
    fv = g.f0
    u = nt.m / nt.n
    drift = sum(np.sin(2 * np.pi * h * u + r.uniform(0, 6.3)) / h for h in (1, 2)) / 1.5
    phase = 2 * np.pi * fv * nt.tau + (fv * (2 ** (drift_c / 1200) - 1) / g.df) * drift * 0.5
    if vib:
        vr, vd = vib[0] * g.df, fv * (2 ** (vib[1] / 1200) - 1)
        phase = phase + nt.ramp(0.1, nt.Ts) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + r.uniform(0, 6.3))
    K = min(len(H_db), int(0.45 * sr / fv))
    voice = harm_sum(phase, [10 ** (H_db[k] / 20) for k in range(K)], r.uniform(0, 6.3, K + 1), fv, sr)
    # breath: per band, its power = the harmonics' power in that band x the measured ratio
    loop = voice[nt.T:]
    Sv = np.abs(np.fft.rfft(loop)) ** 2
    fq = np.fft.rfftfreq(len(loop), 1 / sr)
    br = 0.0
    for lo, hi, db in breath:
        if lo >= 0.45 * sr:
            continue
        hv = Sv[(fq >= lo) & (fq < hi)].sum()
        coef = r.standard_normal(20000) + 1j * r.standard_normal(20000)
        nz = g.periodic_noise(coef, nt, lambda f, lo=lo, hi=hi: ((f >= lo) & (f < hi) & (f < 0.45 * sr)) * 1.0)
        Sn = (np.abs(np.fft.rfft(nz[nt.T:])) ** 2).sum()
        br = br + nz * np.sqrt(hv * 10 ** ((db + BREATH_TRIM) / 10) / (Sn + 1e-20))
    lvl = 10 ** (swell / 20 * np.sin(2 * np.pi * u + r.uniform(0, 6.3)))
    return (voice + br) * lvl * nt.ramp(0.0, rise)


# 2026-11-05 (user: the JD recording has REVERB, "I hear a resonance"): harmonic level H1..30 (peak, dB vs H1) and
# how far each peak stands above the valley around it (dB), long window. Above ~H7 JD 43's "ring" barely stands out
# (0..16 dB): it is breath/air smeared by the reverb, NOT harmonics; drawn as harmonics it whistled. Each harmonic
# now keeps only what stands above its valley; the valley floor becomes smooth air (minus the reverb's share,
# matched to the JD's tonality). The chain itself (JD saws 1-3 vs 1/k) only tilts 3-5 dB, smoothly: left as is.
JD43_LV = [0, -17, -36, -45, -44, -61, -56, -62, -55, -56, -53, -56, -52, -39, -44, -45, -37, -41, -41, -40, -45, -48,
           -54, -52, -49, -55, -61, -70, -73, -78]
JD43_PV = [54, 44, 31, 28, 29, 13, 16, 4, 6, 2, 4, 5, 10, 16, 14, 7, 8, 8, 12, 11, 2, 4, -1, 2, 9, 6, 5, 1, 4, 6]
JD45_LV = [0, -34, -20, -45, -45, -47, -31, -50, -65, -57, -56, -55, -55, -60, -52, -47, -50, -27, -36, -31, -36, -35,
           -39, -40, -43, -38, -51, -44, -57, -56]
JD45_PV = [49, 24, 39, 14, 22, 25, 35, 20, 2, 16, 17, 19, 15, 11, 19, 18, 8, 19, 11, 15, 9, 12, 9, 11, 11, 15, 7, 14, 3, 12]
AIR_TRIM = 24.0     # +28 = the JD's own tonality (reverb included); 4 dB under: the reverb's share


def jd_voice2(g, nt, sr, LV, PV, drift_c, vib, swell, rise, seed):
    """a voice to the JD's numbers, the reverb taken out: each harmonic only the power standing above its valley,
    the valleys (smoothed over 5 harmonics) as soft periodic air, AIR_TRIM dB down (the reverb's share)"""
    r = np.random.default_rng(seed)
    fv = g.f0
    u = nt.m / nt.n
    drift = sum(np.sin(2 * np.pi * h * u + r.uniform(0, 6.3)) / h for h in (1, 2)) / 1.5
    phase = 2 * np.pi * fv * nt.tau + (fv * (2 ** (drift_c / 1200) - 1) / g.df) * drift * 0.5
    if vib:
        vr, vd = vib[0] * g.df, fv * (2 ** (vib[1] / 1200) - 1)
        phase = phase + nt.ramp(0.1, nt.Ts) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + r.uniform(0, 6.3))
    lv, pv = np.array(LV, float), np.array(PV, float)
    harm_pow = 10 ** (lv / 10) * np.clip(1 - 10 ** (-pv / 10), 0, 1)
    K = min(len(lv), int(0.45 * sr / fv))
    voice = harm_sum(phase, list(np.sqrt(harm_pow[:K])), r.uniform(0, 6.3, K + 1), fv, sr)
    valley = np.convolve(10 ** ((lv - pv) / 10), np.ones(5) / 5, "same")      # power per harmonic spacing
    kk = np.arange(1, len(lv) + 1)
    coef = r.standard_normal(20000) + 1j * r.standard_normal(20000)
    air = g.periodic_noise(coef, nt, lambda f: np.sqrt(np.interp(f / fv, kk, valley, right=0.0)) * (f > 0.5 * fv)
                           * (f < 0.45 * sr))
    # scale: the air's power per harmonic spacing against the voice's, as measured, minus the reverb's share
    Sv = (np.abs(np.fft.rfft(voice[nt.T:])) ** 2).sum()
    Sa = (np.abs(np.fft.rfft(air[nt.T:])) ** 2).sum()
    want = harm_pow[:K].sum()
    air = air * np.sqrt(Sv / (Sa + 1e-20) * valley[:K].sum() / want * 10 ** (AIR_TRIM / 10))
    lvl = 10 ** (swell / 20 * np.sin(2 * np.pi * u + r.uniform(0, 6.3)))
    return (voice + air) * lvl * nt.ramp(0.0, rise)


def voices_jd(rng, L=0.48, T_s=0.35, sr_a=11025, sr_b=13000):
    """VOICES from the JD-800 (user: waves 43 and 45, 2026-11-05). A: JD 43, a dark pure 'ooh' voice (H2 -15,
    H3 -36, a faint ring around H13..22), breath 4-10 dB under the harmonics, a slow drift (+-8 c), soft swells, a
    slow rise. B: JD 45, the male voice (H3 -20, H7 -31, the ring around H18..23), a slow light vibrato (2 Hz, 4 c),
    breath as measured. Mostly tone, as the JD's are (the WHISPERS were "way too noisy")."""
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    na = Note(g.sr_a, g.n_a, T_s)
    nb = Note(g.sr_b, g.n_b, T_s)
    a = jd_voice2(g, na, g.sr_a, JD43_LV, JD43_PV, 8, None, 1.5, 0.25, 43)
    b = jd_voice2(g, nb, g.sr_b, JD45_LV, JD45_PV, 5, (1, 4), 1.0, 0.09, 45)
    return [(a, na), (b, nb)]


def voices(rng, L=0.72, T_s=0.35, sr_a=10000, sr_b=9000):
    """VOICES (2026-11-05, user: a "voices" wave instead of a male VOX). A WHISPERS: five voices whispering, each
    breath shaped by a vowel drifting ooh -> oh -> aah -> eh around the loop (each voice at its own point), under
    them one faint hummed 'ooh' so a note keeps a pitch; its attack the whispers swelling in one by one. B HIGH
    VOICES: five soft high 'ooh' voices, near-sine, a loop bin apart (a slow chorus), each with its own vibrato
    blooming after the entry, a little air; its attack the voices entering a hair apart, sliding up onto the note.
    Not CHOIR (full ooh/aah vowels) nor SOLSTICE: spirits whispering, then singing."""
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    out = []
    # A: the whispers
    nt = Note(g.sr_a, g.n_a, T_s)
    sr = g.sr_a
    u = nt.m / nt.n
    y = 0.0
    for p in range(5):
        off = p / 5 + rng.uniform(-0.05, 0.05)
        ws = [np.maximum(0.0, np.cos(2 * np.pi * (u + off - i / 4))) ** 2 for i in range(4)]
        tot = sum(ws) + 1e-9
        e0 = 0.03 + 0.04 * p + rng.uniform(0, 0.02)
        on = nt.ramp(e0, min(e0 + 0.12, nt.Ts))
        for i, v in enumerate(WHISPER_VOWELS):
            coef = rng.standard_normal(20000) + 1j * rng.standard_normal(20000)
            nz = g.periodic_noise(coef, nt, lambda f, v=v: formant(f, v, 0.03) * (f > 250) * (f < 0.45 * sr)
                                  * (1 + 0.5 * np.clip((f - 2500) / 1500, 0, 1)))
            y = y + on * ws[i] / tot * nz / (np.std(nz[nt.T:]) + 1e-12)
    ooh = [(330, 60, 1.0), (900, 90, 0.2), (2400, 160, 0.03)]
    fv = g.f0
    k_top = int(0.45 * sr / fv)
    hum = harm_sum(2 * np.pi * fv * nt.tau, [k ** -1.8 * formant(k * fv, ooh, 0.01) for k in range(1, k_top + 1)],
                   rng.uniform(0, 6.3, k_top + 1), fv, sr)
    hum = hum / np.std(hum[nt.T:])
    out.append((y / np.std(y[nt.T:]) + 0.30 * hum * nt.ramp(0.05, 0.30), nt))
    # B: the high voices
    nt = Note(g.sr_b, g.n_b, T_s)
    sr = g.sr_b
    u = nt.m / nt.n
    vow = [(350, 60, 1.0), (900, 90, 0.22), (2800, 200, 0.04)]
    y = 0.0
    for step, vcyc, vc in ((-1, 4, 11), (0, 4, 9), (0, 3, 13), (1, 4, 10), (1, 3, 12)):
        fv = g.f0 + step * g.df
        vr, vd = vcyc * g.df, fv * (2 ** (vc / 1200) - 1)
        wph = rng.uniform(0, 2 * np.pi, 3)
        wan = sum(np.sin(2 * np.pi * h * u + wph[h - 1]) / h for h in range(1, 4)) / 1.83
        e0 = rng.uniform(0.0, 0.07)
        phase = 2 * np.pi * fv * nt.tau + nt.ramp(e0 + 0.10, nt.Ts) * vd / vr \
            * np.sin(2 * np.pi * vr * nt.tau + rng.uniform(0, 6.3)) + 0.4 * wan + nt.scoop(fv, -rng.uniform(30, 70), 0.04)
        k_top = int(0.45 * sr / fv)
        voice = harm_sum(phase, [k ** -1.6 * formant(k * fv, vow, 0.005) for k in range(1, k_top + 1)],
                         rng.uniform(0, 6.3, k_top + 1), fv, sr)
        y = y + nt.ramp(e0, e0 + 0.10) * voice
    coef = rng.standard_normal(20000) + 1j * rng.standard_normal(20000)
    air = g.periodic_noise(coef, nt, lambda f: formant(f, vow, 0.05) * (f > 300))
    y = y / np.std(y[nt.T:]) + 0.10 * air / np.std(air[nt.T:]) * nt.ramp(0.0, 0.1)
    out.append((y, nt))
    return out


# ------------------------------------------------------------------------------------------- ROCK GONG --- #

def stone_side(sr, L, T_s, parts, seed):
    """partials (ratio, start dB, loop dB, tau s, twin dB or None) on the loop grid: each goes from its start level to
    its loop level by `tau` (exactly there at the handoff: the loop holds it), a twin one loop-bin above beating
    slowly with it"""
    n, f0 = gp.loop_len(sr, gp.note_hz(57), L)
    df = sr / n
    nt = Note(sr, n, T_s)
    r = np.random.default_rng(seed)
    t = np.minimum(nt.t, nt.Ts)
    y = 0.0
    for ratio, d0, d1, tau, tw in parts:
        f = round(ratio * f0 / df) * df
        if f + df >= 0.45 * sr or f <= 0:
            continue
        a0, a1 = 10 ** (d0 / 20), 10 ** (d1 / 20)
        if tau:
            g = (np.exp(-t / tau) - np.exp(-nt.Ts / tau)) / (1 - np.exp(-nt.Ts / tau))
            env = a1 + (a0 - a1) * g
        else:
            env = a1
        y = y + env * np.sin(2 * np.pi * f * nt.tau + r.uniform(0, 6.3))
        if tw is not None:
            y = y + 10 ** (tw / 20) * env * np.sin(2 * np.pi * (f + df) * nt.tau + r.uniform(0, 6.3))
    return y, nt, f0, df, r


def rock_gong(rng, L=0.5, T_s=0.40, sr_a=10000, sr_b=12000):
    """ROCK GONG (2026-11-05, user: something MOODY with the M1 Universe / D-50 Fantasia wow, neolithic; from JD-800
    waves 90 and 88, measured from the user's recording at their real notes, 270.12 s and 265.46 s; then improvised).
    A, the ringing rock struck (JD 90): a gong comb 1.84 2.11 2.39 2.68 2.95 3.19/3.25 3.51 3.82 4.08 x, levels as
    measured, dying at 75..125 dB/s as measured; the fundamental ringing on (-18 dB/s), a dark hum at 0.35 x. Then
    (improvised) the bloom: the hum and the octave-plus-minor-third partial (2.39) stay and breathe as a dark drone,
    beating slowly against their twins, the chamber's low air sighing under it. B, the stone bowed (JD 88): a tap,
    then the tone swelling in over 0.3 s (fundamental, H2 -10, H3 -21, a partial a fourth below at 0.76 x, a beating
    cluster 3.51/3.66/3.82 x, glassy partials 8.07 9.10 18.65 x as measured); improvised: the bow's grit catching,
    pitched, as it swells, and the glass partials beating so the stone whistles."""
    # A: struck
    # 2026-11-05 user: the low slow beat "too obvious and cyclical" (a 0.5 s loop repeats any beat twice a second),
    # and the strike may be muddy in chords: no twins on the low partials, the rest shallow (-20 dB); the comb thinned
    # to its 7 most telling partials (the minor third kept) and dying ~30 % faster
    pa = [(1.0, 0, -18, 0.40, None), (0.347, -8, -40, 0.10, None), (1.835, -5, -38, 0.066, None),
          (2.393, -8, -30, 0.062, -20), (2.681, -6, -46, 0.067, None), (3.187, -7, -44, 0.077, None),
          (3.246, -8, -44, 0.077, None), (3.816, -8, -50, 0.080, None), (4.081, -9, -52, 0.05, None),
          # improvised: the bloom, a dark minor drone rising out of the dying comb (sub-octave, root, minor third)
          (0.5, -60, -12, 0.22, None), (1.189, -60, -20, 0.22, -22)]
    ya, nta, f0, df, r = stone_side(sr_a, L, T_s, pa, 90)
    ya = ya * (1 - np.exp(-nta.t / 0.0008))
    mallet = nta.noise(r, lambda f: ga_band(f, 2200, 2.0) * (f < 0.45 * sr_a)) * ga.env_ad(nta.t, 0.0003, 0.005) * nta.gone(0.1)
    u = nta.m / nta.n
    air = Grid(sr_a, gp.note_hz(57), L).periodic_noise(r.standard_normal(20000) + 1j * r.standard_normal(20000), nta,
                                                        lambda f: ga_band(f, 330, 1.4) * (f > 60))
    air = air / np.std(air[nta.T:]) * (0.6 + 0.4 * np.sin(2 * np.pi * u + 1.0)) * nta.ramp(0.05, 0.35)
    tone_rms = np.std(ya[nta.T:])
    ya = ya + 0.25 * np.abs(ya).max() * mallet + 10 ** (-20 / 20) * tone_rms * 2.5 * air
    # B: bowed
    pb = [(0.764, -22, -22, 0, None), (1.0, 0, 0, 0, None), (2.0, -10, -10, 0, -22), (3.0, -21, -21, 0, None),
          (3.51, -14, -16, 0, -18), (3.66, -14, -16, 0, None), (3.815, -14, -16, 0, None),     # improvised: +8 dB,
          (8.07, -14, -14, 0, -12), (9.10, -14, -14, 0, -12), (18.65, -14, -16, 0, -14)]    # the stone whistles
    yb, ntb, f0b, dfb, rb = stone_side(sr_b, L, T_s, pb, 88)
    swell = 10 ** (-20 * (1 - ntb.ramp(0.02, 0.30)) / 20)
    yb = yb * swell * (1 - np.exp(-ntb.t / 0.002))
    tap = ntb.noise(rb, lambda f: ga_band(f, 3000, 1.6) * (f < 0.45 * sr_b)) * ga.env_ad(ntb.t, 0.0002, 0.006) * ntb.gone(0.1)
    grit = ntb.noise(rb, lambda f: ga_band(f, 2600, 1.5) * (f < 0.45 * sr_b)) \
        * (0.5 + 0.5 * np.sin(2 * np.pi * f0b * ntb.tau)) * np.exp(-ntb.t / 0.12) * ntb.ramp(0.0, 0.03) * ntb.gone(0.1)
    pk = np.abs(yb[ntb.T:]).max()
    yb = yb + 0.9 * pk * tap + 0.18 * pk * grit
    return [(ya, nta), (yb, ntb)]


# ----------------------------------------------------------------------------------- the new percussion --- #

def barrow_drum(rng):
    """BARROW DRUM (2026-11-05, replaces POT DRUM): a big low skin drum, NO pitch sag (the pitch envelope does that).
    A: a soft felt beater, a round dry thud, the overtones damped fast. B: a hard beater inside the barrow's stone
    chamber: a sharper knock and the chamber's low resonances (near 1.5x and 2.3x) booming on with the drum."""
    R = [1.0, 1.59, 2.14, 2.30, 2.65]
    phs = rng.uniform(0, 2 * np.pi, 8)
    a = struck(11025, 36, 0, [(1.0, 1.0, 0.30, 0.12), (1.59, 0.35, 0.0, 0.04), (2.14, 0.2, 0.0, 0.025),
                              (2.30, 0.15, 0.0, 0.02), (2.65, 0.1, 0.0, 0.015)],
               [(160, 1.5, 0.6, 0.002, 0.02), (900, 1.5, 0.15, 0.001, 0.006)], phs=phs, seed=61, onset=0.003)
    b = struck(11025, 36, 0, [(1.0, 1.0, 0.30, 0.14), (1.5, 0.45, 0.16, 0.20), (1.59, 0.4, 0.0, 0.05),
                              (2.14, 0.3, 0.0, 0.035), (2.3, 0.3, 0.09, 0.25), (2.65, 0.2, 0.0, 0.02)],
               [(1400, 1.6, 0.6, 0.0004, 0.006), (220, 1.5, 0.5, 0.001, 0.025)], phs=phs, seed=62, onset=0.0008)
    return [a, b]


def stone_clack(rng):
    """STONE CLACK (2026-11-05, replaces FRAME DRUM; the rocky family of MENHIR the user likes). A: two river
    stones knocked together, a dull hard clack (stone modes 1..3.3x around 1 kHz, gone in ~15 ms, a tiny ring).
    B: flint knapped: a sharper crack and the flint's glassy tink ringing on (modes higher, longer)."""
    phs = rng.uniform(0, 2 * np.pi, 8)
    a = struck(12000, 84, 0, [(1.0, 1.0, 0.003, 0.018), (1.47, 0.7, 0.0, 0.012), (2.09, 0.6, 0.0, 0.009),
                              (2.56, 0.4, 0.0, 0.007), (3.3, 0.3, 0.0, 0.005)],
               [(2500, 2.0, 0.9, 0.0001, 0.003), (700, 1.5, 0.3, 0.0005, 0.006)], phs=phs, seed=63, onset=0.0002)
    b = struck(12000, 84, 0, [(1.0, 0.6, 0.0, 0.02), (1.58, 1.0, 0.10, 0.09), (2.32, 0.7, 0.05, 0.06),
                              (3.1, 0.5, 0.02, 0.04), (4.05, 0.35, 0.0, 0.02)],
               [(4800, 1.5, 0.8, 0.00005, 0.0015)], phs=phs, seed=64, onset=0.0001)
    return [a, b]


def lyre(rng, sr=12000, T_s=0.30, L=0.12):
    """LYRE (2026-11-05, replaces HARP). A: a gut string plucked with the finger, the harmonics and their decays
    from the JD-800's wave 57 harp (HARP_A_DB/DEC, measured), through a small tortoise-shell soundbox (body
    resonances ~260 Hz and ~900 Hz). B: a plectrum on a buzzing bridge: every harmonic struck bright, and the
    bridge's buzz, a resonance sweeping down the harmonics from the 18th to the 6th over 0.25 s and staying there
    (a sitar-like 'wow'). The loop is the ring; the tail fix decays it as the attack did."""
    out = []
    ph = rng.uniform(0, 2 * np.pi, 64)
    for side in "AB":
        n, f0 = gp.loop_len(sr, gp.note_hz(60), L)
        nt = Note(sr, n, T_s)
        t = np.minimum(nt.t, nt.Ts)
        y = 0.0
        kc = 6 * 3.0 ** (1 - nt.ramp(0.0, 0.25)) if side == "B" else None
        for k in range(1, 31):
            f = k * f0
            if f >= 0.45 * sr:
                break
            if side == "A":
                if k > len(HARP_A_DB):
                    break
                a, d = 10 ** (HARP_A_DB[k - 1] / 20), HARP_A_DEC[k - 1]
                a *= 1 + 0.8 * np.exp(-0.5 * (np.log2(f / 260) / 0.35) ** 2) + 0.5 * np.exp(-0.5 * (np.log2(f / 900) / 0.3) ** 2)
                bz = 1.0
            else:
                a = k ** -0.55
                d = -4.0 - 3.0 * k
                bz = 0.35 + 1.6 * np.exp(-0.5 * (np.log2(k / kc) / 0.3) ** 2)
            g_end = 10 ** (d * nt.Ts / 20)
            env = a * (g_end + (1 - g_end) * (10 ** (d * t / 20) - g_end) / (1 - g_end))
            y = y + env * bz * np.sin(2 * np.pi * f * nt.tau + ph[k])
        y = y * (1 - np.exp(-nt.t / (0.0012 if side == "A" else 0.0003)))
        prng = np.random.default_rng(71 if side == "A" else 72)
        lo, hi, dec, lvl = (300, 3000, 0.006, 0.06) if side == "A" else (2500, 0.45 * sr, 0.004, 0.14)
        snap = nt.noise(prng, lambda f: ((f > lo) & (f < hi)) * 1.0) * ga.env_ad(nt.t, 0.0003, dec) * nt.gone(0.1)
        out.append((y + lvl * np.abs(y).max() * snap, nt))
    return out


# ------------------------------------------------------------------------- the tweaks (2026-11-05 night) --- #

def knock(nt, f0, ratios, q, dec, seed, levels=None):
    """a wooden knock: noise through a cluster of heavily damped modes (bands at f0 x r, width f/q), what wood's
    dense lossy modes sound like. JD-800 wave 77 (a dry wood crack: modes 1.25..2.4 kHz, -16 dB in 50 ms) and wave
    49 (log drum: a cluster 1.25..2.3 x f0, noise 3..6 dB over the harmonics for the first 60 ms) measure this way."""
    pr = np.random.default_rng(seed)
    lv = levels or [1.0] * len(ratios)
    shape = lambda f: sum(l * np.exp(-0.5 * ((f - r * f0) / (r * f0 / q)) ** 2) for r, l in zip(ratios, lv)) \
        * (f < 0.45 * nt.sr)
    return nt.noise(pr, shape) * ga.env_ad(nt.t, 0.0002, dec) * nt.gone(0.1)


def wood_block2(rng, sr=11025):
    """WOOD BLOCK, woodier (user, 2026-11-05): the block's two tones ring shorter and quieter under a hollow wooden
    knock, a cluster of damped modes 1.6..3.8 x f0 (JD 77's crack, scaled to the block). A: the soft mallet, the
    knock round and low; B: the hard stick, the knock reaching up to 6 x and drier."""
    phs = rng.uniform(0, 6.3, 6)
    f0 = gp.note_hz(72)
    a, na = _struck(sr, 72, 0.12, [(1.0, 1.0, 0.06, 0.035), (2.76, 0.30, 0.0, 0.012), (4.35, 0.08, 0.0, 0.006)],
                    [(1500, 2.0, 0.2, 0.0005, 0.004)], 0.0, 0.0008, (0.0, 0.03), T_DRUM, 61, phs)
    b, nb = _struck(sr, 72, 0.12, [(1.0, 1.0, 0.05, 0.03), (2.76, 0.6, 0.012, 0.014), (4.35, 0.3, 0.0, 0.007),
                                   (6.1, 0.15, 0.0, 0.004)], [(3500, 2.0, 0.6, 0.0002, 0.0025)], 0.0, 0.0002,
                    (0.0, 0.03), T_DRUM, 62, phs)
    a = a + 0.55 * np.abs(a).max() * knock(na, f0, [1.6, 1.9, 2.3, 2.7, 3.2, 3.8], 9, 0.011, 161,
                                           [1.0, 0.9, 0.8, 0.6, 0.4, 0.25])
    b = b + 0.70 * np.abs(b).max() * knock(nb, f0, [1.9, 2.3, 2.7, 3.2, 3.8, 4.4, 5.2, 6.0], 10, 0.007, 162,
                                           [0.6, 0.8, 1.0, 0.9, 0.8, 0.6, 0.4, 0.3])
    return [(a, na), (b, nb)]


def log_marimba2(rng, sr=11025):
    """LOG MARIMBA, woodier (user, 2026-11-05), after JD-800 wave 48 (Marim Wave: its loop H4 -9 dB, H10 -12, H9 -23,
    H3 -27) and wave 49 (the log drum's knock, a cluster 1.25..2.3 x f0). The bar's 1 : 4 : 10 ring with the 4th up
    toward the JD's level, the upper partial dying fast, the knock of the bar under the mallet. A: yarn mallets; B:
    hard mallets, the 4th and 10th forward, the knock drier and higher."""
    phs = rng.uniform(0, 6.3, 5)
    f0 = gp.note_hz(60)
    a, na = _struck(sr, 60, 0.12, [(1.0, 1.0, 0.30, 0.30), (3.93, 0.35, 0.05, 0.06), (9.94, 0.08, 0.0, 0.015)],
                    [(500, 1.5, 0.15, 0.002, 0.008)], 0.0, 0.002, (0.0, 0.03), T_DRUM, 63, phs)
    b, nb = _struck(sr, 60, 0.12, [(1.0, 1.0, 0.26, 0.26), (3.93, 0.65, 0.09, 0.06), (9.94, 0.35, 0.03, 0.018)],
                    [(2500, 1.8, 0.3, 0.0003, 0.003)], 0.0, 0.0004, (0.0, 0.03), T_DRUM, 64, phs)
    jd49 = [1.25, 1.35, 1.44, 1.54, 1.77, 1.87, 1.99, 2.18, 2.30]
    a = a + 0.40 * np.abs(a).max() * knock(na, f0, jd49, 18, 0.012, 163)
    b = b + 0.55 * np.abs(b).max() * knock(nb, f0, jd49 + [2.9, 3.4, 5.5], 18, 0.008, 164,
                                           [0.7] * 9 + [0.6, 0.5, 0.4])
    return [(a, na), (b, nb)]


def lore_wood(rng):
    """LORE, woodier (user, 2026-11-05): the cane chimes as hollow wood (lore_long wood=1: a louder hollow knock
    under each strike, shorter rings, less of the metallic twin), the gap fix kept; B the slate as before"""
    return [lore_long(rng, ramp=(0.0, 0.06), breeze=(0.15, 0.15), wood=1.0)[0],
            wind_chimes(rng, ramp_b=(0.0, 0.06), gone_b=0.18)[1]]


def pan_flute2(rng):
    """PAN FLUTE (user, 2026-11-05: A more hollow, B airier at 16 kHz). The round-6 pipe and its attack, the tone
    retuned to the JD-800: A after wave 94 (the blown bottle: a near-pure fundamental, H2 -40, H3 -58, the breath
    a soft band at the pipe's own note, the evens all but gone); B after wave 93 (the pan pipe: odd harmonics H3 -14,
    H5 -15, evens -27..-29, breath 3..8 dB over the harmonics above 2 kHz, a 4 Hz vibrato), at 16 kHz for the air."""
    g = Grid(11025, gp.note_hz(72), 0.48, 16000)
    ph = rng.uniform(0, 2 * np.pi, 40)
    coef = rng.standard_normal(8000) + 1j * rng.standard_normal(8000)
    vph, wph = rng.uniform(0, 2 * np.pi), rng.uniform(0, 2 * np.pi, 6)
    hollow = [1.0, 0.01, 0.035, 0.0, 0.008]
    airy = [1.0, 0.035, 0.20, 0.04, 0.18, 0.03, 0.09, 0.02, 0.06, 0.01, 0.04]
    out = []
    for side, amps0, air_l, rush, catch, cents, whistle, seed in [("A", hollow, 0.16, 0.40, 0.050, -30, 0.18, 1),
                                                                  ("B", airy, 0.42, 0.65, 0.025, -45, 0.45, 2)]:
        sr, n = (g.sr_a, g.n_a) if side == "A" else (g.sr_b, g.n_b)
        nt = Note(sr, n, 0.30)
        fv = g.f0
        u = nt.m / nt.n
        vr, vd = 2 * g.df, fv * (2 ** ((6 if side == "A" else 9) / 1200) - 1)
        wan = sum(np.sin(2 * np.pi * h * u + wph[h - 1]) / h for h in range(1, 4)) / 1.83
        phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.10, 0.28) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + vph) \
            + 0.5 * wan + nt.scoop(fv, cents, 0.035)
        amps = []
        for k, a in enumerate(amps0, start=1):
            env = nt.ramp(catch * 0.4, catch * 0.4 + 0.07) if k == 1 else nt.ramp(catch, catch + 0.10)
            amps.append(a * env)
        tone = harm_sum(phase, amps, ph, fv, sr)
        wh = whistle * np.exp(-np.maximum(nt.t - catch * 0.5, 0) / 0.03) * ss((nt.t - catch * 0.3) / 0.015) \
            * nt.gone(0.1) * np.sin(3 * phase + ph[2])
        if side == "A":     # the bottle's breath: a soft band on the note itself, the hiss low
            shape = lambda f: (np.exp(-0.5 * ((f - fv) / 45) ** 2) + 0.25 * ga_band(f, 1800, 2.0)) * (f < 0.45 * sr)
        else:               # the pipe's air: pitched at the odd harmonics, then a wide breath up to ~7 kHz
            shape = lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 50) ** 2) for h in (1, 3, 5)) * 0.4
                               + ga_band(f, 4200, 2.6)) * (f < 0.45 * sr)
        air = g.periodic_noise(coef, nt, shape) * (0.6 + 0.4 * np.sin(phase))
        edge = nt.noise(np.random.default_rng(seed), lambda f: ga_band(f, 2800 if side == "A" else 4500, 2.4)
                        * (f > 300) * (f < 0.45 * sr))
        edge_env = ss(nt.t / 0.012) * np.exp(-nt.t / 0.06) * nt.gone(0.08)
        y = tone + wh + air_l * nt.ramp(0.0, catch + 0.08) * air + rush * edge_env * edge
        out.append((y, nt))
    return out


def pan_flute3(rng):
    """PAN FLUTE for new-age music (user, 2026-11-05: pan_flute2 "much too resonant"; go back to the earlier pipes).
    The round-6 pipe (notes.pan_flute, the one the user called "pretty good") and its attack as they were, the tone
    only nudged: A a touch more hollow (the evens a little lower), B the same pipe blown with more breath, the breath
    a SOFT, WIDE hiss (no narrow pitched bands: they rang), stored at 16 kHz so the air reaches up."""
    g = Grid(11025, gp.note_hz(72), 0.48, 16000)
    ph = rng.uniform(0, 2 * np.pi, 40)
    coef = rng.standard_normal(8000) + 1j * rng.standard_normal(8000)
    vph, wph = rng.uniform(0, 2 * np.pi), rng.uniform(0, 2 * np.pi, 6)
    soft = [1.0, 0.03, 0.10, 0.012, 0.03, 0.006, 0.01]
    full = [1.0, 0.10, 0.65, 0.06, 0.38, 0.04, 0.2, 0.02, 0.1, 0.01, 0.05]
    out = []
    for side, amps0, air_l, wide, rush, catch, cents, whistle, seed in [
            ("A", soft, 0.30, 0.0, 0.55, 0.045, -30, 0.25, 1),
            ("B", full, 0.10, 0.22, 0.45, 0.025, -45, 0.55, 2)]:
        sr, n = (g.sr_a, g.n_a) if side == "A" else (g.sr_b, g.n_b)
        nt = Note(sr, n, 0.30)
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
        tone = harm_sum(phase, amps, ph, fv, sr)
        wh = whistle * np.exp(-np.maximum(nt.t - catch * 0.5, 0) / 0.03) * ss((nt.t - catch * 0.3) / 0.015) \
            * nt.gone(0.1) * np.sin(3 * phase + ph[2])
        pitched = lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 35) ** 2) for h in (1, 2, 3)) * 0.6
                             + ga_band(f, 2500, 2.0)) * (f < TOP)
        air = g.periodic_noise(coef, nt, pitched) * (0.6 + 0.4 * np.sin(phase))
        breath = g.periodic_noise(coef[::-1].copy(), nt, lambda f: ga_band(f, 5000, 3.0) * (f > 1500) * (f < 0.45 * sr))
        breath = breath / (np.std(breath[nt.T:]) + 1e-12) * np.std(tone[nt.T:]) * (0.85 + 0.15 * np.sin(phase))
        edge = nt.noise(np.random.default_rng(seed), lambda f: ga_band(f, 2800, 2.4) * (f > 300) * (f < 0.45 * sr))
        edge_env = ss(nt.t / 0.012) * np.exp(-nt.t / 0.06) * nt.gone(0.08)
        y = tone + wh + nt.ramp(0.0, catch + 0.08) * (air_l * air + wide * breath) + rush * edge_env * edge
        out.append((y, nt))
    return out


def singing_stone3(rng):
    """SINGING STN with contrast (user, 2026-11-05: A and B too alike; A more RESONANT, B more NOISY). Same slab, same
    modes. A: wet and pressed gently, the stone RINGS: the modes pure and long (the strike's ring lasting into the
    rub), only a thin halo of roughness, the twins beating slowly. B: dry and ground hard: a wide gritty scrape around
    every mode, the stick-slip chatter deep, a broad grinding hiss over it; the tone still under it."""
    sr, n = 11025, 4096
    step = sr / n
    f0 = round(220.0 / step) * step
    kf = round(f0 / step)
    ratios = [1.0, 2.76, 5.40, 8.93, 13.3]
    phs = rng.uniform(0, 2 * np.pi, (len(ratios), 2))
    coef = np.exp(2j * np.pi * rng.uniform(size=n // 2 + 1))
    coef2 = np.exp(2j * np.pi * rng.uniform(size=n // 2 + 1))
    out = []
    for side, gains, twin, rough_w, rough_lvl, grit_lvl, grit_pow, strike, sdec, hiss, seed in [
            ("A", [1.0, 0.6, 0.35, 0.18, 0.08], 0.35, 4.0, 0.22, 0.10, 8, [2.0, 1.8, 1.5, 1.2, 0.9], 0.55, 0.0, 3),
            ("B", [1.0, 0.8, 0.6, 0.45, 0.3], 0.6, 40.0, 1.5, 0.65, 3, [1.4, 1.4, 1.2, 1.0, 0.8], 0.20, 0.45, 4)]:
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
            strike_part += strike[j] * gn * np.exp(-nt.t / (sdec / r ** 0.7)) * s1
        sn = np.abs(sines[nt.T:]).max()
        bands = [(f0 * r, rough_w * r ** 0.5, gn) for r, gn in zip(ratios, gains)]
        f = np.fft.rfftfreq(n, 1.0 / sr)
        rough = np.fft.irfft(gp.formant(f, bands, floor=0.0) * (f < sr * 0.45) * coef, n=n)
        rough = rough[nt.m] / np.abs(rough).max()
        grind = np.fft.irfft(ga_band(f, 2200, 3.0) * (f > 300) * (f < sr * 0.45) * coef2, n=n)
        grind = grind[nt.m] / np.abs(grind).max()
        x = (nt.ramp(0.04, 0.42) * sines + strike_part * nt.gone(0.2)) / sn \
            + nt.ramp(0.02, 0.40) * (rough_lvl * rough + hiss * grind)
        grit = 1.0 + grit_lvl * nt.ramp(0.08, 0.45) * np.maximum(0, np.sin(2 * np.pi * kf * t / n)) ** grit_pow
        tick = nt.noise(np.random.default_rng(seed), lambda f: ga_band(f, 3500, 2.0)) * np.exp(-nt.t / 0.002)
        out.append((x * grit + 0.8 * tick, nt))
    return out


MENHIR_R = [1.0, 1.66, 2.59, 3.69, 4.96, 6.65, 8.68]


def menhir_b(rng, variant):
    """MENHIR with another B attack (user, 2026-11-05: likes it, the rocky percussion; hear variations of B). A as is.
    SPLIT: the stone cracking along its grain, three cracks in 70 ms over the heavy low modes. ANTLER: an antler pick
    struck on the stone, a hard bright tick and the stone's upper modes ringing out. EARTH: the stone dropped into
    its socket, a heavy thud, the soil's grit, only the low modes left."""
    a = menhir(rng)[0]
    phs = np.random.default_rng(29).uniform(0, 2 * np.pi, len(MENHIR_R) + 1)
    if variant == "SPLIT":
        y, nt = struck(11025, 36, 0.35, [(r, p, g, d) for r, p, g, d in zip(MENHIR_R, [1.0, .8, .55, .4, .3, .2, .12],
                                                                         [.30, .10, .03, 0, 0, 0, 0],
                                                                         [.15, .10, .07, .05, .04, .03, .02])],
                       [(200, 1.5, 0.5, 0.002, 0.03)], phs=phs, seed=31, onset=0.0015, T_s=0.40)
        pr = np.random.default_rng(32)
        for t0, lv, c in ((0.0, 0.9, 1500), (0.028, 0.6, 2200), (0.066, 0.45, 1100)):
            y = y + lv * np.abs(y).max() * nt.noise(pr, lambda f, c=c: ga_band(f, c, 2.2) * (f < 0.45 * nt.sr)) \
                * ga.env_ad(np.maximum(nt.t - t0, 0), 0.0003, 0.007) * (nt.t >= t0) * nt.gone(0.1)
    elif variant == "ANTLER":
        y, nt = struck(11025, 36, 0.35, [(r, p, g, d) for r, p, g, d in zip(MENHIR_R, [0.7, .6, .6, .6, .55, .45, .35],
                                                                         [.28, .10, .05, .02, 0, 0, 0],
                                                                         [.15, .12, .10, .08, .07, .05, .04])],
                       [(4200, 1.5, 0.9, 0.0001, 0.0015), (1800, 1.5, 0.3, 0.0003, 0.004)], phs=phs, seed=33,
                       onset=0.0002, T_s=0.40)
    else:   # EARTH
        y, nt = struck(11025, 36, 0.35, [(r, p, g, d) for r, p, g, d in zip(MENHIR_R, [1.0, .5, .2, .08, .03, 0, 0],
                                                                         [.32, .07, .01, 0, 0, 0, 0],
                                                                         [.18, .10, .06, .04, .03, .02, .02])],
                       [(110, 1.5, 0.8, 0.003, 0.05), (2000, 2.0, 0.0, 0.001, 0.01)], phs=phs, seed=34,
                       onset=0.004, T_s=0.40)
        pr = np.random.default_rng(35)
        grains = np.zeros(nt.N)
        for t0 in np.sort(pr.gamma(2.0, 0.018, 60)):
            s0 = int(t0 * nt.sr)
            if s0 < nt.T - 200:
                m = int(0.002 * nt.sr); k = np.arange(m) / nt.sr
                grains[s0:s0 + m] += pr.uniform(0.2, 1.0) * np.sin(2 * np.pi * pr.uniform(1200, 3500) * k) \
                    * np.exp(-k / 0.0004)
        y = y + 0.35 * np.abs(y).max() * grains / (np.abs(grains).max() + 1e-9) * nt.gone(0.1)
    return [a, (y, nt)]


# ------------------------------------------------------------------------------------------------ list --- #
RS_BDB = 2.7       # RAINSTICK: B's loop vs A's at equal RMS for equal K-weighted loudness (k_loud)
# name, model, root, B dB, drum shrink (rate cap, ring loop s) or None, tune, gate, env
L_ = [("SOLSTICE", solstice_strings, 57, 0.0, None, [57, 60, 64], 2.0, (0.2, 0.5, 1.0, 0.8)),
      ("CHOIR", choir, 57, 2.0, None, [57, 60, 64, 62], 1.2, SUST),     # the original (user: male_vox "bad")
      ("PAN FLUTE", pan_flute3, 72, 1.0, None, [74, 76, 79, 81, 76], 0.6, SUST),
      ("HORNS", horns, 48, 3.0, None, [43, 48, 50, 48], 0.9, SUST),
      ("SINGING STN", singing_stone3, 57, 2.0, None, [50, 57, 53], 1.4, SUST),
      ("LORE", lore_wood, 60, 0.0, None, [60, 55], 2.6, SUST),         # RAINSTICK dropped (user, 2026-11-05)
      ("STAR DUST", star_dust2, DUST_ROOT, 0.0, None, [57, 64, 69], 2.0, SUST),   # whole notes + a tone (2026-11-05)
      ("ROCK GONG", rock_gong, 57, 0.0, None, [48, 55, 60, 63], 3.0, SUST)]   # JD 90 -> JD 88, VOICES' slot (2026-11-05)
for _d in [d for d in DRUMS if d[0] not in ("MUSICAL BOW", "FRAME DRUM", "POT DRUM")] + [
        ("RAINSTICK", rainstick2, 60, 0.20, 14000)]:     # was SHAKER (shaker_rain); A trickle -> B rain (2026-11-05); BARROW DRUM, STONE CLACK out (user)
    _name, _fn, _root, _L, _cap = _d
    _t = [60, 60, 62, 60] if _name == "RAINSTICK" else ([_root + i for i in (0, 7, 3, 12)] if _root >= 48 else
                                                     [_root + 12 + i for i in (0, 7, 3, 12)])
    L_.append((_name, _fn, _root, RS_BDB if _name == "RAINSTICK" else 0.0, (_cap, _L), _t,   # K-loudness matched
               0.42 if _name != "RAINSTICK" else 0.24, DEC))
for _name, _fn, _root in [("WOOD BLOCK", wood_block2, 72), ("LOG MARIMBA", log_marimba2, 60)]:   # woodier
    L_.append((_name, _fn, _root, 0.0, (11025, 0.12), [_root + i for i in (0, 4, 7, 12)], 0.35, DEC))

import os
if os.environ.get("DEF_AUDITION"):          # MENHIR's B variants as extra waves, for the audition renders only
    for _v, _nm in (("SPLIT", "MENHIR SPLIT"), ("ANTLER", "MENHIR ANTLR"), ("EARTH", "MENHIR EARTH")):
        L_.append((_nm, (lambda r, v=_v: menhir_b(r, v)), 36, 0.0, (11025, 0.15), [48, 55, 51, 60], 0.42, DEC))


def size(w):
    return sum(len(s["atk"]) + len(s["loop"]) + 1 for s in w)


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    print(f"rate rule: cut under {THR:.0f} dB (today's -35 in brackets)")
    rows = []
    for name, fn, root, bdb, shr, tune, gate, env in L_:
        if shr:
            SHRINK.update(sr=shr[0], sr_bright=shr[0], L=shr[1], T=0.20)
        wf = finish_pair_float(fn(np.random.default_rng(4242)), bdb)
        r35 = [rate_for(s, -35) for s in wf]; r = [rate_for(s, THR) for s in wf]
        w35 = [mu(rerate(s, q)) for s, q in zip(wf, r35)]
        w = [mu(rerate(s, q)) for s, q in zip(wf, r)]
        rows.append((name, size(w) / 1024, size(w35) / 1024))
        print(f"{name:12s} A {w[0]['sr']:6.0f} ({w35[0]['sr']:5.0f})  B {w[1]['sr']:6.0f} ({w35[1]['sr']:5.0f})  "
              f"{size(w) / 1024:5.1f} KB ({size(w35) / 1024:5.1f})  seam {max(seam(s) for s in w):.2f}", flush=True)
        if RENDER:
            fn_ = name.lower().replace(" ", "_").replace(">", "_to_")
            a = abmid(lambda m, n, sc: play(w, root, m, n, sc), root, tune, gate, env)
            pk = np.abs(a).max() / 0.8
            lower = any(abs(x["sr"] - y["sr"]) > 1 for x, y in zip(w, w35))
            ga.write_wav(OUT / f"{fn_}_{'LOWER_rate' if lower else 'A_mid_B'}.wav", a / pk, SR)
            if lower:
                b = abmid(lambda m, n, sc: play(w35, root, m, n, sc), root, tune, gate, env)
                ga.write_wav(OUT / f"{fn_}_TODAY_rate.wav", b / pk, SR)
    # STAR DUST (classic wave): its own rate rule per part
    d = star_dust(np.random.default_rng(5))
    k = gp.PEAK / max(np.abs(d["atk"]).max(), np.abs(d["A"]).max(), np.abs(d["B"]).max())
    kb_d = 0
    for key, x in (("atk", d["atk"]), ("A", d["A"]), ("B", d["B"])):
        s = dict(atk=np.zeros(0), loop=x * k, sr=d["sr"], n=len(x))
        q = rate_for(s, THR)
        kb_d += len(x) * q / d["sr"] + 1
    rows.append(("STAR DUST", kb_d / 1024, 26.4))
    print(f"{'STAR DUST':12s} {kb_d / 1024:5.1f} KB (26.4 at 12 kHz)")
    # MOONSONG, CAIRN SPIRIT: today's loops (no attack), each at the rule's rate
    mu_ = ga.mulaw_roundtrip
    ga.mulaw_roundtrip = lambda x: x
    RAW = gp.build_samples(rerate=False)
    ga.mulaw_roundtrip = mu_
    for wave, parts in (("MOONSONG", ("MOON HUM", "MOON HALO")), ("CAIRN SPIRIT", ("CAIRN SPIRIT", "CAIRN HALO"))):
        kb, kb35, rs = 0, 0, []
        for p in parts:
            s = dict(atk=np.zeros(0), loop=RAW[p]["data"], sr=RAW[p]["sr"], n=len(RAW[p]["data"]))
            for thr, acc in ((THR, "kb"), (-35, "kb35")):
                q = rate_for(s, thr)
                x, _ = gp.resample_loop(RAW[p]["data"], RAW[p]["sr"], q) if q < s["sr"] - 1 else (RAW[p]["data"], 0)
                if acc == "kb":
                    kb += len(x) + 1; rs.append(q)
                else:
                    kb35 += len(x) + 1
        rows.append((wave, kb / 1024, kb35 / 1024))
        print(f"{wave:12s} {' / '.join(f'{q:.0f}' for q in rs)} Hz  {kb / 1024:5.1f} KB ({kb35 / 1024:5.1f})")
    tot = sum(r[1] for r in rows)
    print(f"\nTOTAL {tot:.1f} KB at the {THR:.0f} dB rule ({sum(r[2] for r in rows):.1f} at -35); budget {BUDGET_KB:.0f} KB "
          f"-> {BUDGET_KB - tot:+.1f} KB; {len(rows)} waves; entries/headers ~{(len(rows) * 5 * 36 + 3 * 32) / 1024:.1f} KB "
          f"(already inside the budget)")
