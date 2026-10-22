#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the GitHub Pages site for release VERSION (web/site/make_site.py) and push it to the gh-pages branch of
# origin, replacing what was there (the branch holds only the generated site). X0X's tools/publish_pages.sh, for Carnac.
#   sh web/site/publish_site.sh 0.9.2        (after src/hardware/build_fm1.py --release 0.9.2 --out build/release-0.9.2
#                                             and copying synth.fwsc to carnac-0.9.2.fwsc there)
# The tip link is https://ko-fi.com/orbvorgis (make_site.py TIP); CARNAC_TIP_URL=... overrides it
# Once: on GitHub, Settings > Pages > Source: Deploy from a branch, gh-pages, / (root).
set -e
cd "$(dirname "$0")/../.."
V="${1:?usage: web/site/publish_site.sh VERSION}"
PKG="build/release-$V/carnac-$V.fwsc"
[ -f "$PKG" ] || { echo "no $PKG: make the release build first"; exit 1; }
# python3, or python on Windows (where python3 can be the Microsoft Store placeholder)
PY=python3
$PY -c "" 2>/dev/null || PY=python
$PY web/site/make_site.py "$PKG" "$V" build/site
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
cd build/site
rm -rf .git
git init -q -b gh-pages
git add -A
git commit -q -m "Site for Carnac OS $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
echo "pushed the site for $V to gh-pages: https://owenkirby.github.io/carnac-os/"
