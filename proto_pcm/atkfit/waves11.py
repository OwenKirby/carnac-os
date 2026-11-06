"""Round 9 all-rounders: LYRE (A a nylon lyre / harp pluck; B a fingered bass string, its own samples rooted at A1 so
the bass octaves play it near its own pitch) and EMBER KEYS (a tine electric piano, A mellow, B a hard hit whose
pickup barks). Attack + ring loop, as the drums."""
import sys, pathlib
here = pathlib.Path(__file__).parent
_src5 = open(here / "waves5.py").read()
exec(_src5[:_src5.rindex("if __name__ ==")])


def lyre(rng):
    # A: nylon lyre at A3. A plucked string's partials sin(pi k p) / k^1.5 (p: where it is plucked), each decaying
    # faster the higher it is, a little stiffness stretching them, the wooden body warming the low ones, a nail click
    root_a, sr_a, p = 57, 11025, 0.18
    f0a = gp.note_hz(root_a)
    pa = []
    for k in range(1, int(0.45 * sr_a / f0a)):
        r = k * np.sqrt(1 + 0.00008 * k * k)
        amp = abs(np.sin(np.pi * k * p)) / k ** 1.5 * (0.6 + 0.6 * np.exp(-0.5 * (np.log2(k * f0a / 260) / 0.8) ** 2))
        pa.append((r, amp, amp * 0.42 / (1 + 0.12 * k * k), 1.6 / (1 + 0.04 * k * k)))
    a = _struck(sr_a, root_a, 0.20, pa, [(3200, 1.5, 0.35, 0.0002, 0.0015), (300, 1.2, 0.2, 0.001, 0.008)],
                0.0, 0.0004, (8.0, 0.02), 0.30, 81, rng.uniform(0, 6.3, len(pa) + 1))
    # B: a fingered bass string at A1 (55 Hz): plucked further from the bridge, round; steel, so stiffer (it growls);
    # the low partials ring on; a finger thump and a faint click; released sharp, settling onto the note
    root_b, sr_b, p = 33, 6000, 0.22
    f0b = gp.note_hz(root_b)
    pb = []
    for k in range(1, int(0.45 * sr_b / f0b)):
        r = k * np.sqrt(1 + 0.0004 * k * k)
        amp = abs(np.sin(np.pi * k * p)) / k ** 1.8
        pb.append((r, amp, amp * 0.6 / (1 + 0.35 * k * k), 2.5 / (1 + 0.08 * k * k)))
    b = _struck(sr_b, root_b, 0.26, pb, [(140, 1.2, 0.5, 0.002, 0.012), (1500, 1.5, 0.12, 0.0005, 0.003)],
                0.0, 0.0015, (15.0, 0.025), 0.30, 82, rng.uniform(0, 6.3, len(pb) + 1))
    return [a, b]


def ember_keys(rng):
    """A tine electric piano at C4: the tine's fundamental and its tonebar, a high 'ding' of the tine's own mode that
    dies in tens of ms, a hammer thump; through the pickup's asymmetric curve (even harmonics). A: a soft hit, mellow.
    B: a hard hit: the pickup driven harder, so the attack barks and the sustain stays bell-like."""
    out = []
    for side, gain, bias, seed in (("A", 1.0, 0.12, 83), ("B", 2.6, 0.30, 84)):
        parts = [(1.0, 1.0, 0.45, 1.2), (2.0, 0.12, 0.03, 0.5), (3.0, 0.05, 0.008, 0.3), (4.0, 0.06, 0.0, 0.12),
                 (7.1, 0.35, 0.0, 0.035), (11.4, 0.12, 0.0, 0.015)]
        y, nt = _struck(11025, 60, 0.15, parts, [(400, 1.5, 0.25, 0.001, 0.006)], 0.0, 0.0005, (0.0, 0.03), 0.30,
                        seed, np.random.default_rng(85).uniform(0, 6.3, len(parts) + 1))
        y = y / np.abs(y).max()
        y = np.tanh(gain * y + bias) - np.tanh(bias)               # the pickup: asymmetric, so the 2nd harmonic grows
        y = y - np.mean(y[nt.T:])                                  # no DC in the loop
        out.append((y, nt))
    return out


