/* SPDX-License-Identifier: GPL-3.0-only
   Carnac web editor: the SEQ tab (2026-11-19). Edits the sequence the FM1 is playing now, heard at once; SAVE writes it
   into one of the 32 sequence slots under a name, as the panel's SAVE does.

   The firmware side is src/hardware/synth_panel.c "SEQ OUT" with the wire format of src/seq_wire.h, in the editor frame
   F0 7D 46 4C cmd .. F7:
     60 REQ   []                      -> 4 x LANE, NAME, POS
     61 LANE  [lane, 8 footer + 16 x 7 step bytes]    both ways
     62 STEP  [lane, step, 7 bytes]                   both ways
     63 NAME  [16 char_map indices]                   both ways
     64 SAVE  [slot, 16 chars] -> [slot, rc]  (0 saved, 1 failed, 2 busy)
     65 SLOTS [first] -> [first, n, n x (used, 16 chars)]
     66 LOAD  [slot]
     67 POS   <- [playing | rec << 1, step of lane 1..4 (127: none)]
     68 PLAY  [on]
     69 ACK   <- [cmd], once each of the above is handled. The FM1's USB holds one received SysEx at a time (a second
              one before the main loop took the first is lost), so the editor sends one, waits for its ACK, then the
              next; no ACK in 250 ms: sent again. A step queued twice is sent once, with its newest value.
   footer = EVENT, TARGET, MD, PRESCALE, LOOP START, LOOP END, SCALE, ROOT; step = note 1..4, VALUE, FLAGS, EMPTY
   (bit i: note slot i unused). The FM1 sends the panel's own edits back (a step, or the lane), so both stay in step.

   Keys (the SEQ tab open, not typing in a field): A W S E D F T G Y H U J K play C..C into the selected step and move
   on (Shift: add to the chord), Z / X octave, arrows move / change, Delete rest, . tie, 1-4 lane, Space play, Ctrl+Z undo. */
