"""Round 13 (2026-11-05): the SOLSTICE split into ingredients.
SOLSTICE: A = round 5's warm pad exactly; B = the same players playing brighter (strings bowed harder, the choir
opening 'oh' -> 'ah'), no dust. STAR DUST: its own layer wave, tuned to the played note's harmonics so it sits on any
pad. A = sparse glints and a breath of air; B = STARFALL, denser, longer rings, cascades stepping down the harmonics.
STAR DUST is a classic wave (flags 0): one glissando attack over free-running loops, so it can join a sounding pad.
Prints sizes at the lowest safe rate; renders A -> 50 % -> B."""
import sys, pathlib
here = pathlib.Path(__file__).parent
exec(open(here / "waves8.py").read().split('\nif __name__ == "__main__":')[0])
_w6 = open(here / "waves6.py").read()
exec(_w6[_w6.index("RATES = ["):_w6.index("def abmid")])


def warm_pad_bright(g, nt, sr, strings, voices, bell_ph):
    """warm_pad's players, the same draws, vibrato and drift, playing brighter"""
    ah = [(730, 90, 1.0), (1090, 110, 0.55), (2440, 150, 0.22), (3300, 200, 0.08)]
    body = [(450, 300, 0.8), (1100, 500, 0.8), (2800, 900, 0.7)]
    u = nt.m / nt.n
    y = 0.0
    for p in strings:
        fv = g.f0 + p["step"] * g.df
        vr, vd = p["vcyc"] * g.df, fv * (2 ** (p["vc"] / 1200) - 1)
        wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
        phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.15, 0.38) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
            + 0.4 * wan + nt.scoop(fv, -15, 0.05)
        k = np.arange(1, int(min(TOP, 0.45 * sr) / fv) + 1)
        amps = k ** -0.85 * formant(k * fv, body, 0.2) / (1 + (k * fv / 3600) ** 4)
        y = y + nt.ramp(p["enter"], p["enter"] + 0.25) * harm_sum(phase, list(amps), p["ph"], fv, sr)
    for p in voices:
        fv = g.f0 + p["step"] * g.df
        vr, vd = p["vcyc"] * g.df, fv * (2 ** (p["vc"] / 1200) - 1)
        wan = sum(np.sin(2 * np.pi * h * u + p["wph"][h - 1]) / h for h in range(1, 4)) / 1.83
        phase = 2 * np.pi * fv * nt.tau + nt.ramp(0.2, 0.38) * vd / vr * np.sin(2 * np.pi * vr * nt.tau + p["vph"]) \
            + 0.5 * wan + nt.scoop(fv, -30, 0.05)
        k = np.arange(1, int(min(TOP, 0.45 * sr) / fv) + 1)
        amps = k ** -1.1 * formant(k * fv, ah, 0.02)
        breath = g.periodic_noise(p["coef"], nt, lambda f: (sum(np.exp(-0.5 * ((f - h * fv) / 30) ** 2) for h in (1, 2, 3, 4))
                                                            + 0.5 * ga_band(f, 2800, 2.0)) * (f < TOP * 1.2))
        on = nt.ramp(p["enter"], p["enter"] + 0.22)
        y = y + 0.9 * on * harm_sum(phase, list(amps), p["ph"], fv, sr) + 0.12 * on * breath * (0.7 + 0.3 * np.sin(phase))
    lvl = np.sqrt(np.mean(y[nt.T:] ** 2))
    y = y + nt.ramp(0.05, 0.38) * 0.45 * lvl * (0.75 + 0.25 * np.sin(2 * np.pi * u + 1.0)) * \
        np.sin(2 * np.pi * g.f0 * nt.tau + bell_ph[1])
    return y, lvl


def solstice_strings(rng, L=1.0, T_s=0.40, sr_a=11025, sr_b=12000):
    g = Grid(sr_a, gp.note_hz(57), L, sr_b)
    strings, voices, bell_ph = warm_layers(rng)
    out = []
    for side, sr, n in (("A", g.sr_a, g.n_a), ("B", g.sr_b, g.n_b)):
        nt = Note(sr, n, T_s)
        y, _ = (warm_pad if side == "A" else warm_pad_bright)(g, nt, sr, strings, voices, bell_ph)
        out.append((y, nt))
    return out


# ------------------------------------------------------------------------------------------ STAR DUST --- #

DUST_SR = 12000
DUST_ROOT = 57


def glint(f, sr, prng, d=None, dur=0.9):
    t = ga.t_axis(sr, dur); d = d or prng.uniform(0.12, 0.45)
    y = np.sin(2 * np.pi * f * t + prng.uniform(0, 6.3)) * np.exp(-t / d)
    if 2.76 * f < 0.45 * sr:
        y += 0.25 * np.sin(2 * np.pi * 2.76 * f * t) * np.exp(-t / (0.3 * d))
    return y * (1 - np.exp(-t / 0.0008))


