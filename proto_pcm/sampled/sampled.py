"""Round 1 of the sampled waves (2026-11-06): SPIRIT VOX, HOARFROST, SIDHE HARP, in the CAIRN SPIRIT spirit (layers,
a WOW attack, a dramatic SCAN) but built from the new source WAVs, every layer de-reverbed (dry.py) and locked to
the root. Each model returns [(y, nt), (y, nt)] for finish_pair_float, A and B with the same attack length.

SAMPLED_L is in definitive.py's L_ format. DRYCHECK (a dir) also writes each layer wet and dry for the ear."""
import os, pathlib, numpy as np
import dry
from dry import SR

_cache = {}
DRYCHECK = os.environ.get("DRYCHECK")


def layer(rel, k, root, skip=0.0, beta=1.0, shift=0):
    """note k of rel (sounding at MIDI root): de-reverbed, locked to the root, then shifted `shift` semitones,
    starting 5 ms before its onset (+ skip s)"""
    key = (rel, k, root, skip, beta, shift)
    if key not in _cache:
        m, on, off = dry.get_note(rel, k)
        prof = dry.reverb_profile(m, off, verbose=f"{pathlib.Path(rel).stem[:12]:12} n{k}")
        y = dry.dereverb(m, prof, beta=beta)
        n_in = len(y)
        y, cents = dry.to_root(y, root)
        if shift:
            y = dry.resample(y, 2 ** (-shift / 12))
        on2 = int(round(on * len(y) / len(m)))
        start = max(0, on2 - int(0.005 * SR)) + int(skip * SR)
        if DRYCHECK:
            import gen_attacks as ga
            d = pathlib.Path(DRYCHECK); d.mkdir(parents=True, exist_ok=True)
            w = dry.resample(m, len(y) / n_in)
            pk = np.abs(w).max() / 0.8
            nm = f"{pathlib.Path(rel).stem}_n{k}"
            ga.write_wav(d / f"{nm}_1_wet.wav", w / pk, SR)
            ga.write_wav(d / f"{nm}_2_dry.wav", y / pk, SR)
        print(f"     {pathlib.Path(rel).stem} n{k}: {cents:+.0f} c corrected")
        _cache[key] = y[start:]
    return _cache[key]


def rms(x):
    return np.sqrt(np.mean(x ** 2)) + 1e-12


def mix(parts, dur):
    """sum of (signal, gain dB vs the first part's RMS over 0.3..1.3 s, fade) to dur s"""
    n = int(dur * SR); out = np.zeros(n); ref = None
    for x, db, shape in parts:
        x = x[:n]; x = np.pad(x, (0, n - len(x)))
        r = rms(x[int(0.3 * SR):int(1.3 * SR)])
        ref = ref or r
        g = ref / r * 10 ** (db / 20)
        out += g * x * (shape(np.arange(n) / SR) if shape else 1.0)
    return out


def fade_in(t0, t1):
    return lambda t: np.clip((t - t0) / (t1 - t0), 0, 1) ** 2


def gone_by(t0, t1):          # 1 until t0, smooth to 0 at t1 (attack-only layers end before the handoff)
    return lambda t: 0.5 + 0.5 * np.cos(np.pi * np.clip((t - t0) / (t1 - t0), 0, 1))


def tame_burst(y, T_s, over_db=4.0, win=0.03):
    """the attack may rise at most over_db above the body's level (median over the loop source): a vox onset, not a
    hit. Gain from a smoothed envelope, so the onset keeps its shape, only lower."""
    T = int(T_s * SR); w = int(win * SR)
    e = np.sqrt(np.convolve(y * y, np.ones(w) / w, "same") + 1e-12)
    body = np.median(e[T:])
    g = np.minimum(1.0, body * 10 ** (over_db / 20) / e)
    g[T:] = 1.0
    g = np.convolve(g, np.ones(w) / w, "same")
    return y * g


def whole_note(y, T_s, L_s, xf_s, flat=True, evn=0.6):
    T, xf = int(T_s * SR), int(xf_s * SR)
    if flat:
        y, k = dry.flatten(y, T - xf, T + int(L_s * SR))
    if evn:
        y = dry.even(y, T - xf, depth=evn)
    loop, n, rho = dry.make_loop(y, T, L_s, xf)
    print(f"     loop {n / SR:.3f} s, crossfade rho {rho:+.2f}")
    return dry.whole(y, T, loop)


