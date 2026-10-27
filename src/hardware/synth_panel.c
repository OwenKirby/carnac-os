/* SPDX-License-Identifier: GPL-3.0-only */
/* The synth's app on the FM1 that is not the hardware: the settings and the SAVE banks (with the preset store and the
 * factory bank), and the panel (buttons, encoders, knobs, keys, LEDs). A unity-build fragment, included by
 * src/hardware/synth_main_fm1.c (the device: after its audio ISR, before its editor) and by src/host/carnac_web.c (the
 * browser emulator, web/emu: the same code on a simulated FM1), so what the emulator does is what the panel does.
 *
 * It needs, before it: X0X's plat.h (plat_buttons / plat_keys / plat_enc / plat_leds ..), storage.c (st_load / st_save /
 * st_read / st_erase / st_prog / st_buf), fm1_ms, flash_ok, fm1_led_dim_div, syn_lock / syn_unlock, perf_test.h's
 * pt_state / PT_RUN, and every synth source (src/dsp, UIStateMachine.c). */
#include "perf_test.h"
#include "hardware/mem_section.h"             /* SYNTH_POOL */

/* ---------------------------------------------------------- the store --- */
/* The instrument's settings across a power cycle: the THEME (GLOBAL > THEME, UIStateMachine.theme; 2026-10-16),
 * GLOBAL's MASTER TUNE, VEL CURVE and CLOCK OUT (dsp/preset.c instrument; 2026-10-17), and since 2026-10-25 GLOBAL's
 * MIDI CH LAYER1/2, CUSTOM CC1/2, BEND RANGE and CLOCK SOURCE. Those six live in the performance (perf_state, where the
 * runtime reads them; MasterPerformancePreset's layout unchanged) but belong to the instrument: set_inst holds them and
 * settings_apply puts them back onto every performance that loads, the power-on one too, so neither a load nor a power
 * cycle drops the MIDI setup or the external clock. A performance that stacks its layers on one channel (the factory's
 * DUAL ones) keeps stacking, on the instrument's layer-1 channel (settings_take). INT TEMPO and BASE VELOCITY stay the performance's. In
 * X0X's settings object (storage.c OBJ_SET: two 4 KiB sectors at 0xFC000, written in turn, the CRC'd header last, so a
 * save torn by a power cut leaves the one before in charge). The payload starts "CRNC": X0X's own settings loader
 * wants its PROJ_FORMAT word followed by three zero bytes and at least 20 bytes, so after a reinstall of X0X it takes
 * its defaults and never reads ours. Saved from the main loop SET_SAVE_MS after the last change (scrolling the list
 * writes once); a save mutes the audio for the sector erase (tens of ms, audio_silence), and core 1 is idle in RAM
 * then (fm1_cpu1.h). Room for more instrument settings: rsv. */
static UIStateMachine sm;                           /* (the panel's state machine, defined below) */
#define SET_VER 3u                                  /* 3 (2026-10-25): + the MIDI channels, CCs, BEND RANGE, CLOCK SOURCE */
#define SET_SAVE_MS 2000u
typedef struct { uint8_t midi_ch[NLAYER], cc1, cc2, bend, clk; } SetInst;
typedef struct {
    uint8_t magic[4];                               /* "CRNC" */
    uint8_t ver;                                    /* SET_VER (1: the THEME alone; 2: + InstrumentSettings) */
    uint8_t theme;                                  /* UITHEME_* */
    uint8_t master_tune, vel_curve, clock_out;      /* InstrumentSettings (ver 2) */
    SetInst inst;                                   /* set_inst (ver 3) */
    uint8_t rsv[1];
} SynSettings;
PS_STATIC_ASSERT(sizeof(SynSettings) == 16u, "SynSettings: the record in flash");
#define SET_N (4u + sizeof(SetInst))                /* the settings compared: theme, tune, curve, clock out, set_inst */
static SetInst set_inst = {{0u, 1u}, 0u, 0u, 2u, CLKSRC_INTERNAL};   /* perf_defaults' */
static uint8_t set_saved[SET_N];                    /* what flash holds (0xFF: nothing yet, settings_load) */
static uint32_t set_changed_ms;
static uint8_t set_pending;

