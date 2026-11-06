"""SOLSTICE: a whole-note PCM pad that combines the iconic 90s ROMpler pads (2026-11-05, after MOONSONG's brief):
the D-50 Fantasia's bell over a swelling pad, the JV / M1 warm string-choir, the JD Glass Pad / Proteus Ethereal
air and glass, the Wavestation's spectral motion inside the loop. A: WARM (dark strings + an 'oh' choir + breath).
B: GLASS HEAVEN (bright strings, an open 'aah' with its ring, an octave-and-fifth halo, glass harmonics twinkling, a
slow shimmer sweeping up through the overtones). The attack: a glass bell strike, the pad swelling under it, the
bell ringing down into the loop's own shimmer."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "notes.py").read().split("TUNES = {")[0])


def solstice(rng, L=1.0, T_s=0.40, sr_a=11025, sr_b=14000, L_b=2.0):
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    g.n_b = int(round(g.n_a * L_b / L * sr_b / sr_a))      # B: L_b long, on a grid L_b / L finer, the same f0 in Hz
    df_b = g.df * L / L_b
    g.sr_b = g.n_b * df_b
    oh = [(450, 80, 1.0), (850, 100, 0.45), (2400, 150, 0.08)]
    aah = [(730, 90, 1.0), (1090, 110, 0.7), (2440, 160, 0.4), (2900, 300, 0.55), (3400, 300, 0.25)]
    body = [(450, 300, 1.0), (1100, 500, 0.7), (2800, 900, 0.4)]
    strings = [dict(step=s, vcyc=v, vc=c, vph=rng.uniform(0, 6.3), wph=rng.uniform(0, 6.3, 6),
                    ph=rng.uniform(0, 6.3, 80), enter=rng.uniform(0.04, 0.12))
               for s, v, c in [(-2, 5, 8), (-1, 4, 10), (0, 5, 7), (0, 6, 9), (1, 4, 8), (2, 5, 10)]]
    voices = [dict(step=s, vcyc=v, vc=c, vph=rng.uniform(0, 6.3), wph=rng.uniform(0, 6.3, 6),
                   ph=rng.uniform(0, 6.3, 80), coef=rng.standard_normal(6000) + 1j * rng.standard_normal(6000),
                   enter=rng.uniform(0.08, 0.16))
              for s, v, c in [(-1, 4, 12), (0, 5, 10), (1, 4, 14), (0, 3, 9)]]
    bell_ph = rng.uniform(0, 6.3, 40)
    tw = [(k, rng.integers(1, 4), rng.uniform(0, 6.3)) for k in (8, 10, 12, 15, 16, 20, 24)]
    tw_b = [(k, c, ph) for (k, c, ph), k2 in zip(tw, (16, 20, 24, 28, 30, 32, 36))]
    sweep_ph = rng.uniform(0, 6.3)
    air_coef = rng.standard_normal(20000) + 1j * rng.standard_normal(20000)   # (drawn last: A's draws unchanged)
    ee = [(270, 60, 0.6), (2290, 200, 1.0), (3010, 260, 0.8), (3400, 300, 0.5)]
    cold_body = [(1800, 900, 1.0), (3600, 1200, 0.8)]
    out = []
    for side, sr, n in (("A", g.sr_a, g.n_a), ("B", g.sr_b, g.n_b)):
        nt = Note(sr, n, T_s)
        u = nt.m / n
        y = 0.0
        # the strings: a section, each player drifting, the vibrato blooming after the entry
        for p in strings:
            fv = g.f0 + p["step"] * g.df
            vc_k = 1.0 if side == "A" else 0.35                  # B: cold, nearly still
            vr, vd = (p["vcyc"] * g.df if side == "A" else round(p["vcyc"] * L_b / L) * df_b), fv * (2 ** (vc_k * p["vc"] / 1200) - 1)
            wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
            phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.15, 0.38) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
                + 0.4 * wan + nt.scoop(fv, -15, 0.05)
            kt = int(min(TOP * (1.0 if side == "A" else 1.2), 0.45 * sr) / fv)
            k = np.arange(1, kt + 1)
            amps = k ** -1.1 * formant(k * fv, body, 0.15) if side == "A" else                 k ** -0.8 * formant(k * fv, cold_body, 0.25) * (1 - 0.7 * np.exp(-(k - 1) / 1.5))
            if side == "A":
                amps = amps / (1 + (k * fv / 2200) ** 4)
            y = y + nt.ramp(p["enter"], p["enter"] + 0.25) * harm_sum(phase, list(amps), p["ph"], fv, sr)
        # the choir: an 'oh' (A) or an open 'aah' with its ring (B), and their breath, pitched
        for p in voices:
            fv = g.f0 + p["step"] * g.df
            vr = p["vcyc"] * g.df if side == "A" else round(p["vcyc"] * L_b / L) * df_b
            vd = fv * (2 ** ((1.0 if side == "A" else 0.35) * p["vc"] / 1200) - 1)
            wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
            phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.2, 0.38) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
                + 0.5 * wan + nt.scoop(fv, -30, 0.05)
            kt = int(min(TOP, 0.45 * sr) / fv)
            k = np.arange(1, kt + 1)
            amps = (k ** -1.5 * formant(k * fv, oh, 0.01)) if side == "A" else (k ** -0.9 * formant(k * fv, ee, 0.03))
            breath = g.periodic_noise(p["coef"], nt, lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 30) ** 2) for h in (1, 2, 3, 4))
                                                                + 0.4 * ga_band(f, 2600, 2.0)) * (f < TOP * 1.2))
            on = nt.ramp(p["enter"], p["enter"] + 0.22)
            y = y + 0.9 * on * harm_sum(phase, list(amps), p["ph"], fv, sr) \
                + (0.10 if side == "A" else 0.30) * on * breath * (0.7 + 0.3 * np.sin(phase))
        lvl = np.sqrt(np.mean(y[nt.T:] ** 2))
        t0 = 2 * np.pi * g.f0 * nt.tau
        if side == "B":
            # the halo: octave and fifths, each swelling once a loop; the glass twinkling; the shimmer sweep, a
            # narrow peak climbing harmonics 6 -> 18 and back once a loop (the Wavestation's motion, inside the wave)
            # the air: cold, unpitched, high, breathing once a loop
            air = g.periodic_noise(air_coef, nt, lambda f: ga_band(f, 4800, 1.4) * (f > 2000))
            y = y + nt.ramp(0.05, 0.38) * 0.55 * lvl * (0.75 + 0.25 * np.sin(2 * np.pi * u + 2.0)) * air
            for kk, gg in ((4, 0.5), (8, 0.45), (12, 0.3), (16, 0.25)):
                sw = 0.65 + 0.35 * np.sin(2 * np.pi * u + kk)
                y = y + nt.ramp(0.1, 0.38) * gg * lvl * sw * np.sin(kk * t0 + bell_ph[kk])
            for (kk, cyc, ph), k2 in zip(tw, (16, 20, 24, 28, 30, 32, 36)):   # higher glass, slower glints
                if k2 * g.f0 >= 0.45 * sr: continue
                tw_ = (0.5 + 0.5 * np.sin(2 * np.pi * max(1, cyc - 1) * u + ph)) ** 3
                y = y + 0.30 * lvl * tw_ * np.sin(k2 * t0 + bell_ph[kk])
            pos = 18 + 8 * np.sin(2 * np.pi * u + sweep_ph)      # once a loop: once every 2 s
            for kk in range(8, 28):
                y = y + nt.ramp(0.1, 0.38) * 0.24 * lvl * np.exp(-0.5 * ((kk - pos) / 0.9) ** 2) * np.sin(kk * t0 + bell_ph[kk % 40])
        else:
            # A: the warm root swelling under it, once a loop
            y = y + nt.ramp(0.05, 0.38) * 0.7 * lvl * (0.75 + 0.25 * np.sin(2 * np.pi * u + 1.0)) * np.sin(t0 + bell_ph[1])
        # the bell (Fantasia): a strike on harmonics 2..12 (B brighter and louder), ringing down into the loop
        bell = [(4, 1.0, 0.5), (6, 0.5, 0.35), (8, 0.7, 0.4), (12, 0.45, 0.3), (16, 0.35, 0.25), (24, 0.2, 0.18)]
        if side == "A":
            bell = [(1, 0.6, 0.4), (2, 1.0, 0.35), (3, 0.35, 0.25), (4, 0.4, 0.25)]
        strike = sum(a * np.exp(-nt.t / d) * np.sin(kk * t0 + bell_ph[kk % 40]) for kk, a, d in bell
                     if kk * g.f0 < 0.45 * sr) * (1 - np.exp(-nt.t / 0.0008)) * nt.gone(0.15)
        y = y + (1.4 if side == "B" else 0.9) * lvl * strike
        out.append((y, nt))
    return out


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    w = finish_pair(solstice(np.random.default_rng(777)), 1.5)
    size = 0
    for s, tag in zip(w, "AB"):
        x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
        d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
        T, n = len(s["atk"]), len(s["loop"])
        size += T + n + 1
        print(f"SOLSTICE {tag}: attack {T} B + loop {n} B ({n / s['sr']:.2f} s) at {s['sr']:.0f} Hz; seam "
              f"{d[T-1]/typ:.2f}, wrap {d[T+n-1]/typ:.2f}")
    print(f"size {size / 1024:.1f} KB as prototyped")
    root = 57
    CH = [[45, 52, 60, 64, 67], [41, 48, 57, 60, 64], [48, 55, 64, 67, 71], [43, 50, 57, 62, 66]]   # Am9 Fmaj7 Cmaj7 Gadd9-ish
    parts = []
    for scan in (0.0, 0.5, 1.0):
        seg = np.zeros(int((4 * 3.6 + 3.0) * SR))
        for c, chord in enumerate(CH):
            n_out = int(6.0 * SR)
            e = gp.adsr(n_out, 0.08, 0.5, 1.0, int(3.4 * SR), 1.2)
            for m in chord:
                v = play(w, root, m, n_out, scan) * e * 0.25
                s0 = int(c * 3.6 * SR)
                L_ = min(n_out, len(seg) - s0)
                seg[s0:s0 + L_] += v[:L_]
        parts += [seg, np.zeros(SR)]
    x = np.concatenate(parts)
    x = gp.reverb(x, 4.0, 0.35)
    ga.write_wav(OUT / "solstice2_pad_A_mid_B.wav", x / np.abs(x).max() * 0.8, SR)
    # single notes: the bell into the pad, A -> 50 % -> B
    single = []
    for scan in (0.0, 0.5, 1.0):
        for m in (57, 64):
            n_out = int(3.2 * SR)
            single.append(play(w, root, m, n_out, scan) * gp.adsr(n_out, 0.002, 0.5, 1.0, int(2.4 * SR), 0.6))
        single.append(np.zeros(int(0.6 * SR)))
    y = np.concatenate(single)
    ga.write_wav(OUT / "solstice2_notes_A_mid_B.wav", y / np.abs(y).max() * 0.8, SR)
