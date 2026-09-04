/*
 * cr78_plugin.cpp — Schwung plugin_api_v2 wrapper for the CW-78 engine.
 *
 * Runs in-process inside the shim's SPI callback (SCHED_FIFO 90). render_block
 * therefore does no allocation, no file I/O and takes no locks — all of that
 * happens in create_instance.
 *
 * Structure follows 8W8's sc808_plugin.cpp, which follows 6W6's, which
 * follows 9W9's er99_plugin.c, on purpose: the kits should feel identical
 * under the hands, so the pad map, silent-select window, per-lane mutes,
 * pad-follow is the same mechanism with a CR-78 roster.
 * GPL-3.0.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "cr78_engine.h"
#include "cr78_params.h"
#include "cr78_rhythms.h"
#include "plugin_api_v1.h"   /* defines both v1 and v2 */

static const host_api_v1_t *g_host = NULL;

/*
 * Deployment instrumentation. The loadtest drives every pad through this same
 * plugin API and passes on the device, so when the module is silent in the
 * chain the fault is between the HOST and here — the sample rate it hands us,
 * the notes it forwards, or a state blob it pushes on load. None of that is
 * visible from a render. This logs it.
 *
 * Rate-limited hard: the first few of each kind and then nothing, so it costs
 * one predictable branch on the realtime path and cannot flood the log.
 */
#define CR78_DBG_MIDI   24
#define CR78_DBG_BLOCKS 8
static int g_dbg_midi = 0, g_dbg_block = 0;
static void dbg(const char *fmt, ...)
{
    if(!g_host || !g_host->log) return;
    char b[256]; va_list ap; va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    g_host->log(b);
}

/* Page ids from gen_params.py. Pad-follow publishes one of these. */
static const char *const kLevelOf[CR78_NUM_VOICES] = {
    "bd", "sd", "rs", "hh",
    "cy", "ma", "cl", "hb",
    "lb", "lc", "cb", "tb",
    "gu", "mb"
};

typedef struct {
    cr78_engine_t  *engine;
    char            module_dir[512];

    /* Pad-follow state. Deliberately NOT updated while the transport runs —
     * otherwise every sequenced note yanks the editor to a different drum and
     * you can never keep a page open while a pattern plays. */
    int             focus_voice;
    unsigned        focus_count;

    /* Last tempo pushed to the synced delay, so a param write only happens
     * when the BPM actually moves — it is constant for thousands of blocks at
     * a time and a write is not free. */
    float           last_bpm;

    /* Which bus page the FX pad shows next: 0 = Reverb, 1 = Delay. */
    int             fx_flip;

    /* ---- Preset rhythm playback ----
     * The CR-78's own patterns, on the machine's own 48-steps-to-the-bar
     * clock. With Mode off this fires nothing at all.
     *
     * Two clocks: phase-locked to the transport when it runs, free-running at
     * the host tempo when it does not — see the block in render_block. */
    int             rhy_last_step;
    int             rhy_prev_mode;
    double          rhy_free_samps;   /* free-run position, in samples */

    /* Silent-select support: while samples_rendered < mute_until, the first
     * incoming note is swallowed. The editor sets this just before re-injecting
     * a Shift+Pad press into Move, so Move updates its pad selection while CW-78
     * stays quiet. */
    uint64_t        samples_rendered;
    uint64_t        mute_until;
    int             mute_one;

    /* Lane the on-device editor is showing (0-13, 14 = master). The editor
     * WRITES it on every pad-follow; the remote panel reads it back through
     * the manager's change push. It has to be a set_param because the shim
     * only notifies the manager about writes — a value the DSP merely
     * computes (ui_focus_level) never reaches the browser. */
    char            ui_focus[16];
} cr78_instance_t;

/* ---- MIDI note map -------------------------------------------------- */
/*
 * Move's 32 pads are notes 68..99, laid out as 4 rows of 8, bottom-left first:
 *
 *   row 3 (top)    92 93 94 95 | 96 97 98 99
 *   row 2          84 85 86 87 | 88 89 90 91
 *   row 1          76 77 78 79 | 80 81 82 83
 *   row 0 (bottom) 68 69 70 71 | 72 73 74 75
 *                  ^---------^
 *                  drum block
 *
 * The kit lives in the LEFT block so it reads like a drum machine instead of
 * being smeared across the grid.
 *
 * FOURTEEN voices, so unlike 8W8 there is a MASTER PAD — pad 15, note 94 —
 * the way 9W9 and 6W6 have one, and pad 16 is left empty rather than filled
 * with a drum the CR-78 does not have. Master is still reachable by the jog
 * as well; the pad is the shortcut, not the only route.
 *
 * Bottom row is the machine's own front-panel order (BD SD RS HH); the noise
 * voices continue above, then the drums, then the two Add Voice oddities.
 */
