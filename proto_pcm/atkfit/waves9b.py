"""Round 6: SOLSTICE with a single voice. SOLO: the choir replaced by one singer (both sides; B keeps the star dust).
SOLO -> CHOIR: A the singer alone over the strings, B the full choir and the star dust (SCAN = the others joining)."""
import sys, pathlib
here = pathlib.Path(__file__).parent
_s = open(here / "waves8.py").read()
exec(_s.split('if __name__ == "__main__":')[0])


def solo_voice(g, nt, sr, rng_seed=55):
    """The male voice after the JD-800's wave 45 (measured from the user's recording, 2026-11-05; synthesised, nothing
    of the recording used): a straight tone, no vibrato, only a slow drift of +-8 cents; no slide onto the note; an 85 ms
    rise; by harmonic number (it moves with the pitch, as a sample does): a strong fundamental, resonances at the 3rd
    (-19 dB) and the 7th (-26 dB), a bright band of ring and air around the 17th..21st (-27 dB); clean below 1 kHz,
    breath rising to the harmonics' own level above 2.5 kHz."""
    r = np.random.default_rng(rng_seed)
    fv = g.f0
    u = nt.m / nt.n
    drift = sum(np.sin(2 * np.pi * h * u + r.uniform(0, 6.3)) / h for h in (1, 2)) / 1.5
    phase = 2 * np.pi * fv * nt.tau + 2 * np.pi * fv * (2 ** (8 / 1200) - 1) / g.df * drift / (2 * np.pi) * 0.5
    k = np.arange(1, int(0.45 * sr / fv) + 1)
    lk = np.log2(k)
    env_db = np.maximum.reduce([0.0 - 40 * (lk / 0.55) ** 2,
                                -19 - 40 * ((lk - np.log2(3)) / 0.35) ** 2,
                                -26 - 40 * ((lk - np.log2(7)) / 0.35) ** 2,
                                -27 - 40 * ((lk - np.log2(19)) / 0.45) ** 2,
                                np.full(len(k), -52.0)])
    amps = 10 ** (env_db / 20)
    voice = harm_sum(phase, list(amps), r.uniform(0, 6.3, len(amps) + 1), fv, sr)
    coef = r.standard_normal(20000) + 1j * r.standard_normal(20000)
    def breath_shape(f):
        kk = np.maximum(f / fv, 1.0)
        e = np.maximum.reduce([0.0 - 40 * (np.log2(kk) / 0.55) ** 2, -19 - 40 * ((np.log2(kk) - np.log2(3)) / 0.35) ** 2,
                               -26 - 40 * ((np.log2(kk) - np.log2(7)) / 0.35) ** 2,
                               -27 - 40 * ((np.log2(kk) - np.log2(19)) / 0.45) ** 2, np.full(np.shape(f), -52.0)])
        rise = np.clip((f - 1000) / 1500, 0, 1) ** 1.5                    # clean low, as loud as the harmonics high
        return 10 ** (e / 20) * (0.05 + rise) * (f < 0.45 * sr)
    breath = g.periodic_noise(coef, nt, breath_shape)
    hv = np.std(voice[nt.T:])
    on = nt.ramp(0.0, 0.11)
    # the breath's level: matched to the voice's own level in its band (about equal above 2.5 kHz)
    return on * (voice / hv + 0.35 * breath / np.std(breath[nt.T:]))


def solstice_vox(rng, mode, L=1.0, T_s=0.40, sr_a=11025, sr_b=12000):
    """mode 'solo': the singer on both sides; 'solo2choir': the singer on A, the choir on B"""
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    strings, voices, bell_ph = warm_layers(rng)
    drng = np.random.default_rng(4321)
    out = []
    for side, sr, n in (("A", g.sr_a, g.n_a), ("B", g.sr_b, g.n_b)):
        nt = Note(sr, n, T_s)
        choir = mode == "solo2choir" and side == "B"
        y, lvl = warm_pad(g, nt, sr, strings, voices if choir else [], bell_ph)
        if not choir:
            y = y + 0.55 * lvl * solo_voice(g, nt, sr)
        if side == "B":
            H_ = [6, 8, 10, 12, 15, 16, 20, 24]
            def glint(h, prng):
                f = h * g.f0; dur = 0.7; t = ga.t_axis(sr, dur); d = prng.uniform(0.12, 0.45)
                y_ = np.sin(2 * np.pi * f * t + prng.uniform(0, 6.3)) * np.exp(-t / d)
                if 2.76 * f < 0.45 * sr:
                    y_ += 0.25 * np.sin(2 * np.pi * 2.76 * f * t) * np.exp(-t / (0.3 * d))
                return y_ * (1 - np.exp(-t / 0.0008))
            times = np.sort(drng.uniform(0, L, drng.poisson(14.0 * L)))
            ev = [(tt, 10 ** (drng.uniform(-18, 0) / 20), glint(H_[drng.integers(len(H_))], drng)) for tt in times]
            dust = gp.circular_hits(n, sr, ev); dust = dust / (np.sqrt(np.mean(dust ** 2)) + 1e-12)
            air = g.periodic_noise(drng.standard_normal(20000) + 1j * drng.standard_normal(20000), nt,
                                   lambda f: ga_band(f, 3800, 1.6) * (f > 1500))
            u = nt.m / n
            y = y + nt.ramp(0.15, 0.38) * (0.45 * lvl * dust[nt.m] + 0.25 * lvl * (0.6 + 0.4 * np.sin(2 * np.pi * u + 0.5)) * air)
            gl = np.zeros(nt.N + int(sr))
            for i, h in enumerate([4, 5, 6, 8, 10, 12, 15, 16, 20, 24]):
                if h * g.f0 >= 0.45 * sr: break
                s0 = int((0.01 + 0.028 * i) * sr); gg = glint(h, drng)
                gl[s0:s0 + len(gg)] += (0.6 + 0.4 * i / 9) * gg
            y = y + 0.9 * lvl * gl[:nt.N] * nt.gone(0.12)
        out.append((y, nt))
    return out


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    PAD_ENV = (0.005, 0.5, 1.0, 1.2)
    for mode, fname in (("solo", "solstice7_solo"), ("solo2choir", "solstice7_solo_to_choir")):
        w = finish_pair(solstice_vox(np.random.default_rng(777), mode), 0.0)
        report(fname, w)
        x = np.concatenate(sum(([chords([(w, 57, 1.0, PAD_ENV)], sc), np.zeros(SR)] for sc in (0.0, 0.5, 1.0)), []))
        x = gp.reverb(x, 4.0, 0.35)
        ga.write_wav(OUT / f"{fname}_A_mid_B.wav", x / np.abs(x).max() * 0.8, SR)
        single = []
        for scan in (0.0, 0.5, 1.0):
            n_out = int(3.0 * SR)
            single += [play(w, 57, 64, n_out, scan) * gp.adsr(n_out, 0.01, 0.5, 1.0, int(2.3 * SR), 0.6), np.zeros(int(0.5 * SR))]
        y = gp.reverb(np.concatenate(single), 3.0, 0.25)
        ga.write_wav(OUT / f"{fname}_note_A_mid_B.wav", y / np.abs(y).max() * 0.8, SR)
