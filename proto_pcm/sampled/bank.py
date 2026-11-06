"""The PCM bank rebuilt from the new source WAVs (2026-11-06, the user: "rebuild the whole bank from scratch so they
all sound like they came from the same process", within 242 KB). One process for every sampled wave (sampled.py /
dry.py): each layer de-reverbed from its own release, locked to the root, layered, the slow trend flattened and the
swells evened, the loop cut from the note itself with a crossfade into the pre-loop material.

NOTE definitive_image names samples by the wave name's first 7 characters: they must differ between waves.

A wave is a spec: per side a list of layers, the attack length T (the same for A and B), each side's loop length,
the stored rates. Percussive waves get their tails (dB/s) from the sources' own decay over the loop region (the
flattening hides it from definitive_image's measure): TAILS, filled as the waves are built.

AUDITION picks a batch (env BANK=batch1 ...); bank_list() gives definitive_image's L_ entries."""
import os, numpy as np
import dry, sampled
from dry import SR
from sampled import layer, mix, fade_in, gone_by, tame_burst

TAILS = {}
RATES = {}


def whole(name, side, y, T, L, xf, perc):
    """whole_note with the source decay kept for the tail"""
    Ti, xi = int(T * SR), int(xf * SR)
    _, k = dry.flatten(y, Ti - xi, Ti + int(L * SR))
    if perc:
        TAILS.setdefault(name, [0, 0])[side] = int(min(255, max(0, -k)))
    return sampled.whole_note(y, T, L, xf)


def sparsify(y, over_db=5.0, win=0.002, soft=4.0):
    """keep only the grains standing over_db above the running median (a trickle from a pour)"""
    w = int(win * SR)
    e = 20 * np.log10(np.sqrt(np.convolve(y * y, np.ones(w) / w, "same") + 1e-14))
    med = np.convolve(e, np.ones(int(0.2 * SR)) / int(0.2 * SR), "same")
    g = np.clip((e - med - over_db) / soft, 0, 1)
    g = np.convolve(g, np.ones(w) / w, "same")
    return y * g


def hp1(y, fc):
    a = np.exp(-2 * np.pi * fc / SR); out = np.zeros_like(y); s = 0.0; p = 0.0
    for i, v in enumerate(y):           # one-pole high-pass (short signals only)
        s = a * (s + v - p); p = v; out[i] = s
    return out


# ---------------------------------------------------------------------------------------------- PAN FLUTE --- #
def pan1(rng, T=0.30, L=0.70, xf=0.20):
    """A HOLLOW: the pure pan pipe (Pan Flute C5, an octave down: no formants to bend), its breath onset.
    B BREATH: the breathy, chiffy pan (Pan_Flute (1) C4), air in every note."""
    R = 60
    a = layer("reviewed/Pan Flute.wav", 0, 72, shift=-12)
    b = layer("reviewed/Pan_Flute (1).wav", 2, R)
    d = T + L + 0.2
    return [whole("PAN 1", 0, mix([(a, 0, None)], d), T, L, xf, False),
            whole("PAN 1", 1, mix([(b, 0, None)], d), T, L, xf, False)]


def pan2(rng, T=0.30, L=0.70, xf=0.20):
    """A PAN: the pan flute section (Pan_Flute2 C4). B SHAKUHACHI: the real one (Shakuhachi C4), its deep breathing
    vibrato and overblown air (the synthetic one was rejected; this is the recording)."""
    R = 60
    a = layer("reviewed/Pan_Flute2.wav", 2, R)
    b = layer("reviewed/Shakuhachi.wav", 2, R)
    d = T + L + 0.2
    return [whole("PAN 2", 0, mix([(a, 0, None)], d), T, L, xf, False),
            whole("PAN 2", 1, mix([(b, 0, None)], d), T, L, xf, False)]