#define CR78_PAD_MASTER  (-2)
#define CR78_PAD_FX      (-3)   /* pad 16: Reverb, then Delay, alternating */

static int pad_to_voice(const uint8_t _note)
{
    switch(_note)
    {
    /* row 0 */
    case 68: return CR78_BD;   case 69: return CR78_SD;
    case 70: return CR78_RS;   case 71: return CR78_HH;
    /* row 1 */
    case 76: return CR78_CY;   case 77: return CR78_MA;
    case 78: return CR78_CL;   case 79: return CR78_HB;
    /* row 2 */
    case 84: return CR78_LB;   case 85: return CR78_LC;
    case 86: return CR78_CB;   case 87: return CR78_TB;
    /* row 3 */
    case 92: return CR78_GU;   case 93: return CR78_MB;
    case 94: return CR78_PAD_MASTER;
    /* Pad 16 shows the send buses — Reverb on one press, Delay on the next.
     * A CR-78 has fourteen voices, so the block has two pads with no drum to
     * hold, and a dead pad helps nobody: 15 is Main, 16 is the FX pages. */
    case 95: return CR78_PAD_FX;
    default: return -1;
    }
}

/*
 * Drum-rack map. On a Move DRUM track the pads do not send raw pad notes —
 * they send the drum rack's own notes from 36 (C1), bottom-left first, 4 per
 * row. Same kit order, so the physical layout is identical whichever way the
 * notes arrive. Default, because it makes each drum a separately sequencable
 * lane on a Move drum track.
 *
 * Fourteen lanes, so 36..49. Notes 50 and 51 are the empty pad and Master and
 * deliberately trigger nothing.
 */
static int drumrack_to_voice(const uint8_t _note)
{
    if(_note >= 36 && _note <= 36 + CR78_NUM_VOICES - 1)
        return (int)(_note - 36);
    return -1;
}

/* note_map: 0 = drum rack (default), 1 = General MIDI */
static int g_note_map = 0;

static int note_to_voice(const uint8_t _note)
{
    const int pad = pad_to_voice(_note);
    if(pad != -1) return pad;

    if(g_note_map == 0) return drumrack_to_voice(_note);

    switch(_note)   /* GM drum map */
    {
    case 35: case 36: return CR78_BD;
    case 38: case 40: return CR78_SD;
    case 37:          return CR78_RS;   /* side stick */
    /* ONE hi-hat. GM's closed, pedal and open hats all land on it, because
     * that is the machine: there is no open hat to choke and no choke
     * control anywhere in this module. */
    case 42: case 44: case 46: return CR78_HH;
    case 49: case 51: case 52: case 55: case 57: case 59: return CR78_CY;
    case 70:          return CR78_MA;
    case 75:          return CR78_CL;
    case 60:          return CR78_HB;   /* hi bongo  */
    case 61:          return CR78_LB;   /* low bongo */
    /* ONE conga. GM's three conga notes all land on it, for the same reason
     * as the hats. */
    case 62: case 63: case 64: return CR78_LC;
    case 56:          return CR78_CB;
    case 54:          return CR78_TB;
    case 73: case 74: return CR78_GU;   /* short / long guiro */
    /* The metallic beat has no GM equivalent at all — it is a Roland idea.
     * The triangles are the closest thing in the map to a bright metal tick. */
    case 80: case 81: return CR78_MB;
    default:          return -1;
    }
}

/* ---- plugin_api_v2 -------------------------------------------------- */

