# Carnac OS User Manual

## Introduction

Carnac OS turns the M-VAVE FM-1 into an eight-voice wavetable synthesizer with two layers, a four-lane sequencer, a chord arpeggiator and a stone-circle-themed screen. It is built on the X0X platform and takes its name from the megalithic alignments at Carnac.

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
5. **Browse a page's other settings.** Turn the Select Encoder. A list of the page's rows opens over the whole screen; the Algorithm Encoder changes the highlighted value.
6. **Save it.** Press SAVE. The button flashes while a save is armed; press it again to confirm (see Presets and saving).

To play two sounds at once, change PERF MODE on HOME from SINGL to DUAL or SPLIT. Layer 2 is unavailable in SINGL.

## Panel and controls

Ten section buttons open the pages. Pressing a button again moves to its next page; hidden pages (for example OSC2 in SINGLE) are skipped.

| Button | Pages | What it holds |
| --- | --- | --- |
| FX | OSC1, OSC2, OSCMIX | Oscillators and their mixer (named FX on the panel, it opens the oscillator pages) |
| SEL | FILTER1, FILTER2, FLTMIX | Filters and their mixer |
| ENV | ENV1, ENV2, ENV3 | Envelopes. ENV1 is hardwired to the amplifier |
| LFO | LFO1, LFO2, LFO3 | LFOs. LFO1 is global; LFO2 and LFO3 run per voice |
| EDIT | MOD1, MOD2, MOD3 | Three modulation slots |
| GLO | AMP, EFFECTS | Amplifier, reverb and delay |
| HOME | LAYER (the focused layer), ZONES, PERF, GLOBAL (SINGL: no ZONES) | Layer settings and play knobs, splits, the performance browser, global settings |
| ARP | one page | The chord arpeggiator |
| SAVE | SAVE | Saving (COPY and PASTE on SAVE + OCT) |
| SEQ | one page | The four-lane sequencer |

**Encoders and knobs**

- **Select Encoder** moves through the rows of the current page. Each click opens a full-screen list of the page's rows; it closes 1.5 seconds after the last click, or when you touch another control.
- **Algorithm Encoder** changes the highlighted value. It keeps the list open while you turn it.
- **Preset Encoder** loads the next or previous stored preset. What it loads depends on the page: a layer patch on the sound pages, a performance on HOME, an arp pattern on ARP, a sequencer slot on SEQ.
- **Knobs 1 to 4** set the four values on the page. Hidden or unused knobs are drawn greyed.

**Shortcuts**

- **HOME + OCT-** or **HOME + OCT+** switches focus to layer 1 or layer 2. HOME acts when you release it, and not at all after a chord. After 200 ms of holding HOME, the focused layer's OCT LED lights and the other flashes. Layer 2 is refused in SINGL mode.
- **SAVE** + OCT- copies what SAVE would save on this page; SAVE + OCT+ pastes it (the save page opens and asks first). SAVE acts when you release it. Holding HOME or SAVE for 200 ms shows a strip at the bottom of the screen naming what OCT- and OCT+ do.

**Lights.** Only the button for the current page is lit. While the sequencer or arpeggiator plays, its button (SEQ or ARP) blinks on the beat, lit for the first half of each quarter note, even when its page is open. MIDI clock out (GLOBAL > CLOCK OUT) goes out on USB.

**Transport.** PLAY starts and stops the sequencer on the SEQ page and the arpeggiator on the ARP page. On any other page it starts both, or stops whichever is playing. The PLAY light shows what PLAY would stop. One started while the other plays waits for the other's next bar, so they stay in time. REC arms recording on the SEQ and ARP pages.

## Voices and layers

Carnac has eight physical voices shared by up to two layers. Each layer plays its own patch, with its own architecture, filters, envelopes and effects sends.

**Performance mode** (HOME, PERF page)

- **SINGL**: one layer, using every voice the architecture allows.
- **DUAL**: each layer has its own MIDI channel. The FM1's keys play the layer you have selected, so you hear the layer you are editing (both layers when the two share a channel). External MIDI reaches each layer on its channel.
- **SPLIT**: layer 1 and layer 2 share the keyboard. The ZONES page (HOME) sets, on its four knobs, SPLIT NOTE and SPLIT FADE (by note) and VEL SPLIT and VEL FADE (by velocity).

In DUAL and SPLIT the voices are shared out: layer 1 gets half rounded up, layer 2 half rounded down, based on the smaller of the two layers' caps. MAX VOICES on the LAYER page shows the result. A layer set to MONO RETRIG or MONO LEGATO needs only one voice, so the other layer gets the rest of the budget: with two SINGLE patches that is 1 + 7 instead of 4 + 4.

**Architecture** (ARCH on the LAYER page, for the focused layer) decides the signal path of every voice, and the most voices it may use.

| Architecture | Signal path | Max voices |
| --- | --- | --- |
| SINGLE | OSC1 as up to four detuned playheads (SPREAD) into FILTER1; at SPREAD 0, one plain oscillator | 8 |
| DUAL OSC | OSC1 and OSC2 with cross-modulation into FILTER1 | 6 |
| DUAL FILT | OSC1 into FILTER1 then FILTER2, serial to parallel | 7 |
| DUAL SPRD | OSC1 and OSC2, each as four detuned playheads, mixed by OSC MIX (no cross-modulation) into FILTER1 | 4 |
| FULL | DUAL OSC's oscillators into DUAL FILT's filters: OSC1 and OSC2 with cross-modulation into FILTER1 then FILTER2 | 4 |
| STACK | Two complete paths (OSC1 into FILTER1, OSC2 into FILTER2) blended | 6 |
| BINAURAL | OSC1 into FILTER1 once per ear, the right ear beat-offset | 6 |
| STRIKE | OSC1 into FILTER1, plus a short attack sound at the start of each note | 8 |

