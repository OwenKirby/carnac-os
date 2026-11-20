# Carnac editor (draft)

A browser page that edits a Carnac sound on the FM1 over Web MIDI: CCs, NRPNs and Program Changes, grouped like
the panel's pages. Plain HTML/CSS/JS, no build step.

## Opening it

- **Locally:** open `web/editor/index.html` in Chrome or Edge (Web MIDI). `file://` works because the scripts
  are plain scripts, not modules. You can also serve the folder: `python -m http.server` in `web/`, then open
  `http://localhost:8000/editor/`.
- Pick **OUT** (the FM1's USB port; the editor picks a port named FM1 / Carnac / M-VAVE if it finds one), and
  **CH**, the layer's channel (GLOBAL's MIDI CH LAYER1 / LAYER2).
- **SEND:** *CC* sends a CC where the map has one, NRPN otherwise. *NRPN always* sends the exact stored value
  for everything.
- **Without hardware:** pick *Carnac emulator* and open `web/emu/` (the site's `play/`) in another tab of the
  same site. The two talk over a `BroadcastChannel("carnac-midi")`: MIDI and, since 2026-11-19, SysEx both ways
  (RECEIVE and the SEQ tab work against the emulator). `carnac.wasm` is not in git: build it with
  `web/emu/build.sh` (needs `x0x-firmware/` and emsdk) from a tree with the CC map and the SEQ SysEx.

## Files

| file | what |
|---|---|
| `index.html`, `style.css` | the page: the THEME's colours (SAMHAIN until the FM1 says otherwise), the site's rune face, hewn tablets; one column on a phone, up to four wide |
| `viz.js` | the pictures, in whole pixels like the FM1's screen: moon dials, the oscillator's frames, the mixer's menhirs, noise fog, filter response, envelope barrows (draggable, a light running in real time), LFO shapes with a moving playhead and moon, the delay's stones and the reverb's fog |
| `app.js` | the controls, built from `params.js`. It holds values as the firmware stores them and works out each CC from them the way `ui_sm_cc` reads it back |
| `midi.js` | the only module that sends bytes: Web MIDI port, emulator, or log only. CC, NRPN (99/98/6, sent whole every time), RPN, BANK SELECT + PC |
| `sysex.js` | the sound dump's codec (ED_PAR_REQ 56 / DUMP 57 / END 58) |
| `seq.js` | the SEQ tab: its own SysEx (60-69, below), the live sequence, SAVE into a slot |
| `gen_params.py` | reads the firmware's tables and writes `params.js` |
| `params.js` | **generated**: do not edit |
| `check_params.py` | spot-checks `params.js` against the C source in a second, independent way |

Regenerating (the CC map is on `proto/cc-map` for now, so point it at a tree that has it):

    python web/editor/gen_params.py <path/to/src> -o web/editor/params.js
    python web/editor/check_params.py <path/to/src> web/editor/params.js

`gen_params.py` reads `CC_MAP`, `UI_PAGES`, every `*_ROWS` and `*_KNOBS` table, the name lists,
`ARCH_ORDER`, the `varch_uses_*` helpers and the MFX tables in `src/UIStateMachine.c`. It evaluates their
sizes from the enums and `#define`s in `src/**/*.h`, and reads the PCM wave names from `src/dsp/pcm_area*.bin`.
It stops with an error if a table changes shape. The one piece it cannot read is `ui_sm_knob`'s `switch`
(which knob table a page uses), which is code. `PAGE_KNOBS` in the script mirrors it.

## What works today (against the CC map's code)

- Every mapped CC and every NRPN page `ui_sm_nrpn` accepts: HOME (LAYER rows, ARCH by NRPN), OSC 1 / 2, OSC MIX
  with the noise block (CC 115-119, every architecture), FILTER 1 / 2, FILTER MIXER, ENV 1-3, LFO 1-3, AMP
  with the MFX type and its four knobs, FX (delay, reverb), MIX, the sends, TUNING, ZONES, GLOBAL, and the 16 MOD
  routes (NRPN 64+route, LSB 0-4).
- Relabelling: the OSC MIX and FILTER MIX knobs follow the ARCH, the MFX knobs follow the MFX type, and FILTER
  knob 1 follows FLAVOUR. Pages the ARCH lacks are dimmed (OSC2 without OSC2, FILTER 2, FILTER MIX), and rows it
  hides are left out.
- Program Change: CC 0 = PATCH 0 / PERF 1 / SEQ 2 / ARP 3, CC 32 = 0, then PC.
- SEND EDITED (only what you moved or loaded) and SEND ALL (everything the ARCH shows) send ARCH first, then the
  MFX type, then the rest, about 6 ms apart, because the FM1's controller queue holds 32. SAVE / LOAD FILE writes
  the editor's values as JSON. PANIC sends CC 123 + 120.
- Checked: `check_params.py` runs 29 checks and all pass. The page runs in headless Chrome with no console
  errors: every tab under 4 architectures, controls moved, the MIDI log captured. The logged bytes were fed
  through the firmware's own `synth_midi_3` → `synth_cc_take` → `ui_sm_cc` / `ui_sm_nrpn`, in a host build of
  the `proto/cc-map` tree, and every value landed as sent, in both CC and NRPN mode.

## Stubbed / not there

- **RECEIVE FROM SYNTH:** disabled. It needs CC OUT or a SysEx dump. Until then the editor shows what it last
  sent, and a control never moved is drawn dim and sits at a default.
- **The MFX's rows** (MODE, POLARITY, AGE, COLOUR, WIDTH, ROOT, CHORD, LEVEL, LOW) are shown greyed. No CC or
  NRPN reaches them: AMP's page has one row (DRV MODEL), so NRPN 13:5+ is refused.
- **WAVE names** are the banks plus the PCM waves *as built in this tree*. An FM1 with other waves uploaded
  has a different list, and since WAVE's CC and NRPN pick by place in the list, the names could be wrong (the
  sound stays one the synth has).
