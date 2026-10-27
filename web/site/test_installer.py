#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The web installer against a simulated FM1, in headless Edge or Chrome (no hardware):

  python3 web/site/test_installer.py build/site

A copy of install/index.html gets a fake Web MIDI (X0X's FakeFM1 from x0x-firmware/web/test_web.mjs, plus Carnac's
editor INFO answer) and a driver that presses the buttons; each case is one page load, served from a local HTTP
server so the page fetches the real package. Cases: an update from an older Carnac, a test build and a newer
Carnac (both need the tick), stock firmware, an FM1 left in update mode, and no FM1; the waves (PCM areas 0 and 1,
wavetable bank 0 in area 2) after each good install, alone when the right version lacks them, not again when the FM1
holds them, and again when area 2 holds another image of the same length (found by PCM_ID's header CRC)."""
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

BROWSERS = [r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            r"C:\Program Files\Google\Chrome\Application\chrome.exe",
            "/usr/bin/chromium", "/usr/bin/google-chrome"]

MOCK = r"""<script>
(() => {
const P = new URLSearchParams(location.search);
const CASE = P.get("case");
const pack7 = (data) => { const o = []; let a = 0, n = 0; for (const b of data) { a |= b << n; n += 8; while (n >= 7) { o.push(a & 0x7F); a >>>= 7; n -= 7; } } if (n) o.push(a & 0x7F); return o; };
const unpack7 = (s) => { const o = []; let a = 0, n = 0; for (const b of s) { a |= (b & 0x7F) << n; n += 7; while (n >= 8) { o.push(a & 0xFF); a >>>= 8; n -= 8; } } return o; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7], UP = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const INFO = [0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7];
const T = window.__t = { bad: 0, served: 0, written: false, log: [], pcmFrames: 0 };
const START = { older: ["FM-1_900", "0.9.0"], dev: ["FM-1_900", "DEV"], newer: ["FM-1_900", "9.9.9"], same: ["FM-1_900", P.get("after")],
                stock: ["FM-1_015", null], loader: ["ota-FM-1_900", null], none: [null, null] }[CASE];
const access = { inputs: new Map(), outputs: new Map() };
let input, output, identity, carnac, waiting = null, queue = [];
function boot(id, ver, name) {
  for (const m of [access.inputs, access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
  identity = id; carnac = ver; waiting = null; queue = [];
  if (!id) return;
  const k = Math.random().toString(36).slice(2);
  input = { id: "i" + k, name, state: "connected", onmidimessage: null, open: async () => {} };
  output = { id: "o" + k, name, state: "connected", open: async () => {}, send: (d) => {
    if (output.state !== "connected") throw new Error("InvalidStateError");
    setTimeout(() => rx(Array.from(d)), 1); } };
  access.inputs.set(input.id, input); access.outputs.set(output.id, output);
}
function tx(b) { const i = input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(b) }); }, 1); }
function next() {
  const r = queue.shift(); if (!r) return; waiting = r;
  const [addr, len] = r;
  const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
  let s = 0; for (let i = 6; i < 14; i++) s += u[i]; u.push(~s & 0xFF);
  tx([0xF0, ...pack7(u), 0xF7]);
}
let image = null;
/* the PCM areas (pcm_store_fm1.c), kept across restarts like the flash */
const PCMSIZE = { 0: 0x9000, 1: 0xE000, 2: 0x6000 }, area = T.areas = { 0: null, 1: null, 2: null };
let up = null;
const unp = (s) => { const o = []; for (let i = 0; i < s.length; i += 8) for (let j = 0; j < 7 && i + 1 + j < s.length; j++) o.push(s[i + 1 + j] | (((s[i] >> j) & 1) << 7)); return o; };
function pcm(cmd, a) {
  const R = (...b) => tx([0xF0, 0x7D, 0x46, 0x4C, cmd, ...b, 0xF7]);
  if (cmd !== 103 && cmd !== 104) T.pcmFrames++;
  if (cmd === 100) { up = { area: a[0], buf: new Uint8Array(PCMSIZE[a[0]]), next: 32 }; area[a[0]] = null; R(a[0], 0); }
  else if (cmd === 101) {
    const off = a[0] | a[1] << 7 | a[2] << 14, b = unp(a.slice(3));
    let rc = 0;
    if (!up || off !== up.next) rc = 3; else { up.buf.set(b, off); up.next += b.length; }
    R(a[0], a[1], a[2], rc);
  } else if (cmd === 102) {
    const h = unp(a);
    let rc = 3;
    if (up && h.length === 32) { up.buf.set(h, 0); if (new DataView(up.buf.buffer).getUint32(8, true) === up.next) { area[up.area] = up.buf.slice(0, up.next); rc = 0; } }
    up = null;
    R(rc, 0);
  } else if (cmd === 103) {
    const img = area[a[0]], n = img ? img[6] | img[7] << 8 : 0, used = img ? img.length : 0, kib = PCMSIZE[a[0]] / 1024;
    R(3, a[0], n, kib & 0x7F, kib >> 7, used & 0x7F, (used >> 7) & 0x7F, (used >> 14) & 0x7F);
  } else if (cmd === 104) {
    const img = area[a[0]], h = img ? new DataView(img.buffer, img.byteOffset).getUint32(28, true) : 0;
    R(a[0], img ? 1 : 0, h & 0x7F, (h >>> 7) & 0x7F, (h >>> 14) & 0x7F, (h >>> 21) & 0x7F, h >>> 28);
  }
}
window.__setPcm = (k, img) => { area[k] = img.slice(); };
function rx(d) {
  if (eq(d, HS)) {
    const t = [...new TextEncoder().encode(identity)];
    tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
  } else if (eq(d, INFO)) {
    if (carnac !== null) tx([0xF0, 0x7D, 0x46, 0x4C, 0x01, ...new TextEncoder().encode("CARNAC OS " + carnac), 0, 0, 0, 0, 16, 0, 0, 2, 0, 0xF7]);
  } else if (eq(d, UP)) {
    queue = identity.startsWith("ota-")
      ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [image.length - 100, 100], [0xF0000000, 8]]
      : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
    next();
  } else if (d[1] === 0x7D && d[2] === 0x46 && d[3] === 0x4C && d[4] >= 100 && d[4] <= 104) {
    if (carnac !== null) pcm(d[4], d.slice(5, -1));
  } else if (waiting) {
    const u = unpack7(d.slice(1, -1)), [addr, len] = waiting;
    const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
    const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(image.subarray(addr, addr + len));
    if (!eq(got, want)) T.bad++;
    waiting = null; T.served++;
    if (addr === 0xE0000000) setTimeout(() => boot("ota-FM-1_900", null, "Felucca Update"), 300);
    else if (addr === 0xF0000000) { T.written = true; setTimeout(() => boot("FM-1_900", P.get("after") || "0.0.0", "FM-1"), 300); }
    else next();
  }
}
boot(START[0], START[1], START[0] && START[0].startsWith("ota-") ? "Felucca Update" : "FM-1");
navigator.requestMIDIAccess = async () => access;
window.__setImage = (img) => { image = img; };
})();
</script>
"""

DRIVER = r"""<script type="module">
const $ = (id) => document.getElementById(id);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const until = async (f, ms = 20000) => { const end = Date.now() + ms; while (!f()) { if (Date.now() > end) return false; await sleep(50); } return true; };
const out = (o) => { const p = document.createElement("pre"); p.id = "RESULT"; p.textContent = "RESULT " + JSON.stringify(o) + " END"; document.body.append(p); };
(async () => {
  const m = JSON.parse(document.querySelector("script[data-meta]").textContent);
  const raw = new Uint8Array(await (await fetch(m.pkg)).arrayBuffer());
  const img = new Uint8Array(raw.length - 20);                     /* fm1pkg.js logicalImage */
  for (let i = 0; i < 20; i++) img.set(raw.subarray(i * 0x30, i * 0x30 + 0x2F), i * 0x2F);
  img.set(raw.subarray(20 * 0x30), 20 * 0x2F);
  window.__setImage(img);
  const pcm = {};
  for (const a of m.pcm) pcm[a.area] = new Uint8Array(await (await fetch(a.file)).arrayBuffer());
  const pcmQ = new URLSearchParams(location.search).get("pcm");
  if (pcmQ === "ok" || pcmQ === "old2") for (const k in pcm) window.__setPcm(+k, pcm[k]);
  if (pcmQ === "old2") { const o = pcm[2].slice(); o[28] ^= 1; window.__setPcm(2, o); }   /* same length, another image */
  if (!(await until(() => !$("find").disabled))) return out({ err: "find never enabled", status: $("status").textContent });
  $("find").click();
  await until(() => $("device").textContent || $("status").className === "bad");
  await sleep(200);
  const r = { device: $("device").textContent, confirm: $("confirmbox").classList.contains("show"),
              goDisabled: $("go").disabled, goText: $("go").textContent, confirmText: $("confirmtext").textContent };
  if (r.confirm) { $("confirm").click(); await sleep(50); r.goAfterTick = !$("go").disabled; }
  if (!$("go").disabled) {
    $("go").click();
    await until(() => !$("find").disabled && $("bar").value > 0 || $("status").className === "bad", 120000);
    await until(() => !$("find").disabled, 120000);
  }
  Object.assign(r, { tipShown: !$("tip").hidden, tipWanted: !!m.tip, status: $("status").textContent, bad: $("status").className === "bad", bar: $("bar").value,
                     served: window.__t.served, wrongBytes: window.__t.bad, written: window.__t.written,
                     pcmFrames: window.__t.pcmFrames,
                     pcmOk: Object.keys(pcm).every((k) => { const g = window.__t.areas[k]; return !!g && g.length === pcm[k].length && g.every((x, i) => x === pcm[k][i]); }) });
  out(r);
})().catch((e) => out({ err: String(e && e.stack || e) }));
</script>
"""

CASES = {   # case -> (query, check)
    "older Carnac -> this one": ("case=older&after={v}", lambda r, v: r["device"].endswith("Carnac OS 0.9.0.") and not r["confirm"]
                       and r["tipShown"] == r["tipWanted"]
                       and r["written"] and r["wrongBytes"] == 0 and r["pcmOk"]
                       and r["status"] == f"Done. The FM1 runs Carnac OS {v}, with its waves." and r["bar"] == 100),
    "test build needs the tick": ("case=dev&after={v}", lambda r, v: r["confirm"] and r["goDisabled"] and r["goAfterTick"]
                                  and r["written"] and not r["bad"]),
    "newer on the FM1 needs the tick": ("case=newer&after={v}", lambda r, v: r["confirm"] and r["goDisabled"] and r["goAfterTick"]
                                        and "older than the 9.9.9" in r["confirmText"] and r["written"] and not r["bad"]),
    "stock firmware -> Carnac": ("case=stock&after={v}", lambda r, v: "other firmware (FM-1_015)" in r["device"] and not r["confirm"]
                                 and r["written"] and r["status"].startswith("Done.")),
    "left in update mode -> finish": ("case=loader&after={v}", lambda r, v: "update mode" in r["device"] and r["goText"].startswith("Finish")
                                      and r["written"] and r["wrongBytes"] == 0 and r["pcmOk"] and r["status"].startswith("Done.")),
    "same version, PCM waves missing": ("case=same&after={v}", lambda r, v: "waves are missing" in r["device"]
                                        and r["goText"] == "Write the waves" and not r["written"] and r["pcmOk"]
                                        and r["status"].startswith("Done.") and not r["bad"]),
    "same version, PCM waves there": ("case=same&after={v}&pcm=ok", lambda r, v: "already runs" in r["device"] and r["written"]
                                      and r["pcmOk"] and r["pcmFrames"] == 0 and r["status"].startswith("Done.")),
    "same version, Foundations out of date": ("case=same&after={v}&pcm=old2", lambda r, v: "waves are missing" in r["device"]
                                              and r["goText"] == "Write the waves" and not r["written"] and r["pcmOk"]
                                              and r["pcmFrames"] == 66 and r["status"].startswith("Done.")),
    "wrong version after install": ("case=older&after=0.9.0", lambda r, v: r["written"] and r["bad"] and not r["tipShown"]
                                    and "reports Carnac OS 0.9.0" in r["status"] and r["pcmFrames"] == 0),
    "no FM1": ("case=none", lambda r, v: r["bad"] and r["status"].startswith("FM1 not found") and not r["written"]),
}


def main(site):
    site = Path(site).resolve()
    browser = next((b for b in BROWSERS if os.path.exists(b)), None)
    if not browser:
        sys.exit("no Edge or Chrome found")
    page = (site / "install" / "index.html").read_text(encoding="utf-8")
    meta = re.search(r"const meta = (\{.*?\});", page)
    version = re.search(r'"version": "([^"]+)"', meta[1])[1]
    test = page.replace("<head>", "<head>\n" + MOCK, 1).replace(
        "</body>", f'<script type="application/json" data-meta>{meta[1].replace("../firmware/", "../firmware/")}</script>\n'
        + DRIVER + "</body>", 1)
    tdir = site / "install-test"
    tdir.mkdir(exist_ok=True)
    (tdir / "index.html").write_text(test, encoding="utf-8")
    class Quiet(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *a):
            pass
    handler = functools.partial(Quiet, directory=str(site))
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    port = srv.server_address[1]
    profile = tempfile.mkdtemp(prefix="carnac-installer-test-")
    failed = 0
    try:
        for name, (query, check) in CASES.items():
            url = f"http://127.0.0.1:{port}/install-test/?{query.format(v=version)}"
            p = subprocess.run([browser, "--headless=new", "--disable-gpu", "--no-first-run",
                                f"--user-data-dir={profile}", "--virtual-time-budget=240000",
                                "--dump-dom", url], capture_output=True, text=True, encoding="utf-8", timeout=300)
            m = re.search(r"RESULT (\{.*?\}) END", p.stdout, re.S)
            r = json.loads(m[1].replace("&quot;", '"').replace("&amp;", "&")) if m else {"err": "no result"}
            good = "err" not in r and bool(check(r, version))
            failed += not good
            print(f"{name:40} {'ok' if good else 'FAIL'}")
            if not good:
                print("   ", r)
    finally:
        srv.shutdown()
        shutil.rmtree(tdir, ignore_errors=True)
        shutil.rmtree(profile, ignore_errors=True)
    print("installer tests FAILED (%d)" % failed if failed else "installer tests passed")
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