def pan3(rng, T=0.30, L=0.70, xf=0.20):
    """A HOLLOW: the pure pan pipe (Pan Flute C5). B REED: the bright ethnic pipe (Ethno_Pipes1 C5), reedy, its
    tremolo breathing. Root C5 like today's PAN FLUTE."""
    R = 72
    a = layer("reviewed/Pan Flute.wav", 0, R)
    b = layer("reviewed/Ethno_Pipes1.wav", 2, R)
    d = T + L + 0.2
    return [whole("PAN 3", 0, mix([(a, 0, None)], d), T, L, xf, False),
            whole("PAN 3", 1, mix([(b, 0, None)], d), T, L, xf, False)]


# ------------------------------------------------------------------------------------------------ STRINGS --- #
def strings1(rng, T=0.40, L=0.75, xf=0.25):
    """A WARM: the soft string pad (String Pad C4). B BOWED: the string ensemble (String Ens C4), its bow bite and
    section shimmer."""
    R = 60
    a = layer("reviewed/String Pad.wav", 0, R, skip=0.3)
    b = layer("reviewed/String Ens.wav", 0, R)
    d = T + L + 0.2
    return [whole("STR PAD", 0, mix([(a, 0, fade_in(0, 0.15))], d), T, L, xf, False),
            whole("STR PAD", 1, mix([(b, 0, None)], d), T, L, xf, False)]


def strings2(rng, T=0.40, L=0.75, xf=0.25):
    """A SYNTH: the rich 90s synth strings (Syn.Strings1 C4). B BOWED: the string ensemble (String Ens C4)."""
    R = 60
    a = layer("reviewed/Syn.Strings1.wav", 2, R)
    b = layer("reviewed/String Ens.wav", 0, R)
    d = T + L + 0.2
    return [whole("STR SYN", 0, mix([(a, 0, None)], d), T, L, xf, False),
            whole("STR SYN", 1, mix([(b, 0, None)], d), T, L, xf, False)]


def strings3(rng, T=0.40, L=0.80, xf=0.25):
    """the bowed/drone role, dramatic. A BOWED: the string ensemble (String Ens C4), bow bite and section shimmer.
    B DRONE: the slow mystic pad (Mystic_Pad C4, taken bloomed), a hollow evolving drone."""
    R = 60
    a = layer("reviewed/String Ens.wav", 0, R)
    b = layer("inspiration pads/Mystic_Pad.wav", 2, R, skip=1.4)
    d = T + L + 0.2
    return [whole("STR DRONE", 0, mix([(a, 0, None)], d), T, L, xf, False),
            whole("STR DRONE", 1, mix([(b, 0, fade_in(0, 0.2))], d), T, L, xf, False)]


def strings_90s(rng, T=0.35, L=0.50, xf=0.20):
    """STRINGS (user, 2026-11-06: STR DRONE should be a rich 90s string ensemble, a blend of a synth string pad and an
    ensemble). A SYNTH: the rich synth strings (Syn.Strings1 C4) with the soft string pad (String Pad C4) blended in.
    B ENSEMBLE: the real section (String Ens C4), its bow bite, with a faint octave-up sheen of the same section."""
    R = 60
    syn = layer("reviewed/Syn.Strings1.wav", 2, R)
    pad = layer("reviewed/String Pad.wav", 0, R, skip=0.3)
    ens = layer("reviewed/String Ens.wav", 0, R)
    ens8 = layer("reviewed/String Ens.wav", 0, R, shift=12)
    d = T + L + 0.2
    a = mix([(syn, 0, None), (pad, -4, fade_in(0.0, 0.2))], d)
    b = mix([(ens, 0, None), (ens8, -12, fade_in(0.05, 0.3))], d)
    return [whole("STRINGS", 0, a, T, L, xf, False), whole("STRINGS", 1, b, T, L, xf, False)]