static uint8_t set_stacked;                         /* 1: the live performance plays both layers on one channel */

/* the live performance's instrument settings into set_inst. Stacked, layer 2 follows layer 1's channel and keeps the
 * instrument's own for the next performance that is not; MIDI CH LAYER2 set apart unstacks it */
static void settings_take(void)
{
    MasterPerformancePreset *p = &perf_state;
    if (set_stacked && p->midi_ch[0] != p->midi_ch[1]) {
        if (p->midi_ch[1] == set_inst.midi_ch[0])
            p->midi_ch[1] = p->midi_ch[0];          /* (one byte: the audio side reads it whole) */
        else
            set_stacked = 0;
    }
    set_inst.midi_ch[0] = p->midi_ch[0] & 0x0Fu;
    if (!set_stacked)
        set_inst.midi_ch[1] = p->midi_ch[1] & 0x0Fu;
    set_inst.cc1 = p->cc1_num;
    set_inst.cc2 = p->cc2_num;
    set_inst.bend = p->bend_range;
    set_inst.clk = p->clock_source;
}

/* a performance just loaded: the instrument's settings onto it */
static void settings_apply(MasterPerformancePreset *p)
{
    set_stacked = (uint8_t)((p->midi_ch[1] & 0x0Fu) == (p->midi_ch[0] & 0x0Fu));
    p->midi_ch[0] = set_inst.midi_ch[0];
    p->midi_ch[1] = set_stacked ? set_inst.midi_ch[0] : set_inst.midi_ch[1];
    p->cc1_num = set_inst.cc1;
    p->cc2_num = set_inst.cc2;
    p->bend_range = set_inst.bend;
    p->clock_source = set_inst.clk;
}

static void settings_now(uint8_t s[SET_N])
{
    s[0] = sm.theme;
    s[1] = instrument.master_tune;
    s[2] = instrument.vel_curve;
    s[3] = instrument.clock_out;
    memcpy(&s[4], &set_inst, sizeof set_inst);
}

/* the stored settings, when valid: 1 */
static int settings_read(SynSettings *r)
{
    return st_load(OBJ_SET, r, sizeof *r) == (int)sizeof *r && r->magic[0] == 'C' && r->magic[1] == 'R' &&
           r->magic[2] == 'N' && r->magic[3] == 'C' && r->ver >= 1u && r->ver <= SET_VER && r->theme < UITHEME_COUNT;
}

/* the THEME of the last session, for the splash before anything else is up (flash only read); SAMHAIN without one */
static uint32_t settings_theme(void)
{
    SynSettings r;
    return flash_ok && settings_read(&r) ? r.theme : UITHEME_SAMHAIN;
}

/* power-on, before syn_boot_perf (which puts set_inst onto performance 0) */
static void settings_load(void)
{
    SynSettings r;
    memset(set_saved, 0xFF, sizeof set_saved);
    if (settings_read(&r)) {
        sm.theme = r.theme;
        if (r.ver >= 2u && r.master_tune >= MASTER_TUNE_MIN && r.master_tune <= MASTER_TUNE_MAX &&
            r.vel_curve < VELCURVE_COUNT && r.clock_out < CLKOUT_COUNT) {
            instrument.master_tune = r.master_tune;
            instrument.vel_curve = r.vel_curve;
            instrument.clock_out = r.clock_out;
        }
        if (r.ver >= 3u && r.inst.midi_ch[0] < 16u && r.inst.midi_ch[1] < 16u && r.inst.cc1 <= VAL_MAX &&
            r.inst.cc2 <= VAL_MAX && r.inst.bend <= VAL_MAX && r.inst.clk < CLKSRC_COUNT)
            set_inst = r.inst;
        settings_now(set_saved);
    }
}

