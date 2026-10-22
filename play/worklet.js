// SPDX-License-Identifier: GPL-3.0-only
// Carnac in an AudioWorklet, after X0X's web/emu/worklet.js (github.com/charlesvestal/fm1-x0x): carnac.wasm is the
// FM1's app on a simulated FM1 (src/host/carnac_web.c). Each render quantum asks it for 128 frames, which runs the
// device's clock forward; between quanta the page's input goes in and, about 30 times a second, the screen, the
// lights and (after a SAVE) the flash the page keeps go out.

const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();

class Carnac extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.sentBlits = -1;
    this.sentWrites = -1;
    this.flashDue = 0;
    this.lastFrame = 0;
    this.busy = 0;
    this.frames = 0;
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      const wasi = {
        clock_time_get: (id, prec, out) => {
          new DataView(this.mem.buffer).setBigUint64(out, BigInt(Math.round(currentTime * 1e9)), true);
          return 0;
        },
        fd_write: (fd, iov, n, out) => { new DataView(this.mem.buffer).setUint32(out, 0, true); return 0; },
        proc_exit: () => {},
      };
      const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
      const env = new Proxy({}, { get: () => () => 0 });
      const { instance } = await WebAssembly.instantiate(m.wasm, { wasi_snapshot_preview1: stub, env });
      this.ex = instance.exports;
      this.mem = this.ex.memory;
      if (this.ex._initialize) this.ex._initialize();
      this.ex.web_nor_erase();                        // a new unit's flash, then what this browser kept of it
      if (m.flash) {
        const b = new Uint8Array(m.flash).subarray(0, this.ex.web_nor_len());
        new Uint8Array(this.mem.buffer, this.ex.web_nor(), b.length).set(b);
      }
      this.ex.web_master(m.master ?? 768);
      this.ex.web_boot();
      this.sentWrites = this.ex.web_nor_writes();
      this.port.postMessage({ type: "ready" });
    } else if (!this.ex) {
      return;
    } else if (m.type === "buttons") {
      this.ex.web_buttons(m.mask >>> 0);
    } else if (m.type === "keys") {
      this.ex.web_keys(m.mask >>> 0);
    } else if (m.type === "enc") {
      this.ex.web_enc(m.role, m.n | 0);
    } else if (m.type === "master") {
      this.ex.web_master(m.value | 0);
    } else if (m.type === "midi") {
      this.ex.web_midi(m.b0 & 0xFF, m.b1 & 0x7F, m.b2 & 0x7F);
    }
  }

  publish() {
    const ex = this.ex;
    const blits = ex.web_blits();
    const msg = { type: "frame", buttons: ex.web_lit_buttons() >>> 0, keys: ex.web_lit_keys() >>> 0,
                  dim: ex.web_dim_keys() >>> 0, dimDiv: ex.web_dim_div(), red: ex.web_play_red() };
    if (this.frames >= 44100) {                       // the share of real time spent running the device
      msg.load = Math.round(100 * this.busy / (this.frames / 44.1));
      this.busy = this.frames = 0;
    }
    const transfer = [];
    if (blits !== this.sentBlits) {
      this.sentBlits = blits;
      msg.fb = new Uint16Array(this.mem.buffer, ex.web_fb(), 240 * 240).slice();
      transfer.push(msg.fb.buffer);
    }
    // the flash, once its writes have settled (a SAVE is several sector writes): the page keeps it
    const writes = ex.web_nor_writes();
    if (writes !== this.sentWrites) {
      this.sentWrites = writes;
      this.flashDue = currentTime + 0.5;
    } else if (this.flashDue && currentTime >= this.flashDue) {
      this.flashDue = 0;
      msg.flash = new Uint8Array(this.mem.buffer, ex.web_nor(), ex.web_nor_len()).slice();
      transfer.push(msg.flash.buffer);
    }
    this.port.postMessage(msg, transfer);
  }

  process(inputs, outputs) {
    if (!this.ex) return true;
    const out = outputs[0];
    const n = out[0].length;
    const t0 = clock();
    this.ex.web_render(n);
    this.busy += clock() - t0;
    this.frames += n;
    out[0].set(new Float32Array(this.mem.buffer, this.ex.web_out_l(), n));
    if (out[1]) out[1].set(new Float32Array(this.mem.buffer, this.ex.web_out_r(), n));
    if (currentTime - this.lastFrame >= 1 / 30) {
      this.lastFrame = currentTime;
      this.publish();
    }
    return true;
  }
}

registerProcessor("carnac", Carnac);
