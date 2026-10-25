#include "preset.h"
#include "maq_seq.h"                 /* MAQ_GATE_DEFAULT */
#include "hardware/mem_section.h"
#include "hardware/synth_libc.h"

EngineState engine_state[NLAYER];
volatile uint8_t engine_load_count[NLAYER];
MasterPerformancePreset perf_state;
SequencerLane seq_lanes[SEQ_NLANE];
ArpState arp_state;
InstrumentSettings instrument = {64u, VELCURVE_LINEAR, CLKOUT_OFF};

/* GLOBAL > VEL CURVE on an incoming note-on's velocity 1..127: SOFT lifts the low end (2v - v^2/127), HARD presses it
 * down (v^2/127), LINEAR leaves it; never 0 (that would be a note-off) */
uint8_t instrument_velocity(uint8_t v)
{
    uint32_t q = (uint32_t)v * v / 127u;
    if (v == 0u || v > 127u)
        return v;
    if (instrument.vel_curve == VELCURVE_SOFT) q = 2u * v - q;
    else if (instrument.vel_curve != VELCURVE_HARD) q = v;
    return (uint8_t)(q < 1u ? 1u : q > 127u ? 127u : q);
}

/* GLOBAL > MASTER TUNE in cents, -50..+50 */
int32_t instrument_tune_cents(void)
{
    uint8_t t = instrument.master_tune;
    t = t < MASTER_TUNE_MIN ? (uint8_t)MASTER_TUNE_MIN : t > MASTER_TUNE_MAX ? (uint8_t)MASTER_TUNE_MAX : t;
    return (int32_t)t - 64;
}
/* 128 x 174 B = 22,272 B: in .pool on the FM1, out of the 96 KiB .data + .bss region (hardware/mem_section.h).
 * preset_defaults fills every slot at boot, so .pool's zero start never shows. */
EngineState patch_bank[PATCH_SLOTS] SYNTH_POOL;

/* spec defaults for one layer's patch. Bipolar fields are stored with 64 = 0. */
void engine_defaults(EngineState *e)
{
    memset(e, 0, sizeof(*e));

    for (unsigned i = 0; i < NOSC; i++) {
        OscParams *o = &e->osc[i];
        o->octave = BIPOLAR_CENTER;      /* 0 octaves */
        o->fine = BIPOLAR_CENTER;        /* 0 cents */
        o->scan_mod_src = MSRC_ENV3;
        o->pitch_mod_src = MSRC_LFO1;
        o->key_track = 127;              /* full 1V/oct */
    }

    e->oscmix.balance = BIPOLAR_CENTER;  /* 50/50 */
    e->oscmix.mix_mod_src = MSRC_KEYBOARD;
    e->oscmix.xmod_mod_src = MSRC_MODWHEEL;
    e->oscmix.spread_mod_src = MSRC_MODWHEEL;
    e->oscmix.sub_octave = SUBOCT_DOWN1;
    e->oscmix.sub_waveform = SUBWAVE_SINE;
    e->oscmix.noise_colour = NOISE_WHITE;
    e->oscmix.noise_mod_src = MSRC_MODWHEEL;
    e->oscmix.noise_mod_amt = BIPOLAR_CENTER;
    e->oscmix.bin_mod_phase = 64;        /* 180 deg: the ears' LFO mod alternates */
    e->oscmix.bin_beat = 32;             /* 4 Hz */
    e->oscmix.bin_scan_offset = BIPOLAR_CENTER;
    e->oscmix.noise_col_mod_src = MSRC_MODWHEEL;
    e->oscmix.noise_col_mod_amt = BIPOLAR_CENTER;
    e->oscmix.atk_sample = 0;            /* STRIKE: BONE CHIFF */
    e->oscmix.atk_level = ATK_LEVEL_DEFAULT;
    e->oscmix.atk_tune = BIPOLAR_CENTER;
    {
        static const uint8_t def[8] = ATK_V8_DEFAULTS;  /* unfiltered, whole, from the start, full velocity, AUTO, no bend */
        e->oscmix.atk_filter = def[0];
        e->oscmix.atk_decay = def[1];
        e->oscmix.atk_start = def[2];
        e->oscmix.atk_vel = def[3];
        e->oscmix.atk_track = def[4];
        e->oscmix.atk_bend = def[5];
        e->oscmix.atk_bend_mod_src = def[6];
        e->oscmix.atk_bend_mod_amt = def[7];
    }

    for (unsigned i = 0; i < NFILTER; i++) {
        e->filter[i].cutoff = 110;       /* open */
        e->filter[i].mod_amt = BIPOLAR_CENTER;
        e->filter[i].flavour = FLT_MULTI;
        e->filter[i].morph = 36;         /* LP12: as the SVF's lowpass played (FREC_VER_ENGINE 9) */
        e->filter[i].mod_src = MSRC_ENV2;
        e->filter[i].morph_mod_src = MSRC_LFO1;
        e->filter[i].morph_mod_amt = BIPOLAR_CENTER;
    }

    e->fltmix.balance = BIPOLAR_CENTER;
    e->fltmix.balance_mod_amt = BIPOLAR_CENTER;
    e->fltmix.routing_mod_amt = BIPOLAR_CENTER;
    e->fltmix.balance_mod_src = MSRC_ENV3;
    e->fltmix.routing_mod_src = MSRC_ENV3;
    e->fltmix.stack_width = 0;           /* A and B centred */
    e->fltmix.env3_cutoff = BIPOLAR_CENTER;
    e->fltmix.bin_mod_phase = 64;        /* 180 deg */
    e->fltmix.bin_width = 127;           /* full separation */
    e->fltmix.bin_cutoff_offset = BIPOLAR_CENTER;
    e->fltmix.bin_phase_mod_amt = BIPOLAR_CENTER;
    e->fltmix.bin_phase_mod_src = MSRC_MODWHEEL;
    e->fltmix.bin_cutoff_mod_src = MSRC_LFO1;
    e->fltmix.bin_cutoff_mod_amt = BIPOLAR_CENTER;

    for (unsigned i = 0; i < NENV; i++) {
        EnvParams *p = &e->env[i];
        p->decay = 32;
        p->sustain = 64;
        p->release = 32;
        p->vel_to_time = BIPOLAR_CENTER;
        p->key_to_time = BIPOLAR_CENTER;
    }

    for (unsigned i = 0; i < NLFO; i++) {
        LfoParams *l = &e->lfo[i];
        l->rate = 64;
        l->fade = BIPOLAR_CENTER;        /* no fade */
        l->speed_mod_src = MSRC_VELOCITY;
        l->speed_mod_amt = BIPOLAR_CENTER;
    }

    for (unsigned i = 0; i < NMODSLOT; i++) {
        e->mod[i].func = MFN_ADD;
        e->mod[i].amount = BIPOLAR_CENTER;
    }

    e->amp.pan_mod_src = MSRC_KEYBOARD;
    e->arch = VARCH_DUAL_OSC;            /* both oscillators into FILTER1: the old default path minus FILTER2 */
}

