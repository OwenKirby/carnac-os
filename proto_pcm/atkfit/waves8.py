"""Round 5: SOLSTICE split in two waves. SOLSTICE: the warm pad (A as liked), B the same pad with star dust overlaid
(glass glints tuned to the note's harmonics, a slow breath of air), B's attack a glissando of glints. SUNSTONE: the
bell alone (octave-up Fantasia strike) ringing into a short glow, A soft, B crystal. Layer them (STACK / DUAL)."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "notes.py").read().split("TUNES = {")[0])
c_src = open(here / "waves7c.py").read()
exec(c_src[c_src.index("BELL = ["):c_src.index("def solstice3")])
BELL_OCT = 2.0


def warm_layers(rng):
    """round 1's draws, in order (so the warm pad is the one liked)"""
    strings = [dict(step=s, vcyc=v, vc=c, vph=rng.uniform(0, 6.3), wph=rng.uniform(0, 6.3, 6),
                    ph=rng.uniform(0, 6.3, 80), enter=rng.uniform(0.04, 0.12))
               for s, v, c in [(-2, 5, 8), (-1, 4, 10), (0, 5, 7), (0, 6, 9), (1, 4, 8), (2, 5, 10)]]
    voices = [dict(step=s, vcyc=v, vc=c, vph=rng.uniform(0, 6.3), wph=rng.uniform(0, 6.3, 6),
                   ph=rng.uniform(0, 6.3, 80), coef=rng.standard_normal(6000) + 1j * rng.standard_normal(6000),
                   enter=rng.uniform(0.08, 0.16))
              for s, v, c in [(-1, 4, 12), (0, 5, 10), (1, 4, 14), (0, 3, 9)]]
    bell_ph = rng.uniform(0, 6.3, 40)
    return strings, voices, bell_ph


def warm_pad(g, nt, sr, strings, voices, bell_ph):
    oh = [(450, 80, 1.0), (850, 100, 0.45), (2400, 150, 0.08)]
    body = [(450, 300, 1.0), (1100, 500, 0.7), (2800, 900, 0.4)]
    u = nt.m / nt.n
    y = 0.0
    for p in strings:
        fv = g.f0 + p["step"] * g.df
        vr, vd = p["vcyc"] * g.df, fv * (2 ** (p["vc"] / 1200) - 1)
        wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
        phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.15, 0.38) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
            + 0.4 * wan + nt.scoop(fv, -15, 0.05)
        k = np.arange(1, int(min(TOP, 0.45 * sr) / fv) + 1)
        amps = k ** -1.1 * formant(k * fv, body, 0.15) / (1 + (k * fv / 2200) ** 4)
        y = y + nt.ramp(p["enter"], p["enter"] + 0.25) * harm_sum(phase, list(amps), p["ph"], fv, sr)
    for p in voices:
        fv = g.f0 + p["step"] * g.df
        vr, vd = p["vcyc"] * g.df, fv * (2 ** (p["vc"] / 1200) - 1)
        wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
        phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.2, 0.38) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
            + 0.5 * wan + nt.scoop(fv, -30, 0.05)
        k = np.arange(1, int(min(TOP, 0.45 * sr) / fv) + 1)
        amps = k ** -1.5 * formant(k * fv, oh, 0.01)
        breath = g.periodic_noise(p["coef"], nt, lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 30) ** 2) for h in (1, 2, 3, 4))
                                                            + 0.4 * ga_band(f, 2600, 2.0)) * (f < TOP * 1.2))
        on = nt.ramp(p["enter"], p["enter"] + 0.22)
        y = y + 0.9 * on * harm_sum(phase, list(amps), p["ph"], fv, sr) + 0.10 * on * breath * (0.7 + 0.3 * np.sin(phase))
    lvl = np.sqrt(np.mean(y[nt.T:] ** 2))
    y = y + nt.ramp(0.05, 0.38) * 0.7 * lvl * (0.75 + 0.25 * np.sin(2 * np.pi * u + 1.0)) * \
        np.sin(2 * np.pi * g.f0 * nt.tau + bell_ph[1])
    return y, lvl


