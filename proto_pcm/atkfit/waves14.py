"""Round 14 (2026-11-05): two B sides measured from the JD-800 recording (measure only, nothing of it used).
PAN FLUTE -> SHAKUHACHI (JD 95, the shakuhachi attack): A = the pan pipe as before; B = an open bamboo pipe, an
instant breath puff, the note scooping up 25 c in ~40 ms, the tone swelling ~4 dB over 250 ms while H2 grows -17 ->
-5 dB, H3 -19, a shelf of -26..-36 dB to ~7 kHz; breath per octave against H1: -8 -5 -10 -14 -17 dB (250 Hz..8 kHz),
nothing above 8 kHz.
SINGING STN -> FINE WINE (JD 35, the singing glass): A = the rubbed stone as before; B = a wet-rimmed glass two
octaves above the key, near-sine (2x -31, 3x -34, 4x -38, 5x -49, 6x -23 dB), its main partial beating with a twin
4 Hz away (12 dB deep, as the JD), a ghost of the key pitch at -34 dB, rising to -10 dB in 40 ms, steady pitch.
Prints sizes at the lowest safe rate; renders A -> 50 % -> B and B alone."""
import sys, pathlib
here = pathlib.Path(__file__).parent
_w6 = open(here / "waves6.py").read()
exec(_w6.split('\nif __name__ == "__main__":')[0])


def band_noise(g, nt, coef, octave_db, f_top):
    """periodic noise on the grid: power per octave (dB against a unit sine, centre = the octave's low edge)"""
    nb = nt.n // 2 + 1
    f = np.arange(nb) * g.df
    edges = np.array([lo for lo, _ in octave_db] + [octave_db[-1][0] * 2])
    y = np.zeros(nt.n)
    c0 = np.zeros(nb, complex); k = min(nb, len(coef)); c0[:k] = coef[:k]
    for (lo, db) in octave_db:
        m = (f >= lo) & (f < 2 * lo) & (f < f_top) & (f < 0.45 * nt.sr)
        if not m.any():
            continue
        yb = np.fft.irfft(np.where(m, c0, 0), n=nt.n)
        yb *= np.sqrt(0.5 * 10 ** (db / 10)) / (np.sqrt(np.mean(yb ** 2)) + 1e-12)
        y += yb
    return y[nt.m]


def shakuhachi_b(rng, g, sr_b, T_s=0.30):
    """B on pan_flute's grid (same f0 and df), at its own rate"""
    nt = Note(sr_b, g.n_b, T_s)
    fv = g.f0
    u = nt.m / nt.n
    ph = rng.uniform(0, 2 * np.pi, 40)
    vph, wph = rng.uniform(0, 2 * np.pi), rng.uniform(0, 2 * np.pi, 6)
    vr, vd = 2 * g.df, fv * (2 ** (7 / 1200) - 1)              # not in the JD's 170 ms: a slow, light vibrato late in
    wan = sum(np.sin(2 * np.pi * h * u + wph[h - 1]) / h for h in range(1, 4)) / 1.83
    phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.18, nt.Ts) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + vph) \
        + 0.4 * wan + nt.scoop(fv, -25, 0.018)
    H = [0, -5, -19, -26, -26, -28, -35, -31, -35, -30, -29, -28, -30, -34, -41, -35, -35, -34, -42, -37]
    swell = 10 ** ((-4 + 4 * nt.ramp(0.0, 0.25)) / 20)          # the tone swells ~4 dB over 250 ms
    amps = []
    for k, db in enumerate(H, start=1):
        a = 10 ** (db / 20) * (1.0 if k * fv < 7000 else 0.0)
        if k == 2:                                               # H2 grows -17 -> -5 as the blowing firms up
            a = a * 10 ** (-12 * (1 - nt.ramp(0.0, 0.10)) / 20)
        amps.append(a * swell * ss(nt.t / 0.004))
    tone = harm_sum(phase, amps, ph, fv, nt.sr)
    coef = rng.standard_normal(8000) + 1j * rng.standard_normal(8000)
    air = band_noise(g, nt, coef, [(250, -8), (500, -5), (1000, -10), (2000, -14), (4000, -17)], 8000.0)
    air = air * (0.75 + 0.25 * np.sin(phase))                    # the air through the bore, pitched by it
    # the puff: breath at the lip before the bore speaks, 0 ms rise, gone in ~40 ms (JD: breath leads the tone)
    puff = nt.noise(np.random.default_rng(95), lambda f: (ga_band(f, 1500, 3.0)) * (f < 8000)) \
        * np.exp(-nt.t / 0.035) * nt.gone(0.1)
    y = tone + air * ss(nt.t / 0.003) + 2.5 * np.sqrt(np.mean(air[nt.T:] ** 2)) * puff
    return y, nt


