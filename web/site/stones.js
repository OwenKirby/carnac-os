/* SPDX-License-Identifier: GPL-3.0-only
   The FM1's stones for the Carnac OS site, drawn the way the screen draws them, pixel by pixel into small canvases that
   the CSS scales up by whole pixels:
     sq_stone        src/ui_seq_draw.c's menhir: a hewn slanted top, a taper, chips, a coarse grain, grit and a crack
     rockTone        src/ui_seq_draw.c sq_rock_tone: a tablet's mottled face (SQ_ROCK ORANGE), set as --rock-tex
     laneTex         the highlight's brighter noise (SQ_LANE_NOISE ORANGE), set as --lane-tex
   and on the pages:
     <canvas data-alignment="SEED">         a row of menhirs running away to the right, on a strip of grass
     <canvas data-steps="ID">               16 SEQ step stones that fill with <progress id=ID>'s value */
(() => {
  const RGB = (r, g, b) => [r, g, b];
  const THEME = RGB(255, 110, 0), ACCENT = RGB(255, 190, 90), MID = RGB(178, 98, 21), DIM = RGB(107, 59, 13);
  const STONE = RGB(66, 34, 6), SURF = RGB(34, 16, 0);                       /* SQ_STONE: an unlit stone */
  const ROCK = [RGB(14, 5, 0), RGB(36, 14, 0), RGB(48, 19, 0), RGB(62, 25, 0), RGB(76, 31, 0)];
  const LANE = [RGB(80, 28, 0), RGB(140, 56, 0), RGB(110, 42, 0)];           /* darker, lighter, base */

  /* sq_hash: 32 noisy bits for (x, y), the same every time */
  const hash = (x, y) => {
    let n = (Math.imul(x, 0x9E3779B1) ^ Math.imul(y, 0x85EBCA77)) >>> 0;
    n = (n ^ (n >>> 15)) >>> 0;
    return Math.imul(n, 0x2C1B3C6D) >>> 0;
  };
  /* sq_shade: c at full, 3/4, 1/2, 1/4 */
  const shade = (c, t) => c.map((v) => t === 0 ? v : t === 1 ? v - (v >> 2) : t === 2 ? v >> 1 : v >> 2);
  const css = (c) => `rgb(${c[0]},${c[1]},${c[2]})`;
  const pset = (ctx, x, y, c) => { ctx.fillStyle = css(c); ctx.fillRect(x, y, 1, 1); };

  function stoneIn(r, w, h, seed, side) {
    const s = hash(seed, 7);
    const deep = ((s >>> 4) & 1) === side;
    let k, d;
    if (deep) { k = (h >> 2) + (s >>> 8) % ((h >> 2) + 1); d = ((w / 3) | 0) + (s >>> 16) % (((w / 6) | 0) + 1); }
    else { k = 1 + (s >>> 12) % 3; d = 1 + (s >>> 20) % (((w / 5) | 0) + 1); }
    if (k < 1) k = 1;
    let inn = r < k ? (((k - r) * d + k - 1) / k) | 0 : 0;
    inn += (((h - 1 - r) * ((w >> 3) + 1)) / (2 * h)) | 0;
    if (h >= 8 && w >= 6 && r >= k && (hash(r >> 1, ((s >>> 8) + side) | 0) >>> 29) === 0) inn++;
    return inn;
  }

  /* sq_stone without the decay: w wide standing on row yb - 1, h tall, its bottom `fill` rows drawn; edge: the outline */
  function stone(ctx, x, yb, w, h, fill, c, seed, edge) {
    if (h <= 0 || w <= 0 || fill <= 0) return;
    if (fill > h) fill = h;
    const top = yb - h;
    ctx.fillStyle = css(c);
    if (w < 4 || h < 5) {
      if (edge) { ctx.fillRect(x + 1, top, w - 2, 1); ctx.fillRect(x, top + 1, 1, h - 1); ctx.fillRect(x + w - 1, top + 1, 1, h - 1); }
      else if (h > 2 && w > 2) { ctx.fillRect(x + 1, yb - fill, w - 2, 1); ctx.fillRect(x, yb - fill + 1, w, fill - 1); }
      else ctx.fillRect(x, yb - fill, w, fill);
      return;
    }
    let cx = -1, cy0 = 0, cy1 = 0, pl = 0, pr = 0;
    if (h >= 12 && w >= 6 && !edge) {                  /* a crack, wandering down from somewhere on the face */
      const n = hash(seed, 31);
      cx = 2 + n % (w - 4);
      cy1 = ((h / 3) | 0) + (n >>> 8) % (h >> 1);
      cy0 = cy1 - (h >> 2) - (n >>> 16) % ((h >> 2) + 1);
    }
    for (let r = 0; r < h; r++) {
      let l = stoneIn(r, w, h, seed, 0), rt = w - stoneIn(r, w, h, seed, 1);
      if (rt - l < 2) { l = ((l + rt) >> 1) - 1; rt = l + 2; }
      const j = h - 1 - r;
      if (r >= h - fill)
        for (let i = l; i < rt; i++) {
          const n = hash(i + Math.imul(seed, 131), j);
          if (edge && !(i === l || i === rt - 1 || r === 0 || i < pl || i >= pr || r === h - fill)) {
            if ((n >>> 27) === 0) pset(ctx, x + i, top + r, shade(c, 3));
            continue;
          }
          const g = hash((i >> 1) + Math.imul(seed, 7), (j >> 1) + 300) >>> 29;
          let t = g < 6 ? 0 : 1;
          if ((n >>> 27) === 0) t = 2;
          if (i === l && t < 1) t = 1;
          if (i === cx + (hash(j >> 2, seed) >>> 31) && j >= cy0 && j < cy1) t = 3;
          pset(ctx, x + i, top + r, shade(c, t));
        }
      pl = l; pr = rt;
    }
  }

  /* a tile of w x h pixels, each coloured by tone(x, y), as a data: URL */
  function tile(w, h, tone) {
    const cv = document.createElement("canvas");
    cv.width = w; cv.height = h;
    const ctx = cv.getContext("2d");
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) pset(ctx, x, y, tone(x, y));
    return cv.toDataURL();
  }
  const rockTone = (x, y) => {
    const m = (hash((x / 7) | 0, ((y / 5) | 0) + 911) >>> 31) + (hash(((x + 3) / 4) | 0, (((y + 1) / 3) | 0) + 577) >>> 31);
    const f = hash(x, y) >>> 29;
    return ROCK[1 + m + (f === 7) - (f === 0)];
  };
  const laneTone = (x, y) => {
    const f = hash(x >> 1, (y >> 1) + 41) >>> 29;
    return f === 0 ? LANE[0] : f === 7 ? LANE[1] : LANE[2];
  };

  /* a row of menhirs running away to the right: big and lit at the left, small and dim towards the horizon */
  function alignment(cv) {
    const seed = +cv.dataset.alignment || 1, W = 240, H = 44, ground = H - 4;
    cv.width = W; cv.height = H;
    const ctx = cv.getContext("2d");
    for (let x = 0; x < W; x++) {                       /* the field and its grass, as on the SEQ page */
      const n = hash(x, seed + 900);
      ctx.fillStyle = css(SURF); ctx.fillRect(x, ground, 1, 4);
      if ((n >>> 29) < 3) pset(ctx, x, ground - 1 - ((n >>> 8) & 1), (n >>> 12) & 1 ? DIM : STONE);
      if ((n >>> 27) === 0) pset(ctx, x, ground - 2, MID);
    }
    let x = 2;
    for (let k = 0; x < W - 4; k++) {
      const s = hash(k, seed), far = Math.min(1, x / W);
      const h = Math.max(6, Math.round((38 - 30 * far) * (0.7 + 0.3 * ((s >>> 8) & 255) / 255)));
      const w = Math.max(4, Math.round((13 - 8 * far) * (0.75 + 0.25 * ((s >>> 16) & 255) / 255)));
      const c = far < 0.3 ? THEME : far < 0.6 ? MID : far < 0.85 ? DIM : STONE;
      stone(ctx, x, ground + 1, w, h, h, c, (seed * 97 + k) | 0, 0);
      x += w + 3 + Math.round((12 - 9 * far) * (((s >>> 24) & 255) / 255));
    }
  }

  /* 16 SEQ step stones (SQ_BOX_W 12): each an outline in the unlit stone, filling with its share of the progress;
     the one being filled in ACCENT, the full ones in THEME */
  function steps(cv) {
    const bar = document.getElementById(cv.dataset.steps), N = 16, CW = 14, SH = 22;
    cv.width = N * CW; cv.height = SH + 2;
    const ctx = cv.getContext("2d");
    let last = -1;
    const draw = () => {
      const v = bar ? +bar.value || 0 : 0;
      if (v !== last) {
        last = v;
        ctx.clearRect(0, 0, cv.width, cv.height);
        const p = v / 100 * N;
        for (let i = 0; i < N; i++) {
          const x = i * CW + 1, part = Math.max(0, Math.min(1, p - i)), seed = 3 + i * 17;
          stone(ctx, x, SH + 1, 12, SH, SH, STONE, seed, 1);
          if (part > 0) stone(ctx, x, SH + 1, 12, SH, Math.max(1, Math.round(part * SH)), part < 1 ? ACCENT : THEME, seed, 0);
        }
      }
      requestAnimationFrame(draw);
    };
    draw();
  }

  const root = document.documentElement.style;
  try {
    root.setProperty("--rock-tex", `url(${tile(64, 64, rockTone)})`);
    root.setProperty("--lane-tex", `url(${tile(64, 64, laneTone)})`);
  } catch (_) { /* a canvas that cannot export: the plain colours stand in */ }
  document.querySelectorAll("canvas[data-alignment]").forEach(alignment);
  document.querySelectorAll("canvas[data-steps]").forEach(steps);
})();