- Not sendable through the map: HOME's play knobs (VELOCITY, MOD WHEEL, CC1), GLOBAL's TEMPO knob (`UIKNOB_BPM`
  is refused; the INT TEMPO row works, up to 127 = 167 BPM, because an NRPN's data is 7 bits), PERF page rows (PERF
  MODE, links), ARP / SEQ / REC.

## Things found in the CC map code (proto/cc-map, uncommitted)

1. **NRPN LSB 16-20 act on the MFX on any page.** `cc_one` handles `item >= CCI_MFX` before it looks at the page,
   and `ui_sm_nrpn` only refuses `lsb > CCI_MFX_TYPE`. So NRPN 0:16 (OSC1 page) sets MFX knob 1, and NRPN 14:17
   (EFFECTS) sets layer 1's MFX knob 2 (checked in a host build). These aliases would freeze at V1 by accident. A
   one-line guard in `ui_sm_nrpn` fixes it: `lsb >= CCI_MFX && msb != UIPAGE_AMP` → 0.
2. **The MFX rows can't be reached** (see above). If they should be, they need items of their own, for example
   LSB 21+ = MFX row r, by the MFX_ROWS index (which the editor already has in `params.js`).
3. **RPN 0/1 MASTER TUNE scale:** the firmware maps data 0..127 to -50..+49 cents. GM's RPN 1 (fine tuning) spans
   about -100..+100 cents on the MSB (and adds an LSB). A sequencer that sends a standard fine-tune will land at
   half the intended offset. Fine if it is a choice; worth a line in the manual.
4. **ARCH by NRPN takes 7** (`DUAL SPREAD`, retired): the ARCH row's range is `VARCH_DUAL_OSC..VARCH_COUNT-1`,
   not `ARCH_ORDER`. `ui_sm_enforce` / loading may turn it into DUAL OSC; the editor never sends it. The NRPN
   could clamp through `ARCH_ORDER` the way the row's turn does.
