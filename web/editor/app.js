/* SPDX-License-Identifier: GPL-3.0-only
   Carnac web editor: the page. Every control comes from params.js (gen_params.py, read from the firmware's tables);
   nothing here names a CC number. Values are held as the firmware stores them (what an NRPN sends); a CC is worked
   out from that value the way ui_sm_cc reads it back (a list in equal bands of 0..127).

   RECEIVE asks the FM1 for the whole sound (sysex.js); with GLOBAL CC OUT on, the panel's moves come back as CC /
   NRPN and every load sends the whole sound. A control neither moved nor received since the page opened is drawn
   cold. NRPN numbers: params.js nrpn_map (src/nrpn_map.h, fixed). */
(function () {
  "use strict";
  const P = window.CARNAC_PARAMS, M = window.CarnacMidi, X = window.CarnacSysex;
  const L = P.lists, C = P.consts;
  const $ = (id) => document.getElementById(id);
  const PAGE = {};
  P.pages.forEach((p) => { PAGE[p.id] = p; });
  const CC_OF = {};
  P.cc_map.forEach((m) => { CC_OF[m.page + ":" + m.item] = m.cc; });
  const WAVES = P.waves.banks.concat(P.waves.pcm);
  const NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const noteName = (n) => NOTE[n % 12] + (Math.floor(n / 12) - 1);
  const ARCH = {};
  P.archs.forEach((a) => { ARCH[a.code] = a; });
  const STORE_KEY = "carnac-editor-v1";
  const NUM = {}, AT = {};                        // "PAGE:item" -> [msb, lsb] (the fixed numbers), and back
  P.nrpn_map.forEach((n) => { NUM[n.page + ":" + n.item] = [n.msb, n.lsb]; AT[n.msb + ":" + n.lsb] = [n.page, n.item]; });
  const ARCH_PLACE = (code) => Math.max(0, P.archs.findIndex((a) => a.code === code));   // (ARCH goes by list place)

  // ------------------------------------------------------------------------------------------- state
  const S = { values: {}, touched: {}, ch: 0, out: "none", mode: "cc", view: "sound", sel: {}, pg: {} };
  try {
    const kept = JSON.parse(localStorage.getItem(STORE_KEY) || "null");
    if (kept && kept.values) Object.assign(S, kept);
    if (!["sound", "mod", "seq", "setup"].includes(S.view)) S.view = S.tab === "seq" ? "seq" : "sound";
    delete S.tab;
    if (typeof S.sel !== "object" || !S.sel) S.sel = {};
    if (typeof S.pg !== "object" || !S.pg) S.pg = {};
  } catch (_) { /* no storage: this session only */ }
  let saveTimer = 0;
  function keep() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => {
      try { localStorage.setItem(STORE_KEY, JSON.stringify(S)); } catch (_) { /* private window */ }
    }, 300);
  }

  // ------------------------------------------------------------------------------------------- controls
  /* a control: { key, label, page, item | route+field, min, max, kind, names, perf, cc, nrpn, fmt, def }
     page/item: the NRPN's MSB / LSB (ui_sm_nrpn), and the CC map's key */
  const nrpnOf = (pid, item) => NUM[(pid === "HOME2" ? "HOME1" : pid) + ":" + item] || null;
  const isPerf = (pid) => PAGE[pid].store !== "engine" && pid !== "HOME1" && pid !== "HOME2";
  const SKIP_KNOB = new Set(["none", "perf", "src", "dest", "seq", "bpm"]);   // (ui_sm_knob / cc_one refuse these)
  const SKIP_ROW = new Set(["RK_VOICES", "RK_ROUTE", "RK_PERF_SLOT", "RK_RENAME", "RK_LINK"]);

  function signed(v) { const s = v - 64; return (s > 0 ? "+" : "") + s; }

  function knobCtl(pid, i, k, key) {
    let min = 0, max = 127, names = null;
    if (k.kind === "list") { max = k.n - 1; names = L[k.names] || null; }
    if (k.kind === "tune") max = 2 * C.OSC_COARSE_MAX;
    const d = { key: key || `${pid}:k${i}`, label: k.label, page: pid, item: i, min, max, kind: k.kind, names,
                perf: isPerf(pid), cc: CC_OF[pid + ":" + i], nrpn: nrpnOf(pid, i) };
    d.def = k.kind === "bip" ? 64 : k.kind === "tune" ? C.OSC_COARSE_ZERO : names ? 0 : 64;
    d.fmt = (v) => names ? (names[v] ?? v) : k.kind === "bip" ? signed(v) : k.kind === "tune" ? `${v - C.OSC_COARSE_ZERO} st`
      : k.kind === "note" ? noteName(v) : k.kind === "porta" && v === 0 ? "OFF" : String(v);
    return d;
  }

  function rowCtl(pid, r, ri, key) {
    const item = C.CCI_ROW + ri;
    const wave = r.rk === "RK_WAVE";
    const names = wave ? WAVES : (r.names && L[r.names]) || null;
    const max = wave ? WAVES.length - 1 : r.max;   // (past 127: a 14-bit NRPN, INT TEMPO to 255 BPM)
    const f = new Set(r.flags);
    const d = { key: key || `${pid}:r${ri}`, label: r.label, page: pid, item, min: r.min, max, kind: names ? "list" : "row",
                names, wave, bool: r.names === "BOOL_NAMES", bip: f.has("RF_BIPOLAR") || r.rk === "RK_FINE", arch: r.rk === "RK_ARCH", perf: isPerf(pid), cc: CC_OF[pid + ":" + item],
                nrpn: nrpnOf(pid, item), mask: r.mask };
    d.def = f.has("RF_BIPOLAR") || r.rk === "RK_FINE" ? Math.max(r.min, Math.min(max, 64)) : r.min;
    d.fmt = (v) => names ? (names[v] ?? v) : r.rk === "RK_FINE" ? `${signed(v)} ct` : f.has("RF_BIPOLAR") ?
      (pid === "TUNING" ? `${signed(v)} ct` : signed(v)) : f.has("RF_NOTE") ? noteName(v) : f.has("RF_CHANNEL") ?
      `CH ${v + 1}` : f.has("RF_BPM") ? `${C.MIDI_CLOCK_BPM_BASE + v} BPM` : String(v);
    return d;
  }

  function mfxKnobCtl(type, i) {
    const k = P.knob_tables.MFX_KNOBS[type][i];
    const d = knobCtl("AMP", C.CCI_MFX + i, k, `MFX:${type}:k${i}`);
    d.cc = CC_OF["AMP:" + (C.CCI_MFX + i)];
    return d;
  }

  /* MFX row ri (of P.mfx.rows; 0 is the type): NRPN AMP 21 + ri - 1, the type's own rows only */
  function mfxRowCtl(t, r, ri) {
    const d = rowCtl("AMP", r, ri, `MFX:${t}:r${ri}`);
    d.item = C.CCI_MFX_ROW + ri - 1;
    d.cc = undefined;
    d.nrpn = nrpnOf("AMP", d.item);
    d.mask = undefined;
    return d;
  }

  function routeCtl(i, field) {
    const F = [["SOURCE", "MSRC_NAMES"], ["VIA", "MSRC_NAMES"], ["DEST", "MDEST_NAMES"], ["AMOUNT", null], ["VIA DEPTH", null]][field];
    const names = F[1] ? L[F[1]] : null;
    const d = { key: `MOD:${i}:${field}`, label: F[0], route: i, field, min: 0, max: names ? names.length - 1 : 127,
                kind: names ? "list" : field === 3 ? "bip" : "raw", names, perf: false, cc: undefined,
                nrpn: [P.nrpn_route_msb + i, field] };
    d.def = field === 3 ? 64 : 0;
    d.fmt = (v) => names ? names[v] : field === 3 ? signed(v) : String(v);
    return d;
  }

  const val = (d) => (d.key in S.values ? S.values[d.key] : d.def);

  // the CC value that ui_sm_cc reads back as v: the bottom of v's band, rounded up into it
  function ccValue(d, v) {
    const n = d.max - d.min + 1, i = v - d.min;
    return n === 128 ? i : Math.min(127, Math.ceil((i * 128) / n));
  }

  function sendNow(d, v) {
    const ch = S.ch;
    const what = `${d.label} = ${d.fmt(v)}`;
    if (d.route !== undefined) return M.nrpn(ch, d.nrpn[0], d.nrpn[1], v, `ROUTE ${d.route + 1} ${what}`);
    if (d.arch) {                                  // ARCH: its place in the list, never the stored code
      const pl = ARCH_PLACE(v);
      if (S.mode === "cc" && d.cc !== undefined) return M.cc(ch, d.cc, Math.ceil((pl * 128) / P.archs.length), `CC ${d.cc}: ${what}`);
      return d.nrpn ? M.nrpn(ch, d.nrpn[0], d.nrpn[1], pl, `NRPN ${d.nrpn[0]}:${d.nrpn[1]}: ${what}`) : false;
    }
    if (S.mode === "cc" && d.cc !== undefined) return M.cc(ch, d.cc, ccValue(d, v), `CC ${d.cc}: ${what}`);
    if (d.nrpn && v > 127) return M.nrpn14(ch, d.nrpn[0], d.nrpn[1], v, `NRPN ${d.nrpn[0]}:${d.nrpn[1]}: ${what}`);
    if (d.nrpn && d.max > 127) return M.nrpn14(ch, d.nrpn[0], d.nrpn[1], v, `NRPN ${d.nrpn[0]}:${d.nrpn[1]}: ${what}`);
    if (d.nrpn) return M.nrpn(ch, d.nrpn[0], d.nrpn[1], v, `NRPN ${d.nrpn[0]}:${d.nrpn[1]}: ${what}`);
    return false;
  }

  // a dragged slider sends at most every 15 ms, always its last value
  const pending = new Map();
  let flushTimer = 0;
  function sendSoon(d, v) {
    pending.set(d.key, [d, v]);
    if (!flushTimer) flushTimer = setTimeout(() => {
      flushTimer = 0;
      for (const [dd, vv] of pending.values()) sendNow(dd, vv);
      pending.clear();
    }, 15);
  }

  // ------------------------------------------------------------------------------------------- context
  const arch = () => { const a = S.values["HOME1:r0"]; return ARCH[a] ? a : P.archs[1].code; };   // DUAL OSC until set
  const mfxType = () => S.values["AMP:mt"] || 0;
  const flavour = (n) => S.values[`FILTER${n}:r1`] || 0;
  const shows = (d) => d.mask === undefined || ((d.mask >> arch()) & 1) === 1;

  // ------------------------------------------------------------------------------------------- drawing
  /* The page shows the whole sound at once, as modules (stone tablets) in the order the signal runs: VOICE, the
     oscillators, the mixer and noise, the filters, AMP and MFX, the envelopes, the LFOs, the effects, then the MOD
     routes across the foot. Knobs are moon dials (viz.js), lists are chips, two-way rows are toggles. SEQUENCE (seq.js)
     and SETUP (tuning, zones, program, GLOBAL) are views of their own. */
  const V = window.CarnacViz;
  let mirrors = new Map();          // key -> [redraw fns] (a control drawn twice, as the sends on AMP and FX)
  let VIZ = [];                     // the pictures: { cv, draw(t), anim, seen }
  let dirty = true;                 // a value moved: redraw every picture on the next frame
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
  const seedOf = (s) => { let h = 7; for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) | 0; return h & 0xFFFF; };

  function meta(d) {
    const parts = [];
    if (d.cc !== undefined) parts.push(`CC ${d.cc}`);
    if (d.nrpn) parts.push(`NRPN ${d.nrpn[0]}:${d.nrpn[1]}`);
    if (d.perf) parts.push("L1 CH");
    return parts.join(" · ");
  }

  /* a value set from the page: kept, drawn everywhere it shows, sent */
  function setVal(d, v, after) {
    v = Math.max(d.min, Math.min(d.max, v | 0));
    if (S.values[d.key] === v && S.touched[d.key]) return;
    S.values[d.key] = v;
    S.touched[d.key] = 1;
    redrawKey(d.key);
    dirty = true;
    if (!d.local) sendSoon(d, v);
    keep();
    if (after) after(v);
  }
  function mirror(d, draw) {
    if (!mirrors.has(d.key)) mirrors.set(d.key, []);
    draw.owner = owner;
    mirrors.get(d.key).push(draw);
    draw();
  }
  const gone = (f) => f.owner && f.owner.node && !f.owner.node.isConnected;
  function redrawKey(key) {
    const fs = mirrors.get(key);
    if (!fs) return;
    for (let i = fs.length - 1; i >= 0; i--) if (gone(fs[i])) fs.splice(i, 1);
    fs.forEach((f) => f());
  }
  const redrawAll = () => mirrors.forEach((_, k) => redrawKey(k));

  /* a knob: a moon dial. Drag up / right to raise it (Shift: finer), arrows and Page keys, double-click for its default */
  function knob(d, opt = {}) {
    const box = el("div", "knob" + (opt.small ? " small" : ""));
    box.tabIndex = 0;
    box.setAttribute("role", "slider");
    box.setAttribute("aria-label", opt.label || d.label);
    box.setAttribute("aria-valuemin", String(d.min));
    box.setAttribute("aria-valuemax", String(d.max));
    box.title = `${opt.label || d.label}\n${meta(d)}\ndrag up or right · Shift: fine · double-click: ${d.fmt(d.def)}`;
    const lab = el("div", "klab", opt.label || d.label);
    const cv = el("canvas", "dial");
    const out = el("output", "kval");
    box.append(lab, cv, out);
    const bip = d.bip || d.kind === "bip" || d.kind === "tune";
    const seed = seedOf(d.key);
    mirror(d, () => {
      const v = val(d), cold = !S.touched[d.key];
      out.textContent = d.fmt(v);
      box.classList.toggle("cold", cold);
      box.setAttribute("aria-valuenow", String(v));
      box.setAttribute("aria-valuetext", d.fmt(v));
      V.dial(cv, (v - d.min) / Math.max(1, d.max - d.min), bip, cold, seed);
    });
    if (opt.disabled) { box.classList.add("off"); box.tabIndex = -1; return box; }
    const set = (v) => setVal(d, v, opt.after || d.after);
    const pxPer = Math.max(1.1, Math.min(10, 170 / (d.max - d.min + 1)));
    let drag = null;
    cv.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      drag = { id: e.pointerId, x: e.clientX, y: e.clientY, v: val(d) };
      try { cv.setPointerCapture(e.pointerId); } catch (_) { /* (a pointer the browser no longer has) */ }
      box.classList.add("held");
      box.focus({ preventScroll: true });
      e.preventDefault();
    });
    cv.addEventListener("pointermove", (e) => {
      if (!drag || e.pointerId !== drag.id) return;
      const delta = ((drag.y - e.clientY) + (e.clientX - drag.x)) / pxPer * (e.shiftKey ? 0.25 : 1);
      set(Math.round(drag.v + delta));
    });
    const end = () => { drag = null; box.classList.remove("held"); };
    cv.addEventListener("pointerup", end);
    cv.addEventListener("pointercancel", end);
    cv.addEventListener("dblclick", () => set(d.def));
    box.addEventListener("keydown", (e) => {
      const big = Math.max(1, Math.round((d.max - d.min) / 16));
      const step = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: big, PageDown: -big }[e.key];
      if (step) { set(val(d) + step); e.preventDefault(); }
      else if (e.key === "Home") { set(d.min); e.preventDefault(); }
      else if (e.key === "End") { set(d.max); e.preventDefault(); }
      else if (e.key === "Delete" || e.key === "Backspace") { set(d.def); e.preventDefault(); }
    });
    box.addEventListener("wheel", (e) => {          // (only once the knob has been clicked: the page still scrolls)
      if (document.activeElement !== box) return;
      e.preventDefault();
      set(val(d) + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 1 : Math.max(1, Math.round((d.max - d.min) / 64))));
    }, { passive: false });
    return box;
  }

  /* a list: a select dressed as a stone chip */
  function pick(d, opt = {}) {
    const box = el("label", "pick" + (opt.wide ? " wide" : "") + (opt.bare ? " bare" : ""));
    if (!opt.bare) box.append(el("span", "plab", opt.label || d.label));
    const wrap = el("span", "sel");
    const sel = el("select");
    sel.setAttribute("aria-label", opt.label || d.label);
    for (let v = d.min; v <= d.max; v++) {
      const o = el("option", "", d.names[v] ?? String(v));
      o.value = String(v);
      sel.append(o);
    }
    sel.title = meta(d);
    wrap.append(sel);
    box.append(wrap);
    mirror(d, () => { sel.value = String(val(d)); box.classList.toggle("cold", !S.touched[d.key]); });
    if (opt.disabled) { sel.disabled = true; box.classList.add("off"); }
    sel.addEventListener("change", () => setVal(d, parseInt(sel.value, 10), opt.after));
    return box;
  }

  /* a two-way row (OFF / ON): a toggle */
  function toggle(d, opt = {}) {
    const b = el("button", "tog", opt.label || d.label);
    b.type = "button";
    b.title = meta(d);
    mirror(d, () => {
      b.setAttribute("aria-pressed", String(val(d) !== d.min));
      b.classList.toggle("cold", !S.touched[d.key]);
    });
    b.onclick = () => setVal(d, val(d) === d.min ? d.max : d.min, opt.after);
    return b;
  }

  /* a small bar for the MOD routes' amounts (a knob each would not fit sixteen rows on a phone) */
  function bar(d, opt = {}) {
    const box = el("label", "mbar");
    const top = el("span", "mtop");
    const out = el("output", "mval");
    top.append(el("span", "mlab", opt.label || d.label), out);
    const input = el("input");
    input.type = "range"; input.min = String(d.min); input.max = String(d.max); input.step = "1";
    input.setAttribute("aria-label", opt.aria || d.label);
    box.append(top, input);
    const bip = d.kind === "bip";
    mirror(d, () => {
      const v = val(d);
      input.value = String(v);
      out.textContent = d.fmt(v);
      const f = ((v - d.min) * 100) / Math.max(1, d.max - d.min);
      input.style.setProperty("--lo", `${bip ? Math.min(f, 50) : 0}%`);
      input.style.setProperty("--hi", `${bip ? Math.max(f, 50) : f}%`);
      box.classList.toggle("cold", !S.touched[d.key]);
    });
    input.addEventListener("input", () => setVal(d, parseInt(input.value, 10)));
    input.addEventListener("dblclick", () => setVal(d, d.def));
    return box;
  }

  /* the right control for a descriptor */
  function ctl(d, opt = {}) {
    if (d.names && d.bool) return toggle(d, opt);
    if (d.names) return pick(d, opt);
    return knob(d, opt);
  }

  /* a picture: drawn when values move, every frame while it is on screen if it moves on its own */
  const seenBy = "IntersectionObserver" in window ? new IntersectionObserver((es) => {
    es.forEach((e) => { const o = VIZ.find((x) => x.cv === e.target); if (o) { o.seen = e.isIntersecting; if (o.seen) o.draw(performance.now()); } });
  }) : null;
  function picture(cls, draw, anim) {
    const cv = el("canvas", "pic " + cls);
    cv.setAttribute("aria-hidden", "true");
    const o = { cv, anim: !!anim, seen: true, draw: (t) => draw(cv, t, o) };
    VIZ.push(o);
    if (seenBy) seenBy.observe(cv);
    return { cv, o };
  }
  let lastAnim = 0;
  function frame(t) {
    const anim = t - lastAnim > 32;                 // (the moving pictures at 30 frames a second)
    if (anim) lastAnim = t;
    if (VIZ.some((o) => !o.cv.isConnected)) VIZ = VIZ.filter((o) => o.cv.isConnected);
    for (const o of VIZ) if (o.seen && (dirty || (anim && o.anim))) o.draw(t);
    dirty = false;
    requestAnimationFrame(frame);
  }

  /* a module: a tablet of stone with its name, an optional picture, then its controls */
  /* a module: a tablet of stone with its name, then its picture and controls. mount() builds it and can build it again
     in place (a tab or a sub-page picked): the controls of the old one stop being drawn (mirror's owner) */
  let owner = null;
  function mount(make) {
    let sec = null;
    const build = () => { const prev = owner, h = {}; owner = h; const n = make(rebuild); h.node = n; owner = prev; return n; };
    const rebuild = () => { const n = build(); sec.replaceWith(n); sec = n; dirty = true; };
    sec = build();
    return sec;
  }
  function tablet(title, opt = {}) {
    const sec = el("section", "tablet" + (opt.cls ? " " + opt.cls : ""));
    const face = el("div", "face");
    const head = el("header", "thead");
    head.append(el("h2", "", title));
    face.append(head);
    sec.append(face);
    return { sec, face, head };
  }
  const knobRow = (ds, opt = {}) => { const g = el("div", "knobs" + (opt.small ? " small" : "")); ds.forEach((d) => g.append(knob(d, { small: opt.small }))); return g; };
  const chipRow = (nodes) => { const g = el("div", "chips"); nodes.forEach((n) => g.append(n)); return g; };

  /* the shape every SOUND module shares: [tabs] in the header (ENV 1 2 3), its picture, four knobs a page (the
     page's name stones at the header's right, for the rest), then its lists and toggles.
     cfg: { id, title, w (grid width class), tabs: [{ label, off }], body(ti) -> { sub, pic, knobs, pages, chips, absent, top } } */
  function moduleOf(cfg) {
    return mount((rebuild) => {
      const tabs = cfg.tabs || null;
      let ti = Math.min(S.sel[cfg.id] ?? 0, tabs ? tabs.length - 1 : 0);
      if (tabs && tabs[ti].off) ti = 0;
      const b = cfg.body(ti);
      const t = tablet(cfg.title, { cls: cfg.w + " m-" + cfg.id });
      if (tabs) {
        const tt = el("span", "ttabs");
        tabs.forEach((x, i) => {
          const bt = el("button", "ttab", x.label);
          bt.type = "button";
          bt.setAttribute("aria-pressed", String(i === ti));
          bt.setAttribute("aria-label", `${cfg.title} ${x.label}`);
          if (x.off) { bt.disabled = true; bt.title = `${A().name} has no ${cfg.title} ${x.label}`; }
          bt.onclick = () => { S.sel[cfg.id] = i; keep(); rebuild(); };
          tt.append(bt);
        });
        t.head.append(tt);
      }
      if (b.sub) t.head.append(el("span", "tsub", b.sub));
      if (b.absent) { t.sec.classList.add("absent"); t.face.append(el("p", "gone", b.absent)); return t.sec; }
      const ks = b.knobs || [];
      const pages = [];
      for (let i = 0; i < ks.length; i += 4) pages.push(ks.slice(i, i + 4));
      const pk = cfg.id + ":" + ti;
      const pi = Math.min(S.pg[pk] ?? 0, Math.max(0, pages.length - 1));
      if (pages.length > 1) {
        const names = b.pages || [];
        const pg = el("span", "pages");
        pages.forEach((_, i) => {
          const bt = el("button", "pgb", names[i] || (i ? "MORE" + (i > 1 ? " " + i : "") : "MAIN"));
          bt.type = "button";
          bt.setAttribute("aria-pressed", String(i === pi));
          bt.onclick = () => { S.pg[pk] = i; keep(); rebuild(); };
          pg.append(bt);
        });
        t.head.append(pg);
      }
      if (b.top) t.face.append(b.top);
      if (b.pic) t.face.append(b.pic);
      if (pages.length) {
        const row = knobRow(pages[pi]);
        for (let i = pages[pi].length; i < 4; i++) row.append(el("div", "knob ghost"));   // (four places, a page or not)
        t.face.append(row);
      }
      if (b.chips && b.chips.length) t.face.append(chipRow(b.chips));
      if (b.after) t.face.append(b.after);
      if (b.note) t.face.append(el("p", "gone", b.note));
      return t.sec;
    });
  }

  // ------------------------------------------------------------------------------------------- the sound's controls
  function knobsOf(pid) {
    const t = PAGE[pid].knobs;
    if (!t) return [];
    const name = t.split("[")[0];
    const tab = P.knob_tables[name];
    return t.endsWith("[arch]") ? tab[arch()] : tab;
  }
  /* a page's controls as descriptors: its knobs (those that send), and its rows (those the architecture shows) */
  function pageKnobs(pid, skip) {
    const out = [];
    knobsOf(pid).forEach((k, i) => {
      if (SKIP_KNOB.has(k.kind) || (skip && skip(k, i))) return;
      out.push(knobCtl(pid, i, k, PAGE[pid].knobs.endsWith("[arch]") ? `${pid}:${k.label}` : undefined));
    });
    return out;
  }
  function pageRows(pid, skip) {
    const out = [];
    PAGE[pid].rows.forEach((r, ri) => {
      if (SKIP_ROW.has(r.rk) || (skip && skip(r, ri))) return;
      const d = rowCtl(pid, r, ri);
      if (PAGE[pid].store === "engine" && !shows(d)) return;
      d.ri = ri;
      out.push(d);
    });
    return out;
  }
  const by = (ds, label) => ds.find((d) => d.label === label);
  const vOf = (ds, label, dflt) => { const d = by(ds, label); return d ? val(d) : dflt; };
  const nums = (ds) => ds.filter((d) => !d.names);
  const lists = (ds, opt) => ds.filter((d) => d.names).map((d) => ctl(d, opt ? opt(d) : {}));
  /* the rows named in `order` first, in that order, then the rest */
  const ordered = (ds, order) => ds.slice().sort((a, b) => { const i = order.indexOf(a.label), j = order.indexOf(b.label); return (i < 0 ? 99 : i) - (j < 0 ? 99 : j); });

  const NOISE_CC = new Set([115, 116, 117, 118, 119]);
  const noiseRow = (ri) => NOISE_CC.has(CC_OF["OSCMIX:" + (C.CCI_ROW + ri)]);
  const rerender = () => render();
  const A = () => ARCH[arch()];

  /* VOICE: the layer's architecture drawn as its signal path, and how it plays */
  const modVoice = () => moduleOf({ id: "voice", title: "VOICE", w: "w3", body() {
    const rows = pageRows("HOME1");
    const archD = by(rows, "ARCH");
    const pic = picture("arch", (cv, tm) => V.archMap(cv, A().id, tm), true).cv;
    return { sub: A().name, pic, knobs: nums(rows), chips: lists(rows, (d) => d === archD ? { after: rerender } : {}) };
  } });

  /* OSC 1 / 2: the bank's frames as stones and the scan between them */
  const modOsc = () => moduleOf({ id: "osc", title: "OSC", w: "w3", tabs: [{ label: "1" }, { label: "2", off: !A().osc2 }], body(ti) {
    const pid = "OSC" + (ti + 1), knobs = pageKnobs(pid), rows = pageRows(pid);
    const wave = rows.find((d) => d.wave);
    const pic = picture("osc", (cv) => V.osc(cv, val(knobs[0]))).cv;
    return { pic, knobs: knobs.concat(nums(rows)), pages: ["WAVE", "MORE"],
             chips: [wave ? pick(wave, { wide: true, label: "WAVE" }) : null].filter(Boolean).concat(lists(rows.filter((d) => d !== wave))) };
  } });

  /* MIXER: the mixer's voices as menhirs, and the noise (every architecture has it) */
  const modMixer = () => moduleOf({ id: "mixer", title: "MIXER", w: "w3", tabs: [{ label: "MIX" }, { label: "NOISE" }], body(ti) {
    const nli = PAGE.OSCMIX.rows.findIndex((r) => r.label === "NOISE LEVEL");
    const noiseLv = nli >= 0 ? rowCtl("OSCMIX", PAGE.OSCMIX.rows[nli], nli) : null;
    if (ti === 1) {
      const rows = PAGE.OSCMIX.rows.map((r, ri) => [CC_OF["OSCMIX:" + (C.CCI_ROW + ri)], r, ri])
        .filter(([cc]) => NOISE_CC.has(cc)).sort((x, y) => x[0] - y[0]).map(([, r, ri]) => rowCtl("OSCMIX", r, ri));
      const pic = picture("noise", (cv, tm) => V.noise(cv, vOf(rows, "NOISE LEVEL", 0) / 127, vOf(rows, "NOISE COLOUR", 64) / 127, tm), true).cv;
      return { sub: "every arch", pic, knobs: nums(rows), pages: ["NOISE", "MOD"], chips: lists(rows) };
    }
    const knobs = pageKnobs("OSCMIX", (k) => k.label === "NOISE LEVEL");
    const rows = pageRows("OSCMIX", (r, ri) => noiseRow(ri));
    const pic = picture("mix", (cv) => {
      const voices = [], bal = by(knobs, "OSC MIX"), b = bal ? val(bal) / 127 : 0;
      voices.push({ level: bal ? 1 - Math.max(0, b - 0.5) * 2 : 1 });
      if (A().osc2) voices.push({ level: bal ? Math.min(1, b * 2) : 1 });
      const sub = by(knobs, "SUB LEVEL"); if (sub) voices.push({ level: val(sub) / 127 });
      voices.push({ level: noiseLv ? val(noiseLv) / 127 : 0 });
      V.mix(cv, voices);
    }).cv;
    const legend = el("p", "legend", ["OSC1", A().osc2 ? "OSC2" : null, by(knobs, "SUB LEVEL") ? "SUB" : null, "NOISE"].filter(Boolean).join("  ·  "));
    const box = el("div", "picbox"); box.append(pic, legend);
    return { pic: box, knobs: knobs.concat(nums(rows)), chips: lists(rows) };
  } });

  /* FILTER 1 / 2 / MIX: the response of the flavour at its morph, cutoff and resonance */
  function filterParts(n) {
    const pid = "FILTER" + n, knobs = pageKnobs(pid), rows = pageRows(pid);
    const fl = by(rows, "FLAVOUR");
    knobs[0].label = L.MORPH_LABEL[fl ? val(fl) : 0] || knobs[0].label;
    const p = () => ({ cutoff: vOf(knobs, "CUTOFF", 64), res: vOf(knobs, "RESONANCE", 0), morph: val(knobs[0]), flav: fl ? val(fl) : 0, drive: vOf(rows, "DRIVE", 0) });
    return { knobs, rows, fl, p };
  }
  const modFilter = () => moduleOf({ id: "filter", title: "FILTER", w: "w3",
    tabs: [{ label: "1" }, { label: "2", off: !A().filter2 }, { label: "MIX", off: !A().fltmix }], body(ti, rb) {
    if (ti === 2) {
      const f1 = filterParts(1), f2 = A().filter2 ? filterParts(2) : null;
      const pic = picture("flt", (cv) => { const a = f1.p(); V.filter(cv, a.cutoff, a.res, a.morph, a.flav, a.drive, f2 ? f2.p() : null); }).cv;
      return { sub: A().id === "BINAURAL" ? "the two ears" : "F1 · F2 dotted", pic, knobs: pageKnobs("FLTMIX") };
    }
    const f = filterParts(ti + 1);
    const pic = picture("flt", (cv) => { const a = f.p(); V.filter(cv, a.cutoff, a.res, a.morph, a.flav, a.drive); }).cv;
    return { pic, knobs: f.knobs.concat(nums(f.rows)), pages: ["FILTER", "DRIVE"],
             chips: lists(f.rows, (d) => d === f.fl ? { after: rerender } : {}) };
  } });

  /* ENV 1 / 2 / 3: the barrow, its corners dragged, the other two faint behind; the light runs along it in real time */
  function envParts(n) {
    const pid = "ENV" + n, knobs = pageKnobs(pid), rows = pageRows(pid);
    const [dA, dD, dS, dR] = knobs;
    [dA, dD, dR].forEach((d) => { d.fmt = (v) => V.fmtT(V.envT(v)); });
    dS.fmt = (v) => `${Math.round((v * 100) / 127)}%`;
    const inv = by(rows, "INVERT");
    return { knobs, rows, dA, dD, dS, dR, inv: () => !!inv && val(inv) !== 0 };
  }
  const modEnv = () => moduleOf({ id: "env", title: "ENV", w: "w4", tabs: [{ label: "1" }, { label: "2" }, { label: "3" }], body(ti) {
    const e = envParts(ti + 1), others = [1, 2, 3].filter((n) => n !== ti + 1).map(envParts);
    const p = picture("env", (cv, tm, o) => {
      const a = val(e.dA), dd = val(e.dD), s = val(e.dS), r = val(e.dR);
      const G0 = V.envGeom(Math.round(cv.clientWidth / 2) || 100, Math.round(cv.clientHeight / 2) || 40, a, dd, s, r);
      o.G = V.env(cv, a, dd, s, r, e.inv(), V.envHead(G0, a, dd, s, r, tm / 1000),
                  others.map((x) => ({ a: val(x.dA), d: val(x.dD), s: val(x.dS), r: val(x.dR), inv: x.inv() })));
    }, true);
    envDrag(p.cv, p.o, e.dA, e.dD, e.dS, e.dR, e.inv);
    return { sub: ["AMP", "FILTER · PITCH", "SCAN · MOD"][ti], pic: p.cv,
             knobs: e.knobs.concat(ordered(nums(e.rows), ["VEL > LEVEL", "VEL > ATTACK", "KEY > TIME", "AUTO DECAY", "RETRIG RATE"])),
             pages: ["ADSR", "VEL · KEY", "RETRIG"], chips: lists(e.rows) };
  } });
  /* dragging the barrow: the attack's peak sideways, the decay's corner sideways and up and down (the sustain), the
     release's end sideways. On a touch screen the picture lets the page scroll up and down, so sideways only */
  function envDrag(cv, o, dA, dD, dS, dR, inverted) {
    let which = null, pid = -1;
    const at = (e) => { const r = cv.getBoundingClientRect(); return [(e.clientX - r.left) * (cv.width / r.width), (e.clientY - r.top) * (cv.height / r.height)]; };
    cv.addEventListener("pointerdown", (e) => {
      const G = o.G;
      if (!G || e.button !== 0) return;
      const [x] = at(e);
      const c = [["a", G.xa], ["d", G.xd], ["r", G.xr]].map(([k, cx]) => [k, Math.abs(cx - x)]).sort((p, q) => p[1] - q[1])[0];
      which = c[1] < 10 ? c[0] : x < G.xa ? "a" : x < G.xs ? "d" : "r";
      pid = e.pointerId;
      try { cv.setPointerCapture(e.pointerId); } catch (_) { /* (a pointer the browser no longer has) */ }
      cv.classList.add("held");
    });
    cv.addEventListener("pointermove", (e) => {
      const G = o.G;
      if (!which || e.pointerId !== pid || !G) {
        if (G && !which) { const [x] = at(e); cv.style.cursor = Math.min(...[G.xa, G.xd, G.xr].map((cx) => Math.abs(cx - x))) < 6 ? "grab" : "default"; }
        return;
      }
      const [x, y] = at(e);
      if (which === "a") setVal(dA, V.segK((x - G.x0) / G.seg));
      if (which === "d") {
        setVal(dD, V.segK((x - G.xa) / G.seg));
        if (e.pointerType !== "touch") {
          let lv = (G.g - 1 - y) / (G.g - 1 - G.top);
          if (inverted()) lv = 1 - lv;
          setVal(dS, Math.round(lv * 127));
        }
      }
      if (which === "r") setVal(dR, V.segK((x - G.xs) / G.seg));
    });
    const end = () => { which = null; cv.classList.remove("held"); };
    cv.addEventListener("pointerup", end);
    cv.addEventListener("pointercancel", end);
  }

  /* LFO 1 / 2 / 3: two cycles of the shape, the playhead running at the rate, the moon at its phase */
  const lfoPhase = [0, 0, 0, 0], lfoT = [0, 0, 0, 0];
  const modLfo = () => moduleOf({ id: "lfo", title: "LFO", w: "w4", tabs: [{ label: "1" }, { label: "2" }, { label: "3" }], body(ti) {
    const n = ti + 1, pid = "LFO" + n;
    const knobs = pageKnobs(pid), rows = pageRows(pid), all = knobs.concat(rows);
    const sync = by(rows, "CLOCK SYNC"), rate = by(knobs, "RATE");
    const synced = () => !!sync && val(sync) !== 0;
    rate.fmt = (v) => { if (synced()) return `DIV ${Math.floor(v / 16) + 1}/8`; const hz = V.lfoHz(v); return hz < 10 ? `${hz.toFixed(2)} Hz` : `${hz.toFixed(1)} Hz`; };
    const fade = by(all, n === 1 ? "RISE" : "FADE");
    const pic = picture("lfo", (cv, tm) => {
      const dt = Math.min(0.1, Math.max(0, (tm - (lfoT[n] || tm)) / 1000));
      lfoT[n] = tm;
      const hz = synced() ? 0.25 * Math.pow(2, Math.floor(val(rate) / 16) * 0.5) : Math.min(6, V.lfoHz(val(rate)));
      lfoPhase[n] += hz * dt;
      V.lfo(cv, vOf(all, "SHAPE", 0), vOf(all, "DELAY", 0), fade ? (val(fade) - 64) / 64 : 0, lfoPhase[n], true);
    }, true).cv;
    return { sub: n === 1 ? "global · per layer" : "per voice", pic,
             knobs: knobs.concat(ordered(nums(rows), ["SHAPE", "DELAY", "PHASE", "WHEEL>DEPTH", "OSC1 AMT", "OSC2 AMT"])),
             pages: n === 1 ? ["LFO", "SHAPE", "AMOUNTS"] : ["LFO", "MORE"], chips: lists(rows) };
  } });

  /* AMP: the drive's curve and the tremolo; MFX: the layer's effect */
  const modAmp = () => moduleOf({ id: "amp", title: "AMP", w: "w4", tabs: [{ label: "DRIVE" }, { label: "MFX" }], body(ti) {
    if (ti === 0) {
      const knobs = pageKnobs("AMP"), rows = pageRows("AMP");
      const pic = picture("amp", (cv, tm) => V.drive(cv, vOf(knobs, "DRIVE", 0), vOf(rows, "DRV MODEL", 0), vOf(knobs, "TREMOLO", 0), tm), true).cv;
      return { pic, knobs, chips: lists(rows) };
    }
    const ty = mfxType();
    const type = { key: "AMP:mt", label: "MFX", page: "AMP", item: C.CCI_MFX_TYPE, min: 0, max: C.MFX_OFFERED - 1, kind: "list",
                   names: P.mfx.types, perf: false, cc: CC_OF["AMP:" + C.CCI_MFX_TYPE], nrpn: nrpnOf("AMP", C.CCI_MFX_TYPE), def: 0 };
    type.fmt = (v) => P.mfx.types[v];
    const ks = [];
    for (let i = 0; i < 4; i++) if (P.knob_tables.MFX_KNOBS[ty][i].kind !== "none") ks.push(mfxKnobCtl(ty, i));
    const rows = [];
    P.mfx.rows.forEach((r, ri) => { if (ri && ((r.mask >> ty) & 1)) rows.push(mfxRowCtl(ty, r, ri)); });
    const top = chipRow([pick(type, { after: rerender, wide: true, label: "TYPE" })]);
    const knobsNow = () => { const o = {}; ks.forEach((d) => { o[d.label] = (val(d) - d.min) / Math.max(1, d.max - d.min); }); return o; };
    const pic = picture("mfx", (cv, tm) => V.mfx(cv, ty, P.mfx.types[ty], knobsNow(), tm), true).cv;
    return { sub: ty ? "" : "off", top, pic, knobs: ks.concat(nums(rows)), pages: ["MFX", "MORE"], chips: lists(rows) };
  } });

  /* FX: the delay's stones and the reverb's fog; MIX: the two and their cross-feeds */
  const modFx = () => moduleOf({ id: "fx", title: "FX", w: "w4", tabs: [{ label: "DLY · REV" }, { label: "MIX" }], body(ti) {
    const knobs = pageKnobs("EFFECTS"), rows = pageRows("EFFECTS"), mixk = pageKnobs("MIX");
    const pic = picture("fx", (cv, tm) => V.fx(cv, vOf(knobs, "DELAY TIME", 40), vOf(knobs, "DELAY FDBK", 40), vOf(knobs, "REVERB TIME", 60),
      vOf(knobs, "REVERB DAMP", 60), mixk.length ? val(mixk[mixk.length - 1]) / 127 : 0.5, tm), true).cv;
    if (ti === 1) return { sub: "L1 CH", pic, knobs: mixk, pages: ["MIX", "WET"] };
    return { sub: "L1 CH", pic, knobs: knobs.concat(nums(rows)), pages: ["DLY · REV", "TAPE"], chips: lists(rows) };
  } });

  // ------------------------------------------------------------------------------------------- MOD: the ley lines
  const SRC = L.MSRC_NAMES, DST = L.MDEST_NAMES;
  function routesNow() {
    const out = [];
    for (let i = 0; i < C.NROUTE; i++) {
      const f = [0, 1, 2, 3, 4].map((k) => routeCtl(i, k));
      const src = val(f[0]), dst = val(f[2]);
      if (src && dst) out.push({ i, src, via: val(f[1]), dst, amt: (val(f[3]) - 64) / 64, sel: S.modSel === i });
    }
    return out;
  }
  /* the map: every SOURCE (big) or only those in use (the SOUND page's glance), the DESTs in use */
  function leyPicture(big) {
    const p = picture(big ? "leys big" : "leys", (cv, tm, o) => {
      const rs = routesNow();
      const srcs = big ? SRC.map((_, i) => i).slice(1) : [...new Set(rs.flatMap((r) => r.via ? [r.src, r.via] : [r.src]))].sort((a, b) => a - b);
      const dsts = [...new Set(rs.map((r) => r.dst))].sort((a, b) => a - b);
      const rowsN = Math.max(big ? 6 : 3, srcs.length, dsts.length);
      const h = (rowsN * (big ? 10 : 8) + 12) * 2;
      if (cv.style.height !== h + "px") cv.style.height = h + "px";
      o.hits = V.leys(cv, rs, srcs, dsts, SRC, DST, tm, big);
    }, true);
    p.cv.addEventListener("click", (e) => {
      const r = p.cv.getBoundingClientRect(), x = (e.clientX - r.left) * (p.cv.width / r.width), y = (e.clientY - r.top) * (p.cv.height / r.height);
      let best = null, bd = 6;
      (p.o.hits || []).forEach((h) => h.pts.forEach(([px, py]) => { const dd = Math.hypot(px - x, py - y); if (dd < bd) { bd = dd; best = h.i; } }));
      if (!big) { S.view = "mod"; if (best !== null) S.modSel = best; keep(); render(); return; }
      S.modSel = best;
      keep();
      document.querySelectorAll(".route").forEach((row, i) => row.classList.toggle("sel", i === S.modSel));
      dirty = true;
    });
    return p.cv;
  }
  const modModGlance = () => mount(() => {
    const t = tablet("MOD", { cls: "w8 m-modg" });
    const n = routesNow().length;
    t.head.append(el("span", "tsub", `${n} of ${C.NROUTE} ley lines`));
    const go = el("button", "pgb", "OPEN THE MAP ›");
    go.type = "button";
    go.onclick = () => { S.view = "mod"; keep(); render(); };
    t.head.append(go);
    t.face.append(leyPicture(false));
    return t.sec;
  });

  /* MOD view: the whole map, every route under it. Routes are written by place; the panel keeps them packed and sorted
     by SOURCE (it tidies them when its cursor leaves MOD), so keep them so here */
  function modMod() {
    const t = tablet("LEY LINES", { cls: "w12 m-mod" });
    t.head.append(el("span", "tsub", "16 routes · SOURCE × VIA → DEST · keep them packed, sorted by SOURCE · click a line"));
    const all = el("button", "pgb", "ALL 16");
    all.type = "button";
    all.setAttribute("aria-pressed", String(!!S.modAll));
    all.title = "Show every route, or only those in use and the next free one";
    t.head.append(all);
    t.face.append(leyPicture(true));
    const list = el("div", "routes");
    const rows = [], usedOf = [];
    /* the routes in use, and the first free one (the panel's NEW ROUTE); every route with ALL 16 */
    const showRows = () => {
      if (rows.length < C.NROUTE) return;
      const free = usedOf.findIndex((u) => !u());
      rows.forEach((r, i) => { r.hidden = !(S.modAll || usedOf[i]() || i === free); r.classList.toggle("sel", i === S.modSel); });
    };
    all.onclick = () => { S.modAll = !S.modAll; all.setAttribute("aria-pressed", String(S.modAll)); keep(); showRows(); };
    for (let i = 0; i < C.NROUTE; i++) {
      const f = [0, 1, 2, 3, 4].map((k) => routeCtl(i, k));
      const row = el("div", "route");
      const used = () => val(f[0]) !== 0 && val(f[2]) !== 0;
      rows.push(row); usedOf.push(used);
      row.append(el("span", "rnum", String(i + 1).padStart(2, "0")));
      row.append(pick(f[0], { bare: true }), el("span", "rx", "×"), pick(f[1], { bare: true }), el("span", "rx", "→"), pick(f[2], { bare: true }));
      row.append(bar(f[3], { label: "AMT", aria: `route ${i + 1} amount` }), bar(f[4], { label: "VIA", aria: `route ${i + 1} via depth` }));
      row.addEventListener("focusin", () => { if (S.modSel !== i) { S.modSel = i; showRows(); dirty = true; } });
      const mark = () => { row.classList.toggle("used", used()); showRows(); };
      [f[0], f[2]].forEach((d) => mirror(d, mark));
      list.append(row);
    }
    showRows();
    t.face.append(list);
    return [t.sec];
  }

  /* SETUP: what is the performance's or the instrument's, not the sound's */
  /* a stone-tablet menu, as the panel's: a row a setting, its name at the left and its value at the right; the row of
     a setting that is ON lit as the panel's cursor row. A click toggles a two-way row and steps a list (Shift: back) */
  function menu(ds) {
    const m = el("div", "menu");
    ds.forEach((d) => {
      const b = el("button", "mrow");
      b.type = "button";
      b.title = meta(d);
      const lab = el("span", "mlab2", d.label), v = el("span", "mv");
      b.append(lab, v);
      mirror(d, () => {
        const x = val(d);
        v.textContent = d.fmt(x);
        b.setAttribute("aria-pressed", String(d.bool ? x !== d.min : false));
        b.classList.toggle("cold", !S.touched[d.key]);
      });
      b.onclick = (e) => {
        const x = val(d);
        if (d.bool) setVal(d, x === d.min ? d.max : d.min, d.after);
        else setVal(d, e.shiftKey ? (x <= d.min ? d.max : x - 1) : (x >= d.max ? d.min : x + 1), d.after);
      };
      m.append(b);
    });
    return m;
  }

  /* SETUP: what is the performance's or the instrument's, not the sound's: the same tablets as SOUND, every number and
     list a knob, the switches and THEME stone menus */
  const rowsOf = (pid) => pageRows(pid);
  /* the built-in tunings' cents a step from the root (src/dsp/tuning.c TUNING_TABLE, OVERTONE .. WERCKMEISTER III) */
  const TUNING_CENTS = [
    [0, 5, 4, -2, -14, -29, -49, 2, 41, 6, -31, -12], [0, 12, 4, 16, -14, -2, -10, 2, 14, -16, 18, -12],
    [0, 14, 4, -6, 8, -2, 12, 2, -8, 6, -4, 10], [0, 19, 31, 16, -14, -2, -17, 2, 14, -16, -31, -12],
    [0, 0, 0, 0, -50, 0, 0, 0, 0, 0, 0, -50], [0, -24, -7, 10, -14, 3, -21, -3, -27, -10, 7, -17],
    [0, -10, -8, -6, -10, -2, -12, -4, -8, -12, -4, -8],
  ];
  const modTuning = () => moduleOf({ id: "tuning", title: "TUNING", w: "w3", body() {
    const rows = rowsOf("TUNING");
    const pic = picture("tune", (cv) => {
      const t = vOf(rows, "TUNING", 0), table = TUNING_CENTS[t - 1];   // (EQUAL and the USER tables: no offsets the editor knows)
      V.tuning(cv, table || Array(12).fill(0), vOf(rows, "MASTER TUNE", 64), vOf(rows, "BEND RANGE", 2));
    }).cv;
    return { sub: "L1 CH", pic, knobs: rows };
  } });
  const modZones = () => moduleOf({ id: "zones", title: "ZONES", w: "w3", body() {
    const rows = rowsOf("HOME3");
    const pic = picture("zones", (cv) => V.zones(cv, vOf(rows, "SPLIT NOTE", 60), vOf(rows, "SPLIT FADE", 0), vOf(rows, "VEL SPLIT", 64), vOf(rows, "VEL FADE", 0))).cv;
    return { sub: "DUAL · SPLIT", pic, knobs: rows };
  } });
  const globalRows = () => {
    const rows = rowsOf("GLOBAL");
    const ch1 = by(rows, "MIDI CH LAYER1");
    if (ch1) ch1.after = (v) => { S.ch = v; syncHeader(); };
    const th = rows.find((d) => PAGE.GLOBAL.rows[d.ri].rk === "RK_THEME");
    if (th) th.after = () => applyTheme();
    return rows;
  };
  const modClock = () => moduleOf({ id: "clock", title: "CLOCK", w: "w3", body() {
    const rows = globalRows();
    const tempo = by(rows, "INT TEMPO"), src = by(rows, "CLOCK SRC");
    const pic = picture("clock", (cv, tm) => V.sarsen(cv, tempo ? C.MIDI_CLOCK_BPM_BASE + val(tempo) : 120, tm, src && val(src) !== 0), true).cv;
    return { sub: "the sarsen circle", pic, knobs: [tempo, src, by(rows, "CLOCK OUT")].filter(Boolean) };
  } });
  const modVelocity = () => moduleOf({ id: "vel", title: "VELOCITY", w: "w3", body() {
    const rows = globalRows();
    const cur = by(rows, "VEL CURVE"), base = by(rows, "BASE VELOCITY");
    const pic = picture("velc", (cv) => V.velCurve(cv, cur ? val(cur) : 1, base ? val(base) : 64)).cv;
    return { sub: "in → out", pic, knobs: [cur, base].filter(Boolean) };
  } });
  const modMidi = () => moduleOf({ id: "midi", title: "MIDI", w: "w4", body() {
    const rows = globalRows();
    const knobs = ["MIDI CH LAYER1", "MIDI CH LAYER2", "CUSTOM CC1", "CUSTOM CC2"].map((l) => by(rows, l)).filter(Boolean);
    const sw = ["CC IN", "CC OUT", "PC IN", "PC OUT"].map((l) => by(rows, l)).filter(Boolean);
    return { sub: "the instrument", knobs, after: menu(sw),
             note: "A CUSTOM CC set to a mapped number takes it from the map (the FM1 shows a *). CC OUT sends the panel's moves and a dump at each load; CC IN off stops everything this editor sends." };
  } });
  const modProgram = () => moduleOf({ id: "prog", title: "PROGRAM", w: "w4", body() {
    const ds = P.pc_banks.map((b, i) => ({ key: "PC:" + b, label: b, min: 0, max: 127, kind: "raw", local: true, def: 0,
                                         fmt: (v) => String(v).padStart(3, "0"), bank: i }));
    const loads = el("div", "loads");
    ds.forEach((d) => {
      const b = el("button", "mrow load", "");
      b.type = "button";
      b.append(el("span", "mlab2", "LOAD"), el("span", "mv", d.label));
      b.title = `BANK ${d.bank}: CC 0 = ${d.bank}, CC 32 = 0, then the program` + (d.bank ? " (Layer 1's channel)" : " (every layer on the channel)");
      b.onclick = () => M.program(S.ch, d.bank, val(d), `BANK ${d.bank} (${d.label}) PROGRAM ${val(d)}`);
      loads.append(b);
    });
    return { sub: "call a stone by its number", knobs: ds, after: loads,
             note: "Turn to a number, then LOAD. A patch loads into every layer on the channel; a performance, sequence or arp pattern on Layer 1's channel only; an empty slot loads nothing." };
  } });
  const THEME_ROW = PAGE.GLOBAL.rows.findIndex((r) => r.rk === "RK_THEME");
  const modTheme = () => moduleOf({ id: "theme", title: "THEME", w: "w4", body() {
    const th = globalRows().find((d) => PAGE.GLOBAL.rows[d.ri].rk === "RK_THEME");
    const m = el("div", "menu two");
    if (th) for (let i = th.min; i <= th.max; i++) {
      const b = el("button", "mrow theme");
      b.type = "button";
      const cv = el("canvas", "swatch");
      const T = THEMES[i] || THEMES[0];
      V.swatch(cv, T[2], T[10], T[0]);
      b.append(cv, el("span", "mlab2", th.names[i]));
      mirror(th, () => b.setAttribute("aria-pressed", String(val(th) === i)));
      b.onclick = () => setVal(th, i, th.after);
      m.append(b);
    }
    return { sub: "the FM1's and this page's", after: m };
  } });

  const VIEWS = [
    { id: "sound", label: "SOUND", build: () => [modVoice(), modOsc(), modMixer(), modFilter(), modEnv(), modLfo(), modAmp(), modFx(), modModGlance()] },
    { id: "mod", label: "MOD", build: () => modMod() },
    { id: "seq", label: "SEQUENCE", build: () => window.CarnacSeq.build() },
    { id: "setup", label: "SETUP", build: () => [modTuning(), modZones(), modClock(), modVelocity(), modMidi(), modProgram(), modTheme()] },
  ];

  // ------------------------------------------------------------------------------------------- THEME
  /* the FM1's THEME list (THEME_NAMES' order), from src/ui_seq_draw.c: SQ_PAL surf text theme accent mid dim sel;
     SQ_PAL_LANE's bg, text, accent; SQ_STONE; SQ_ROCK[2] and [0] */
  const THEMES = [
    [[34, 16, 0], [255, 140, 30], [255, 110, 0], [255, 190, 90], [178, 98, 21], [107, 59, 13], [115, 50, 0], [110, 42, 0], [255, 200, 140], [255, 232, 200], [66, 34, 6], [48, 19, 0], [14, 5, 0]],
    [[36, 4, 4], [255, 70, 60], [230, 20, 20], [255, 140, 120], [178, 49, 42], [107, 29, 25], [104, 9, 9], [110, 12, 10], [255, 190, 180], [255, 218, 210], [66, 14, 12], [48, 7, 4], [14, 2, 0]],
    [[27, 4, 0], [250, 201, 81], [242, 170, 53], [255, 226, 122], [163, 103, 42], [92, 42, 18], [97, 35, 8], [91, 30, 7], [255, 233, 151], [255, 235, 162], [54, 17, 7], [38, 6, 0], [11, 2, 0]],
    [[12, 21, 34], [112, 173, 255], [90, 160, 255], [155, 198, 255], [78, 120, 178], [47, 73, 107], [41, 72, 115], [39, 69, 110], [191, 218, 255], [206, 226, 255], [28, 44, 66], [17, 30, 48], [5, 9, 14]],
    [[19, 28, 9], [155, 216, 94], [140, 210, 70], [185, 228, 143], [108, 151, 66], [65, 91, 40], [63, 95, 32], [60, 91, 30], [211, 238, 184], [220, 242, 200], [39, 56, 23], [26, 40, 13], [8, 12, 4]],
    [[32, 14, 25], [242, 125, 199], [240, 105, 190], [246, 164, 216], [169, 87, 139], [102, 53, 83], [108, 47, 86], [104, 45, 82], [249, 197, 230], [250, 210, 236], [63, 31, 51], [45, 20, 36], [13, 6, 10]],
    [[25, 15, 34], [194, 129, 255], [185, 110, 255], [213, 167, 255], [136, 90, 178], [82, 54, 107], [83, 50, 115], [80, 47, 110], [228, 199, 255], [234, 212, 255], [50, 32, 66], [35, 21, 48], [10, 6, 14]],
    [[18, 18, 18], [200, 200, 195], [150, 152, 150], [235, 235, 230], [140, 140, 136], [84, 84, 82], [68, 68, 68], [64, 65, 64], [225, 225, 222], [244, 244, 243], [52, 52, 51], [28, 29, 28], [8, 8, 8]],
    [[26, 16, 7], [235, 175, 105], [215, 135, 55], [250, 215, 160], [164, 122, 74], [99, 74, 44], [97, 61, 25], [92, 58, 24], [244, 211, 172], [251, 239, 225], [61, 46, 27], [41, 26, 10], [12, 7, 3]],
  ];
  const ROCKS = [
    [[14, 5, 0], [36, 14, 0], [48, 19, 0], [62, 25, 0], [76, 31, 0]], [[14, 2, 0], [36, 5, 2], [48, 7, 4], [62, 9, 6], [76, 12, 8]],
    [[11, 2, 0], [28, 4, 0], [38, 6, 0], [49, 10, 1], [61, 15, 2]], [[5, 9, 14], [13, 23, 36], [17, 30, 48], [22, 39, 62], [27, 48, 76]],
    [[8, 12, 4], [20, 30, 10], [26, 40, 13], [34, 51, 17], [42, 63, 21]], [[13, 6, 10], [34, 15, 27], [45, 20, 36], [58, 26, 46], [72, 31, 57]],
    [[10, 6, 14], [26, 16, 36], [35, 21, 48], [45, 27, 62], [55, 33, 76]], [[8, 8, 8], [21, 21, 21], [28, 29, 28], [37, 37, 37], [45, 46, 45]],
    [[12, 7, 3], [30, 19, 8], [41, 26, 10], [53, 33, 13], [64, 40, 16]],
  ];
  const LANES = [
    [[80, 28, 0], [140, 56, 0], [110, 42, 0]], [[80, 8, 6], [140, 18, 14], [110, 12, 10]], [[65, 15, 2], [120, 50, 13], [91, 30, 7]],
    [[28, 50, 80], [49, 88, 140], [39, 69, 110]], [[44, 66, 22], [77, 115, 38], [60, 91, 30]], [[75, 33, 60], [132, 58, 104], [104, 45, 82]],
    [[58, 35, 80], [102, 60, 140], [80, 47, 110]], [[46, 47, 46], [82, 84, 82], [64, 65, 64]], [[67, 42, 17], [118, 74, 30], [92, 58, 24]],
  ];
  let themeNow = -1;
  function applyTheme() {
    const i = Math.max(0, Math.min(THEMES.length - 1, THEME_ROW >= 0 ? (S.values["GLOBAL:r" + THEME_ROW] ?? 0) : 0));
    if (i === themeNow) return;
    themeNow = i;
    const T = THEMES[i], hx = (c) => "#" + c.map((v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0")).join("");
    const mixc = (a, b, k) => a.map((v, j) => v + (b[j] - v) * k);
    const names = ["surf", "text", "theme", "accent", "mid", "dim", "sel", "lane", "lane-text", "lane-hi", "stone", "rock", "groove"];
    const st = document.documentElement.style;
    names.forEach((n, j) => st.setProperty("--" + n, hx(T[j])));
    st.setProperty("--body", hx(mixc(T[3], [214, 206, 196], 0.3)));
    st.setProperty("--soft", hx(mixc(T[3], T[4], 0.55)));
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.content = "#000000";
    V.readPalette();
    try {
      const tx = V.tiles(ROCKS[i], LANES[i]);
      st.setProperty("--rock-tex", `url(${tx.rock})`);
      st.setProperty("--lane-tex", `url(${tx.lane})`);
    } catch (_) { /* a canvas that cannot export: the plain colours stand in */ }
    redrawAll();
    drawBanner();
    if (window.CarnacSeq && window.CarnacSeq.repaint) window.CarnacSeq.repaint();
    dirty = true;
  }
  function drawBanner() {
    const b = document.querySelector(".brand h1 .draft"), bar = $("banner");   // (the wordmark's end, not the heading's box)
    V.alignment(bar, 5, b ? b.getBoundingClientRect().right - bar.getBoundingClientRect().left + 14 : 0);
  }

  // ------------------------------------------------------------------------------------------- send all
  /* every control the current architecture and MFX type show, the ones that change the others first */
  function allControls(onlyTouched) {
    const list = [];
    const add = (d) => { if (!onlyTouched || S.touched[d.key]) list.push(d); };
    const rows = (pid, always) => PAGE[pid].rows.forEach((r, ri) => {
      if (SKIP_ROW.has(r.rk)) return;
      const d = rowCtl(pid, r, ri);
      if (PAGE[pid].store !== "engine" || shows(d) || (always && always(ri))) add(d);
    });
    const knobs = (pid, skip) => knobsOf(pid).forEach((k, i) => {
      if (SKIP_KNOB.has(k.kind) || (skip && skip(k))) return;
      add(knobCtl(pid, i, k, PAGE[pid].knobs.endsWith("[arch]") ? `${pid}:${k.label}` : undefined));
    });
    rows("HOME1");                                                  // ARCH first: the pages follow it
    const a = ARCH[arch()];
    add(Object.assign({ key: "AMP:mt", label: "MFX", page: "AMP", item: C.CCI_MFX_TYPE, min: 0, max: C.MFX_OFFERED - 1,
                        names: P.mfx.types, cc: CC_OF["AMP:" + C.CCI_MFX_TYPE], nrpn: nrpnOf("AMP", C.CCI_MFX_TYPE), def: 0,
                        fmt: (v) => P.mfx.types[v] }));
    for (const pid of ["OSC1", "OSC2", "OSCMIX", "FILTER1", "FILTER2", "FLTMIX", "ENV1", "ENV2", "ENV3", "LFO1", "LFO2", "LFO3", "AMP"]) {
      if ((pid === "OSC2" && !a.osc2) || (pid === "FILTER2" && !a.filter2) || (pid === "FLTMIX" && !a.fltmix)) continue;
      if (pid === "OSCMIX") { knobs(pid, (k) => k.label === "NOISE LEVEL"); rows(pid, noiseRow); continue; }
      rows(pid); knobs(pid);
    }
    for (let i = 0; i < 4; i++) if (P.knob_tables.MFX_KNOBS[mfxType()][i].kind !== "none") add(mfxKnobCtl(mfxType(), i));
    P.mfx.rows.forEach((r, ri) => { if (ri && ((r.mask >> mfxType()) & 1)) add(mfxRowCtl(mfxType(), r, ri)); });
    for (let i = 0; i < C.NROUTE; i++) for (let f = 0; f < 5; f++) add(routeCtl(i, f));
    knobs("EFFECTS"); rows("EFFECTS"); knobs("MIX"); rows("TUNING"); rows("HOME3");
    return list;
  }

  let sending = false;
  async function sendAll(onlyTouched) {
    if (sending) return;
    const list = allControls(onlyTouched);
    if (!list.length) { status("Nothing edited yet."); return; }
    if (!onlyTouched && !confirm(`Send all ${list.length} parameters to channel ${S.ch + 1}? Controls never moved go at their defaults.`)) return;
    sending = true;
    for (let i = 0; i < list.length; i++) {
      sendNow(list[i], val(list[i]));
      status(`Sending ${i + 1} / ${list.length}`);
      await new Promise((r) => setTimeout(r, 6));                   // (the FM1 takes 32 waiting controllers at most)
    }
    sending = false;
    status(`Sent ${list.length} parameters.`);
  }

  // ------------------------------------------------------------------------------------------- receive
  /* what the FM1 sends: a dump (RECEIVE, a load, CC OUT turning on) and CC OUT's CCs and NRPNs. A value lands on the
     control the panel's (page, item) is; the layer whose channel the editor is on (Layer 1's for the performance) */
  function ctlKey(pid, item) {
    if (pid === "AMP" && item >= C.CCI_MFX) {
      if (item === C.CCI_MFX_TYPE) return "AMP:mt";
      return item < C.CCI_MFX_TYPE ? `MFX:${mfxType()}:k${item - C.CCI_MFX}` : `MFX:${mfxType()}:r${item - C.CCI_MFX_ROW + 1}`;
    }
    if (item < C.CCI_ROW) {
      const t = PAGE[pid].knobs;
      if (t && t.endsWith("[arch]")) { const k = knobsOf(pid)[item]; return k ? `${pid}:${k.label}` : null; }
      return `${pid}:k${item}`;
    }
    return `${pid}:r${item - C.CCI_ROW}`;
  }
  let reg = null;                                 // key -> control (every control the current ARCH and MFX show)
  const registry = () => {
    if (!reg) {
      reg = new Map();
      allControls(false).forEach((d) => reg.set(d.key, d));
      PAGE.GLOBAL.rows.forEach((r, ri) => { if (!SKIP_ROW.has(r.rk)) { const d = rowCtl("GLOBAL", r, ri); reg.set(d.key, d); } });   // (received, never in SEND ALL)
    }
    return reg;
  };
  /* one value: exact (an NRPN or a dump entry) or a CC's 0..127 */
  function landValue(pid, item, v, exact) {
    if (pid === "HOME1" && item === C.CCI_ROW && PAGE.HOME1.rows[0].rk === "RK_ARCH") {
      const pl = exact ? v : Math.floor((v * P.archs.length) / 128);
      S.values["HOME1:r0"] = P.archs[Math.min(pl, P.archs.length - 1)].code;
      S.touched["HOME1:r0"] = 1; reg = null; return true;
    }
    if (pid === "AMP" && item === C.CCI_MFX_TYPE) {
      S.values["AMP:mt"] = exact ? Math.min(v, C.MFX_OFFERED - 1) : Math.floor((v * C.MFX_OFFERED) / 128);
      S.touched["AMP:mt"] = 1; reg = null; return true;
    }
    const key = ctlKey(pid, item), d = key && registry().get(key);
    if (!d) return false;
    const n = d.max - d.min + 1;
    S.values[key] = exact ? Math.max(d.min, Math.min(d.max, v)) : d.min + Math.floor((v * n) / 128);
    S.touched[key] = 1;
    return true;
  }
  function landRoute(r, f, v) { S.values[`MOD:${r}:${f}`] = v; S.touched[`MOD:${r}:${f}`] = 1; }

  /* the layer the editor's channel plays: 1 when Layer 2 has a channel of its own and it is this one */
  function myLayer() {
    const c1 = S.values["GLOBAL:r0"] ?? 0, c2 = S.values["GLOBAL:r1"] ?? 1;
    return S.ch === c2 && c2 !== c1 ? 1 : 0;
  }

  let dumpBuf = [], redrawTimer = 0;
  const redrawSoon = () => { clearTimeout(redrawTimer); redrawTimer = setTimeout(() => { keep(); refresh(); }, 40); };
  function takeDump(entries) {                   // ARCH and the MFX type first: the other controls follow them
    const first = entries.filter(([, m, l]) => { const a = AT[m + ":" + l]; return a && ((a[0] === "HOME1" && a[1] === C.CCI_ROW) || (a[0] === "AMP" && a[1] === C.CCI_MFX_TYPE)); });
    const lay = myLayer();
    const mine = (e) => e[0] === lay || (e[0] === 0 && AT[e[1] + ":" + e[2]] && isPerf(AT[e[1] + ":" + e[2]][0]));
    first.filter(mine).forEach(([, m, l, v]) => landValue(...AT[m + ":" + l], v, true));
    let n = 0;
    entries.filter(mine).forEach(([, m, l, v]) => {
      if (m >= P.nrpn_route_msb && m < P.nrpn_route_msb + C.NROUTE) { landRoute(m - P.nrpn_route_msb, l, v); n++; return; }
      const a = AT[m + ":" + l];
      if (a && landValue(a[0], a[1], v, true)) n++;
    });
    if (S.values["GLOBAL:r0"] !== undefined && lay === 0) S.ch = S.values["GLOBAL:r0"];
    return n;
  }

  const nr = { msb: 127, lsb: 127, dm: -1 };      // the FM1's NRPN as it comes (99 / 98 / 6 / 38)
  function takeIn(b) {
    if (b[0] === 0xF0 && window.CarnacSeq.take(b)) return;   // (the SEQ tab's messages)
    if (b[0] === 0xF0) {
      const x = X.decode(b);
      if (!x) return;
      if (x.kind === "params") { x.entries.forEach((e) => dumpBuf.push([x.layer, ...e])); return; }
      const n = takeDump(dumpBuf);
      dumpBuf = [];
      syncHeader(); refresh();
      status(`Received the whole sound: ${n} parameters (the FM1 sent ${x.count}).`);
      return;
    }
    if ((b[0] & 0xF0) !== 0xB0 || (b[0] & 0x0F) !== S.ch) return;
    const cc = b[1], v = b[2];
    if (cc === 99) { nr.msb = v; nr.dm = -1; return; }
    if (cc === 98) { nr.lsb = v; nr.dm = -1; return; }
    if (cc === 6 || cc === 38) {
      if (cc === 6) nr.dm = v; else if (nr.dm < 0) return;
      const val = cc === 6 ? v : (nr.dm << 7) | v;
      if (nr.msb >= P.nrpn_route_msb && nr.msb < P.nrpn_route_msb + C.NROUTE) landRoute(nr.msb - P.nrpn_route_msb, nr.lsb, val);
      else { const a = AT[nr.msb + ":" + nr.lsb]; if (!a || !landValue(a[0], a[1], val, true)) return; }
      redrawSoon();
      return;
    }
    const m = P.cc_map.find((e) => e.cc === cc);
    if (m && landValue(m.page, m.item, v, false)) redrawSoon();
  }

  function receive() {
    dumpBuf = [];
    if (M.target.startsWith("port:") && !M.sysexOk) { status("SysEx was not allowed: allow it in the site settings to RECEIVE.", true); return; }
    if (!M.canSysex) { status("Pick the FM1's MIDI port or the emulator first.", true); return; }
    if (!M.sysex(X.request(), "ED_PAR_REQ: send the whole sound")) { status("Could not send to the FM1.", true); return; }
    status("Asked the FM1 for the whole sound...");
  }

  // ------------------------------------------------------------------------------------------- file
  function saveFile() {
    const blob = new Blob([JSON.stringify({ format: "carnac-editor", version: 1, values: S.values, touched: S.touched }, null, 1)],
                          { type: "application/json" });
    const a = el("a");
    a.href = URL.createObjectURL(blob);
    a.download = "carnac-sound.json";
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }
  function loadFile(file) {
    file.text().then((t) => {
      const j = JSON.parse(t);
      if (j.format !== "carnac-editor") throw new Error("not a Carnac editor file");
      S.values = j.values || {}; S.touched = j.touched || {};
      keep(); themeNow = -1; render(); applyTheme();
      status("Loaded. SEND ALL puts it on the synth.");
    }).catch((e) => status("Could not load: " + e.message, true));
  }

  // ------------------------------------------------------------------------------------------- header
  function status(t, bad) { const s = $("status"); s.textContent = t; s.classList.toggle("bad", !!bad); }

  function fillOutputs() {
    const sel = $("out");
    sel.textContent = "";
    for (const o of M.outputs()) {
      const opt = el("option", "", o.name);
      opt.value = o.id;
      sel.append(opt);
    }
    if (S.out === "none" || ![...sel.options].some((o) => o.value === S.out)) {   // (an FM1 port wins over "nowhere")
      const fm1 = [...sel.options].find((o) => /fm1|carnac|m-?vave/i.test(o.textContent));
      S.out = fm1 ? fm1.value : "none";
    }
    sel.value = S.out;
    M.select(S.out);
    syncLink();
  }
  function syncLink() {
    const t = M.target, dot = $("link");
    const live = t.startsWith("port:") || (t === "emu" && Date.now() - M.emuSeen < 5000);
    dot.classList.toggle("on", live);
    dot.title = t === "none" ? "not sending" : t === "emu" ? (live ? "the emulator answered" : "no emulator tab answered yet") : "MIDI port open";
  }
  function syncHeader() {
    $("ch").value = String(S.ch);
    $("mode").value = S.mode;
    syncCtx();
    keep();
  }
  function syncCtx() {
    const c = $("ctx");
    if (c) c.textContent = `${A().name} · MFX ${P.mfx.types[mfxType()]} · CH ${S.ch + 1}`;
  }

  // ------------------------------------------------------------------------------------------- render
  let shape = "";
  const shapeKey = () => `${arch()}|${mfxType()}|${flavour(1)}|${flavour(2)}|${S.view}`;
  function render() {
    mirrors = new Map();
    reg = null;
    if (seenBy) VIZ.forEach((o) => seenBy.unobserve(o.cv));
    VIZ = [];
    shape = shapeKey();
    const nav = $("views");
    nav.textContent = "";
    VIEWS.forEach((v) => {
      const b = el("button", "view", v.label);
      b.type = "button";
      b.setAttribute("role", "tab");
      b.setAttribute("aria-selected", String(v.id === S.view));
      b.onclick = () => {
        if (v.id === S.view) return;
        S.view = v.id; keep(); render();
        if (v.id === "seq") window.CarnacSeq.opened();
      };
      nav.append(b);
    });
    const view = VIEWS.find((v) => v.id === S.view) || VIEWS[0];
    const panel = $("panel");
    panel.className = view.id === "seq" ? "seqview" : "glance v-" + view.id;
    panel.textContent = "";
    view.build().forEach((g) => panel.append(g));
    syncCtx();
    dirty = true;
  }
  /* values came in (a dump, CC OUT): redrawn in place, unless the modules' shape changed (ARCH, MFX, a flavour) */
  function refresh() {
    if (S.view === "seq") { syncCtx(); return; }
    if (shapeKey() !== shape) render();
    else { redrawAll(); dirty = true; syncCtx(); }
    applyTheme();
  }

  // ------------------------------------------------------------------------------------------- log
  const logLines = [];
  M.on("log", ({ bytes, what }) => {
    const hex = bytes.map((b) => b.toString(16).toUpperCase().padStart(2, "0")).join(" ");
    logLines.unshift(`${hex}   ${what || ""}`);
    if (logLines.length > 60) logLines.length = 60;
    const pre = $("log");
    if (pre && $("logbox").open) pre.textContent = logLines.join("\n");
  });

  // ------------------------------------------------------------------------------------------- start
  async function start() {
    for (let c = 0; c < 16; c++) { const o = el("option", "", `CH ${c + 1}`); o.value = String(c); $("ch").append(o); }
    $("ch").onchange = () => { S.ch = parseInt($("ch").value, 10); keep(); render(); };
    $("mode").onchange = () => { S.mode = $("mode").value; keep(); };
    $("out").onchange = () => { S.out = $("out").value; M.select(S.out); keep(); };
    $("sendall").onclick = () => sendAll(false);
    $("sendedit").onclick = () => sendAll(true);
    $("save").onclick = saveFile;
    $("load").onclick = () => $("file").click();
    $("file").onchange = (e) => { if (e.target.files[0]) loadFile(e.target.files[0]); e.target.value = ""; };
    $("receive").onclick = receive;
    M.on("in", takeIn);
    window.CarnacSeq.init({ midi: M, status });
    $("panic").onclick = () => { M.cc(S.ch, 123, 0, "ALL NOTES OFF"); M.cc(S.ch, 120, 0, "ALL SOUND OFF"); };
    $("logbox").addEventListener("toggle", () => { $("log").textContent = logLines.join("\n"); });
    M.on("change", () => { syncLink(); });
    M.on("emu", () => { syncLink(); status("The emulator tab answered."); });
    setInterval(() => { if (M.target === "emu") M.pingEmu(); syncLink(); }, 2000);
    V.readPalette();
    syncHeader();
    render();
    applyTheme();
    requestAnimationFrame(frame);
    if (document.fonts && document.fonts.load) document.fonts.load('10px "Carnac Rune"').then(() => { dirty = true; }, () => {});   // (the pictures' words)
    let rz = 0;
    window.addEventListener("resize", () => { clearTimeout(rz); rz = setTimeout(() => { drawBanner(); dirty = true; if (window.CarnacSeq.repaint) window.CarnacSeq.repaint(); }, 80); });
    const ok = await M.init();
    fillOutputs();
    M.on("ports", fillOutputs);
    if (S.view === "seq") window.CarnacSeq.opened();
    status(ok ? "Pick the FM1's port and its channel, then move anything." :
      navigator.requestMIDIAccess ? "MIDI access was refused: allow it in the site settings, or use the emulator." :
      "This browser has no Web MIDI (use Chrome or Edge). The emulator target still works.", !ok);
  }
  start();
})();
