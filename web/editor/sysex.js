/* SPDX-License-Identifier: GPL-3.0-only
   Carnac web editor: the SysEx codec (the firmware's CC OUT, step 4, 2026-11-18: src/hardware/synth_panel.c).

     CarnacSysex.supported         true: the firmware answers a dump request
     CarnacSysex.request()         -> the bytes of ED_PAR_REQ (F0 7D 46 4C 56 F7): the FM1 sends the whole sound
     CarnacSysex.decode(bytes)     a message from the synth -> { kind: "params", layer, entries: [[msb, lsb, value]] }
                                   (ED_PAR_DUMP 57: layer, then MSB LSB value-MSB value-LSB per parameter, up to 24 a
                                   message), { kind: "end", count } (ED_PAR_END 58), or null: not ours

   A value is the NRPN's (exact: the stored value; WAVE and ARCH the place in their lists). The FM1 sends a dump when
   asked (any time), at every load and when CC OUT turns on (CC OUT on); its later moves come as CC / NRPN. */
(function () {
  "use strict";
  const HDR = [0xF0, 0x7D, 0x46, 0x4C];
  const ED_PAR_REQ = 56, ED_PAR_DUMP = 57, ED_PAR_END = 58;
  const ours = (b) => b.length >= 6 && HDR.every((x, i) => b[i] === x);
  window.CarnacSysex = {
    supported: true,
    request: () => HDR.concat([ED_PAR_REQ, 0xF7]),
    decode(b) {
      if (!ours(b)) return null;
      if (b[4] === ED_PAR_END) return { kind: "end", count: (b[5] | 0) | ((b[6] | 0) << 7) };
      if (b[4] !== ED_PAR_DUMP) return null;
      const entries = [];
      for (let i = 6; i + 3 < b.length - 0 && b[i] !== 0xF7; i += 4) entries.push([b[i], b[i + 1], (b[i + 2] << 7) | b[i + 3]]);
      return { kind: "params", layer: b[5], entries };
    },
  };
})();