static void *create_instance(const char *_module_dir, const char *_json_defaults)
{
    (void)_json_defaults;
    cr78_instance_t *inst = (cr78_instance_t *)calloc(1, sizeof(cr78_instance_t));
    if(!inst) return NULL;

    if(_module_dir)
        snprintf(inst->module_dir, sizeof(inst->module_dir), "%s", _module_dir);

    const float sr = (g_host && g_host->sample_rate > 0)
                   ? (float)g_host->sample_rate : 44100.0f;

    dbg("cw78: create_instance sample_rate=%d defaults=%s",
        (int)sr, _json_defaults ? "yes" : "none");
    inst->engine = cr78_create(sr);
    if(!inst->engine) { free(inst); return NULL; }
    inst->rhy_last_step = -1;
    inst->rhy_prev_mode = 0;
    inst->rhy_free_samps = -1.0;

    if(g_host && g_host->log) g_host->log("cw78: engine ready");
    return inst;
}

static void destroy_instance(void *_instance)
{
    cr78_instance_t *inst = (cr78_instance_t *)_instance;
    if(!inst) return;
    cr78_destroy(inst->engine);
    free(inst);
}

static void on_midi(void *_instance, const uint8_t *_msg, const int _len, const int _source)
{
    cr78_instance_t *inst = (cr78_instance_t *)_instance;
    if(!inst || _len < 3) return;

    const uint8_t status = _msg[0] & 0xF0u;
    const uint8_t note   = _msg[1];
    const uint8_t vel    = _msg[2];

    if(g_dbg_midi < CR78_DBG_MIDI)
    {
        ++g_dbg_midi;
        dbg("cw78: midi %02X %d %d src=%d -> voice %d (map=%s)",
            _msg[0], note, vel, _source, note_to_voice(note),
            g_note_map ? "GM" : "rack36");
    }

    /* Drums are one-shots: note-off is ignored, note-on with velocity 0 too. */
    if(status != 0x90 || vel == 0) return;

    const int v = note_to_voice(note);

    /* The two page pads select a page and play nothing. Unlike the drums
     * they follow even while the transport runs: they exist only to
     * navigate, so "don't yank the page mid-pattern" does not apply — the
     * press IS a navigation request. */
    if(v == CR78_PAD_MASTER || v == CR78_PAD_FX)
    {
        const int focus = (v == CR78_PAD_MASTER)
                        ? CR78_MASTER_PAD
                        : CR78_MASTER_PAD + 1 + (inst->fx_flip ^= 1, inst->fx_flip);
        /* 14 = Master, 15 = Reverb, 16 = Delay — the remote panel and the
         * manager read these off ui_focus. */
        inst->focus_voice = focus;
        inst->focus_count++;
        snprintf(inst->ui_focus, sizeof(inst->ui_focus), "%d", focus);
        return;
    }
    if(v < 0) return;

    /* Silent-select window: swallow (only) the first note that arrives —
     * that is the Shift+Pad press routed back through Move. */
    if(inst->samples_rendered < inst->mute_until && inst->mute_one)
    {
        inst->mute_one = 0;
        return;
    }

    cr78_trigger(inst->engine, v, (int)vel);

    /* Only a hand-played hit moves the editor. While the transport runs, notes
     * arrive constantly and following them makes the UI unusable. */
    const int clock = (g_host && g_host->get_clock_status)
                    ? g_host->get_clock_status() : MOVE_CLOCK_STATUS_STOPPED;
    if(clock != MOVE_CLOCK_STATUS_RUNNING)
    {
        inst->focus_voice = v;
        inst->focus_count++;
        /* Also the lane the remote panel follows. */
        snprintf(inst->ui_focus, sizeof(inst->ui_focus), "%d", v);
    }
}

static void set_param(void *_instance, const char *_key, const char *_val)
{
    cr78_instance_t *inst = (cr78_instance_t *)_instance;
    if(!inst || !_key || !_val) return;

    if(!strcmp(_key, "note_map"))
    {
        g_note_map = atoi(_val) != 0;
        /* Also stored in the engine's enum table so it survives a state cycle. */
        cr78_set_param(inst->engine, _key, _val);
        return;
    }
    if(!strcmp(_key, "ui_focus"))
    {
        snprintf(inst->ui_focus, sizeof(inst->ui_focus), "%s", _val);
        return;
    }
    if(!strcmp(_key, "mute_ms"))
    {
        const int ms = atoi(_val);
        const float sr = (g_host && g_host->sample_rate > 0)
                       ? (float)g_host->sample_rate : 44100.0f;
        inst->mute_until = inst->samples_rendered
                         + (uint64_t)((ms > 0 ? ms : 0) * 0.001f * sr);
        inst->mute_one = 1;
        return;
    }
    if(!strcmp(_key, "mutes"))
    {
        cr78_set_mutes(inst->engine, (unsigned)atoi(_val));
        return;
    }
    /* Slot autosave and preset recall both arrive here. */
    if(!strcmp(_key, "state"))
    {
        dbg("cw78: set state len=%d head=%.60s", (int)strlen(_val), _val);
        cr78_deserialize(inst->engine, _val);
        /* note_map lives in the engine's enum table; mirror it back out to the
         * file-static the note router reads. */
        {
            char b[16];
            if(cr78_get_param(inst->engine, "note_map", b, sizeof(b)) > 0)
                g_note_map = atoi(b) != 0;
        }
        /* A patch written before the sequencer was removed still carries a
         * "seq_<lane>=<mask>;" tail after the JSON. cr78_deserialize reads the
         * JSON and stops, so those keys are simply ignored and an old preset
         * loads exactly as it should. */
        return;
    }
    cr78_set_param(inst->engine, _key, _val);
}

