"""Round 10: EMBER KEYS brighter (less wood: a metal tick, the tine's bell partials louder and longer, more 2nd and 3rd
in the ring, the pickup driven harder, 14 kHz) and LYRE as a bright harp (A) and a funk slap bass (B: thumb thud and
fret click, roundwound brightness dying fast, fret buzz in the first instant, its own root A1, 11 kHz)."""
import sys, pathlib
here = pathlib.Path(__file__).parent
_s11 = open(here / "waves11.py").read()
exec(_s11[:_s11.rindex("if __name__ ==")])


def harp_bass(rng):
    # A: a bright harp at A3: gut / nylon plucked a quarter of the way along, a resonant soundboard warming the low
    # partials, a long ring, a little 'zing' (the string's first instant, bright)
    root_a, sr_a, p = 57, 11025, 0.24
    f0a = gp.note_hz(root_a)
    pa = []
    for k in range(1, int(0.45 * sr_a / f0a)):
        r = k * np.sqrt(1 + 0.00005 * k * k)
        board = 0.7 + 0.5 * np.exp(-0.5 * (np.log2(k * f0a / 250) / 0.7) ** 2) + 0.3 * np.exp(-0.5 * (np.log2(k * f0a / 650) / 0.5) ** 2)
        amp = abs(np.sin(np.pi * k * p)) / k ** 1.25 * board
        pa.append((r, amp, amp * 0.45 / (1 + 0.06 * k * k), 2.2 / (1 + 0.03 * k * k)))
    a = _struck(sr_a, root_a, 0.20, pa, [(4000, 1.5, 0.30, 0.0002, 0.0012), (250, 1.2, 0.12, 0.001, 0.01)],
                0.0, 0.0003, (6.0, 0.015), 0.30, 91, rng.uniform(0, 6.3, len(pa) + 1))
    # B: a funk slap bass at A1: the thumb slaps near the neck end (bright: partials 1/k, roundwound steel, stiff),
    # the highs dying fast so it stays tight, the low partials ringing on; the thumb's thud, the string's snap against
    # the frets (a click), a fret buzz: the string clipped on the frets for the first ~40 ms
    root_b, sr_b, p = 33, 11025, 0.12
    f0b = gp.note_hz(root_b)
    pb = []
    for k in range(1, int(0.45 * sr_b / f0b)):
        r = k * np.sqrt(1 + 0.0008 * k * k)
        amp = abs(np.sin(np.pi * k * p)) / k ** 1.05
        pb.append((r, amp, amp * 0.5 / (1 + 0.5 * k * k), 1.2 / (1 + 0.25 * k ** 1.6)))
    yb, nt = _struck(sr_b, root_b, 0.26, pb, [(110, 1.2, 0.8, 0.001, 0.015), (3500, 1.6, 0.45, 0.0002, 0.002)],
                     0.0, 0.0004, (20.0, 0.02), 0.30, 92, rng.uniform(0, 6.3, len(pb) + 1))
    buzz = 1.0 + 3.0 * np.exp(-nt.t / 0.02) * nt.gone(0.2)          # fret buzz: driven hard, then linear (no join)
    pk = np.abs(yb[nt.T:]).max()
    yb = np.tanh(buzz * yb / pk) / np.tanh(1.0) * pk / buzz * (1 + 0.6 * (buzz - 1) / 3)
    return [a, (yb, nt)]


def ember_keys2(rng):
    """A brighter tine piano at C4: a metallic tick instead of the wooden thump, the tine's bell partials (7.1, 11.4,
    14.9 x) louder and ringing longer, the 2nd and 3rd stronger in the ring, the pickup driven harder. A: a soft hit;
    B: a hard one, the attack barking, the ring staying bell-like."""
    out = []
    for side, gain, bias, seed in (("A", 1.4, 0.16, 93), ("B", 3.0, 0.32, 94)):
        parts = [(1.0, 1.0, 0.45, 1.4), (2.0, 0.35, 0.12, 0.7), (3.0, 0.14, 0.04, 0.45), (4.0, 0.08, 0.012, 0.22),
                 (7.1, 0.55, 0.0, 0.09), (11.4, 0.28, 0.0, 0.035), (14.9, 0.12, 0.0, 0.018)]
        y, nt = _struck(14000, 60, 0.15, parts, [(2800, 1.5, 0.22, 0.0003, 0.002)], 0.0, 0.0004, (0.0, 0.03), 0.30,
                        seed, np.random.default_rng(95).uniform(0, 6.3, len(parts) + 1))
        y = y / np.abs(y).max()
        y = np.tanh(gain * y + bias) - np.tanh(bias)
        y = y - np.mean(y[nt.T:])
        out.append((y, nt))
    return out


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    WV = {}
    for name, fn in (("LYRE", harp_bass), ("EMBER KEYS", ember_keys2)):
        w = finish_pair(fn(np.random.default_rng(4242)), 0.0)
        size = 0; seams = []
        for s in w:
            x = np.concatenate([s["atk"], s["loop"], s["loop"][:2]])
            d = np.abs(np.diff(x)); typ = np.percentile(d, 99)
            T, n = len(s["atk"]), len(s["loop"])
            size += T + n + 1; seams.append(max(d[T - 1], d[T + n - 1]) / typ)
        print(f"{name:11s} A {w[0]['sr']:.0f} Hz ({len(w[0]['atk'])} + {len(w[0]['loop'])} B), "
              f"B {w[1]['sr']:.0f} Hz ({len(w[1]['atk'])} + {len(w[1]['loop'])} B): {size / 1024:.1f} KB, worst seam {max(seams):.2f}")
        WV[name] = w
    L = WV["LYRE"]; R = (57, 33)
    ga.write_wav(OUT / "lyre2_range_A_mid_B.wav",
                 phrase(L, R, [33, 40, 45, 52, 57, 64, 69, 76, 81, 76, 69, 64, 57, 52, 45, 40], 0.32, (0.001, 0.6, 0.6, 0.5)), SR)
    funk = [28, 28, 40, 28, 31, 33, 28, 40, 26, 26, 38, 26, 31, 33, 35, 36]
    ga.write_wav(OUT / "lyre2_funk_bass_A_mid_B.wav", phrase(L, R, funk, 0.17, (0.001, 0.25, 0.0, 0.05)), SR)
    ga.write_wav(OUT / "lyre2_harp_A_mid_B.wav",
                 phrase(L, R, [57, 60, 64, 69, 72, 76, 81, 84, 81, 76, 72, 69, 64, 60], 0.18, (0.001, 1.2, 0.3, 0.8)), SR)
    E = WV["EMBER KEYS"]; RE = (60, 60)
    ga.write_wav(OUT / "ember_keys2_chords_A_mid_B.wav",
                 phrase(E, RE, [[48, 55, 60, 64, 67], [45, 52, 57, 60, 64], [41, 48, 53, 57, 60], [43, 50, 55, 59, 62]],
                        1.3, (0.001, 1.4, 0.5, 0.5)), SR)
    ga.write_wav(OUT / "ember_keys2_melody_A_mid_B.wav",
                 phrase(E, RE, [72, 74, 76, 79, 76, 74, 72, 67, 69, 72], 0.3, (0.001, 1.0, 0.4, 0.3)), SR)
