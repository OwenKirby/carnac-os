"""A host PCM image (area 0) of the whole-note prototype waves, HANDOFF flagged, plus MOONSONG and CAIRN SPIRIT as
they are today, for render_pads.c. Writes pcm_image_pads.c (symbols pcm_image_pads / pcm_image_pads_len) and
prints the wave codes."""
import sys, struct, zlib, pathlib, numpy as np
DSP, OUTC = sys.argv[1], pathlib.Path(sys.argv[2])
here = pathlib.Path(__file__).parent
sys.argv = ["notes.py", DSP, str(here / "_unused")]
g = {"__file__": str(here / "waves2.py")}
exec(open(here / "notes.py").read().split("TUNES = {")[0], g)
exec(open(here / "waves2.py").read().split("TUNES = {\n    \"ANGKLUNG\"")[0].split("exec(src)")[1], g)
gp, ga = g["gp"], g["ga"]

HAND = [  # (wave name, model, roots, B dB)
    ("CHOIR", g["choir"], 57, 2.0), ("PAN FLUTE", g["pan_flute"], 72, 1.0), ("HORNS", g["horns"], 48, 3.0),
    ("SINGING STN", g["singing_stone2"], 57, 2.0), ("RAINSTICK", g["rainstick"], 60, 4.0),
    ("LOG DRUM", g["log_drum"], 55, 0.0), ("ANGKLUNG", g["angklung"], 60, 0.0),
    ("WIND CHIMES", g["wind_chimes"], 60, 0.0), ("LORE", g["lore"], 60, 2.0)]
samples = []          # (name, kind, root, rate, float data)
waves = []            # (name, la, lb, atk_a, atk_b, level, flags)
for i, (name, fn, root, bdb) in enumerate(HAND):
    w = g["finish_pair"](fn(np.random.default_rng(4242)), bdb)
    tag = name[:6].strip()
    for s, ab in zip(w, "AB"):
        samples.append((f"{tag} L{ab}", 1, root, int(round(s["sr"])), s["loop"]))
        samples.append((f"{tag} K{ab}", 2, root, int(round(s["sr"])), s["atk"]))
    waves.append((name, f"{tag} LA", f"{tag} LB", f"{tag} KA", f"{tag} KB", 127, 1))
S = gp.build_samples()
for nm in ("MOON HUM", "MOON HALO", "CAIRN SPIRIT", "CAIRN HALO", "BREATH"):
    s = S[nm]
    samples.append((nm, s["kind"], s["root"], s["sr"], s["data"]))
waves.append(("MOONSONG", "MOON HUM", "MOON HALO", None, None, 0, 0))
waves.append(("CAIRN SPIRIT", "CAIRN SPIRIT", "CAIRN HALO", "BREATH", None, 40, 0))

NAME_LEN, ENTRY, HDR = 12, 36, 32
count = len(waves) + len(samples)
assert count <= 63
ref = {s[0]: len(waves) + i for i, s in enumerate(samples)}            # area 0: (0 << 6) | index
off = HDR + ENTRY * count
entries, blobs = [], []
for name, la, lb, ka, kb, lvl, fl in waves:
    entries.append(struct.pack("<12sBBBB BBBBBBHHBB8x", name.encode().ljust(NAME_LEN, b"\0"), 3, 0, 0, 0,
                               ref[la], ref[lb], ref[ka] if ka else 0xFF, 0xFF, lvl, 0, 4096, 0,
                               (ref[kb] if kb else 0xFF) if fl else 0, fl))
for name, kind, root, rate, x in samples:
    q = [int(v) & 0xFF for v in ga.mulaw_encode(np.clip(x, -1, 1))]
    if kind == 1:
        q.append(q[0])
    blob = bytes(q)
    entries.append(struct.pack("<12sBBBBIIIII", name.encode().ljust(NAME_LEN, b"\0"), kind, root, 0, 0, rate, off,
                               len(x), 0, 0))
    blobs.append(blob)
    off += len(blob)
body = b"".join(entries) + b"".join(blobs)
h = struct.pack("<IHHII12x", gp.MAGIC, gp.VERSION, count, HDR + len(body), zlib.crc32(body))
img = h + struct.pack("<I", zlib.crc32(h)) + body
lines = ["#include <stdint.h>", f"const uint32_t pcm_image_pads_len = {len(img)}u;",
         f"const uint8_t pcm_image_pads[{len(img)}] __attribute__((aligned(4))) = {{"]
for i in range(0, len(img), 32):
    lines.append("    " + ",".join(str(b) for b in img[i:i + 32]) + ",")
lines.append("};")
OUTC.write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"{len(img)} B image, {count} entries")
for i, w in enumerate(waves):
    print(f"  wave code {64 + i}: {w[0]}")