/* the main loop's: settings that changed and then held still SET_SAVE_MS go to flash */
static void settings_tick(void)
{
    SynSettings r;
    uint8_t now[SET_N];
    if (pt_state() != PT_RUN)                       /* (PERF TEST plays the defaults, then puts the live state back) */
        settings_take();
    settings_now(now);
    if (memcmp(now, set_saved, sizeof now) == 0) {
        set_pending = 0;
        return;
    }
    if (!set_pending) {
        set_pending = 1;
        set_changed_ms = fm1_ms;
        return;
    }
    if (fm1_ms - set_changed_ms < SET_SAVE_MS || pt_state() == PT_RUN)
        return;
    memset(&r, 0, sizeof r);
    r.magic[0] = 'C'; r.magic[1] = 'R'; r.magic[2] = 'N'; r.magic[3] = 'C';
    r.ver = SET_VER;
    r.theme = now[0];
    r.master_tune = now[1];
    r.vel_curve = now[2];
    r.clock_out = now[3];
    r.inst = set_inst;
    if (st_save(OBJ_SET, &r, sizeof r) == 0)
        memcpy(set_saved, now, sizeof now);         /* (a failed save retries SET_SAVE_MS later) */
    else
        set_changed_ms = fm1_ms;
}

/* The banks a SAVE writes and a load reads: patches in preset.c's patch_bank (128), sequences and arps below (32 each
 * since 2026-10-17), all in .pool; the 32 performances are whole snapshots (WholePerformance) kept in flash only, their
 * names here. syn_used marks the slots written (or the factory's), so a never-written slot reads EMPTY. Kept in flash
 * by the preset store below (2026-10-16; before, RAM only); the browser emulator writes it to its simulated NOR. */
static SequencerLane syn_seq_bank[BANK_SLOTS][SEQ_NLANE] SYNTH_POOL;
static ArpState syn_arp_bank[BANK_SLOTS] SYNTH_POOL;
static uint8_t syn_perf_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_seq_name[BANK_SLOTS][NAME_LEN];  /* the sequences' and arp patterns' names (2026-10-25), char_map */
static uint8_t syn_arp_name[BANK_SLOTS][NAME_LEN];
static uint8_t syn_used[UISAVE_COUNT][PATCH_SLOTS / 8u];
static WholePerformance syn_perf_load SYNTH_POOL;   /* a performance being loaded: read outside syn_lock, taken inside */
static uint8_t syn_perf_req;                        /* 1: a performance load waits for the main loop (syn_perf_tick) */

static UIStateMachine sm;

static uint8_t syn_slot_used(uint8_t target, uint8_t slot)
{
    return (uint8_t)((syn_used[target][slot >> 3] >> (slot & 7u)) & 1u);
}

#include "preset_store_fm1.c"                      /* the SAVEs in flash */
#include "factory_presets.c"                       /* the factory bank, STONE & FIRE */

static uint8_t syn_slot_query(void *ctx, uint8_t target, uint8_t slot, uint8_t name[NAME_LEN])
{
    (void)ctx;
    if (target >= UISAVE_COUNT || slot >= ui_sm_bank_slots(target) || !syn_slot_used(target, slot))
        return UISLOT_EMPTY;
    if (target == UISAVE_PATCH_PRESET) memcpy(name, patch_bank[slot].name, NAME_LEN);
    else if (target == UISAVE_WHOLE_PERF) memcpy(name, syn_perf_name[slot], NAME_LEN);
    else if (target == UISAVE_SEQ_SETTINGS) memcpy(name, syn_seq_name[slot], NAME_LEN);
    else memcpy(name, syn_arp_name[slot], NAME_LEN);
    return UISLOT_USED;
}

/* the machine's commit_pending: save_target into save_slot, its chunk marked for the preset store (inside syn_lock);
 * ps_tick reports it done once it is in flash. A performance: the live one and everything it plays, staged whole */
static void syn_commit(void)
{
    uint8_t t = sm.save_target, s = sm.save_slot;
    if (ps_commit_wait)
        return;                                     /* (the one before is still being written) */
    if (t >= UISAVE_COUNT || s >= ui_sm_bank_slots(t)) {
        ui_sm_commit_done(&sm, 0u);
        return;
    }
    switch (t) {
    case UISAVE_PATCH_PRESET: patch_store(sm.s.active_synth_layer, s); break;
    case UISAVE_SEQ_SETTINGS:
        memcpy(syn_seq_bank[s], seq_lanes, sizeof syn_seq_bank[s]);
        ps_name_save(t, s, sm.seq_name);
        break;
    case UISAVE_ARP_SEQ:
        syn_arp_bank[s] = arp_state;
        ps_name_save(t, s, sm.arp_name);
        break;
    default:
        ps_perf_save.perf = perf_state;
        ps_perf_save.engine[0] = engine_state[0];
        ps_perf_save.engine[1] = engine_state[1];
        memcpy(ps_perf_save.seq, seq_lanes, sizeof ps_perf_save.seq);
        ps_perf_save.arp = arp_state;
        ps_perf_save_slot = s;
        memcpy(syn_perf_name[s], perf_state.name, NAME_LEN);
        break;
    }
    syn_used[t][s >> 3] |= (uint8_t)(1u << (s & 7u));
    ps_dirty |= 1u << (PS_KIND[t].first + s / PS_PER(PS_KIND[t].stored));
    ps_commit_wait = 1;
}