# ------------------------------------------------------------------------------------------- SPIRIT VOX --- #
def spirit_vox(rng, T=0.45, L=1.15, xf=0.30):
    """A BARROW: the dark angel choir (Angels C3) with the airy Glass_Voices underneath, a hum of the octave below.
    B OTHERWORLD: the bright fantasy vox (Fantasy_Vox C3, its own burst) with a solo voice (Voice C4) an octave up as a halo,
    a pure vox (the crystal bell strike was taken out, 2026-11-06: percussion comes from the other oscillator)."""
    R = 48
    ang = layer("reviewed/Angels.wav", 2, R)
    gv3 = layer("reviewed/Glass_Voices.wav", 1, R, skip=0.6)          # Glass_Voices swells ~1 s: take it bloomed
    fv = layer("inspiration pads/Fantasy_Vox.wav", 1, R)
    solo = layer("reviewed/Voice.wav", 0, R + 12)                    # a solo voice an octave up (not A's source)
    dur = T + L + 0.2
    a = mix([(ang, 0, None), (gv3, -9, fade_in(0.0, 0.35))], dur)
    b = mix([(fv, 0, None), (solo, -10, fade_in(0.05, 0.4))], dur)    # no bell (user: no percussion baked in)
    b = tame_burst(b, T, over_db=4.0)                                 # Fantasy_Vox's burst sat 12 dB over its body
    return [whole_note(a, T, L, xf), whole_note(b, T, L, xf)]


# -------------------------------------------------------------------------------------------- HOARFROST --- #
def hoarfrost(rng, T=0.40, L=1.20, xf=0.30):
    """A RIME: the glassy pad already bloomed (Glassy_Pad C4 from 0.9 s, a 120 ms breath in) with ShiftedGlass's
    slow shimmer over it. B ICE RAIN: Ice_Rain's strike and its rain of glints alone (no glassy bed: A and B never share a source)."""
    R = 60
    gp_ = layer("inspiration pads/Glassy_Pad.wav", 2, R, skip=0.9)
    sg = layer("reviewed/ShiftedGlass.wav", 2, R, skip=0.9)
    ir = layer("inspiration pads/Ice_Rain.wav", 2, R)
    dur = T + L + 0.2
    a = mix([(gp_, 0, fade_in(0.0, 0.12)), (sg, -6, fade_in(0.0, 0.25))], dur)
    b = mix([(ir, 0, None)], dur)              # Ice_Rain alone: no A material in B (user heard A at B, 2026-11-06)
    return [whole_note(a, T, L, xf), whole_note(b, T, L, xf)]


# ------------------------------------------------------------------------------------------- SIDHE HARP --- #
def sidhe_harp(rng, T=0.35, xf=0.12):
    """A: the atmospheric harp (Atmos_Harp C4), its pluck into its own ring (0.5 s loop, held level).
    B: the same pluck blooming into the desert crystal's halo (DesertCrystl C4), which rings on (1.0 s loop)."""
    R = 60
    hp = layer("reviewed/Atmos_Harp.wav", 3, R)
    dc = layer("inspiration pads/DesertCrystl.wav", 3, R)
    a = mix([(hp, 0, None)], T + 0.8)
    b = mix([(dc, 0, fade_in(0.0, 0.06)), (hp, -3, gone_by(0.08, T - 0.02))], T + 1.3)
    return [whole_note(a, T, 0.5, xf), whole_note(b, T, 1.0, xf)]


# ------------------------------------------------------------------------------------------------ CHOIR --- #
def classic_choir(rng, T=0.40, L=1.10, xf=0.30):
    """The classic ROMpler choir (user, 2026-11-06: "a more classic vox/choir"), nothing added: A OOH = the dark
    ensemble 'oh' (Choir C3) thickened by a second section (Ooh_Aah2 C3); B AAH = the open choir aahs (Choir_Aahs
    C3, air to 9 kHz) with the forward Ahh section for presence."""
    R = 48
    oo = layer("reviewed/Choir.wav", 2, R)
    oo2 = layer("reviewed/Ooh_Aah2.wav", 2, R)
    aa = layer("reviewed/Choir_Aahs.wav", 1, R)
    ah = layer("reviewed/Ahh.wav", 0, R, skip=0.15)                  # Ahh rises slowly: take it from its swell
    dur = T + L + 0.2
    a = mix([(oo, 0, None), (oo2, -5, None)], dur)
    b = mix([(aa, 0, None), (ah, -7, fade_in(0.0, 0.2))], dur)
    return [whole_note(a, T, L, xf), whole_note(b, T, L, xf)]


SUST_ = (0.01, 0.5, 1.0, 0.6)
DEC_ = (0.001, 1.2, 0.0, 0.6)
SAMPLED_L = [("SPIRIT VOX", spirit_vox, 48, 0.0, None, [48, 55, 60, 63], 2.4, SUST_),
             ("HOARFROST", hoarfrost, 60, 0.0, None, [60, 67, 72, 63], 2.4, SUST_),
             ("CHOIR", classic_choir, 48, 0.0, None, [48, 55, 60, 64], 2.4, SUST_)]
# SIDHE HARP out (user, 2026-11-06: "bad")
