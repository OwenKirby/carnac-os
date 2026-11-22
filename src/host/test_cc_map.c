/* Host test: the CC map's step 2 (2026-11-18): incoming Control Changes through the runtime's queue (synth_cc_take)
 * and the panel's map (ui_sm_cc), as synth_panel.c's syn_perf_tick drains them: the layer by channel, the
 * performance's controllers on Layer 1's channel only, lists in bands, WAVE over the wave list, TUNE in semitones, the
 * MFX's knobs by position, OSC2's controllers idle without OSC2, a fast sweep coalesced. Exit code 0 = pass.
 *
 *   gcc -O2 -Isrc -Isrc/dsp -Isrc/hardware $(ls src/dsp/?*.c | grep -v engine.c | grep -v test_wavetables.c) \
 *       src/UIStateMachine.c src/host/test_cc_map.c -lm -o test_cc_map */
#include <stdio.h>
#include <string.h>
#include "UIStateMachine.h"
#include "dsp/midi_clock.h"
#include "preset.h"
#include "dsp/synth_runtime.h"
#include "dsp/midi_io.h"
#include "dsp/pcm_osc.h"
#include "dsp/fx.h"

extern MidiInterfaceState master_midi;
static UIStateMachine sm;
static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void drain(void)                              /* synth_panel.c syn_perf_tick's loop */
{
    uint8_t e[5];
    while (synth_cc_take(e)) {
        uint8_t ch = e[0] & 0x0Fu, k = e[0] >> 4, layers = 0u, perf;
        for (uint8_t l = 0; l < NLAYER; l++)
            if ((perf_state.midi_ch[l] & 0x0Fu) == ch && (l == 0u || perf_state.perf_mode != PERF_SINGL_SYNTH))
                layers |= (uint8_t)(1u << l);
        perf = (uint8_t)(ch == (perf_state.midi_ch[0] & 0x0Fu));
        if (k == SYNTH_CCK_CC)
            (void)ui_sm_cc(&sm, e[1], layers, perf, e[3]);
        else
            (void)ui_sm_nrpn(&sm, (uint8_t)(k == SYNTH_CCK_RPN), e[1], e[2], layers, perf,
                             (uint16_t)((e[3] & 0x7Fu) << 7 | (e[4] & 0x7Fu)));
    }
}

static void cc(uint8_t ch, uint8_t num, uint8_t val) { synth_midi_3((uint8_t)(0xB0u | ch), num, val); drain(); }

static void nrpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t v)
{
    synth_midi_3((uint8_t)(0xB0u | ch), 99, msb);
    synth_midi_3((uint8_t)(0xB0u | ch), 98, lsb);
    synth_midi_3((uint8_t)(0xB0u | ch), 6, v);
    drain();
}
/* a 14-bit NRPN / RPN value: DATA ENTRY MSB, then LSB (cc: 99 an NRPN, 101 an RPN) */
static void par14(uint8_t ch, uint8_t sel, uint8_t msb, uint8_t lsb, uint16_t v)
{
    synth_midi_3((uint8_t)(0xB0u | ch), sel, msb);
    synth_midi_3((uint8_t)(0xB0u | ch), (uint8_t)(sel - 1u), lsb);
    synth_midi_3((uint8_t)(0xB0u | ch), 6, (uint8_t)(v >> 7));
    synth_midi_3((uint8_t)(0xB0u | ch), 38, (uint8_t)(v & 0x7Fu));
    drain();
}
static void rpn(uint8_t ch, uint8_t msb, uint8_t lsb, uint8_t v)
{
    synth_midi_3((uint8_t)(0xB0u | ch), 101, msb);
    synth_midi_3((uint8_t)(0xB0u | ch), 100, lsb);
    synth_midi_3((uint8_t)(0xB0u | ch), 6, v);
    drain();
}