/* the machine's load_pending (the Preset Encoder): load_target from load_slot into the live state (inside syn_lock). A
 * performance waits for the main loop (syn_perf_tick): it is read from flash */
static void syn_load(void)
{
    uint8_t t = sm.load_target, s = sm.load_slot, ok;
    ok = (uint8_t)(t < UISAVE_COUNT && s < ui_sm_bank_slots(t) && syn_slot_used(t, s));
    if (ok && t == UISAVE_WHOLE_PERF) {
        syn_perf_req = 1;
        return;
    }
    if (ok) {
        switch (t) {
        case UISAVE_PATCH_PRESET: patch_load(sm.load_layer, s); break;
        case UISAVE_SEQ_SETTINGS:
            memcpy(seq_lanes, syn_seq_bank[s], sizeof seq_lanes);
            memcpy(sm.seq_name, syn_seq_name[s], NAME_LEN);
            break;
        default:
            arp_state = syn_arp_bank[s];
            memcpy(sm.arp_name, syn_arp_name[s], NAME_LEN);
            break;
        }
    }
    ui_sm_load_done(&sm, ok);
}

/* a performance just loaded: the live sequence and arp pattern take the names of the slots they came from (seq_link,
 * arp_link), or none */
static void syn_link_names(void)
{
    uint8_t q = perf_state.seq_link, a = perf_state.arp_link;
    if (q < BANK_SLOTS && syn_slot_used(UISAVE_SEQ_SETTINGS, q)) memcpy(sm.seq_name, syn_seq_name[q], NAME_LEN);
    else memset(sm.seq_name, 0, NAME_LEN);
    if (a < BANK_SLOTS && syn_slot_used(UISAVE_ARP_SEQ, a)) memcpy(sm.arp_name, syn_arp_name[a], NAME_LEN);
    else memset(sm.arp_name, 0, NAME_LEN);
}

/* the main loop's: the performance load syn_load left. Its slot from flash, outside syn_lock (a factory slot never
 * saved over: made, factory_perf), then the live state takes it whole inside the lock: the performance's settings, both
 * layers' patches, the sequence and the arp pattern. (The Preset Encoder waits: load_pending holds load_slot.) */
static void syn_perf_tick(void)
{
    uint8_t ok;
    if (!syn_perf_req)
        return;
    ok = (uint8_t)(ps_perf_read(sm.load_slot, &syn_perf_load) == 0 || factory_perf(sm.load_slot, &syn_perf_load) == 0);
    syn_lock();
    if (ok) {
        perf_state = syn_perf_load.perf;
        settings_apply(&perf_state);                /* the instrument's MIDI channels, CLOCK SOURCE .. */
        engine_state[0] = syn_perf_load.engine[0];
        engine_state[1] = syn_perf_load.engine[1];
        engine_load_count[0]++;
        engine_load_count[1]++;
        memcpy(seq_lanes, syn_perf_load.seq, sizeof seq_lanes);
        arp_state = syn_perf_load.arp;
        syn_link_names();
    }
    ui_sm_load_done(&sm, ok);
    syn_unlock();
    syn_perf_req = 0;
}


/* power-on, after ps_load and before the audio starts (no lock: nothing else runs yet, and syn_unlock would turn the
 * interrupts on early): the factory's performance 0 whole, NEWGRANGE DAWN, always (a performance saved in slot 0 stays
 * there, loaded from the PERF page as before), so the first key played sounds the bank's opening patch with its
 * effects instead of the bare defaults */