The caps keep the FM-1 within its processing budget. Carnac uses both of the FM-1's cores, and lightens the load by itself rather than drop out if the processor gets close to its limit.

The ARCH row steps through the architectures in the table's order. SINGLE is the one briefly called SPREAD: its SPREAD knob splits the oscillator into four detuned playheads, and at SPREAD 0 it is one plain oscillator. Patches saved with the older SINGLE load with SPREAD at 0 and sound as they did. FULL and DUAL SPRD are the heaviest paths, so they keep 4 voices; the cap only applies to the patches that use them.

**ANALOG** (ANALOG on the LAYER page, right after ARCH) makes the layer behave more like an analog synth. It is stored with the patch. Each voice gets its own small fixed tolerances: pitch, cutoff, resonance, envelope speed and level. Each note scatters them a little. From 40 up, slow drift eases in as well. At 0 the layer is exact.

**Voice mode** (VOICING on the LAYER page): MONO RETRIG, MONO LEGATO, POLY REUSE, POLY RESET or UNISON. In UNISON, UNISON DETUNE spreads the outer voices by up to 50 cents. Each layer has its own voice mode in SPLIT as well as DUAL, so a mono bass can sit under poly chords.

**Keys, SEQ and ARP on one layer.** When the keys, the sequencer and the arpeggiator share a layer and its voices run out, the keys come first, then the SEQ, then the ARP. A new note never takes a voice from a higher rank: an ARP note that finds every voice held by the SEQ or the keys is skipped. When the ARP or SEQ must give up a note, a middle note of its chord goes first, so the lowest and highest notes keep sounding. The keys still give up their oldest note. Each source releases only its own notes, so the ARP and the SEQ can play the same pitch without cutting each other off. On a MONO layer, the highest-ranked note held sounds, and a lower one waits until it is let go.

**Glide.** PORTA (the glide mode) is CONST RATE, CONST TIME or LEGATO TIED, and PORTA TIME (knob 4 on the LAYER page; 0 is no glide) sets how long it takes. In polyphonic modes the glide starts from the last note played on that layer. A chord (notes within 30 ms of each other) fans out from the note played before it.

**Layer focus.** The sound pages edit the focused layer. Switch with HOME + OCT- (layer 1) or HOME + OCT+ (layer 2). The screen uses one colour scheme for both layers, set by THEME.

## Oscillators and filters

### Oscillators (FX button)

OSC1 and OSC2 play a wave from the WAVE list: first the 15 wavetable banks (FOUNDATIONS, then HEARTH, BARROW, TRILITHON, LEY LINE, SOLSTICE, MOONSTONE, STAR CHART, DRUID CHANT, OVERTONE RITE, VOTIVE CHOIR, BONE FLUTE, EROSION, FROST SHATTER and DUST), then the PCM waves, then the 20 attack sounds as one-shots. OSC2 appears only in architectures that use it (DUAL OSC, STACK, FULL and DUAL SPRD).

**FOUNDATIONS** is bank 0, the classic analog shapes, drawn from a model of an SH-2-style oscillator core with its slewed edges and bent ramp. WAVE SCAN steps through TRIANGLE, SAW, SQUARE, PULSE 35 %, PULSE 20 %, PULSE 10 %, PULSE MIN (4.5 %) and SAW+SQUARE. MENHIR and HOLLOW REEDS have left the list, so a wavetable patch saved before 2026-10-26 may land on a different bank: set its WAVE again.

- **Knobs:** WAVE SCAN (position within the wavetable), OCTAVE (-3 to +3), FINE TUNE (up to 7 semitones up or down, finest near the centre, shown in cents), PITCH MOD (amount).
- **Rows:** WAVE, SCAN MOD SRC and AMT, PITCH MOD SRC, KEY TRACK, and on OSC2 OSC SYNC. In SINGLE and STRIKE, knob 3 is SCAN AMT (the scan's mod amount) and FINE TUNE is a row.

**PCM waves.** A PCM wave is a recorded sound: an optional attack, then loop A and an optional loop B. WAVE SCAN crossfades from loop A to loop B, and the attack plays again on every new note. The SPREAD knob (SINGLE, DUAL SPRD) works on PCM waves too, with four detuned playheads per loop. FM cross-modulation works on them; PM and OSC SYNC do nothing.

| PCM wave | Attack | SCAN goes from … to … |
| --- | --- | --- |
| WOOD CHIMES | a brush sweep | sparse to dense |
| RAINSTICK | SEED RATTLE | a trickle to a pour |
| LORE | none | still, soft cane-chime knocks to a denser gust |
| MOONSONG | none | a low humming choir to overtone singers' halo |
| CHOIR | a breath | hummed "ooh" to a ringing "aah" |
| HORNS | HORN BLAT | soft to overblown and brassy |
| CHOIR+WOOD | none | the choir's "aah" to the wood chimes |
| SINGING STN | LITHOPHONE | a stone rubbed softly to rubbed hard |
| PAN FLUTE | BONE CHIFF | breathy to full |
| CAIRN SPIRIT | a breath | a dark "oh" choir and flutes over a stone drone to a bright "aah" with a halo and shimmer |

The PCM waves and the FOUNDATIONS bank live in flash beside the firmware; a few PCM waves are built into the firmware itself. The installers write them after the firmware and rewrite only what changed. Without them, the PCM waves that need them stay silent and FOUNDATIONS plays a sine.

**OSCMIX** mixes the oscillators, the sub oscillator and the noise. Its four knobs change with the architecture:

| Architecture | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| SINGLE | SPREAD | NOISE | SUB LEVEL | SPREAD MOD |
| DUAL OSC | OSC MIX | CROSS-MOD | SUB LEVEL | NOISE |
| DUAL FILT | not used | not used | SUB LEVEL | NOISE |
| DUAL SPRD | OSC MIX | SPREAD | SUB LEVEL | NOISE |
| FULL | OSC MIX | CROSS-MOD | SUB LEVEL | NOISE |
| STACK | XMOD TYPE | CROSS-MOD | SUB LEVEL | NOISE |
| BINAURAL | OSC PHASE | BEAT HZ | SUB LEVEL | NOISE |
| STRIKE | ATK LEVEL | NOISE COLOUR | SUB LEVEL | NOISE |

Cross-modulation types are FM, AM, RING and PM. The sub oscillator sits one or two octaves down (SUB OCTAVE) and is a SINE, a SQUARE, or CURRENT, a copy of OSC1's live wavetable frame. Noise colour morphs from white (0) through pink (64) to brown (127). Like the classic polysynths, Carnac has one noise source that every voice shares (BINAURAL's right ear has its own), so noise in a chord sounds like one breath. Each voice still sets its own noise level and colour, and pink or brown noise now costs far less CPU. In BINAURAL, BEAT HZ offsets the right ear's pitch by up to 15.9 Hz, in 1/8 Hz steps.