def strings_ens(rng, T=0.35, L=0.50, xf=0.20):
    """STR ENS, the other way round. A ENSEMBLE: the real section (String Ens C4) with the soft pad (String Pad C4)
    under it, warm. B SYNTH: the rich synth strings (Syn.Strings1 C4) with their own octave-up sheen, bright."""
    R = 60
    ens = layer("reviewed/String Ens.wav", 0, R)
    pad = layer("reviewed/String Pad.wav", 0, R, skip=0.3)
    syn = layer("reviewed/Syn.Strings1.wav", 2, R)
    syn8 = layer("reviewed/Syn.Strings1.wav", 2, R, shift=12)
    d = T + L + 0.2
    a = mix([(ens, 0, None), (pad, -5, fade_in(0.0, 0.2))], d)
    b = mix([(syn, 0, None), (syn8, -11, fade_in(0.05, 0.3))], d)
    return [whole("STR ENS", 0, a, T, L, xf, False), whole("STR ENS", 1, b, T, L, xf, False)]



def str_solstice(ns, real_b=False):
    """STR ENS after SOLSTICE (user, 2026-11-06: the ensemble wasn't warm and lush enough, its loop too short: "look
    back at SOLSTICE"). A WARM: SOLSTICE's warm pad as liked (seed 777: six detuned string players with body formants,
    a soft 'oh' choir under them, a root swell), a 1.0 s loop on its own grid. B: SOLSTICE's same players bowed
    harder (real_b False), or the real section (String Ens + String Pad, the old STR ENS A) on a 0.9 s loop (real_b)."""
    def fn(rng):
        a, b = ns["solstice_strings"](np.random.default_rng(777))
        if not real_b:
            return [a, b]
        T, L, xf = 0.40, 0.90, 0.20
        ens = layer("reviewed/String Ens.wav", 0, 60, shift=-3)        # rooted on SOLSTICE's A3
        pad = layer("reviewed/String Pad.wav", 0, 60, skip=0.3, shift=-3)
        return [a, whole("STR ENS", 1, mix([(ens, 0, None), (pad, -5, fade_in(0.0, 0.2))], T + L + 0.2), T, L, xf, False)]
    return fn

def tilt(y, db_per_oct, f0):
    """zero-phase tilt of db_per_oct per octave above f0"""
    X = np.fft.rfft(y); f = np.fft.rfftfreq(len(y), 1 / SR)
    X *= 10 ** (db_per_oct * np.log2(np.maximum(f, f0) / f0) / 20)
    return np.fft.irfft(X, len(y))


def bridge_pluck(y, root, beta=1 / 9, tilt_db=6.0):
    """a string plucked near the bridge with a nail: the pluck-position comb 1 - z^-(beta N) (the harmonics at
    multiples of 1/beta drop out, the upper ones come forward) and a tilt up of tilt_db per octave over 1 kHz"""
    N = SR / (440.0 * 2 ** ((root - 69) / 12))
    D = max(1, int(round(beta * N)))
    c = y.copy(); c[D:] -= y[:-D]
    X = np.fft.rfft(c); f = np.fft.rfftfreq(len(c), 1 / SR)
    X *= 10 ** (tilt_db * np.log2(np.maximum(f, 1000) / 1000) / 20)
    out = np.fft.irfft(X, len(c))
    return out / (sampled.rms(out[:int(0.3 * SR)]) + 1e-12) * sampled.rms(y[:int(0.3 * SR)])


def keep_bright(y, early=(0.03, 0.12), late=(0.15, 0.5), start=0.10, ramp=0.05, cap_db=15.0):
    """a bright pluck's highs die first, so its held ring goes dark: EQ the ring (zero-phase, per octave band, capped)
    back to the band balance just after the pluck, faded in over `ramp` before `start` s (the pluck itself stays)"""
    X = np.fft.rfft(y); f = np.fft.rfftfreq(len(y), 1 / SR)
    B = np.array([125, 250, 500, 1000, 2000, 4000, 8000])

    def prof(t0, t1):
        seg = y[int(t0 * SR):int(t1 * SR)]
        W = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; ff = np.fft.rfftfreq(len(seg), 1 / SR)
        e = np.array([10 * np.log10(W[(ff >= b / 1.41) & (ff < b * 1.41)].sum() + 1e-20) for b in B])
        return e - e.max()
    g_db = np.clip(prof(*early) - prof(*late), 0, cap_db)
    g = 10 ** (np.interp(np.log2(np.clip(f, B[0], B[-1])), np.log2(B), g_db) / 20)
    ye = np.fft.irfft(X * g, len(y))
    ye *= sampled.rms(y[int(late[0] * SR):int(late[1] * SR)]) / (sampled.rms(ye[int(late[0] * SR):int(late[1] * SR)]) + 1e-12)
    w = np.clip((np.arange(len(y)) / SR - (start - ramp)) / ramp, 0, 1)
    return y * (1 - w) + ye * w