static void syn_boot_perf(void)
{
    if (factory_perf(0u, &syn_perf_load) != 0)
        return;
    perf_state = syn_perf_load.perf;
    settings_apply(&perf_state);
    engine_state[0] = syn_perf_load.engine[0];
    engine_state[1] = syn_perf_load.engine[1];
    engine_load_count[0]++;
    engine_load_count[1]++;
    memcpy(seq_lanes, syn_perf_load.seq, sizeof seq_lanes);
    arp_state = syn_perf_load.arp;
    syn_link_names();
    sm.perf_slot = 0u;
    ui_sm_enforce(&sm);                             /* the performance's mode, its patches' architectures */
}


/* ------------------------------------------------------------- panel --- */
#define KEY_F3 53u                                  /* the lowest key at octave 0 */
#define OCT_MIN (-3)
#define OCT_MAX 3
#define NOTE_NONE 0xFFu
static int32_t octave;
static uint32_t keys_prev, btn_prev;
static uint8_t key_note[NKEYS];                     /* the note key k played (its note-off goes there), NOTE_NONE */
static uint8_t key_ch[NKEYS];                       /* .. and its channel: a layer change keeps the note-off on it */
static uint8_t oct_held[2], oct_used[2];            /* OCT- / OCT+ down, and used as a modifier since */
static uint8_t home_held, home_chord;               /* HOME down; an OCT pressed since (a layer chord, not a page) */
static uint32_t home_ms;                            /* when HOME went down */
static uint8_t save_held, save_chord;               /* the same for SAVE: an OCT since = COPY / PASTE, not a save */
static uint32_t save_ms;
#define HOME_HINT_MS 200u                           /* HOME held this long: the OCT LEDs show the layer chord */
static uint32_t knob_last_ms[4];
static uint32_t bend_last_ms;                       /* the last PITCH BEND detent (HOME knob 2): its spring waits for it */
#define BEND_SPRING_WAIT_MS 150u                    /* no detent this long: the bend glides back to centre */

static uint8_t base_note(void) { return (uint8_t)((int32_t)KEY_F3 + 12 * octave); }
static uint8_t keys_page(void) { return sm.page == UIPAGE_SEQ || sm.page == UIPAGE_ARP; }
static uint8_t key_vel(void)
{
    uint8_t v = sm.perf_knob[UIPERF_VELOCITY];
    return v ? v : 1u;                              /* a note-on with velocity 0 is a note-off */
}

/* every note the keys are sounding off (before the octave moves: no note may hang) */
static void keys_release(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (key_note[k] != NOTE_NONE) {
            synth_midi_3((uint8_t)(0x80u | key_ch[k]), key_note[k], 0);
            key_note[k] = NOTE_NONE;
        }
}

static void octave_shift(int32_t d)
{
    int32_t o = octave + d;
    if (o < OCT_MIN || o > OCT_MAX)
        return;
    keys_release();
    octave = o;
}

/* one key edge, as the browser's worklet takes it: on SEQ / ARP the machine first (step selectors, loop shortcuts,
 * chord and step keys), else (or when it does not take it) played */
static void key_edge(uint32_t k, uint32_t down)
{
    uint8_t note = (uint8_t)(base_note() + k), taken = 0, mods;
    if (keys_page()) {
        mods = (uint8_t)((oct_held[0] ? UISEQ_MOD_OCT_DOWN : 0u) | (oct_held[1] ? UISEQ_MOD_OCT_UP : 0u));
        if (oct_held[0]) oct_used[0] = 1;
        if (oct_held[1]) oct_used[1] = 1;
        taken = (uint8_t)(ui_sm_seq_key(&sm, (uint8_t)k, note, (uint8_t)down, mods) ||
                          ui_sm_arp_key(&sm, (uint8_t)k, note, (uint8_t)down, key_vel()));
    }
    if (down && !taken && key_note[k] == NOTE_NONE) {
        key_ch[k] = ui_sm_key_channel(&sm);         /* DUAL: the selected layer */
        synth_midi_3((uint8_t)(0x90u | key_ch[k]), note, key_vel());
        key_note[k] = note;
    } else if (!down && key_note[k] != NOTE_NONE) {  /* released the way it was pressed */
        synth_midi_3((uint8_t)(0x80u | key_ch[k]), key_note[k], 0);
        key_note[k] = NOTE_NONE;
    }
}

