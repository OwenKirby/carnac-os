# Carnac OS User Manual

## Introduction

Carnac OS turns the M-VAVE FM-1 into an eight-voice wavetable synthesizer with two layers, a four-lane sequencer, a chord arpeggiator and a stone-circle-themed screen. It takes its name from the megalithic alignments at Carnac.

This manual follows the instrument as it is today. Every change is logged in the Changelog at the end, so you can see what moved since you last read it.

- **Controls** are named as they appear on the FM-1 panel: ten section buttons, a Select Encoder, an Algorithm Encoder, a Preset Encoder, four knobs, OCT- and OCT+, PLAY and REC, and the keyboard.
- **Parameter names** are written in capitals, as they appear on screen (CUTOFF, SPREAD, PRESCALE).
- **Values** run 0 to 127 unless a section says otherwise. Bipolar values show as -64 to +63, with 0 in the middle.

## Getting started

Carnac starts on the HOME screen in SINGL mode: one layer, all eight voices, ready to play from the keyboard.

1. **Play.** Press keys. The note keys sound the focused layer.
2. **Load a sound.** Turn the Preset Encoder. A full-screen preset list opens and stays up for two seconds after the last click, and each click loads the next stored patch.
3. **Shape it.** Press FX for the oscillators, SEL for the filters, ENV for the envelopes, LFO for the LFOs. Press the same button again to step through that section's pages.
4. **Turn the knobs.** The four knobs set the four values shown at the bottom of the page.
5. **Browse a page's other settings.** Turn the Select Encoder. The menu moves through the page's rows; the Algorithm Encoder changes the highlighted value.
6. **Save it.** Press SAVE. The button flashes while a save is armed; press it again to confirm (see Presets and saving).

To play two sounds at once, change PERF MODE on HOME from SINGL to DUAL or SPLIT. Layer 2 is unavailable in SINGL.

## Panel and controls

Ten section buttons open the pages. Pressing a button again moves to its next page; hidden pages (for example FILTER2 in SINGLE) are skipped.

| Button | Pages | What it holds |
| --- | --- | --- |
| FX | OSC1, OSC2 (NOISE with one oscillator), OSCMIX | Oscillators and their mixer (named FX on the panel, it opens the oscillator pages) |
| SEL | FILTER1, FILTER2, FLTMIX | Filters and their mixer |
| ENV | ENV1, ENV2, ENV3 | Envelopes. ENV1 is hardwired to the amplifier |
| LFO | LFO1, LFO2, LFO3 | LFOs. LFO1 is global; LFO2 and LFO3 run per voice |
| EDIT | MOD MATRIX, AMP | The modulation routes; the amplifier and sends, with the layer's MFX at the end of the AMP page |
| GLO | EFFECTS, MIX | Delay and reverb; the live mix and the feedback paths between them. Each press flips between the two |
| HOME | LAYER (the focused layer), ZONES, PERF (SINGL: no ZONES) | Layer settings and play knobs, splits, the performance browser. GLOBAL and TUNING open with HOME held + the GLO or PIT key |
| ARP | CHORDS, STEPS (two views) | The chord arpeggiator |
| SAVE | SAVE | Saving (COPY and PASTE on SAVE + OCT) |
| SEQ | one page | The four-lane sequencer |

**Encoders and knobs**

- **Select Encoder** moves through the rows of the current page. The menu scrolls with it, four rows at a time; no popup opens.
- **Algorithm Encoder** changes the highlighted value. On a row that picks from a list (a wave, a filter FLAVOUR, a mode, ON or OFF) it opens the parameter popup with that row's options; a number changes in place, with no popup.
- **Preset Encoder** loads the next or previous stored preset. What it loads depends on the page: a layer patch on the sound pages, a performance on HOME, an arp pattern on ARP, a sequencer slot on SEQ.
- **Knobs 1 to 4** set the four values on the page. Hidden or unused knobs are drawn greyed.

**Shortcuts**

- **HOME + OCT-** or **HOME + OCT+** switches focus to layer 1 or layer 2. HOME acts when you release it, and not at all after a chord. After 200 ms of holding HOME, the focused layer's OCT LED lights and the other flashes. Layer 2 is refused in SINGL mode.
- **SAVE** + OCT- copies what SAVE would save on this page; SAVE + OCT+ pastes it (the save page opens and asks first). SAVE acts when you release it. Holding HOME or SAVE for 200 ms shows a strip at the bottom of the screen naming what OCT- and OCT+ do.
- **HOME + a black key.** The black keys carry legends, left to right OP1 to OP6, PIT, GLO, MONO, POLY and one bare key. Holding HOME, four of them have a job: **PIT** opens the TUNING page, **GLO** opens GLOBAL, **MONO** steps the focused layer's voicing through MONO RETRIG, MONO LEGATO and UNISON, and **POLY** steps it through POLY REUSE, POLY RESET and POLY UNISON (from the other family, each starts at its first mode). They follow the key, not the note, so OCT doesn't move them, and they don't sound. After 200 ms of holding HOME only these four keys light: dim means you can press it, lit means that page is open or that is the voicing's family. The HOME strip names the voicing. A large popup also shows the new voicing for a moment. The other black keys do nothing under HOME.
- **HOME + ARP** turns CHORD KEYS on or off: the keys play the ARP's chord bank from any sound page (see ARP below).

**Lights.** Only the button for the current page is lit. While the sequencer or arpeggiator plays, its button (SEQ or ARP) blinks on the beat, lit for the first half of each quarter note, even when its page is open. MIDI clock out (GLOBAL > CLOCK OUT) goes out on USB.

**Transport.** PLAY starts and stops the sequencer on the SEQ page and the arpeggiator on the ARP page. On any other page it starts both, or stops whichever is playing. The PLAY light shows what PLAY would stop. One started while the other plays waits for the other's next bar, so they stay in time. REC arms recording on the SEQ and ARP pages. On any other page REC opens the REC page, the loop recorder (see REC: the loop recorder); hold HOME and press REC to open it from anywhere. Once a loop is recorded, PLAY on any page other than SEQ, ARP and REC starts or stops it together with the sequencer and the arpeggiator.

## Voices and layers

Carnac has eight physical voices shared by up to two layers. Each layer plays its own patch, with its own architecture, filters, envelopes and effects sends.

**Performance mode** (HOME, PERF page)

- **SINGL**: one layer, using every voice the architecture allows.
- **DUAL**: each layer has its own MIDI channel. The FM1's keys play the layer you have selected, so you hear the layer you are editing (both layers when the two share a channel). External MIDI reaches each layer on its channel.
- **SPLIT**: layer 1 and layer 2 share the keyboard. The ZONES page (HOME) sets, on its four knobs, SPLIT NOTE and SPLIT FADE (by note) and VEL SPLIT and VEL FADE (by velocity).

In DUAL and SPLIT the voices are shared out: the two layers split them by what their voices cost, as evenly as the cost allows: two patches that each play 8 alone get 4 + 4, and a light patch gets what a heavy one leaves. MAX VOICES on the LAYER page shows the result. A layer set to MONO RETRIG or MONO LEGATO needs only one voice, so the other layer gets the rest of the budget: with two SINGLE patches that is 1 + 7 instead of 4 + 4.

**Architecture** (ARCH on the LAYER page, for the focused layer) decides the signal path of every voice, and the most voices it may use.

| Architecture | Signal path | Max voices |
| --- | --- | --- |
| SINGLE | OSC1 as up to four detuned playheads (SPREAD) into FILTER1; at SPREAD 0, one plain oscillator | 8 |
| DUAL OSC | OSC1 and OSC2 with cross-modulation into FILTER1 | 8 |
| DUAL FILT | OSC1 into FILTER1 then FILTER2, serial to parallel | 8 |
| FULL | DUAL OSC's oscillators into DUAL FILT's filters: OSC1 and OSC2 with cross-modulation into FILTER1 then FILTER2 | 6 |
| STACK | Two complete paths (OSC1 into FILTER1, OSC2 into FILTER2) blended | 6 |
| BINAURAL | OSC1 into FILTER1 once per ear, the right ear beat-offset | 6 |

Max voices is for a plain patch. Carnac prices every voice by what its patch costs the processor, and a layer gets as many as both of the FM-1's cores can carry. An SH-2 filter, a PCM wave or an MFX makes a voice dearer, so such a patch may get fewer: STACK with two SH-2 filters plays 6 voices, as does STACK with an MFX. MAX VOICES on the LAYER page shows what the patch gets. If the processor still nears its limit, Carnac first cuts the quietest note that is only ringing out after its key was let go. Only when every voice is held does it take a voice away, never below two a layer, and it gives it back once the load falls.

**What the price counts.** Besides the patch itself, the price counts each MOD MATRIX route (a little on every voice), the performance's effects and the transport. Each reverb type has its own cost (TOMB the least, CELESTIAL the most), and so do PING PONG and the DLY > REV and REV > DLY feeds, which cost more when LOOP TONE wears the loop. The SEQ, the ARP and the REC loop are always counted as running, so pressing PLAY never causes a dropout. A patch can therefore get fewer voices inside a heavy performance than on its own.

**VOICES strip.** When an edit changes how many voices a layer gets (an architecture, a filter, a route, an effect), a strip across the bottom third of the screen shows it for 1.5 seconds: VOICES (L1 VOICES or L2 VOICES with two layers), the new count in large runes beside WAS and the old count, and eight menhirs, solid for the voices the layer gets. With two layers, a line below gives the other layer's count. The strip covers only the picture, so the rows and knob dials above stay readable while you turn. Knobs and encoders leave it up; any button puts it away. On the SEQ, ARP and REC pages it fills the screen. Loading a patch or performance changes the voices without showing it.

The ARCH row steps through the architectures in the table's order. SINGLE is the one briefly called SPREAD: its SPREAD knob splits the oscillator into four detuned playheads, and at SPREAD 0 it is one plain oscillator. Patches saved with the older SINGLE load with SPREAD at 0 and sound as they did. DUAL SPRD has left the list: patches saved with it load as DUAL OSC.

**ANALOG** (ANALOG, the last row of the LAYER page) makes the layer behave more like an analog synth. It is stored with the patch. Each voice gets its own small fixed tolerances: pitch, cutoff, resonance, envelope speed and level. Each note scatters them a little. From 40 up, slow drift eases in as well. At 0 the layer is exact.

**Voice mode** (VOICING on the LAYER page): MONO RETRIG, MONO LEGATO, POLY REUSE, POLY RESET, UNISON or POLY UNISON. In UNISON, UNISON DETUNE spreads the outer voices by up to 50 cents. POLY UNISON plays chords like the Jupiter-6's unison: every note is a unison stack, and the layer's voices are shared between the notes held. With 8 voices, one note gets all 8, two notes 4 each, three notes 3, 3 and 2 (the extra voices go to the newest notes), four notes 2 each, and eight or more one each, the oldest giving its voice up. A new note thins the notes already sounding without restarting them, and their detune and stereo spread close up over the voices they keep. A note you let go rings out on its own voices; the others don't grow back. UNISON DETUNE works here too. Keys, SEQ, ARP and the REC loop all share the stack this way. Each layer has its own voice mode in SPLIT as well as DUAL, so a mono bass can sit under poly chords. Hold HOME and press the MONO or POLY key to change it from any page.

**Keys, SEQ and ARP on one layer.** When the keys, the sequencer and the arpeggiator share a layer and its voices run out, the keys come first, then the REC loop, then the SEQ, then the ARP. A new note never takes a voice from a higher rank: an ARP note that finds every voice held by the SEQ or the keys is skipped. When the ARP or SEQ must give up a note, a middle note of its chord goes first, so the lowest and highest notes keep sounding. The keys still give up their oldest note. Each source releases only its own notes, so the ARP and the SEQ can play the same pitch without cutting each other off. On a MONO layer, the highest-ranked note held sounds, and a lower one waits until it is let go. The SEQ, the ARP and the REC loop each let at most two of their released notes ring on a layer; a further note takes over the oldest.

**Glide.** PORTA (the glide mode) is CONST RATE, CONST TIME or LEGATO TIED, and PORTA TIME (knob 4 on the LAYER page; 0 is no glide) sets how long it takes. In polyphonic modes the glide starts from the last note played on that layer. A chord (notes within 30 ms of each other) fans out from the note played before it.

**Layer focus.** The sound pages edit the focused layer. Switch with HOME + OCT- (layer 1) or HOME + OCT+ (layer 2). The screen uses one colour scheme for both layers, set by THEME.

## Oscillators and filters

### Oscillators (FX button)

OSC1 and OSC2 play a wave from the WAVE list: first the 9 wavetable banks (FOUNDATIONS, then LEY LINE, MOONSTONE, STAR CHART, OVERTONE HALO, PASSAGE TOMB, AURORA, FROST SHATTER and DUST), then the 14 PCM waves. OSC2 appears only in architectures that use it (DUAL OSC, STACK and FULL).