def star_dust(rng, sr=DUST_SR, LA=1.0, LB=0.8):
    """returns dict(atk, A, B, sr) (floats); the loops are drawn on circles (circular_hits), the air on the loop's bins"""
    f0 = gp.note_hz(DUST_ROOT)
    nA, fA = gp.loop_len(sr, f0, LA)
    nB = int(round(nA * LB / LA)); dfB = sr / nB
    drng = np.random.default_rng(4321)

    def hits(n, L, rate, H, d_rng, cascade):
        df = sr / n
        ev = []
        times = np.sort(drng.uniform(0, L, drng.poisson(rate * L)))
        for tt in times:
            h = H[drng.integers(len(H))]
            a = 10 ** (drng.uniform(-18, 0) / 20)
            f = round(h * f0 / df) * df
            if f >= 0.45 * sr:
                continue
            ev.append((tt, a, glint(f, sr, drng, drng.uniform(*d_rng))))
            if cascade and drng.uniform() < 0.35:              # a falling star: three glints stepping down
                hs = [x for x in H if x < h][-3:][::-1]
                for j, h2 in enumerate(hs):
                    f2 = round(h2 * f0 / df) * df
                    ev.append(((tt + 0.055 * (j + 1)) % L, a * 0.75 ** (j + 1), glint(f2, sr, drng, drng.uniform(*d_rng))))
        return gp.circular_hits(n, sr, ev)

    def air(n, gain_wave):
        df = sr / n; nb = n // 2 + 1; f = np.arange(nb) * df
        c = (drng.standard_normal(nb) + 1j * drng.standard_normal(nb)) * ga_band(f, 3800, 1.6) * (f > 1500) * (f < 0.45 * sr)
        y = np.fft.irfft(c, n=n); y /= np.sqrt(np.mean(y ** 2)) + 1e-12
        return y * gain_wave(np.arange(n) / n)

    H_A = [6, 8, 10, 12, 15, 16, 20, 24]
    H_B = [8, 10, 12, 15, 16, 18, 20, 24]
    a = hits(nA, LA, 11.0, H_A, (0.12, 0.45), False)
    a = a / np.sqrt(np.mean(a ** 2)) + 0.45 * air(nA, lambda u: 0.6 + 0.4 * np.sin(2 * np.pi * u + 0.5))
    b = hits(nB, LB, 26.0, H_B, (0.25, 0.7), True)
    b = b / np.sqrt(np.mean(b ** 2)) + 0.30 * air(nB, lambda u: 0.7 + 0.3 * np.sin(4 * np.pi * u))
    # the attack: a glissando of glints up the harmonics (round 5's), 0.4 s, alone over the loops' start
    T = int(0.45 * sr)
    gl = np.zeros(T + sr)
    for i, h in enumerate([4, 5, 6, 8, 10, 12, 15, 16, 20, 24]):
        if h * f0 >= 0.45 * sr:
            break
        s0 = int((0.01 + 0.028 * i) * sr); gg = glint(h * f0, sr, drng)
        gl[s0:s0 + len(gg)] += (0.6 + 0.4 * i / 9) * gg
    atk = gl[:T] * ss((T - np.arange(T)) / (0.12 * sr))
    return dict(atk=atk, A=a, B=b, sr=sr, nA=nA, nB=nB)


def dust_rates(d):
    """the lowest safe rate of each part, by waves6's rule (attack checked with its loop A)"""
    out = {}
    for key in ("A", "B"):
        s = dict(atk=d["atk"] if key == "A" else np.zeros(0), loop=d[key], sr=d["sr"], n=len(d[key]))
        out[key] = lowest(s)
    return out


def dust_store(d):
    """gain to PEAK, mu-law, resampled to the lowest safe rates: dict(atk, A, B, srs)"""
    k = gp.PEAK / max(np.abs(d["atk"]).max() * 1.0, np.abs(d["A"]).max(), np.abs(d["B"]).max())
    r = dust_rates(d)
    res = {}
    for key in ("A", "B"):
        x, sr2 = gp.resample_loop(d[key] * k, d["sr"], r[key]) if r[key] < d["sr"] - 1 else (d[key] * k, d["sr"])
        res[key] = dict(loop=ga.mulaw_roundtrip(x), sr=sr2)
    res["atk"] = dict(x=ga.mulaw_roundtrip(d["atk"] * k), sr=d["sr"])
    return res