static int get_param(void *_instance, const char *_key, char *_buf, const int _len)
{
    cr78_instance_t *inst = (cr78_instance_t *)_instance;
    if(!inst || !_key || !_buf || _len <= 0) return -1;

    /* Served dynamically: module.json is capped at 8 KB by the loader and the
     * parameter surface is larger than that — 115 params over 17 pages. */
    if(!strcmp(_key, "chain_params"))
    {
        if(_len <= CR78_CHAIN_PARAMS_LEN) return -1;   /* refuse to truncate */
        memcpy(_buf, cr78_chain_params_json, CR78_CHAIN_PARAMS_LEN + 1);
        return CR78_CHAIN_PARAMS_LEN;
    }
    /* ui_hierarchy is deliberately NOT served. enterComponentEdit prefers a
     * module's hierarchy and only falls back to loading the module's own
     * ui_chain.js when there isn't one. CW-78 ships ui_chain.js for the pad
     * gestures, so the hierarchy must stay absent here... */
    /*
     * SERVED, AND EMPTY — not an error. The host's component load gate reads
     * this key with three answers: JSON means "I declare a hierarchy", ""
     * means "served, and I have none — fall back to ui_chain.js now", and
     * null/error means "the read did not complete, ask again". Returning -1
     * is the third answer, so Swap Module INTO this module sat on the host's
     * "Loading..." card until the user pressed Back, forever. Reported from
     * hardware on 9W9 and identical here.
     *
     * The normal entry path is unchanged: getComponentHierarchy treats "" as
     * "no hierarchy" and loads ui_chain.js exactly as before.
     */
    if(!strcmp(_key, "ui_hierarchy"))
    {
        if(_len < 1) return -1;
        _buf[0] = 0;
        return 0;
    }
    /* ...and is published under a key the host does not probe, for
     * ui_chain.js to feed the shared param_pages controller. */
    if(!strcmp(_key, "ui_pages"))
    {
        if(_len <= CR78_UI_PAGES_LEN) return -1;
        memcpy(_buf, cr78_ui_pages_json, CR78_UI_PAGES_LEN + 1);
        return CR78_UI_PAGES_LEN;
    }

    /*
     * Pad-follow. Publishes "<trigger-count>:<page-id>" so the editor can jump
     * to the drum you just hit. The counter is what the UI watches: it only
     * navigates on a NEW hit, so browsing to another page by hand is never
     * yanked away underneath you.
     *
     * The Master pad publishes "root", which is the page id gen_params.py
     * gives the top level.
     */
    if(!strcmp(_key, "ui_focus_level"))
    {
        const int v = inst->focus_voice;
        if(inst->focus_count == 0) return -1;
        if(v == CR78_MASTER_PAD)
            return snprintf(_buf, (size_t)_len, "%u:root", inst->focus_count);
        if(v == CR78_MASTER_PAD + 1)
            return snprintf(_buf, (size_t)_len, "%u:rev", inst->focus_count);
        if(v == CR78_MASTER_PAD + 2)
            return snprintf(_buf, (size_t)_len, "%u:dly", inst->focus_count);
        if(v < 0 || v >= CR78_NUM_VOICES) return -1;
        return snprintf(_buf, (size_t)_len, "%u:%s", inst->focus_count, kLevelOf[v]);
    }

    if(!strcmp(_key, "ui_focus"))
        return snprintf(_buf, (size_t)_len, "%s", inst->ui_focus[0] ? inst->ui_focus : "0");
    if(!strcmp(_key, "clock_running"))
    {
        const int clock = (g_host && g_host->get_clock_status)
                        ? g_host->get_clock_status() : MOVE_CLOCK_STATUS_STOPPED;
        return snprintf(_buf, (size_t)_len, "%d",
                        clock == MOVE_CLOCK_STATUS_RUNNING ? 1 : 0);
    }
    if(!strcmp(_key, "mutes"))
        return snprintf(_buf, (size_t)_len, "%u", cr78_get_mutes(inst->engine));
    if(!strcmp(_key, "state"))
    {
        return cr78_serialize(inst->engine, _buf, _len);
    }

    return cr78_get_param(inst->engine, _key, _buf, _len);
}

