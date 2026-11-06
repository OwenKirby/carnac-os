"""Host PCM images (areas 0 and 1) of the definitive list as definitive.py builds it, for render_def.c: the whole-note
waves HANDOFF flagged at the rule's rates, STAR DUST classic, MOONSONG and CAIRN SPIRIT as today. Writes
src/host/pcm_image_def.c: pcm_image_def0/1 (+ _len) and DEF_TUNES, the audition phrase of each wave in order.
  python definitive_image.py <src/dsp> <out .c> [--thr -25]"""
import sys, struct, zlib, pathlib
here = pathlib.Path(__file__).parent
DSP, OUTC = sys.argv[1], pathlib.Path(sys.argv[2])
THR = float(sys.argv[sys.argv.index("--thr") + 1]) if "--thr" in sys.argv else -25.0
MATCH = "--match" in sys.argv                        # sustained: the attack rises to meet its loop
TAIL = "--notail" not in sys.argv                    # percussive waves: the loop decays on at the attack's rate
KEEP_HIGH = {"SINGING STN"}                         # the user: too dull at the lower rate (2026-11-05): today's rule
# per-wave (A Hz, B Hz) set by ear, one by one (2026-11-05); "--rate NAME A B" adds or overrides one
RATE_SET = {"SOLSTICE": (8000, 10000)}            # 6k/7k dull + a loop click at B, but it had character: 8k/10k
RATE_SET.update({"PAN FLUTE": (8000, 16000),        # 2026-11-05 tweaks: B airy at 16 kHz (user)
                 "LOG DRUM": (11025, 11025), "WOOD BLOCK": (11025, 11025), "LOG MARIMBA": (11025, 11025),  # rate bumps
                 "MENHIR SPLIT": (6000, 11025), "MENHIR ANTLR": (6000, 11025), "MENHIR EARTH": (6000, 8000)})
# the drums' ring (their loop) louder, so a held note sustains (user, 2026-11-05: "serve double duty as sustained
# tones"); x2 = +6 dB, WOOD BLOCK x3 (its ring sat 20 dB under the strike). Capped at 0.9 of each partial's strike.
RING_SET = {"LOG DRUM": 2.0, "LITHOPHONE": 2.0, "MENHIR": 2.0, "WOOD BLOCK": 3.0, "LOG MARIMBA": 2.0}
TAIL_SET = {"RAINSTICK": (0, 0)}                  # held, both loops go on (a sparse attack measures as a fast decay)
for _i, _a in enumerate(sys.argv):
    if _a == "--rate":
        RATE_SET[sys.argv[_i + 1]] = (float(sys.argv[_i + 2]), float(sys.argv[_i + 3]))
sys.argv = [sys.argv[0], DSP, str(here / "_unused")]
ns = {"__file__": str(here / "definitive.py")}
exec(compile(open(here / "definitive.py").read().split('\nif __name__ == "__main__":')[0], "definitive.py", "exec"), ns)
np, gp, ga = ns["np"], ns["gp"], ns["ga"]

waves = []                  # dict(name, root, tune, gate, perc, samples=[(tag, kind, rate, data)], la, lb, ka, kb, lvl, fl)
for name, fn, root, bdb, shr, tune, gate, env in ns["L_"]:
    if shr:
        ns["SHRINK"].update(sr=shr[0], sr_bright=shr[0], L=shr[1], T=0.20)
    ns["RING_GAIN"] = RING_SET.get(name, 1.0)
    wf = ns["finish_pair_float"](fn(np.random.default_rng(4242)), bdb)
    if MATCH and env[2] != 0.0:    # sustained: the attack's last 150 ms rises to its loop's level
        for x in wf:
            a, n = x["atk"], int(0.02 * x["sr"])
            r = lambda v: np.sqrt(np.mean(v ** 2)) + 1e-12
            g = r(x["loop"][:n]) / r(a[-n:])
            if g > 10 ** (1.5 / 20):
                m = int(0.15 * x["sr"])
                a = a.copy(); a[-m:] *= 1 + (g - 1) * (0.5 - 0.5 * np.cos(np.pi * np.arange(m) / m))
                x["atk"] = a
                print(f"   {name} level match +{20 * np.log10(g):.1f} dB")
    thr = -35.0 if name in KEEP_HIGH else THR
    rs = RATE_SET.get(name) or [ns["rate_for"](s, thr) for s in wf]
    w = [ns["mu"](ns["rerate"](s, min(r, s["sr"]))) for s, r in zip(wf, rs)]
    print(f"{name:12s} A {w[0]['sr']:6.0f}  B {w[1]['sr']:6.0f}  {sum(len(x['atk']) + len(x['loop']) + 1 for x in w) / 1024:5.1f} KB", flush=True)
    t = name[:7].strip()
    tails = [0, 0]
    if TAIL and env[2] == 0.0:                      # the attack's own decay over its last 100 ms, dB/s
        for i, x in enumerate(wf):
            a, n, m = x["atk"], int(0.02 * x["sr"]), int(0.1 * x["sr"])
            r = lambda v: np.sqrt(np.mean(v ** 2)) + 1e-12
            tails[i] = int(min(255, max(0, -20 * np.log10(r(a[-n:]) / r(a[-m:-m + n])) / 0.08)))
        tails = list(TAIL_SET.get(name, tails))
        print(f"   {name} tail {tails[0]} / {tails[1]} dB/s")
    smp = []
    for s, ab in zip(w, "AB"):
        smp.append((f"{t} L{ab}", 1, root, s["sr"], s["loop"]))
        smp.append((f"{t} K{ab}", 2, root, s["sr"], s["atk"]))
    waves.append(dict(name=name, root=root, tune=tune, gate=gate, perc=env[2] == 0.0, samples=smp,
                      la=f"{t} LA", lb=f"{t} LB", ka=f"{t} KA", kb=f"{t} KB", lvl=127, fl=1, tails=tails))
