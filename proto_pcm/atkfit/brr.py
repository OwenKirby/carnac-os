"""Block-adaptive 4-bit (BRR-style) codec experiment for the PCM store, against today's 8-bit mu-law.

Block: 16 samples = 1 header byte (shift 0..12, filter 0..3) + 8 bytes of 4-bit residuals = 9 bytes (4.5 bit/sample,
0.5625 of mu-law). Decoded s = clamp16(p + (n << shift)), p from the last two decoded samples:
  f0: 0   f1: s1*15/16   f2: s1*61/32 - s2*15/16   f3: s1*115/64 - s2*13/16
Loops: length a multiple of 16 (resampled to it), the decoder's history carried round the wrap (encoded until the
history at the end is the history the start was encoded with). Keyframes: every KEY blocks a filter-0 block (no
history needed) so a playhead can start there (free-running loops, B's offset, SPREAD's playheads)."""
import numpy as np

FILT = [(0.0, 0.0), (15 / 16, 0.0), (61 / 32, -15 / 16), (115 / 64, -13 / 16)]
KEY = 16


def enc_block(x, h1, h2, filters):
    """x: 16 targets (int16 scale). Best (err, shift, filter, nibbles, decoded) over every shift x filter, each
    quantised sample by sample (its prediction from the decoded ones)."""
    cands = [(s, f) for f in filters for s in range(13)]
    S = np.array([c[0] for c in cands]); F = np.array([c[1] for c in cands])
    a1 = np.array([FILT[f][0] for f in F]); a2 = np.array([FILT[f][1] for f in F])
    s1 = np.full(len(cands), float(h1)); s2 = np.full(len(cands), float(h2))
    sc = 2.0 ** S
    err = np.zeros(len(cands)); nib = np.zeros((len(cands), 16), int); dec = np.zeros((len(cands), 16))
    for i in range(16):
        p = np.floor(a1 * s1 + a2 * s2)
        n = np.clip(np.rint((x[i] - p) / sc), -8, 7)
        d = np.clip(p + n * sc, -32768, 32767)
        err += (d - x[i]) ** 2
        nib[:, i] = n; dec[:, i] = d
        s2, s1 = s1, d
    k = int(np.argmin(err))
    return err[k], S[k], F[k], nib[k], dec[k]


def encode(x, loop):
    """x: float -1..1 (a loop's length a multiple of 16). -> decoded float, bytes."""
    xi = np.clip(np.rint(x * 32767.0), -32768, 32767)
    nb = len(xi) // 16
    h1 = h2 = 0.0
    for _ in range(3 if loop else 1):
        out = np.zeros(nb * 16)
        a, b = h1, h2
        for j in range(nb):
            fl = [0] if j % KEY == 0 else [0, 1, 2, 3]
            _, s, f, n, d = enc_block(xi[j * 16:(j + 1) * 16], a, b, fl)
            out[j * 16:(j + 1) * 16] = d
            a, b = d[-1], d[-2]
        if not loop or (a == h1 and b == h2):
            break
        h1, h2 = a, b
    return out / 32767.0, nb * 9


def pad16_loop(x, sr):
    import gen_pcm_waves as gp
    n = len(x); m = int(round(n / 16)) * 16
    if m == n:
        return x, sr
    spec = np.fft.rfft(x)
    y = np.fft.irfft(spec[:m // 2 + 1] if m < n else np.concatenate([spec, np.zeros(m // 2 + 1 - len(spec))]), n=m) * (m / n)
    return y, sr * m / n


def pad16_once(x):
    return np.concatenate([x, np.zeros((-len(x)) % 16)])
