/* SPDX-License-Identifier: GPL-3.0-only
   Carnac web editor: the way out. One module owns every byte the editor sends, so a later CC OUT echo, a SysEx dump
   or the SEQ control channel plug in here (and in sysex.js) without touching the page.

   Targets: a Web MIDI output (the FM1 over USB), the browser emulator (web/emu, open in another tab of the same site:
   a BroadcastChannel, no MIDI port needed), or nowhere (the log only). In: every MIDI input whose name matches the
   FM1's (CC OUT's CCs and NRPNs, the dumps), handed to the page as "in" events.

   The messages are the firmware's (src/dsp/synth_runtime.c cc_apply, src/UIStateMachine.c ui_sm_cc / ui_sm_nrpn):
   a CC is one message; an NRPN is 99 (MSB), 98 (LSB), 6 (data; then 38, its LSB, for a value past 127: INT TEMPO),
   sent whole every time, so a synth that rebooted or another sender on the channel can never leave a stale
   parameter selected; an RPN the same over 101 / 100;
   a Program Change goes after BANK SELECT (CC 0 = what it loads, CC 32 = 0). */
(function () {
  "use strict";
  const EMU_CHANNEL = "carnac-midi";        // web/emu/index.html listens on this BroadcastChannel
  let access = null, port = null, target = "none", bc = null, emuSeen = 0;
  const listeners = { log: [], change: [], emu: [], ports: [], in: [] };
  const IN_NAME = /fm-?1|carnac|x0x/i;
  let sysexOk = false;
  const emit = (k, x) => listeners[k].forEach((f) => { try { f(x); } catch (e) { console.error(e); } });

  function openBc() {
    if (bc || typeof BroadcastChannel === "undefined") return;
    bc = new BroadcastChannel(EMU_CHANNEL);
    bc.onmessage = (e) => {
      if (e.data && e.data.type === "here") { emuSeen = Date.now(); emit("emu", e.data); }
      else if (e.data && e.data.type === "sysex-out" && target === "emu") {   // the emulator's SysEx: F0..F7, back to back
        const b = e.data.bytes || [];
        for (let i = 0; i < b.length;) {
          let j = i;
          while (j < b.length && b[j] !== 0xF7) j++;
          if (b[i] === 0xF0) emit("in", b.slice(i, j + 1));
          i = j + 1;
        }
      }
    };
  }

  async function init() {
    openBc();
    if (!navigator.requestMIDIAccess) return false;
    try {
      access = await navigator.requestMIDIAccess({ sysex: true });    // (the dump: ED_PAR_REQ, ED_PAR_DUMP)
      sysexOk = true;
    } catch (_) {
      try {
        access = await navigator.requestMIDIAccess({ sysex: false });   // (refused SysEx: CC / NRPN only)
      } catch (__) {
        access = null;
        return false;
      }
    }
    access.onstatechange = () => {
      listen();
      emit("ports");                           // (the page lists them again and re-selects)
    };
    listen();
    return true;
  }

  function listen() {
    if (!access) return;
    for (const i of access.inputs.values()) {
      if (!IN_NAME.test(i.name || "") || i.onmidimessage) continue;
      i.onmidimessage = (e) => {
        const b = Array.from(e.data);
        if (b[0] === 0xF8 || b[0] === 0xFE) return;   // (clock, active sensing)
        emit("in", b);
      };
    }
  }

  function outputs() {
    const list = [];
    if (access) for (const o of access.outputs.values()) list.push({ id: "port:" + o.id, name: o.name || o.id });
    if (bc) list.push({ id: "emu", name: "Carnac emulator (play/ in another tab)" });
    list.push({ id: "none", name: "Nowhere (log only)" });
    return list;
  }

  function select(id) {
    target = id || "none";
    port = null;
    if (target.startsWith("port:") && access) {
      port = access.outputs.get(target.slice(5)) || null;
      if (!port) target = "none";
    }
    if (target === "emu" && bc) bc.postMessage({ type: "ping" });
    emit("change");
    return target;
  }

  function send(bytes, what) {
    if (target.startsWith("port:") && port) {
      try { port.send(bytes); } catch (e) { emit("log", { bytes, what: "FAILED: " + e.message }); return false; }
    } else if (target === "emu" && bc && bytes[0] === 0xF0) {
      bc.postMessage({ type: "sysex", bytes });
    } else if (target === "emu" && bc) {
      for (let i = 0; i < bytes.length; i += 3) bc.postMessage({ type: "midi", b0: bytes[i], b1: bytes[i + 1] | 0, b2: bytes[i + 2] | 0 });
    }
    emit("log", { bytes, what });
    return true;
  }

  const ch7 = (ch) => ch & 0x0F, v7 = (v) => Math.max(0, Math.min(127, v | 0));
  const api = {
    init, outputs, select, send,
    get target() { return target; },
    get sysexOk() { return sysexOk; },
    get emuSeen() { return emuSeen; },
    on(kind, fn) { listeners[kind].push(fn); },
    pingEmu() { if (bc) bc.postMessage({ type: "ping" }); },
    cc(ch, num, val, what) { return send([0xB0 | ch7(ch), v7(num), v7(val)], what || `CC ${num} = ${v7(val)}`); },
    nrpn(ch, msb, lsb, val, what) {
      const s = 0xB0 | ch7(ch);
      return send([s, 99, v7(msb), s, 98, v7(lsb), s, 6, v7(val)], what || `NRPN ${msb}:${lsb} = ${v7(val)}`);
    },
    nrpn14(ch, msb, lsb, val, what) {          // a value past 127: DATA ENTRY MSB, then LSB
      const s = 0xB0 | ch7(ch), v = Math.max(0, Math.min(16383, val | 0));
      return send([s, 99, v7(msb), s, 98, v7(lsb), s, 6, v >> 7, s, 38, v & 0x7F], what || `NRPN ${msb}:${lsb} = ${v}`);
    },
    sysex(bytes, what) { return sysexOk || target === "emu" ? send(bytes, what) : false; },
    get canSysex() { return target === "emu" ? !!bc : sysexOk && target.startsWith("port:"); },
    rpn(ch, msb, lsb, val, what) {
      const s = 0xB0 | ch7(ch);
      return send([s, 101, v7(msb), s, 100, v7(lsb), s, 6, v7(val)], what || `RPN ${msb}:${lsb} = ${v7(val)}`);
    },
    program(ch, bank, prog, what) {
      const s = 0xB0 | ch7(ch);
      return send([s, 0, v7(bank), s, 32, 0, 0xC0 | ch7(ch), v7(prog)], what || `BANK ${bank} PC ${v7(prog)}`);
    },
  };
  window.CarnacMidi = api;
})();
