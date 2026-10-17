
#include "voice_allocator.h"
#include "preset.h"          // perf_state and engine_state: the voice limits come from the live patches

SynthVoiceNode poly_bank[POLYPHONY_VOICES];
volatile uint8_t voice_shed;

void voice_mod_fn_reset(SynthVoiceNode *v, int slot) {
    ModFnState *m = &v->mod_fn[slot];
    m->out_q15 = 0;
    m->held_q15 = 0;
    m->tick = 0xFFFFFFFFu;     /* never equal to a frame tick, so the slot evaluates on its next read */
    m->prev_side = 0;
    m->init = 0;
    m->busy = 0;
}

void voice_allocator_init(void) {
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        SynthVoiceNode *n = &poly_bank[v];
        osc_init(&n->osc1);
        osc_init(&n->osc2);
        adsr_init(&n->amp_env);
        adsr_init(&n->mod_env);
        adsr_init(&n->aux_env);
        dual_filter_init(&n->synth_filters);
        amp_init(&n->master_amp);
        noise_state_init(&n->noise[0]);
        noise_state_init(&n->noise[1]);
        noise_seed_init(&n->noise[0], 2u * (uint32_t)v);
        noise_seed_init(&n->noise[1], 2u * (uint32_t)v + 1u);
        n->active_note = 33;
        n->is_gate_open = 0;
        n->age_counter = 0;
        n->mod_pass = 0;
        /* free-running oscillators: each voice starts at its own point of the cycle (voice_render_block keeps them
         * turning), so a unison stack never opens with every voice in phase. */
        uint32_t ph = (uint32_t)(v + 1) * 0x9E3779B9u;
        osc_set_phase(&n->osc1, ph);
        osc_set_phase(&n->osc2, ph);
        n->sub_phase = ph >> 1;
        n->layer = 0;
        n->release_boost_q15 = 0;

        n->amp_env.level_q15 = 0;
        n->mod_env.level_q15 = 0;
        n->aux_env.level_q15 = 0;

        for (int s = 0; s < (int)NMODSLOT; s++)
            voice_mod_fn_reset(n, s);
        n->glide_q16 = 0;
        n->glide_step_q16 = 0;
        n->detune_cents = 0;
        n->unison_gain_q15 = 32767;
        n->unison_pan = 0;
        n->route_q15 = 32767;
        n->vol = n->vol_step = n->vol_b = n->vol_b_step = 0.f;
        n->atk_p = NULL;
        n->atk_pos = n->atk_step = n->atk_end = 0;
        n->atk_sample = 0;
        n->atk_vol = n->atk_vol_step = 0.f;
        n->atk_pre = n->atk_pre_step = 0.f;
        n->atk_fade = 1.f;
        n->atk_f1 = 0;
        n->atk_tt = 0xFFFFu;
        n->atk_base = 0;
        n->atk_glide = 0.f;
        for (int k = 0; k < 4; k++) n->mix_a[k] = n->mix_b[k] = 0.f;
        n->choke = 0;
        n->choke_q15 = 32767;
        n->arch = VARCH_SINGLE;
        n->ctrl_left = 0;
        n->snap = 1;
        n->silent = 1;
        n->lfo_q15[0] = n->lfo_q15[1] = 0;

        for (int i = 0; i < 2; i++) {
            n->poly_lfos.engines[i].phase_accumulator = 0;
            n->poly_lfos.engines[i].delay_counter = 0;
            n->poly_lfos.engines[i].fade_counter = 0;
            n->poly_lfos.engines[i].envelope_amp_q15 = 0;
        }
    }
}

void pool_range(uint8_t layer, int *first, int *count) {
    uint8_t mode = perf_state.perf_mode < PERF_COUNT ? perf_state.perf_mode : (uint8_t)PERF_SINGL_SYNTH;
    *first = layer ? POLYPHONY_VOICES / 2 : 0;
    *count = layer < NLAYER ? layer_voice_limit(mode, &engine_state[0], &engine_state[1], layer) : 0;
    if (voice_shed && *count > 1)
        *count = *count > (int)voice_shed + 1 ? *count - (int)voice_shed : 1;
}

int check_if_any_other_voice_held(int current_voice_index, uint8_t layer) {
    for (int v = 0; v < POLYPHONY_VOICES; v++) {
        if (v != current_voice_index && poly_bank[v].layer == layer && poly_bank[v].is_gate_open) {
            return 1;
        }
    }
    return 0;
}

/* -1 when the layer has no voices (layer 2 under SINGL_SYNTH) */
int allocate_voice_slot(uint8_t layer) {
    int first, count, end;
    pool_range(layer, &first, &count);
    if (count <= 0) return -1;
    end = first + count;

    // Stage 1: Try to look for an inactive, perfectly silent voice slot
    for (int v = first; v < end; v++) {
        if (voice_is_free(&poly_bank[v])) {
            return v;
        }
    }

    // Stage 2: Voice Stealing (Notes in Release)
    int oldest_released_voice = -1;
    uint32_t maximum_released_age = 0;
    for (int v = first; v < end; v++) {
        if (!poly_bank[v].is_gate_open) {
            // FIXED: Using >= ensures chords distribute evenly across slots during voice-stealing turnarounds
            if (poly_bank[v].age_counter >= maximum_released_age) {
                maximum_released_age = poly_bank[v].age_counter;
                oldest_released_voice = v;
            }
        }
    }
    if (oldest_released_voice != -1) return oldest_released_voice;

    // Stage 3: Emergency Stealing (Active Held Notes)
    int oldest_active_voice = first;
    uint32_t maximum_active_age = 0;
    for (int v = first; v < end; v++) {
        // FIXED: Using >= ensures chord notes don't stack up onto the same index slot during peak polyphony choke
        if (poly_bank[v].age_counter >= maximum_active_age) {
            maximum_active_age = poly_bank[v].age_counter;
            oldest_active_voice = v;
        }
    }
    return oldest_active_voice;
}