In STRIKE, each new note starts with a short attack sound layered over the wavetable, the way an LA synth puts a sampled attack in front of its tone. A note played legato over a held one keeps the running attack. STRIKE adds an ATTACK page to the FX section, between OSC1 and OSCMIX; its screen shows the sound's outline, dim at full length and bright as it will play.

| Control | Where | What it does |
| --- | --- | --- |
| DECAY | knob 1 | 127 plays the whole sound; lower fades it out sooner (64 is about 94 ms, 20 about 10 ms) |
| TO FILTER | knob 2 | 0 keeps the attack out of the filter and ENV1, so it is heard even under a slow pad; 127 sends it through FILTER1 with the oscillator, where the filter and its envelope shape it, but then ENV1 shapes it too; values between blend the two |
| BEND | knob 3 | the attack starts off pitch and glides to it in about 40 ms: + starts above (a drum's drop), - below (a horn's scoop); 2 steps a semitone, so +24 is an octave |
| BEND MOD | knob 4 | how far BEND MOD SRC moves the bend, + or -; a full source at +24 adds an octave (with VELOCITY, harder notes bend further) |
| SOUND | row (first) | one of the 20 attack sounds below |
| START | row | skips into the sound, up to halfway (to lose a click or a breath) |
| TUNE | row | up to 24 semitones up or down |
| VELOCITY | row | 127 follows velocity like the voice; 0 plays the attack at the same level at any velocity |
| KEY TRACK | row | AUTO: pitched sounds follow OSC1's pitch, the percussion ones keep their own; FIXED: never follows; TRACK: always follows (a percussion sound sits at its own pitch on C4) |
| BEND MOD SRC | row (last) | the mod source for BEND MOD (default VELOCITY); it is read live while the bend glides, so an LFO or envelope works too |
| LEVEL | OSCMIX knob 1 | the attack's loudness |

| Group | Attack sounds | Follow the keys |
| --- | --- | --- |
| Breath | BONE CHIFF, REED CHIFF, HORN BLAT, CONCH | yes |
| Stone | FLINT, STONE KNOCK, MENHIR THUD, PEBBLES | no |
| Stone | LITHOPHONE | yes |
| Wood & Bone | WOOD BLOCK, LOG DRUM | yes |
| Wood & Bone | BONE CLACK, ANTLER RASP | no |
| String | BOW PLUCK, MOUTH BOW | yes |
| Hide & Fire | FRAME DRUM, POT DRUM | yes |
| Hide & Fire | SEED RATTLE, CRACKLE, CAVE CLAP | no |

### Filters (SEL button)

FILTER1 and FILTER2 share the same controls. FILTER2 appears in DUAL FILT and STACK.

