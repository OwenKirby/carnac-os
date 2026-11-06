"""JD-800-style attacks: each attack is the opening of its own loop (cut from the loop's tone and breath, shaped), and
ends exactly on the loop, which picks up sample-aligned. One attack per loop (A and B), the nearer to SCAN plays."""
import sys, pathlib, numpy as np
sys.path.insert(0, sys.argv[1]); OUT = pathlib.Path(sys.argv[2]); OUT.mkdir(parents=True, exist_ok=True)
import gen_pcm_waves as gp, gen_attacks as ga
SR = gp.SR
S = gp.build_samples()

def ss(x):                                   # smoothstep 0..1
    x = np.clip(x, 0, 1); return x * x * (3 - 2 * x)

class Split:
    """A loop split into its partials (by harmonic number, or for an inharmonic loop its strong bins) and the rest
    (breath, roughness), each a periodic signal on the loop's own grid."""
    def __init__(self, s, inharm=False, tol=3):
        self.x, self.sr, self.root = s["data"], s["sr"], s["root"]
        n = self.n = len(self.x); X = self.X = np.fft.rfft(self.x); k = np.arange(len(X))
        k0 = n * gp.note_hz(self.root) / self.sr
        self.k0 = k0
        if inharm:
            m = np.abs(X); self.tone = m > np.percentile(m, 99.0)
            self.h = np.maximum(1, np.round(k / k0)).astype(int)
        else:
            self.h = np.round(k / k0).astype(int)
            self.tone = (np.abs(k - self.h * k0) <= tol) & (self.h >= 1)
    def part(self, sel):
        return np.fft.irfft(self.X * sel, n=self.n)
    def air(self): return self.part(~self.tone)
    def harm(self, lo, hi=999): return self.part(self.tone & (self.h >= lo) & (self.h <= hi))

def rd(x, pos):                              # periodic lerp read
    n = len(x); i = np.floor(pos).astype(int); f = pos - i
    return x[i % n] * (1 - f) + x[(i + 1) % n] * f

def onset(sp, M_s, parts, scoop_cents=0.0, scoop_tau=0.02, j0=0.0):
    """parts: [(signal, env(t, T) -> gain that is 1 at T)]. The read position scoops up to pitch; returns (attack,
    the loop position where the loop takes over)."""
    M = int(M_s * sp.sr); t = np.arange(M) / sp.sr
    c = 1 - 2 ** (-scoop_cents / 1200)
    w = np.arange(M) - c * scoop_tau * sp.sr * (1 - np.exp(-t / scoop_tau))
    pos = j0 + w
    y = sum(env(t, M_s) * rd(sig, pos) for sig, env in parts)
    end = (j0 + M - c * scoop_tau * sp.sr * (1 - np.exp(-M_s / scoop_tau))) % sp.n
    return y, end

def bump(g, tau, rise=0.0):                  # 1 + an extra that dies away and is exactly 0 at T
    return lambda t, T: ss(t / rise if rise else 1.0) * (1 + g * np.exp(-t / tau) * (1 - t / T) ** 2)
def grow(t0, t1):
    return lambda t, T: ss((t - t0) / max(t1 - t0, 1e-6))

def pan(sp):
    return onset(sp, 0.12, [
        (sp.air(), lambda t, T: ss(t / 0.002) * (1 + 3.5 * np.exp(-t / 0.018) * (1 - t / T) ** 2)),   # the chiff
        (sp.harm(1, 1), grow(0.004, 0.040)),
        (sp.harm(3, 3), lambda t, T: ss(t / 0.006) * (1 + 3.0 * np.exp(-t / 0.015) * (1 - t / T) ** 2)),  # overblown 12th
        (sp.harm(2, 2) + sp.harm(4), grow(0.010, 0.060))], scoop_cents=40, scoop_tau=0.012)

def voice(sp, scoop=45):
    hs = [(sp.harm(h, h), grow(0.012 + 0.004 * h, min(0.17, 0.05 + 0.009 * h))) for h in range(1, 16)]
    return onset(sp, 0.18, [(sp.air(), lambda t, T: ss(t / 0.02) * (1 + 3.0 * np.exp(-t / 0.05) * (1 - t / T) ** 2))]
                 + hs + [(sp.harm(16), grow(0.08, 0.17))], scoop_cents=scoop, scoop_tau=0.03)