# STAR DUST: a whole-note wave in L_ since 2026-11-05 (star_dust2), no longer the classic one
S = gp.build_samples()
for name, a, b, k, lvl, tune in (("CAIRN SPIRIT", "CAIRN SPIRIT", "CAIRN HALO", "BREATH", 40, [57, 60, 64]),):
    # MOONSONG out of the list (user, 2026-11-05: liked, but flash goes to the basics; bring back if room)
    smp = [(nm, S[nm]["kind"], S[nm]["root"], S[nm]["sr"], S[nm]["data"]) for nm in (a, b) + ((k,) if k else ())]
    waves.append(dict(name=name, root=S[a]["root"], tune=tune, gate=2.4, perc=False, samples=smp, la=a, lb=b, ka=k,
                      kb=None, lvl=lvl, fl=0))


def pack(ws, area):
    samples = [s for w in ws for s in w["samples"]]
    count = len(ws) + len(samples)
    assert count <= 63, count
    ref = {s[0]: (area << 6) | (len(ws) + i) for i, s in enumerate(samples)}
    off = 32 + 36 * count
    entries, blobs = [], []
    for w in ws:
        tl = w.get("tails", (0, 0))
        entries.append(struct.pack("<12sBBBB BBBBBBHHBBBB6x", w["name"].encode()[:12].ljust(12, b"\0"), 3, 0, 0, 0,
                                   ref[w["la"]], ref[w["lb"]], ref[w["ka"]] if w["ka"] else 0xFF, 0xFF, w["lvl"], 0,
                                   4096, 0, (ref[w["kb"]] if w["kb"] else 0xFF) if w["fl"] else 0, w["fl"],
                                   tl[0], tl[1]))
    for name, kind, root, rate, x in samples:
        q = [int(v) & 0xFF for v in ga.mulaw_encode(np.clip(x, -1, 1))]
        if kind == 1:
            q.append(q[0])
        entries.append(struct.pack("<12sBBBBIIIII", name.encode()[:12].ljust(12, b"\0"), kind, root, 0, 0,
                                   int(round(rate)), off, len(x), 0, 0))
        blobs.append(bytes(q)); off += len(q)
    body = b"".join(entries) + b"".join(blobs)
    h = struct.pack("<IHHII12x", gp.MAGIC, gp.VERSION, count, 32 + len(body), zlib.crc32(body))
    return h + struct.pack("<I", zlib.crc32(h)) + body


split = 10
NCL = 1                                                     # the classic waves at the end (CAIRN SPIRIT)
imgs = [pack(waves[:split] + waves[-NCL:], 0), pack(waves[split:-NCL], 1)]
order = waves[:split] + waves[-NCL:] + waves[split:-NCL]           # the wave list: area 0's, then area 1's
lines = ["/* Generated by proto_pcm/atkfit/definitive_image.py: do not edit (prototype scaffolding). */",
         "#include <stdint.h>"]
for a, img in enumerate(imgs):
    lines += [f"const uint32_t pcm_image_def{a}_len = {len(img)}u;",
              f"const uint8_t pcm_image_def{a}[{len(img)}] __attribute__((aligned(4))) = {{"]
    lines += ["    " + ",".join(str(b) for b in img[i:i + 32]) + "," for i in range(0, len(img), 32)]
    lines.append("};")
lines.append("const struct { const char *name; uint8_t perc; uint16_t gate_ms; uint8_t n; uint8_t notes[8]; } DEF_TUNES[] = {")
for w in order:
    lines.append(f'    {{"{w["name"]}", {int(w["perc"])}, {int(w["gate"] * 1000)}, {len(w["tune"])}, '
                 f'{{{", ".join(str(m) for m in w["tune"])}}}}},')
lines.append("};")
lines.append(f"const uint32_t DEF_N = {len(order)}u;")
OUTC.write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"images {len(imgs[0])} + {len(imgs[1])} B; {len(order)} waves:", ", ".join(w["name"] for w in order))