- **Knobs:** TYPE MORPH (labelled by what it does in the circuit: LP-BP-HP, MODE or SPREAD), CUTOFF, RESONANCE, MOD AMOUNT (0 to 127, positive only: how far the FILTER page's MOD SRC opens the cutoff; for a downward sweep use an inverted ENV2 or ENV3, or a mod slot with a negative AMOUNT).
- **Smoothing:** CUTOFF, RESONANCE and TYPE MORPH (and OSC SCAN) glide to a new knob value over about 25 ms, so a slow turn sweeps smoothly. Loading a patch or performance jumps straight to its values.
- **DRIVE** (the first row) sets how hard the signal goes into the circuit, from clean at 0 to driven hard at 127. The level stays roughly even as it turns, so it adds grit rather than volume.
- **FLAVOUR** (the second row) picks the circuit, and TYPE MORPH means something different in each.

| Flavour | Character | TYPE MORPH |
| --- | --- | --- |
| SH-2 | Roland-style four-pole OTA filter: clean at DRIVE 0, a resonance that rings up softly and self-oscillates in tune up to the top of the cutoff | Low-pass through band-pass to high-pass |
| MULTI | Four-pole ladder multimode | LP24, LP18, LP12, LP6, BP, HP12, HP24, notch |
| MS-20 | MS-20 style two-pole filter with a growling resonance; DRIVE pushes it into its diodes | Low-pass through band-pass to high-pass |
| FORMANT | Vowel-like band filter | Band spread |

**KEY TRACK** (the third row, after DRIVE and FLAVOUR) moves the cutoff with the note played, pivoting at C4. At 0 the cutoff stays put; at 127 it follows the keyboard one octave per octave.

The filter mixer (FLTMIX) appears in DUAL FILT (BALANCE, ROUTING from serial to parallel), STACK (BLEND A/B, WIDTH, ENV3>CUTOFF) and BINAURAL (FLT PHASE, WIDTH, CUTOFF L/R, PHASE MOD).

## Modulation

Almost every destination in Carnac has a MOD SRC row (which source drives it) and an amount. Sources are: none, velocity, keyboard, mod wheel, aftertouch, ENV1 to ENV3, LFO1 to LFO3, MODSLOT1 to MODSLOT3, and CUSTOMCC1 and CUSTOMCC2. The two custom CC numbers are set on the GLOBAL page; the CC1 knob on the LAYER page sets CUSTOMCC1 directly, with no number needed.

**Envelopes (ENV button).** Knobs are ATTACK, DECAY, SUSTAIN and RELEASE. Extra rows: VEL > LEVEL (0 to 127: how much quieter a soft note makes the envelope, wherever it goes; on ENV1 it is the velocity-to-volume control, 111 by default, the old fixed curve of about -18 dB at the softest note), VEL > TIME and KEY > TIME (bipolar), RETRIG RATE, RETRIG SYNC, LEGATO, AUTO DECAY (a rapid repeat of a note shortens the release) and, on ENV2 and ENV3 only, INVERT. ENV1 is hardwired to the amplifier. An inverted envelope is drawn upside down in the display, as its destinations receive it: at rest it adds nothing, so the destination's knob sets where it sits, and with the note it pushes below that point, by as much as the envelope rises. As an AMP MOD or DEPTH MOD source it still reads full at rest and dips with the note (in source).

**LFOs (LFO button).** Knobs are SHAPE (sine through slewed random), RATE, DELAY (above 100 the LFO runs in release mode) and FADE (negative fades out, positive fades in). Extra rows: DEPTH MOD SRC and DEPTH MOD AMT (0 to 127: at 0 the LFO runs at full depth; at 127 the source alone sets the depth, so the mod wheel or aftertouch brings vibrato in from nothing; an LFO or a MOD slot cannot be the depth source), SPEED MOD SRC and AMT, CLOCK SYNC, PHASE and KEY RESET. LFO1 is shared by all voices; LFO2 and LFO3 run separately in each voice.

**Mod slots (EDIT button).** Three slots, MOD1 to MOD3, each combining two sources into a new one that other destinations can then use. The knobs are SOURCE A, FUNCTION, SOURCE B and AMOUNT. AMOUNT starts at 0, and at 0 the slot does nothing.

- **Functions:** A \* B, A + B, A - B, MAX, MIN, SAMPLE B ON A (B is held each time A crosses its middle), QUANTIZE B TO A (B sets how coarsely A is stepped), ABS(A - B), LAG (A slews, B sets how slowly) and DIODE (A - B with the negative half cut off).
- **The page:** the capstone shows the slot as an equation, such as LFO1 \* ENV1. Below it, the portal at the centre shows the slot's recent output as rings radiating from a glowing eye. Around it stands a ring of stones, one for every destination the architecture has, always in the same place. A ray runs from the eye to each stone this slot feeds, carrying the slot's signal outward: the more the destination gets, the wider the ray swings and the brighter and taller its stone. A stone fed by another source is an outline; one fed by nothing is a low stub. At AMOUNT 0 the portal goes dark and the rays stop.
- **Destinations:** the line under the capstone names the stone you are on, with its amount if this slot feeds it, or the source that feeds it now (OFF for none).
- **Editing destinations:** turn the Select Encoder to walk round the ring, and the Algorithm Encoder to set the selected stone's amount. Turning the amount of a stone this slot does not feed routes the slot to it, starting from 0, and replaces what fed it before (the popup shows the destination). You can still route from each destination's own page by setting its MOD SRC to MODSLOT1, 2 or 3.
- **With no note playing:** velocity, keyboard, mod wheel, aftertouch, the envelopes, LFO2 and LFO3 belong to a voice and read 0 when nothing sounds, so the portal only moves while a note plays. LFO1 keeps moving without one.

* Knobs: SOURCE A, FUNCTION, SOURCE B, AMOUNT.
* Functions: a\*b (with one source set, it passes that source), a+b, a-b, max, min, sample B on A, quantize B to A, |a-b|, lag and diode.

**Pitch bend and mod wheel.** The LAYER page's knobs are VELOCITY (the keys' velocity), MOD WHEEL, CC1 and PORTA TIME. Pitch bend comes in over MIDI only. BEND RANGE is on the GLOBAL page.

## Effects

**Amplifier (AMP page, GLO button).** Each layer's DRIVE (knob 1) comes after its amp envelope, so velocity and ENV1 play into it: a soft note drives less than a hard one. At 0 it is off. Turning it up adds grit while the level stays roughly even (the old drive got up to 7 dB louder). DRIVE MODEL picks its character:

| Model | Character | Gain at 127 |
| --- | --- | --- |
| SOFT | Smooth saturation, rounding into full scale | +30 dB |
| HARD | Clean up to about 3/4 of full scale, then a short bend into clipping | +30 dB |
| FOLD | The peaks fold back on themselves; the fold points are rounded | +16 dB |
| TAPE | Soft saturation with a slight bias (even harmonics); the treble falls away as it drives harder | +24 dB |
| TUBE | Uneven, so mainly the 2nd harmonic at low and middle settings; its bias moves with the level, so loud notes bloom. Pushed hard it turns square | +30 dB |
| OVERDRIVE | Like a Tube Screamer: only the part above about 720 Hz is clipped, the lows stay clean, and the tone rolls off at about 3.4 kHz | +40 dB |

