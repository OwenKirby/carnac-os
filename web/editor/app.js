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
  const S = { values: {}, touched: {}, ch: 0, out: "none", mode: "cc", tab: "home" };
  try {
    const kept = JSON.parse(localStorage.getItem(STORE_KEY) || "null");
    if (kept && kept.values) Object.assign(S, kept);
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
                names, wave, arch: r.rk === "RK_ARCH", perf: isPerf(pid), cc: CC_OF[pid + ":" + item],
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
  let mirrors = new Map();          // key -> [redraw fns] (a control drawn twice, as the sends on AMP and FX)
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };

  function meta(d) {
    const parts = [];
    if (d.cc !== undefined) parts.push(`CC ${d.cc}`);
    if (d.nrpn) parts.push(`NRPN ${d.nrpn[0]}:${d.nrpn[1]}`);
    if (d.perf) parts.push("L1 CH");
    return parts.join(" · ");
  }

  function control(d, opt = {}) {
    const box = el("div", "ctl" + (d.names ? " list" : "") + (opt.wide ? " wide" : ""));
    const top = el("div", "top");
    const lab = el("label", "lab", d.label);
    const out = el("output", "val");
    top.append(lab, out);
    let input;
    const id = "c_" + d.key.replace(/[^A-Za-z0-9]/g, "_") + "_" + Math.random().toString(36).slice(2, 6);
    if (d.names) {
      input = el("select");
      for (let v = d.min; v <= d.max; v++) {
        const o = el("option", "", d.names[v] ?? String(v));
        o.value = String(v);
        input.append(o);
      }
    } else {
      input = el("input");
      input.type = "range";
      input.min = String(d.min);
      input.max = String(d.max);
      input.step = "1";
    }
    input.id = id;
    lab.htmlFor = id;
    const m = el("div", "meta", meta(d));
    box.append(top, input, m);
    if (opt.note) box.append(el("div", "note", opt.note));
    if (opt.disabled) { input.disabled = true; box.classList.add("off"); }
    const draw = () => {
      const v = val(d);
      input.value = String(v);
      out.textContent = d.fmt(v);
      box.classList.toggle("cold", !S.touched[d.key]);
      if (!d.names) input.style.setProperty("--fill", `${((v - d.min) * 100) / Math.max(1, d.max - d.min)}%`);
    };
    const set = () => {
      const v = parseInt(input.value, 10);
      S.values[d.key] = v;
      S.touched[d.key] = 1;
      (mirrors.get(d.key) || []).forEach((f) => f());
      sendSoon(d, v);
      keep();
      if (opt.after) opt.after(v);
    };
    input.addEventListener(d.names ? "change" : "input", set);
    if (!mirrors.has(d.key)) mirrors.set(d.key, []);
    mirrors.get(d.key).push(draw);
    draw();
    return box;
  }

  function group(title, ctls, cls) {
    const g = el("section", "group" + (cls ? " " + cls : ""));
    if (title) g.append(el("h3", "", title));
    const grid = el("div", "grid");
    ctls.forEach((c) => grid.append(c));
    g.append(grid);
    return g;
  }

  const rerender = () => render();

  // ------------------------------------------------------------------------------------------- pages
  function knobsOf(pid) {
    const t = PAGE[pid].knobs;
    if (!t) return [];
    const name = t.split("[")[0];
    const tab = P.knob_tables[name];
    return t.endsWith("[arch]") ? tab[arch()] : tab;
  }

  /* a sound page as the panel has it: its four knobs, then its rows (those the architecture shows) */
  function soundPage(pid, opts = {}) {
    const knobs = [];
    knobsOf(pid).forEach((k, i) => {
      if (SKIP_KNOB.has(k.kind) || (opts.skipKnob && opts.skipKnob(k, i))) return;
      let kk = k;
      if ((pid === "FILTER1" || pid === "FILTER2") && i === 0) kk = { ...k, label: L.MORPH_LABEL[flavour(pid.slice(-1))] || k.label };
      if (pid === "MIX" && i === 0) kk = { ...k, label: "BALANCE · DRY/WET" };
      const key = PAGE[pid].knobs.endsWith("[arch]") ? `${pid}:${k.label}` : undefined;
      knobs.push(control(knobCtl(pid, i, kk, key)));
    });
    const rows = [];
    PAGE[pid].rows.forEach((r, ri) => {
      if (SKIP_ROW.has(r.rk) || (opts.skipRow && opts.skipRow(r, ri))) return;
      const d = rowCtl(pid, r, ri);
      if (PAGE[pid].store === "engine" && !shows(d)) return;
      const after = opts.after && opts.after[ri];
      rows.push(control(d, { after, wide: d.wave }));
    });
    const out = [];
    if (knobs.length) out.push(group(opts.knobTitle || "KNOBS", knobs, "knobs"));
    if (rows.length) out.push(group(opts.rowTitle || "ROWS", rows));
    return out;
  }

  function absent(text) { const g = el("section", "group absent"); g.append(el("p", "", text)); return [g]; }

  const NOISE_CC = new Set([115, 116, 117, 118, 119]);
  const noiseRow = (ri) => NOISE_CC.has(CC_OF["OSCMIX:" + (C.CCI_ROW + ri)]);

  const TABS = [
    { id: "home", label: "HOME", lore: "The hearth stone: what the sound is raised from.", build() {
      const out = soundPage("HOME1", { rowTitle: "LAYER", after: { 0: rerender }, skipKnob: () => true });   // (its knobs: play controllers, PORTA TIME again)
      out.push(...soundPage("TUNING", { rowTitle: "TUNING", skipKnob: () => true }));
      out.push(...soundPage("HOME3", { rowTitle: "ZONES (DUAL / SPLIT)", skipKnob: () => true }));
      return out;
    } },
    { id: "osc1", label: "OSC 1", lore: "The first stone sings.", build: () => soundPage("OSC1") },
    { id: "osc2", label: "OSC 2", lore: "Its companion, a fifth away or a world away.", page: "OSC2", build() {
      if (!ARCH[arch()].osc2) return absent(`${ARCH[arch()].name} has one oscillator: OSC2's place on the panel is the NOISE page, and its controllers do nothing. The noise is on OSC MIX.`);
      return soundPage("OSC2");
    } },
    { id: "oscmix", label: "OSC MIX", lore: "Where the stones' voices meet the wind.", build() {
      const out = soundPage("OSCMIX", { skipKnob: (k) => k.label === "NOISE LEVEL", skipRow: (r, ri) => noiseRow(ri) });
      const noise = PAGE.OSCMIX.rows.map((r, ri) => [CC_OF["OSCMIX:" + (C.CCI_ROW + ri)], r, ri])
        .filter(([cc]) => NOISE_CC.has(cc)).sort((x, y) => x[0] - y[0])
        .map(([, r, ri]) => control(rowCtl("OSCMIX", r, ri)));
      out.push(group("NOISE (every architecture)", noise));
      return out;
    } },
    { id: "flt1", label: "FILTER 1", lore: "The passage narrows.", build: () => soundPage("FILTER1", { after: { 1: rerender } }) },
    { id: "flt2", label: "FILTER 2", lore: "A second chamber.", page: "FILTER2", build() {
      if (!ARCH[arch()].filter2) return absent(`${ARCH[arch()].name} has one filter.`);
      return soundPage("FILTER2", { after: { 1: rerender } });
    } },
    { id: "fltmix", label: "FLT MIX", lore: "Two chambers, one passage.", page: "FLTMIX", build() {
      if (!ARCH[arch()].fltmix) return absent(`${ARCH[arch()].name} has no FILTER MIXER.`);
      return soundPage("FLTMIX");
    } },
    ...[1, 2, 3].map((n) => ({ id: "env" + n, label: "ENV " + n, lore: ["The swell of the struck stone.", "The shape the light takes through the gap.", "The slow tide under it."][n - 1], build: () => soundPage("ENV" + n) })),
    ...[1, 2, 3].map((n) => ({ id: "lfo" + n, label: "LFO " + n, lore: ["The moon's pull.", "The turning sky.", "The breath of the barrow."][n - 1], build: () => soundPage("LFO" + n) })),
    { id: "amp", label: "AMP · MFX", lore: "The voice leaves the chamber.", build() {
      const out = soundPage("AMP");
      const t = mfxType();
      const type = { key: "AMP:mt", label: "MFX", page: "AMP", item: C.CCI_MFX_TYPE, min: 0, max: C.MFX_OFFERED - 1, kind: "list",
                     names: P.mfx.types, perf: false, cc: CC_OF["AMP:" + C.CCI_MFX_TYPE], nrpn: nrpnOf("AMP", C.CCI_MFX_TYPE), def: 0 };
      type.fmt = (v) => P.mfx.types[v];
      const knobs = [control(type, { after: rerender })];
      for (let i = 0; i < 4; i++) {
        const k = P.knob_tables.MFX_KNOBS[t][i];
        if (k.kind !== "none") knobs.push(control(mfxKnobCtl(t, i)));
      }
      out.push(group("MFX", knobs, "knobs"));
      const rows = [];
      P.mfx.rows.forEach((r, ri) => {
        if (ri === 0 || !((r.mask >> t) & 1)) return;
        rows.push(control(mfxRowCtl(t, r, ri)));
      });
      if (rows.length) out.push(group("MFX ROWS", rows));
      return out;
    } },
    { id: "fx", label: "FX · MIX", lore: "The tomb answers: echo and reverberation.", build() {
      const out = soundPage("EFFECTS", { knobTitle: "DELAY · REVERB" });
      out.push(...soundPage("MIX", { knobTitle: "MIX", skipRow: () => true }));
      const sends = P.knob_tables.AMP_KNOBS.map((k, i) => [k, i]).filter(([k]) => /SEND/.test(k.label))
        .map(([k, i]) => control(knobCtl("AMP", i, k), { note: "the layer's own (on AMP too)" }));
      out.push(group("SENDS", sends));
      return out;
    } },
    { id: "mod", label: "MOD", lore: "Sixteen ley lines between the stones.", build() {
      const out = [];
      for (let i = 0; i < C.NROUTE; i++) {
        const g = group(`ROUTE ${i + 1}`, [0, 1, 2, 3, 4].map((f) => control(routeCtl(i, f))), "route");
        out.push(g);
      }
      const note = el("section", "group absent");
      note.append(el("p", "", "Routes are written by place (NRPN 64+route). The panel keeps its routes packed and sorted by SOURCE (it tidies them when its cursor leaves the MOD page), so keep them that way here: used routes first, grouped by SOURCE, unused ones at SOURCE none. RECEIVE reads back where they are."));
      out.unshift(note);
      return out;
    } },
    { id: "prog", label: "PROGRAM", lore: "Call a stone by its number.", build: buildProgram },
    { id: "global", label: "GLOBAL", lore: "The site itself.", build() {
      const out = soundPage("GLOBAL", { skipKnob: () => true, rowTitle: "GLOBAL (L1 CH)", after: { 0: (v) => { S.ch = v; syncHeader(); } } });
      const warn = el("section", "group absent");
      warn.append(el("p", "", "MIDI CH LAYER1 moves the synth: the editor follows it. A CUSTOM CC1 / CC2 set to a mapped number takes that CC away from the map (GLOBAL's choice wins; the FM1 shows it with a *). CC OUT sends the panel's moves and a dump at each load; CC IN off stops everything this editor sends."));
      out.unshift(warn);
      return out;
    } },
    { id: "seq", label: "SEQ", lore: "The procession, step by step.", build() {
      return absent("Not built yet. Planned: SEQ upload over SysEx and a MAQ16/3-style SEQ control channel (notes on a channel of its own switch steps). This tab is their place.");
    } },
  ];

  function buildProgram() {
    const out = [];
    const banks = P.pc_banks;
    const g = el("section", "group");
    g.append(el("h3", "", "PROGRAM CHANGE"));
    const grid = el("div", "grid prog");
    banks.forEach((b, i) => {
      const box = el("div", "ctl");
      const top = el("div", "top");
      top.append(el("label", "lab", b), el("output", "val", `BANK ${i}`));
      const row = el("div", "progrow");
      const num = el("input");
      num.type = "number"; num.min = "0"; num.max = "127"; num.value = String(S.values["PC:" + b] || 0);
      num.setAttribute("aria-label", `${b} program number`);
      const go = (d) => {
        let v = Math.max(0, Math.min(127, (parseInt(num.value, 10) || 0) + d));
        num.value = String(v);
        S.values["PC:" + b] = v;
        keep();
        M.program(S.ch, i, v, `BANK ${i} (${b}) PROGRAM ${v}`);
      };
      const prev = el("button", "quiet small", "−"), load = el("button", "small", "LOAD"), next = el("button", "quiet small", "+");
      prev.onclick = () => go(-1); next.onclick = () => go(1); load.onclick = () => go(0);
      prev.setAttribute("aria-label", `previous ${b}`); next.setAttribute("aria-label", `next ${b}`);
      row.append(prev, num, next, load);
      box.append(top, row, el("div", "meta", `CC 0 = ${i}, CC 32 = 0, PC` + (i ? " · L1 CH" : " · every layer on the channel")));
      grid.append(box);
    });
    g.append(grid);
    out.push(g);
    out.push(...absent("A patch loads at once into every layer on the channel. A performance, sequence or arp pattern loads on Layer 1's channel only, as the Preset Encoder would; an empty slot loads nothing."));
    return out;
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
  const redrawSoon = () => { clearTimeout(redrawTimer); redrawTimer = setTimeout(() => { keep(); render(); }, 40); };
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
    if (b[0] === 0xF0) {
      const x = X.decode(b);
      if (!x) return;
      if (x.kind === "params") { x.entries.forEach((e) => dumpBuf.push([x.layer, ...e])); return; }
      const n = takeDump(dumpBuf);
      dumpBuf = [];
      syncHeader(); render();
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
    if (!M.sysexOk) { status("SysEx was not allowed: allow it in the site settings to RECEIVE.", true); return; }
    dumpBuf = [];
    if (!M.target.startsWith("port:")) { status("Pick the FM1's MIDI port first (RECEIVE needs the real FM1).", true); return; }
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
      keep(); render();
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
    keep();
  }

  // ------------------------------------------------------------------------------------------- render
  function render() {
    mirrors = new Map();
    reg = null;
    const nav = $("tabs");
    nav.textContent = "";
    TABS.forEach((t) => {
      const b = el("button", "tab", t.label);
      b.type = "button";
      b.setAttribute("role", "tab");
      b.setAttribute("aria-selected", String(t.id === S.tab));
      const off = (t.id === "osc2" && !ARCH[arch()].osc2) || (t.id === "flt2" && !ARCH[arch()].filter2) ||
                  (t.id === "fltmix" && !ARCH[arch()].fltmix);
      if (off) b.classList.add("dim");
      b.onclick = () => { S.tab = t.id; keep(); render(); };
      nav.append(b);
    });
    const tab = TABS.find((t) => t.id === S.tab) || TABS[0];
    const panel = $("panel");
    panel.textContent = "";
    const head = el("div", "phead");
    head.append(el("h2", "", tab.label), el("p", "lore", tab.lore));
    const ctx = el("p", "ctx", `ARCH ${ARCH[arch()].name} · MFX ${P.mfx.types[mfxType()]} · CH ${S.ch + 1}`);
    head.append(ctx);
    panel.append(head);
    tab.build().forEach((g) => panel.append(g));
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
    $("panic").onclick = () => { M.cc(S.ch, 123, 0, "ALL NOTES OFF"); M.cc(S.ch, 120, 0, "ALL SOUND OFF"); };
    $("logbox").addEventListener("toggle", () => { $("log").textContent = logLines.join("\n"); });
    M.on("change", () => { syncLink(); });
    M.on("emu", () => { syncLink(); status("The emulator tab answered."); });
    setInterval(() => { if (M.target === "emu") M.pingEmu(); syncLink(); }, 2000);
    syncHeader();
    render();
    const ok = await M.init();
    fillOutputs();
    M.on("ports", fillOutputs);
    status(ok ? "Pick the FM1's port and its channel, then move anything." :
      navigator.requestMIDIAccess ? "MIDI access was refused: allow it in the site settings, or use the emulator." :
      "This browser has no Web MIDI (use Chrome or Edge). The emulator target still works.", !ok);
  }
  start();
})();
