/* SPDX-License-Identifier: GPL-3.0-only */
/* The NRPN map (2026-11-19, the user: "separate fixed"): each NRPN number (MSB, LSB) and the panel control it is, by
 * page and item (UIStateMachine.c: item 0..3 a knob, 4 + r row r; on AMP 16..19 the MFX's knobs, 20 its type, 21 + r
 * its row r + 1). The numbers are the editor's and every MIDI rig's, so they never change: a new control gets a new
 * number (a new line, here and in src/host/nrpn_map_golden.txt), a control that goes keeps its number unused, and a
 * row that moves on its page changes its item here, never its number. test_nrpn_map checks every line against the
 * golden list by the control's name. Numbered at first as the pages stood on 2026-11-19 (MSB the page, LSB the
 * item). The MOD routes are not here: MSB 64 + route, LSB 0..4 (SOURCE, VIA, DEST, AMOUNT, VIA DEPTH). Included in
 * UIStateMachine.c's NRPN_MAP, N(msb, lsb, page, item). */
    /* OSC1 */
    N(0, 0, OSC1, 0), N(0, 1, OSC1, 1), N(0, 2, OSC1, 2), N(0, 3, OSC1, 3), N(0, 4, OSC1, 4),
    N(0, 5, OSC1, 5), N(0, 6, OSC1, 6),
    /* OSC2 */
    N(1, 0, OSC2, 0), N(1, 1, OSC2, 1), N(1, 2, OSC2, 2), N(1, 3, OSC2, 3), N(1, 4, OSC2, 4),
    N(1, 5, OSC2, 5), N(1, 6, OSC2, 6), N(1, 7, OSC2, 7), N(1, 8, OSC2, 8),
    /* OSCMIX */
    N(2, 0, OSCMIX, 0), N(2, 1, OSCMIX, 1), N(2, 2, OSCMIX, 2), N(2, 3, OSCMIX, 3), N(2, 4, OSCMIX, 4),
    N(2, 5, OSCMIX, 5), N(2, 6, OSCMIX, 6), N(2, 7, OSCMIX, 7), N(2, 8, OSCMIX, 8), N(2, 9, OSCMIX, 9),
    N(2, 10, OSCMIX, 10), N(2, 11, OSCMIX, 11), N(2, 12, OSCMIX, 12), N(2, 13, OSCMIX, 13), N(2, 14, OSCMIX, 14),   /* 12 CLUSTER, 13 COLOUR, 14 INHARM (2026-11-21) */
    /* FILTER1 */
    N(3, 0, FILTER1, 0), N(3, 1, FILTER1, 1), N(3, 2, FILTER1, 2), N(3, 3, FILTER1, 3), N(3, 4, FILTER1, 4),
    N(3, 5, FILTER1, 5), N(3, 6, FILTER1, 6),
    /* FILTER2 */
    N(4, 0, FILTER2, 0), N(4, 1, FILTER2, 1), N(4, 2, FILTER2, 2), N(4, 3, FILTER2, 3), N(4, 4, FILTER2, 4),
    N(4, 5, FILTER2, 5), N(4, 6, FILTER2, 6),
    /* FLTMIX */
    N(5, 0, FLTMIX, 0), N(5, 1, FLTMIX, 1), N(5, 2, FLTMIX, 2), N(5, 3, FLTMIX, 3),
    /* ENV1 */
    N(6, 0, ENV1, 0), N(6, 1, ENV1, 1), N(6, 2, ENV1, 2), N(6, 3, ENV1, 3), N(6, 4, ENV1, 4),
    N(6, 5, ENV1, 5), N(6, 6, ENV1, 6), N(6, 7, ENV1, 7), N(6, 8, ENV1, 8), N(6, 9, ENV1, 9),
    N(6, 10, ENV1, 10),
    /* ENV2 */
    N(7, 0, ENV2, 0), N(7, 1, ENV2, 1), N(7, 2, ENV2, 2), N(7, 3, ENV2, 3), N(7, 4, ENV2, 4),
    N(7, 5, ENV2, 5), N(7, 6, ENV2, 6), N(7, 7, ENV2, 7), N(7, 8, ENV2, 8), N(7, 9, ENV2, 9),
    N(7, 10, ENV2, 10), N(7, 11, ENV2, 11),
    /* ENV3 */
    N(8, 0, ENV3, 0), N(8, 1, ENV3, 1), N(8, 2, ENV3, 2), N(8, 3, ENV3, 3), N(8, 4, ENV3, 4),
    N(8, 5, ENV3, 5), N(8, 6, ENV3, 6), N(8, 7, ENV3, 7), N(8, 8, ENV3, 8), N(8, 9, ENV3, 9),
    N(8, 10, ENV3, 10), N(8, 11, ENV3, 11),
    /* LFO1 */
    N(9, 0, LFO1, 0), N(9, 1, LFO1, 1), N(9, 2, LFO1, 2), N(9, 3, LFO1, 3), N(9, 4, LFO1, 4),
    N(9, 5, LFO1, 5), N(9, 6, LFO1, 6), N(9, 7, LFO1, 7), N(9, 8, LFO1, 8), N(9, 9, LFO1, 9),
    N(9, 10, LFO1, 10), N(9, 11, LFO1, 11),
    /* LFO2 */
    N(10, 0, LFO2, 0), N(10, 1, LFO2, 1), N(10, 2, LFO2, 2), N(10, 3, LFO2, 3), N(10, 4, LFO2, 4),
    N(10, 5, LFO2, 5), N(10, 6, LFO2, 6), N(10, 7, LFO2, 7),
    /* LFO3 */
    N(11, 0, LFO3, 0), N(11, 1, LFO3, 1), N(11, 2, LFO3, 2), N(11, 3, LFO3, 3), N(11, 4, LFO3, 4),
    N(11, 5, LFO3, 5), N(11, 6, LFO3, 6), N(11, 7, LFO3, 7),
    /* AMP */
    N(13, 0, AMP, 0), N(13, 1, AMP, 1), N(13, 2, AMP, 2), N(13, 3, AMP, 3), N(13, 4, AMP, 4),
    N(13, 16, AMP, 16), N(13, 17, AMP, 17), N(13, 18, AMP, 18), N(13, 19, AMP, 19), N(13, 20, AMP, 20),
    N(13, 21, AMP, 21), N(13, 22, AMP, 22), N(13, 23, AMP, 23), N(13, 24, AMP, 24), N(13, 25, AMP, 25),
    N(13, 26, AMP, 26), N(13, 27, AMP, 27), N(13, 28, AMP, 28), N(13, 29, AMP, 29), N(13, 30, AMP, 30),
    N(13, 31, AMP, 31), N(13, 32, AMP, 32), N(13, 33, AMP, 33),
    /* EFFECTS */
    N(14, 0, EFFECTS, 0), N(14, 1, EFFECTS, 1), N(14, 2, EFFECTS, 2), N(14, 3, EFFECTS, 3), N(14, 4, EFFECTS, 4),
    N(14, 5, EFFECTS, 5), N(14, 6, EFFECTS, 6), N(14, 7, EFFECTS, 7),
    /* MIX */
    N(15, 0, MIX, 0), N(15, 1, MIX, 1), N(15, 2, MIX, 2), N(15, 3, MIX, 3), N(15, 4, MIX, 4),
    N(15, 5, MIX, 5), N(15, 6, MIX, 6), N(15, 7, MIX, 7), N(15, 8, MIX, 8),
    /* HOME1 */
    N(16, 0, HOME1, 0), N(16, 1, HOME1, 1), N(16, 2, HOME1, 2), N(16, 3, HOME1, 3), N(16, 4, HOME1, 4),
    N(16, 5, HOME1, 5), N(16, 6, HOME1, 6), N(16, 7, HOME1, 7), N(16, 8, HOME1, 8), N(16, 9, HOME1, 9),
    N(16, 10, HOME1, 10),
    /* HOME3 */
    N(18, 0, HOME3, 0), N(18, 1, HOME3, 1), N(18, 2, HOME3, 2), N(18, 3, HOME3, 3), N(18, 4, HOME3, 4),
    N(18, 5, HOME3, 5), N(18, 6, HOME3, 6), N(18, 7, HOME3, 7),
    /* GLOBAL */
    N(20, 0, GLOBAL, 0), N(20, 1, GLOBAL, 1), N(20, 2, GLOBAL, 2), N(20, 3, GLOBAL, 3), N(20, 4, GLOBAL, 4),
    N(20, 5, GLOBAL, 5), N(20, 6, GLOBAL, 6), N(20, 7, GLOBAL, 7), N(20, 8, GLOBAL, 8), N(20, 9, GLOBAL, 9),
    N(20, 10, GLOBAL, 10), N(20, 11, GLOBAL, 11), N(20, 12, GLOBAL, 12), N(20, 13, GLOBAL, 13), N(20, 14, GLOBAL, 14),
    N(20, 15, GLOBAL, 15), N(20, 16, GLOBAL, 16), N(20, 17, GLOBAL, 17),
    /* TUNING */
    N(21, 0, TUNING, 0), N(21, 1, TUNING, 1), N(21, 2, TUNING, 2), N(21, 3, TUNING, 3), N(21, 4, TUNING, 4),
    N(21, 5, TUNING, 5), N(21, 6, TUNING, 6), N(21, 7, TUNING, 7),
