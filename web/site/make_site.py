#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the Carnac OS site (GitHub Pages, https://owenkirby.github.io/carnac-os/):

  index.html                    the landing page (web/site/index.html)
  carnac.css, stones.js         the FM1's look for both pages: its palette, the rune face (carnac-rune.woff, made by
  carnac-rune.woff, splash.png  web/site/make_rune_font.py) and stones; splash.png is src/carnac_splash.c, rendered
                                by src/host/render_splash.c with no version
  install/index.html            the web installer (web/site/installer.html with X0X's fm1pkg.js and fm1ota.js
                                and the package's metadata inlined; Chrome or Edge, Web MIDI)
  firmware/carnac-VER.fwsc      the package the installer writes; also the download (+ .sha256)
  play/                         the browser synth (web/emu, built by web/emu/build.sh), unless --no-emu

  python3 web/site/make_site.py build/release-0.9.2/carnac-0.9.2.fwsc 0.9.2 build/site [--no-emu] [--tip URL]

--tip URL (or $CARNAC_TIP_URL; default the Ko-fi page, TIP below; --tip "" for none) sets the "Tip me" card to the landing page and a tip line after a good install.

The package must be a release build (src/hardware/build_fm1.py --release VER): the installer checks after the
install that the FM1 answers "CARNAC OS VER", so VER must be the version the build was made with. When the
build's synth.bin sits beside the package, that string is checked in it here too."""
import argparse
import hashlib
import html
import json
import os
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_DIR = HERE.parents[1]
X0X_WEB = REPO_DIR / "x0x-firmware" / "web"
sys.path.insert(0, str(X0X_WEB))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)

REPO = "https://github.com/OwenKirby/carnac-os"
TIP = "https://ko-fi.com/orbvorgis"
LOOK_FILES = ("carnac.css", "stones.js", "carnac-rune.woff", "splash.png")   # the FM1's look, shared by both pages
EMU_FILES = ("index.html", "worklet.js", "carnac.wasm")
EMU_CARD = ('    <a class="tablet" href="play/"><strong>Play it in the browser</strong><span>The same synth engine, with sound: '
            'mouse, touch or keyboard, no FM1 needed.</span></a>\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package")
    ap.add_argument("version")
    ap.add_argument("out")
    ap.add_argument("--no-emu", action="store_true", help="leave the browser synth out")
    ap.add_argument("--tip", default=os.environ.get("CARNAC_TIP_URL", TIP), metavar="URL",
                    help=f"tip page (default {TIP}; --tip \"\" for none)")
    a = ap.parse_args()
    pkg, out, version = Path(a.package), Path(a.out), a.version
    if a.tip and not re.fullmatch(r"https://[^\s\"<>]+", a.tip):
        raise SystemExit(f"--tip {a.tip!r}: give the https:// address of the tip page")
    if not re.fullmatch(r"\d{1,2}\.\d{1,2}(\.\d{1,2})?", version):
        raise SystemExit(f"version {version!r}: use X.Y or X.Y.Z (the installer compares versions on the FM1)")
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d+", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not an X0X/Felucca-built package")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    binf = pkg.parent / "synth.bin"
    if binf.exists():
        if b"CARNAC OS " + version.upper().encode() + b"\0" not in binf.read_bytes():
            raise SystemExit(f"{binf}: no 'CARNAC OS {version}': this is not a --release {version} build")
        print(f"  ok    {binf.name} says CARNAC OS {version}")
    else:
        print(f"  note  no synth.bin beside {pkg.name}: the version string was not checked")
    sha = hashlib.sha256(raw).hexdigest()
    name = f"carnac-{version}.fwsc"

    if out.exists():                                # files out; a folder OneDrive (or a browser) holds stays, empty
        for f in sorted(out.rglob("*"), key=lambda x: len(x.parts), reverse=True):
            if f.is_dir():
                try:
                    f.rmdir()
                except OSError:
                    pass
            else:
                f.unlink()
    for d in ("install", "firmware"):
        (out / d).mkdir(parents=True, exist_ok=True)
    for f in LOOK_FILES:
        shutil.copy(HERE / f, out / f)
    shutil.copy(pkg, out / "firmware" / name)
    (out / "firmware" / (name + ".sha256")).write_text(f"{sha}  {name}\n", encoding="utf-8")

    page = (HERE / "installer.html").read_text(encoding="utf-8")
    lib = strip_module((X0X_WEB / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((X0X_WEB / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "name": name, "sha256": sha, "tip": a.tip or None,
                       "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"installer.html must contain {mark} once")
    page = page.replace("/*LIB*/", lib).replace("/*META*/", meta)
    page = page.replace("__REPO_SHORT__", REPO.removeprefix("https://")).replace("__REPO__", REPO)
    (out / "install" / "index.html").write_text(page, encoding="utf-8")

    emu = REPO_DIR / "web" / "emu"
    emu_card = ""
    if not a.no_emu:
        missing = [f for f in EMU_FILES if not (emu / f).exists()]
        if missing:
            raise SystemExit(f"web/emu: {', '.join(missing)} missing: run web/emu/build.sh, or pass --no-emu")
        (out / "play").mkdir(exist_ok=True)
        for f in EMU_FILES:
            shutil.copy(emu / f, out / "play" / f)
        emu_card = EMU_CARD
        print("  note  play/ is web/emu as built now: rebuild it from the release commit if the tree has moved on")

    landing = (HERE / "index.html").read_text(encoding="utf-8")
    tip_card = (f'    <a class="tablet" href="{html.escape(a.tip)}" target="_blank" rel="noopener"><strong>Tip me</strong>'
                '<span>Carnac OS is free. If it makes your FM1 sing, a tip keeps it going.</span></a>\n') if a.tip else ""
    landing = (landing.replace("__EMU__", emu_card).replace("__TIP__", tip_card).replace("__VERSION__", version)
               .replace("__PKG__", name).replace("__REPO__", REPO))
    (out / "index.html").write_text(landing, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), firmware/{name} ({len(raw)} B, sha256 {sha[:12]}...)"
          + (", play/" if emu_card else "") + (f", tip {a.tip}" if a.tip else ", no tip link"))


if __name__ == "__main__":
    main()
