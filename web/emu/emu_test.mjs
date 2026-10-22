// SPDX-License-Identifier: GPL-3.0-only
// The browser build (web/emu/carnac.wasm) headless, as the worklet drives it: power on, the splash, a key played
// (sound, its light), FX pressed (the page changes), a knob turned, a SAVE confirmed (the flash is written), then a
// second power-on from that flash (the save is still there). Optionally writes the screens as PNGs.
//   node web/emu/emu_test.mjs [web/emu/carnac.wasm] [PNG_DIR]
import fs from "fs";
import zlib from "zlib";
import path from "path";
import { fileURLToPath } from "url";

const here = path.dirname(fileURLToPath(import.meta.url));
const wasmPath = process.argv[2] || path.join(here, "carnac.wasm");
const pngDir = process.argv[3];
const BTN = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, HOME: 6, SAVE: 7, ARP: 8, SEQ: 9, PLAY: 10, REC: 11 };
const ENC = { SELECT: 0, ALGO: 1, PRESET: 2, K1: 3 };
const wasm = fs.readFileSync(wasmPath);
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) fails++; };

async function power(flash) {
  let mem;
  const wasi = { clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; } };
  const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
  const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: stub, env: new Proxy({}, { get: () => () => 0 }) });
  const ex = instance.exports;
  mem = ex.memory;
  if (ex._initialize) ex._initialize();
  ex.web_nor_erase();
  if (flash) new Uint8Array(mem.buffer, ex.web_nor(), flash.length).set(flash);
  ex.web_master(1023);
  ex.web_boot();
  let peak = 0;
  const run = (ms) => {
    const n = Math.round(ms * 44.1);
    for (let d = 0; d < n; d += 128) {
      const k = Math.min(128, n - d);
      ex.web_render(k);
      for (const v of new Float32Array(mem.buffer, ex.web_out_l(), k)) peak = Math.max(peak, Math.abs(v));
    }
  };
  const fb = () => new Uint16Array(mem.buffer, ex.web_fb(), 240 * 240).slice();
  const tap = (b) => { ex.web_buttons(1 << b); run(60); ex.web_buttons(0); run(60); };
  const peakReset = () => { const p = peak; peak = 0; return p; };
  return { ex, mem, run, fb, tap, peakReset };
}

function png(file, px) {
  const raw = Buffer.alloc(240 * (1 + 240 * 3));
  let n = 0;
  for (let y = 0; y < 240; y++) {
    raw[n++] = 0;
    for (let x = 0; x < 240; x++) {
      const v = px[y * 240 + x], p = ((v >> 8) | (v << 8)) & 0xFFFF;
      raw[n++] = ((p >> 11) & 31) * 255 / 31; raw[n++] = ((p >> 5) & 63) * 255 / 63; raw[n++] = (p & 31) * 255 / 31;
    }
  }
  const crcT = Array.from({ length: 256 }, (_, i) => { let c = i; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
  const chunk = (t, d) => {
    const b = Buffer.concat([Buffer.from(t), d]);
    let c = 0xFFFFFFFF;
    for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8);
    const len = Buffer.alloc(4); len.writeUInt32BE(d.length);
    const crc = Buffer.alloc(4); crc.writeUInt32BE((c ^ 0xFFFFFFFF) >>> 0);
    return Buffer.concat([len, b, crc]);
  };
  const ihdr = Buffer.from([0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0]);
  fs.writeFileSync(file, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr),
                                        chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]));
}
const shot = (name, px) => { if (pngDir) { fs.mkdirSync(pngDir, { recursive: true }); png(path.join(pngDir, name + ".png"), px); } };

// ---- first power-on: a new unit
const d = await power(null);
d.run(500);
const splash = d.fb();
shot("01-splash", splash);
check(splash.some((p) => p !== 0), "the splash is drawn");
d.run(1500);
const home = d.fb();
shot("02-home", home);
check(home.some((p, i) => p !== splash[i]), "the main loop's first page replaces the splash");
check(d.ex.web_page() !== 255, `page ${d.ex.web_page()} up`);
d.peakReset();
d.ex.web_keys(1 << 7);                               // C4 at octave 0
d.run(400);
check(d.ex.web_lit_keys() & (1 << 7), "the held key is lit");
d.ex.web_keys(0);
d.run(300);
const p = d.peakReset();
check(p > 0.01 && p < 1, `a key sounds (peak ${p.toFixed(3)})`);
const pg0 = d.ex.web_page();
d.tap(BTN.FX);
d.run(100);
shot("03-fx", d.fb());
check(d.ex.web_page() !== pg0, `FX opens a page (${pg0} -> ${d.ex.web_page()})`);
check(d.ex.web_lit_buttons() & (1 << BTN.FX), "FX is lit");
d.ex.web_enc(ENC.K1, 5);
d.run(100);
shot("04-knob", d.fb());
// a SAVE: SAVE (the page), SAVE (ARE YOU SURE?), SAVE (yes); the store writes its chunk from the main loop
const w0 = d.ex.web_nor_writes();
d.tap(BTN.SAVE); d.run(200);
shot("05-save", d.fb());
d.tap(BTN.SAVE); d.run(200);
shot("06-sure", d.fb());
d.ex.web_enc(ENC.ALGO, 1); d.run(100);              // OVERWRITE? defaults to NO (a factory slot): YES
d.tap(BTN.SAVE); d.run(800);
shot("07-saved", d.fb());
const w1 = d.ex.web_nor_writes() - w0;
check(w1 > 3, `a SAVE writes the flash (${w1} writes: erase, payload pages, header)`);
// SEQ, PLAY: the transport runs and its light blinks
d.tap(BTN.SEQ); d.run(100);
d.tap(BTN.PLAY); d.run(1000);
shot("08-seq", d.fb());
check(d.ex.web_lit_buttons() & (1 << BTN.PLAY), "PLAY is lit while the sequencer plays");
const flash = new Uint8Array(d.mem.buffer, d.ex.web_nor(), d.ex.web_nor_len()).slice();

// ---- second power-on from that flash
const e = await power(flash);
e.run(2200);
shot("09-reboot", e.fb());
check(e.ex.web_nor_writes() === 0, "booting from a kept flash writes nothing (the settings are already there)");
let used = 0;
for (const b of new Uint8Array(e.mem.buffer, e.ex.web_nor(), e.ex.web_nor_len())) if (b !== 0xFF) used++;
check(used > 1000, `the kept flash holds the saved chunk (${used} bytes not erased)`);

console.log(fails ? `emu_test: ${fails} FAILED` : "emu_test: OK");
process.exit(fails ? 1 : 0);