/* a knob turned d detents: the page's knob map for the focused layer (ui_sm_knob), as web_knob_set has it with a
 * relative step. Fast turns (detents < 25 ms apart) move 4 a detent. */
static void knob_turn(uint32_t k, int32_t d)
{
    UiKnobInfo info;
    uint8_t *p = ui_sm_knob(&sm, sm.page, sm.s.active_synth_layer, (uint8_t)k, &info);
    int32_t v, hi;
    if (fm1_ms - knob_last_ms[k] < 25u)
        d *= 4;
    knob_last_ms[k] = fm1_ms;
    if (info.kind == UIKNOB_SEQ) {
        ui_sm_seq_knob_turn(&sm, (uint8_t)k, (int8_t)(d > 127 ? 127 : d < -127 ? -127 : d));
        return;
    }
    if (!p)
        return;
    if (sm.page == UIPAGE_PERF) {                   /* the links: load the next used slot, not just renumber */
        ui_sm_perf_link_turn(&sm, (uint8_t)k, (int16_t)(d > 127 ? 127 : d < -127 ? -127 : d));
        return;
    }
    hi = info.n_enum ? (int32_t)info.n_enum - 1 : (int32_t)VAL_MAX;
    if (info.n_enum)                                /* a list moves one entry a detent */
        d = d > 0 ? 1 : -1;
    v = (int32_t)*p + d;
    v = v < 0 ? 0 : v > hi ? hi : v;
    *p = (uint8_t)v;
    if (info.kind == UIKNOB_PERF) {                 /* LAYER: the play controllers; velocity stays here (key_vel) */
        uint32_t pk = (uint32_t)(p - sm.perf_knob);
        if (pk == UIPERF_BEND) {
            synth_midi_3(0xE0, 0, (uint8_t)v);
            bend_last_ms = fm1_ms;
        } else if (pk == UIPERF_MODWHEEL) {
            synth_midi_3(0xB0, 1, (uint8_t)v);
        } else if (pk == UIPERF_CC1 || pk == UIPERF_CC2) {
            synth_custom_cc((uint8_t)(pk - UIPERF_CC1), (uint8_t)v);   /* the CUSTOMCC1 / 2 mod sources */
        }
    }
    ui_sm_knob_written(&sm, sm.page, (uint8_t)k);
}

/* PITCH BEND is spring-loaded, as a wheel is: BEND_SPRING_WAIT_MS after its last detent it glides back to centre, a
 * third of the way each UI frame (~0.2 s from full throw), sent as MIDI as the knob sends it. Once per UI frame,
 * inside the lock. */
static void bend_spring(void)
{
    int32_t v = sm.perf_knob[UIPERF_BEND], d = (int32_t)BIPOLAR_CENTER - v;
    if (d == 0 || fm1_ms - bend_last_ms < BEND_SPRING_WAIT_MS)
        return;
    v += d > 0 ? (d + 2) / 3 : (d - 2) / 3;
    sm.perf_knob[UIPERF_BEND] = (uint8_t)v;
    synth_midi_3(0xE0, 0, (uint8_t)v);
}

static int8_t detents8(int32_t d) { return (int8_t)(d > 127 ? 127 : d < -127 ? -127 : d); }

/* the panel since the last call: buttons, encoders, knobs, keys. Main loop; every machine / synth call inside the
 * lock. Returns 1 when anything happened. */