5. **CUSTOM CC1 / CC2 shadow mapped CCs** (`cc_apply`: GLOBAL's choice wins). That is by design, but the editor
   cannot know it happened. The GLOBAL tab says so.
6. MOD routes by place: the panel re-packs and sorts routes (`routes_pack`) when its cursor leaves MOD, so a
   route the editor wrote at place 5 can move. Without a read-back the editor's MOD tab can drift from the synth.

## Open questions for the user

1. **What freezes at V1?** The NRPN numbering is "MSB = UI page number, LSB = item". The page numbers are the
   `UIPAGE_*` enum's order, so inserting or reordering a page renumbers every NRPN after it. Freeze the
   current numbers as a table of their own (independent of the enum), or freeze the enum order? The same goes for
   item = row index: a row added in the middle of a page shifts the rest.
2. **SysEx dump format.** The editor wants a patch dump request, a patch dump (both layers, or by layer), and
   ideally a perf dump. Raw `EngineState` bytes (small, fast, but break the editor on every struct change) or
   tagged by NRPN address (page, item, value: survives layout changes, and those numbers freeze anyway)?
   Manufacturer ID 0x7D (non-commercial) while it is a hobby firmware?
3. **CC OUT echo.** When the panel moves a knob, should the FM1 send the CC (or the NRPN) for it? If so, should it
   send on the layer's channel, and only for mapped CCs or NRPN for everything? The editor would then set the
   control without sending it back (the hook is `midi.js`; an input listener is all it needs). Should a Program
   Change or perf load trigger a full dump so the editor can resync?
4. Should PROGRAM show the slot names (needs a name request over SysEx), and are PCs 0-based on the screen?
5. Is `Foundations` the right name for WAVE entry 0, given that bank 0 plays the VCO (`osc_bank0_vco`)? The screen
   shows the bank's name, and the editor copies the screen.

## 2026-11-19: RECEIVE, CC OUT, fixed numbers

With the proto/cc-map firmware (fm1-ccout1 and later):

- **RECEIVE FROM SYNTH** works: it sends `F0 7D 46 4C 56 F7` and reads the FM1's dump (`57`: layer, then
  MSB LSB value-MSB value-LSB per parameter; `58`: the end and the count). Chrome asks for SysEx permission.
- **CC OUT**: with GLOBAL > CC OUT on, the panel's moves come back as CC (where the map has one) or NRPN, and every
  load sends the whole sound; the editor follows both (on its channel; Layer 2 when it has a channel of its own).
- **NRPN numbers** come from `src/nrpn_map.h` (`params.js` `nrpn_map`), fixed for V1, not from page / row order.
- **ARCH** goes by its place in the list (never the stored code); **INT TEMPO** runs to 255 BPM (a 14-bit NRPN,
  CC 38 after CC 6); the **MFX rows** are live (NRPN AMP 21 + row).
- Tested in headless Chrome with a stand-in MIDI port playing a dump captured from the FM1
  (session scratchpad `edtest/make_rx_harness.py`): ARCH, routes, a CC and a 14-bit NRPN after it land; every tab
  draws with no errors. Not yet tried by hand against the FM1 in a real browser.

## 2026-11-19: the SEQ tab

Edits the sequence the FM1 plays **now** (heard at once); **SAVE** writes it into one of the 32 sequence slots
under the name, as the panel's SAVE does; **LOAD** loads a slot as the Preset Encoder would. Opening the tab asks
the FM1 for the sequence and the slot names. The panel's own SEQ edits come back as they happen (step record, a
load, a performance), and the playhead lights the playing step.

- Four lanes of 16 steps, each with EVENT, TARGET (layer, lane, or the lanes it acts on), STEP (prescale, with
  musical names), MODE, and on NOTE lanes SCALE + ROOT. The loop: drag the rail's ends (or LOOP START / END on
  the selected step, the only way on a phone).
- NOTE lanes: drag a step up / down (by scale degree under a SCALE), the keyboard below the lanes, or the computer
  keyboard: `A W S E D F T G Y H U J K` = C..C into the selected step and on to the next (Shift: add to its chord,
  up to 4 notes), `Z X` octave. Value lanes: draw across the steps. REST / TIE / SKIP per step.
- Lane tools: shift left / right and reverse inside the loop, transpose (-12 -1 +1 +12), RND, COPY / PASTE,
  CLEAR. UNDO (Ctrl+Z) sends back what it restores. SAVE FILE / LOAD FILE: `.carnac-seq.json`.

Firmware: `src/hardware/synth_panel.c` "SEQ OUT", wire format `src/seq_wire.h` (not the flash layout). Commands, in
the editor frame `F0 7D 46 4C cmd .. F7`: 60 REQ, 61 LANE, 62 STEP, 63 NAME, 64 SAVE, 65 SLOTS, 66 LOAD, 67 POS
(out), 68 PLAY, 69 ACK (out). **X0X's USB keeps one received SysEx at a time** and drops one that arrives before
the main loop took it (measured: 5 sent back to back, 2 arrived), so every SEQ command is ACKed and the editor
sends one at a time (resent after 250 ms; 16 step writes take ~24 ms on the FM1). Tests:
`src/host/test_seq_sysex.c` (run_tests), `web/emu/emu_test.mjs`, and a headless-Chrome harness of this tab (not
in the repo: scratchpad `edseq/make_harness.py`, 22 checks).