def nylon(rng, T=0.22, L=0.25, xf=0.07):
    """NYLON (user, 2026-11-06: replaces MARIMBA; nylon harp, harp only). A ROUND: the harp's C5 note an octave down
    (Atmos_Harp), darker and rounder, the flesh of the finger, a long warm ring. B BRIDGE: its C3 note an octave up,
    plucked near the bridge with a nail (bridge_pluck), bright and twangy. Both hold their ring when the amp holds."""
    R = 60
    a = tilt(layer("reviewed/Atmos_Harp.wav", 4, 72, shift=-12), -5.0, 700.0)   # round: the flesh of the finger
    b = keep_bright(bridge_pluck(layer("reviewed/Atmos_Harp.wav", 2, 48, shift=12), R, tilt_db=9.0),
                    cap_db=20.0)                                             # nail near the bridge; the ring stays bright
    d = T + L + 0.2
    return [sampled.whole_note(mix([(a, 0, None)], d), T, L, xf, evn=0.3),
            sampled.whole_note(mix([(b, 0, None)], d), T, L, xf, evn=0.3)]


def nylon_fit(r):
    w = nylon(r)
    TAILS["NYLON"] = NYLON_TAILS
    return w


NYLON_TAILS = [22, 34]       # dB/s: the harp's own decay at C4 (A rounder and longer than the bridge pluck B)


# --------------------------------------------------------------------------------------------------- BELL --- #
def bell1(rng, T=0.35, L=0.70, xf=0.20):
    """the metallic sustain. A TEMPLE: the tremolo bell (Bellery C4) ringing on. B CRYSTAL: the desert crystal
    (DesertCrystl C4), dense bright partials that hold."""
    R = 60
    a = layer("reviewed/Bellery.wav", 0, R)
    b = layer("inspiration pads/DesertCrystl.wav", 3, R)
    d = T + L + 0.2
    return [whole("BELL 1", 0, mix([(a, 0, None)], d), T, L, xf, True),
            whole("BELL 1", 1, mix([(b, 0, None)], d), T, L, xf, True)]


def bell2(rng, T=0.35, L=0.70, xf=0.20):
    """A TEMPLE: the tremolo bell (Bellery C4). B TINE: the music box's steel tine (Music Box C4), pure and high."""
    R = 60
    a = layer("reviewed/Bellery.wav", 0, R)
    b = layer("reviewed/Music Box.wav", 0, R)
    d = T + L + 0.2
    return [whole("BELL 2", 0, mix([(a, 0, None)], d), T, L, xf, True),
            whole("BELL 2", 1, mix([(b, 0, None)], d), T, L, xf, True)]