static uint32_t panel_input(void)
{
    static const uint8_t SECTION[NB] = {
        BTN_FX, BTN_SEL, BTN_ENV, BTN_LFO, BTN_EDIT, BTN_GLO, BTN_HOME, BTN_SAVE, BTN_ARP, BTN_SEQ,
        0xFF, 0xFF, 0xFF, 0xFF,                     /* PLAY, REC, OCT-, OCT+: below */
    };
    uint32_t btn = plat_buttons(), down = btn & ~btn_prev, up = ~btn & btn_prev;
    uint32_t keys = plat_keys(), kch = keys ^ keys_prev, i, any = (down | up | kch) != 0u;
    int32_t e[NE];
    for (i = 0; i < NE; i++) {
        e[i] = plat_enc((int)i);
        any |= e[i] != 0;
    }
    btn_prev = btn;
    keys_prev = keys;
    if (!any)
        return 0;
    syn_lock();
    for (i = 0; i < NB; i++)
        if ((down >> i) & 1u) {
            if (i == B_HOME) {                      /* acts on release (below), unless an OCT makes it a chord */
                home_held = 1;
                home_chord = 0;
                home_ms = fm1_ms;
            } else if (i == B_SAVE) {               /* acts on release too, unless an OCT makes it COPY / PASTE */
                save_held = 1;
                save_chord = 0;
                save_ms = fm1_ms;
            } else if (home_held && (i == B_OCTDN || i == B_OCTUP)) {
                home_chord = 1;                     /* the layer, not the octave (refused for L2 under SINGL) */
                ui_sm_set_layer_focus(&sm, (uint8_t)(i == B_OCTUP));
            } else if (save_held && (i == B_OCTDN || i == B_OCTUP)) {
                save_chord = 1;                     /* OCT-: COPY, OCT+: PASTE (asks first) */
                ui_sm_save_oct(&sm, (uint8_t)(i == B_OCTUP));
            } else if (SECTION[i] != 0xFFu)
                ui_sm_handle(&sm, UIEV_BUTTON, (int8_t)SECTION[i]);
            else if (i == B_PLAY)
                ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_PLAY);
            else if (i == B_REC)
                ui_sm_handle(&sm, UIEV_TRANSPORT, UITR_REC);
            else {                                  /* OCT- / OCT+: off the SEQ page they act at once */
                uint32_t w = i == B_OCTUP;
                oct_held[w] = 1;
                oct_used[w] = 0;
                if (sm.page != UIPAGE_SEQ) {
                    octave_shift(w ? 1 : -1);
                    oct_used[w] = 1;
                }
            }
            if (sm.commit_pending)
                syn_commit();
        }
    if (((up >> B_SAVE) & 1u) && save_held) {
        save_held = 0;
        ui_sm_set_hold(&sm, BTN_COUNT);
        if (!save_chord) {
            ui_sm_handle(&sm, UIEV_BUTTON, BTN_SAVE);
            if (sm.commit_pending)
                syn_commit();
        }
    }
    if (((up >> B_HOME) & 1u) && home_held) {
        home_held = 0;
        ui_sm_set_hold(&sm, BTN_COUNT);
        if (!home_chord) {
            ui_sm_handle(&sm, UIEV_BUTTON, BTN_HOME);
            if (sm.commit_pending)
                syn_commit();
        }
    }
    for (i = B_OCTDN; i <= B_OCTUP; i++)
        if ((up >> i) & 1u) {                       /* SEQ: a tap that set no loop boundary moves the octave */
            uint32_t w = i == B_OCTUP;
            if (oct_held[w] && !oct_used[w])
                octave_shift(w ? 1 : -1);
            oct_held[w] = 0;
        }
    if (e[EN_SELECT])
        ui_sm_handle(&sm, UIEV_SELECT_ENC, detents8(e[EN_SELECT]));
    if (e[EN_ALGO])
        ui_sm_handle(&sm, UIEV_ALGO_ENC, detents8(e[EN_ALGO]));
    if (e[EN_PRESET] && !home_held)                 /* (HOME held: a slip of the hand, not a load) */
        ui_sm_handle(&sm, UIEV_PRESET_ENC, detents8(e[EN_PRESET]));
    for (i = 0; i < 4u; i++)
        if (e[EN_K1 + i])
            knob_turn(i, e[EN_K1 + i]);
    if (sm.load_pending && !syn_perf_req)           /* the Preset Encoder's, or a PERF page link's (knob or row) */
        syn_load();
    for (i = 0; i < NKEYS; i++)
        if ((kch >> i) & 1u)
            key_edge(i, (keys >> i) & 1u);
    syn_unlock();
    return 1;
}

/* the LEDs: the open section's button alone (no glow on the others), PLAY (green; red while recording), the keys the
 * SEQ / ARP pages give a job (ui_sm_key_leds: its "mid" level shows lit, X0X's LEDs have two levels), and the keys
 * held. SAVE flashes while a press of it would act: on the save page with an ACTION armed (SAVE by default), and
 * under the confirm popup */