The drive works on each voice, so a chord's notes do not distort each other.

There is one reverb and one delay, shared by both layers. Each layer feeds them through its own sends on the AMP page (DELAY SEND and REVERB SEND). The effects settings are on the EFFECTS page (GLO button) and belong to the performance.

**EFFECTS knobs:** DELAY TIME, DELAY FDBK, REVERB TIME, REVERB DAMP. Unsynced, DELAY TIME runs from about 1.5 ms to 1.0 s in steps of about 8 ms.

**EFFECTS rows:** DELAY TYPE, DELAY SYNC, DEGRADE, PING PONG, DELAY HP, then REVERB TYPE, REVERB PREDELAY: the delay's rows first, then the reverb's.

| Reverb type | Character |
| --- | --- |
| SPRING | Two dispersive springs, the classic drip |
| TOMB | Comb-network room (formerly ROOM) |
| MOD | Lush and chorused, inspired by Lexicon |
| CELESTIAL | Reverb with an octave-up pitch shift in the feedback, for floating ambient sounds |
| DOLMEN | A small stone chamber, like a cave |
| TUMULUS | A cavern, named for the Tumulus Saint-Michel at Carnac |

| Delay type | Character |
| --- | --- |
| TAPE | Tape echo that wobbles, saturates and wears |
| BBD | Bucket-brigade echo, dark and warm, with a breath of noise that follows the echo. Clean at every DELAY TIME (no bitcrushed grit at long times) |

- **DEGRADE** is one control for wow, flutter and filtering. It drives both the tape and the BBD delay.
- **PING PONG** bounces the echoes between left and right.
- **DELAY HP** puts a gentle high-pass filter in the delay's feedback loop, for a dub-style delay. The first echo keeps its bass, and each repeat after it loses a little more, so high FEEDBACK settings thin out instead of piling up low end. 0 is off (the delay as before); from 1 to 127 the cutoff sweeps from about 30 Hz to about 1 kHz, so the top of the range strips the repeats down to the mids. It works on both TAPE and BBD, and the delay still self-oscillates past FEEDBACK 100 (with BBD at DELAY HP 127, only at FEEDBACK 127).
- **DELAY SYNC** locks the delay time to the tempo: DELAY TIME then steps from 1/24 (fully left) through 1/16, 1/12, 1/8, 1/6, 1/4 and 1/3 to 1/2 (fully right), short to long as when unsynced.
- **Self-oscillation.** With FEEDBACK above 100, both delays build up on themselves, in a musical way. At the top of the range they run away to a dark, filtered howl, so turn it up slowly.

> Very loud sends are limited before they reach the reverb and delay, so playing several voices into a hot reverb should not buzz. If you hear digital buzz or a high whine after a note ends, report it with the reverb type and the patch.

## Sequencer and arpeggiator

### SEQ: the four-lane sequencer

SEQ has four lanes of 16 steps. Each lane has its own event type, speed, direction and loop, so lanes of different lengths drift against each other. Press SEQ to open lane 1 with steps 1 to 4 on knobs 1 to 4; press SEQ again for the next lane (1, 2, 3, 4, then back to 1), keeping the same steps on the knobs. The keyboard's bottom 16 white keys are the lane's steps: the playhead lights brightest, the four steps on the knobs medium, and every other step that plays (inside the loop, not muted or skipped) dimly. Press a white key to put that step and the next three on the knobs (from step 13 on, steps 13 to 16). While the sequencer plays, the knob card of the step under the playhead is outlined, as on the ARP page.

**Lane rows** (Select Encoder, Algorithm Encoder):

| Row | What it sets |
| --- | --- |
| STEPS | Which four steps knobs 1 to 4 edit. The Select Encoder moves through steps 1–4, 5–8, 9–12 and 13–16, then on through the rows below in the footer's order, then back to steps 1–4. A window a step key left between groups moves to the next group (or back to the one it starts in) |
| PRESCALE | The footer's g value: MIDI clock ticks per step, 1 to 32. 6 is a sixteenth note, 12 an eighth, 24 a quarter |
| EVENT TYPE | Note, dynamics, transpose, timing, gate, ratchet, probability skip, probability mute, mod wheel, aftertouch, custom CC 1 and 2, program change (DUAL mode only) or layer |
| TARGET | What the lane acts on: layer 1, layer 2 or both for note-style events; one or more lanes for modifier events |
| SCALE and ROOT | NOTE lanes only, see below |
| PLAY MODE | FOr (forward), bAC (backward), PE1 and PE2 (pendulum), rnd (random) |

**Loops.** Hold OCT- and press one of the 16 white keys to set the lane's loop start. Hold OCT+ for the loop end.

**Step record.** Press REC on SEQ, then play keys. Each note is written into every NOTE lane at the record step. Keys struck while others are still held build a chord of up to four notes. With PLAY and REC both on, the record step re-fires at each lane's PRESCALE rate on the clock, so it keeps sounding rather than playing once.