# -------------------------------------------------------------------------------------------- RAINSTICK --- #
def rainstick(rng, T=0.25, LA=0.45, LB=0.60, xf=0.12):
    """doubles as a SHAKER (user). A SHAKE: one sharp burst of seeds (the pour's densest grains, brightened, 4 ms
    rise, ~70 ms fall) handing off to a sparse trickle (the pour gated to its loudest grains): a short amp envelope
    plays a shaker, a held note trickles. B RAIN: the pour itself (Rain_Stick), dense and even."""
    R = 60
    m, on, off = dry.get_note("reviewed/Rain_Stick.wav", 2)
    prof = dry.reverb_profile(m, off)
    pour = dry.dereverb(m, prof, beta=0.8)[on:]                  # unpitched: no root lock
    dense = pour[int(0.9 * SR):]
    t = np.arange(int((T + LA + 0.2) * SR)) / SR
    burst = hp1(dense[:len(t)], 1500) * np.where(t < 0.004, t / 0.004, np.exp(-(t - 0.004) / 0.07))
    trickle = sparsify(pour[int(0.6 * SR):][:len(t)], over_db=1.5, soft=6.0)   # dense enough to hold without gaps
    trickle = trickle / sampled.rms(trickle[int(0.3 * SR):]) * sampled.rms(burst[:int(0.03 * SR)]) * 10 ** (-13 / 20)
    a = burst + trickle * fade_in(0.0, 0.05)(t)
    b = pour[:int((T + LB + 0.2) * SR)]
    return [whole("RAINSTICK", 0, a, T, LA, xf, True), whole("RAINSTICK", 1, b, T, LB, xf, False)]


# ------------------------------------------------------------------------- the approved three, lean ----- #
def choir_l(rng):
    return sampled.classic_choir(rng, T=0.35, L=0.75, xf=0.25)


def spirit_l(rng):
    return sampled.spirit_vox(rng, T=0.40, L=0.75, xf=0.25)


def frost_l(rng):
    return sampled.hoarfrost(rng, T=0.35, L=0.80, xf=0.25)


# ------------------------------------------------------------------------------------------ CAIRN SPIRIT --- #
CAIRN_BREATH = 0.35            # was 0.9 (user, 2026-11-06: attack could be softer)