def pan_shaku(rng):
    pa = pan_flute(rng)[0]                                       # the pan pipe as before (A)
    g = Grid(11025, gp.note_hz(72), 0.48, 16000)
    return [pa, shakuhachi_b(np.random.default_rng(9595), g, g.sr_b)]


def fine_wine_b(sr=12000, T_s=0.30, seed=35):
    """the stone's f0 x 4, on its own grid: n so that the glass is harmonic 220 of df (~4 Hz), the twin at 221"""
    rng = np.random.default_rng(seed)
    step = 11025 / 4096
    f_stone = round(220.0 / step) * step
    fg = 4 * f_stone
    n = int(round(sr * 220 / fg)); df = sr / n
    srx = df * n                                                 # == sr
    nt = Note(srx, n, T_s)
    t = nt.tau
    parts = [(1, 1.0, 0.0), (2, -31, 0.03), (3, -34, 0.05), (4, -38, 0.06), (5, -49, 0.07), (6, -23, 0.05),
             (0.25, -34, 0.10), (0.5, -50, 0.10)]
    ph = rng.uniform(0, 2 * np.pi, 12)
    y = 0.0
    for j, (r, db, late) in enumerate(parts):
        k = int(round(220 * r))
        if k * df >= 0.45 * srx:
            continue
        a = 1.0 if r == 1 else 10 ** (db / 20)
        rise = 1 - np.exp(-np.maximum(nt.t - late, 0) / 0.018)  # -10 dB in ~40 ms; the upper partials a hair later
        y = y + a * rise * np.sin(2 * np.pi * k * df * t + ph[j])
        if r == 1:                                               # the twin: 4 Hz beat, 12 dB deep
            y = y + 0.6 * a * rise * np.sin(2 * np.pi * (k + 1) * df * t + ph[11])
    return y, nt


def stone_glass(rng):
    sa = singing_stone2(rng)[0]
    return [sa, fine_wine_b()]


NEW = [("PAN>SHAKU", pan_shaku, 72, 0.0, [72, 74, 76, 79, 81, 79, 76, 72], 0.7),
       ("STONE>GLASS", stone_glass, 57, 0.0, [50, 57, 53, 62, 57, 50, 45, 50], 1.4)]

if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for name, fn, root, bdb, tune, gate in NEW:
        wf = finish_pair_float(fn(np.random.default_rng(4242)), bdb)
        low = [mu(rerate(s, lowest(s))) for s in wf]
        kb = sum(len(s["atk"]) + len(s["loop"]) + 1 for s in low) / 1024
        for s, t in zip(low, "AB"):
            print(f"{name} {t}: attack {len(s['atk'])} + loop {len(s['loop'])} at {s['sr']:.0f} Hz, seam {seam(s):.2f}")
        print(f"{name}: {kb:.1f} KB")
        fn_ = name.lower().replace(">", "_to_")
        a = abmid(lambda m, n, sc: play(low, root, m, n, sc), root, tune, gate, SUST)
        ga.write_wav(OUT / f"{fn_}_A_mid_B.wav", a / np.abs(a).max() * 0.8, SR)
        x = gp.reverb(a, 2.5, 0.25)
        ga.write_wav(OUT / f"{fn_}_A_mid_B_reverb.wav", x / np.abs(x).max() * 0.8, SR)