(function () {
  "use strict";
  const HDR = [0xF0, 0x7D, 0x46, 0x4C];
  const CMD = { REQ: 60, LANE: 61, STEP: 62, NAME: 63, SAVE: 64, SLOTS: 65, LOAD: 66, POS: 67, PLAY: 68, ACK: 69 };
  const NLANE = 4, NSTEP = 16, FOOT = 8, STEPN = 7, NSLOT = 32, NAMELEN = 16;
  const TIE = 127, F_MUTE = 1, F_TIE = 2, F_SKIP = 4;
  const CM = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_/";
  const NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const noteName = (n) => NOTE[n % 12] + (Math.floor(n / 12) - 1);
  const pct = (v) => Math.round((v * 100) / 127) + "%";
  /* the event registry (seq_data.h SEQEV_*; the panel's short names): what TARGET means and how a VALUE reads */
  const EV = [
    { s: "Note", n: "NOTE", tg: "layer", note: true },
    { s: "Dyn", n: "DYNAMICS", tg: "mask", f: (v) => "vel " + (v || 1), d: 100 },
    { s: "Trns", n: "TRANSPOSE", tg: "lane", bip: true, f: (v) => (v >= 64 ? "+" : "") + (v - 64), d: 64 },
    { s: "Time", n: "TIMING", tg: "mask", f: (v) => (v === 0 ? "SKIP" : ["4", "8", "16", "32"][v >> 5] + " tk"), d: 64 },
    { s: "Gate", n: "GATE", tg: "mask", f: (v) => (v === 127 ? "LEGATO" : Math.round(((v + 1) * 100) / 128) + "%"), d: 64 },
    { s: "Rtch", n: "RATCHET", tg: "mask", f: (v) => "x" + ((v >> 5) + 1), d: 0 },
    { s: "PSkp", n: "PROB SKIP", tg: "mask", f: pct, d: 0 },
    { s: "PMut", n: "PROB MUTE", tg: "mask", f: pct, d: 0 },
    { s: "MW", n: "MOD WHEEL", tg: "layer", f: String, d: 0 },
    { s: "AT", n: "AFTERTOUCH", tg: "layer", f: String, d: 0 },
    { s: "CC1", n: "CUSTOM CC1", tg: "layer", f: String, d: 0 },
    { s: "CC2", n: "CUSTOM CC2", tg: "layer", f: String, d: 0 },
    { s: "PC", n: "PROGRAM (DUAL only)", tg: "layer", f: (v) => "P" + (v + 1), d: 0 },
    { s: "Layr", n: "LAYER", tg: "lane", f: (v) => (v < 43 ? "L1" : v < 86 ? "L2" : "BOTH"), d: 0 },
    { s: "PAT", n: "POLY AT", tg: "layer", f: String, d: 0 },
  ];
  const MODES = ["FOr  FORWARD", "bAC  BACKWARD", "PE1  PENDULUM 1", "PE2  PENDULUM 2", "rnd  RANDOM"];
  const SCALES = ["OFF", "GENRE (the ARP's)", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN",
                  "HARMONIC MINOR", "MAJOR PENTA", "MINOR PENTA", "BLUES"];
  const SCALE_C = [0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x9AD, 0x295, 0x4A9, 0x4E9];   // seq_scale.c, from MAJOR
  const PRESCALE_NAME = { 3: "1/32", 4: "1/16T", 6: "1/16", 8: "1/8T", 9: "1/16.", 12: "1/8", 16: "1/4T", 18: "1/8.", 24: "1/4", 32: "1/2T" };
  const PIANO = "awsedftgyhujk";                 // C .. C, the computer keyboard as two rows of a piano

  // ------------------------------------------------------------------------------------------- the model
  const KEY = "carnac-seq-v1";
  const newStep = (note) => ({ n: [note, null, null, null], v: 64, f: 0 });
  const newLane = (ev) => ({ ev, tg: EV[ev].tg === "mask" ? 1 : 0, md: 0, ps: 6, ls: 0, le: 15, sc: 0, rt: 0,
                             steps: Array.from({ length: NSTEP }, () => newStep(48)) });
  let Q = { lanes: [newLane(0), newLane(1), newLane(4), newLane(5)], name: "", got: false, oct: 4,
            sel: { l: 0, s: 0, slot: 0 }, slots: [], saveSlot: 8, clip: null };
  try {
    const k = JSON.parse(localStorage.getItem(KEY) || "null");
    if (k && Array.isArray(k.lanes) && k.lanes.length === NLANE) Q = Object.assign(Q, k, { got: false, slots: [] });
  } catch (_) { /* no storage */ }
  let keepT = 0;
  const keep = () => {
    clearTimeout(keepT);
    keepT = setTimeout(() => {
      try { localStorage.setItem(KEY, JSON.stringify({ lanes: Q.lanes, name: Q.name, oct: Q.oct, sel: Q.sel, saveSlot: Q.saveSlot })); } catch (_) { /* private */ }
    }, 300);
  };
  const run = { playing: 0, rec: 0, pos: [127, 127, 127, 127] };

  // ------------------------------------------------------------------------------------------- the wire
  function packStep(st) {
    const o = [];
    let empty = 0;
    for (let i = 0; i < 4; i++) {
      if (st.n[i] === null || st.n[i] === undefined) { empty |= 1 << i; o.push(0); } else o.push(st.n[i] & 0x7F);
    }
    o.push(st.v & 0x7F, st.f & 7, empty);
    return o;
  }
  function unpackStep(b) {
    return { n: [0, 1, 2, 3].map((i) => ((b[6] >> i) & 1 ? null : b[i])), v: b[4], f: b[5] & 7 };
  }
  function packLane(L) {
    const o = [L.ev, L.tg, L.md, L.ps, L.ls, L.le, L.sc, L.rt];
    L.steps.forEach((st) => o.push(...packStep(st)));
    return o;
  }
  function unpackLane(b) {
    const L = { ev: b[0], tg: b[1], md: b[2], ps: b[3], ls: b[4], le: b[5], sc: b[6], rt: b[7], steps: [] };
    for (let s = 0; s < NSTEP; s++) L.steps.push(unpackStep(b.slice(FOOT + s * STEPN, FOOT + (s + 1) * STEPN)));
    return L;
  }
  const nameBytes = (t) => Array.from({ length: NAMELEN }, (_, i) => Math.max(0, CM.indexOf((t[i] || " ").toUpperCase())));
  const nameText = (b) => b.map((c) => CM[c] || " ").join("").trimEnd();

  let api = null;                                // { midi, status, el }
  const M = () => api.midi;
  const can = () => !!(api && M() && M().canSysex);
  /* the way out: one message in flight, the next after its ACK */
  const outq = [];
  let inflight = null;
  function sx(cmd, args, what, key) {
    if (!can()) return false;
    const m = { cmd, args, what, key };
    const i = key === undefined ? -1 : outq.findIndex((x) => x.key === key);
    if (i >= 0) outq[i] = m; else outq.push(m);
    pump();
    return true;
  }
  function wire(m) { M().sysex(HDR.concat([m.cmd], m.args, [0xF7]), m.what + (m.tries > 1 ? ` (again, ${m.tries})` : "")); }
  function pump() {
    if (inflight || !outq.length) return;
    inflight = outq.shift();
    inflight.t = Date.now(); inflight.tries = 1;
    wire(inflight);
  }
  setInterval(() => {
    if (!inflight) return;
    if (!can()) { inflight = null; outq.length = 0; return; }
    if (Date.now() - inflight.t < 250) return;
    if (inflight.tries >= 4) {
      api.status("The synth stopped answering the SEQ tab: check the port, then RECEIVE SEQ.", true);
      inflight = null; outq.length = 0; return;
    }
    inflight.tries++; inflight.t = Date.now();
    wire(inflight);
  }, 50);
  function sendStep(l, s) {
    sx(CMD.STEP, [l, s, ...packStep(Q.lanes[l].steps[s])], `SEQ lane ${l + 1} step ${s + 1}`, `s${l}:${s}`);
  }
  function sendLane(l) {
    for (let i = outq.length - 1; i >= 0; i--) if (outq[i].key && outq[i].key.startsWith(`s${l}:`)) outq.splice(i, 1);
    sx(CMD.LANE, [l, ...packLane(Q.lanes[l])], `SEQ lane ${l + 1} (whole)`, `l${l}`);
  }
  function request() {
    if (!can()) return false;
    sx(CMD.REQ, [], "SEQ: ask for the sequence");
    [0, 8, 16, 24].forEach((f) => sx(CMD.SLOTS, [f], `SEQ: slot names ${f + 1}-${f + 8}`));
    return true;
  }

  /* a message from the synth: 1 when it was the SEQ tab's */
  function take(b) {
    if (b.length < 6 || !HDR.every((x, i) => b[i] === x) || b[4] < CMD.REQ || b[4] > CMD.ACK) return false;
    const a = b.slice(5, b[b.length - 1] === 0xF7 ? b.length - 1 : b.length);
    switch (b[4]) {
      case CMD.ACK:
        if (inflight && inflight.cmd === a[0]) { inflight = null; pump(); }
        break;
      case CMD.LANE:
        if (a.length >= 1 + FOOT + NSTEP * STEPN && a[0] < NLANE) {
          Q.lanes[a[0]] = unpackLane(a.slice(1));
          Q.got = true; keep(); redrawLane(a[0]);
          if (ui.banner) { ui.banner.remove(); ui.banner = null; }
        }
        break;
      case CMD.STEP:
        if (a.length >= 2 + STEPN && a[0] < NLANE && a[1] < NSTEP) {
          Q.lanes[a[0]].steps[a[1]] = unpackStep(a.slice(2));
          keep(); redrawCells(a[0], true);
        }
        break;
      case CMD.NAME:
        Q.name = nameText(a.slice(0, NAMELEN)); keep();
        if (ui.name && document.activeElement !== ui.name) ui.name.value = Q.name;
        break;
      case CMD.POS:
        run.playing = a[0] & 1; run.rec = (a[0] >> 1) & 1; run.pos = a.slice(1, 5);
        drawPlayhead();
        break;
      case CMD.SLOTS: {
        const f = a[0], n = a[1];
        for (let k = 0; k < n; k++) {
          const e = a.slice(2 + k * 17, 2 + k * 17 + 17);
          Q.slots[f + k] = { used: !!e[0], name: nameText(e.slice(1)) };
        }
        drawSlots();
        break;
      }
      case CMD.SAVE:
        api.status(a[1] === 0 ? `Saved into sequence slot ${a[0] + 1}.` : a[1] === 2 ? "The FM1 is busy saving: try again." : "The save failed.", a[1] !== 0);
        if (a[1] === 0) sx(CMD.SLOTS, [a[0] & ~7], "SEQ: slot names");
        break;
      default:
        break;
    }
    return true;
  }

  // ------------------------------------------------------------------------------------------- scales
  function maskOf(L) {
    if (!EV[L.ev].note || L.sc < 2 || L.sc >= 2 + SCALE_C.length) return 0;   // (OFF; GENRE follows the ARP: not known here)
    const m = SCALE_C[L.sc - 2] << (L.rt % 12);
    return (m | (m >> 12)) & 0xFFF;
  }
  const inScale = (n, m) => (m >> (n % 12)) & 1;
  function snap(n, m) {
    if (!m || n > 126) return n;
    for (let d = 0; d < 12; d++) {
      if (n - d >= 0 && inScale(n - d, m)) return n - d;
      if (n + d <= 126 && inScale(n + d, m)) return n + d;
    }
    return n;
  }
  function stepBy(n, deg, m) {
    if (!m) return Math.max(0, Math.min(126, n + deg));
    const dir = deg < 0 ? -1 : 1;
    for (let k = Math.abs(deg); k > 0; k--) {
      let x = n + dir;
      while (x >= 0 && x <= 126 && !inScale(x, m)) x += dir;
      if (x < 0 || x > 126) break;
      n = x;
    }
    return n;
  }

  // ------------------------------------------------------------------------------------------- undo
  const undo = [];
  function mark() {
    undo.push(JSON.stringify(Q.lanes));
    if (undo.length > 60) undo.shift();
  }
  function undoLast() {
    if (!undo.length) { api.status("Nothing to undo."); return; }
    const before = JSON.parse(undo.pop());
    before.forEach((L, l) => {
      if (JSON.stringify(L) !== JSON.stringify(Q.lanes[l])) { Q.lanes[l] = L; sendLane(l); }
    });
    keep(); redrawAll();
  }

  // ------------------------------------------------------------------------------------------- edits
  const lane = (l) => Q.lanes[l];
  function setFooter(l, k, v) {
    mark();
    const L = lane(l);
    if (k === "ev" && v !== L.ev) {               // seq_lane_set_event: TARGET means something else now
      L.ev = v;
      L.tg = EV[v].tg === "mask" ? 1 : 0;
      if (!EV[v].note) L.steps.forEach((st) => { if (st.v === undefined) st.v = EV[v].d; });
    } else L[k] = v;
    if (k === "ls" && L.ls > L.le) L.le = L.ls;
    if (k === "le" && L.le < L.ls) L.ls = L.le;
    sendLane(l); keep(); redrawLane(l);
  }
  function setStep(l, s, fn, gesture, fromInsp) {
    if (!gesture) mark();
    fn(lane(l).steps[s]);
    sendStep(l, s); keep(); redrawCells(l, !fromInsp);
  }
  function enterNote(n, chord) {
    const { l, s } = Q.sel, L = lane(l);
    if (!EV[L.ev].note) { api.status("Lane " + (l + 1) + " is a " + EV[L.ev].n + " lane: pick a NOTE lane to play notes in."); return; }
    n = snap(Math.max(0, Math.min(126, n)), maskOf(L));
    setStep(l, s, (st) => {
      if (chord) {
        if (st.n.includes(n)) return;
        const i = st.n.findIndex((x, k) => k > 0 && x === null);
        if (i > 0) st.n[i] = n;
      } else {
        st.n[Q.sel.slot] = n;
        st.f &= ~(F_MUTE | F_SKIP);
        if (st.n[0] === TIE || st.n[0] === null) st.n[0] = n;
      }
    });
    if (!chord) moveSel(1);
  }
  function moveSel(d) {
    const L = lane(Q.sel.l);
    let s = Q.sel.s + d;
    if (d > 0 && s > L.le) s = L.ls;              // (inside the loop, as the procession walks)
    if (d < 0 && s < L.ls) s = L.le;
    Q.sel.s = (s + NSTEP) % NSTEP;
    Q.sel.slot = 0;
    keep(); redrawCells(Q.sel.l, true);
  }
  function select(l, s) {
    const old = Q.sel.l;
    Q.sel = { l, s, slot: Q.sel.l === l && Q.sel.s === s ? Q.sel.slot : 0 };
    keep();
    if (!live()) return;
    ui.lanes.forEach((b, k) => b.classList.toggle("act", k === l));
    if (old !== l) fillCells(old);
    fillCells(l); drawInspector();
  }

  /* lane tools */
  const TOOLS = [
    ["◀", "Shift the steps left (inside the loop)", (L) => rot(L, 1)],
    ["▶", "Shift the steps right (inside the loop)", (L) => rot(L, -1)],
    ["⇄", "Reverse the steps inside the loop", (L) => { const w = L.steps.slice(L.ls, L.le + 1).reverse(); L.steps.splice(L.ls, w.length, ...w); }],
    ["−12", "Notes an octave down", (L) => trans(L, -12), true],
    ["−1", "Notes a step down (a scale degree under a SCALE)", (L) => trans(L, -1), true],
    ["+1", "Notes a step up", (L) => trans(L, 1), true],
    ["+12", "Notes an octave up", (L) => trans(L, 12), true],
    ["RND", "Random values (a NOTE lane: random notes in the lane's scale, around the first step's)", rnd],
    ["COPY", "Copy this lane", (L) => { Q.clip = JSON.parse(JSON.stringify(L)); api.status("Lane copied."); return "nosend"; }],
    ["PASTE", "Paste the copied lane over this one", (L, l) => { if (!Q.clip) return "nosend"; Q.lanes[l] = JSON.parse(JSON.stringify(Q.clip)); }],
    ["CLEAR", "Every step back to a plain one", (L) => { L.steps = L.steps.map(() => EV[L.ev].note ? newStep(48) : Object.assign(newStep(48), { v: EV[L.ev].d })); }],
  ];
  function rot(L, d) {
    const w = L.steps.slice(L.ls, L.le + 1);
    const r = d > 0 ? w.slice(d).concat(w.slice(0, d)) : w.slice(d).concat(w.slice(0, w.length + d));
    L.steps.splice(L.ls, w.length, ...r);
  }
  function trans(L, d) {
    const m = maskOf(L);
    L.steps.forEach((st) => {
      st.n = st.n.map((x, i) => (x === null || (i === 0 && x === TIE) ? x
        : Math.abs(d) === 12 ? Math.max(0, Math.min(126, x + d)) : stepBy(snap(x, m), d, m)));
    });
  }
  function rnd(L) {
    if (EV[L.ev].note) {
      const m = maskOf(L) || 0xFFF, base = L.steps[0].n[0] !== null && L.steps[0].n[0] < TIE ? L.steps[0].n[0] : 48;
      L.steps.forEach((st) => { st.n = [snap(base + Math.floor(Math.random() * 13) - 3, m), null, null, null]; st.f = 0; });
    } else {
      L.steps.forEach((st) => { st.v = Math.floor(Math.random() * 128); });
    }
  }

  // ------------------------------------------------------------------------------------------- drawing
  const ui = { root: null, lanes: [], cells: [], strips: [], insp: null, name: null, slotSel: null, saveSel: null, play: null };
  const el = (t, c, txt) => { const e = document.createElement(t); if (c) e.className = c; if (txt !== undefined) e.textContent = txt; return e; };
  const live = () => ui.root && document.body.contains(ui.root);
  const cap = (node, e) => { try { node.setPointerCapture(e.pointerId); } catch (_) { /* (a pen lifted already) */ } };

  function opt(sel, v, label, cur) {
    const o = el("option", "", label); o.value = String(v); if (v === cur) o.selected = true; sel.append(o); return o;
  }
  function field(label, node, cls) {
    const f = el("label", "sfield" + (cls ? " " + cls : ""));
    f.append(el("span", "", label), node);
    return f;
  }

  function build() {
    const root = el("div", "seq");
    ui.root = root;
    // the strip: transport, name, slots, files
    const top = el("section", "group seqtop");
    const play = el("button", "", run.playing ? "STOP" : "PLAY");
    play.type = "button"; play.title = "Start or stop the sequencer (Space)";
    play.onclick = () => sx(CMD.PLAY, [run.playing ? 0 : 1], run.playing ? "SEQ stop" : "SEQ play");
    ui.play = play;
    const get = el("button", "quiet", "RECEIVE SEQ");
    get.type = "button"; get.title = "Ask the FM1 for the sequence it plays now";
    get.onclick = () => { if (!request()) api.status("Pick the FM1's port (SysEx allowed) or the emulator first.", true); else api.status("Asked for the sequence."); };
    const name = el("input", "seqname");
    name.maxLength = NAMELEN; name.value = Q.name; name.placeholder = "NAME"; name.spellcheck = false;
    name.setAttribute("aria-label", "Sequence name");
    name.oninput = () => { const p = name.selectionStart; name.value = name.value.toUpperCase().replace(/[^ A-Z0-9.\-_/]/g, ""); name.setSelectionRange(p, p); };
    name.onchange = () => { Q.name = name.value; keep(); sx(CMD.NAME, nameBytes(Q.name), `SEQ name ${Q.name}`, "name"); };
    ui.name = name;
    const slotSel = el("select"); slotSel.setAttribute("aria-label", "Sequence slot to load");
    const load = el("button", "quiet", "LOAD");
    load.type = "button"; load.title = "Load that slot into the live sequence (as the Preset Encoder would)";
    load.onclick = () => { const s = parseInt(slotSel.value, 10); sx(CMD.LOAD, [s], `SEQ load slot ${s + 1}`); };
    const saveSel = el("select"); saveSel.setAttribute("aria-label", "Sequence slot to save into");
    saveSel.onchange = () => { Q.saveSlot = parseInt(saveSel.value, 10); keep(); };
    const save = el("button", "", "SAVE");
    save.type = "button"; save.title = "Save the live sequence into that slot under the name";
    save.onclick = () => {
      const s = parseInt(saveSel.value, 10), used = Q.slots[s] && Q.slots[s].used;
      if (used && !confirm(`Slot ${s + 1} holds ${Q.slots[s].name || "a sequence"}. Save over it?`)) return;
      Q.name = name.value; keep();
      if (sx(CMD.SAVE, [s, ...nameBytes(Q.name)], `SEQ save into slot ${s + 1}`)) api.status(`Saving into slot ${s + 1}...`);
    };
    ui.slotSel = slotSel; ui.saveSel = saveSel;
    const fsave = el("button", "quiet", "SAVE FILE"), fload = el("button", "quiet", "LOAD FILE"), undoB = el("button", "quiet", "UNDO");
    [fsave, fload, undoB].forEach((b) => { b.type = "button"; });
    fsave.onclick = saveFile; fload.onclick = loadFile; undoB.onclick = undoLast; undoB.title = "Ctrl+Z";
    const row1 = el("div", "seqrow");
    row1.append(play, get, field("NAME", name), undoB);
    const row2 = el("div", "seqrow");
    row2.append(field("SLOT", slotSel), load, field("SAVE TO", saveSel), save, fsave, fload);
    top.append(row1, row2);
    root.append(top);
    ui.banner = null;
    if (!Q.got) {
      const n = el("section", "group absent");
      ui.banner = n;
      n.append(el("p", "", can() ? "This is the editor's own copy: RECEIVE SEQ (or opening this tab) brings the FM1's sequence."
        : "Not connected: pick the FM1's port (allow SysEx) or the emulator, then RECEIVE SEQ. Edits made now stay in the editor until then."));
      root.append(n);
    }
    ui.lanes = []; ui.cells = []; ui.strips = [];
    const lanesBox = el("div", "lanes");
    for (let l = 0; l < NLANE; l++) {
      const box = el("section", "lane");
      ui.lanes.push(box);
      lanesBox.append(box);
    }
    root.append(lanesBox);
    ui.insp = el("section", "group insp");
    root.append(ui.insp);
    const help = el("p", "small", "");
    help.innerHTML = "Keys: <b>A W S E D F T G Y H U J K</b> play C..C into the selected step (Shift: add to its chord), " +
      "<b>Z X</b> octave, <b>← →</b> step, <b>↑ ↓</b> note or value (Shift: octave / x8), <b>Delete</b> rest, <b>.</b> tie, " +
      "<b>1-4</b> lane, <b>Space</b> play, <b>Ctrl+Z</b> undo. Drag a note up or down; draw values across a lane; drag the loop's ends.";
    root.append(help);
    for (let l = 0; l < NLANE; l++) drawLane(l);
    drawInspector(); drawSlots(); drawPlayhead();
    return [root];
  }

  function drawSlots() {
    if (!live()) return;
    [ui.slotSel, ui.saveSel].forEach((sel, k) => {
      const cur = k ? Q.saveSlot : parseInt(sel.value || "0", 10);
      sel.textContent = "";
      for (let s = 0; s < NSLOT; s++) {
        const e = Q.slots[s];
        opt(sel, s, String(s + 1).padStart(2, "0") + "  " + (e ? (e.used ? e.name || "SEQUENCE" : "— empty") : "?"), cur);
      }
    });
  }

  function redrawLane(l) { if (live()) { drawLane(l); if (Q.sel.l === l) drawInspector(); } }
  /* a step changed: the cells only (the strip keeps a drag's pointer), and the inspector unless it made the change */
  function redrawCells(l, insp) { if (live()) { fillCells(l); if (insp && Q.sel.l === l) drawInspector(); } }
  function redrawAll() { if (live()) { for (let l = 0; l < NLANE; l++) drawLane(l); drawInspector(); } }

  function targetCtl(l) {
    const L = lane(l), t = EV[L.ev].tg, box = el("span", "tgt");
    if (t === "mask") {
      for (let k = 0; k < NLANE; k++) {
        const b = el("button", "chip" + ((L.tg >> k) & 1 ? " on" : ""), String(k + 1));
        b.type = "button"; b.title = `Acts on lane ${k + 1}`;
        b.setAttribute("aria-pressed", String(!!((L.tg >> k) & 1)));
        b.onclick = () => setFooter(l, "tg", L.tg ^ (1 << k));
        box.append(b);
      }
      return field("ON LANES", box);
    }
    const sel = el("select");
    if (t === "lane") for (let k = 0; k < NLANE; k++) opt(sel, k, `LANE ${k + 1}`, L.tg);
    else ["L1", "L2", "BOTH"].forEach((n, k) => opt(sel, k, n, L.tg));
    sel.onchange = () => setFooter(l, "tg", parseInt(sel.value, 10));
    return field(t === "lane" ? "OF LANE" : "TARGET", sel);
  }

  function drawLane(l) {
    const box = ui.lanes[l], L = lane(l), E = EV[L.ev] || EV[0];
    box.textContent = "";
    box.classList.toggle("act", Q.sel.l === l);
    const head = el("div", "lhead");
    const tag = el("button", "lname", `LANE ${l + 1}`);
    tag.type = "button"; tag.onclick = () => select(l, Q.sel.l === l ? Q.sel.s : L.ls);
    const ev = el("select"); EV.forEach((e, k) => opt(ev, k, e.n, L.ev));
    ev.onchange = () => setFooter(l, "ev", parseInt(ev.value, 10));
    const ps = el("select");
    for (let p = 1; p <= 32; p++) opt(ps, p, `g${String(p).padStart(2, "0")}` + (PRESCALE_NAME[p] ? "  " + PRESCALE_NAME[p] : ""), L.ps);
    ps.onchange = () => setFooter(l, "ps", parseInt(ps.value, 10));
    const md = el("select"); MODES.forEach((m, k) => opt(md, k, m, L.md));
    md.onchange = () => setFooter(l, "md", parseInt(md.value, 10));
    head.append(tag, field("EVENT", ev), targetCtl(l), field("STEP", ps), field("MODE", md));
    if (E.note) {
      const sc = el("select"); SCALES.forEach((n, k) => opt(sc, k, n, L.sc));
      sc.onchange = () => setFooter(l, "sc", parseInt(sc.value, 10));
      head.append(field("SCALE", sc));
      if (L.sc >= 2) {
        const rt = el("select"); NOTE.forEach((n, k) => opt(rt, k, n, L.rt));
        rt.onchange = () => setFooter(l, "rt", parseInt(rt.value, 10));
        head.append(field("ROOT", rt));
      }
    }
    const tools = el("div", "ltools");
    TOOLS.forEach(([t, title, fn, noteOnly]) => {
      if (noteOnly && !E.note) return;
      const b = el("button", "quiet small", t);
      b.type = "button"; b.title = title;
      b.onclick = () => { mark(); if (fn(lane(l), l) !== "nosend") { sendLane(l); keep(); redrawLane(l); } else undo.pop(); };
      tools.append(b);
    });
    head.append(tools);
    box.append(head);

    // the loop: a rail over the steps, its two ends dragged
    const rail = el("div", "loop");
    rail.title = "The loop: drag either end";
    for (let s = 0; s < NSTEP; s++) {
      const c = el("div", "lc" + (s >= L.ls && s <= L.le ? " in" : "") + (s === L.ls ? " st" : "") + (s === L.le ? " en" : ""));
      rail.append(c);
    }
    let end = null;
    const at = (e) => { const r = rail.getBoundingClientRect(); return Math.max(0, Math.min(NSTEP - 1, Math.floor(((e.clientX - r.left) / r.width) * NSTEP))); };
    rail.onpointerdown = (e) => {
      const s = at(e);
      end = Math.abs(s - L.ls) <= Math.abs(s - L.le) && !(s > L.le) ? "ls" : "le";
      cap(rail, e);
      mark();
      move(e);
    };
    const move = (e) => {
      if (!end) return;
      let s = at(e);
      if (end === "ls") s = Math.min(s, L.le); else s = Math.max(s, L.ls);
      if (L[end] !== s) { L[end] = s; [...rail.children].forEach((c, k) => { c.className = "lc" + (k >= L.ls && k <= L.le ? " in" : "") + (k === L.ls ? " st" : "") + (k === L.le ? " en" : ""); }); }
    };
    rail.onpointermove = move;
    rail.onpointerup = rail.onpointercancel = () => { if (end) { end = null; sendLane(l); keep(); redrawLane(l); } };
    box.append(rail);

    // the steps
    const strip = el("div", "steps" + (E.note ? " notes" : " values"));
    ui.strips[l] = strip;
    stepInput(strip, l);
    box.append(strip);
    fillCells(l);
  }

  function fillCells(l) {
    const strip = ui.strips[l], L = lane(l), E = EV[L.ev] || EV[0];
    if (!strip) return;
    strip.textContent = "";
    const cells = [];
    const m = maskOf(L);
    let lo = 127, hi = 0;
    if (E.note) L.steps.forEach((st) => st.n.forEach((x, i) => { if (x !== null && !(i === 0 && x === TIE)) { lo = Math.min(lo, x); hi = Math.max(hi, x); } }));
    if (hi - lo < 12) { const mid = (lo + hi) >> 1; lo = mid - 6; hi = mid + 6; }
    for (let s = 0; s < NSTEP; s++) {
      const st = L.steps[s], c = el("div", "cell");
      if (s < L.ls || s > L.le) c.classList.add("out");
      if (Q.sel.l === l && Q.sel.s === s) c.classList.add("sel");
      if (st.f & F_MUTE) c.classList.add("mute");
      if (st.f & F_SKIP) c.classList.add("skip");
      const tie = (st.f & F_TIE) || st.n[0] === TIE;
      if (tie) c.classList.add("tie");
      c.dataset.s = String(s);
      c.append(el("span", "num", String(s + 1)));
      if (E.note) {
        const notes = st.n.filter((x, i) => x !== null && !(i === 0 && x === TIE));
        if (notes.length && !(st.n[0] === TIE)) {
          const top = notes[0], mark_ = el("span", "pitch");
          mark_.style.bottom = Math.round(((snap(top, m) - lo) / Math.max(1, hi - lo)) * 70 + 8) + "%";
          c.append(mark_);
          c.append(el("span", "nn", noteName(snap(top, m))));
          if (notes.length > 1) c.append(el("span", "ch", notes.slice(1).map((x) => noteName(snap(x, m))).join(" ")));
        }
        c.append(el("span", "fl", st.f & F_SKIP ? "SKIP" : st.f & F_MUTE ? "REST" : tie ? "TIE" : ""));
        c.title = `Step ${s + 1}: drag up / down to change the note; double-click: rest`;
      } else {
        const bar = el("span", "bar" + (E.bip ? " bip" : ""));
        if (E.bip) {
          const h = Math.abs(st.v - 64) / 64 * 50;
          bar.style.height = h + "%";
          bar.style.bottom = (st.v >= 64 ? 50 : 50 - h) + "%";
        } else bar.style.height = Math.max(2, (st.v / 127) * 100) + "%";
        c.append(bar, el("span", "vv", E.f(st.v)), el("span", "fl", st.f & F_SKIP ? "SKIP" : st.f & F_MUTE ? "OFF" : ""));
        c.title = `Step ${s + 1}: ${E.f(st.v)} (draw across the lane to set values)`;
      }
      cells.push(c);
      strip.append(c);
    }
    ui.cells[l] = cells;
    drawPlayhead();
  }

  /* pointer: a NOTE lane's step dragged up / down moves its note; a value lane is drawn across */
  function stepInput(strip, l) {
    let g = null;
    const cellAt = (e) => {
      const r = strip.getBoundingClientRect(), cols = getComputedStyle(strip).gridTemplateColumns.split(" ").length || NSTEP;
      const rows = Math.ceil(NSTEP / cols), col = Math.floor(((e.clientX - r.left) / r.width) * cols), row = Math.floor(((e.clientY - r.top) / r.height) * rows);
      const s = Math.max(0, Math.min(rows - 1, row)) * cols + Math.max(0, Math.min(cols - 1, col));
      return Math.min(NSTEP - 1, s);
    };
    const valueAt = (e, s) => {
      const c = ui.cells[l][s].getBoundingClientRect();
      return Math.max(0, Math.min(127, Math.round((1 - (e.clientY - c.top) / c.height) * 127)));
    };
    strip.onpointerdown = (e) => {
      if (e.button !== 0) return;
      const s = cellAt(e), L = lane(l);
      if (Q.sel.l !== l || Q.sel.s !== s) select(l, s);
      mark();
      cap(strip, e);
      if (EV[L.ev].note) {
        const st = L.steps[s];
        g = { kind: "note", s, y: e.clientY, base: st.n[0] !== null && st.n[0] !== TIE ? st.n[0] : null, moved: false };
      } else {
        const v = valueAt(e, s);
        g = { kind: "draw", last: s, lastV: v };
        setStep(l, s, (st) => { st.v = v; }, true);
      }
      e.preventDefault();
    };
    strip.onpointermove = (e) => {
      if (!g) return;
      const L = lane(l);
      if (g.kind === "note") {
        if (g.base === null) return;
        const d = Math.round((g.y - e.clientY) / 9);
        if (!d && !g.moved) return;
        g.moved = true;
        const n = stepBy(snap(g.base, maskOf(L)), d, maskOf(L));
        if (L.steps[g.s].n[0] !== n) setStep(l, g.s, (st) => { st.n[0] = n; }, true);
      } else {
        const s = cellAt(e), v = valueAt(e, s), n = Math.abs(s - g.last), dir = s > g.last ? 1 : -1;
        const put = (k, x) => { if (L.steps[k].v !== x) { L.steps[k].v = x; sendStep(l, k); } };
        if (!n) put(s, v);
        for (let j = 1; j <= n; j++) put(g.last + dir * j, Math.round(g.lastV + ((v - g.lastV) * j) / n));   // (a line from the step before)
        g.last = s; g.lastV = v;
        keep(); redrawCells(l, true);
      }
    };
    strip.onpointerup = strip.onpointercancel = () => { if (g && g.kind === "note" && !g.moved) undo.pop(); g = null; };
    strip.ondblclick = (e) => { const s = cellAt(e); setStep(l, s, (st) => { st.f ^= F_MUTE; }); };
  }

  function drawPlayhead() {
    if (!live()) return;
    if (ui.play) { ui.play.textContent = run.playing ? "STOP" : "PLAY"; ui.play.classList.toggle("on", !!run.playing); }
    ui.cells.forEach((cells, l) => cells && cells.forEach((c, s) => c.classList.toggle("ph", run.pos[l] === s)));
  }

  function drawInspector() {
    if (!live()) return;
    const box = ui.insp, { l, s } = Q.sel, L = lane(l), E = EV[L.ev], st = L.steps[s];
    box.textContent = "";
    box.append(el("h3", "", `LANE ${l + 1} · STEP ${s + 1} · ${E.n}`));
    const row = el("div", "seqrow");
    if (E.note) {
      const m = maskOf(L);
      for (let i = 0; i < 4; i++) {
        const x = st.n[i], empty = x === null || (i === 0 && x === TIE);
        const chip = el("button", "slot" + (Q.sel.slot === i ? " on" : "") + (empty ? " empty" : ""),
          empty ? (i === 0 && x === TIE ? "TIE" : `+ NOTE ${i + 1}`) : noteName(snap(x, m)));
        chip.type = "button"; chip.title = i ? "A chord note: pick, then play a key" : "The step's note";
        chip.onclick = () => { Q.sel.slot = i; drawInspector(); };
        row.append(chip);
        if (i > 0 && !empty) {
          const x_ = el("button", "quiet small", "×"); x_.type = "button"; x_.title = `Take note ${i + 1} out of the chord`;
          x_.onclick = () => { setStep(l, s, (q) => { q.n[i] = null; }); Q.sel.slot = 0; };
          row.append(x_);
        }
      }
    } else {
      const r = el("input"); r.type = "range"; r.min = "0"; r.max = "127"; r.value = String(st.v);
      r.style.setProperty("--fill", (st.v / 127) * 100 + "%");
      const out = el("output", "val", E.f(st.v));
      let started = false;
      r.oninput = () => {
        if (!started) { mark(); started = true; }
        const v = parseInt(r.value, 10);
        setStep(l, s, (q) => { q.v = v; }, true, true);
        out.textContent = E.f(v); r.style.setProperty("--fill", (v / 127) * 100 + "%");
      };
      r.onchange = () => { started = false; };
      const w = el("div", "ctl wide"); const t = el("div", "top"); t.append(el("label", "lab", "VALUE"), out); w.append(t, r);
      row.append(w);
    }
    [["REST", F_MUTE, "The step plays nothing (Delete)"], ["TIE", F_TIE, "Hold the notes before it through this step (.)"], ["SKIP", F_SKIP, "Leave the step out: the lane jumps over it"]].forEach(([n, f, t]) => {
      const b = el("button", "chip" + (st.f & f ? " on" : ""), n);
      b.type = "button"; b.title = t; b.setAttribute("aria-pressed", String(!!(st.f & f)));
      b.onclick = () => setStep(l, s, (q) => { q.f ^= f; });
      row.append(b);
    });
    [["LOOP START", "ls"], ["LOOP END", "le"]].forEach(([n, k]) => {   // (the loop rail's ends, for a phone too)
      const b = el("button", "chip" + (L[k] === s ? " on" : ""), n);
      b.type = "button"; b.title = `The lane's loop ${k === "ls" ? "starts" : "ends"} on this step`;
      b.onclick = () => setFooter(l, k, s);
      row.append(b);
    });
    box.append(row);
    if (E.note) box.append(piano(l, s));
  }

  /* two octaves to click into the selected note slot, the scale's notes marked */
  function piano(l, s) {
    const L = lane(l), m = maskOf(L), wrap = el("div", "kbd"), cur = L.steps[s].n[Q.sel.slot];
    const ctl = el("div", "seqrow");
    const dn = el("button", "quiet small", "OCT −"), up = el("button", "quiet small", "OCT +");
    dn.type = up.type = "button";
    dn.onclick = () => { Q.oct = Math.max(0, Q.oct - 1); keep(); drawInspector(); };
    up.onclick = () => { Q.oct = Math.min(8, Q.oct + 1); keep(); drawInspector(); };
    ctl.append(dn, el("span", "octv", `C${Q.oct} – B${Q.oct + 1}`), up);
    const keys = el("div", "keys");
    for (let k = 0; k < 24; k++) {
      const n = (Q.oct + 1) * 12 + k, black = [1, 3, 6, 8, 10].includes(k % 12);
      const b = el("button", "key" + (black ? " b" : " w") + (m && !inScale(n, m) ? " off" : "") + (cur === n ? " on" : ""));
      b.type = "button"; b.title = noteName(n); b.setAttribute("aria-label", noteName(n));
      if (n > 126) b.disabled = true;
      b.onclick = (e) => { Q.sel = { l, s, slot: Q.sel.slot }; enterNote(n, e.shiftKey); };
      keys.append(b);
    }
    wrap.append(ctl, keys);
    return wrap;
  }

  // ------------------------------------------------------------------------------------------- keys
  document.addEventListener("keydown", (e) => {
    if (!live()) return;
    const t = e.target;
    if (t && (t.tagName === "INPUT" || t.tagName === "SELECT" || t.tagName === "TEXTAREA")) return;
    const { l, s } = Q.sel, L = lane(l), E = EV[L.ev], k = e.key.toLowerCase();
    if ((e.ctrlKey || e.metaKey) && k === "z") { undoLast(); e.preventDefault(); return; }
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const pi = PIANO.indexOf(k);
    if (pi >= 0) { enterNote((Q.oct + 1) * 12 + pi, e.shiftKey); e.preventDefault(); return; }
    if (k === "z" || k === "x") { Q.oct = Math.max(0, Math.min(8, Q.oct + (k === "z" ? -1 : 1))); keep(); drawInspector(); api.status(`Octave C${Q.oct}.`); e.preventDefault(); return; }
    if (k >= "1" && k <= "4") { select(+k - 1, Q.sel.s); e.preventDefault(); return; }
    if (e.key === "ArrowLeft" || e.key === "ArrowRight") { moveSel(e.key === "ArrowLeft" ? -1 : 1); e.preventDefault(); return; }
    if (e.key === "ArrowUp" || e.key === "ArrowDown") {
      const d = e.key === "ArrowUp" ? 1 : -1;
      if (E.note) {
        const st = L.steps[s], i = Q.sel.slot, x = st.n[i];
        if (x === null || x === TIE) return;
        const m = maskOf(L);
        setStep(l, s, (q) => { q.n[i] = e.shiftKey ? Math.max(0, Math.min(126, x + 12 * d)) : stepBy(snap(x, m), d, m); });
      } else setStep(l, s, (q) => { q.v = Math.max(0, Math.min(127, q.v + d * (e.shiftKey ? 8 : 1))); });
      e.preventDefault(); return;
    }
    if (e.key === "Delete" || e.key === "Backspace") { setStep(l, s, (q) => { q.f ^= F_MUTE; }); e.preventDefault(); return; }
    if (e.key === ".") { setStep(l, s, (q) => { q.f ^= F_TIE; }); e.preventDefault(); return; }
    if (e.key === " ") { sx(CMD.PLAY, [run.playing ? 0 : 1], run.playing ? "SEQ stop" : "SEQ play"); e.preventDefault(); }
  });

  // ------------------------------------------------------------------------------------------- files
  function saveFile() {
    const blob = new Blob([JSON.stringify({ format: "carnac-seq", version: 1, name: Q.name, lanes: Q.lanes }, null, 1)], { type: "application/json" });
    const a = el("a");
    a.href = URL.createObjectURL(blob);
    a.download = (Q.name || "sequence").toLowerCase().replace(/[^a-z0-9]+/g, "-") + ".carnac-seq.json";
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }
  function loadFile() {
    const inp = el("input"); inp.type = "file"; inp.accept = ".json,application/json";
    inp.onchange = () => {
      const f = inp.files[0];
      if (!f) return;
      f.text().then((t) => {
        const j = JSON.parse(t);
        if (j.format !== "carnac-seq" || !Array.isArray(j.lanes) || j.lanes.length !== NLANE) throw new Error("not a Carnac sequence file");
        mark();
        Q.lanes = j.lanes; Q.name = (j.name || "").toUpperCase().slice(0, NAMELEN);
        for (let l = 0; l < NLANE; l++) sendLane(l);
        sx(CMD.NAME, nameBytes(Q.name), `SEQ name ${Q.name}`);
        keep(); if (ui.name) ui.name.value = Q.name; redrawAll();
        api.status(can() ? "Loaded and sent to the synth (SAVE keeps it in a slot)." : "Loaded into the editor.");
      }).catch((e) => api.status("Could not load: " + e.message, true));
    };
    inp.click();
  }

  // ------------------------------------------------------------------------------------------- the page's
  window.CarnacSeq = {
    init(a) { api = a; },
    build,
    take,
    opened() { request(); },
    _q: () => Q,                                 // (tests)
  };
})();