## 2026-11-19: the whole sound at a glance

The tabs are gone. **SOUND** fits a laptop screen (1440 x 900): nine tablets on a 12-column grid in the order the
signal runs. Row 1: VOICE (the ARCH drawn as its signal path, lights running down the ley lines), OSC [1 2], MIXER
[MIX NOISE], FILTER [1 2 MIX]. Row 2: ENV [1 2 3] (the other two envelopes faint behind), LFO [1 2 3], AMP [DRIVE MFX].
Row 3: FX [DLY · REV, MIX] and the MOD map at a glance (click it to open MOD). A tab the ARCH lacks is unlit. Every
tablet shows a picture, four knobs and its lists and toggles; its other knobs are on sub-pages named at the tablet's
right (ENV: ADSR, VEL · KEY, RETRIG). Two columns under 1260 px, one on a phone.
**MOD** is the ley-line map: every SOURCE down the left, the DESTs in use down the right, each route a line between
them (bright and thick by its amount, broken when negative, its VIA a stone on the line with a thread back to the VIA
source); click a line to pick its route in the list below (the routes in use and the next free one; ALL 16 for every
route). **SEQUENCE** is drawn as the FM1's SEQ page: the transport, four step cards for the selected lane's knob window (rock
tablets with grass under them, the playing step's rimmed; a card turns like a knob), then four lanes of sixteen stones
(viz.js seqLane: each stone rises out of the ground by its step's value, its shape whole, inside the outline of its full height, on the turf of the loop; the
selected lane cream with its window bracketed; a rest hollow, a skip crumbled, a tie a fallen stone; drag the loop's
ends along the grass), each with its footer of stone chips (STEP, EVENT, TARGET, SCALE, ROOT, MODE) and tools. **SETUP** is tablets like SOUND's, every number and list a knob: TUNING
(an octave of stones from the ROOT), ZONES (the keyboard with the split stone), CLOCK (a sarsen circle lit a stone a
sixteenth at INT TEMPO), VELOCITY (the curve), MIDI (channels and custom CCs as knobs, CC / PC IN / OUT as a stone-tablet
menu), PROGRAM (a knob a bank picks the number; nothing is sent until its LOAD) and THEME (a menu of the nine palettes).

- The ENV picture outlines the other two envelopes over the selected one's earth (the first dashed, the second dotted).
- The MFX tab animates its type from its own knobs: phasers sweep notches along the plain, ENSEMBLE / CHORUS drift three
  voices of a wave, the flangers sweep a comb (BBD in steps), PITCH SHIFT climbs echoes of a shifted wave, BBD ECHO runs
  a light down a row of darkening stones, BRONZE AGE crushes a wave on its rails with sparks, WIND HARP shakes four
  strings between two stones as gusts pass, RINGING STONES sends rings out from the stones as they are struck.

- Knobs are moon dials (the panel's parameter popup: left horizon 0, overhead the middle, right horizon the top).
  Drag up or right; Shift for fine; arrow / Page / Home / End keys; double-click or Delete for the default; the wheel
  once the knob has been clicked. Lists are chips, two-way rows toggles, the routes' amounts small bars.
- Envelope times show as the firmware sets them (envelopes.c: 1 ms x 20000^((k/127)^0.75)), LFO rates in Hz
  (lfo.c: 0.05 x 800^(k/127)) or a division when synced. The pictures use the firmware's curves and LFO shape ring;
  the filter curve is a sketch of a 4-pole loop, not the exact circuit; the drive picture a sketch of each model.
- THEME (GLOBAL) recolours the page with the FM1's palettes, whether it is set here or received.
- Moving pictures redraw at 30 fps, only while on screen. On a touch screen the envelope picture lets the page scroll,
  so its corners move sideways there (SUSTAIN on its knob).
- A tablet's tab or sub-page rebuilds only that tablet (mount()); the controls it drops stop being drawn.