def solstice_pad(rng, L=1.0, T_s=0.40, sr_a=11025, sr_b=12000, dust_rate=14.0):
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    strings, voices, bell_ph = warm_layers(rng)
    drng = np.random.default_rng(4321)
    out = []
    for side, sr, n in (("A", g.sr_a, g.n_a), ("B", g.sr_b, g.n_b)):
        nt = Note(sr, n, T_s)
        y, lvl = warm_pad(g, nt, sr, strings, voices, bell_ph)
        if side == "B":
            # star dust: glass glints on harmonics 6..24 of the note (a major chord's sparkle, in tune with the pad),
            # placed round the loop at random, -18..0 dB, each a short glass ping (two partials, the second stretched)
            H_ = [6, 8, 10, 12, 15, 16, 20, 24]
            def glint(h, prng):
                f = h * g.f0
                dur = 0.7; t = ga.t_axis(sr, dur); d = prng.uniform(0.12, 0.45)
                y_ = np.sin(2 * np.pi * f * t + prng.uniform(0, 6.3)) * np.exp(-t / d)
                if 2.76 * f < 0.45 * sr:
                    y_ += 0.25 * np.sin(2 * np.pi * 2.76 * f * t) * np.exp(-t / (0.3 * d))
                return y_ * (1 - np.exp(-t / 0.0008))
            times = np.sort(drng.uniform(0, L, drng.poisson(dust_rate * L)))
            ev = [(tt, 10 ** (drng.uniform(-18, 0) / 20), glint(H_[drng.integers(len(H_))] * 1, drng)) for tt in times
                  if (H_[0] * g.f0) < 0.45 * sr]
            ev = [(tt, a, s) for tt, a, s in ev]
            dust = gp.circular_hits(n, sr, ev)
            dust = dust / (np.sqrt(np.mean(dust ** 2)) + 1e-12)
            air = g.periodic_noise(drng.standard_normal(20000) + 1j * drng.standard_normal(20000), nt,
                                   lambda f: ga_band(f, 3800, 1.6) * (f > 1500))
            u = nt.m / n
            y = y + nt.ramp(0.15, 0.38) * (0.45 * lvl * dust[nt.m] + 0.25 * lvl * (0.6 + 0.4 * np.sin(2 * np.pi * u + 0.5)) * air)
            # the attack: a glissando of glints up the harmonics, fairy dust, gone by the handoff
            gl = np.zeros(nt.N + int(sr))
            for i, h in enumerate([4, 5, 6, 8, 10, 12, 15, 16, 20, 24]):
                if h * g.f0 >= 0.45 * sr: break
                s0 = int((0.01 + 0.028 * i) * sr); gg = glint(h, drng)
                gl[s0:s0 + len(gg)] += (0.6 + 0.4 * i / 9) * gg
            y = y + 0.9 * lvl * gl[:nt.N] * nt.gone(0.12)
        out.append((y, nt))
    return out


def sunstone(rng, L=0.25, T_s=0.70, sr=12000):
    """the bell alone: the octave-up Fantasia strike, ringing into its glow (A soft, B crystal: brighter, the upper
    partials glowing too)"""
    n, f0 = gp.loop_len(sr, gp.note_hz(57), L)
    df = sr / n
    bph = rng.uniform(0, 6.3, len(BELL))
    out = []
    for side in "AB":
        nt = Note(sr, n, T_s)
        y = 0.0
        for j, (r, a, d, glow) in enumerate(BELL):
            f = round(f0 * r * BELL_OCT / df) * df
            if f >= 0.45 * sr:
                continue
            sus = glow * (1.0 if side == "A" else 1.8 + 0.25 * j)
            env = (sus + (a - sus) * np.exp(-nt.t / d) * nt.gone(0.2)) * (1 - np.exp(-nt.t / 0.0006))
            y = y + env * np.sin(2 * np.pi * f * nt.tau + bph[j])
        y = y + 0.15 * nt.noise(np.random.default_rng(91), lambda f: ga_band(f, 4000, 1.5) * (f < 0.45 * sr)) * \
            ga.env_ad(nt.t, 0.0002, 0.0015)
        out.append((y, nt))
    return out


def report(name, w):
    size = 0
    for s, tag in zip(w, "AB"):
        x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
        d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
        T, n = len(s["atk"]), len(s["loop"])
        size += T + n + 1
        print(f"{name} {tag}: attack {T} B + loop {n} B ({n / s['sr']:.2f} s) at {s['sr']:.0f} Hz; seam {d[T-1]/typ:.2f}, wrap {d[T+n-1]/typ:.2f}")
    print(f"{name}: {size / 1024:.1f} KB as rendered")


def chords(players, scan, amp_atk=0.005):
    CH = [[45, 52, 60, 64, 67], [41, 48, 57, 60, 64], [48, 55, 64, 67, 71], [43, 50, 57, 62, 66]]
    seg = np.zeros(int((4 * 3.6 + 3.0) * SR))
    for c, chord in enumerate(CH):
        n_out = int(6.0 * SR)
        for w, root, gain, env in players:
            e = gp.adsr(n_out, *env[:3], int(3.4 * SR), env[3])
            for m in chord:
                v = play(w, root, m, n_out, scan) * e * 0.25 * gain
                s0 = int(c * 3.6 * SR); L_ = min(n_out, len(seg) - s0)
                seg[s0:s0 + L_] += v[:L_]
    return seg


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    pad = finish_pair(solstice_pad(np.random.default_rng(777)), 0.0)
    bell = finish_pair(sunstone(np.random.default_rng(778)), 0.0)
    report("SOLSTICE", pad)
    report("SUNSTONE", bell)
    PAD_ENV, BELL_ENV = (0.005, 0.5, 1.0, 1.2), (0.001, 1.2, 0.0, 1.0)
    for name, players in (("solstice5_pad", [(pad, 57, 1.0, PAD_ENV)]),
                          ("sunstone_bell", [(bell, 57, 1.0, (0.001, 0.8, 0.3, 1.0))]),
                          ("solstice5_plus_sunstone", [(pad, 57, 1.0, PAD_ENV), (bell, 57, 0.7, BELL_ENV)])):
        x = np.concatenate(sum(([chords(players, sc), np.zeros(SR)] for sc in (0.0, 0.5, 1.0)), []))
        x = gp.reverb(x, 4.0, 0.35)
        ga.write_wav(OUT / f"{name}_A_mid_B.wav", x / np.abs(x).max() * 0.8, SR)