**FOUNDATIONS** is bank 0, the classic analog shapes: a model of the SH-2's VCO-1, worked out live as it plays, with its slewed edges and bent ramp. WAVE SCAN steps through TRIANGLE, SAW, SQUARE, PULSE 35 %, PULSE 20 %, PULSE 10 %, PULSE MIN (4.5 %) and SAW+SQUARE. Between SQUARE and PULSE MIN the pulse width really moves, as on the original, so sweeping WAVE SCAN (by hand, with ENV3>SCAN or with a route) is true pulse-width modulation. The list is down to nine banks (OVERTONE HALO, PASSAGE TOMB and AURORA are new), so an older wavetable patch may land on a different bank: set its WAVE again.

- **Knobs:** WAVE SCAN (position within the wavetable), TUNE (in semitones, up to 36 up or down: three octaves either way), LFO2>PITCH (LFO2's vibrato on this oscillator) and ENV2>PITCH (ENV2 bending the pitch, up to 24 semitones up or down, finer near 0). LFO1's vibrato is on the LFO1 page.
- **Rows:** WAVE, ENV3>SCAN (how far ENV3 moves WAVE SCAN), KEY TRACK, and on OSC2 FINE (-50 to +50 cents, to detune it against OSC1) and OSC SYNC. KEY TRACK (how far the oscillator follows the keys) shows only with two oscillators (DUAL OSC, FULL, STACK), where one can drone or track partly against the other, as in two-oscillator FM.

**PCM waves.** A PCM wave is a recorded sound: an optional attack, then loop A and an optional loop B. WAVE SCAN crossfades from loop A to loop B, and the attack plays again on every new note. The SPREAD knob (SINGLE) works on PCM waves too, with four detuned playheads per loop. FM, PM and the other cross-modulations work on them; OSC SYNC does nothing. The struck waves (BELL, RAINSTICK, MENHIR, LOG DRUM, WOOD BLOCK) ring out by themselves; with ENV1's SUSTAIN above 0 they hold as a sustained tone instead.

| PCM wave | SCAN goes from … to … |
| --- | --- |
| CHOIR | a dark ensemble "ooh" to open, airy "aah"s |
| SPIRIT VOX | a dark angel choir over a low hum to a bright fantasy voice with a solo voice an octave up as a halo |
| PAN FLUTE | a hollow, pure pan pipe to a breathy, chiffy one |
| HORNS | soft to overblown and brassy |
| STR ENS | a warm string ensemble with a soft choir under it to the same players bowing harder |
| HOARFROST | a glassy pad with a slow shimmer to a rain of icy glints |
| SINGING STN | a stone pressed gently and ringing to the stone ground hard, gritty |
| LORE | hollow wooden chimes knocking to temple-bell chimes |
| BELL | a temple bell's ringing tremolo to a desert crystal's dense, bright partials |
| ROCK GONG | a ringing rock struck, its hum blooming into a dark drone, to the stone bowed, swelling until it whistles |
| RAINSTICK | a shaker burst and a sparse trickle to the full pour |
| MENHIR | a maul striking a standing stone, heard close, to the same stone heard from across the field, darker |
| LOG DRUM | a slit drum struck with a padded beater to a hard stick |
| WOOD BLOCK | a soft mallet to a hard stick, drier |

The PCM waves live in flash beside the firmware; a few PCM waves are built into the firmware itself. The installers write them after the firmware and rewrite only what changed. Without them, the PCM waves that need them play FOUNDATIONS instead. The FM-1 then says so when it starts: its start-up screen shows the field with no standing stones and the words WAVES MISSING, or WAVES OUT OF DATE when the waves in flash belong to another version. Run the installer again to bring them back.

**OSCMIX** mixes the oscillators, the sub oscillator and the noise. Its four knobs change with the architecture:

| Architecture | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| SINGLE | SPREAD | SUB WAVE | SUB LEVEL | NOISE LEVEL |
| DUAL OSC | OSC MIX | CROSS-MOD | SUB LEVEL | NOISE LEVEL |
| DUAL FILT | not used | SUB WAVE | SUB LEVEL | NOISE LEVEL |
| FULL | OSC MIX | CROSS-MOD | SUB LEVEL | NOISE LEVEL |
| STACK | XMOD TYPE | CROSS-MOD | SUB LEVEL | NOISE LEVEL |
| BINAURAL | OSC PHASE | BEAT HZ | SUB LEVEL | SUB WAVE |

With two oscillators (DUAL OSC, FULL, STACK) SUB WAVE is a row; in BINAURAL, NOISE LEVEL is a row. With two oscillators the noise's settings are rows here too (NOISE COLOUR, COL MOD, HARMONIC, LEVEL MOD), with XMOD TYPE in DUAL OSC and FULL; BINAURAL adds R SCAN OFFSET. To move SPREAD, use a MOD MATRIX route to SPREAD.

Cross-modulation types are FM, AM, RING and PM. SUB WAVE sets the sub oscillator's wave and octave in one setting: SINE, SQR (square) or CURR (CURRENT, a copy of OSC1's live wavetable frame), one (-1) or two (-2) octaves down. COLOUR runs from white noise (0) through pink (16) and brown (32), then narrows into a resonance tuned to the note, from a breathy whistle to a pure, airy one at 127. HARMONIC puts that resonance on one of OSC1's overtones, 1 to 8, with a stop at each. NOISE LEVEL sits on the OSCMIX page in every architecture. With one oscillator, the NOISE page (in OSC2's place) shapes the noise: its knobs are COLOUR, HARMONIC, COL MOD and LEVEL MOD (how far ENV3 moves the colour and the level). Like the classic polysynths, Carnac has one noise source that every voice shares (BINAURAL's right ear has its own), so noise in a chord sounds like one breath. The resonant colours above brown are each voice's own, tuned to its note. Each voice still sets its own noise level and colour, and pink or brown noise now costs far less CPU. In BINAURAL, BEAT HZ offsets the right ear's pitch by up to 15.9 Hz, in 1/8 Hz steps.

### Filters (SEL button)

FILTER1 and FILTER2 share the same controls. FILTER2 appears in DUAL FILT, FULL and STACK.

- **Knobs:** TYPE MORPH (labelled by what it does in the circuit: LP-BP-HP, MODE or SPREAD), CUTOFF, RESONANCE, ENV2>CUTOFF (0 to 127: how far ENV2 opens the cutoff; for a downward sweep turn on ENV2's INVERT, or use a MOD MATRIX route with a negative AMOUNT).
- **Smoothing:** CUTOFF, RESONANCE and TYPE MORPH (and OSC SCAN) glide to a new knob value over about 25 ms, so a slow turn sweeps smoothly. Loading a patch or performance jumps straight to its values.
- **DRIVE** (the first row) sets how hard the signal goes into the circuit, from clean at 0 to driven hard at 127. The level stays roughly even as it turns, so it adds grit rather than volume.
- **FLAVOUR** (the second row) picks the circuit, and TYPE MORPH means something different in each.

| Flavour | Character | TYPE MORPH |
| --- | --- | --- |
| SH-2 | Roland-style four-pole OTA filter: clean at DRIVE 0, a resonance that rings up softly and self-oscillates in tune up to the top of the cutoff | Low-pass through band-pass to high-pass |
| MULTI | Four-pole ladder multimode | LP24, LP18, LP12, LP6, BP, HP12, HP24, notch |
| MS-10 | Korg MS-10 style two-pole Sallen-Key filter (the Korg35). Its diodes sit in the signal path, so a loud signal takes the resonance away and the whole sound distorts, while quiet passages ring; the ringing leans to one side. RES rises evenly and self-oscillates from about 108 at every cutoff, in tune up to about 2.7 kHz. DRIVE pushes it into its diodes. Patches saved with the old MS-20 load on the MS-10 and ring less | Low-pass through band-pass to high-pass (6 dB per octave, with a resonant bump) |
| FORMANT | Vowel-like band filter | Band spread |

**KEY TRACK** (the third row, after DRIVE and FLAVOUR) moves the cutoff with the note played, pivoting at C4. At 0 the cutoff stays put; at 127 it follows the keyboard one octave per octave.

The filter mixer (FLTMIX) appears in DUAL FILT and FULL (BALANCE, ROUTING from serial to parallel, BALANCE MOD, ROUTING MOD), STACK (BLEND A/B, CROSS-FEED, BLEND MOD, ENV3>CUTOFF) and BINAURAL (FLT PHASE, WIDTH, CUTOFF L/R, CUT L/R MOD). The MOD knobs are ENV3's amounts, except CUT L/R MOD, which is LFO1's. CROSS-FEED sends one filter's output into the other as well: turned right, FILTER1 also feeds FILTER2; turned left, FILTER2 feeds FILTER1; in the middle it is off.

## Modulation

Modulation comes two ways. The common moves are fixed on their own pages, such as ENV2 > CUTOFF on the filter's knob 4, LFO1's TO PITCH and TO FILTER, and LFO2 and ENV2 to pitch on the OSC pages. Everything else goes through the MOD MATRIX (below). Sources are: none, velocity, keyboard, random, mod wheel, aftertouch, poly aftertouch, CC1, CC2, ENV1 to ENV3 and LFO1 to LFO3. The two CC numbers are set on the GLOBAL page; the CC1 knob on the LAYER page sets CC1 directly, with no number needed.

**Envelopes (ENV button).** Knobs are ATTACK, DECAY, SUSTAIN and RELEASE. Extra rows: VEL > LEVEL (0 to 127: how much quieter a soft note makes the envelope, wherever it goes; on ENV1 it is the velocity-to-volume control, 111 by default, the old fixed curve of about -18 dB at the softest note), VEL > ATTACK and KEY > TIME (bipolar), RETRIG RATE, RETRIG SYNC, LEGATO, AUTO DECAY (a rapid repeat of a note shortens the release) and, on ENV2 and ENV3 only, INVERT. ENV1 is hardwired to the amplifier. An inverted envelope is drawn upside down in the display, as its destinations receive it: at rest it adds nothing, so the destination's knob sets where it sits, and with the note it pushes below that point, by as much as the envelope rises. ENV2 has fixed paths to the cutoff (FILTER knob 4) and the pitch (OSC knob 4); ENV3 to WAVE SCAN, the noise and the filter mixer; anything else is a MOD MATRIX route.

**LFOs (LFO button).** LFO1 is laid out like the Jupiter-6's: its knobs are RISE (how it comes in after DELAY: positive fades in, negative fades out), RATE, TO PITCH (vibrato) and TO FILTER (on every filter the architecture plays). Its rows are WHEEL>DEPTH, OSC1 AMT and OSC2 AMT (how much of TO PITCH each oscillator gets; OSC2 AMT only with two oscillators), SHAPE (sine through slewed random), DELAY (above 100 the LFO runs in release mode), CLOCK SYNC, PHASE and KEY RESET. LFO1 is shared by all voices, and its DELAY and RISE start again only on a new phrase: the first note after every key was up. LFO2 and LFO3 run separately in each voice. Their knobs are SHAPE, RATE, DELAY and FADE (negative fades out, positive fades in), their rows WHEEL>DEPTH, CLOCK SYNC, PHASE and KEY RESET. LFO2 reaches the pitch through each OSC page's LFO2>PITCH knob, and LFO3 the level through TREMOLO on the AMP page; any other destination, and any LFO's speed, is a MOD MATRIX route. WHEEL>DEPTH runs 0 to 127: at 0 the LFO runs at full depth; at 127 the mod wheel alone sets the depth, so the wheel brings vibrato in from nothing.

**MOD MATRIX (EDIT button).** Each layer has 16 routes. A route takes a SOURCE, an optional VIA that scales it, and a DEST, with an AMOUNT from -64 to +63. EDIT opens the MOD MATRIX; press it again for AMP.

- **Controls:** the knobs are SOURCE, VIA, DEST and VIA DEPTH for the route under the cursor; the Algorithm Encoder sets its AMOUNT.
- **VIA:** at VIA DEPTH 0 the VIA is ignored; at 127 the route is exactly SOURCE times VIA. LFO2 via MOD WHEEL, for example, brings vibrato in as the wheel goes up. VIA NONE passes the source alone.
- **The rows:** routes are sorted by source. Each source's group starts with a small stone and the source's name; each row shows the destination's full name and, after a knot, the VIA. + ROUTE at the end adds a route, starting from the source of the group the cursor was last in.
- **Removing a route:** set its SOURCE to NONE; the route goes when the cursor leaves it.
- **Destinations:** OSC1 and OSC2 SCAN and pitch, PITCH (both oscillators), OSC MIX, CROSS-MOD, SPREAD, BEAT HZ, NOISE level and colour, HARMONIC, F1 and F2 CUTOFF, RESO, MORPH and DRIVE, BALANCE, ROUTING, PHASE, CUT L/R, LFO1 to LFO3 speed and depth, AMP LEVEL, PAN, GLIDE, DLY SEND, REV SEND and MFX 1 to 4. DEST steps only through the ones the layer's architecture has.
- **The map:** under the rows, the sources in use stand on the left and their destinations on the right, a line for each route. The cursor's route burns bright, other routes from its source show, the rest are dotted. A VIA ties a knot whose size follows VIA DEPTH. Sparks run along the lines: at an LFO's rate, with an envelope's level, or as a burst on each new note.
- **From other pages:** a route also appears as a row at the bottom of its destination's page, where the Algorithm Encoder sets its amount. EDIT on that row, or just after turning one of the page's knobs, jumps to that route in the MOD MATRIX, or to + ROUTE already aimed at that destination.
- **With no note playing:** velocity, keyboard, aftertouch, the envelopes, LFO2 and LFO3 belong to a voice and read 0 when nothing sounds. LFO1 keeps moving without one.

**MOD MATRIX popups.** On the MOD MATRIX page, turning a route's knob brings up a full-screen popup, so you can see what you are picking. It stays up for 1.5 seconds after the last turn; the Select Encoder or any button puts it away.

- **SOURCE, VIA, DEST** (knobs 1 to 3): the knob's whole list, seven entries at a time, with the current one on the lit slab. Sources are spelled out (MOD WHEEL, AFTERTOUCH, LFO 2). DEST lists only the destinations the layer's architecture has. The line at the foot shows the whole route in short, such as LFO2 \* WHL > CUT1 +45.
- **VIA DEPTH** (knob 4) and **AMOUNT** (Algorithm Encoder): the route as a card, with SOURCE, VIA, DEST, VIA DEPTH and AMOUNT one per row. The one you are turning sits on the slab, with a bar under the card; for AMOUNT the bar grows out from the middle.

**Pitch bend and mod wheel.** The LAYER page's knobs are VELOCITY (the keys' velocity), MOD WHEEL, CC1 and PORTA TIME. Pitch bend comes in over MIDI only. BEND RANGE is on the TUNING page.

## Effects

**Amplifier (AMP page, EDIT button).** Each layer's DRIVE (knob 1) comes after its amp envelope, so velocity and ENV1 play into it: a soft note drives less than a hard one. At 0 it is off. Turning it up adds grit while the level stays roughly even (the old drive got up to 7 dB louder). DRIVE MODEL picks its character:

| Model | Character | Gain at 127 |
| --- | --- | --- |
| SOFT | Smooth saturation, rounding into full scale | +30 dB |
| HARD | Clean up to about 3/4 of full scale, then a short bend into clipping | +30 dB |
| FOLD | The peaks fold back on themselves; the fold points are rounded | +16 dB |
| TAPE | Soft saturation with a slight bias (even harmonics); the treble falls away as it drives harder | +24 dB |
| TUBE | Uneven, so mainly the 2nd harmonic at low and middle settings; its bias moves with the level, so loud notes bloom. Pushed hard it turns square | +30 dB |
| OVERDRIVE | Like a Tube Screamer: only the part above about 720 Hz is clipped, the lows stay clean, and the tone rolls off at about 3.4 kHz | +40 dB |

The drive works on each voice, so a chord's notes do not distort each other.

**MFX (the end of the AMP page).** Each layer has one modulation effect after its amplifier and before its sends, saved with the patch: OFF, STONE PHASER, ENSEMBLE, ENS+PHASER, CHORUS, TAPE FLANGER, BBD FLANGER, PITCH SHIFT, BBD ECHO, BRONZE AGE, WIND HARP or RINGING STONES. Scroll the Select Encoder down onto the MFX row and the page becomes the MFX's: the four knobs are that type's own controls, the Algorithm Encoder picks the type, the header reads L1 MFX and the type, and the type's wind picture replaces the voices. The type's extra rows (MODE, POLARITY, WIDTH, AGE, ROOT, CHORD and so on) and any mod routes into it show under the MFX row and keep the MFX's knobs. Scroll back up to DRV MODEL and the knobs are DRIVE, TREMOLO, DELAY SEND and REVERB SEND again. Pressing EDIT to come back to AMP always gives AMP's own knobs. At OFF the knobs are greyed; a new type starts at its own defaults.

There is one reverb and one delay, shared by both layers. Each layer feeds them through its own sends on the AMP page (DELAY SEND and REVERB SEND). The effects settings are on the EFFECTS and MIX pages and belong to the performance. GLO always opens EFFECTS, and each press flips between EFFECTS and MIX, so you can shape the feedback paths with one press either way.

**EFFECTS knobs:** DELAY TIME, DELAY FDBK, REVERB TIME, REVERB DAMP. Unsynced, DELAY TIME runs from about 1.5 ms to 1.0 s in steps of about 8 ms. Turning it while echoes ring bends them, as on a tape echo: the play head glides to the new time with its speed held within an octave of the tape's (half speed while the delay grows, double while it shrinks), so a 0.23 to 0.52 s turn settles in about 0.6 s.

**EFFECTS rows:** DELAY TYPE, DELAY SYNC, DEGRADE, PING PONG, then REV TYPE: the delay's rows first, then the reverb's.

**MIX page (GLO, second press).** Knobs and rows: BALANCE (layer 1 against layer 2 in DUAL and SPLIT; DRY/WET in SINGL), LOOP TONE (below the middle it darkens the delay's repeats, above it thins them like the old DELAY HP; either side of the middle it also wears the loop, see the delay below), DLY > REV (the echoes fed into the reverb) and REV > DLY (the reverb fed back into the delay). Both feeds up together let a reverb bloom and hold; take either back to 0 and it dies away. The picture draws ley lines from each layer to the delay and reverb stones, the loop between them glowing as it fills.

| Reverb type | Character |
| --- | --- |
| SPRING | Two dispersive springs, the classic drip |
| TOMB | Comb-network room (formerly ROOM) |
| MOD | Lush and chorused, inspired by Lexicon |
| CELESTIAL | Reverb with an octave-up pitch shift in the feedback, for floating ambient sounds |
| DOLMEN | A small stone chamber, like a cave |
| TUMULUS | A cavern, named for the Tumulus Saint-Michel at Carnac |

**One delay: a half-rate tape echo.** Its tape runs at 22 kHz and keeps 16 bits a moment, so echoes reach about 9 kHz, warm like the affordable tape and digital delays of the 80s, and every repeat comes back a little darker, so high FEEDBACK tails turn dusty rather than harsh. Filters on the way onto and off the tape keep bright notes from folding back as grit, and the tape never slows down, so nothing aliases at any DELAY TIME. It holds 1 s of echo. Once nothing goes in and the echoes have died away, the delay rests and costs the processor nothing until you play again. DELAY TYPE and the BBD are gone; the MFX covers those colours (installed).

- **DEGRADE** is one control for wow, flutter and filtering. It is the tape's age: more of it gives deeper wow and flutter and duller repeats, though even at 127 they keep their top to about 3 kHz. Past 64 the tape also breaks up: dropouts written onto the tape, which compound with each pass, and the odd snag of the head. At 64 and below it never breaks up. The tape keeps 16 bits a sample and uses emphasis, so its hiss sits about 50 dB under the echo, even under a dark bass.
- **PING PONG** bounces the echoes between left and right.
- **LOOP TONE** right of its middle (MIX page; it was the DELAY HP row) puts a gentle high-pass filter in the delay's feedback loop, for a dub-style delay. The first echo keeps its bass, and each repeat after it loses a little more, so high FEEDBACK settings thin out instead of piling up low end. At the middle it is off; from there to fully right the cutoff sweeps from about 30 Hz to about 1 kHz, so the far right strips the repeats down to the mids. Either side of its middle LOOP TONE also wears the delay's tape and the DLY > REV / REV > DLY loop, more the further you go: to the left like old tape (the echoes saturate lopsidedly), to the right like a mixing desk pushed hard. Either way the echoes swell a little, breathe slowly and drift in length, so a held loop keeps moving. At the middle the loop is clean. The delay still self-oscillates past FEEDBACK 100.
- **DELAY SYNC** locks the delay time to the tempo: DELAY TIME then steps from 1/16T (fully left) through 1/16, 1/8T, 1/8, 1/4T, 1/4 and 1/2T to 1/2 (fully right), short to long as when unsynced, and follows the tempo as it changes. The knob shows the division; unsynced, it shows the time in ms.
- **Self-oscillation.** With FEEDBACK above 100, the delay builds up on itself, in a musical way. At the top of the range they run away to a dark, filtered howl, so turn it up slowly.

> Very loud sends are limited before they reach the reverb and delay, so playing several voices into a hot reverb should not buzz. If you hear digital buzz or a high whine after a note ends, report it with the reverb type and the patch.

## Sequencer and arpeggiator

### SEQ: the four-lane sequencer

SEQ has four lanes of 16 steps. Each lane has its own event type, speed, direction and loop, so lanes of different lengths drift against each other. Press SEQ to open lane 1 with steps 1 to 4 on knobs 1 to 4; press SEQ again for the next lane (1, 2, 3, 4, then back to 1), keeping the same steps on the knobs. The keyboard's bottom 16 white keys are the lane's steps: the playhead lights brightest, the four steps on the knobs medium, and every other step that plays (inside the loop, not muted or skipped) dimly. Press a white key to put that step and the next three on the knobs (from step 13 on, steps 13 to 16). While the sequencer plays, the knob card of the step under the playhead is outlined, as on the ARP page.

**Lane rows** (Select Encoder, Algorithm Encoder):

| Row | What it sets |
| --- | --- |
| STEPS | Which four steps knobs 1 to 4 edit. The Select Encoder moves through steps 1–4, 5–8, 9–12 and 13–16, then on through the rows below in the footer's order, then back to steps 1–4. A window a step key left between groups moves to the next group (or back to the one it starts in) |
| PRESCALE | The footer's g value: MIDI clock ticks per step, 1 to 32. 6 is a sixteenth note, 12 an eighth, 24 a quarter |
| EVENT TYPE | Note, dynamics, transpose, timing, gate, ratchet, probability skip, probability mute, mod wheel, aftertouch, poly aftertouch (on the notes the SEQ plays), custom CC 1 and 2, program change (DUAL mode only) or layer |
| TARGET | What the lane acts on: layer 1, layer 2 or both for note-style events; one or more lanes for modifier events |
| SCALE and ROOT | NOTE lanes only, see below |
| PLAY MODE | FOr (forward), bAC (backward), PE1 and PE2 (pendulum), rnd (random) |

**Loops.** Hold OCT- and press one of the 16 white keys to set the lane's loop start. Hold OCT+ for the loop end.

**Step record.** Press REC on SEQ, then play keys. Each note is written into every NOTE lane at the record step. Keys struck while others are still held build a chord of up to four notes. With PLAY and REC both on, the record step re-fires at each lane's PRESCALE rate on the clock, so it keeps sounding rather than playing once.

**Scale mode.** On a NOTE lane, SCALE holds the lane to a scale: OFF, GENRE (follows the arpeggiator's genre bank), Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic minor, Major pentatonic, Minor pentatonic or Blues. ROOT sets the tonic. Turning a step's note knob moves through the scale's degrees; played notes are snapped to the scale as you enter them. Stored notes are never changed, so turning SCALE off gives your original notes back.

### ARP: the chord arpeggiator

ARP plays chord patterns of up to 16 steps. The page has two views: **CHORDS** makes the whole keyboard chord keys for playing, and **STEPS** makes the 16 lowest white keys the pattern's steps for editing. Press ARP on the ARP page to switch views; the page remembers the last one.

- **CHORDS: play.** Every key is a chord key. It sounds the chord for its note from the GENRE BANK, in that key's octave, arpeggiated by the four knobs while you hold it. While the pattern plays, a key also transposes it by its distance from C4 (C4 plays it as stored). OCT- and OCT+ shift the chord keyboard. Rows: GENRE BANK, LATCH, LAYER and TRANSPOSE MAQ.
- **LATCH** keeps a live arpeggio playing after you let go. Turn the LATCH row on, or hold ARP and press a key.
- **CHORD KEYS** (hold HOME and press ARP; the same again turns it off) lets you play the chord bank while you shape a sound. Every key on a sound page then works as on CHORDS: the GENRE BANK, the live ARP knobs, LATCH, and the transposition while a pattern plays. It does nothing on the ARP, SEQ and REC pages, is never recorded, and is off at power-on (it is not saved). The ARP button stays lit while it is on, and turning it off stops the chord. In DUAL it plays the ARP's LAYER.
- **The CHORDS screen** shows the chord sounding. Its name is in large letters, for example G#MAJ7, or DMIN7B5/G# when the lowest note is not the root (minor is written MIN). On the right, KEY or STEP 3 says what is playing it. Below, a keyboard of four octaves, from the chord's lowest C, lights the notes being struck now; the chord's other notes over its octave range are half-lit. At the bottom are the last four chords played, the newest on the right and lit. Playing the same chord again adds no new one.
- **STEPS: edit.** The 16 lowest white keys are steps 1 to 16; the black keys and the keys above them do nothing. Press a step to select it: its RATE, SPAN, STYLE and VARIATION go on the knobs, and its CHORD, ACCENT and CLEAR on the rows. Stopped, the step's chord sounds while you hold it. Hold a step and press a later one to tie every step after the held one up to it (TIE carries the chord on); SEQ LENGTH grows to fit. The screen shows eight step cards, the half with the selected step.
- **STEPS rows:** CHORD (REST, TIE or a chord root), ACCENT (the step's first strike at ACCENT VEL), CLEAR (makes the step a rest), SEQ LENGTH (1 to 16), TIMING (every step's length, 2 bars down to 1/32T), GATE (how long each strike sounds, for the whole pattern) and ACCENT VEL.
- **SEQ LENGTH from the keys:** on STEPS, hold OCT+ and press a step key to make it the last step (the selection stays). While OCT+ is held, the step keys light 1 to SEQ LENGTH dim and the last one bright. The ARP menu shows a scroll bar when it has more rows than fit.
- **Knobs:** RATE (the length of one phrase event, 1/2 down to 1/128, with triplets; at 1/16 a 16-event phrase lasts a bar), SPAN (how wide the chord is voiced: AUTO uses the phrase's own range, 1 to 4 OCT spread the same phrase over more octaves; while a chord key is held it changes at once), STYLE (the family of phrases) and VARIATION (one of that style's 12 named phrases; its list changes with STYLE). A phrase loops over the bar and keeps going across steps: the steps change only the chord under it. A new chord arriving while a note is still held is struck at once. On CHORDS the knobs are the live values a chord key plays; on STEPS they are the selected step's.
- **LAYER** (a CHORDS row): L1, L2 or BOTH in DUAL mode. New patterns start on L2, the SEQ's note lane on L1. In a mode without the chosen layer the ARP plays L1 and keeps the setting.
- **Record:** arm REC. Playing, a chord key on CHORDS records into the nearest step, and turning a knob writes into the playing step. Stopped, chords fill the steps in turn, starting from the step selected on STEPS.
- **TRANSPOSE MAQ** makes the sequencer's NOTE lanes follow each step's chord root and the chord key you press, so the arp and the sequencer stay in key together.

Each phrase strikes parts of the chord: the bass note (the lowest an octave down), single voices, the whole chord, guitar down- and up-strokes, or a muted chk. Phrases carry their own accents and short notes. BEAT's first three variations are HOLD: the chord struck once and held while the following steps play the same chord (HOLD UP and HOLD DN strum it, the notes RATE apart). HARP and a few piano and guitar phrases let their notes ring, up to 4 at once, the oldest let go first and all of them at a new chord.

| STYLE | VARIATION (1 to 12) |
| --- | --- |
| BEAT | HOLD, HOLD UP, HOLD DN, WHOLE, HALF, QUARTER, EIGHTHS, 16THS, CHARLSTN, TRESILLO, DOTTED, PUSH |
| ARP | UP, DOWN, UP-DOWN, RANDOM, UP 2 OCT, DN 2 OCT, U-D 2OCT, CONVERGE, DIVERGE, PEDAL, BROKEN, WEAVE |
| PULSE | OCTAVES, ROOT 5TH, BERLIN 1, BERLIN 2, GALLOP, DRIVE, ACID, OFFBASS, ROLLING, CASCADE, MINIMAL, 3 OVER 4 |
| STAB | OFFBEAT, SKANK, UPBEAT, HOUSE, FUNK, GATE, CLAVE, BACKBEAT, SYNCOPE, DEMBOW, STUTTER, DUB TECH |
| STRUM | BAR, HALVES, FOUR, DOWN-UP, FOLK, ISLAND, WALTZ, BALLAD, TRAVIS, FLAMENCO, SHUFFLE, ROCK |
| GUITAR | FUNK 16, CHANK, REGGAE, SKA, PALM, DISCO, HIGHLIFE, COUNTRY, GALLOP, BOSSA, STABS, TREMOLO |
| KEYS | ALBERTI, BALLAD, STRIDE, GOSPEL, BOOGIE, MONTUNO, RIPPLE, SATIE, NOCTURNE, TRILL, OCT TOP, GLASS |
| BASS | OOM-PAH, BOOMCHIK, WALTZ, BOSSA, HABANERA, TUMBAO, WALKING, ONE DROP, DISCO, POLKA, STEPPERS, BOUNCE |
| HARP | CASCADE, FALLING, ROLLING, GLISS, KORA, BELLS, DRONE, ROLLED, WAVES, RAIN, STARS, LYRE |

The GENRE BANK holds 101 chord sets: the Roland J-6's 100 in its order, from Pop, Jazz, Blues, Trad and the Stack and Utility sets through Cinematic, Synthwave, House, Techno, EDM, Gospel and R&B, Lo-fi, Funk, Neo Soul and Bossa Nova to Classical and Modern, each with the J-6's voicings, then Phrygian, the default.

### REC: the loop recorder

REC on any page except SEQ and ARP opens the REC page, a looper for what you play on the keys, the panel's or an external keyboard's. It records the notes with their velocity and length, the mod wheel and the pitch bend, up to 16 bars and 256 notes. Hold HOME and press REC to open it from anywhere. The loop is a scratch pad: it is not saved.

- **Recording.** The page opens armed. The first note starts the loop, on the next bar when the sequencer or arpeggiator is running. Press REC again to close the loop: its length snaps to the nearest beat, and overdub carries on. From then on REC flips between PLAY and OVERDUB, and each overdub is a pass of its own.
- **Undo.** Hold REC for 0.6 s to take back the newest pass. It works on the REC page and on every other page except SEQ and ARP.
- **Transport.** On the REC page PLAY starts and stops the loop alone. On any other page except SEQ and ARP, PLAY starts or stops the loop together with the sequencer and the arpeggiator.
- **Knobs:** VELOCITY and MOD WHEEL (the same as on the LAYER page), GATE (64 plays the notes as played, lower shortens them down to staccato, 127 makes them twice as long) and DENSITY (127 plays every note; lower thins the loop, the weakest notes first).
- **Rows** (the footer: the Select Encoder walks it, the Algorithm Encoder sets it): TRANSPOSE (-24 to +24 semitones), QUANTIZE (OFF, 1/8, 1/16 or 1/16T: new notes start on the grid), DOUBLE (the loop twice as long, copied) and CLEAR (everything). DOUBLE and CLEAR ask first: press SAVE to go ahead, or touch any other control to leave it.
- **The screen** is a field of standing stones. Time runs left to right, with the bars as way-markers on the horizon; low notes stand near and large, high ones far and hazy, and a stone's height is its velocity. Layer 1's stones are in the theme colour, layer 2's cream; older passes are darker, sounding stones lit, and notes DENSITY leaves out only outlined. Under the field, ridge lines show the mod wheel and the bend; the playhead is a line of light.

## Presets and saving

Carnac stores four kinds of thing, 128 patches and 32 of each of the others:

| Kind | What it holds |
| --- | --- |
| PATCH | One layer's sound |
| SEQUENCE | The four sequencer lanes |
| ARP | An arpeggiator pattern |
| PERFORMANCE | A complete performance: the mode, global settings, both patches, and the sequencer and arp patterns it links to |

**Loading.** Turn the Preset Encoder. It loads the kind that fits the page you are on: a patch on the sound pages, a performance on HOME, an arp pattern on ARP, a sequencer slot on SEQ. The preset list covers the whole screen and closes two seconds after the last click.

**Links.** HOME's PERF page, the performance browser, shows which patch, sequencer and arp slots the current performance uses: L1 PATCH, L2 PATCH, SEQ LINK and ARP LINK (its knobs), with PERF PRESET and PERF MODE as rows. Its dolmen draws the performance as a capstone on four pillars, one per part, cut from the same stone as the knob tablets above it; a pillar is thicker the more its part holds, an outline while it is still factory-fresh, and the capstone thickens with the whole. Turning a link, by its knob or its row, loads the next stored patch, sequence or arp pattern straight away, skipping empty slots; PERF PRESET loads the next performance the same way. While a slot row turns, the popup shows the slot's number with its name under it.

**Saving.**

1. Press SAVE. The save page opens on what the page you were on saves (HOME and the sound pages: the performance, with your sound tweaks inside it; SEQ: the sequence; ARP: the arp pattern) at the slot it came from. To save a sound as a patch of its own, turn TARGET to PATCH: the slot jumps to the focused layer's patch. The header says what will be saved, e.g. SAVE L2 PATCH.
2. Pick the slot with the Algorithm Encoder or knob 2. The bottom of the screen shows the whole bank: a mark for each used slot, your slot as a standing stone, and the moon above it, full over an empty slot and a dark crescent when you would overwrite. The line under the name says EMPTY or OVERWRITES and the name there.
3. To rename, turn knob 3 (CHAR POS, which letter) or knob 4 (CHARACTER). A full-screen keyboard opens: what you are naming at the top, the name in large letters with the cursor's letter lit, and below it A to Z, 0 to 9 and . - \_ / in rows of eight, with SPACE as a long stone underneath; the letter knob 4 is on is lit in its channel. It closes 4 seconds after the last turn, or as soon as you touch another control; pressing SAVE closes it and saves. Patches, performances, sequences and arp patterns all take a name (sequence and arp names since 2026-10-25). A loaded sequence or arp keeps its slot's name, a performance brings the names of the sequence and arp slots it links, and the factory ones are named after their performance. Names are up to 16 characters from letters, digits, space and . - \_ /.
4. Press SAVE. An empty slot is written at once; a used one asks OVERWRITE? first (NO or YES with the Algorithm Encoder, then SAVE).
5. The screen reports SAVED NAME TO SLOT N, and the next control takes you back to the page you came from.

**Copy and paste.** Hold SAVE and press OCT- to copy what SAVE would save on the current page (the focused layer's patch, the performance, the sequence or the arp pattern). Hold SAVE and press OCT+ to paste it over the same kind, for example layer 1's patch onto layer 2: the save page opens and asks PASTE? first. While SAVE is held, a strip names both, with PASTE dimmed when the clipboard holds another kind.

**Factory bank: THE OLD KNOWLEDGE.** The stones remember what their builders knew. Performance slots 0 to 19 hold 20 factory performances, each with its patches, effects, MIX settings, tempo, tuning, sequence and arp pattern in the same slots. Patch slots 0 to 34 hold 35 factory patches: pads, warm vintage sounds, Berlin-school sequencer voices, techno and dub, 90s digital sounds, ritual sounds and two PCM drum kits. Power-on plays performance 0, LUNAR STANDSTILL: the keys play a polyphonic pad, and PLAY adds the moon's bells arpeggiating over it. A slot you have saved over shows your version in the browser. Every patch gives the MOD WHEEL (knob 2 on the LAYER page) a job, listed in the last column. On the two kits the low keys play one hit and the high keys the other; the hits are tuned, so they ring at the pitch you play (installed, not yet released).

| Perf | Name | Style | Sounds | What PLAY does |
| --- | --- | --- | --- | --- |
| 0 | LUNAR STANDSTILL | the hero pad | LUNAR STANDSTILL on the keys, MOON GLASS (DUAL) | the arp runs the bells up and down two octaves of Dm9, Bbm9, Gm9 and Am9, two bars each; the sequence holds the pad's root and rides both layers' MOD WHEEL |
| 1 | HEARTH CIRCLE | warm vintage | HEARTH CIRCLE | strums open quartal chords a bar each, the root below |
| 2 | OGHAM | warm vintage | DOLMEN BASS under C4, OGHAM LEAD from C4 (SPLIT) | an A Dorian bassline below the split, a lane sweeping its cutoff |
| 3 | CALANAIS | Berlin school | ORRERY PULSE under C4, SKY DISC CHOIR from C4 (SPLIT) | octave-jumping sixteenths in E Phrygian under a four-bar cutoff sweep |
| 4 | NEBRA SKY DISC | Berlin school | SKY DISC CHOIR on the keys, ORRERY PULSE (DUAL) | the arp runs the pulse up two octaves of Em, Cm, Dm and Em; the choir holds the root; a lane sweeps the pulse |
| 5 | ORRERY | Berlin school | LONG MEG BASS, CALANAIS SEQ (DUAL) | polymeter: the bass in eighths over 16 steps, CALANAIS SEQ over 12, a wheel lane over 7 |
| 6 | GRIMES GRAVES | hypnotic techno | FLINT ACID | an A Phrygian acid line with accents and slides, a wheel lane wandering at random over 7 steps |
| 7 | ROLLRIGHT | techno | MONOLITH BASS on the keys, STONE KIT (DUAL) | the log drum four to the floor, wood knocks between, the bass rolling on the off-beats |
| 8 | TRISKELE | hypnotic techno | TRISKELE, FLINT KIT (DUAL) | minor-pentatonic sixteenths, the ice cracking on a lane of 7, the dynamics on 5; rainstick sixteenths, the menhir on two and four |
| 9 | DEEP CHAMBER | dub techno | MONOLITH BASS, RITUAL STAB (DUAL) | minor-seventh stabs on the off-beats into a long dark echo feeding the reverb; the bass follows the roots |
| 10 | DUB OFFERING | dub | DUB BARROW, HENGE SKANK (DUAL) | skank chords on the off-beats, the bass following, the echoes near feedback spilling into the spring |
| 11 | MELODICA RITE | dub | BOG REED on the keys, DUB BARROW (DUAL) | a falling D minor melody over a one-drop bassline |
| 12 | SIGNAL FIRE | dub | SPIRIT SIREN, SPIRIT ZAP (DUAL) | holds the siren while a wheel lane winds it up two bars in every four; the zaps fire into the echoes |
| 13 | AURORA ALTAR | 90s | AURORA ALTAR | major-ninth chords planing a bar each, Fmaj9, Dbmaj9, Ebmaj9 and Cmaj9, strummed |
| 14 | FROST RUNES | 90s | FROST RUNES | the bell-pad arpeggiated in sixteenths over open sus chords |
| 15 | SINGING STONES | ritual | SINGING STONES, in PYTHAGOREAN tuning on D | open fifths strummed down, the stones answering |
| 16 | OVERTONE RITE | ritual | PASSAGE TOMB, in OVERTONE tuning on D | a slow melody swinging back and forth, the WIND HARP answering in D minor |
| 17 | BELTANE | warm vintage | BELTANE STRINGS | the string machine in slow block chords |
| 18 | SARSEN BEAT | techno | STONE KIT on the keys, FLINT KIT (DUAL) | stone and wood alone: the log drum four to the floor, wood, rain and menhir; a wheel lane over 7 swaps the hits |
| 19 | RAINMAKER | ritual | RAINMAKER | knocks and chimes walking open chords, the rain swelling under them |

| Patch | Name | Sound | MOD WHEEL |
| --- | --- | --- | --- |
| 0 | LUNAR STANDSTILL | DUAL OSC: the Overtone Halo, its SCAN walked by LFO1, over the PCM STR ENS strings swelling in; a slow filter; ENS+PHASER | the overtones climb, the phaser quickens, the filter opens, a vibrato comes in |
| 1 | MOON GLASS | a Moonstone FM bell an octave up, clanging on the strike; higher notes further right | the bell turns clangorous |
| 2 | HEARTH CIRCLE | DUAL OSC: VCO SAW and PULSE 35 % with slow PWM, sine sub, a warm driven ladder low-pass, CHORUS | the filter opens, a vibrato comes in |
| 3 | BELTANE STRINGS | a VCO SAW as four drifting playheads, a mellow low-pass, ENSEMBLE | brighter, the ensemble swings deeper |
| 4 | AUROCHS HORN | DUAL OSC: two VCO SAWs 7 cents apart, an MS-10 swelling like breath into brass, TUBE | the horn blares |
| 5 | AMBER KEYS | DUAL OSC: VCO TRIANGLE and a SQUARE an octave up, a soft low-pass the strike opens, STONE PHASER | the tremolo and phaser deepen: a 70s electric piano |
| 6 | OGHAM LEAD | VCO SAW + SQUARE, square sub, a driven resonant MS-10, slow glide | the cutoff opens, the vibrato reaches full depth |
| 7 | DOLMEN BASS | VCO SAW an octave down, sine sub, a driven SH-2; tied notes slide | the filter opens |
| 8 | SKYFIRE SYNC | FULL: a hard-sync tear, OSC2 thrown up an octave and a half; MS-10 low-pass beside a resonant SH-2 band-pass; OVERDRIVE | the tear stays open |
| 9 | SKARA BRAE ORGAN | DUAL OSC: VCO SQUAREs at 8' and 4', MS-10, TUBE, scanner CHORUS, the pan turning like a rotor | the rotor spins up |
| 10 | ORRERY PULSE | VCO PULSE 20 % with PWM, sine sub, a resonant SH-2 pluck | the cutoff opens |
| 11 | CALANAIS SEQ | Star Chart, each pluck lighting more harmonics, a resonant MULTI; higher notes further right | the whole sky comes out, the filter opens |
| 12 | SKY DISC CHOIR | the PCM CHOIR, its vowel drifting, a tape wow on the pitch | ooh opens to aah |
| 13 | STONE FLUTE | the PCM PAN FLUTE, its breath the noise ringing in the flute's band, a vibrato that waits | airy, the vibrato deepens |
| 14 | TUMULUS DRONE | BINAURAL: VCO SAW + SQUARE an octave down in each ear, 4 Hz apart, each ear's MS-10 wandering | low-pass to band-pass: a vocal drone |
| 15 | LONG MEG BASS | DUAL FILT: VCO SAW an octave down into a plucked SH-2 low-pass, then a MULTI high-pass | the SH-2 opens |
| 16 | FLINT ACID | VCO SAW into an SH-2 driven hard near the whistle, accents digging in, OVERDRIVE; tied notes slide | the cutoff opens, the drive rises |
| 17 | RITUAL STAB | DUAL OSC: VCO SAW and PULSE 35 %, a short MULTI envelope, mostly into the delay | the filter opens |
| 18 | MONOLITH BASS | VCO TRIANGLE an octave down, sine sub, a driven MS-10, a short pluck | the filter opens, the triangle leans to a saw |
| 19 | CAIRN KICK | VCO TRIANGLE two octaves down, its pitch falling two octaves, a driven SH-2, TUBE | a harder beater |
| 20 | FLINT HAT | white noise and a VCO SQUARE three octaves up through a MULTI high-pass | the noise darkens: the hat turns to a shaker |
| 21 | TRISKELE | Frost Shatter plucked through a resonant MULTI, LFO1 working the cutoff | the ice cracks, the filter opens |
| 22 | DUB BARROW | VCO SAW an octave down, sine sub, a warm SH-2; tied steps slide | a synced eighth-note wobble on the cutoff |
| 23 | HENGE SKANK | DUAL OSC: VCO SAW and a SQUARE an octave up, a short driven MS-10 band-pass, TUBE, a ducking BBD ECHO | band-pass to high-pass |
| 24 | SPIRIT SIREN | VCO SQUARE through an SH-2, made for the delay | LFO2 sweeps the pitch, wider and wider |
| 25 | BOG REED | DUAL OSC: VCO SQUARE and PULSE 35 %, an MS-10 between low-pass and band-pass, a vibrato that waits | brighter, the vibrato deepens |
| 26 | SPIRIT ZAP | VCO SQUARE an octave up falling two and a half octaves, a ringing SH-2, mostly delay | low-pass to band-pass |
| 27 | AURORA ALTAR | Aurora, its ripples tightening and drifting, each voice wandering in stereo, CHORUS | the ripples run faster and wider |
| 28 | FROST RUNES | STACK: a PCM BELL strike ringing into an Overtone Halo pad through CROSS-FEED | the blend moves to the pad, the halo climbs |
| 29 | HALO OF BRODGAR | DUAL OSC: Ley Line waking its harmonics and the PCM SPIRIT VOX, an octave-up PITCH SHIFT shimmer | the shimmer comes forward |
| 30 | PASSAGE TOMB | Passage Tomb's sliding resonances and a WIND HARP tuned to D minor | the harp blooms, the chamber opens |
| 31 | SINGING STONES | the PCM SINGING STN and RINGING STONES tuned in fifths on D | the scrape turns to the singing tone, the stones ring louder |
| 32 | RAINMAKER | STACK: PCM LORE wooden knocks with the PCM RAINSTICK swelling in behind | the knocks turn to chimes, the rain comes forward |
| 33 | STONE KIT | PCM LOG DRUM two octaves down on the low keys (the kick), WOOD BLOCK two octaves up on the high keys, TUBE | both swap to their other hits |
| 34 | FLINT KIT | PCM MENHIR on the low keys (the snare's place), the RAINSTICK cut short on the high keys (the shaker) | both swap to their other hits |

## Display and themes

The screen is mostly black, so the panel gives off little light when played outdoors at night. Text is set in a rune-style face, and every popup covers the whole screen. Pages are drawn with lines, lit stones and sparse fog rather than filled textures.

- **Page header.** The current section's pages are listed at the right, with the current one lit and underlined. The layer and architecture are at the left.
- **Page menu.** Each page lists its rows four at a time in large runes, the label at the left and the value at the right; the knob dials below are larger, with the value in each dial's gap. Long names are shortened on screen, for example DRV MODEL, REV TYPE and CLOCK SRC, and on the MOD MATRIX's SOURCE and VIA knobs the sources show as none, velo, keyb, rand, modwh, aftch, polat (poly aftertouch), cc1, cc2, env1 to env3 and lfo1 to lfo3. The LAYER page's rows are ARCH, VOICING, UNISON DETUNE, PORTA, PORTA TIME, MAX VOICES and ANALOG; the header names the layer.
- **Battery moon.** The moon at the top right of every page shows the battery. It waxes with the charge: a dark new moon when empty, a full moon when full. Below a third of a charge it is lit pale. On a computer's USB power it is full with eight stars around it. Until the first reading it hangs full and unlit.
- **Parameter popup (shown only while the Algorithm Encoder turns a row that picks from a list).** A row such as WAVE, FLAVOUR, a type, a mode or an ON/OFF row brings it up over the whole screen: the page small at the top, the row's name in large letters, and its options as a ledger, seven around the one it holds, which sits on a lit slab. When there are more, a stone riding a line of dots on the right shows where you are. It closes 1.5 seconds after the last turn. A number opens no popup: it changes in its row and on its knob dial. The MOD MATRIX has popups of its own (see Modulation).
- **Preset list.** While the Preset Encoder turns, the stored presets of the page's kind fill the screen as a ledger: eight at a time in large letters, each with its slot number, the one loading on a lit slab (in the middle until an end of the bank is near) and the rest all alike. A stone riding a line of dots on the right shows where you are among them all. An empty slot under the cursor reads UNSAVED, and for patches the title names the layer they load into.
- **SEQ and ARP** use the same colours as every other page and do not change with the layer.

**THEME** (HOME, GLOBAL page) sets the screen colour. It belongs to the instrument, so loading a preset never changes it. The CARNAC OS start-up screen is drawn in the theme you last chose.

| Theme | Colour |
| --- | --- |
| SAMHAIN | Orange (default) |
| BLOOD MOON | Red |
| FIRE | A bonfire: ember red in the dark shades, flame yellow in the bright ones |
| SOLSTICE | Ice blue |
| LICHEN | Green |
| HEATHER | Heather pink |
| RITUAL | Violet |
| GRANITE | Grey stone |
| OAK | Wood |

## System

**GLOBAL page** (hold HOME and press the GLO key). Knobs: CURVE (VEL CURVE), CLOCK (CLOCK OUT), BASE VEL (BASE VELOCITY) and TEMPO (INT TEMPO, greyed when following MIDI clock). Rows, the MIDI setup first: MIDI CH LAYER1 and LAYER2, CLOCK SRC (INTERNAL or MIDI), CUSTOM CC1 and CC2 (a \* after the number: that CC drives the CUSTOM CC instead of its place in the CC map), CC IN, CC OUT, PC IN and PC OUT (see MIDI implementation below), then VEL CURVE (SOFT, LINEAR or HARD, applied to incoming MIDI notes), BASE VELOCITY, CLOCK OUT (OFF or USB: while the clock is internal, MIDI clock, start and stop go out on USB; the TRS jack only receives), INT TEMPO (40 to 255 BPM) and THEME. INT TEMPO and BASE VELOCITY belong to the performance; every other GLOBAL setting belongs to the instrument: it survives power-off and stays as you set it when you load a performance. A DUAL performance that plays both layers on one channel stays that way, on your MIDI CH LAYER1 channel, and changing LAYER1 there moves layer 2 with it. The sequencer and arp follow the clock, internal or MIDI, and changing page never disturbs it. GLOBAL and TUNING sit side by side in the header; HOME takes you back to LAYER.

**TUNING page** (hold HOME and press the PIT key). Its four rows are its four knobs: MASTER TUNE (TUNE, -50 to +50 cents), TUNING, ROOT and BEND RANGE (BEND, 0 to 24 semitones at full bend). TUNING retunes every key by a few cents: EQUAL (none), then the tunings closest to Carnac's drones and stone-age sound, OVERTONE (harmonics 16 to 30), JUST (5-limit), PYTHAGORAS (pure fifths) and SEPTIMAL (7-limit), then MAQAM RAST (3rd and 7th a quarter tone flat), MEANTONE and WERCK III, then USER 1 to 4. ROOT (shown for the built-in tunings only; its knob is greyed otherwise) picks the key the tuning starts from. KEY TRACK scales the tuning, so an oscillator that does not track stays put. The USER tunings are whole-keyboard tables you load over USB as MIDI Tuning Standard SysEx (a bulk dump goes to USER program + 1, a scale/octave tuning to the USER tuning playing, else USER 1), or as a Scala .scl file on the web installer's Load a tuning section; they belong to the instrument and survive power-off. TUNING, ROOT and BEND RANGE belong to the performance, MASTER TUNE to the instrument. The picture is the octave as a rising row of 13 standing stones, from ROOT to its octave: each stone stands off a mark on the ground (its place in equal temperament) by its real cents, so EQUAL stands even and JUST or MAQAM RAST lean visibly out of line. ROOT and its octave are lit, the notes you play light up, a dashed span shows how far a full bend reaches, and A4 in Hz shows MASTER TUNE.

**MIDI implementation** (installed, not yet released). Every control a sound or a performance is made of answers MIDI on its layer's channel: the ones you play as Control Changes (CC), all of them as NRPN with exact values. A DUAL performance with both layers on one channel moves both layers. The performance's own controls (delay, reverb, MIX, GLOBAL, TUNING) answer on Layer 1's channel. A CC spreads 0 to 127 evenly over a control's values: a list in equal bands, a switch on from 64.

| CC | Controls |
| --- | --- |
| 0 | BANK SELECT for the next Program Change: 0 patch, 1 performance, 2 sequence, 3 arp pattern (CC 32 ignored) |
| 1 | Mod wheel |
| 3, 5, 9 | ANALOG, PORTA TIME, UNISON DETUNE |
| 12, 13 | DELAY TIME, DELAY FDBK |
| 14, 15, 17, 18, 19 | OSC 1: WAVE SCAN, TUNE, LFO2>PITCH, ENV2>PITCH, ENV3>SCAN |
| 20 to 25 | OSC 2: WAVE SCAN, TUNE, FINE, LFO2>PITCH, ENV2>PITCH, ENV3>SCAN |
| 26 to 29 | OSC MIXER knobs 1 to 4 (they follow the architecture, as on the panel) |
| 30, 31 | OSC 1 WAVE, OSC 2 WAVE (the list in order: VCO, the wavetable banks, the PCM waves) |
| 40, 41, 42, 44 | ENV 2: ATTACK, DECAY, SUSTAIN, RELEASE |
| 45 to 48 | ENV 3: ATTACK, DECAY, SUSTAIN, RELEASE |
| 49, 50, 76, 77, 78 | LFO 1: RISE, TO FILTER, RATE, TO PITCH, DELAY |
| 51 to 54 | LFO 2: RATE, DELAY, FADE, SHAPE |
| 55 to 58 | LFO 3: RATE, DELAY, FADE, SHAPE |
| 59, 90, 92 | AMP DRIVE, DRV MODEL, TREMOLO |
| 60 to 63, 95 | REVERB TIME, REVERB DAMP, DEGRADE, PING PONG, REV TYPE |
| 64 | Sustain pedal |
| 70, 71, 74, 102, 103, 104 | FILTER 1: TYPE MORPH, RESONANCE, CUTOFF, ENV2>CUTOFF, DRIVE, KEY TRACK |
| 72, 73, 75, 79 | ENV 1: RELEASE, ATTACK, DECAY, SUSTAIN |
| 80 to 83 | MIX: BALANCE, TONE, DLY>REV, REV>DLY |
| 85 to 89 | MFX knobs 1 to 4 (they follow the MFX type), then the MFX type |
| 91, 94 | REVERB SEND, DELAY SEND |
| 105 to 110 | FILTER 2: TYPE MORPH, CUTOFF, RESONANCE, ENV2>CUTOFF, DRIVE, KEY TRACK |
| 111 to 114 | FILTER MIXER knobs 1 to 4 (they follow the architecture) |
| 115 to 119 | NOISE LEVEL, NOISE COLOUR, HARMONIC, COL MOD, LEVEL MOD, in every architecture |
| 120 to 127 | ALL SOUND OFF, RESET ALL CONTROLLERS, LOCAL CONTROL (ignored), then ALL NOTES OFF (a held sustain pedal keeps its notes) |

**NRPN.** Every control of the sound and performance pages has a fixed NRPN number: CC 99 and 98 select it, CC 6 sends the value, and CC 38 after it adds the low 7 bits where a value passes 127 (INT TEMPO). The value is exact, as stored; WAVE and ARCH go by their place in the list on screen. The numbers are frozen for version 1: a new control gets a new number, and none ever moves. The 16 MOD routes are NRPN 64 to 79, one per route, with LSB 0 SOURCE, 1 VIA, 2 DEST, 3 AMOUNT, 4 VIA DEPTH; the MOD page keeps routes sorted by SOURCE. The web editor lists every number.

**RPN.** RPN 0/0 is BEND RANGE (semitones, up to 24). RPN 0/1 is MASTER TUNE on the General MIDI scale, plus or minus 100 cents across its range; the FM1 stops at plus or minus 50. Both answer on Layer 1's channel.

**Program Change.** After BANK SELECT, bank 0 loads that patch at once into every layer on the channel. Banks 1 to 3 (performance, sequence, arp pattern) load on Layer 1's channel, as the Preset Encoder does; an empty slot loads nothing.

**CC IN, CC OUT, PC IN and PC OUT** (GLOBAL rows, kept through power-off). CC IN (on) takes the CC map, NRPNs and RPNs; off, only the wheel, sustain, CUSTOM CC1 and CC2, bank select and the channel messages work. PC IN (on) takes Program Changes. CC OUT (off) sends what you change on the panel: its CC where it has one, else its NRPN, on the layer's channel (Layer 2 only on a channel of its own). A value that came in over MIDI is never sent back, so a DAW cannot loop. Each load, and turning CC OUT on, sends the whole sound as one dump instead. PC OUT (off) sends BANK SELECT and the Program Change for each load you make on the panel.

**The dump.** SysEx in the editor frame, manufacturer ID 7D (non-commercial). F0 7D 46 4C 38 F7 asks for the whole sound, any time. The FM1 answers with messages F0 7D 46 4C 39, the layer, then four bytes per parameter (NRPN MSB, NRPN LSB, value high 7 bits, value low 7 bits), up to 24 a message, then F0 7D 46 4C 3A with the count (low 7 bits, high 7 bits). A whole sound is 436 parameters for both layers and takes about 60 ms over USB.

**The sequence.** The web editor's SEQ tab edits the sequence playing now over SysEx in the same frame, and you hear each edit at once. F0 7D 46 4C 3C F7 asks for it. The FM1 answers with four lane messages (3D), the sequence's name (3F) and the playhead (43). A lane message is the lane number, eight footer bytes (EVENT, TARGET, MD, PRESCALE, LOOP START, LOOP END, SCALE, ROOT) and 16 steps of seven bytes (four notes, VALUE, FLAGS, and a byte marking the unused note slots). One step alone travels as 3E. The editor sends the same messages back, and the FM1 sends its own SEQ edits (the panel, step record, a load) as they happen, once the editor has asked or CC OUT is on. Other commands: 40 saves the live sequence into a slot under a name, 41 lists slot names, 42 loads a slot, 44 starts or stops the sequencer. 46 goes both ways and carries a lane and a block of four steps: from the editor, the FM1 opens its SEQ page on that lane with those four steps on its knobs; from the FM1, it reports the panel's own lane and step changes so the editor follows. The FM1 answers every one of these with 45 (an acknowledgement); the editor waits for it before it sends the next message, because the FM1 takes one SysEx message at a time.

**Processing load.** Carnac runs on both of the FM-1's cores. The sound is the same on one core or two. If the FM-1 gets close to its limit, Carnac lightens the load by itself rather than drop out, and the voice price in Voices and layers keeps each patch, with its routes, its performance's effects and a running SEQ, ARP and REC loop, inside the budget. On the device, eight plain SINGLE voices use about 43 percent of the processor. The factory performances run at about 36 to 70 percent while playing. The heaviest combinations (SH-2 filters in STACK or BINAURAL at their most voices, or heavy patches with the sequencer and arpeggiator running) reach 75 to 85 percent, where Carnac may lighten the load.

**USB audio.** Connected to a computer, the FM-1 is also a USB audio input: 16-bit stereo at 44.1 kHz, class compliant, so no driver is needed. It shows up as "Line (Carnac OS FM-1)" and carries the same output as the jacks, after the MASTER knob. Record it in any audio app.

**Installing and recovering.** Install Carnac from the web installer at [owenkirby.github.io/carnac-os](https://owenkirby.github.io/carnac-os/), in Chrome or Edge, with the FM-1 on USB. It writes the firmware, checks the version, then writes the PCM waves. The .fwsc package holds the firmware only: a command-line installer writes the firmware and leaves the waves out. After one of those, run the web installer once, or pcm\_store\_upload.py sync from the source at the same release (see the README). If an install stops part-way, replug the cable and start again: the FM-1 stays in update mode and the installer carries on. If the FM-1 does not start after an update, switch it off and on twice; it then starts in safe mode, which keeps USB working. The same page can put M-VAVE's stock firmware back. Deeper recovery steps are in the source repository (docs/fm1-recovery.md).

## Changelog

Newest first. Features marked *in source* are written but not yet installed on the FM-1.

| Date | Change |
| --- | --- |
| 2026-11-25 | Delay: the tape runs at half the sample rate again, with better filters on the way on and off it: brighter echoes (to about 9 kHz) that still never alias or hiss, and every repeat a little darker. The reverb now runs from RAM, which pays for the brighter delay: about the same processor load overall, a little less on performances with a big reverb (installed). |
| 2026-11-24 | Delay: turning DELAY TIME glides the echoes in pitch like the old tape again (within an octave, about 0.6 s), instead of a zip. The ARP plays on through page changes (installed). |
| 2026-11-24 | Voices: each core's voice budget rises from 56 to 60 and the voice price is re-measured, so heavier patches (SH-2 filters, PCM waves, an MFX) get up to 2 more voices; STACK with two SH-2 filters plays 6 (installed). |
| 2026-11-24 | Delay: the tape runs at a quarter of the sample rate with 16-bit steps, a darker (about 5 kHz), BBD-like echo with less hiss and no aliasing grit, and it rests when silent. The delay and the voices' control code now run from RAM: about 7 points less processor load on average, with no dropouts in the PERF TEST (installed). |
| 2026-11-23 | ARP STYLE and VARIATION are now 9 families of 12 named phrases (BEAT, ARP, PULSE, STAB, STRUM, GUITAR, KEYS, BASS, HARP) that play parts of the chord, from guitar strums to bass-and-chord figures, looping over the bar while the steps change the chord. The OCTAVE knob became SPAN (AUTO or 1 to 4 OCT). Factory arps moved onto the new phrases; DUB OFFERING now skanks on 2 and 4. Saved arps load but sound different (installed, not yet released). |
| 2026-11-21 | The MS-10 replaces the MS-20 filter: a model of Korg's MS-10 Sallen-Key filter (the Korg35), its diodes in the signal path, so loud playing takes the resonance away and distorts everything while quiet passages ring, lopsided. RES now rises evenly and self-oscillates from about 108 at every cutoff, in tune up to about 2.7 kHz; TYPE MORPH's high-pass is the real 6 dB per octave. Patches load with the MS-10 in the MS-20's place and ring less than they did (installed, not yet released). |
| 2026-11-21 | The VOICES popup is now a strip across the bottom third of the screen, so the rows and knob dials stay readable while you turn; the encoders no longer put it away, buttons still do. On SEQ, ARP and REC it still fills the screen (installed, not yet released). |
| 2026-11-20 | New factory bank THE OLD KNOWLEDGE replaces SAMHAIN: 35 patches and 20 performances, each performance with its own effects, MIX, tempo, tuning, sequence and arp. Power-on plays LUNAR STANDSTILL. The voice price now counts MOD MATRIX routes, the reverb type, PING PONG, the MIX feeds and LOOP TONE's wear, and always counts the SEQ, ARP and REC loop as running, so PLAY never causes a dropout. New VOICES popup: an edit that changes a layer's voice count shows the new count for 1.5 seconds (installed, not yet released). |
| 2026-11-20 | Quieter delay tape: the hiss under a dark bass falls from about 30 to about 42 dB below the echo, the echo itself unchanged. DEGRADE: above 36 the repeats darken only to about 3 kHz at 127 (they went to about 450 Hz), and past 64 the tape breaks up, with dropouts that compound each pass and head snags. At 64 and below nothing changes (installed, not yet released). |
| 2026-11-20 | The DLY > REV and REV > DLY feeds run at half rate and REV > DLY moved to the second core, so the loop costs about a third less with the same sound. LOOP TONE now also wears the delay's tape as it leaves the middle: saturated echoes that swell, breathe and drift in length. At the middle the loop is clean (installed, not yet released). |
| 2026-11-20 | CHORD KEYS: hold HOME and press ARP, and every key on a sound page plays the ARP's chord bank (GENRE BANK, the live ARP knobs, LATCH) while you shape the sound. Never recorded, off at power-on; the ARP button stays lit while it is on (installed, not yet released). |
| 2026-11-20 | ARP VARIATION sets the rhythm only: every cell lasts one RATE, so the three-cell rhythms no longer change the note division. Triplets come from a T RATE. Four-cell rhythms play as before (installed, not yet released). |
| 2026-11-20 | New voicing POLY UNISON: a poly mode in the style of the Jupiter-6 unison, where the notes held share the layer's voices as unison stacks (one note all of them, two half each, and so on). HOME + POLY now steps through POLY REUSE, POLY RESET and POLY UNISON, and the VOICING popup shows it as three stacks in a row. Installed on the FM1, in main, not yet released. |
| 2026-11-14 | Drop-out fixes. The delay is now a half-rate, 8-bit tape echo (darker, grainy repeats, never aliases, 1.5 s; PING PONG to 0.74 s a side) and costs a fraction of the old one. DELAY TYPE and the BBD are removed. Voice limits now follow the measured load: a heavy performance (big reverb, SEQ and ARP, PCM stacks) gets fewer new voices while it runs, held notes keep playing, and a new note steals a release tail instead of the audio dropping out (installed) |
| 2026-11-19 | Carnac OS 0.9.7 built, with every row above the 0.9.6 row (not yet installed, PERF TESTed or released). Saved sequences carry over. Patches, performances and arp patterns saved with 0.9.6 or earlier do not: their formats moved on (patches v14, performances v15, arps v6) on the way to version 1, and older ones are not read. |
| 2026-11-19 | Web editor redesign: the whole sound on one screen as stone tablets (VOICE with the architecture's signal path, OSC, MIXER, FILTER, ENV, LFO, AMP and MFX, FX, a MOD glance), a MOD view with a ley-line map, a SETUP view of knobs and stone menus, and a SEQUENCE view drawn like the FM1's SEQ page. The page follows THEME. Clicking a step in the editor puts its lane and block of four on the FM1's knobs, and the panel's lane and step changes show in the editor (new SEQ SysEx command 46; in source, not yet installed). |
| 2026-11-19 | Web editor SEQ tab: edit the live sequence from a browser (four lanes of 16 steps, chords, value lanes drawn with the mouse, loop, playhead), SAVE it into a slot by name, LOAD a slot. Works with the FM1 and the browser emulator. New SEQ SysEx commands 3C to 45 (installed, not yet released). |
| 2026-11-19 | Full MIDI implementation: a CC map on regular synth conventions (filter, envelopes, LFOs, effects, MFX, noise in every architecture), a fixed NRPN number for every control, RPN BEND RANGE and MASTER TUNE, Program Change for patches, performances, sequences and arps. New GLOBAL rows CC IN, CC OUT, PC IN, PC OUT: CC OUT sends the panel's moves and a whole-sound dump at each load, for the web editor. INT TEMPO runs to 255 BPM over NRPN. Wavetables are now 8-bit, freeing about 64 KB of flash for Bluetooth MIDI and new waves. Patches will be replaced by a new factory bank before 0.9.7 (installed, not yet released). |
| 2026-11-13 | Manual brought up to date with the work since 0.9.6. Now described: the MOD MATRIX (16 routes) in place of the MOD slots and the pages' MOD SRC rows, with fixed paths on the pages (OSC knobs WAVE SCAN, TUNE, LFO2>PITCH and ENV2>PITCH; FILTER knob 4 ENV2>CUTOFF; LFO1 laid out like the Jupiter-6; WHEEL>DEPTH on every LFO; VEL > ATTACK on the envelopes). FOUNDATIONS played by a model of the SH-2's VCO-1 with real pulse-width modulation, nine wavetable banks and a rebuilt bank of 14 PCM waves. STRIKE and DUAL SPRD gone (DUAL SPRD patches load as DUAL OSC). The NOISE page with its resonant COLOUR and HARMONIC; STACK's CROSS-FEED. Voices priced by what each patch costs (DUAL OSC and DUAL FILT now 8, FULL 6), and a ringing tail cut before a voice is shed. The J-6 chord bank and the ARP's VARIATION knob. The REC page loop recorder. Poly aftertouch SEQ lanes. LOOP TONE in place of DELAY HP, and DELAY TIME showing its division. INT TEMPO up to 255 BPM. The SAVE name keyboard. The ledger preset list, and the parameter popup now only on list rows. The Select Encoder opens no popup. The THEME list corrected (FIRE, RITUAL). The factory bank at 26 patches and 8 performances (installed). |
| 2026-11-13 | MOD MATRIX popups: turning SOURCE, VIA or DEST shows that knob's list (sources spelled out, DEST only the architecture's), with the route in short at the foot. Turning VIA DEPTH or AMOUNT shows the whole route as a card with a bar (installed). |
| 2026-11-13 | HOME + black-key shortcuts: hold HOME and press PIT for the new TUNING page, GLO for GLOBAL, MONO or POLY to change the focused layer's voicing. While HOME is held only those keys light. TUNING is a new page with MASTER TUNE, TUNING, ROOT and BEND RANGE (now 0 to 24) on its rows and knobs, and a picture of the octave as standing stones leaning by each tuning's cents. GLOBAL lost those rows; its knobs are now CURVE, CLOCK, BASE VEL and TEMPO. HOME now cycles LAYER, ZONES and PERF only (SINGL: LAYER and PERF). Settings and patches carry over (installed). |
| 2026-11-13 | The MFX page is gone: the MFX is now the end of the AMP page (EDIT). Scroll onto its row and the knobs become the MFX type's, with its rows and picture; scroll back up and they are AMP's again. GLO now flips between EFFECTS and MIX with one press. On the ARP's STEPS view, OCT+ held + a step key sets SEQ LENGTH, and the ARP menu has a scroll bar. Patches and performances carry over (installed). |
| 2026-11-13 | SAVE on a sound page now opens the save page on the performance, not the patch: a performance already keeps its own copy of both layers' sounds, so a tweak is kept without saving a patch. Turn TARGET to PATCH to save the patch itself; the slot follows the TARGET. COPY and PASTE on a sound page still work on the patch (installed). |
| 2026-11-13 | The ARP page has two views, switched with ARP. CHORDS makes every key a chord key in its own octave, and its screen shows the chord sounding: its name, a four-octave keyboard with the struck notes lit, and the last four chords. STEPS makes the 16 lowest white keys the steps: patterns now have up to 16 steps, and holding a step and pressing a later one ties them. LATCH is a CHORDS row, or hold ARP and press a key. The COPY, TIE, CLEAR and ACCENT function keys are gone (CLEAR and ACCENT are STEPS rows). Saved arps and performances load empty after this install; patches and sequences are kept (installed). |
| 2026-11-11 | LFO1's DELAY and FADE now restart only on a new phrase (the first note after every key of the layer was up), as on the Juno-106: notes played over held ones keep the vibrato going, and KEY RESET restarts LFO1's phase at the same moment. OSCMIX reworked: SUB OCTAVE and SUB WAVE are one SUB WAVE setting (SINE, SQR or CURR, -1 or -2 octaves), NOISE LEVEL is on the OSCMIX page in every architecture, and the NOISE page's knobs are COLOUR, HARMONIC, COL MOD and LVL MOD. SINGLE's SPREAD MOD amount is now the SPR MOD AMT row. On the OSC pages FINE TUNE is knob 3 in every architecture (SINGLE's SCAN AMT knob is the SCAN MOD AMT row again), and KEY TRACK shows only with two oscillators. Patches load unchanged (installed). |
| 2026-10-29 | Custom tunings: GLOBAL gained TUNING (EQUAL, OVERTONE, JUST, PYTHAGORAS, SEPTIMAL, MAQAM RAST, MEANTONE, WERCK III, USER 1 to 4) and ROOT, saved with each performance. USER tunings load over MIDI Tuning Standard SysEx or from a Scala .scl file on the web installer. Older performances play in EQUAL, unchanged (installed). |
| 2026-10-29 | Carnac OS 0.9.6 released: 0.9.5 plus the wave check at power-on (the row below). Patches, performances and settings carry over unchanged (installed). |
| 2026-10-29 | The FM-1 checks its waves at power-on. If the PCM waves or FOUNDATIONS are missing (the firmware was installed without them, for example the .fwsc alone with another tool), the start-up screen shows the field with no standing stones and WAVES MISSING for 3 seconds; if they come from another version, WAVES OUT OF DATE. The web installer, or pcm\_store\_upload.py sync, fixes it. Nothing else changes (installed). |
| 2026-10-27 | New power-on performance CAIRN SPIRIT in slot 0: press PLAY and the arp plays CAIRN SPIRIT pad chords (A minor add9, F major 9, C add9, G sus4) while the sequencer pulses a CROM PULSE bass line under them that follows each chord. Its arp pattern is no longer empty. SAMHAIN and the other factory performances move down one slot (SAMHAIN is now 1, WILL O WISP 8), and so do their sequences and arp patterns. A performance, sequence or arp you saved yourself stays in its slot (installed). |
| 2026-10-27 | The PERF page's dolmen is now drawn in the knob tablets' stone colour instead of the theme colour (an empty part's pillar is an outline in the stone's lightest tone) (installed). |
| 2026-10-27 | SPREAD is called SINGLE again, and the ARCH list runs SINGLE, DUAL OSC, DUAL FILT, DUAL SPRD, FULL, STACK, BINAURAL, STRIKE. On the PERF page, turning L1 PATCH, L2 PATCH, SEQ LINK, ARP LINK or PERF PRESET now loads that preset (before, it only changed the number); the popup shows the preset's name under its number. GLOBAL lists MIDI CH LAYER1 and LAYER2, CLOCK SRC, MASTER TUNE, BEND RANGE and CUSTOM CC1 and CC2 first. Patches and performances are unchanged (installed). |
| 2026-10-27 | New factory bank SAMHAIN replaces RITES OF STONE: 32 patches (8 basses, 8 leads, 8 mystical sounds, 8 percussion), mostly FOUNDATIONS through the SH-2 and MS-20, and 8 performances. Power-on now plays performance 0, SAMHAIN: DUB BARROW under CAIRN SPIRIT with a dub bass line. NEWGRANGE DAWN stays as patch 16. Your saved patches, performances, sequences and arps are kept; factory slots you never saved over now hold the new sounds (installed). |
| 2026-10-27 | PCM waves reworked: new RAINSTICK, LORE, MOONSONG, SINGING STN, PAN FLUTE and CAIRN SPIRIT; CLAY CHIMES, SHELL CHIMES and STRINGS are gone, and patches that used them now play RAINSTICK, LORE and MOONSONG. CHOIR and HORNS have a much wider SCAN (ooh to a ringing aah, soft to overblown). The HOLLOW REEDS wavetable bank is gone. Attack sounds and PCM loops are stored more compactly to make room, sounding the same (installed). |
| 2026-10-26 | New wavetable bank FOUNDATIONS, first in the WAVE list: the classic analog shapes (TRIANGLE, SAW, SQUARE, three PULSE widths, PULSE MIN, SAW+SQUARE) drawn from a model of an SH-2-style oscillator. The MENHIR bank is gone and the other banks moved, so older wavetable patches may play a different bank. The installers now write the PCM waves and FOUNDATIONS by themselves after the firmware (installed). |
| 2026-10-26 | ARCH list reworked: new FULL (DUAL OSC's oscillators into DUAL FILT's filters, 4 voices) and DUAL SPRD (both oscillators as SPREAD, mixed by OSC MIX, 4 voices). SINGLE is gone: SPREAD at SPREAD 0 is the same sound, and SINGLE patches load that way. DUAL OSC now plays 6 voices (was 5) and SPREAD 8 (was 6) (installed). |
| 2026-10-26 | Easier-to-read screen: page menus show four rows at a time in large runes, with bigger knob dials. Long row names are shortened to fit (SPD MOD SRC, DRV MODEL, CLOCK SRC and so on), and the LAYER rows drop their L1 / L2 prefix (VOICE MODE is now VOICING). SEQ step keys snap to blocks of four (installed). |
| 2026-10-27 | ENV2 and ENV3 INVERT now runs from 0 down to -1 instead of from full down to 0: an inverted envelope leaves its destination at the knob's setting at rest and pushes it below that with the note, so FILTER CUTOFF, pitch or MORPH can sweep down from where you set them. AMP MOD and DEPTH MOD still read it as full at rest, dipping with the note. Patches that use INVERT sound different, among them the factory CONCH CALL (in source). |
| 2026-10-26 | Voices shared by keys, SEQ and ARP: when a layer runs out, the keys outrank the SEQ, which outranks the ARP, and the ARP and SEQ thin their chords from the middle. The ARP and SEQ no longer cut each other's notes on the same pitch. New arp patterns default to L2 (the SEQ stays on L1), and SINGL no longer resets the ARP's LAYER to L1 (in source). |
| 2026-10-26 | New VEL > LEVEL row on ENV1, ENV2 and ENV3: how much quieter a soft note makes that envelope, everywhere it is routed. ENV1's replaces the fixed velocity-to-volume curve (111 plays as before; 0 makes volume ignore velocity). New DEPTH MOD SRC and DEPTH MOD AMT rows on LFO1 to LFO3, so the mod wheel, aftertouch, velocity, an envelope or a CC can set an LFO's depth without a mod slot. Saved patches and performances load unchanged. Costs about 1 to 2 points of CPU on most PERF TEST rows, up to 5 on heavy SH-2 patches (installed) |
| 2026-10-26 | FILTER1 and FILTER2 MOD AMOUNT is now 0 to 127, positive only, with twice the steps it had (it was -64 to +63). The same goes for F1 CUTOFF and F2 CUTOFF on the MOD page ring. Factory patches sound as before; saved patches are converted when they load (a negative amount becomes 0) (installed) |
| 2026-10-26 | DELAY TIME settles in about a tenth of a second instead of half a second. Before, a note played soon after turning DELAY TIME got a first echo at an in-between length and repeats at another; now every repeat keeps the new length. Turning DELAY TIME while the delay howls still bends its pitch, a little quicker (installed) |
| 2026-10-26 | BBD delay cleaned up: long delays no longer sound bitcrushed or aliased. The BBD now models four chips in a chain, so its clock stays high even at a 1-second delay, and its filters stay below the clock. Its darkness, wobble and noise still follow DEGRADE (installed) |
| 2026-10-26 | DELAY TIME now tops out at 1.0 s instead of 1.47 s, so the knob has finer steps (about 8 ms instead of 12) over the useful range. The factory performances keep their delay times. Your saved performances with DELAY SYNC off play their delay about a third shorter: set DELAY TIME again (installed). |
| 2026-10-26 | New EFFECTS row DELAY HP: a gentle high-pass in the delay's feedback loop, so repeats lose more bass each time, like a dub delay. 0 = off, which is how saved performances load; 1 to 127 sweeps about 30 Hz to 1 kHz. The EFFECTS rows now list the delay's settings first, then the reverb's (installed). |
| 2026-10-25 | Carnac OS 0.9.4 released, with every row above the 0.9.2 row (0.9.3 added the RITES OF STONE bank and power-on into NEWGRANGE DAWN). New since 0.9.3: AMP DRIVE TUBE and OVERDRIVE, sequence and arp names, GLOBAL settings that survive power-off, shared noise, MONO layers taking one voice in DUAL and SPLIT, the ARP step peek, finer BINAURAL BEAT HZ, and tight MIDI: notes and clock coming in over USB or TRS now play at a steady 23 ms delay and the clock locks to an external tempo instead of jittering. DELAY SYNC divisions now run short to long across DELAY TIME, like the unsynced delay. From 0.9.3, patches, performances, sequences, arps and settings all carry over; a saved performance with DELAY SYNC on may land on a different division, so set its DELAY TIME again. |
| 2026-10-25 | GLOBAL settings now survive power-off and performance loads: MIDI CH LAYER1 and LAYER2, CUSTOM CC1 and CC2, BEND RANGE and CLOCK SOURCE join MASTER TUNE, VEL CURVE, CLOCK OUT and THEME. DUAL performances that stack both layers keep stacking, on your layer-1 channel. INT TEMPO and BASE VELOCITY still come with each performance (in source). |
| 2026-10-25 | AMP DRIVE reworked: two new models, TUBE and OVERDRIVE; smoother SOFT, HARD, FOLD and TAPE curves; much more gain (up to +30 to +40 dB); and the level holds roughly even as DRIVE turns. Patches keep their settings, but driven ones sound different and come out up to 5 dB quieter than before. Each driven voice costs about half a point of CPU (installed). |
| 2026-10-25 | Sequences and arp patterns can be named on the SAVE page like patches and performances; the preset list, SAVED and the CLIP line show the names. Saved sequences and arps keep loading, with the factory names until renamed (installed). |
| 2026-10-25 | ARP: holding a step key that has a chord puts that step's RATE, OCTAVE, STYLE and RHYTHM on the knobs; the live values come back when you let go. Rests and TIEs leave the knobs alone (in source) |
| 2026-10-25 | BINAURAL BEAT HZ now spans 0 to 15.9 Hz in 1/8 Hz steps (was 0 to 31.75 Hz in 1/4 Hz steps), for finer control. In source, not yet installed. |
| 2026-10-25 | Voices: in DUAL and SPLIT a MONO layer (MONO RETRIG or MONO LEGATO) takes one voice and the other layer gets the rest (two SINGLE patches: 1 + 7 instead of 4 + 4). In SPLIT, layer 2 now has its own VOICE MODE and UNISON DETUNE instead of copying layer 1's; saved SPLIT performances use the layer 2 voice mode stored with them (in source). |
| 2026-10-25 | Noise is now one shared source for all voices, as on classic polysynths (BINAURAL's right ear keeps its own). Each voice still sets its own noise level and colour. Noisy patches use much less CPU: up to 11 points less on the heaviest test patches, and 12 more of the 70 load tests now pass. With noise off, the sound is unchanged (installed) |
| 2026-10-21 | New factory bank RITES OF STONE: 32 performances and patches built on PCM waves, the SH-2, MS-20 and FORMANT filters, STRIKE attacks, mod slots, synced envelope retrigger, DUAL and SPLIT. The FM-1 now powers up into performance 0, NEWGRANGE DAWN, instead of a bare default patch. The old bank STONE & FIRE is gone (installed). |
| 2026-10-21 | Carnac OS 0.9.2 released, with everything below it in this table. New since the rows below: PCM waves in the WAVE list (chimes, strings, choir, horns, with their own attacks; SPREAD works on them), the HOLLOW REEDS wavetable bank, ANALOG on the LAYER page, KEY TRACK on both FILTER pages. The web installer writes the PCM waves after the firmware. From 0.9.1, patches carry over; sequences and performances saved with 0.9.1 do not (installed). |
| 2026-10-21 | ENV2 / ENV3 display: an inverted envelope is drawn upside down (in source). |
| 2026-10-21 | The start-up screen follows THEME: the standing stones, sun and title are drawn in the colours of the theme saved last session (BLOOD MOON red, SOLSTICE blue, GRANITE grey, and so on). SAMHAIN keeps the original orange (in source). |
| 2026-10-21 | The moon at the top right now shows the battery instead of the bar clock: it waxes with the charge, is lit pale below a third, and wears a ring of stars on USB power. USB audio input documented (it was already on). Installed (synth\_battmoon1). |
| 2026-10-21 | DUAL: the keys play the selected layer, on that layer's MIDI channel; SINGL and SPLIT keys follow layer 1's channel. Installed (synth\_dualkeys1). |
| 2026-10-21 | Knob smoothing: CUTOFF, RESONANCE and TYPE MORPH on both FILTER pages and OSC SCAN now glide to the knob over about 25 ms instead of jumping a step at a time, so a slow turn sweeps instead of stepping. MIDI CC and sequencer lanes that move these knobs glide too; loading a patch or performance jumps straight to its values. Patches sound the same at rest (installed). |
| 2026-10-21 | The SEQ and ARP buttons blink on the beat while their transport plays (installed). |
| 2026-10-21 | ARP timing split: the new TIMING row sets every step's length, and knob 1 RATE now sets how fast each step's notes play (1/2 to 1/128) and a HOLD strum's speed. HOLD is first in the STYLE list and no longer re-strums a repeated chord at fast TIMING; its STRUM is NONE, UP or DOWN. The ARP's dim step-key lights are brighter (half instead of quarter). Saved arps and performances load sounding as before: TIMING from the first chord step, each RATE filling its old step, HOLD strums at the nearest RATE (in source). |
| 2026-10-21 | Filters reworked: the SH-2 (Roland OTA four-pole) replaces the SVF, and MS-20 LOW and HIGH become one MS-20 whose TYPE MORPH sweeps low-pass, band-pass and high-pass. New DRIVE row, first on each FILTER page, for every flavour (on the MS-20 it takes over what TYPE MORPH did). Saved patches and performances load: SVF ones as MULTI (LP12, BP or HP12, matching their morph), MS-20 LOW and HIGH ones as the MS-20 at the same drive, low-pass or high-pass. The default patch's filter is now MULTI LP12 (in source). |
| 2026-10-21 | SEQ controls reworked: SEQ picks the lane (each press, the next lane), the 16 white keys are the lane's steps with playhead and knob-window lights, a step key puts that step and the next three on the knobs, the Select Encoder walks the steps in groups of four and then the footer (PRESCALE, EVENT TYPE, TARGET, SCALE, ROOT, PLAY MODE), and the playing step's knob card is outlined. The LANE row is gone (in source) |
| 2026-10-17 | Sequencer and arpeggiator have their own transports: PLAY on SEQ runs the sequencer, on ARP the arpeggiator, on any other page both, and one started while the other plays waits for its next bar. ARP knob 1 is now RATE, each step's own length; GATE moved to the ARP rows in place of TIME DIVISION. New STYLE HOLD holds the chord without retriggering, with RHYTHM as an opening strum. New genre Phrygian, now the default. TAPE DEGRADE renamed DEGRADE. Saved arps and performances load with every step at the old TIME DIVISION (installed). |
| 2026-10-17 | STRIKE's ATTACK page reorganised: knobs DECAY, TO FILTER, BEND and the new BEND MOD; START is now a row after SOUND, and a new BEND MOD SRC row picks what moves the bend (default VELOCITY). Saved patches load with BEND MOD off (installed). |
| 2026-10-17 | MOD slots: with the a\*b function, a slot with only SOURCE A (or only SOURCE B) set now passes that source through instead of giving silence; with neither set it gives nothing. HOME reorganised: its pages are LAYER (the focused layer's settings; knobs VELOCITY, MOD WHEEL, CC1, CC2), ZONES (knobs SPLIT NOTE, SPLIT FADE, VEL SPLIT, VEL FADE), PERF (the performance browser, moved from SAVE, with PERF MODE and the dolmen redrawn) and GLOBAL (moved from GLO, which now cycles AMP and EFFECTS). Pitch bend and the porta knob left the panel. New save page: SAVE opens it on what you were editing, saves at once to an empty slot, asks only before overwriting, shows the bank as stones with the moon over your slot, and returns you to your page. COPY and PASTE moved to SAVE + OCT- / OCT+, and holding HOME or SAVE shows what OCT does. GLOBAL gained MASTER TUNE, VEL CURVE and CLOCK OUT (USB). OSC FINE TUNE now reaches 7 semitones; in SINGLE, SPREAD and STRIKE knob 3 is SCAN AMT and FINE TUNE a row. The big parameter popup now opens only while the Algorithm Encoder turns a value; the Select Encoder moves through the list without it. MOD pages redrawn: a ring of destination stones round the portal with rays carrying the signal out to each fed destination, and routing from the MOD page by turning a destination's amount (installed). |
| 2026-10-16 | Manual started. Reverb buzz with several voices fixed (send limiting), endless PLATE whine fixed. New SPRING reverb, BBD delay and DELAY TYPE. Both delays can self-oscillate above FEEDBACK 100. Layer colour schemes dropped for one palette; HOME + OCT still switches layers. Minimal black screen and full-screen popups, with the moon-and-arch value display, rune face and global THEME list *(in source)*. SEQ note lanes gained SCALE and ROOT. Reverb types PLATE and HALL removed and ROOM renamed TOMB, so the list is now SPRING, TOMB, MOD, CELESTIAL, DOLMEN and TUMULUS (in source; factory presets that used PLATE or HALL now use TOMB, which sounds the same, and saved performances from before this change will not load). MOD pages redrawn: the capstone shows the slot's equation, the portal radiates rings bent by the modulation and goes dark at AMOUNT 0, and the slot's destinations are listed over the portal, stepped with the Select Encoder and set with the Algorithm Encoder. New wavetable bank HEARTH (bank 15 of 15), a warm pad that morphs from banked embers to a caught fire. New STRIKE architecture: a short attack sound (20 to choose from, all neolithic: chiffs, conch, stones, wood, bone, drums, bow pluck) at the start of each note over the wavetable. Patches saved before this load unchanged, with STRIKE's settings at their defaults. STRIKE got its own ATTACK page (FX: OSC1, ATTACK, OSCMIX) with DECAY, TO FILTER, START, VELOCITY and KEY TRACK; the attack's LEVEL sits on OSCMIX with the other sources (ATK LEVEL, NOISE COLOUR, SUB LEVEL, NOISE; SUB OCTAVE a row), START is ATTACK's knob 2. ATTACK gained BEND (knob 4, as OSC1's PITCH MOD): its knobs are now START, DECAY, TO FILTER, BEND, and SOUND is its first row. The GLOBAL page moved from GLO to the end of HOME: GLO cycles AMP and EFFECTS. HOME's first page now follows the layer in focus (HOME + OCT), showing that layer's ARCH, VOICE MODE, glide and voices, so HOME cycles HOME, ZONES, GLOBAL (SINGL: HOME, GLOBAL) (installed) |
| 2026-10-15 | Two cores live. New reverbs MOD, CELESTIAL, DOLMEN, TUMULUS, PING PONG and the TAPE DEGRADE control. Float filters, MULTI filter and MS-20 growl. SEQ with PLAY and REC re-fires the record step on the clock. SAVE button is the shortcut to ACTION, only the current page's button lights, pitch bend springs back, the Select Encoder opens a parameter list, MOD 4 removed, poly glide starts from the last note |
