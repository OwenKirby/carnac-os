"""Host PCM image of a bank batch (bank.py, env BANK) + CAIRN SPIRIT, for render_def.c: definitive_image.py's code
on bank.bank_list(), each wave at its spec's rates, percussive tails from the sources' own decays.
  BANK=batch1 python bank_image.py <src/dsp> <out .c>"""
import sys, os, pathlib
here = pathlib.Path(__file__).parent
atk = here.parent / "atkfit"
sys.path.insert(0, str(here)); sys.path.insert(0, sys.argv[1])
src = open(atk / "definitive_image.py").read()
src = src.replace("here = pathlib.Path(__file__).parent", f"here = pathlib.Path({str(atk)!r})")
src = src.replace('for name, fn, root, bdb, shr, tune, gate, env in ns["L_"]:',
                  'import bank\n_L = bank.bank_list(ns["L_"], ns=ns)\nRATE_SET = bank.RATES\nTAIL_SET = bank.TAILS\n'
                  'for name, fn, root, bdb, shr, tune, gate, env in _L:')
src = src.replace("split = 10", "split = int(os.environ.get('SPLIT', 0)) or (len(waves) - 1) // 2")
k = "    t = name[:7].strip()\n"
assert k in src
src = src.replace(k, "    LOUD[name] = [ns['k_loud'](x['loop'], x['sr']) for x in w]\n"     # held loudness per side
                  "    _g = min(1.0, 10 ** ((LOUD_CEIL - max(LOUD[name])) / 20))\n"   # the loudest down to the ceiling
                  "    w = [dict(x, atk=x['atk'] * _g, loop=x['loop'] * _g) for x in w]\n"
                  "    LOUD[name] = [v + 20 * np.log10(_g) for v in LOUD[name]]\n" + k, 1)
LOUD_CEIL = -13.0           # K dB: voices/pan sat ~2 dB over the pads; the struck waves can't come up (peak-bound)
LOUD = {}
assert "bank.bank_list" in src and "SPLIT" in src
exec(compile(src, "definitive_image.py (bank)", "exec"))
print("held loudness (K, dB) A / B:")
for k_, v in LOUD.items():
    print(f"   {k_:12s} {v[0]:6.1f} {v[1]:6.1f}")