static void setup(uint8_t mode, uint8_t ch1, uint8_t ch2, uint8_t arch)
{
    synth_init();
    perf_state.perf_mode = mode;
    perf_state.midi_ch[0] = ch1;
    perf_state.midi_ch[1] = ch2;
    perf_state.cc1_num = perf_state.cc2_num = 0;
    engine_state[0].arch = engine_state[1].arch = arch;
    engine_state[0].mfx.type = engine_state[1].mfx.type = MFX_OFF;
    ui_sm_init(&sm, &perf_state, &engine_state[0], &engine_state[1], seq_lanes, &arp_state);
}

int main(void)
{
    /* SINGL on channel 1: the GM2 sound controllers, the layer's own */
    setup(PERF_SINGL_SYNTH, 0, 1, VARCH_DUAL_OSC);
    cc(0, 74, 100);
    cc(0, 71, 33);
    cc(0, 73, 5);
    CHECK(engine_state[0].filter[0].cutoff == 100 && engine_state[0].filter[0].resonance == 33 &&
          engine_state[0].env[0].attack == 5, "CC 74 / 71 / 73: CUTOFF, RESONANCE, ATTACK");
    {
        uint8_t keep = engine_state[1].filter[0].cutoff;
        cc(1, 74, 7);                                /* SINGL: Layer 2's channel plays nothing */
        CHECK(engine_state[0].filter[0].cutoff == 100 && engine_state[1].filter[0].cutoff == keep,
              "CC 74 on Layer 2's channel moved something under SINGL");
    }

    /* lists in bands, WAVE over the list, TUNE in semitones, a switch */
    cc(0, 90, 127);
    CHECK(engine_state[0].amp.drive_model == DRIVE_COUNT - 1, "DRIVE MODEL at 127: %u", engine_state[0].amp.drive_model);
    cc(0, 90, 0);
    CHECK(engine_state[0].amp.drive_model == 0, "DRIVE MODEL at 0");
    cc(0, 30, 0);
    CHECK(engine_state[0].osc[0].wavebank == wave_list_code(0), "WAVE at 0: the list's first");
    cc(0, 30, 127);
    CHECK(engine_state[0].osc[0].wavebank == wave_list_code(wave_list_count() - 1u), "WAVE at 127: the list's last");
    cc(0, 15, 64);
    CHECK(osc_coarse_semis(engine_state[0].osc[0].coarse) == 0, "TUNE at 64: %d semitones",
          (int)osc_coarse_semis(engine_state[0].osc[0].coarse));
    cc(0, 15, 0);
    CHECK(osc_coarse_semis(engine_state[0].osc[0].coarse) == -OSC_COARSE_MAX, "TUNE at 0");
    cc(0, 15, 127);
    CHECK(osc_coarse_semis(engine_state[0].osc[0].coarse) == OSC_COARSE_MAX, "TUNE at 127");
    cc(0, 63, 100);
    CHECK(DELAY_PINGPONG(&perf_state) == 1, "PING PONG on at 100");
    cc(0, 63, 10);
    CHECK(DELAY_PINGPONG(&perf_state) == 0, "PING PONG off at 10");

    /* the performance's: EFFECTS and MIX on Layer 1's channel */
    cc(0, 12, 77);
    cc(0, 80, 20);
    CHECK(perf_state.delay_time == 77 && fx_live[FX_L_BALANCE] == 20, "CC 12 / 80: DELAY TIME, DRY/WET");

    /* DUAL, the layers on channels 1 and 2: each its own; the performance's only on Layer 1's */
    setup(PERF_DUAL_SYNTH, 0, 1, VARCH_DUAL_OSC);
    cc(1, 74, 11);
    CHECK(engine_state[1].filter[0].cutoff == 11 && engine_state[0].filter[0].cutoff != 11, "CC 74 on Layer 2's channel");
    cc(1, 3, 90);
    CHECK(engine_state[1].analog == 90, "CC 3 (ANALOG) on Layer 2's channel");
    {
        uint8_t keep = perf_state.delay_time;
        cc(1, 12, (uint8_t)(keep ^ 1u));
        CHECK(perf_state.delay_time == keep, "CC 12 (a performance controller) moved from Layer 2's channel");
    }
    /* DUAL stacked on one channel: both move (the user's rule) */
    setup(PERF_DUAL_SYNTH, 0, 0, VARCH_DUAL_OSC);
    cc(0, 74, 55);
    CHECK(engine_state[0].filter[0].cutoff == 55 && engine_state[1].filter[0].cutoff == 55, "stacked: CC 74 moved both");

    /* without OSC2 its controllers do nothing (its place is the NOISE page) */
    setup(PERF_SINGL_SYNTH, 0, 1, VARCH_SPREAD);
    {
        EngineState keep = engine_state[0];
        cc(0, 20, 3);
        cc(0, 21, 3);
        cc(0, 31, 3);
        CHECK(!memcmp(&keep, &engine_state[0], sizeof keep), "OSC2's controllers moved something without OSC2");
    }

    /* the noise in every architecture: its own five controllers, whichever knob or row shows it */
    for (uint8_t a = 0; a < VARCH_COUNT; a++) {
        if (a == VARCH_DUAL_SPREAD) continue;        /* (retired: loads as DUAL OSC) */
        setup(PERF_SINGL_SYNTH, 0, 1, a);
        cc(0, 115, 101);
        cc(0, 116, 102);
        cc(0, 117, 103);
        cc(0, 118, 104);
        cc(0, 119, 105);
        const OscMixerParams *m = &engine_state[0].oscmix;
        /* one-oscillator archs (2026-11-22): CC 115 is OSC/NOISE, the balance, and the noise level has no control */
        int single = a == VARCH_SINGLE || a == VARCH_SPREAD;
        CHECK((single ? m->balance == 101 && m->noise_level == 0 : m->noise_level == 101) && m->noise_colour == 102 &&
              engine_state[0].noise_harmonic == 103 && m->noise_col_mod_amt == 104 && m->noise_mod_amt == 105,
              "arch %u: the noise's CC 115..119", a);
    }
    setup(PERF_SINGL_SYNTH, 0, 1, VARCH_DUAL_OSC);
    cc(0, 95, 127);
    CHECK(perf_state.reverb_type == REVTYPE_COUNT - 1, "CC 95: REVERB TYPE at 127");

    /* the MFX: its type in bands, its knobs by position */
    setup(PERF_SINGL_SYNTH, 0, 1, VARCH_DUAL_OSC);
    cc(0, 89, (uint8_t)(MFX_CHORUS * 128u / MFX_OFFERED + 1u));
    CHECK(engine_state[0].mfx.type == MFX_CHORUS, "MFX TYPE: %u", engine_state[0].mfx.type);
    cc(0, 85, 99);
    CHECK(engine_state[0].mfx.knob[MFX_K_CH_RATE] == 99, "MFX knob 1 (CHORUS RATE)");

    /* a sweep faster than the panel takes it: the last value, nothing lost */
    for (uint32_t i = 0; i < 200u; i++)
        synth_midi_3(0xB0, 74, (uint8_t)(i & 0x7Fu));
    synth_midi_3(0xB0, 71, 9);
    drain();
    CHECK(engine_state[0].filter[0].cutoff == (199u & 0x7Fu) && engine_state[0].filter[0].resonance == 9,
          "a fast sweep: CUTOFF %u, RESONANCE %u", engine_state[0].filter[0].cutoff, engine_state[0].filter[0].resonance);

    /* the wheel still reaches the mod source; an unmapped controller moves nothing */
    cc(0, 1, 70);
    CHECK(master_midi.mod_wheel_val == 70, "CC 1 lost the wheel");
    {
        EngineState k0 = engine_state[0];
        MasterPerformancePreset kp = perf_state;
        cc(0, 84, 50);
        cc(0, 93, 50);
        CHECK(!memcmp(&k0, &engine_state[0], sizeof k0) && !memcmp(&kp, &perf_state, sizeof kp),
              "an unmapped controller moved something");
    }

    /* NRPN: a panel control by its place, the value exact; routes; RPN 0 / 1 */
    setup(PERF_SINGL_SYNTH, 0, 1, VARCH_DUAL_OSC);
    nrpn(0, UIPAGE_FILTER1, 4 + 1, FLT_MS10);       /* FILTER1's FLAVOUR row: exact, no CC of its own */
    CHECK(engine_state[0].filter[0].flavour == FLT_MS10, "NRPN FILTER1 FLAVOUR: %u", engine_state[0].filter[0].flavour);
    nrpn(0, UIPAGE_FILTER1, 4 + 1, 100);            /* (past the list: its last) */
    CHECK(engine_state[0].filter[0].flavour == FLT_COUNT - 1, "NRPN FLAVOUR held to the list");
    nrpn(0, UIPAGE_AMP, 4, DRIVE_TUBE);
    CHECK(engine_state[0].amp.drive_model == DRIVE_TUBE, "NRPN DRIVE MODEL exact");
    nrpn(0, UIPAGE_OSC1, 1, OSC_COARSE_ZERO + 7u);  /* TUNE: the coarse byte, +7 semitones */
    CHECK(osc_coarse_semis(engine_state[0].osc[0].coarse) == 7, "NRPN TUNE +7");
    nrpn(0, UIPAGE_OSC1, 4, 3);                     /* WAVE by its place in the list */
    CHECK(engine_state[0].osc[0].wavebank == wave_list_code(3), "NRPN WAVE: place 3");
    nrpn(0, UIPAGE_OSC2, 4 + 2, 0);                 /* FINE below its range: its floor */
    CHECK(engine_state[0].osc[1].fine == 64 - OSC_FINE_MAX, "NRPN FINE held to its range: %u", engine_state[0].osc[1].fine);
    nrpn(0, UIPAGE_AMP, 20, MFX_METAL);
    CHECK(engine_state[0].mfx.type == MFX_METAL, "NRPN MFX TYPE exact");
    nrpn(0, 64 + 2, 0, MSRC_LFO2);                  /* route 3: SOURCE, DEST, AMOUNT */
    nrpn(0, 64 + 2, 2, MDST_F1_CUTOFF);
    nrpn(0, 64 + 2, 3, 90);
    CHECK(engine_state[0].route[2].src == MSRC_LFO2 && engine_state[0].route[2].dest == MDST_F1_CUTOFF &&
          engine_state[0].route[2].amount == 90, "NRPN MOD route 3");
    nrpn(0, 64 + 2, 2, 127);
    CHECK(engine_state[0].route[2].dest == MDST_COUNT - 1, "NRPN DEST held to the list");
    nrpn(0, UIPAGE_HOME1, 4 + 0, 4);                /* ARCH by its place in the list (STACK); the voices follow */
    CHECK(engine_state[0].arch == VARCH_STACK, "NRPN ARCH: place 4 is STACK");
    nrpn(0, UIPAGE_HOME1, 4 + 0, 7);                /* past the list: its last, never a retired code (DUAL SPREAD) */
    CHECK(engine_state[0].arch == VARCH_BINAURAL, "NRPN ARCH held to the list: %u", engine_state[0].arch);
    nrpn(0, UIPAGE_HOME1, 4 + 0, 1);
    CHECK(engine_state[0].arch == VARCH_DUAL_OSC, "NRPN ARCH: place 1 is DUAL OSC");
    /* the MFX's knobs, type and rows: AMP's place only */
    {
        uint8_t keep = engine_state[0].mfx.type;
        nrpn(0, UIPAGE_OSC1, 20, MFX_CHORUS);       /* (an MFX place on another page: nothing) */
        nrpn(0, UIPAGE_EFFECTS, 20, MFX_CHORUS);
        CHECK(engine_state[0].mfx.type == keep, "an MFX NRPN off the AMP page moved the MFX");
    }
    nrpn(0, UIPAGE_AMP, 20, MFX_METAL);
    nrpn(0, UIPAGE_AMP, 21 + 11, 77);               /* METAL's LEVEL row (MFX row 12) */
    CHECK(engine_state[0].mfx.row[MFX_R_MT_LEVEL] == 77, "NRPN MFX row: METAL LEVEL %u", engine_state[0].mfx.row[MFX_R_MT_LEVEL]);
    {
        MfxParams k = engine_state[0].mfx;
        nrpn(0, UIPAGE_AMP, 21 + 6, 99);            /* AGE: CHORUS's, not METAL's: refused */
        nrpn(0, UIPAGE_AMP, 21 + 13, 99);           /* past the MFX rows */
        CHECK(!memcmp(&k, &engine_state[0].mfx, sizeof k), "an MFX row of another type moved something");
    }
    /* the tempo past 127: 14 bits (GLOBAL INT TEMPO row 12, TEMPO knob 4) */
    par14(0, 99, UIPAGE_GLOBAL, 4 + 12, 200);
    CHECK(perf_state.int_tempo == 200, "NRPN INT TEMPO 14-bit: %u", perf_state.int_tempo);
    par14(0, 99, UIPAGE_GLOBAL, 3, 180);
    CHECK(perf_state.int_tempo == 180, "NRPN TEMPO knob 14-bit: %u", perf_state.int_tempo);
    par14(0, 99, UIPAGE_GLOBAL, 3, 1000);
    CHECK(perf_state.int_tempo == MIDI_CLOCK_TEMPO_MAX, "NRPN TEMPO held to 255 BPM");
    nrpn(0, UIPAGE_GLOBAL, 4 + 12, 1);               /* an MSB alone: LSB 0 (128) */
    CHECK(perf_state.int_tempo == 128, "NRPN INT TEMPO MSB alone: %u", perf_state.int_tempo);
    nrpn(0, UIPAGE_GLOBAL, 3, 1);
    CHECK(perf_state.int_tempo == 128, "NRPN TEMPO knob MSB alone: %u", perf_state.int_tempo);
    nrpn(0, UIPAGE_EFFECTS, 4 + 3, REVTYPE_TOMB);
    CHECK(perf_state.reverb_type == REVTYPE_TOMB, "NRPN REVERB TYPE exact");
    {
        EngineState k0 = engine_state[0];
        nrpn(0, UIPAGE_SEQ, 0, 5);                  /* not a sound page: nothing */
        nrpn(0, UIPAGE_MOD, 0, 5);
        synth_midi_3(0xB0, 99, 127); synth_midi_3(0xB0, 98, 127); synth_midi_3(0xB0, 6, 9); drain();   /* null */
        CHECK(!memcmp(&k0, &engine_state[0], sizeof k0), "an NRPN outside the map moved something");
    }
    rpn(0, 0, 0, 12);
    CHECK(perf_state.bend_range == 12, "RPN 0: BEND RANGE");
    rpn(0, 0, 1, 127);                              /* GM: +100 cents; the FM1 stops at +50 */
    CHECK(ui_sm_instrument(&sm)->master_tune == MASTER_TUNE_MAX, "RPN 1: MASTER TUNE at +full: %u", ui_sm_instrument(&sm)->master_tune);
    rpn(0, 0, 1, 64);
    CHECK(ui_sm_instrument(&sm)->master_tune == 64, "RPN 1: MASTER TUNE centre");
    par14(0, 101, 0, 1, 8192 + 2048);               /* +25 cents */
    CHECK(ui_sm_instrument(&sm)->master_tune == 64 + 25, "RPN 1: MASTER TUNE +25 cents: %u", ui_sm_instrument(&sm)->master_tune);
    par14(0, 101, 0, 1, 8192 - 1024);               /* -12.5: toward zero */
    CHECK(ui_sm_instrument(&sm)->master_tune == 64 - 12, "RPN 1: MASTER TUNE -12 cents: %u", ui_sm_instrument(&sm)->master_tune);
    /* a fast run of NRPNs to different parameters: each lands (none merged into another) */
    for (uint8_t i = 0; i < 4u; i++) {
        synth_midi_3(0xB0, 99, 64 + 4); synth_midi_3(0xB0, 98, 3); synth_midi_3(0xB0, 6, (uint8_t)(10 + i));
        synth_midi_3(0xB0, 99, 64 + 5); synth_midi_3(0xB0, 98, 3); synth_midi_3(0xB0, 6, (uint8_t)(20 + i));
    }
    drain();
    CHECK(engine_state[0].route[4].amount == 13 && engine_state[0].route[5].amount == 23, "a fast run of NRPNs");

    /* CC OUT (step 4): every parameter read back (ui_sm_par_get) and sent in again changes nothing, as an NRPN and as
     * its CC; in every architecture, both layers apart; what came in is marked (not echoed) */
    {
        static const uint8_t archs[] = {VARCH_SPREAD, VARCH_DUAL_OSC, VARCH_DUAL_FILTER, VARCH_FULL, VARCH_STACK,
                                        VARCH_BINAURAL};
        uint16_t n = ui_sm_par_count(), live = 0u, with_cc = 0u;
        CHECK(n <= 640u, "parameters: %u, past CC OUT's 640", n);
        for (uint8_t a = 0; a < sizeof archs; a++) {
            setup(PERF_DUAL_SYNTH, 0, 1, archs[a]);
            engine_state[0].mfx.type = (uint8_t)(a % 2u ? MFX_CHORUS : MFX_METAL);
            for (uint16_t i = 0; i < n; i++) {
                UiPar q;
                EngineState e0 = engine_state[0], e1 = engine_state[1];
                MasterPerformancePreset pf = perf_state;
                InstrumentSettings in = *ui_sm_instrument(&sm);
                if (!ui_sm_par_get(&sm, i, &q))
                    continue;
                live++;
                CHECK(ui_sm_par_index(q.msb, q.lsb, q.layer) == i, "par %u: its place %u:%u back to %u", i, q.msb,
                      q.lsb, ui_sm_par_index(q.msb, q.lsb, q.layer));
                sm.par_in_n = 0u;
                (void)ui_sm_nrpn(&sm, 0u, q.msb, q.lsb, (uint8_t)(1u << q.layer), (uint8_t)(q.layer == 0u),
                                 (uint16_t)(q.wide ? q.v : (uint16_t)q.v << 7));
                CHECK(!memcmp(&e0, &engine_state[0], sizeof e0) && !memcmp(&e1, &engine_state[1], sizeof e1) &&
                      !memcmp(&pf, &perf_state, sizeof pf) && !memcmp(&in, ui_sm_instrument(&sm), sizeof in),
                      "arch %u: NRPN %u:%u layer %u value %u, read back, moved something", archs[a], q.msb, q.lsb,
                      q.layer, q.v);
                CHECK(sm.par_in_n == 1u && sm.par_in[0] == i, "NRPN %u:%u marked as come in", q.msb, q.lsb);
                if (q.cc != 0u) {
                    with_cc++;
                    (void)ui_sm_cc(&sm, q.cc, (uint8_t)(1u << q.layer), (uint8_t)(q.layer == 0u), q.ccv);
                    CHECK(!memcmp(&e0, &engine_state[0], sizeof e0) && !memcmp(&e1, &engine_state[1], sizeof e1) &&
                          !memcmp(&pf, &perf_state, sizeof pf),
                          "arch %u: CC %u value %u (%u:%u = %u), read back, moved something", archs[a], q.cc, q.ccv,
                          q.msb, q.lsb, q.v);
                }
                engine_state[0] = e0;
                engine_state[1] = e1;
                perf_state = pf;
            }
        }
        CHECK(live > 1000u && with_cc > 400u, "read back: %u parameters, %u with a CC", live, with_cc);
    }

    if (!fails)
        printf("CC map in: layers by channel, the performance's on Layer 1's, bands, WAVE, TUNE, MFX, sweeps; NRPN, routes, ARCH, MFX rows, 14-bit tempo, RPN; every parameter read back round-trips\n");
    return fails != 0;
}
