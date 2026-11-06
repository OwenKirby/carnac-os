"""SOLSTICE round 3: the bell is the star (the Fantasia idea): a bright, slightly metallic bell over the pad, ringing
~0.7 s while the pad swells in under it. A WARM: the round-1 loop (liked), the bell ringing out. B CRYSTAL: the same
warm pad a little brighter, and the bell never quite dies: it rings down into a sustained glow of its own partials
(the same oscillators carry on in the loop), Fantasia's shimmering tail. 1 s loops."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "notes.py").read().split("TUNES = {")[0])

# the bell: (ratio to the note, strike level, decay s, sustained glow in B). Fundamental and octave strong, the upper
# partials a little stretched (metal), so the strike rings bright and bell-like, not like the pad's own harmonics
BELL = [(1.0, 0.9, 1.0, 0.10), (2.0, 1.0, 0.8, 0.16), (3.0, 0.35, 0.5, 0.05), (4.07, 0.55, 0.45, 0.12),
        (5.43, 0.35, 0.35, 0.08), (6.8, 0.25, 0.25, 0.06), (8.9, 0.15, 0.18, 0.04), (11.3, 0.08, 0.12, 0.02)]


def solstice3(rng, L=1.0, T_s=0.70, sr_a=11025, sr_b=12000, bell_db=0.0):
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    oh = [(450, 80, 1.0), (850, 100, 0.45), (2400, 150, 0.08)]
    ah = [(650, 90, 1.0), (1000, 110, 0.6), (2500, 160, 0.25)]
    body = [(450, 300, 1.0), (1100, 500, 0.7), (2800, 900, 0.4)]
    strings = [dict(step=s, vcyc=v, vc=c, vph=rng.uniform(0, 6.3), wph=rng.uniform(0, 6.3, 6),
                    ph=rng.uniform(0, 6.3, 80), enter=rng.uniform(0.04, 0.12))
               for s, v, c in [(-2, 5, 8), (-1, 4, 10), (0, 5, 7), (0, 6, 9), (1, 4, 8), (2, 5, 10)]]
    voices = [dict(step=s, vcyc=v, vc=c, vph=rng.uniform(0, 6.3), wph=rng.uniform(0, 6.3, 6),
                   ph=rng.uniform(0, 6.3, 80), coef=rng.standard_normal(6000) + 1j * rng.standard_normal(6000),
                   enter=rng.uniform(0.08, 0.16))
              for s, v, c in [(-1, 4, 12), (0, 5, 10), (1, 4, 14), (0, 3, 9)]]
    bell_ph = rng.uniform(0, 6.3, 40)
    rng.integers(1, 4, 7); rng.uniform(0, 6.3, 7); rng.uniform(0, 6.3)        # (round 1's draws: A's loop unchanged)
    bphase = rng.uniform(0, 6.3, len(BELL))
    out = []
    for side, sr, n in (("A", g.sr_a, g.n_a), ("B", g.sr_b, g.n_b)):
        nt = Note(sr, n, T_s)
        u = nt.m / n
        swell = lambda e0, e1: nt.ramp(min(e0 + 0.10, T_s - 0.3), min(e1 + 0.30, T_s - 0.02))   # the pad slower: the bell first
        y = 0.0
        for p in strings:
            fv = g.f0 + p["step"] * g.df
            vr, vd = p["vcyc"] * g.df, fv * (2 ** (p["vc"] / 1200) - 1)
            wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
            phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.3, 0.65) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
                + 0.4 * wan + nt.scoop(fv, -15, 0.05)
            kt = int(min(TOP, 0.45 * sr) / fv)
            k = np.arange(1, kt + 1)
            amps = k ** (-1.1 if side == "A" else -0.95) * formant(k * fv, body, 0.15)
            amps = amps / (1 + (k * fv / (2200 if side == "A" else 3200)) ** 4)
            y = y + swell(p["enter"], p["enter"] + 0.25) * harm_sum(phase, list(amps), p["ph"], fv, sr)
        for p in voices:
            fv = g.f0 + p["step"] * g.df
            vr, vd = p["vcyc"] * g.df, fv * (2 ** (p["vc"] / 1200) - 1)
            wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
            phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.35, 0.65) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
                + 0.5 * wan + nt.scoop(fv, -30, 0.05)
            kt = int(min(TOP, 0.45 * sr) / fv)
            k = np.arange(1, kt + 1)
            amps = k ** -1.5 * formant(k * fv, oh, 0.01) if side == "A" else k ** -1.2 * formant(k * fv, ah, 0.02)
            breath = g.periodic_noise(p["coef"], nt, lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 30) ** 2) for h in (1, 2, 3, 4))
                                                                + 0.4 * ga_band(f, 2600, 2.0)) * (f < TOP * 1.2))
            on = swell(p["enter"], p["enter"] + 0.22)
            y = y + 0.9 * on * harm_sum(phase, list(amps), p["ph"], fv, sr) + 0.10 * on * breath * (0.7 + 0.3 * np.sin(phase))
        lvl = np.sqrt(np.mean(y[nt.T:] ** 2))
        t0 = 2 * np.pi * g.f0 * nt.tau
        y = y + swell(0.05, 0.38) * 0.7 * lvl * (0.75 + 0.25 * np.sin(2 * np.pi * u + 1.0)) * np.sin(t0 + bell_ph[1])
        # the bell: each partial on the loop's grid; it strikes, rings, and in B settles on its glow (the same oscillator
        # carries on in the loop, so the strike rings down into it seamlessly)
        bell = 0.0
        for j, (r, a, d, glow) in enumerate(BELL):
            f = round(g.f0 * r / g.df) * g.df
            if f >= 0.45 * sr:
                continue
            sus = glow if side == "B" else 0.0
            env = (sus + (a - sus) * np.exp(-nt.t / d) * nt.gone(0.2)) * (1 - np.exp(-nt.t / 0.0006))
            bell = bell + env * np.sin(2 * np.pi * f * nt.tau + bphase[j])
        click = nt.noise(np.random.default_rng(91), lambda f: ga_band(f, 4000, 1.5) * (f < 0.45 * sr)) * \
            ga.env_ad(nt.t, 0.0002, 0.0015)
        y = y + 10 ** (bell_db / 20) * 2.6 * lvl * bell + 0.4 * lvl * click
        out.append((y, nt))
    return out


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    w = finish_pair(solstice3(np.random.default_rng(777)), 0.0)
    size = 0
    for s, tag in zip(w, "AB"):
        x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
        d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
        T, n = len(s["atk"]), len(s["loop"])
        size += T + n + 1
        print(f"SOLSTICE {tag}: attack {T} B + loop {n} B at {s['sr']:.0f} Hz; seam {d[T-1]/typ:.2f}, wrap {d[T+n-1]/typ:.2f}")
    print(f"size {size / 1024:.1f} KB as rendered")
    root = 57
    CH = [[45, 52, 60, 64, 67], [41, 48, 57, 60, 64], [48, 55, 64, 67, 71], [43, 50, 57, 62, 66]]
    parts = []
    for scan in (0.0, 0.5, 1.0):
        seg = np.zeros(int((4 * 3.6 + 3.0) * SR))
        for c, chord in enumerate(CH):
            n_out = int(6.0 * SR)
            e = gp.adsr(n_out, 0.005, 0.5, 1.0, int(3.4 * SR), 1.2)
            for m in chord:
                v = play(w, root, m, n_out, scan) * e * 0.25
                s0 = int(c * 3.6 * SR); L_ = min(n_out, len(seg) - s0)
                seg[s0:s0 + L_] += v[:L_]
        parts += [seg, np.zeros(SR)]
    x = gp.reverb(np.concatenate(parts), 4.0, 0.35)
    ga.write_wav(OUT / "solstice3_pad_A_mid_B.wav", x / np.abs(x).max() * 0.8, SR)
    single = []
    for scan in (0.0, 0.5, 1.0):
        for m in (57, 64, 69):
            n_out = int(3.0 * SR)
            single.append(play(w, root, m, n_out, scan) * gp.adsr(n_out, 0.002, 0.5, 1.0, int(2.2 * SR), 0.6))
        single.append(np.zeros(int(0.6 * SR)))
    y = gp.reverb(np.concatenate(single), 3.0, 0.25)
    ga.write_wav(OUT / "solstice3_notes_A_mid_B.wav", y / np.abs(y).max() * 0.8, SR)
