/* SPDX-License-Identifier: GPL-3.0-only
   Carnac editor: the pictures, drawn as the FM1's screen draws them, pixel by pixel into small canvases that the CSS
   scales up by whole pixels (image-rendering: pixelated). Colours come from the page's THEME tokens (style.css, the
   SQ_PAL tables of src/ui_seq_draw.c), read again whenever the THEME changes.

     dial        a knob: the value is a moon on an arch over two stones (the panel's parameter popup): the left horizon
                 is 0, overhead the middle, the right horizon the top; the moon waxes on the way up and wanes after
     osc         the bank's 8 frames as a row of stones, the scan's place between them lit (OSC1 / OSC2 pages)
     mix         the mixer's voices as menhirs, each as tall as its level (OSC MIX)
     noise       a fog of grains, its colour the grain size, its level the density
     filter      the response of the flavour's circuit at its morph, cutoff and resonance; the cutoff a stone on the ground
     env         the ADSR as a barrow on the grass, with a light running along it in real time (envelopes.c's curves);
                 its three corners can be dragged
     lfo         two cycles of the shape after its delay and fade, the playhead running at the LFO's rate, and a moon
                 whose phase is the LFO's
     fx          the delay's repeats as a receding row of stones, the reverb as fog
   Each drawing takes a canvas and a getter of the editor's values, so it can be redrawn when they change. */