static int get_error(void *_instance, char *_buf, const int _len)
{
    (void)_instance; (void)_buf; (void)_len;
    return 0;
}

static void render_block(void *_instance, int16_t *_out_lr, const int _frames)
{
    cr78_instance_t *inst = (cr78_instance_t *)_instance;
    if(!inst) { memset(_out_lr, 0, (size_t)_frames * 2 * sizeof(int16_t)); return; }

    inst->samples_rendered += (uint64_t)_frames;

    /* Tempo for the synced delay, pushed once per block and only on change. */
    if(g_host && g_host->get_bpm)
    {
        const float bpm = g_host->get_bpm();
        if(bpm > 20.0f && bpm != inst->last_bpm)
        {
            inst->last_bpm = bpm;
            char b[24];
            snprintf(b, sizeof(b), "%.4f", bpm);
            cr78_set_param(inst->engine, "dly_bpm", b);
        }
    }

    /*
     * The preset rhythms.
     *
     * Read the four enums once per BLOCK rather than caching them on writes:
     * they also arrive through cr78_deserialize on a state load, which does
     * not pass through this file's set_param, and a cache that misses that
     * plays the wrong pattern until the next knob move. Four table lookups
     * per 128 samples is nothing.
     */
    {
        char b[16];
        int mode = 0, button = 0, ab = 0;
        if(cr78_get_param(inst->engine, "rhy_mode",  b, sizeof b) > 0) mode   = atoi(b);
        if(cr78_get_param(inst->engine, "rhy_style", b, sizeof b) > 0) button = atoi(b);
        if(cr78_get_param(inst->engine, "rhy_ab",    b, sizeof b) > 0) ab     = atoi(b);
        /*
         * Style is a BUTTON, not a pattern, and the lever works on ALL
         * seventeen — that is where Roland's "34 preset rhythms" figure comes
         * from: 17 buttons x 2 lever positions. On the fourteen single-label
         * buttons A and B are two VARIATIONS of the style (the score's A and
         * B bars); on the three dual-label buttons they are two different
         * STYLES, and the chosen style plays its printed (A) bar.
         *
         * An earlier version had the lever dead on single buttons and picking
         * a measure of the wrong pattern on dual ones.
         */
        const int style   = cr78_resolve_pattern(button, ab);
        const int measure = cr78_button_is_dual(button) ? 0 : ab;

        /* A mode flip re-arms the stepper so Play always begins cleanly. */
        if(mode != inst->rhy_prev_mode)
        {
            inst->rhy_prev_mode = mode;
            inst->rhy_free_samps = -1.0;
            inst->rhy_last_step = -1;
        }

        /*
         * THE RHYTHM PLAYS ONLY WHILE THE TRANSPORT RUNS. Gus's spec, after
         * two shipped versions got this wrong in two different ways:
         *
         *   v1 followed get_beat_position() alone — which is clip-relative
         *   and can sit at -1 while the set is audibly playing — so Play was
         *   silent until the slot's clip state changed and the pattern came
         *   in "out of the blue".
         *
         *   v2 added a free-running clock for whenever bp < 0. Worse: bp is
         *   not guaranteed stable block-to-block, and every time it flapped
         *   between valid and -1 the two clocks disagreed about the current
         *   step, the != last-step guard passed on both, and the pattern
         *   MACHINE-GUNNED triggers at block rate. That is the "horrible
         *   noise" field report, verbatim.
         *
         * So: get_clock_status() is the gate — stopped means silent, full
         * stop. While RUNNING, the step comes from bp when bp is valid, and
         * from a sample counter that is RE-SYNCED TO bp on every valid block
         * — so if bp drops out mid-run the counter continues from the same
         * phase and the two sources can never disagree about the step.
         */
        const int rclock = (g_host && g_host->get_clock_status)
                         ? g_host->get_clock_status() : MOVE_CLOCK_STATUS_STOPPED;
        if(mode > 0 && rclock == MOVE_CLOCK_STATUS_RUNNING)
        {
            const cr78_rhythm_t *r = &g_cr78_rhythms[style];
            const int spb = r->stepsPerBar;

            const float bpm = (g_host && g_host->get_bpm && g_host->get_bpm() > 20.0f)
                            ? g_host->get_bpm() : 120.0f;
            const double sr = (g_host && g_host->sample_rate > 0)
                            ? (double)g_host->sample_rate : 44100.0;
            const double sampsPerTick = sr * 60.0 / ((double)bpm * 12.0);

            /* 12 ticks to the beat: a sixteenth is 3, a triplet eighth is 4,
             * both exact — one clock plays a shuffle and a disco pattern.
             * The waltz (36 to its bar) drifts against a 4/4 host bar; that
             * is what playing 3/4 in 4/4 means, not a bug. */
            const double bp = (g_host && g_host->get_beat_position)
                            ? g_host->get_beat_position() : -1.0;
            if(bp >= 0.0)
                inst->rhy_free_samps = bp * 12.0 * sampsPerTick;   /* re-sync */
            else if(inst->rhy_free_samps < 0.0)
                inst->rhy_free_samps = 0.0;         /* running, bp not valid yet */
            else
                inst->rhy_free_samps += (double)_frames;

            int step = (int)(inst->rhy_free_samps / sampsPerTick) % spb;
            if(step < 0) step += spb;

            if(step != inst->rhy_last_step)
            {
                inst->rhy_last_step = step;
                const cr78_hit_t    *hits = measure ? r->b    : r->a;
                const int            n    = measure ? r->bN   : r->aN;
                const unsigned char *acc  = measure ? r->bAcc : r->aAcc;
                const int            accN = measure ? r->bAccN: r->aAccN;

                /* Accent is a per-STEP channel — one CV into the BA662 that
                 * lifts whatever happens to be sounding. */
                int accented = 0;
                for(int i = 0; i < accN; ++i)
                    if((int)acc[i] == step) { accented = 1; break; }
                const int vel = accented ? 127 : 88;

                for(int i = 0; i < n; ++i)
                    if((int)hits[i].step == step)
                        cr78_trigger(inst->engine, (int)hits[i].voice, vel);
            }
        }
        else
        {
            inst->rhy_last_step = -1;
            inst->rhy_free_samps = -1.0;
        }
    }

    /* Stack scratch: no allocation on the realtime path. Schwung's block is
     * 128 frames; guard anyway. */
    float mono[512];
    const int cap = (int)(sizeof(mono) / sizeof(mono[0]));
    int done = 0;
    while(done < _frames)
    {
        int chunk = _frames - done;
        if(chunk > cap) chunk = cap;

        cr78_render(inst->engine, mono, chunk);

        if(g_dbg_block < CR78_DBG_BLOCKS)
        {
            float pk = 0.0f;
            for(int i = 0; i < chunk; ++i)
            { const float a = mono[i] < 0 ? -mono[i] : mono[i]; if(a > pk) pk = a; }
            if(pk > 0.0f)
            { ++g_dbg_block; dbg("cw78: render peak %.4f over %d frames", pk, chunk); }
        }

        for(int i = 0; i < chunk; ++i)
        {
            float v = mono[i];
            if(v >  1.0f) v =  1.0f;
            if(v < -1.0f) v = -1.0f;
            const int16_t s = (int16_t)(v * 32767.0f);
            _out_lr[(done + i) * 2 + 0] = s;
            _out_lr[(done + i) * 2 + 1] = s;
        }
        done += chunk;
    }
}

static plugin_api_v2_t g_api;

extern "C" plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *_host)
{
    g_host = _host;
    /* Field-by-field, not a designated initialiser: C++14, and designated
     * initialisers are C++20. */
    g_api.api_version      = 2;
    g_api.create_instance  = create_instance;
    g_api.destroy_instance = destroy_instance;
    g_api.on_midi          = on_midi;
    g_api.set_param        = set_param;
    g_api.get_param        = get_param;
    g_api.get_error        = get_error;
    g_api.render_block     = render_block;
    return &g_api;
}
