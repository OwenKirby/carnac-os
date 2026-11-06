"""Host PCM image of the sampled waves (sampled.py SAMPLED_L) + CAIRN SPIRIT, for render_def.c: definitive_image.py's
code run on SAMPLED_L instead of the definitive list.
  python sampled_image.py <src/dsp> <out .c> [--thr -30] [--rate NAME A B]"""
import sys, pathlib
here = pathlib.Path(__file__).parent
atk = here.parent / "atkfit"
sys.path.insert(0, str(here)); sys.path.insert(0, sys.argv[1])
if "--thr" not in sys.argv:
    sys.argv += ["--thr", "-30"]
src = open(atk / "definitive_image.py").read()
src = src.replace("here = pathlib.Path(__file__).parent", f"here = pathlib.Path({str(atk)!r})")
src = src.replace('for name, fn, root, bdb, shr, tune, gate, env in ns["L_"]:',
                  'import sampled\nfor name, fn, root, bdb, shr, tune, gate, env in sampled.SAMPLED_L:')
src = src.replace("split = 10", "split = 2")
src = src.replace('TAIL_SET = {"RAINSTICK": (0, 0)}',
                  'TAIL_SET = {"RAINSTICK": (0, 0), "SIDHE HARP": (30, 10)}   # the sources own decays (the evener hides them)')
import os
if os.environ.get("ZERO_A"):              # diagnostic: side A silenced (-60 dB), to hear/measure B alone
    k = '    wf = ns["finish_pair_float"](fn(np.random.default_rng(4242)), bdb)'
    src = src.replace(k, k + '; wf[0]["atk"] = wf[0]["atk"] * 1e-3; wf[0]["loop"] = wf[0]["loop"] * 1e-3')
    assert "1e-3" in src
assert "SIDHE HARP" in src and "sampled.SAMPLED_L" in src and "split = 2" in src
exec(compile(src, "definitive_image.py (sampled)", "exec"))