def cairn(rng, T=0.35):
    """CAIRN SPIRIT (gen_pcm_waves.cairn_spirit, the loved 90s spirit pad) made a whole note like the sampled waves:
    its loops untouched (periodic by construction), an attack of its own on each side: a real pan-flute breath
    (Pan_Flute (1) C4 onset, de-reverbed, high-passed to its air) over the pad swelling in, ending exactly at the
    loop's start."""
    import gen_pcm_waves as gp
    a, b, sr, root = gp.cairn_spirit(np.random.default_rng(4242) if rng is None else rng)
    br = layer("reviewed/Pan_Flute (1).wav", 2, 60)[:int(0.4 * SR)]
    br = hp1(br, 1800.0)
    br = dry.resample(br, sr / SR)
    Tn = int(T * sr); t = np.arange(Tn) / sr
    out = []
    for i, loop in enumerate((a, b)):
        n = len(loop)
        pre = np.concatenate([np.tile(loop, Tn // n + 1)])[-Tn:]                       # the Tn samples before a loop start
        swell = sampled.fade_in(0.0, T)(t) ** 0.7                    # softer (user, 2026-11-06): slower swell
        k = sampled.rms(loop) / (sampled.rms(br[:int(0.05 * sr)]) + 1e-9)
        gone = 0.5 + 0.5 * np.cos(np.pi * np.clip(t / T, 0, 1))
        bre = br[:Tn] * np.minimum(1.0, t / 0.03)                      # the breath eases in over 30 ms, no chiff
        y = np.concatenate([pre * swell + CAIRN_BREATH * k * bre * gone, loop])
        out.append((y, dry.NoteT(Tn, n, sr)))
    return out


# -------------------------------------------------------------------------------------- PERCUSSION ----- #
def marimba(rng, T=0.20, L=0.25, xf=0.06):
    """the wood bar and the tine, replacing LOG MARIMBA + LITHOPHONE. A MARIMBA: the clean rosewood bar (Marimba
    C4). B KALIMBA: the thumb piano's steel tine on its box (Kalimba C4). Both hold their ring when the amp holds."""
    R = 60
    a = layer("reviewed/Marimba.wav", 2, R)
    b = layer("reviewed/Kalimba.wav", 2, R)
    d = T + L + 0.2
    return [whole("MARIMBA", 0, mix([(a, 0, None)], d), T, L, xf, True),
            whole("MARIMBA", 1, mix([(b, 0, None)], d), T, L, xf, True)]


def glass_harp(rng, T=0.25, L=0.40, xf=0.10):
    """GLASS HARP (the user's idea). A: the music box's glassy pluck (Music Box C4). B: a struck crystal (Crystal
    C4), brighter and longer. Both hold their ring when the amp holds."""
    R = 60
    a = layer("reviewed/Music Box.wav", 0, R)
    b = layer("reviewed/Crystal.wav", 2, R)
    d = T + L + 0.2
    return [whole("GLASS HARP", 0, mix([(a, 0, None)], d), T, L, xf, True),
            whole("GLASS HARP", 1, mix([(b, 0, None)], d), T, L, xf, True)]


# ------------------------------------------------------------------------------------------------ CHIMES --- #
PENTA = [0, 2, 4, 7, 9, 12, 14, 16, 19, 21, 24]           # the chimes' tuning around the key (moves with the key)
PENTA_W = [3, 1, 2, 2, 1, 3, 1, 2, 2, 1, 2]


def scatter(hit, n, rate, rng, gain_db=(-12, 0), oct_up=0):
    """strikes of `hit` (tuned to PENTA around the root) at random times on a circular loop of n samples: the rings
    wrap, so the loop is seamless by construction"""
    loop = np.zeros(n)
    k = rng.poisson(rate * n / SR)
    w = np.array(PENTA_W, float); w /= w.sum()
    for _ in range(k):
        st = PENTA[rng.choice(len(PENTA), p=w)] + 12 * oct_up
        h = dry.resample(hit, 2 ** (-st / 12))
        g = 10 ** (rng.uniform(*gain_db) / 20)
        t0 = rng.integers(n)
        idx = (t0 + np.arange(len(h))) % n
        np.add.at(loop, idx, g * h)
    return loop


def chimes(rng, T=0.30, L=1.0):
    """LORE's role, rebuilt from real strikes (user, 2026-11-06: "nothing doing a wood/wind chime anymore"). A WOOD:
    hollow wooden chimes, short knocks (Marimba2's bar, de-reverbed, its first 0.25 s) tuned pentatonic around the
    key, ~7 a second. B METAL: temple-bell chimes (Bellery's strike and tremolo ring), ~4 a second, rings wrapping.
    The attack is a gust: the loop's own lead-in plus a cluster of strikes, faded out exactly at the handoff."""
    rng = np.random.default_rng(77)
    R = 60
    wood = layer("reviewed/Marimba2.wav", 0, R)[:int(0.25 * SR)]
    wood = wood * np.exp(-np.arange(len(wood)) / (0.10 * SR))       # rings a touch longer: no silent gaps on a 0.6 s loop
    metal = layer("reviewed/Bellery.wav", 0, R)[:int(1.2 * SR)]
    metal = metal * np.minimum(1, (len(metal) - np.arange(len(metal))) / (0.2 * SR))
    n, Tn = int(L * SR), int(T * SR)
    out = []
    for hit, rate, oct_up in ((wood, 11.0, 0), (metal, 4.0, 0)):
        loop = scatter(hit, n, rate, rng, oct_up=oct_up)
        lead = np.tile(loop, 2)[-Tn:]
        g = np.zeros(Tn)
        for t0 in rng.uniform(0, 0.12, 4):
            st = PENTA[rng.integers(len(PENTA))]
            h = dry.resample(hit, 2 ** (-st / 12))[:Tn]
            i = int(t0 * SR); g[i:i + len(h)] += 0.8 * h[:Tn - i]
        t = np.arange(Tn) / SR
        fade = 0.5 + 0.5 * np.cos(np.pi * np.clip((t - (T - 0.08)) / 0.08, 0, 1))
        y = np.concatenate([lead * np.minimum(1, t / 0.15) + g * fade, loop])
        out.append((y, dry.NoteT(Tn, n)))
    return out


def singing_stone4(ns):
    """SINGING STN, B less noisy and a bit more tonal (user, 2026-11-06): the scrape narrower (40 -> 25), roughness
    1.5 -> 0.8, grinding hiss 0.45 -> 0.2, chatter 0.65 -> 0.5; A as it was."""
    path = ns["__file__"]
    text = open(path).read()
    a = text.index("def singing_stone3(rng):"); b = text.index("\n\n\n", a)
    f = text[a:b].replace("def singing_stone3(rng):", "def singing_stone4(rng):")
    old = '("B", [1.0, 0.8, 0.6, 0.45, 0.3], 0.6, 40.0, 1.5, 0.65, 3, [1.4, 1.4, 1.2, 1.0, 0.8], 0.20, 0.45, 4)'
    assert old in f
    f = f.replace(old, '("B", [1.0, 0.8, 0.6, 0.45, 0.3], 0.6, 25.0, 0.8, 0.5, 3, [1.4, 1.4, 1.2, 1.0, 0.8], 0.20, 0.20, 4)')
    exec(compile(f, "singing_stone4", "exec"), ns)
    return ns["singing_stone4"]


SUST = (0.01, 0.5, 1.0, 0.6)
DEC = (0.001, 1.2, 0.0, 0.6)
CH = lambda r: [r, r + 7, r + 12, r + 3]
BATCHES = {
    "batch1": [("CHOIR", choir_l, 48, CH(48), SUST, (11025, 13000)),
               ("SPIRIT VOX", spirit_l, 48, CH(48), SUST, (11025, 14000)),
               ("HOARFROST", frost_l, 60, CH(60), SUST, (12000, 14000)),
               ("PAN 1", pan1, 60, [60, 64, 67, 72], SUST, (11025, 14000)),
               ("PAN 2", pan2, 60, [60, 64, 67, 72], SUST, (11025, 14000)),
               ("PAN 3", pan3, 72, [72, 76, 79, 84], SUST, (12000, 14000)),
               ("STR PAD", strings1, 60, CH(60), SUST, (11025, 13000)),
               ("STR SYN", strings2, 60, CH(60), SUST, (11025, 13000)),
               ("STR DRONE", strings3, 60, CH(60), SUST, (11025, 12000)),
               ("BELL 1", bell1, 60, [60, 67, 72, 76], DEC, (14000, 14000)),
               ("BELL 2", bell2, 60, [60, 67, 72, 76], DEC, (14000, 14000)),
               ("RAINSTICK", rainstick, 60, [60, 60, 62, 60], DEC, (12000, 14000))],
}


BATCHES["batch3"] = [
    ("SINGING STN", "SING4", None, None, None, (11025, 11025)),
    ("CAIRN NEW", cairn, 57, [57, 60, 64], SUST, (11025, 13000)),
    ("LORE", chimes, 60, [60, 67, 72], SUST, (12000, 13000)),
]
BATCHES["batch2"] = [
    ("HORNS", "SYN", None, None, None, (11025, 13000)),
    ("SINGING STN", "SYN", None, None, None, (11025, 13000)),
    ("CAIRN NEW", cairn, 57, [57, 60, 64], SUST, (11025, 13000)),
    ("MARIMBA", marimba, 60, [60, 67, 63, 72], DEC, (14000, 14000)),
    ("GLASS HARP", glass_harp, 60, [60, 67, 72, 76], DEC, (14000, 14000)),
    ("MENHIR", "SYN", None, None, None, (11025, 11025)),
    ("LOG DRUM", "SYN", None, None, None, (11025, 11025)),
    ("WOOD BLOCK", "SYN", None, None, None, (11025, 11025)),
    ("ROCK GONG", "SYN", None, None, None, (11025, 12000)),       # in doubt (user): heard again at the new rates
]


def bell_fit(r):
    """bell1 lean; its tremolo hides the decay on a 0.5 s loop: the tails measured on batch 1's 0.7 s (17 / 11 dB/s)"""
    w = bell1(r, T=0.30, L=0.50, xf=0.15)
    TAILS["BELL"] = [17, 11]
    return w


BATCHES["fit"] = [   # the whole bank, lean (2026-11-06): the user's picks at fitted lengths and rates
    ("CHOIR", lambda r: sampled.classic_choir(r, T=0.30, L=0.65, xf=0.20), 48, CH(48), SUST, (7000, 13000)),   # A ooh: -34 dB over 3.15 kHz (2026-11-06)
    ("SPIRIT VOX", lambda r: sampled.spirit_vox(r, T=0.35, L=0.65, xf=0.20), 48, CH(48), SUST, (11025, 13000)),
    ("HOARFROST", lambda r: sampled.hoarfrost(r, T=0.30, L=0.65, xf=0.20), 60, CH(60), SUST, (12000, 13000)),
    ("PAN FLUTE", lambda r: pan1(r, T=0.25, L=0.55, xf=0.18), 60, [60, 64, 67, 72], SUST, (11025, 13000)),
    ("STR ENS", "SOL", None, None, None, (8000, 10000) if os.environ.get("STRV", "1") == "1" else (8000, 12000)),   # SOLSTICE strings (user, 2026-11-06); was strings_ens
    ("BELL", lambda r: bell_fit(r), 60, [60, 67, 72, 76], DEC, (13000, 13000)),
    ("RAINSTICK", lambda r: rainstick(r, T=0.25, LA=0.40, LB=0.45, xf=0.10), 60, [60, 60, 62, 60], DEC, (12000, 12000)),   # B 12 kHz: 0.7 KB for the fit
    ("HORNS", "SYN", None, None, None, (6000, 13000)),     # A soft: 6 kHz (2026-11-06)
    ("SINGING STN", "SING4", None, None, None, (11025, 11025)),
    # CAIRN SPIRIT dropped (user, 2026-11-06, budget): rebuild it as a factory perf, SPIRIT VOX + PAN FLUTE in DUAL
    ("LORE", lambda r: chimes(r, T=0.25, L=0.60), 60, [60, 67, 72], SUST, (11025, 12000)),
    ("ROCK GONG", "SYN", None, None, None, (10000, 12000)),
    # NYLON out (user, 2026-11-06: "the loop is too obvious"); its 11 KB went to longer CHOIR/SPIRIT VOX/HOARFROST loops
    # GLASS HARP dropped (user, 2026-11-06, budget): overlapped BELL's crystal B and HOARFROST
    ("MENHIR", "SYN", None, None, None, (11025, 11025)),
    ("LOG DRUM", "SYN", None, None, None, (11025, 11025)),
    ("WOOD BLOCK", "SYN", None, None, None, (11025, 11025)),
]


def bank_list(L_def=(), batch=None, ns=None):
    """definitive_image's L_ entries for a batch; "SYN" entries are the synthetic waves taken from definitive.L_
    (their own models, tunes and drum shrink settings), only their rates set here"""
    syn = {e[0]: e for e in L_def}
    if (batch or os.environ.get("BANK")) == "fit":
        dry.SEARCH = 0.03                     # loops stay near their asked length
    out = []
    for name, fn, root, tune, env, rates in BATCHES[batch or os.environ.get("BANK", "batch1")]:
        RATES[name] = rates
        if fn == "SYN":
            out.append(syn[name])
        elif fn == "SOL":
            e = syn["SOLSTICE"]
            out.append((name, str_solstice(ns, os.environ.get("STRV", "1") == "2")) + tuple(e[2:4]) + (None,) + tuple(e[5:]))
        elif fn == "SING4":
            e = syn["SINGING STN"]
            out.append((name,) + (singing_stone4(ns),) + tuple(e[2:]))
        else:
            out.append((name, fn, root, 0.0, None, tune, 2.4 if env is SUST else 0.5, env))
    return out