**Scale mode.** On a NOTE lane, SCALE holds the lane to a scale: OFF, GENRE (follows the arpeggiator's genre bank), Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Harmonic minor, Major pentatonic, Minor pentatonic or Blues. ROOT sets the tonic. Turning a step's note knob moves through the scale's degrees; played notes are snapped to the scale as you enter them. Stored notes are never changed, so turning SCALE off gives your original notes back.

### ARP: the chord arpeggiator

ARP plays chord patterns of up to 8 steps. The lowest 13 keys are chord keys, and a chord key sounds the chord for its note from the current GENRE BANK. The next 8 white keys are step keys.

- **Play live:** hold a chord key.
- **Capture a step:** hold a step key and press a chord key. The chord and the four ARP knobs are stored in that step. While you hold a step key that has a chord, the four knobs show that step's values and turning one edits the step; let go of the last step key and the knobs return to their live values. Holding a rest or TIE step leaves the knobs as they are (in source).
- **Knobs:** RATE (how fast the step's notes play: the length of one rhythm cell, 1/2 down to 1/128, with triplets; the rhythm repeats until the step ends), OCTAVE (1 to 4), STYLE (hold, block, up, down, up-down, down-up, random, strum up, strum down) and RHYTHM (24 patterns). HOLD strikes the chord once and holds it; while following steps play the same chord it is not struck again, at any TIMING and octave range. With HOLD, RHYTHM becomes STRUM: NONE, UP or DOWN, with the notes RATE apart.
- **Function keys** (the black keys among the step keys): LATCH keeps a live arpeggio playing after you let go. TIE carries the previous chord on. CLEAR makes a rest. COPY copies a step to others. ACCENT plays the first strike at ACCENT VEL.
- **Rows:** LAYER (L1, L2 or BOTH in DUAL mode; new patterns start on L2, the SEQ's note lane on L1; in a mode without the chosen layer the ARP plays L1 and keeps the setting), TIMING (every step's length, 2 bars down to 1/32T), GATE (how long each strike sounds, for the whole pattern), SEQ LENGTH (1 to 8), TRANSPOSE MAQ, GENRE BANK and ACCENT VEL.
- **Record:** arm REC. Playing, a chord key records into the nearest step. Stopped, chords fill steps 1, 2, 3 in turn.
- **TRANSPOSE MAQ** makes the sequencer's NOTE lanes follow each step's chord root and the chord key you press, so the arp and the sequencer stay in key together.

The GENRE BANK holds dozens of chord sets, from Pop, Jazz, Blues and House to Synthwave, Cinematic, EDM, Gospel, Lo-fi, Funk and Neo Soul.

## Presets and saving

Carnac stores four kinds of thing, each in 128 slots:

| Kind | What it holds |
| --- | --- |
| PATCH | One layer's sound |
| SEQUENCE | The four sequencer lanes |
| ARP | An arpeggiator pattern |
| PERFORMANCE | A complete performance: the mode, global settings, both patches, and the sequencer and arp patterns it links to |

**Loading.** Turn the Preset Encoder. It loads the kind that fits the page you are on: a patch on the sound pages, a performance on HOME, an arp pattern on ARP, a sequencer slot on SEQ. The preset list covers the whole screen and closes two seconds after the last click.

**Links.** HOME's PERF page, the performance browser, shows which patch, sequencer and arp slots the current performance uses: L1 PATCH, L2 PATCH, SEQ LINK and ARP LINK (its knobs), with PERF PRESET and PERF MODE as rows. Its dolmen draws the performance as a capstone on four pillars, one per part, cut from the same stone as the knob tablets above it; a pillar is thicker the more its part holds, an outline while it is still factory-fresh, and the capstone thickens with the whole. Turning a link, by its knob or its row, loads the next stored patch, sequence or arp pattern straight away, skipping empty slots; PERF PRESET loads the next performance the same way. While a slot row turns, the popup shows the slot's number with its name under it.

**Saving.**

1. Press SAVE. The save page opens on what the page you were on saves (a sound page: the focused layer's patch; HOME: the performance; SEQ; ARP) at the slot it came from. The header says it, e.g. SAVE L2 PATCH.
2. Pick the slot with the Algorithm Encoder or knob 2. The bottom of the screen shows the whole bank: a mark for each used slot, your slot as a standing stone, and the moon above it, full over an empty slot and a dark crescent when you would overwrite. The line under the name says EMPTY or OVERWRITES and the name there.
3. To rename, turn knobs 3 (position) and 4 (character); the letter keyboard shows while they turn. Patches, performances, sequences and arp patterns all take a name (sequence and arp names since 2026-10-25). A loaded sequence or arp keeps its slot's name, a performance brings the names of the sequence and arp slots it links, and the factory ones are named after their performance. Names are up to 16 characters from letters, digits, space and . - \_ /.
4. Press SAVE. An empty slot is written at once; a used one asks OVERWRITE? first (NO or YES with the Algorithm Encoder, then SAVE).
5. The screen reports SAVED NAME TO SLOT N, and the next control takes you back to the page you came from.

**Copy and paste.** Hold SAVE and press OCT- to copy what SAVE would save on the current page (the focused layer's patch, the performance, the sequence or the arp pattern). Hold SAVE and press OCT+ to paste it over the same kind, for example layer 1's patch onto layer 2: the save page opens and asks PASTE? first. While SAVE is held, a strip names both, with PASTE dimmed when the clipboard holds another kind.

**Factory bank: SAMHAIN.** Performance slots 0 to 7 hold 8 factory performances, each with its own patch, effects, tempo, sequence and arp pattern. Patch slots 0 to 31 hold 32 factory patches in four groups of eight: basses, leads, mystical sounds and percussion, most of them built on FOUNDATIONS through the SH-2 and MS-20, with a dub and pagan bent. Power-on always plays performance 0, SAMHAIN. A slot you have saved over shows your version in the browser. Every patch gives the MOD WHEEL (HOME knob 3) a job. PLAY on an ARP-led performance plays its chord progression with a bass under it; on a SEQ-led one, its sequence (installed).

| Perf | Name | Led by | What plays |
| --- | --- | --- | --- |
| 0 | SAMHAIN | SEQ | SPLIT: DUB BARROW below C4, CAIRN SPIRIT from C4, with a dub bass line |
| 1 | SIDHE DOOR | ARP | RUNE PLUCK arpeggiating a minor-ninth progression into the echoes |
| 2 | WILD HUNT | SEQ | a galloping Phrygian line |
| 3 | BOG SQUELCH | SEQ | a sliding acid line, Phrygian |
| 4 | CAIRN RITE | SEQ | the BODHRAN pattern, pitch as the drum |
| 5 | DUB OFFERING | SEQ | HENGE STAB on the off-beats, the echoes dubbed out |
| 6 | ALL SOULS | ARP | the choir in slow block chords |
| 7 | WILL O WISP | SEQ | a harmonic-minor melody |

| Patch | Name | Sound | MOD WHEEL |
| --- | --- | --- | --- |
| 0 | DUB BARROW | Round dub bass: SAW an octave down over a sine sub, warm SH-2; tied steps slide | synced eighth-note cutoff wobble |
| 1 | SIDHE SUB | Sub bass: TRIANGLE an octave down, SH-2 nearly closed | cutoff opens |
| 2 | BOG SQUELCH | Acid bass: SAW into a hard-driven SH-2 near the whistle, OVERDRIVE | envelope digs deeper |
| 3 | WIGHT GROWL | DUAL OSC SAW+SQUARE against a detuned SAW into a growling MS-20, TUBE | synced eighth-note wobble |
| 4 | CROM PULSE | PULSE 20 % with LFO1 moving the width, SH-2 | pulse narrows to a reedy nasal |
| 5 | UNDERHILL | DUAL OSC reggae bass: TRIANGLE with a SQUARE two octaves down, soft MS-20 | square comes up |
| 6 | PEAT THUMP | TRIANGLE falling into the note, driven SH-2: half bass, half drum | filter opens |
| 7 | TUMULUS DRONE | SINGLE: SAW+SQUARE as four playheads drifting apart, wandering MS-20, TAPE | MS-20 low-pass to band-pass |
| 8 | WILL O WISP | PULSE 20 % through a whistling SH-2, blooming vibrato, slow glide; mono | pulse thins |
| 9 | BANSHEE | SAW+SQUARE into a resonant MS-20 band-pass, slow swell, TUBE; mono | band-pass to high-pass |
| 10 | HAG SYNC | DUAL OSC sync tear, OSC2 thrown up and falling, MS-20, OVERDRIVE | OSC2 rises further |
| 11 | GHOST LIGHT | Theremin: TRIANGLE an octave up, open SH-2, long glide | vibrato |
| 12 | PUCA PIPE | STRIKE: REED CHIFF over a hollow SQUARE, breath, delayed vibrato | more breath |
| 13 | WILD HUNT | UNISON SAWs through a driven SH-2, TUBE | filter opens |
| 14 | BONE WHISTLE | STRIKE: BONE CHIFF over PULSE MIN through a resonant SH-2 band-pass | band-pass to high-pass |
| 15 | SPIRIT SIREN | Dub siren: SQUARE through an SH-2, made for the delay | LFO2 sweeps the pitch, wider and wider |
| 16 | NEWGRANGE DAWN | STACK: an FM bell over the PCM choir, filters opening over 3 s, CELESTIAL | vibrato, choir brighter |
| 17 | THIN VEIL | DUAL SPRD SAW and PULSE 35 %, soft MS-20 swelling | MS-20 toward band-pass, the pad turns to voices |
| 18 | CRYPT ORGAN | DUAL OSC SQUAREs at 8' and 4', MS-20, TUBE, slow rotor pan | the 4' comes forward |
| 19 | CORPSE CANDLE | DUAL OSC: a TRIANGLE ring-modulated by a bell, an iron toll settling | more ring |
| 20 | ALL SOULS | SINGLE: PCM CHOIR, its SPREAD playheads wide, warm SH-2 | choir opens, "ooh" to "aah" |
| 21 | OTHERWORLD | STACK: PCM MOONSONG hum under a PULSE 10 % through a moving MS-20 band-pass | overtone halo rises |
| 22 | CAIRN SPIRIT | SINGLE: PCM CAIRN SPIRIT, SPREAD wide, drifting between dark and bright over 10 s | choir opens to the shimmer |
| 23 | RUNE PLUCK | PULSE 10 % plucked through a snapping SH-2; higher notes shorter | pluck opens |
| 24 | CAIRN KICK | TRIANGLE falling two octaves in a blink, driven SH-2, TUBE | harder beater |
| 25 | BONE SNARE | STRIKE: BONE CLACK over a dropping TRIANGLE and noise, MS-20 band-pass | noise darkens |
| 26 | BODHRAN | STRIKE: FRAME DRUM tracked to the keys over a TRIANGLE body | skin bends in from below |
| 27 | SEED RATTLE | STRIKE: SEED RATTLE over PEBBLES and noise, MS-20 high-pass | fuller shake |
| 28 | HENGE STAB | DUAL OSC SAW and SQUARE a fifth apart, short MS-20 band-pass into the delay | stab thins for the echoes |
| 29 | LITHIC CHIME | STRIKE: LITHOPHONE over the PCM SINGING STN, soft SH-2 | stone struck harder |
| 30 | ANTLER CLAV | PULSE MIN through a snapping MS-20 band-pass, HARD clip | band-pass to low-pass |
| 31 | SPIRIT ZAP | Dub laser: SQUARE falling two and a half octaves through a ringing SH-2 | low-pass to band-pass |

## Display and themes

The screen is mostly black, so the panel gives off little light when played outdoors at night. Text is set in a rune-style face, and every popup covers the whole screen. Pages are drawn with lines, lit stones and sparse fog rather than filled textures.

- **Page header.** The current section's pages are listed at the right, with the current one lit and underlined. The layer and architecture are at the left.
- **Page menu.** Each page lists its rows four at a time in large runes, the label at the left and the value at the right; the knob dials below are larger, with the value in each dial's gap. Long names are shortened on screen, for example SPD MOD SRC (SPEED MOD SRC), DRV MODEL, REV TYPE, REV PREDELAY and CLOCK SRC, and mod sources show as velo, keyb, modwh, aftch, mod1 to mod3, cc1 and cc2. The LAYER page's rows are ARCH, ANALOG, VOICING, UNISON DETUNE, PORTA, PORTA TIME and MAX VOICES; the header names the layer.
- **Battery moon.** The moon at the top right of every page shows the battery. It waxes with the charge: a dark new moon when empty, a full moon when full. Below a third of a charge it is lit pale. On a computer's USB power it is full with eight stars around it. Until the first reading it hangs full and unlit.
- **Parameter popup (shown only while the Algorithm Encoder turns a value; the Select Encoder moves through the page's own list).** Each row is a standing stone, with the cursor's stone lit. The value is a moon on an arch over the stones: the left horizon is the minimum, overhead is the middle (zero for bipolar values) and the right horizon is the maximum. The moon waxes on the way up and wanes on the way down.
- **Preset list.** The slot picked is marked by brightness.
- **SEQ and ARP** use the same colours as every other page and do not change with the layer.

**THEME** (HOME, GLOBAL page) sets the screen colour. It belongs to the instrument, so loading a preset never changes it. The CARNAC OS start-up screen is drawn in the theme you last chose.

| Theme | Colour |
| --- | --- |
| SAMHAIN | Orange (default) |
| BLOOD MOON | Red |
| SOLSTICE | Ice blue |
| LICHEN | Green |
| HEATHER | Violet |
| FULL MOON | Silver |
| GRANITE | Grey stone |
| OAK | Wood |

## System

**GLOBAL page** (the last page of HOME). Knobs: TUNE (MASTER TUNE, -50 to +50 cents), CURVE (VEL CURVE), CLOCK (CLOCK OUT) and TEMPO (INT TEMPO, greyed when following MIDI clock). Rows, the MIDI setup first: MIDI CH LAYER1 and LAYER2, CLOCK SRC (INTERNAL or MIDI), MASTER TUNE, BEND RANGE, CUSTOM CC1 and CC2, then VEL CURVE (SOFT, LINEAR or HARD, applied to incoming MIDI notes), BASE VELOCITY, CLOCK OUT (OFF or USB: while the clock is internal, MIDI clock, start and stop go out on USB; the TRS jack only receives), INT TEMPO and THEME. INT TEMPO and BASE VELOCITY belong to the performance; every other GLOBAL setting belongs to the instrument: it survives power-off and stays as you set it when you load a performance. A DUAL performance that plays both layers on one channel stays that way, on your MIDI CH LAYER1 channel, and changing LAYER1 there moves layer 2 with it. The sequencer and arp follow the clock, internal or MIDI, and changing page never disturbs it.

**Processing load.** Carnac runs on both of the FM-1's cores. The sound is the same on one core or two. If the FM-1 gets close to its limit, Carnac lightens the load by itself rather than drop out, and the voice caps in Voices and layers keep the heaviest architectures inside the budget. On the device, eight plain SINGLE voices use about 43 percent of the processor. The heaviest combinations (SH-2 filters in DUAL FILT, STACK or BINAURAL, or heavy patches with the sequencer and arpeggiator running) reach 75 to 85 percent, where Carnac may lighten the load.

**USB audio.** Connected to a computer, the FM-1 is also a USB audio input: 16-bit stereo at 44.1 kHz, class compliant, so no driver is needed. It shows up as "Line (X0X FM-1)" and carries the same output as the jacks, after the MASTER knob. Record it in any audio app.

**Installing and recovering.** Install Carnac from the web installer at [owenkirby.github.io/carnac-os](https://owenkirby.github.io/carnac-os/), in Chrome or Edge, with the FM-1 on USB. It writes the firmware, checks the version, then writes the PCM waves. If an install stops part-way, replug the cable and start again: the FM-1 stays in update mode and the installer carries on. If the FM-1 does not start after an update, switch it off and on twice; it then starts in safe mode, which keeps USB working. The same page can put M-VAVE's stock firmware back. Deeper recovery steps are in the source repository (docs/fm1-recovery.md).

## In the works

These features are in active development for future releases:

- **MIDI Program Change**: receive program change messages to load patches and performances from external MIDI controllers and sequencers.
- **Full CC implementation**: comprehensive MIDI CC support with the ability to map any CC number to any parameter, and record CC changes into sequences.
- **Voice optimization**: further refinements to voice allocation and voice-stealing algorithms for smoother, more musical note handling in dense polyphonic situations.
- **MFX (Multi-Effects)**: additional built-in effects and modulation processing pathways under development.

## Changelog

Newest first. Features marked *in source* are written but not yet installed on the FM-1.

| Date | Change |
| --- | --- |
| 2026-10-27 | The PERF page's dolmen is now drawn in the knob tablets' stone colour instead of the theme colour (an empty part's pillar is an outline in the stone's lightest tone) *(in source)*. |
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