static uint32_t page_button(uint8_t page)
{
    if (page <= UIPAGE_OSCMIX) return 1u << B_FX;
    if (page <= UIPAGE_FLTMIX) return 1u << B_SEL;
    if (page <= UIPAGE_ENV3) return 1u << B_ENV;
    if (page <= UIPAGE_LFO3) return 1u << B_LFO;
    if (page <= UIPAGE_MOD3) return 1u << B_EDIT;
    if (page <= UIPAGE_EFFECTS) return 1u << B_GLO;
    if (page <= UIPAGE_GLOBAL) return 1u << B_HOME;                   /* HOME's views, then GLOBAL */
    if (page == UIPAGE_ARP) return 1u << B_ARP;
    if (page == UIPAGE_SEQ) return 1u << B_SEQ;
    return 1u << B_SAVE;
}

/* the first half of each quarter note (24 clock pulses) since `origin`, the transport's first step; dark while a start
 * waits for its bar (origin still ahead) */
static uint32_t beat_lit(uint32_t origin)
{
    int32_t d = (int32_t)(midi_clock_pulse_count() - origin);
    return d >= 0 && (uint32_t)d % 24u < 12u;
}

static void panel_leds(void)
{
    UiKeyLeds kl;
    uint32_t b = page_button(sm.page), rec = maq_recording(synth_seq()) || sm.arp_rec;
    uint8_t mods = (uint8_t)((oct_held[0] ? UISEQ_MOD_OCT_DOWN : 0u) | (oct_held[1] ? UISEQ_MOD_OCT_UP : 0u));
    if (ui_sm_play_lit(&sm) || rec)
        b |= 1u << B_PLAY;
    if (rec)
        b |= 1u << B_REC;
    if (octave < 0) b |= 1u << B_OCTDN;
    if (octave > 0) b |= 1u << B_OCTUP;
    if (home_held && fm1_ms - home_ms >= UI_HOLD_MS)   /* the hold strip names what OCT- / OCT+ do */
        ui_sm_set_hold(&sm, BTN_HOME);
    else if (save_held && fm1_ms - save_ms >= UI_HOLD_MS)
        ui_sm_set_hold(&sm, BTN_SAVE);
    if (home_held && fm1_ms - home_ms >= HOME_HINT_MS) {   /* the layer chord: focused lit, the other flashing */
        uint32_t on = sm.s.active_synth_layer ? 1u << B_OCTUP : 1u << B_OCTDN, other = (1u << B_OCTDN | 1u << B_OCTUP) & ~on;
        b &= ~(1u << B_OCTDN | 1u << B_OCTUP);
        b |= on;
        if (sm.perf->perf_mode != PERF_SINGL_SYNTH && ((fm1_ms / 125u) & 1u))   /* 4 Hz; SINGL has no L2 */
            b |= other;
    }
    if (maq_playing(synth_seq())) {                 /* a playing transport blinks its button on the beat */
        b &= ~(1u << B_SEQ);
        if (beat_lit(maq_origin(synth_seq())))
            b |= 1u << B_SEQ;
    }
    if (carp_playing(synth_arp())) {
        b &= ~(1u << B_ARP);
        if (beat_lit(carp_origin(synth_arp())))
            b |= 1u << B_ARP;
    }
    if (ui_sm_save_armed(&sm)) {
        b &= ~(1u << B_SAVE);
        if ((fm1_ms / 250u) & 1u)                   /* 2 Hz */
            b |= 1u << B_SAVE;
    }
    if (!ui_sm_key_leds(&sm, base_note(), mods, &kl))
        kl.lit = kl.mid = kl.dim = 0;
    plat_glow(0);
    plat_play_red((int)rec);
    plat_keys_dim(kl.dim);
    plat_leds(b, kl.lit | kl.mid | keys_prev);
    if (sm.page == UIPAGE_ARP && kl.dim)            /* the ARP's chord steps at half brightness, not X0X's quarter */
        fm1_led_dim_div = 2u;                       /* (2026-10-21: too faint to read; the playhead stays full) */
}