def play_classic(st, root, note, n_out, scan, atk_level=1.0, seed=0):
    """flags-0 firmware: the loops free-run (a random start, A and B in step by fraction), the attack laid over them"""
    ratio = 2 ** ((note - root) / 12)
    prng = np.random.default_rng(seed + note)
    u0 = prng.uniform()
    y = np.zeros(n_out)
    for wgt, s in ((1 - scan, st["A"]), (scan, st["B"])):
        if wgt == 0:
            continue
        n = len(s["loop"]); pos = (u0 * n + np.arange(n_out) * s["sr"] / SR * ratio) % n
        j = pos.astype(int); fr = pos - j
        y += wgt * (s["loop"][j] * (1 - fr) + s["loop"][(j + 1) % n] * fr)
    a = st["atk"]["x"]; pos = np.arange(n_out) * st["atk"]["sr"] / SR * ratio
    ok = pos < len(a) - 1; j = pos[ok].astype(int); fr = pos[ok] - j
    y[ok] += atk_level * (a[j] * (1 - fr) + a[j + 1] * fr)
    return y


def chord_reel(voices_fn, scan):
    CH = [[45, 52, 60, 64, 67], [41, 48, 57, 60, 64], [48, 55, 64, 67, 71], [43, 50, 57, 62, 66]]
    seg = np.zeros(int((4 * 3.6 + 3.0) * SR))
    for c, chord in enumerate(CH):
        n_out = int(6.0 * SR)
        for m in chord:
            v = voices_fn(m, n_out, scan) * 0.25
            s0 = int(c * 3.6 * SR); L_ = min(n_out, len(seg) - s0)
            seg[s0:s0 + L_] += v[:L_]
    return seg


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    sol_f = finish_pair_float(solstice_strings(np.random.default_rng(777)), 0.0)
    sol = [mu(rerate(s, lowest(s))) for s in sol_f]
    size_sol = sum(len(s["atk"]) + len(s["loop"]) + 1 for s in sol)
    for s, t in zip(sol, "AB"):
        print(f"SOLSTICE {t}: attack {len(s['atk'])} + loop {len(s['loop'])} at {s['sr']:.0f} Hz; seam {seam(s):.2f}")
    print(f"SOLSTICE: {size_sol / 1024:.1f} KB (A alone {(len(sol[0]['atk']) + len(sol[0]['loop']) + 1) / 1024:.1f})")

    d = star_dust(np.random.default_rng(5))
    st = dust_store(d)
    size_d = len(st["atk"]["x"]) + sum(len(st[k]["loop"]) + 1 for k in "AB")
    print(f"STAR DUST: attack {len(st['atk']['x'])} at {st['atk']['sr']:.0f}, A {len(st['A']['loop'])} at "
          f"{st['A']['sr']:.0f}, B {len(st['B']['loop'])} at {st['B']['sr']:.0f}: {size_d / 1024:.1f} KB")
    print(f"together {(size_sol + size_d) / 1024:.1f} KB (round 5 SOLSTICE was 24.6)")

    PAD_ENV, DUST_ENV = (0.005, 0.5, 1.0, 1.2), (0.02, 0.5, 1.0, 1.4)

    def pad_v(m, n, sc):
        return play(sol, 57, m, n, sc) * gp.adsr(n, *PAD_ENV[:3], int(3.4 * SR), PAD_ENV[3])

    def dust_v(m, n, sc):
        return play_classic(st, DUST_ROOT, m, n, sc) * gp.adsr(n, *DUST_ENV[:3], int(3.4 * SR), DUST_ENV[3])

    for name, fn, rev in (("solstice_strings", pad_v, 0.35), ("star_dust", dust_v, 0.35),
                          ("solstice_plus_dust", lambda m, n, sc: pad_v(m, n, sc) + 0.55 * dust_v(m, n, sc), 0.35)):
        x = np.concatenate(sum(([chord_reel(fn, sc), np.zeros(SR)] for sc in (0.0, 0.5, 1.0)), []))
        x = gp.reverb(x, 4.0, rev)
        ga.write_wav(OUT / f"{name}_A_mid_B.wav", x / np.abs(x).max() * 0.8, SR)
    # the old round-5 SOLSTICE B (pad + dust) for comparison: SCAN 1 only
    old = finish_pair(solstice_pad(np.random.default_rng(777)), 0.0)
    x = gp.reverb(chord_reel(lambda m, n, sc: play(old, 57, m, n, sc) * gp.adsr(n, *PAD_ENV[:3], int(3.4 * SR), PAD_ENV[3]), 1.0), 4.0, 0.35)
    ga.write_wav(OUT / "old_round5_solstice_B.wav", x / np.abs(x).max() * 0.8, SR)