def play_any(w, roots, note, n_out, scan):
    """the proposed firmware with each side at its own root: the attack nearer SCAN alone, then both loops from index
    0 as it ends, each at its own step, SCAN's crossfade"""
    k = 0 if scan < 0.5 else 1
    steps = [s["sr"] / SR * 2 ** ((note - r) / 12) for s, r in zip(w, roots)]
    atk = w[k]["atk"]; sk = steps[k]
    pos = np.arange(n_out) * sk
    ia = pos < len(atk) - 1
    y = np.zeros(n_out)
    i = pos[ia].astype(int); fr = pos[ia] - i
    y[ia] = atk[i] * (1 - fr) + atk[i + 1] * fr
    t_end = len(atk) / sk
    idx = np.arange(n_out)[~ia]
    for wgt, s, st in ((1 - scan, w[0], steps[0]), (scan, w[1], steps[1])):
        if wgt == 0:
            continue
        lp = ((idx - t_end) * st) % s["n"]
        j = lp.astype(int); f2 = lp - j
        y[~ia] += wgt * (s["loop"][j % s["n"]] * (1 - f2) + s["loop"][(j + 1) % s["n"]] * f2)
    return y


def phrase(w, roots, notes, gate, env, scans=(0.0, 0.5, 1.0)):
    out = []
    for scan in scans:
        parts = []
        for m in notes:
            n_out = int((gate + 1.2) * SR)
            e = gp.adsr(n_out, env[0], env[1], env[2], int(gate * SR), env[3])
            ms = m if isinstance(m, (list, tuple)) else [m]
            parts.append(sum(play_any(w, roots, mm, n_out, scan) for mm in ms) * e / len(ms) ** 0.5)
        hop = int((gate + 0.05) * SR)
        o = np.zeros(hop * (len(parts) - 1) + len(parts[-1]))
        for i, xx in enumerate(parts):
            o[i * hop:i * hop + len(xx)] += xx
        out += [o, np.zeros(int(0.6 * SR))]
    o = np.concatenate(out)
    return o / np.abs(o).max() * 0.8


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    WV = {}
    for name, fn in (("LYRE", lyre), ("EMBER KEYS", ember_keys)):
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
    ga.write_wav(OUT / "lyre_range_A_mid_B.wav",
                 phrase(L, R, [33, 40, 45, 52, 57, 64, 69, 76, 81, 76, 69, 64, 57, 52, 45, 40], 0.32, (0.001, 0.6, 0.6, 0.5)), SR)
    ga.write_wav(OUT / "lyre_bassline_A_mid_B.wav",
                 phrase(L, R, [28, 28, 35, 33, 31, 31, 38, 36, 33, 33, 40, 38, 35, 35, 31, 33], 0.30, (0.001, 0.8, 0.5, 0.15)), SR)
    ga.write_wav(OUT / "lyre_strum_A_mid_B.wav",
                 phrase(L, R, [[45, 52, 57, 60, 64], [41, 48, 53, 57, 60], [43, 50, 55, 59, 62], [40, 47, 52, 56, 59]],
                        1.2, (0.001, 1.0, 0.4, 0.6)), SR)
    E = WV["EMBER KEYS"]; RE = (60, 60)
    ga.write_wav(OUT / "ember_keys_chords_A_mid_B.wav",
                 phrase(E, RE, [[48, 55, 60, 64, 67], [45, 52, 57, 60, 64], [41, 48, 53, 57, 60], [43, 50, 55, 59, 62]],
                        1.3, (0.001, 1.4, 0.5, 0.5)), SR)
    ga.write_wav(OUT / "ember_keys_melody_A_mid_B.wav",
                 phrase(E, RE, [72, 74, 76, 79, 76, 74, 72, 67, 69, 72], 0.3, (0.001, 1.0, 0.4, 0.3)), SR)