/* the factory sequencer slot: lane 1 plays C3 on every 1/16 to layer 1; lanes 2..4 shape it (GATE half, DYNAMICS
 * 100, RATCHET single), so each knob on the SEQ page does something audible from the start */
void seq_defaults(SequencerLane lanes[SEQ_NLANE])
{
    static const uint8_t ev[SEQ_NLANE] = {SEQEV_NOTE, SEQEV_GATE, SEQEV_DYNAMICS, SEQEV_RATCHET};
    static const uint8_t val[SEQ_NLANE] = {0, MAQ_GATE_DEFAULT, 100, 0};
    for (unsigned l = 0; l < SEQ_NLANE; l++) {
        SequencerLane *ln = &lanes[l];
        memset(ln, 0, sizeof(*ln));
        for (unsigned i = 0; i < SEQ_NSTEP; i++) {
            ln->steps[i].note_buffer[0] = 48;    /* C3 */
            ln->steps[i].note_buffer[1] = SEQ_NOTE_NULL;
            ln->steps[i].note_buffer[2] = SEQ_NOTE_NULL;
            ln->steps[i].note_buffer[3] = SEQ_NOTE_NULL;
            ln->steps[i].value = val[l];
        }
        ln->event_type = ev[l];
        ln->target_mask = l == 0 ? (uint8_t)SEQ_TGT_L1 : (uint8_t)SEQ_TGT_LANE1;
        ln->playback_mode = SEQ_MODE_FORWARD;
        ln->prescale = SEQ_PRESCALE_1_16;
        ln->loop_start = 0;
        ln->loop_end = SEQ_NSTEP - 1u;
        ln->scale = SEQSCALE_OFF;
        ln->scale_root = 0;
    }
}

void preset_defaults(void)
{
    live_defaults();
    for (unsigned s = 0; s < PATCH_SLOTS; s++)
        patch_bank[s] = engine_state[0];
}

void live_defaults(void)
{
    seq_defaults(seq_lanes);
    arp_defaults(&arp_state);
    for (unsigned l = 0; l < NLAYER; l++) {
        engine_defaults(&engine_state[l]);
        engine_load_count[l]++;
    }

    perf_defaults(&perf_state);
}

void perf_defaults(MasterPerformancePreset *p)
{
    memset(p, 0, sizeof(*p));
    p->layer_patch[0] = 0;
    p->layer_patch[1] = 1;
    p->perf_mode = PERF_SINGL_SYNTH;
    p->voice_mode[0] = VMODE_POLY_REUSE;
    p->voice_mode[1] = VMODE_POLY_REUSE;
    p->unison_detune[0] = 48;            /* +-7 cents at the outer voices */
    p->unison_detune[1] = 48;
    p->split_note = 60;
    p->vel_split = 127;                  /* every velocity is at or below it: layer 1 (no velocity split) */
    p->midi_ch[0] = 0;                   /* channel 1 */
    p->midi_ch[1] = 1;                   /* channel 2 */
    p->delay_degrade = 36;               /* tape echo character: the fixed wear before it was a parameter */
    p->delay_pingpong = 0;
    p->delay_type = DELAYTYPE_TAPE;
    p->reverb_type = REVTYPE_TOMB;       /* the old default, PLATE, played the ROOM network */
    p->base_velocity = 69;
    p->bend_range = 2;
    p->int_tempo = 80;                   /* 120 BPM */
}

void patch_store(uint8_t layer, uint8_t slot)
{
    if (layer >= NLAYER || slot >= PATCH_SLOTS) return;
    patch_bank[slot] = engine_state[layer];
}

void patch_load(uint8_t layer, uint8_t slot)
{
    if (layer >= NLAYER || slot >= PATCH_SLOTS) return;
    engine_state[layer] = patch_bank[slot];
    engine_load_count[layer]++;
    perf_state.layer_patch[layer] = slot;
}
