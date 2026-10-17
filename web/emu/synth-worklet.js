// AudioWorkletProcessor for the synth core (web/emu/synth.wasm, from build.sh). Same shape as the Felucca
// template (felucca-firmware/web/emu/worklet.js): the worklet thread has no fetch and no document, so the page
// fetches synth.wasm and posts its bytes here. The module is standalone (no imports), so it instantiates on its
// own. The core renders Q15 stereo into its own buffer at full level, limited to -1 dBFS (synth_runtime.c); process()
// only scales Q15 to the float range Web Audio wants and copies it to the output.

const Q15_TO_FLOAT = 1 / 32768;

class SynthCoreProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.maxFrames = 128;
    this.played = new Set();                         // SEQ page: notes the core did not take and that are sounding
    this.port.onmessage = (e) => this.onMessage(e.data);
  }

  async onMessage(m) {
    if (m.type === "load") {
      try {
        const { instance } = await WebAssembly.instantiate(m.wasm, {});
        const ex = instance.exports;
        ex._initialize();
        ex.web_init();
        this.ex = ex;
        this.maxFrames = ex.web_max_frames();
        this.port.postMessage({ type: "ready", rate: ex.web_sample_rate(), maxFrames: this.maxFrames });
      } catch (err) {
        this.port.postMessage({ type: "error", message: String((err && err.message) || err) });
      }
    } else if (m.type === "midi" && this.ex) {
      this.ex.web_midi(m.status | 0, m.data1 | 0, m.data2 | 0);
    } else if (m.type === "knob" && this.ex) {
      this.ex.web_knob_set(m.page | 0, m.layer | 0, m.knob | 0, m.value | 0);
    } else if (m.type === "read" && this.ex) {
      // the page's four knobs for the layer's voice architecture: position (255 where the core has no byte: a greyed
      // knob), name and kind (UIKNOB_*), plus which pages that architecture shows,
      // its name and the voices the layer may sound
      const ex = this.ex, page = m.page | 0, layer = m.layer | 0;
      const ks = [0, 1, 2, 3];
      const values = ks.map((k) => ex.web_knob_get(page, layer, k));
      const labels = ks.map((k) => this.cstr(ex.web_knob_label(page, layer, k)) || "");
      const kinds = ks.map((k) => ex.web_knob_kind(page, layer, k));
      const visible = [];
      for (let p = 0; p < m.npages; p++) visible.push(!!ex.web_page_visible(p, layer));
      const arch = ex.web_layer_arch(layer);
      // SEQ step knobs (UIKNOB_SEQ = 7): the value as the screen's knob card reads it ("C#3", "x8", "BOTH"); list knobs
      // (UIKNOB_LIST = 8: SUB OCTAVE, XMOD TYPE): the entry's name
      const texts = ks.map((k) => (kinds[k] === 7 ? this.cstr(ex.web_seq_knob_text(k)) :
                                   kinds[k] === 8 ? this.cstr(ex.web_knob_text(page, layer, k)) : null));
      this.port.postMessage({ type: "knobs", page: m.page, layer: m.layer, values, labels, kinds, visible, texts,
                              arch: this.cstr(ex.web_arch_name(arch)), voices: ex.web_layer_voices(layer) });
    } else if (m.type === "perf" && this.ex) {
      this.ex.web_perf_mode(m.mode | 0);
      this.postRows();
    } else if (m.type === "ui" && this.ex) {
      // the Select and Algorithm encoders and the page / layer they act on (the core's UI state machine)
      const ex = this.ex;
      if (m.op === "page") ex.web_ui_page(m.page | 0);
      else if (m.op === "layer") ex.web_ui_layer(m.layer | 0);
      else if (m.op === "select") ex.web_ui_select(m.detents | 0);
      else if (m.op === "algo") ex.web_ui_algo(m.detents | 0);
      else if (m.op === "preset") ex.web_ui_preset(m.detents | 0);       // the Preset Encoder: loads the page's kind
      else if (m.op === "transport") ex.web_ui_transport(m.which | 0);   // 0 PLAY, 1 REC
      else if (m.op === "button") ex.web_ui_button(m.btn | 0);           // a section button (BTN_*), as the FM1 sends it
      this.postRows();
      this.postSeqState();
    } else if (m.type === "seqkey" && this.ex) {
      // A key on the SEQ page: the core takes the span of the bottom 16 white keys as lane / step-group selectors and
      // loop shortcuts, black keys included (ignored) (7.1), and records into the step in step record (7.2). What it
      // does not take is played. A note that was played always gets
      // its note-off, even if REC changed in between, so no note can hang.
      const ex = this.ex, note = m.note | 0;
      this.ledBase = note - (m.key | 0);
      this.ledMods = m.mods | 0;
      // (the ARP page's chord and step keys, 8.1, go to the core the same way)
      const taken = ex.web_seq_key(m.key | 0, note, m.down ? 1 : 0, m.mods | 0) ||
                    ex.web_arp_key(m.key | 0, note, m.down ? 1 : 0, m.vel | 0);
      if (m.down && !taken) {
        ex.web_midi(0x90, note, m.vel | 0);
        this.played.add(note);
      } else if (!m.down && this.played.has(note)) {
        ex.web_midi(0x80, note, 0);
        this.played.delete(note);
      }
      this.postRows();
      this.postSeqState();
    } else if (m.type === "screen" && this.ex) {
      this.ledBase = m.base | 0;                                 // the keyboard's lowest note and the OCT buttons
      this.ledMods = m.mods | 0;                                 // held, for the key LEDs
      this.postScreen(!!m.force);
    } else if (m.type === "seqpalette" && this.ex) {
      this.ex.web_seq_palette(m.index | 0);
      this.postScreen(true);
    }
  }

  postSeqState() {
    const ex = this.ex;
    this.port.postMessage({ type: "seqstate", playing: !!ex.web_seq_playing(), recording: !!ex.web_seq_recording(),
                            arpPos: ex.web_arp_pos(), arpHeld: ex.web_arp_held(), leds: this.keyLeds() });
  }

  // the key LEDs (ui_sm_key_leds): [lit, mid, dim], bit k = k keys above the lowest
  keyLeds() {
    const ex = this.ex, b = this.ledBase === undefined ? 48 : this.ledBase, mods = this.ledMods | 0;
    return [ex.web_key_leds(b, mods, 0) >>> 0, ex.web_key_leds(b, mods, 1) >>> 0, ex.web_key_leds(b, mods, 2) >>> 0];
  }

  // The firmware's screen (src/ui_page_draw.c, which hands SEQ and ARP to their own renderers) on the core's stand-in
  // canvas: only the regions it redrew, each with its pixels (RGB565) and its text, for the page to paint over what it
  // already shows.
  postScreen(force) {
    const ex = this.ex;
    const now = currentTime, ms = this.lastScreen === undefined ? 0 : Math.round((now - this.lastScreen) * 1000);
    this.lastScreen = now;                                       // the UI clock: the preset list closes on its own
    const n = ex.web_screen_draw(force ? 1 : 0, Math.max(0, ms));
    const buf = ex.memory.buffer;
    const fb = new Uint16Array(buf, ex.web_seq_fb(), 240 * 240);
    const tab = new Uint16Array(buf, ex.web_seq_blits(), n * 6);
    const pos = new Int16Array(buf, ex.web_seq_text_pos(), 160 * 2);
    const col = new Uint16Array(buf, ex.web_seq_text_col(), 160);
    const face = new Uint8Array(buf, ex.web_seq_text_face(), 160);
    const strBase = ex.web_seq_text_str(), strLen = ex.web_seq_text_len();
    const blits = [], transfer = [];
    for (let b = 0; b < n; b++) {
      const x = tab[b * 6], y = tab[b * 6 + 1], w = tab[b * 6 + 2], h = tab[b * 6 + 3];
      const t0 = tab[b * 6 + 4], tn = tab[b * 6 + 5];
      const px = new Uint16Array(w * h);
      for (let r = 0; r < h; r++) px.set(fb.subarray((y + r) * 240 + x, (y + r) * 240 + x + w), r * w);
      const texts = [];
      for (let t = t0; t < t0 + tn; t++)
        texts.push({ x: pos[t * 2], y: pos[t * 2 + 1], col: col[t], face: String.fromCharCode(face[t]),
                     s: this.cstr(strBase + t * strLen) });
      blits.push({ x, y, w, h, px, texts });
      transfer.push(px.buffer);
    }
    this.port.postMessage({ type: "screen", blits, playing: !!ex.web_seq_playing(), recording: !!ex.web_seq_recording(),
                            arpPos: ex.web_arp_pos(), arpHeld: ex.web_arp_held(), leds: this.keyLeds() }, transfer);
  }

  // the page's selectable rows: label, stored value, value text (null = plain number), whether it is highlightable and
  // whether it is hidden (UIROW_HIDDEN = 3: SEQ TARGET with one option; the page skips it, the cursor never lands on it);
  // and where the core is: its page (the browser's id and the raw UIPAGE_*) and the layer in focus
  postRows() {
    const ex = this.ex;
    const rows = [];
    const n = ex.web_ui_count();
    for (let r = 0; r < n; r++) {
      rows.push({
        label: this.cstr(ex.web_ui_label(r)),
        value: ex.web_ui_value(r),
        text: this.cstr(ex.web_ui_text(r)),
        active: !!ex.web_ui_active(r),
        hidden: ex.web_ui_state(r) === 3,
      });
    }
    this.port.postMessage({ type: "rows", cursor: ex.web_ui_cursor(), rows, wpage: ex.web_ui_wpage(),
                            uipage: ex.web_ui_uipage(), layer: ex.web_ui_focus_layer(), saveArmed: !!ex.web_ui_save_armed(),
                            perf: ex.web_perf_mode(255),   // a loaded performance brings its mode
                            preset: { target: ex.web_ui_preset_target(), slot: ex.web_ui_preset_slot() } });
  }

  // a NUL-terminated ASCII string in the module's memory; 0 (no string) reads as null
  cstr(ptr) {
    if (!ptr) return null;
    const bytes = new Uint8Array(this.ex.memory.buffer);
    let s = "";
    for (let i = ptr; bytes[i] !== 0; i++) s += String.fromCharCode(bytes[i]);
    return s;
  }

  process(inputs, outputs) {
    const ex = this.ex;
    const out = outputs[0];
    if (!ex || !out || out.length === 0) return true;

    const n = out[0].length;                         // 128 frames per render quantum
    const frames = Math.min(n, this.maxFrames);
    ex.web_render(frames);
    // Re-read the view each quantum: the wasm memory can be replaced if it ever grows.
    const lr = new Int32Array(ex.memory.buffer, ex.web_out_ptr(), frames * 2);

    const left = out[0];
    const right = out.length > 1 ? out[1] : null;
    for (let i = 0; i < frames; i++) {
      const l = lr[2 * i] * Q15_TO_FLOAT;
      const r = lr[2 * i + 1] * Q15_TO_FLOAT;
      if (right) {
        left[i] = l;
        right[i] = r;
      } else {
        left[i] = 0.5 * (l + r);                    // mono output device: fold the stereo pair
      }
    }
    for (let i = frames; i < n; i++) {
      left[i] = 0;
      if (right) right[i] = 0;
    }
    return true;
  }
}

registerProcessor("synth-core", SynthCoreProcessor);