def stone(sp):
    lo = sp.part(sp.tone & (np.arange(len(sp.X)) < 1.6 * sp.k0)); hi = sp.part(sp.tone & (np.arange(len(sp.X)) >= 1.6 * sp.k0))
    n = sp.n; tick = np.zeros(n); tick[:3] = [1.0, -0.6, 0.2]
    return onset(sp, 0.22, [
        (lo, bump(1.5, 0.08, 0.0008)), (hi, bump(4.0, 0.03, 0.0005)),
        (sp.air(), lambda t, T: 0.35 + 0.65 * ss(t / 0.15))])           # the hand starts rubbing as the strike rings

RECIPES = {"PAN FLUTE": (pan, False), "CHOIR": (voice, False), "CAIRN SPIRIT": (voice, False),
           "SINGING STN": (stone, True), "HORNS": (lambda sp: voice(sp, 150), False)}

def build(wave):
    name, la, lb = wave[:3]
    fn, inh = RECIPES[name]
    return [fn(Split(S[l], inh)) + (S[l],) for l in (la, lb)]

def play_new(wave, atks, note, n, scan, rng):
    name, la, lb, brate, boff = wave[:5]
    A, B = S[la], S[lb]
    k_i = 0 if scan[0] < 0.5 else 1
    katk, kend, ks = atks[k_i]
    step = ks["sr"] / SR * 2 ** ((note - ks["root"]) / 12)
    M = len(katk); nk = min(n, int(M / step))
    # the loops start so that the nearer one is at `kend` when the attack ends (both loops of a pair share a grid)
    frac = (kend - nk * step) / ks["n"] if "n" in ks else (kend - nk * step) / len(ks["data"])
    a = gp.read_loop(A["data"], A["sr"], note, A["root"], n, 1.0, frac % 1.0 * len(A["data"]))
    b = gp.read_loop(B["data"], B["sr"], note, B["root"], n, brate, (frac + boff) % 1.0 * len(B["data"]))
    s = np.clip(scan, 0, 1)
    y = a * (1 - s) + b * s
    k, _ = gp.read_once(np.append(katk, 0.0), ks["sr"], note, ks["root"], n)
    y[:nk] = k[:nk]
    return y

def phrase(play, wave, notes, scan_v, rng):
    parts = []
    for m, g in notes:
        n = int((g + 0.6) * SR); sc = np.full(n, scan_v)
        env = gp.adsr(n, 0.002, 0.3, 1.0, int(g * SR), 0.18)
        parts.append(play(m, n, sc) * env)
    return parts

def seq(parts, gap):
    out = np.zeros(sum(int(gap * SR) for _ in parts) + len(parts[-1]))
    p = 0
    for x in parts:
        out[p:p + len(x)] += x; p += int(gap * SR)
    return out

TUNES = {"PAN FLUTE": [74, 76, 79, 81, 79, 76, 74, 69], "CHOIR": [57, 60, 64, 62, 57, 55, 57, 64],
         "CAIRN SPIRIT": [57, 60, 64, 62, 57, 55, 57, 64], "SINGING STN": [50, 57, 53, 62, 57, 50, 45, 50],
         "HORNS": [43, 48, 50, 48, 43, 38, 43, 48]}
for wave in gp.WAVES:
    if wave[0] not in RECIPES: continue
    atks = build(wave)
    for (x, end, s), tag in zip(atks, "AB"):
        print(f"{wave[0]:12s} {tag}: attack {len(x)} B at {s['sr']} Hz, loop picks up at {end:.1f}/{len(s['data'])}, "
              f"end jump {abs(x[-1] - gp.read_loop(s['data'], s['sr'], s['root'], s['root'], 1, 1.0, end)[0]):.3f}")
    for scan_v, tag in ((0.0, "A"), (1.0, "B")):
        notes = [(m, 0.55) for m in TUNES[wave[0]]]
        rng = np.random.default_rng(3)
        old = seq(phrase(lambda m, n, sc: gp.play_wave(S, wave, m, n, sc, rng), wave, notes, scan_v, rng), 0.65)
        new = seq(phrase(lambda m, n, sc: play_new(wave, atks, m, n, sc, rng), wave, notes, scan_v, rng), 0.65)
        pk = max(np.abs(old).max(), np.abs(new).max()) / 0.8
        fn = wave[0].lower().replace(" ", "_")
        ga.write_wav(OUT / f"{fn}_scan{tag}_1_now.wav", old / pk, SR)
        ga.write_wav(OUT / f"{fn}_scan{tag}_2_jd.wav", new / pk, SR)