(function () {
  "use strict";

  // ---------------------------------------------------------------------------------------------- palette
  let PAL = {};
  const hex = (s) => { s = s.trim(); if (s[0] !== "#") return [255, 110, 0]; const n = parseInt(s.slice(1), 16); return [n >> 16, (n >> 8) & 255, n & 255]; };
  function readPalette() {
    const cs = getComputedStyle(document.documentElement);
    const get = (n) => hex(cs.getPropertyValue("--" + n) || "#000");
    PAL = { bg: get("bg"), surf: get("surf"), text: get("text"), theme: get("theme"), accent: get("accent"), mid: get("mid"),
            dim: get("dim"), sel: get("sel"), stone: get("stone"), rock: get("rock"), groove: get("groove"), lane: get("lane"),
            hi: get("lane-hi") };
  }
  const shade = (c, t) => c.map((v) => t === 0 ? v : t === 1 ? v - (v >> 2) : t === 2 ? v >> 1 : v >> 2);
  const mixc = (a, b, t) => a.map((v, i) => Math.round(v + (b[i] - v) * t));

  /* sq_hash: 32 noisy bits for (x, y), the same every time */
  const hash = (x, y) => {
    let n = (Math.imul(x, 0x9E3779B1) ^ Math.imul(y, 0x85EBCA77)) >>> 0;
    n = (n ^ (n >>> 15)) >>> 0;
    return Math.imul(n, 0x2C1B3C6D) >>> 0;
  };

  /* a pixel surface over a canvas, w x h art pixels: put() writes into an ImageData, done() shows it (transparent where
     nothing was put, so the tablet's black shows through) */
  function surface(cv, w, h) {
    if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
    const c2 = cv.getContext("2d"), img = c2.createImageData(w, h), d = img.data, texts = [];
    return {
      width: w, height: h,
      /* words in the rune face at 10 px: one of its pixels an art pixel (6 a letter); drawn over the pixels in done() */
      text(str, x, y, c, align) {
        const wd = str.length * 6 - 1;
        texts.push([str, Math.round(align === "c" ? x - wd / 2 : align === "r" ? x - wd : x), Math.round(y), c]);
      },
      put(x, y, c) {
        x |= 0; y |= 0;
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        const i = (y * w + x) * 4;
        d[i] = c[0]; d[i + 1] = c[1]; d[i + 2] = c[2]; d[i + 3] = 255;
      },
      done() {
        c2.putImageData(img, 0, 0);
        if (!texts.length) return;
        c2.font = '10px "Carnac Rune"';
        c2.textBaseline = "alphabetic";
        c2.textAlign = "left";
        for (const [str, x, y, c] of texts) { c2.fillStyle = `rgb(${c[0]},${c[1]},${c[2]})`; c2.fillText(str, x, y); }
      },
    };
  }
  /* a canvas sized to its box in art pixels of `px` CSS pixels; null while it is not laid out (a hidden view) */
  function fit(cv, px) {
    if (!cv.clientWidth) return null;
    return surface(cv, Math.max(8, Math.round(cv.clientWidth / px)), Math.max(8, Math.round(cv.clientHeight / px)));
  }
  /* the same in 2 px art pixels, but over a canvas of whole CSS pixels, so its words can be the size of the page's
     (the rune face at 10 px, as the knobs' names) while the stones and lines stay in the pictures' blocks. Words are
     placed in art pixels; a letter is 3 of them */
  function fitFine(cv) {
    if (!cv.clientWidth) return null;
    const W = Math.max(16, Math.round(cv.clientWidth)), H = Math.max(16, Math.round(cv.clientHeight));
    const s = surface(cv, W, H);
    return {
      width: W >> 1, height: H >> 1,
      put(x, y, c) { x = (x | 0) * 2; y = (y | 0) * 2; s.put(x, y, c); s.put(x + 1, y, c); s.put(x, y + 1, c); s.put(x + 1, y + 1, c); },
      text(str, x, y, c, align) {
        const wd = str.length * 6 - 1, X = Math.round(x * 2);
        s.text(str, align === "c" ? X - wd / 2 : align === "r" ? X - wd : X, Math.round(y * 2), c);
      },
      done() { s.done(); },
    };
  }
  const pset = (s, x, y, c) => s.put(x, y, c);
  const rect = (s, x, y, w, h, c) => { for (let j = 0; j < (h | 0); j++) for (let i = 0; i < (w | 0); i++) s.put((x | 0) + i, (y | 0) + j, c); };

  /* a plain Bresenham line, so the art stays in whole pixels */
  function line(ctx, x0, y0, x1, y1, c) {
    x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
    const dx = Math.abs(x1 - x0), dy = -Math.abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    let e = dx + dy;
    for (let guard = 0; guard < 4096; guard++) {
      ctx.put(x0, y0, c);
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * e;
      if (e2 >= dy) { e += dy; x0 += sx; }
      if (e2 <= dx) { e += dx; y0 += sy; }
    }
  }
  /* a curve through ys[x] (one height per column), joined column to column */
  function trace(ctx, ys, c, x0 = 0) {
    for (let x = 1; x < ys.length; x++) line(ctx, x0 + x - 1, ys[x - 1], x0 + x, ys[x], c);
  }

  // ---------------------------------------------------------------------------------------------- stones and grass
  function stoneIn(r, w, h, seed, side) {
    const s = hash(seed, 7);
    const deep = ((s >>> 4) & 1) === side;
    let k, d;
    if (deep) { k = (h >> 2) + (s >>> 8) % ((h >> 2) + 1); d = ((w / 3) | 0) + (s >>> 16) % (((w / 6) | 0) + 1); }
    else { k = 1 + (s >>> 12) % 3; d = 1 + (s >>> 20) % (((w / 5) | 0) + 1); }
    if (k < 1) k = 1;
    let inn = r < k ? (((k - r) * d + k - 1) / k) | 0 : 0;
    inn += (((h - 1 - r) * ((w >> 3) + 1)) / (2 * h)) | 0;
    return inn;
  }
  /* ui_seq_draw.c's menhir, simplified: w wide standing on row yb - 1, h tall, a hewn slanted top and a grain */
  function stone(ctx, x, yb, w, h, c, seed, edge) {
    x |= 0; yb |= 0; w |= 0; h |= 0;
    if (h <= 0 || w <= 0) return;
    const top = yb - h;
    if (w < 4 || h < 5) { rect(ctx, x, top, w, h, c); return; }
    for (let r = 0; r < h; r++) {
      let l = stoneIn(r, w, h, seed, 0), rt = w - stoneIn(r, w, h, seed, 1);
      if (rt - l < 2) { l = ((l + rt) >> 1) - 1; rt = l + 2; }
      for (let i = l; i < rt; i++) {
        if (edge && !(i === l || i === rt - 1 || r === 0 || r === h - 1)) continue;
        const n = hash(i + Math.imul(seed, 131), h - r);
        const g = hash((i >> 1) + Math.imul(seed, 7), ((h - r) >> 1) + 300) >>> 29;
        let t = g < 6 ? 0 : 1;
        if ((n >>> 27) === 0) t = 2;
        if (i === l && t < 1) t = 1;
        pset(ctx, x + i, top + r, shade(c, t));
      }
    }
  }
  /* the field: a strip of SURF and tufts of grass along row yb (as the SEQ page's turf) */
  function grass(ctx, x0, x1, yb, seed, lit) {
    for (let x = x0; x < x1; x++) {
      const n = hash(x, seed + 900);
      rect(ctx, x, yb, 1, 2, PAL.surf);
      if ((n >>> 29) < 3) pset(ctx, x, yb - 1 - ((n >>> 8) & 1), (n >>> 12) & 1 ? (lit ? PAL.mid : PAL.dim) : PAL.stone);
      if ((n >>> 27) === 0) pset(ctx, x, yb - 2, lit ? PAL.accent : PAL.mid);
    }
  }

  // ---------------------------------------------------------------------------------------------- the moon
  /* a moon of radius r at (cx, cy); place 0..1 along the arch: a waxing crescent at 0 (lit on the right), full at the
     middle, waning at 1. Its seas a shade darker (the popup's PU_MARE). `clipY`: nothing below the horizon */
  function moon(ctx, cx, cy, r, place, lit, clipY) {
    const f = Math.max(0, Math.min(1, place));
    const t = f <= 0.5 ? 0.55 - 3.1 * f : -1 + 3.1 * (f - 0.5);   // the terminator: 0.55 (thin) .. -1 (full)
    const sea = shade(lit, 1);
    const dark = mixc(PAL.surf, PAL.stone, 0.5);
    for (let y = -r; y <= r; y++) for (let x = -r; x <= r; x++) {
      if (x * x + y * y > r * r + r * 0.6) continue;
      const py = cy + y;
      if (clipY !== undefined && py >= clipY) continue;
      const rr = Math.sqrt(Math.max(0, 1 - (y * y) / (r * r)));
      const u = (f <= 0.5 ? x : -x) / r;
      const on = u > t * rr - 0.001 || f === 0.5;
      let c = on ? lit : dark;
      if (on && r >= 4 && ((x + 1) * (x + 1) + (y + 1) * (y + 1) < r * 0.5 || ((x - 2) * (x - 2) + (y - 1) * (y - 1)) < r * 0.3 ||
                          ((x + 2) * (x + 2) + (y - 2) * (y - 2)) < r * 0.2)) c = sea;
      pset(ctx, cx + x, py, c);
    }
  }

  // ---------------------------------------------------------------------------------------------- the dial
  /* a knob: an arch of dots from the left horizon over the top to the right, lit from its start (0, or overhead for a
     bipolar knob) to the moon. W x H art pixels (the dial's canvas is fixed in art pixels and scaled by CSS) */
  const DIAL_W = 33, DIAL_H = 21;
  function dial(cv, frac, bip, cold, seed) {
    const ctx = surface(cv, DIAL_W, DIAL_H);
    const cx = 16, cy = 18, R = 14, ground = 18;
    const lit = cold ? PAL.mid : PAL.theme, moonc = cold ? PAL.mid : PAL.accent;
    const a0 = bip ? 0.5 : 0, f = Math.max(0, Math.min(1, frac));
    const N = 29;
    for (let i = 0; i < N; i++) {                                   // the sky's path, a dotted arch
      const p = i / (N - 1), ang = Math.PI * (1 - p);
      const x = Math.round(cx + R * Math.cos(ang)), y = Math.round(cy - R * Math.sin(ang));
      const on = (p >= Math.min(a0, f) - 1e-6 && p <= Math.max(a0, f) + 1e-6) && f !== a0;
      if (on) pset(ctx, x, y, lit);
      else if (i % 2 === 0) pset(ctx, x, y, PAL.dim);
    }
    const ang = Math.PI * (1 - f);
    moon(ctx, Math.round(cx + R * Math.cos(ang)), Math.round(cy - R * Math.sin(ang)), 3, f, moonc, ground);
    stone(ctx, cx - 5, ground, 5, 8, cold ? PAL.dim : PAL.mid, seed);    // the stones, in front of the moon
    stone(ctx, cx + 1, ground, 4, 6, cold ? PAL.stone : PAL.dim, seed + 3);
    grass(ctx, 0, DIAL_W, ground, seed, !cold);
    ctx.done();
  }

  // ---------------------------------------------------------------------------------------------- module pictures
  /* every picture: draw(cv, v) where v(key) is the editor's value. 2 screen pixels an art pixel */
  const PX = 2;

  /* OSC: the bank's 8 frames as stones, the scan's place lit between them */
  function osc(cv, scan, bankName) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3;
    grass(ctx, 0, W, g, 11, true);
    const n = 8, sw = Math.max(4, Math.floor((W - 16) / n) - 3), gap = (W - 8 - n * sw) / (n - 1);
    const pos = (scan / 127) * (n - 1);
    for (let i = 0; i < n; i++) {
      const d = Math.abs(pos - i), near = Math.max(0, 1 - d);
      const h = Math.round(H * 0.42 + H * 0.28 * Math.sin(i * 1.9 + 1) * 0.5 + near * H * 0.15);
      const c = near > 0.5 ? PAL.theme : near > 0 ? mixc(PAL.dim, PAL.theme, near) : PAL.stone;
      stone(ctx, 4 + i * (sw + gap), g, sw, Math.min(h, g - 3), c, 40 + i * 13);
    }
    const mx = Math.round(4 + pos * (sw + gap) + sw / 2);         // the scan: a mark on the ground under the stones
    rect(ctx, mx - 1, g, 3, 2, PAL.accent);
    rect(ctx, mx, g - 1, 1, 1, PAL.accent);
    ctx.done();
  }

  /* MIX: a menhir per voice of the mixer, as tall as its level (0..1), with a short label under it */
  function mix(cv, voices) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3;
    grass(ctx, 0, W, g, 23, true);
    const n = voices.length, slot = W / n, sw = Math.min(14, Math.floor(slot * 0.5));
    voices.forEach((vo, i) => {
      const x = Math.round(i * slot + (slot - sw) / 2), full = g - 4;
      stone(ctx, x, g, sw, full, PAL.stone, 60 + i * 7, true);
      const h = Math.round(full * Math.max(0, Math.min(1, vo.level)));
      if (h > 0) stone(ctx, x, g, sw, h, vo.level > 0.02 ? PAL.theme : PAL.dim, 60 + i * 7);
    });
    ctx.done();
  }

  /* NOISE: grains of fog, coarse when dark, fine when bright; as many as the level */
  function noise(cv, level, colour, t) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3;
    const k = Math.floor(t / 90);
    const cells = Math.round(W * (g - 2) * 0.22 * level);
    const size = colour < 0.33 ? 3 : colour < 0.66 ? 2 : 1;
    for (let i = 0; i < cells / (size * size); i++) {
      const n = hash(i, k + 77), x = n % W, y = (n >>> 12) % (g - 2);
      const fade = 1 - y / g;
      pset(ctx, x, y + 1, (n >>> 28) < 3 ? PAL.accent : fade > 0.5 ? PAL.mid : PAL.dim);
      if (size > 1) rect(ctx, x, y + 1, size, 1, PAL.dim);
    }
    grass(ctx, 0, W, g, 31, level > 0.02);
    ctx.done();
  }

  /* FILTER: |H| over 10 octaves for a 4-pole loop with its taps blended LP .. BP .. HP by morph (0..1), resonance
     k = res * 3.9, cutoff on a log axis; FORMANT: two resonant bands; MULTI's MODE picks LP / BP / HP / NOTCH */
  function filterCurve(W, cutoff, res, morph, flav) {
    const ys = new Float32Array(W);
    const fc = Math.pow(2, (cutoff / 127) * 10);                  // 1 .. 1024 (the axis' units)
    const k = (res / 127) * 3.9;
    const mag = (w, f0, kk, m, mode) => {
      // one-pole stage G = 1 / (1 + j w/f0) as complex
      const x = w / f0, d = 1 + x * x;
      const g = [1 / d, -x / d], hp = [x * x / d, x / d];       // G and 1 - G
      const mul = (a, b) => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]];
      const g2 = mul(g, g), g4 = mul(g2, g2), h2 = mul(hp, hp), h4 = mul(h2, h2), bp = mul(g2, h2);
      let out;
      if (mode !== undefined) out = [g4, [bp[0] * 4, bp[1] * 4], h4, [g4[0] + h4[0], g4[1] + h4[1]]][mode];
      else if (m < 0.5) { const t = m * 2; out = [g4[0] * (1 - t) + bp[0] * 4 * t, g4[1] * (1 - t) + bp[1] * 4 * t]; }
      else { const t = (m - 0.5) * 2; out = [bp[0] * 4 * (1 - t) + h4[0] * t, bp[1] * 4 * (1 - t) + h4[1] * t]; }
      const den = [1 + kk * g4[0], kk * g4[1]];                  // the loop: 1 / (1 + k G^4)
      const dd = den[0] * den[0] + den[1] * den[1];
      return Math.hypot(out[0], out[1]) / Math.sqrt(dd) * (1 + kk * 0.35);
    };
    for (let x = 0; x < W; x++) {
      const w = Math.pow(2, (x / (W - 1)) * 10);
      let m;
      if (flav === 3) {                                              // FORMANT: two bands, the morph spreads them
        const f2 = fc * (1.6 + morph * 3);
        const band = (f0) => { const q = 1.5 + res / 30, r = w / f0 - f0 / w; return 1 / Math.sqrt(1 + q * q * r * r); };
        m = Math.max(band(fc), 0.8 * band(f2)) * (1 + res / 127);
      } else if (flav === 1) m = mag(w, fc, k, 0, Math.min(3, Math.floor(morph * 4)));
      else m = mag(w, fc, k * (flav === 2 ? 0.8 : 1), morph);
      ys[x] = m;
    }
    return { ys, fcx: (Math.log2(fc) / 10) * (W - 1) };
  }
  function filter(cv, cutoff, res, morph, flav, drive, other) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3, top = 3;
    for (let o = 1; o < 10; o++) { const x = Math.round((o / 10) * (W - 1)); for (let y = top; y < g; y += 3) pset(ctx, x, y, PAL.surf); }
    const { ys, fcx } = filterCurve(W, cutoff, res, morph / 127, flav);
    const dB = (m) => 20 * Math.log10(Math.max(1e-4, m));
    const yOf = (m) => { const d = Math.max(-48, Math.min(18, dB(m))); return Math.round(top + ((18 - d) / 66) * (g - top - 1)); };
    const yy = Array.from(ys, yOf);
    if (other) {                                                     // FILTER MIX: the second filter's curve, dimmer
      const o = filterCurve(W, other.cutoff, other.res, other.morph / 127, other.flav);
      for (let x = 0; x < W; x++) if (x % 2 === 0) pset(ctx, x, yOf(o.ys[x]), PAL.mid);
    }
    for (let x = 0; x < W; x++) {                                    // the passed band, a dim fill under the curve
      for (let y = yy[x] + 1; y < g; y++) if ((x + y) % 2 === 0) pset(ctx, x, y, PAL.surf);
    }
    const zero = yOf(1);
    for (let x = 0; x < W; x += 2) pset(ctx, x, zero, PAL.dim);
    trace(ctx, yy, PAL.theme);
    if (drive > 8) for (let x = 0; x < W; x++) if (hash(x, 5) >>> 28 < drive / 32) pset(ctx, x, yy[x] - 1, PAL.accent);
    grass(ctx, 0, W, g, 47, true);
    stone(ctx, Math.round(fcx) - 2, g, 5, 7, PAL.accent, 9);         // the cutoff, a stone on the ground
    ctx.done();
  }

  /* ENV: envelopes.c's times: T = 1 ms * 20000^((k/127)^0.75) */
  const envT = (k) => 0.001 * Math.pow(20000, Math.pow(k / 127, 0.75));
  const fmtT = (s) => s < 1 ? `${Math.round(s * 1000)} ms` : `${s < 10 ? s.toFixed(2) : s.toFixed(1)} s`;
  /* the knob's share of its segment's width, and back (the drawing goes by knob, not by time, so short stages show) */
  const segW = (k) => 0.05 + 0.95 * Math.pow(k / 127, 1.3);
  const segK = (w) => Math.round(127 * Math.pow(Math.max(0, (w - 0.05) / 0.95), 1 / 1.3));
  /* the geometry of the barrow in art pixels: corner xs and the sustain level, for drawing and for dragging */
  function envGeom(W, H, a, d, s, r) {
    const g = H - 3, top = 4, seg = (W - 8) * 0.27, hold = (W - 8) * 0.19;
    const x0 = 2, xa = x0 + seg * segW(a), xd = xa + seg * segW(d), xs = xd + hold, xr = xs + seg * segW(r);
    return { g, top, seg, x0, xa, xd, xs, xr, sy: g - 1 - (s / 127) * (g - 1 - top) };
  }
  /* the level at x (0..1), envelopes.c's shapes: the attack aims at 1.5x and stops at full; the decay falls toward 0 and is
     caught at the sustain; the release falls 60 dB in its time */
  function envLevelAt(G, x, s) {
    const S = s / 127;
    if (x <= G.xa) { const u = (x - G.x0) / Math.max(1, G.xa - G.x0); return Math.min(1, 1.5 * (1 - Math.pow(3, -u)) / (1 - 1 / 3)); }
    if (x <= G.xd) { const u = (x - G.xa) / Math.max(1, G.xd - G.xa); return Math.max(S, Math.pow(1000, -u * 0.4 * 2.2)); }
    if (x <= G.xs) return S;
    if (x <= G.xr) { const u = (x - G.xs) / Math.max(1, G.xr - G.xs); return S * Math.pow(1000, -u); }
    return 0;
  }
  function env(cv, a, d, s, r, invert, head, ghosts) {
    const ctx = fit(cv, PX);
    if (!ctx) return null;
    const W = cv.width, H = cv.height;
    const G = envGeom(W, H, a, d, s, r);
    const yOf = (lv) => invert ? Math.round(G.top + lv * (G.g - 1 - G.top)) : Math.round(G.g - 1 - lv * (G.g - 1 - G.top));
    const ys = [];
    for (let x = 0; x < W; x++) ys.push(yOf(x < G.x0 ? 0 : envLevelAt(G, x, s)));
    for (let x = Math.floor(G.x0); x <= Math.ceil(G.xr) && x < W; x++) {   // the barrow's earth: rock tones, mottled
      const y0 = invert ? yOf(0) : ys[x], y1 = invert ? ys[x] : G.g;
      for (let y = Math.min(y0, y1) + 1; y < Math.max(y0, y1); y++) {
        const n = hash(x >> 1, y >> 1);
        pset(ctx, x, y, (n >>> 29) === 0 ? PAL.stone : (n >>> 30) === 0 ? PAL.rock : PAL.surf);
      }
    }
    /* the other two envelopes as outlines over the earth, so they show through it: the first dashed long, the second
       dotted, each a shade of its own; numbered at the attack's peak by a tick of that many pixels */
    (ghosts || []).forEach((gh, gi) => {
      const GG = envGeom(W, H, gh.a, gh.d, gh.s, gh.r);
      const yy = (lv) => gh.inv ? Math.round(GG.top + lv * (GG.g - 1 - GG.top)) : Math.round(GG.g - 1 - lv * (GG.g - 1 - GG.top));
      const c = gi === 0 ? PAL.accent : PAL.mid;
      let prev = null;
      for (let x = Math.floor(GG.x0); x <= Math.min(W - 1, Math.ceil(GG.xr) + 1); x++) {
        const y = yy(envLevelAt(GG, x, gh.s));
        const on = gi === 0 ? (x % 6) < 4 : (x % 3) === 0;
        if (on) { if (prev && Math.abs(prev[1] - y) > 1) line(ctx, prev[0], prev[1], x, y, c); else pset(ctx, x, y, c); }
        prev = on ? [x, y] : null;
      }
    });
    trace(ctx, ys, PAL.theme);
    grass(ctx, 0, W, G.g, 3, true);
    // the corners you can drag: attack's peak, the decay's end on the sustain, the release's end
    const knob = (x, y) => { rect(ctx, x - 1, y - 1, 3, 3, PAL.accent); pset(ctx, x, y, PAL.hi); };
    knob(Math.round(G.xa), yOf(1)); knob(Math.round(G.xd), yOf(s / 127)); knob(Math.round(G.xr), yOf(0) - (invert ? -1 : 1));
    if (head !== null && head !== undefined) {                       // the light running along it
      const hx = Math.round(head), hy = ys[Math.max(0, Math.min(W - 1, hx))];
      rect(ctx, hx, G.top, 1, G.g - G.top, PAL.sel);
      moon(ctx, hx, hy, 2, 0.5, PAL.hi);
    }
    ctx.done();
    return G;
  }
  /* where the light is t seconds into a looped note: the stages in real time, held 0.8 s at the sustain, 0.6 s of rest */
  function envHead(G, a, d, s, r, t) {
    const S = s / 127;
    const tA = envT(a), tD = S > 0.001 ? Math.min(envT(d) * 2.5 * 0.4, envT(d) * 0.4 * 2.5 * Math.log(1 / S) / Math.log(1000)) : envT(d),
          tH = 0.8, tR = envT(r), rest = 0.6, tot = tA + tD + tH + tR + rest;
    let u = t % tot;
    if (u < tA) return G.x0 + (G.xa - G.x0) * (u / tA); u -= tA;
    if (u < tD) return G.xa + (G.xd - G.xa) * (u / tD); u -= tD;
    if (u < tH) return G.xd + (G.xs - G.xd) * (u / tH); u -= tH;
    if (u < tR) return G.xs + (G.xr - G.xs) * (u / tR);
    return null;
  }

  /* LFO: lfo.c's ring of six shapes (sine, triangle, saw, square, stepped, slewed, then the sine again), the knob
     walking it once over 0..127; the random ones from a fixed seed. Hz = 0.05 * 800^(rate/127) when free */
  const lfoHz = (rate) => 0.05 * Math.pow(800, rate / 127);
  function shapeAt(stage, ph, cyc) {
    switch (stage) {
      case 0: return Math.sin(ph * 2 * Math.PI);
      case 1: return ph < 0.5 ? -1 + 4 * ph : 3 - 4 * ph;
      case 2: return 1 - 2 * ph;
      case 3: return ph < 0.5 ? 1 : -1;
      case 4: return ((hash(cyc, 99) >>> 16) / 32768) - 1;
      default: { const a = ((hash(cyc, 99) >>> 16) / 32768) - 1, b = ((hash(cyc + 1, 99) >>> 16) / 32768) - 1; return a + (b - a) * ph; }
    }
  }
  function lfoAt(shape, phase) {
    const pos = (Math.min(127, shape) * 6) / 127;
    let st = Math.floor(pos); const w = pos - st;
    if (st >= 6) st = 0;
    const cyc = Math.floor(phase), ph = phase - cyc;
    const a = shapeAt(st, ph, cyc);
    return w < 1e-6 ? a : a + (shapeAt(st === 5 ? 0 : st + 1, ph, cyc) - a) * w;
  }
  /* shape 0..127, delay 0..127 (the share of the window before it starts), fade -1..1 (in, or out when negative) */
  function lfo(cv, shape, delay, fade, phase, live) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3, top = 3, mid = Math.round((top + g - 1) / 2), amp = (g - 1 - top) / 2 - 1;
    for (let x = 0; x < W; x += 2) pset(ctx, x, mid, PAL.surf);
    const x0 = Math.round((delay / 127) * W * 0.35), cyc = 2, span = W - 12 - x0;
    const env = (x) => { if (x < x0) return 0; const u = (x - x0) / Math.max(1, span); return fade > 0 ? Math.min(1, u / Math.max(0.05, fade)) : fade < 0 ? Math.max(0, 1 - u / Math.max(0.05, -fade)) : 1; };
    const ys = [];
    for (let x = 0; x < W; x++) {
      const p = x < x0 ? 0 : ((x - x0) / Math.max(1, span)) * cyc;
      ys.push(Math.round(mid - lfoAt(shape, p) * amp * env(x)));
    }
    for (let x = x0; x < W; x++) if ((x & 1) === 0) { const y0 = Math.min(mid, ys[x]), y1 = Math.max(mid, ys[x]); for (let y = y0 + 1; y < y1; y += 2) pset(ctx, x, y, PAL.surf); }
    trace(ctx, ys, PAL.theme);
    if (x0 > 0) for (let y = top; y < g; y += 2) pset(ctx, x0, y, PAL.dim);
    grass(ctx, 0, W, g, 17, true);
    if (live) {                                                    // the playhead and the moon, at the LFO's phase
      const f = phase % 1, hx = Math.round(x0 + (f + (Math.floor(phase) % cyc)) / cyc * span);
      if (hx < W) {
        rect(ctx, hx, top, 1, g - top, PAL.sel);
        moon(ctx, hx, ys[hx], 2, 0.5, PAL.hi);
      }
      moon(ctx, W - 6, 6, 4, f, PAL.accent);
    }
    ctx.done();
  }

  /* FX: the delay's repeats as stones running away (time = spacing, feedback = how slowly they shrink), the reverb a fog
     over the plain behind them (time = how far, damping = how dark) */
  function fx(cv, dtime, fdbk, rtime, damp, wet, t) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3;
    const reach = 0.25 + 0.75 * (rtime / 127), fogTop = Math.round(g - (g - 4) * (0.35 + 0.5 * reach));
    const k = Math.floor(t / 140);
    for (let y = fogTop; y < g; y++) for (let x = 0; x < W * reach; x++) {
      const n = hash(x + k * 3, y), dens = (y - fogTop) / (g - fogTop) * (1 - x / (W * reach));
      if ((n >>> 24) / 256 < dens * 0.55 * (0.3 + wet * 0.7)) pset(ctx, x, y, (n & 7) === 0 && damp < 64 ? PAL.mid : PAL.dim);
    }
    grass(ctx, 0, W, g, 61, true);
    const step = 6 + (dtime / 127) * 16, fb = 0.25 + 0.72 * (fdbk / 127);
    let h = g - 6, x = 3, i = 0;
    while (x < W - 4 && h >= 3 && i < 24) {
      const w = Math.max(3, Math.round(8 * Math.pow(0.93, i)));
      stone(ctx, Math.round(x), g, w, Math.round(h), i === 0 ? PAL.theme : i < 3 ? PAL.mid : PAL.dim, 300 + i);
      x += step * Math.pow(0.95, i) + w;
      h *= fb; i++;
    }
    ctx.done();
  }

  /* the header's alignment: menhirs running away to the right on the grass, big and lit at the left (stones.js), and
     a moon over the far end, its phase the day's */
  function alignment(cv, seed, startCss) {
    const ctx = fit(cv, 2);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3;
    const x0 = Math.min(W - 40, Math.max(Math.round(W * 0.3), Math.round((startCss || 0) / 2)));   // (past the wordmark)
    let x = x0;
    for (let k = 0; x < W - 4; k++) {
      const s = hash(k, seed), far = Math.min(1, (x - x0) / Math.max(1, W - x0));
      const h = Math.max(5, Math.round((g - 4) * (1 - 0.75 * far) * (0.7 + 0.3 * ((s >>> 8) & 255) / 255)));
      const w = Math.max(3, Math.round((11 - 7 * far) * (0.75 + 0.25 * ((s >>> 16) & 255) / 255)));
      const c = far < 0.25 ? PAL.theme : far < 0.55 ? PAL.mid : far < 0.8 ? PAL.dim : PAL.stone;
      stone(ctx, x, g, w, h, c, (seed * 97 + k) | 0);
      x += w + 2 + Math.round((10 - 8 * far) * (((s >>> 24) & 255) / 255));
    }
    const day = (Date.now() / 86400000 - 10962.75) / 29.53;         // (a new moon on 2000-01-06 18:14 UTC)
    moon(ctx, W - 14, 9, 5, (day - Math.floor(day)), PAL.accent);
    grass(ctx, 0, W, g, seed, true);
    ctx.done();
  }

  // ---------------------------------------------------------------------------------------------- the architecture
  /* the voice's signal path (PresetStructures.h VARCH_*): tablets of the stages joined by ley lines, a light running
     down each line in the signal's direction. [label, col, row (-1 top, 0 middle, 1 bottom), ghosts]; edges [from, to,
     dashed]: a dashed one is a cross-link (cross-mod, serial filters) */
  const ARCHS = {
    SPREAD: { n: [["OSC1", 0, 0, 1], ["F1", 1, 0], ["AMP", 2, 0]], e: [[0, 1], [1, 2]], note: "OSC1 AS 4 PLAYHEADS" },
    DUAL_OSC: { n: [["OSC1", 0, -1], ["OSC2", 0, 1], ["MIX", 1, 0], ["F1", 2, 0], ["AMP", 3, 0]], e: [[0, 2], [1, 2], [2, 3], [3, 4], [0, 1, 1]], note: "CROSS-MOD" },
    DUAL_FILTER: { n: [["OSC1", 0, 0], ["F1", 1, -1], ["F2", 1, 1], ["FMIX", 2, 0], ["AMP", 3, 0]], e: [[0, 1], [0, 2], [1, 3], [2, 3], [3, 4], [1, 2, 1]], note: "SERIAL .. PARALLEL" },
    FULL: { n: [["OSC1", 0, -1], ["OSC2", 0, 1], ["MIX", 1, 0], ["F1", 2, -1], ["F2", 2, 1], ["FMIX", 3, 0], ["AMP", 4, 0]],
            e: [[0, 2], [1, 2], [2, 3], [2, 4], [3, 5], [4, 5], [5, 6], [0, 1, 1], [3, 4, 1]], note: "DUAL OSC INTO DUAL FILT" },
    STACK: { n: [["OSC1", 0, -1], ["F1", 1, -1], ["ENV1", 2, -1], ["OSC2", 0, 1], ["F2", 1, 1], ["ENV2", 2, 1], ["BLEND", 3, 0]],
             e: [[0, 1], [1, 2], [3, 4], [4, 5], [2, 6], [5, 6]], note: "TWO STRATA, BLENDED" },
    BINAURAL: { n: [["OSC1", 0, 0], ["F1 L", 1, -1], ["F1 R", 1, 1], ["LEFT", 2, -1], ["RIGHT", 2, 1]], e: [[0, 1], [0, 2], [1, 3], [2, 4]], note: "ONE EAR BEAT-OFFSET" },
  };
  /* a tablet's hewn outline (corners cut), filled with `fill` if given */
  function hewn(ctx, x, y, w, h, edge, fill) {
    for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) {
      const a = i + j, b = (w - 1 - i) + j, c = i + (h - 1 - j), d = (w - 1 - i) + (h - 1 - j);
      if (a < 2 || b < 1 || c < 1 || d < 2) continue;
      const onEdge = i === 0 || j === 0 || i === w - 1 || j === h - 1 || a === 2 || b === 1 || c === 1 || d === 2;
      if (onEdge) pset(ctx, x + i, y + j, edge); else if (fill) pset(ctx, x + i, y + j, fill);
    }
  }
  function archMap(cv, id, t) {
    const ctx = fitFine(cv);
    if (!ctx) return;
    const W = ctx.width, H = ctx.height, A = ARCHS[id] || ARCHS.DUAL_OSC;
    const cols = Math.max(...A.n.map((n) => n[1])) + 1, bh = 8, g = H - 3;
    const top = 2, bot = g - 6, mid = Math.round((top + bot) / 2);
    const rowY = (r) => r < 0 ? top : r > 0 ? bot - bh : mid - (bh >> 1);
    const boxes = A.n.map(([lab, c, r, ghosts]) => {
      const w = Math.ceil(lab.length * 3 + 4), x = Math.round(2 + (W - 9 - w) * (cols > 1 ? c / (cols - 1) : 0.5));
      return { lab, x, y: rowY(r), w, ghosts };
    });
    A.e.forEach(([a, b, dashed], ei) => {                              // the ley lines: elbows, a light running along
      const p = boxes[a], q = boxes[b], pts = [];
      const push = (x0, y0, x1, y1) => { const n = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0)); for (let i = 0; i < n; i++) pts.push([Math.round(x0 + (x1 - x0) * i / n), Math.round(y0 + (y1 - y0) * i / n)]); };
      if (dashed) { const x = Math.round(p.x + p.w / 2); push(x, p.y + bh, x, q.y); }
      else {
        const x0 = p.x + p.w, y0 = p.y + (bh >> 1), x1 = q.x - 1, y1 = q.y + (bh >> 1), xm = Math.round((x0 + x1) / 2);
        push(x0, y0, xm, y0); push(xm, y0, xm, y1); push(xm, y1, x1, y1);
      }
      pts.forEach(([x, y], k) => { if (dashed) { if (k % 3 !== 2) pset(ctx, x, y, PAL.dim); } else pset(ctx, x, y, k % 2 === 0 ? PAL.mid : PAL.dim); });
      if (!dashed && pts.length) {
        const k = Math.floor((t / 45 + ei * 7) % (pts.length + 12));
        for (let j = 0; j < 3; j++) { const pp = pts[k - j]; if (pp) pset(ctx, pp[0], pp[1], j ? PAL.theme : PAL.hi); }
      }
    });
    boxes.forEach((b) => {                                            // the stages: hewn tablets, lit at the edge
      if (b.ghosts) for (let gi = 3; gi > 0; gi--) hewn(ctx, b.x + gi * 2, b.y - gi, b.w, bh, gi > 1 ? PAL.stone : PAL.dim, PAL.groove);
      hewn(ctx, b.x, b.y, b.w, bh, PAL.theme, PAL.groove);
      ctx.text(b.lab, b.x + 2.5, b.y + bh - 1.5, PAL.text);
    });
    grass(ctx, 0, W, g, 71, true);
    ctx.text(A.note, W - 1, g - 1.5, PAL.mid, "r");
    ctx.done();
  }

  // ---------------------------------------------------------------------------------------------- the ley lines
  /* the MOD routes: SOURCE stones down the left, DEST stones down the right, each route a ley line between them
     (bright and thick by its amount, broken when negative), its VIA a small stone on the line with a faint line back
     to the VIA source; lights run along the lines. routes: [{ i, src, via, dst, amt -1..1, sel }]. Returns each
     route's points (for picking one with the pointer) */
  function leys(cv, routes, srcs, dsts, srcNames, dstNames, t, big) {
    const ctx = fitFine(cv);
    if (!ctx) return [];
    const W = ctx.width, H = ctx.height;
    const sw = Math.max(4, ...srcs.map((s) => srcNames[s].length)) * 3, dw = Math.max(4, ...dsts.map((d) => dstNames[d].length)) * 3;
    const xS = 2 + sw + 3, xD = W - 2 - dw - 3 - 5;
    const yOf = (n, i) => Math.round(6 + (H - 12) * (n > 1 ? i / (n - 1) : 0.5));
    const sy = {}, dy = {};
    srcs.forEach((s, i) => { sy[s] = yOf(srcs.length, i); });
    dsts.forEach((d, i) => { dy[d] = yOf(dsts.length, i); });
    const used = new Set(routes.map((r) => r.src)), via = new Set(routes.filter((r) => r.via).map((r) => r.via));
    const out = [];
    const bez = (x0, y0, x1, y1, u) => {
      const a = 1 - u, cx0 = x0 + (x1 - x0) * 0.45, cx1 = x1 - (x1 - x0) * 0.45;
      return [a * a * a * x0 + 3 * a * a * u * cx0 + 3 * a * u * u * cx1 + u * u * u * x1, a * a * a * y0 + 3 * a * a * u * y0 + 3 * a * u * u * y1 + u * u * u * y1];
    };
    routes.forEach((r, ri) => {
      if (sy[r.src] === undefined || dy[r.dst] === undefined) return;
      const x0 = xS + 6, y0 = sy[r.src], x1 = xD - 1, y1 = dy[r.dst], n = Math.max(24, Math.round((x1 - x0) * 1.2));
      if (x1 <= x0) return;
      const pts = [];
      for (let k = 0; k <= n; k++) { const [x, y] = bez(x0, y0, x1, y1, k / n); pts.push([Math.round(x), Math.round(y)]); }
      const mag = Math.abs(r.amt), c = r.sel ? PAL.hi : mag > 0.5 ? PAL.theme : mag > 0.15 ? PAL.mid : PAL.dim;
      const thick = mag > 0.66 || r.sel;
      pts.forEach(([x, y], k) => {
        if (r.amt < 0 && (k % 4) > 1) return;
        pset(ctx, x, y, c);
        if (thick) pset(ctx, x, y + 1, r.sel ? PAL.accent : c);
      });
      if (r.via && sy[r.via] !== undefined) {                     // VIA: a stone on the line, a faint line back
        const [mx, my] = pts[Math.round(n * 0.5)];
        const vx = xS + 6, vy = sy[r.via], m = Math.max(1, Math.abs(mx - vx), Math.abs(my - vy));
        for (let k = 0; k < m; k += 3) pset(ctx, Math.round(vx + (mx - vx) * k / m), Math.round(vy + (my - vy) * k / m), PAL.dim);
        stone(ctx, mx - 2, my + 3, 5, 6, r.sel ? PAL.hi : PAL.accent, 13 + ri);
      }
      const speed = 0.5 + mag * 1.5, k = Math.floor(((t / 1000) * speed * 40 + ri * 11) % (pts.length + 10));
      for (let j = 0; j < 3; j++) { const pp = pts[k - j]; if (pp) pset(ctx, pp[0], pp[1], j ? PAL.accent : PAL.hi); }
      out.push({ i: r.i, pts });
    });
    srcs.forEach((s) => {                                            // the stones and their names
      const on = used.has(s) || via.has(s), y = sy[s];
      stone(ctx, xS, y + 4, 5, big ? 8 : 7, on ? PAL.theme : PAL.stone, 200 + s);
      ctx.text(srcNames[s].toUpperCase(), xS - 2, y + 2.5, on ? PAL.text : PAL.dim, "r");
    });
    dsts.forEach((d) => {
      const y = dy[d];
      stone(ctx, xD, y + 4, 5, big ? 8 : 7, PAL.theme, 400 + d);
      ctx.text(dstNames[d], xD + 8, y + 2.5, PAL.text);
    });
    if (!routes.length) ctx.text("NO LEY LINES YET: PICK A SOURCE AND A DEST", W / 2, H / 2 + 2, PAL.mid, "c");
    ctx.done();
    return out;
  }

  /* AMP: a cycle and a half of a sine through the drive (its model's curve), the tremolo's swell as a dotted hull */
  function drive(cv, amt, model, trem, t) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3, mid = Math.round((2 + g) / 2), amp = (g - 4) / 2 - 1;
    const k = 1 + (amt / 127) * 7;
    const curve = (x) => {
      const u = x * k;
      switch (model) {
        case 1: return Math.max(-1, Math.min(1, u));                                    // HARD
        case 2: return Math.sin(u * Math.PI / 2);                                       // FOLD
        case 3: return Math.tanh(u + 0.15 * u * u) / Math.tanh(k + 0.15 * k * k);       // TAPE
        case 4: return Math.tanh(u * 0.9 + 0.25 * Math.max(0, u)) / Math.tanh(k * 1.15); // TUBE
        case 5: return Math.max(-1, Math.min(1, Math.tanh(u * 1.6)));                   // OVERDRIVE
        default: return Math.tanh(u) / Math.tanh(k);                                    // SOFT
      }
    };
    for (let x = 0; x < W; x += 2) pset(ctx, x, mid, PAL.surf);
    const tr = (trem / 127) * 0.6, ph = t / 1000 * 2;
    const ys = [];
    for (let x = 0; x < W; x++) {
      const a = 1 - tr * (0.5 + 0.5 * Math.sin(ph + x / W * 6.28));
      ys.push(Math.round(mid - curve(Math.sin((x / W) * 3 * Math.PI)) * amp * a));
      if (tr > 0.02 && x % 3 === 0) { pset(ctx, x, Math.round(mid - amp * a), PAL.dim); pset(ctx, x, Math.round(mid + amp * a), PAL.dim); }
    }
    trace(ctx, ys, PAL.theme);
    grass(ctx, 0, W, g, 81, true);
    ctx.done();
  }

  // ---------------------------------------------------------------------------------------------- MFX
  /* the layer's MFX, moving: k = its knobs by name, 0..1 (absent ones at 0.5). Each type has its own picture:
       phasers     a plain of grass with notches of night sweeping along it (MANUAL / RATE / DEPTH / FEEDBACK)
       ensemble,   three voices of one wave, drifting apart and back (RATE / DEPTH), the AGE as grit
       chorus
       flangers    a comb of many notches, sweeping (tape: smooth; BBD: stepped)
       pitch       the dry wave dim, the shifted one bright, its echoes climbing (PITCH / FEEDBACK)
       BBD echo    repeats as stones running away, darker each time, a light passing down them (TIME / FEEDBACK)
       bronze age  a wave crushed flat against its rails, sparks where it clips (DIST)
       wind harp   five strings in the wind, each trembling as a gust passes (DECAY / TONE)
       stones      standing stones ringing: rings spreading from each as it is struck (DECAY / TONE) */
  function mfx(cv, type, name, k, t) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3, mid = Math.round((2 + g) / 2), amp = (g - 6) / 2;
    const K = (n, d = 0.5) => (k[n] === undefined ? d : k[n]);
    const s = t / 1000;
    const ys = [];
    const waveAt = (x, ph, f = 3) => Math.sin((x / W) * f * 2 * Math.PI + ph);
    if (/PHASER/.test(name)) {                          // notches sweeping along a plain of light
      const rate = K("RATE", K("PHS RATE")), depth = K("DEPTH", K("PHS DEPTH")), fb = K("FEEDBACK", 0.4), man = K("MANUAL", 0.5);
      const sweep = man + depth * 0.35 * Math.sin(s * (0.3 + rate * 4));
      for (let x = 0; x < W; x++) {
        const u = x / W;
        let m = 1;
        for (let n = 0; n < 4; n++) {                     // four notches spaced like the stages'
          const c = Math.min(0.98, Math.max(0.02, sweep * (0.35 + n * 0.28)));
          const w = 0.03 + (1 - fb) * 0.05;
          m *= 1 - Math.exp(-((u - c) * (u - c)) / (w * w)) * (0.6 + fb * 0.4);
        }
        ys.push(Math.round(g - 2 - m * (g - 6)));
      }
      for (let x = 0; x < W; x++) for (let y = ys[x] + 1; y < g; y++) if ((x + y) % 2 === 0) pset(ctx, x, y, PAL.surf);
      trace(ctx, ys, PAL.theme);
      if (/ENS/.test(name)) for (let v = 0; v < 2; v++) for (let x = 0; x < W; x += 2) pset(ctx, x, Math.round(mid - 4 - waveAt(x, s * (1 + v) * (0.5 + K("ENS RATE") * 3) + v * 2) * 3), PAL.mid);
    } else if (/ENSEMBLE|CHORUS/.test(name)) {          // three voices of one wave, drifting
      const rate = K("RATE"), depth = K("DEPTH"), age = K("AGE", 0.2);
      const cols = [PAL.dim, PAL.mid, PAL.theme];
      for (let v = 0; v < 3; v++) {
        const ph = depth * 1.4 * Math.sin(s * (0.4 + rate * 3) * (1 + v * 0.37) + v * 2.1);
        const yy = [];
        for (let x = 0; x < W; x++) yy.push(Math.round(mid - waveAt(x, ph, 2.5) * amp * (0.75 + v * 0.1)));
        trace(ctx, yy, cols[v]);
      }
      for (let i = 0; i < age * 40; i++) { const n = hash(i, Math.floor(s * 12)); pset(ctx, n % W, 2 + (n >>> 12) % (g - 4), PAL.dim); }
    } else if (/FLANGER/.test(name)) {                  // a comb of notches, sweeping
      const rate = K("RATE"), depth = K("DEPTH"), base = K("MANUAL", K("CENTER")), bbd = /BBD/.test(name);
      let sw = base + depth * 0.4 * Math.sin(s * (0.2 + rate * 2.5));
      if (bbd) sw = Math.round(sw * 24) / 24;           // (the bucket brigade's steps)
      const spacing = 4 + sw * 16;
      for (let x = 0; x < W; x++) {
        const m = 0.5 + 0.5 * Math.cos((x / spacing) * 2 * Math.PI);
        ys.push(Math.round(g - 2 - (0.15 + 0.85 * Math.pow(m, 1.5 + K("FEEDBACK", 0.4) * 3)) * (g - 6)));
      }
      for (let x = 0; x < W; x++) for (let y = ys[x] + 1; y < g; y++) if ((x + y) % 2 === 0) pset(ctx, x, y, bbd ? PAL.stone : PAL.surf);
      trace(ctx, ys, PAL.theme);
    } else if (/PITCH/.test(name)) {                    // the dry wave dim, the shifted one bright, its echoes climbing
      const shift = Math.pow(2, ((k.PITCH !== undefined ? k.PITCH : 0.5) - 0.5) * 2), fb = K("FEEDBACK", 0.3);
      const dry = [], wet = [];
      for (let x = 0; x < W; x++) { dry.push(Math.round(mid - waveAt(x, s * 2, 3) * amp * 0.8)); wet.push(Math.round(mid - waveAt(x, s * 2 * shift, 3 * shift) * amp * 0.8)); }
      for (let x = 0; x < W; x += 2) pset(ctx, x, dry[x], PAL.dim);
      trace(ctx, wet, PAL.theme);
      for (let e = 1; e <= Math.round(fb * 4); e++) {
        const off = ((s * 10 + e * 9) % (g - 4));
        for (let x = 0; x < W; x += 3) pset(ctx, x, Math.round(2 + (g - 6) - off - waveAt(x, s * 2 * shift * (1 + e * 0.1), 3 * shift) * 2), PAL.mid);
      }
    } else if (/ECHO/.test(name)) {                     // the repeats as stones, darker each time
      const step = 6 + K("TIME") * 18, fb = 0.3 + 0.65 * K("FEEDBACK"), period = 0.2 + K("TIME") * 1.2;
      let h = g - 6, x = 3, i = 0;
      const lit = Math.floor((s / period) % 12);
      while (x < W - 4 && h >= 3 && i < 20) {
        const w = Math.max(3, Math.round(8 * Math.pow(0.92, i)));
        const c = i === lit ? PAL.accent : i === 0 ? PAL.theme : mixc(PAL.mid, PAL.stone, Math.min(1, i / 8));
        stone(ctx, Math.round(x), g, w, Math.round(h), c, 500 + i);
        x += step * Math.pow(0.95, i) + w; h *= fb; i++;
      }
    } else if (/BRONZE/.test(name)) {                   // a wave crushed against its rails, sparks where it clips
      const dist = K("DIST"), gain = 1 + dist * 12, rail = amp * (0.9 - dist * 0.25);
      for (let x = 0; x < W; x++) {
        const v = Math.tanh(waveAt(x, s * 3, 2.5) * gain + 0.3 * waveAt(x, s * 7, 9) * dist);
        ys.push(Math.round(mid - v * rail));
        if (Math.abs(v) > 0.97 && (hash(x, Math.floor(s * 20)) >>> 28) < 4 + dist * 8) pset(ctx, x, Math.round(mid - Math.sign(v) * (rail + 1 + (hash(x, 3) >>> 30))), PAL.accent);
      }
      for (let x = 0; x < W; x += 3) { pset(ctx, x, Math.round(mid - rail - 1), PAL.dim); pset(ctx, x, Math.round(mid + rail + 1), PAL.dim); }
      trace(ctx, ys, PAL.theme);
    } else if (/HARP/.test(name)) {                     // five strings in the wind
      const decay = K("DECAY"), tone = K("TONE");
      const nS = 4, x0 = 8, x1 = W - 9;
      for (let n = 0; n < nS; n++) {
        const y0 = Math.round(6 + n * ((g - 13) / (nS - 1))), gust = Math.max(0, Math.sin(s * 0.9 - n * 0.8));   // (a gust running down the strings)
        const a = (1 + 5 * gust) * (0.45 + decay * 0.55);
        const yy = [];
        for (let x = x0; x <= x1; x++) {
          const u = (x - x0) / (x1 - x0);
          const ends = Math.sin(u * Math.PI);                 // (fixed at the stones)
          yy.push(Math.round(y0 + ends * a * (0.65 * Math.sin(u * Math.PI * (4 + n * 2) - s * (10 + tone * 14)) +
                                              0.35 * Math.sin(s * (7 + n * 3)))));
        }
        trace(ctx, yy, gust > 0.5 ? PAL.accent : gust > 0.15 ? PAL.theme : PAL.mid, x0);
      }
      stone(ctx, 1, g, 6, g - 3, PAL.mid, 801);            // the harp's two stones, the strings between them
      stone(ctx, W - 7, g, 6, g - 3, PAL.mid, 802);
      for (let i = 0; i < 6; i++) { const n = hash(i, Math.floor(s * 3)), x = (n % W + s * 30) % W; pset(ctx, Math.round(x), 2 + (n >>> 10) % (g - 4), PAL.dim); }
    } else if (/RINGING/.test(name)) {                  // stones struck, rings spreading from each
      const decay = K("DECAY"), tone = K("TONE"), n = 5;
      for (let i = 0; i < n; i++) {
        const x0 = Math.round((i + 0.5) * W / n), hgt = Math.round((g - 8) * (0.45 + 0.4 * ((hash(i, 9) >>> 24) / 255)));
        const period = 1.2 + i * 0.37, age = (s + i * 0.53) % period, life = 0.3 + decay * 1.4;
        if (age < life) {
          const r = 2 + (age / life) * (W / n) * 0.7, fade = 1 - age / life;
          for (let a = 0; a < 40; a++) {
            const th = Math.PI * (a / 39);
            const px = Math.round(x0 + r * Math.cos(th)), py = Math.round(g - hgt * 0.6 - r * Math.sin(th) * (0.5 + tone * 0.3));
            if (a % 2 === 0) pset(ctx, px, py, fade > 0.5 ? PAL.accent : PAL.mid);
          }
        }
        stone(ctx, x0 - 3, g, 6, hgt, age < 0.12 ? PAL.accent : PAL.theme, 700 + i);
      }
    } else {                                            // OFF: the plain, still
      stone(ctx, Math.round(W / 2) - 3, g, 6, Math.round(g * 0.5), PAL.stone, 7);
    }
    grass(ctx, 0, W, g, 91, type !== 0);
    ctx.done();
  }

  // ---------------------------------------------------------------------------------------------- SETUP pictures
  /* the clock: a sarsen circle of 16 stones seen across the plain, one lit per sixteenth at the tempo, and the heel
     stone outside it (the panel's GLOBAL page) */
  function sarsen(cv, bpm, t, ext) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3, cx = W / 2, cy = g - 13, rx = Math.min(W * 0.36, 60), ry = 9;
    const step = Math.floor((t / 1000) * (bpm / 60) * 4) % 16;
    const order = [...Array(16).keys()].map((i) => { const a = (i / 16) * 2 * Math.PI - Math.PI / 2; return { i, x: cx + rx * Math.cos(a), y: cy + ry * Math.sin(a) }; })
      .sort((a, b) => a.y - b.y);                          // the far stones first, the near ones over them
    grass(ctx, 0, W, g, 13, true);
    order.forEach((p) => {
      const near = (p.y - (cy - ry)) / (2 * ry), h = Math.round(7 + near * 9), w = Math.round(3 + near * 3);
      const c = p.i === step ? PAL.accent : p.i % 4 === 0 ? PAL.theme : near > 0.5 ? PAL.mid : PAL.dim;
      stone(ctx, Math.round(p.x - w / 2), Math.round(p.y + 6), w, h, ext ? mixc(c, PAL.stone, 0.4) : c, 900 + p.i);
    });
    stone(ctx, Math.round(cx + rx + 12), g, 5, 16, step === 0 ? PAL.hi : PAL.mid, 999);   // the heel stone
    ctx.done();
  }
  /* VEL CURVE: velocity in across, out up; SOFT bows up, HARD bows down; BASE VELOCITY a dotted line */
  function velCurve(cv, curve, base) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3, top = 3;
    const ex = [0.5, 1, 2][curve] || 1;
    const ys = [];
    for (let x = 0; x < W; x++) ys.push(Math.round(g - 1 - Math.pow(x / (W - 1), ex) * (g - 1 - top)));
    for (let x = 0; x < W; x++) for (let y = ys[x] + 1; y < g; y++) if ((x + y) % 2 === 0) pset(ctx, x, y, PAL.surf);
    const by = Math.round(g - 1 - (base / 127) * (g - 1 - top));
    for (let x = 0; x < W; x += 3) pset(ctx, x, by, PAL.mid);
    trace(ctx, ys, PAL.theme);
    grass(ctx, 0, W, g, 19, true);
    ctx.done();
  }
  /* ZONES: the keyboard (MIDI 0..127) with the split stone and its fade, the velocity split as a stone row under it */
  function zones(cv, split, fade, vsplit, vfade) {
    const ctx = fit(cv, PX);
    if (!ctx) return;
    const W = cv.width, H = cv.height, g = H - 3, ky = 4, kh = Math.round((g - 8) * 0.6);
    const xOf = (n) => Math.round(2 + (n / 127) * (W - 4));
    const BLACK = [1, 3, 6, 8, 10];
    for (let n = 0; n < 128; n++) {
      const x0 = xOf(n), x1 = xOf(n + 1), black = BLACK.includes(n % 12), lower = n < split;
      const fz = fade > 0 && Math.abs(n - split) < fade / 8;
      const c = black ? PAL.groove : fz ? PAL.accent : lower ? PAL.theme : PAL.mid;
      for (let x = x0; x < Math.max(x0 + 1, x1); x++) for (let y = ky; y < ky + (black ? kh * 0.6 : kh); y++) pset(ctx, x, y, c);
      if (n % 12 === 0) for (let y = ky; y < ky + kh; y++) pset(ctx, x0, y, PAL.groove);
    }
    stone(ctx, xOf(split) - 2, ky + kh + 8, 5, 8, PAL.hi, 3);
    const vy = g - 2, vx = Math.round(2 + (vsplit / 127) * (W - 4));
    for (let x = 2; x < W - 2; x++) if (x % 2 === 0) pset(ctx, x, vy, x < vx ? PAL.theme : PAL.dim);
    for (let x = Math.max(2, vx - vfade / 4); x < Math.min(W - 2, vx + vfade / 4); x++) pset(ctx, Math.round(x), vy - 1, PAL.accent);
    ctx.done();
  }
  /* TUNING, as the panel draws it (ui_page_draw.c pu_viz_tuning): thirteen stones from the ROOT to its octave, each
     standing where its step falls, pulled off equal temperament's socket by its cents (a semitone a step's width), the
     pull drawn along the ground; the root and the octave lit, a step more than 2 cents off a shade up. BEND RANGE: the
     span a full bend reaches from the root; A4 in Hz at the top. cents: the 12 steps' offsets from the root */
  function tuning(cv, cents, tune, bend) {
    const ctx = fitFine(cv);
    if (!ctx) return;
    const W = ctx.width, H = ctx.height, g = H - 4, x0 = 6, step = (W - 14) / 12;
    const mt = tune - 64, hz10 = 4400 + Math.trunc((mt * 25415) / 10000) + Math.trunc((mt * mt * 7339) / 10000000);
    ctx.text(`A4 ${Math.floor(hz10 / 10)}.${hz10 % 10} HZ`, 1, 6, mt ? PAL.theme : PAL.mid);
    if (bend > 0) {
      const n = Math.min(12, bend), x1 = Math.round(x0 + n * step), y = 10;
      for (let x = x0; x <= x1; x += 3) pset(ctx, x, y, PAL.dim);
      rect(ctx, x0, y - 2, 1, 5, PAL.mid); rect(ctx, x1, y - 2, 1, 5, PAL.mid);
      if (bend > 12) { pset(ctx, x1 + 2, y - 1, PAL.mid); pset(ctx, x1 + 3, y, PAL.mid); pset(ctx, x1 + 2, y + 1, PAL.mid); }
    }
    for (let x = 0; x < W; x++) { rect(ctx, x, g + 1, 1, 3, PAL.surf); if ((hash(x, 41) >>> 29) === 0) pset(ctx, x, g + 1 + ((hash(x, 7) >>> 31)), PAL.stone); }
    for (let i = 0; i <= 12; i++) {
      const c = cents[i % 12], sx = Math.round(x0 + i * step);
      const x = Math.round(x0 + i * step + (c * step) / 100), h = Math.round((g - 12) * (0.5 + 0.5 * i / 12));
      const lit = i === 0 || i === 12, off = c > 2 || c < -2;
      rect(ctx, sx, g + 2, 1, 2, PAL.dim);                          // the socket: equal temperament's place
      if (x !== sx) for (let k = Math.min(x, sx); k <= Math.max(x, sx); k++) pset(ctx, k, g + 1, off ? PAL.mid : PAL.dim);
      stone(ctx, x - 2, g + 1, 5, h, lit ? PAL.theme : off ? PAL.mid : PAL.stone, i * 7 + 3);
    }
    ctx.done();
  }
  /* a swatch for a THEME row: a lit stone in the theme's colours */
  function swatch(cv, theme, stoneC, surf) {
    const ctx = surface(cv, 14, 12);
    rect(ctx, 0, 10, 14, 2, surf);
    stone(ctx, 3, 10, 7, 10, theme, 5);
    ctx.done();
  }

  /* the FM1's two stone textures as tiles for the page's CSS (data: URLs), in the THEME's tones:
       rock  sq_rock_tone: a tablet's face, two coarse mottles over a fine grit, SQ_ROCK's five tones
       lane  the highlight's noise (SQ_LANE_NOISE: darker, lighter, base), the panel's cursor row */
  function tiles(rock, lane) {
    const make = (tone) => {
      const cv = document.createElement("canvas"), s = surface(cv, 64, 64);
      for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) s.put(x, y, tone(x, y));
      s.done();
      return cv.toDataURL();
    };
    const rockTone = (x, y) => {
      const m = (hash((x / 7) | 0, ((y / 5) | 0) + 911) >>> 31) + (hash(((x + 3) / 4) | 0, (((y + 1) / 3) | 0) + 577) >>> 31);
      const f = hash(x, y) >>> 29;
      return rock[Math.max(0, Math.min(4, 1 + m + (f === 7) - (f === 0)))];
    };
    const laneTone = (x, y) => { const f = hash(x >> 1, (y >> 1) + 41) >>> 29; return f === 0 ? lane[0] : f === 7 ? lane[1] : lane[2]; };
    return { rock: make(rockTone), lane: make(laneTone) };
  }

  window.CarnacViz = { tiles, mfx, sarsen, velCurve, zones, tuning, swatch, archMap, leys, drive, alignment, readPalette, dial, osc, mix, noise, filter, env, envGeom, envHead, segK, envT, fmtT, lfo, lfoHz, lfoAt, fx,
                       stone, grass, moon, fit, hash, get PAL() { return PAL; } };
})();
