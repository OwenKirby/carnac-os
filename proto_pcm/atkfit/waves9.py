"""Round 6: SOLSTICE with a single voice. SOLO: the choir replaced by one singer (both sides; B keeps the star dust).
SOLO -> CHOIR: A the singer alone over the strings, B the full choir and the star dust (SCAN = the others joining)."""
import sys, pathlib
here = pathlib.Path(__file__).parent
_s = open(here / "waves8.py").read()
exec(_s.split('if __name__ == "__main__":')[0])


def solo_voice(g, nt, sr, rng_seed=55):
    """One singer, a man (the user, round 6): a warm open 'oh' with a man's formants and the singer's ring at ~2.8 kHz,
    a fuller vibrato (5 cycles a
    loop, +-28 cents, its depth breathing and its rate wavering, all whole cycles so the loop holds), breath, a slide
    up onto the note; louder than one chorister"""
    r = np.random.default_rng(rng_seed)
    vowel = [(480, 70, 1.0), (820, 90, 0.6), (2500, 150, 0.2), (2850, 220, 0.35), (3300, 260, 0.12)]
    fv = g.f0
    u = nt.m / nt.n
    vr = 5 * g.df
    depth = 22 * (0.8 + 0.2 * np.sin(2 * np.pi * u + r.uniform(0, 6.3)))           # the vibrato breathing
    vd = fv * (2 ** (depth / 1200) - 1)
    waver = 0.15 * np.sin(2 * np.pi * u + r.uniform(0, 6.3))                       # its rate wavering
    vib = vd / vr * np.sin(2 * np.pi * vr * nt.tau + 2 * np.pi * waver + r.uniform(0, 6.3))
    wan = sum(np.sin(2 * np.pi * h * u + r.uniform(0, 6.3)) / h for h in range(1, 4)) / 1.83
    phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.18, 0.38) * vib + 0.25 * wan + nt.scoop(fv, -60, 0.06)
    k = np.arange(1, int(min(TOP * 1.1, 0.45 * sr) / fv) + 1)
    amps = k ** -1.2 * formant(k * fv, vowel, 0.02)
    shimmer = 1.0 + 0.06 * sum(np.sin(2 * np.pi * h * 3 * u + r.uniform(0, 6.3)) for h in (1, 2)) / 2
    voice = harm_sum(phase, list(amps), r.uniform(0, 6.3, len(amps) + 1), fv, sr) * shimmer
    coef = r.standard_normal(20000) + 1j * r.standard_normal(20000)
    breath = g.periodic_noise(coef, nt, lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 25) ** 2) for h in range(1, 7))
                                                   + 0.5 * ga_band(f, 2800, 1.6)) * (f < 0.45 * sr))
    on = nt.ramp(0.03, 0.22)
    return on * (voice / np.std(voice[nt.T:]) + 0.12 * breath / np.std(breath[nt.T:]) * (0.7 + 0.3 * np.sin(phase)))


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
    for mode, fname in (("solo", "solstice6_solo"), ("solo2choir", "solstice6_solo_to_choir")):
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
