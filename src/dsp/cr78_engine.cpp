/*
 * cr78_engine.cpp — see cr78_engine.h.
 *
 * GPL-3.0. The voice circuits under cr78_*_circuit.h are built from the
 * Roland CR-78 service notes; the module shell (drive characters, send buses,
 * bus glue, parameter plumbing) is ported from 9W9/6W6/8W8. See THIRD_PARTY.md.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cr78_circuit.h"
#include "cr78_drum_circuit.h"
#include "cr78_tank_circuit.h"
#include "cr78_noise_circuit.h"
#include "cr78_sd_circuit.h"
#include "cr78_metal_circuit.h"
#include "cr78_perc_circuit.h"
#include "cr78_engine.h"
#include "cr78_params.h"
#include "cr78_fx.h"
#include "cr78_shape.h"

using namespace cr78;

/*
 * 1: the first shipped surface. There is no migration table below and there
 *    should not be one — nothing has ever been saved against an earlier
 *    layout. From here the pot and enum tables are POSITIONAL and
 *    APPEND-ONLY, exactly as 8W8's are, and the first control removed from
 *    the middle is the day this file grows a kV1PotKeys.
 */
#define CR78_STATE_VERSION 1

/* A mute is a 2 ms fade, not a hard stop — cutting a ringing cymbal dead puts
 * a click where the tail was. */
static const float kChokeSeconds = 0.002f;

namespace {

constexpr const char *kVoiceIds[CR78_NUM_VOICES] = {
    "bd", "sd", "rs", "hh",
    "cy", "ma", "cl", "hb",
    "lb", "lc", "cb", "tb",
    "gu", "mb"
};

/*
 * Which lanes have a NOTE and which have only a ratio.
 *
 * The eight pitched lanes carry their frequency in the voice circuit itself —
 * the bass drum's 62.5 Hz, the rim shot's 1480, and so on, all straight from
 * page 30's factory alignment table — so the Tune pot here is a SEMITONE
 * OFFSET around that and pot 64 is a correctly aligned CR-78.
 *
 * The other six have no note to offset. The hi-hat, cymbal and maracas are
 * filtered noise; the metallic beat is three fixed oscillators; the guiro is
 * a scrape rate; the tambourine is a tank with jingles under it. Their Tune
 * is a frequency RATIO on the whole voice, unity at pot centre.
 */
constexpr bool kIsPitched[CR78_NUM_VOICES] = {
    true,  true,  true,  false,   /* bd sd rs | hh */
    false, false, true,  true,    /* cy ma    | cl hb */
    true,  true,  true,  false,   /* lb lc cb | tb */
    false, false                  /* gu mb */
};

/*
 * Kit balance.
 *
 * THIS WAS WRONG UNTIL IT WAS PUT ON HARDWARE, and the mistake is worth
 * keeping written down because it was a confident one.
 *
 * Page 30 of the service notes ends with an amplitude column — 0.4 Vpp for
 * the bass drum, 0.8 for the rim shot, 0.15 for the bongos — and this file
 * used it as the kit balance, on the grounds that it was Roland's own numbers
 * rather than someone's taste. It is not the mix. It is an ALIGNMENT SPEC:
 * page 30 tells the engineer to put a scope on the voice board and trim each
 * voice to that amplitude AT ITS OWN OUTPUT, one at a time. What reaches the
 * summing amp is that voltage through the channel's output resistor, and
 * those differ by more than two orders of magnitude — the bass drum leaves
 * through R640 at 10 k and the hi bongo through R610 at 1.5 M.
 *
 * Used as a mix it puts the kick level with sixteenth-note maracas and
 * quieter than the rim shot. On the Move that reads as a kit with no kick at
 * all: the bass drum is 62.5 Hz with 97% of its energy under 120 Hz, the
 * device's speaker gives nothing back down there, and a few dB is the
 * difference between "quiet" and "absent".
 *
 * So the balance below is a MIX, written down and defended, the way 8W8's
 * kVoicing is — in dB relative to the bass drum, which is the reference
 * because on a drum machine it has to be:
 *
 *   the rim shot and the claves are CLICKS with a high crest factor; matching
 *   their peaks to a kick's makes them scream. They sit well down.
 *
 *   the maracas play SIXTEENTHS in half the presets. At the kick's level a
 *   bossa nova is a maracas record. -13 is what lets the pattern read.
 *
 *   the toms, congas and bongos sit just under the snare, which is where a
 *   CR-78's own recordings put them.
 *
 * The factory Vpp column is kept below, unused by the balance, because it is
 * still the right thing for voice_check to hold each lane to — it is a
 * per-voice specification and it is correct as one.
 */
constexpr float kMixVoicing[CR78_NUM_VOICES] = {
      0.0f,  /* bd — the reference */
      0.0f,  /* sd */
     -4.0f,  /* rs — a 5 ms click */
     -5.0f,  /* hh */
     -4.0f,  /* cy */
     -9.0f,  /* ma — plays sixteenths */
     -6.0f,  /* cl — a click */
     -3.0f,  /* hb */
     -3.0f,  /* lb */
     -2.0f,  /* lc */
     -4.0f,  /* cb */
     -5.0f,  /* tb */
     -4.0f,  /* gu */
     -4.0f,  /* mb */
};

/*
 * Page 30's alignment column. NOT the balance — see above. Kept because
 * voice_check holds each lane to it as a per-voice specification, which is
 * what it actually is.
 */
constexpr float kFactoryVpp[CR78_NUM_VOICES] = {
    0.40f,  /* bd */
    0.40f,  /* sd */
    0.80f,  /* rs */
    0.40f,  /* hh */
    0.40f,  /* cy */
    0.40f,  /* ma */
    0.15f,  /* cl */
    0.15f,  /* hb */
    0.15f,  /* lb */
    0.30f,  /* lc */
    0.20f,  /* cb */
    0.25f,  /* tb */
    0.30f,  /* gu */
    0.35f,  /* mb */
};

/* Filled by tools/kit_check. See the comment above kMixVoicing. */
constexpr float kVoiceTrim[CR78_NUM_VOICES] = {
    0.3506f,  /* bd */
    0.2671f,  /* sd */
    1.3518f,  /* rs */
    0.8798f,  /* hh */
    0.6671f,  /* cy */
    0.6780f,  /* ma */
    5.5085f,  /* cl */
    0.1116f,  /* hb */
    0.1125f,  /* lb */
    0.1793f,  /* lc */
    0.7082f,  /* cb */
    1.5539f,  /* tb */
    8.1630f,  /* gu */
    0.4953f,  /* mb */
};

/*
 * A short initialiser list on a sized array does not warn — C zero-fills the
 * tail — and a zero trim is a SILENT LANE. 8W8 lost a cymbal to exactly this
 * when a sixteenth voice was added and the table kept fifteen entries. A
 * compile error is cheaper than a render.
 */
constexpr bool trim_table_filled(const int i = 0)
{
    return i >= CR78_NUM_VOICES
         ? true
         : (kVoiceTrim[i] > 0.0f && kFactoryVpp[i] > 0.0f
            && trim_table_filled(i + 1));
}
static_assert(trim_table_filled(),
              "kVoiceTrim or kFactoryVpp has a zero entry: a short "
              "initialiser list zero-fills the tail, and a zero trim "
              "silences that lane");

constexpr bool voice_ids_filled(const int i = 0)
{
    return i >= CR78_NUM_VOICES
         ? true
         : (kVoiceIds[i] != nullptr && kVoiceIds[i][0] != '\0'
            && voice_ids_filled(i + 1));
}
static_assert(voice_ids_filled(), "kVoiceIds is short of CR78_NUM_VOICES");

/* Per-voice pot/enum slots, resolved once at create time so the audio path
 * never searches by string. */
struct VoiceSlots {
    int tune, decay, drive, level;   /* every voice has these four */
    int dist;                        /* enum slot                  */
    /* Send amounts. -1 on the bass drum, which is dry by design — see SENDS
     * in gen_params.py. */
    int rev, dly;
};

struct VoiceRt {
    float hit_gain;      /* this hit's velocity gain            */
    float choke_gain;    /* 1.0 normally, ramps to 0 on a mute  */
    float choke_step;    /* < 0 while fading, else 0            */
    /* Crush's sample-and-hold. Per lane, because one shared state would put
     * the hi-hat's decimator steps on the bass drum. */
    float crush_st[CR78_CRUSH_STATE];
};

int find_pot(const char *key)
{
    for(int i = 0; i < CR78_NUM_POTS; ++i)
        if(!strcmp(g_cr78_pots[i].key, key)) return i;
    return -1;
}

int find_enum(const char *key)
{
    for(int i = 0; i < CR78_NUM_ENUMS; ++i)
        if(!strcmp(g_cr78_enums[i].key, key)) return i;
    return -1;
}

/* pot position 0..127 -> engineering value.
 * EXP: value = min * (max/min)^(pot/127). Fine control at the bottom of a
 * time or frequency range, where the ear actually is. */
float pot_value(int slot, int pot)
{
    const cr78_pot_t &p = g_cr78_pots[slot];
    const float t = (float)pot / 127.0f;
    if(p.curve == CR78_EXP && p.min > 0.0f)
        return p.min * powf(p.max / p.min, t);
    return p.min + (p.max - p.min) * t;
}

} /* namespace */

struct cr78_engine {
    float sample_rate;

    int   pot[CR78_NUM_POTS];        /* raw 0..127, the stored form   */
    float potv[CR78_NUM_POTS];       /* resolved, recomputed on write */
    int   env[CR78_NUM_ENUMS];       /* enum selections               */

    VoiceSlots slot[CR78_NUM_VOICES];
    VoiceRt    rt[CR78_NUM_VOICES];

    /* Voice-specific extras that only some lanes have. */
    int sd_snappy, gu_rate;
    /* Globals. */
    int e_master_dist, e_note_map, e_hat_choke;
    int p_master_drive, p_volume, p_vel_depth, p_comp;
    float crush_master[CR78_CRUSH_STATE];

    /* The two send buses and the bus glue. Heap-allocated with the engine —
     * the delay line alone is 352 KB, which is why cr78_create callocs rather
     * than putting an engine on a stack anywhere. */
    cr78_verb_t verb;
    cr78_dly_t  dly;
    cr78_glue_t glue;
    int p_rev_decay, p_rev_tone, p_rev_hpf, p_rev_level;
    int p_dly_fdbk, p_dly_tone, p_dly_hpf, p_dly_level;
    int e_dly_time;
    /* what the FX structs were last told, so sync only touches them on a real
     * change — resetting a biquad every block would tick the reverb with a
     * filter that never settles */
    float fx_rev_hpf, fx_dly_hpf;
    int   fx_divi;
    float fx_bpm;

    unsigned mutes;

    /*
     * The voice circuits. Four bridged-T drum channels, two passive tanks,
     * three noise lanes, the two-part snare, the metal pair and the two
     * Add Voice oddities.
     */
    DrumVoice       bdv, lcv, lbv, hbv;
    TankVoice       rsv, clv;
    NoiseVoice      hhv, cyv, mav;
    SnareVoice      sdv;
    MetalBeatVoice  mbv;
    CowbellVoice    cbv;
    GuiroVoice      guv;
    TambourineVoice tbv;

    /*
     * ONE noise source for the whole machine, ticked once per sample.
     *
     * Q533 is a single transistor on the VG-11 bussed to the hi-hat, the
     * cymbal, the maracas, the tambourine and the snare's snap through
     * R566 470k. It is shared on the hardware, so it is shared here: a hat
     * and a maracas landing on the same sixteenth hear the SAME noise, and
     * that correlation is audible.
     */
    NoiseSource noise;
};

/*
 * Push the pot table's engineering values into the FX structs.
 *
 * Called at create and after every parameter write. The cheap fields are
 * copied unconditionally; the two that COST something — the send highpass
 * biquads and the delay's retime — are only touched when their input actually
 * moved. Rebuilding a biquad every block is not free, and resetting one would
 * tick the reverb with a filter that never settles.
 */
static void cr78_fx_sync(cr78_engine_t *e)
{
    e->verb.decay = e->potv[e->p_rev_decay];
    e->verb.tone  = e->potv[e->p_rev_tone];
    e->verb.level = e->potv[e->p_rev_level];
    e->dly.fdbk   = e->potv[e->p_dly_fdbk];
    e->dly.tone   = e->potv[e->p_dly_tone];
    e->dly.level  = e->potv[e->p_dly_level];

    const float rh = e->potv[e->p_rev_hpf];
    if(rh != e->fx_rev_hpf)
    {
        e->verb.hpf_hz = rh;
        e->verb.hp.setHiPass(rh, kCR78_SendRQ, e->sample_rate);
        e->fx_rev_hpf = rh;
    }
    const float dh = e->potv[e->p_dly_hpf];
    if(dh != e->fx_dly_hpf)
    {
        e->dly.hpf_hz = dh;
        e->dly.hp.setHiPass(dh, kCR78_SendRQ, e->sample_rate);
        e->fx_dly_hpf = dh;
    }

    const int divi = e->env[e->e_dly_time];
    if(divi != e->fx_divi || e->dly.bpm != e->fx_bpm)
    {
        e->dly.divi = divi;
        cr78_dly_retime(&e->dly, e->sample_rate);
        e->fx_divi = divi;
        e->fx_bpm  = e->dly.bpm;
    }
}

const char *cr78_voice_id(int voice)
{
    return (voice >= 0 && voice < CR78_NUM_VOICES) ? kVoiceIds[voice] : "";
}

cr78_engine_t *cr78_create(float sample_rate)
{
    /*
     * calloc, and that has a consequence worth knowing before you add a voice.
     *
     * NO CONSTRUCTOR RUNS. Every member of every voice object in here is zero
     * bytes, and C++ default member initialisers — `double sr_ = 44100.0;`
     * and friends — are silently skipped. A class that relies on one to be
     * usable before its own init() is called will be quietly wrong rather
     * than obviously broken. 8W8 lost a day to exactly this: a voice called
     * reset() where it needed init(), kept a zeroed coefficient, and returned
     * silence.
     *
     * So: every voice's init() must set everything it needs, explicitly.
     */
    cr78_engine_t *e = (cr78_engine_t *)calloc(1, sizeof(cr78_engine_t));
    if(!e) return NULL;
    e->sample_rate = sample_rate > 0.0f ? sample_rate : 44100.0f;

    for(int i = 0; i < CR78_NUM_POTS; ++i)
    {
        e->pot[i]  = g_cr78_pots[i].def;
        e->potv[i] = pot_value(i, e->pot[i]);
    }
    for(int i = 0; i < CR78_NUM_ENUMS; ++i)
        e->env[i] = g_cr78_enums[i].def;

    /* Resolve every key once. A miss here is a generator/engine mismatch and
     * the loadtest asserts on it rather than letting it degrade silently. */
    char key[64];
    for(int v = 0; v < CR78_NUM_VOICES; ++v)
    {
        const char *id = kVoiceIds[v];
        snprintf(key, sizeof(key), "%s_tune",      id); e->slot[v].tune  = find_pot(key);
        snprintf(key, sizeof(key), "%s_decay",     id); e->slot[v].decay = find_pot(key);
        snprintf(key, sizeof(key), "%s_drive",     id); e->slot[v].drive = find_pot(key);
        snprintf(key, sizeof(key), "%s_level",     id); e->slot[v].level = find_pot(key);
        snprintf(key, sizeof(key), "%s_dist_type", id); e->slot[v].dist  = find_enum(key);
        /* The bass drum declares no sends, so find_pot returns -1 and the
         * render loop skips it. That is the whole of "the kick is dry". */
        snprintf(key, sizeof(key), "%s_rev",       id); e->slot[v].rev   = find_pot(key);
        snprintf(key, sizeof(key), "%s_dly",       id); e->slot[v].dly   = find_pot(key);
        e->rt[v].hit_gain   = 1.0f;
        e->rt[v].choke_gain = 1.0f;
        e->rt[v].choke_step = 0.0f;
    }
    e->sd_snappy = find_pot("sd_snappy");
    e->gu_rate   = find_pot("gu_rate");
    e->p_master_drive = find_pot("master_drive");
    e->p_volume       = find_pot("volume");
    e->p_vel_depth    = find_pot("vel_depth");
    e->p_comp         = find_pot("comp");
    e->p_rev_decay    = find_pot("rev_decay");
    e->p_rev_tone     = find_pot("rev_tone");
    e->p_rev_hpf      = find_pot("rev_hpf");
    e->p_rev_level    = find_pot("rev_level");
    e->p_dly_fdbk     = find_pot("dly_fdbk");
    e->p_dly_tone     = find_pot("dly_tone");
    e->p_dly_hpf      = find_pot("dly_hpf");
    e->p_dly_level    = find_pot("dly_level");
    e->e_dly_time     = find_enum("dly_time");
    e->e_master_dist  = find_enum("master_dist");
    e->e_note_map     = find_enum("note_map");
    e->e_hat_choke    = find_enum("hat_choke");

    const double sr = e->sample_rate;
    e->noise.init();
    e->bdv.init(kDRUM_BD, sr);
    e->lcv.init(kDRUM_LC, sr);
    e->lbv.init(kDRUM_LB, sr);
    e->hbv.init(kDRUM_HB, sr);
    e->rsv.init(kTANK_RS, sr);
    e->clv.init(kTANK_CL, sr);
    /* The cymbal's tank frequency is DERIVED from L3 and C521 — passed in
     * rather than copied into the spec table, so the derivation stays visible
     * at the call site. */
    e->cyv.init(kNOISE_CY, sr, cymbalTankFreq());
    e->hhv.init(kNOISE_HH, sr);
    e->mav.init(kNOISE_MA, sr);
    e->sdv.init(sr);
    e->mbv.init(sr);
    e->cbv.init(sr);
    e->guv.init(sr);
    e->tbv.init(sr);

    /* The FX read their engineering values from the pot table; sync pushes
     * them across and builds the send filters. The delay's read pointer is
     * seeded at its target so the first note does not sweep in from zero. */
    e->fx_bpm = 120.0f;
    e->dly.bpm = 120.0f;
    e->fx_rev_hpf = -1.0f; e->fx_dly_hpf = -1.0f; e->fx_divi = -1;
    cr78_fx_sync(e);
    cr78_verb_init(&e->verb, (float)sr);
    cr78_dly_init(&e->dly, (float)sr);
    e->dly.dcur = e->dly.time_ms * 0.001f * (float)sr;
    e->glue.env_db = 0.0f; e->glue.det = 0.0f;
    return e;
}

void cr78_destroy(cr78_engine_t *e) { free(e); }

void cr78_set_mutes(cr78_engine_t *e, unsigned mask)
{
    e->mutes = mask & ((1u << CR78_NUM_VOICES) - 1u);
    for(int v = 0; v < CR78_NUM_VOICES; ++v)
    {
        if(e->mutes & (1u << v))
        {
            /* Fade rather than cut. The lane keeps rendering through the
             * ramp; it drops out of the mix only once the gain reaches zero. */
            if(e->rt[v].choke_gain > 0.0f && e->rt[v].choke_step == 0.0f)
                e->rt[v].choke_step = -1.0f / (kChokeSeconds * e->sample_rate);
        }
        else if(e->rt[v].choke_step < 0.0f)
        {
            /* Unmuted mid-fade: stop fading, but do not resurrect the tail —
             * the lane comes back on its next hit. */
            e->rt[v].choke_step = 0.0f;
        }
    }
}

unsigned cr78_get_mutes(const cr78_engine_t *e) { return e->mutes; }

/*
 * Cut a lane that is still ringing. A 2 ms FADE and a dump of the voice's
 * envelope capacitor, never a hard stop — cutting a ringing tambourine dead
 * puts a click on the front of whatever cut it, which is the one thing a
 * choke must not do.
 *
 * MOD. A CR-78 has one hi-hat and nothing on the board chokes anything; see
 * the note above the preset rhythms in gen_params.py for why this exists and
 * why it defaults to off.
 */
static void choke_lane(cr78_engine_t *e, int v)
{
    if(e->rt[v].choke_gain > 0.0f && e->rt[v].choke_step == 0.0f)
        e->rt[v].choke_step = -1.0f / (kChokeSeconds * e->sample_rate);
    switch(v)
    {
    case CR78_HH: e->hhv.choke(); break;
    case CR78_MA: e->mav.choke(); break;
    case CR78_TB: e->tbv.choke(); break;
    default: break;
    }
}

void cr78_trigger(cr78_engine_t *e, int voice, int velocity)
{
    if(voice < 0 || voice >= CR78_NUM_VOICES) return;
    if(velocity <= 0) return;                       /* note-off: one-shots */
    if(e->mutes & (1u << voice)) return;

    /*
     * VELOCITY, and on this machine it reaches the voices by BOTH of the two
     * paths the hardware actually has:
     *
     *   the TRIGGER VOLTAGE into each lane's front-end diode, where a harder
     *   hit is a slightly different sound and not only a louder one — this is
     *   what the 0.027 uF / 270 k / diode network on all fourteen lanes does;
     *
     *   and the BA662 VCA, which on a CR-78 is exactly where ACCENT lives —
     *   VR104 50k(B) feeding one panel-wide control voltage into IC503. The
     *   808's per-voice accent switch is not what this machine has, and the
     *   CR-78's arrangement is simply a gain.
     *
     * One number drives both, so Velocity at zero means every hit is
     * identical in level AND in timbre — not level-flat with the tone still
     * moving underneath, which is what feeding the trigger voltage separately
     * would give. At full, both follow the played velocity.
     */
    const int   vi = velocity > 127 ? 127 : velocity;
    const float vn = 1.0f - e->potv[e->p_vel_depth]
                            * (1.0f - (float)vi * (1.0f / 127.0f));

    /*
     * The gain path takes over only BELOW the voltage path's useful floor.
     *
     * Both paths are real — the trigger voltage into the front-end diode and
     * the BA662's accent CV — but applying the full velocity number to both
     * DOUBLE-COUNTS it: the diode's knee makes the voltage path steeper than
     * linear, and multiplying the VCA gain on top left a velocity-110 pad hit
     * 2.6 dB under full where 8W8 sits at 1.3, which reads as "this kit is
     * quieter" on every pad ever played by hand. Above the floor the voltage
     * carries velocity alone (a harder hit is a different sound); below it
     * the voltage runs out of authority and the gain carries the rest down.
     */
    const float gainPart = vn < 0.70f ? vn * (1.0f / 0.70f) : 1.0f;

    e->rt[voice].hit_gain   = gainPart;   /* the VCA / accent path */
    e->rt[voice].choke_gain = 1.0f;
    e->rt[voice].choke_step = 0.0f;

    /*
     * The hat choke group. Off / MA-TB / all three.
     *
     * Applied AFTER this lane's own gate is reset above, so a lane can never
     * choke itself on a retrigger — hitting the maracas twice fast has to
     * leave the second hit alone.
     */
    switch(e->env[e->e_hat_choke])
    {
    case 1:   /* MA/TB — the hi-hat is already the short one, leave it out */
        if(voice == CR78_MA) choke_lane(e, CR78_TB);
        if(voice == CR78_TB) choke_lane(e, CR78_MA);
        break;
    case 2:   /* All3 — one exclusive group */
        if(voice == CR78_HH || voice == CR78_MA || voice == CR78_TB)
        {
            if(voice != CR78_HH) choke_lane(e, CR78_HH);
            if(voice != CR78_MA) choke_lane(e, CR78_MA);
            if(voice != CR78_TB) choke_lane(e, CR78_TB);
        }
        break;
    default: break;
    }

    const VoiceSlots &s = e->slot[voice];
    const float decay = e->potv[s.decay];
    const float tune  = e->potv[s.tune];
    /* Pitched lanes carry Tune as semitones around their factory frequency;
     * the other six carry it as a ratio already. */
    const double ratio = kIsPitched[voice] ? pow(2.0, (double)tune / 12.0)
                                           : (double)tune;

    switch(voice)
    {
    /*
     * The four bridged-T drum channels. Decay is RING TIME IN SECONDS and the
     * circuit solves the loop gain that produces it at the current pitch, so
     * the knob keeps its meaning when Tune moves.
     */
    case CR78_BD: e->bdv.setTune(ratio); e->bdv.setDecay(decay); e->bdv.trigger(vn); break;
    case CR78_LC: e->lcv.setTune(ratio); e->lcv.setDecay(decay); e->lcv.trigger(vn); break;
    case CR78_LB: e->lbv.setTune(ratio); e->lbv.setDecay(decay); e->lbv.trigger(vn); break;
    case CR78_HB: e->hbv.setTune(ratio); e->hbv.setDecay(decay); e->hbv.trigger(vn); break;

    /* The two passive tanks. Decay IS the tank's Q — there is no loop here
     * and nothing to sustain them. */
    case CR78_RS: e->rsv.setTune(ratio); e->rsv.setDecay(decay); e->rsv.trigger(vn); break;
    case CR78_CL: e->clv.setTune(ratio); e->clv.setDecay(decay); e->clv.trigger(vn); break;

    /* The three noise lanes. Decay is the envelope capacitor. */
    case CR78_HH: e->hhv.setTune(ratio); e->hhv.setDecay(decay); e->hhv.trigger(vn); break;
    case CR78_CY: e->cyv.setTune(ratio); e->cyv.setDecay(decay); e->cyv.trigger(vn); break;
    case CR78_MA: e->mav.setTune(ratio); e->mav.setDecay(decay); e->mav.trigger(vn); break;

    case CR78_SD:
        /* One Decay knob drives both halves; the snap keeps its hardware
         * proportion to the shell so the balance does not walk. */
        e->sdv.setTune(ratio);
        e->sdv.setDecay(decay);
        e->sdv.setSnappy((float)e->pot[e->sd_snappy] / 127.0f);
        e->sdv.trigger(vn);
        break;

    case CR78_CB: e->cbv.setTune(ratio); e->cbv.setDecay(decay); e->cbv.trigger(vn); break;
    case CR78_MB: e->mbv.setTune(ratio); e->mbv.setDecay(decay); e->mbv.trigger(vn); break;
    case CR78_TB: e->tbv.setTune(ratio); e->tbv.setDecay(decay); e->tbv.trigger(vn); break;

    case CR78_GU:
        e->guv.setTune(ratio);
        e->guv.setDecay(decay);
        /* The scrape RATE, between the hardware's own 77 and 125 Hz. */
        e->guv.setRate(e->potv[e->gu_rate]);
        e->guv.trigger(vn);
        break;

    default: break;
    }
}

/* One sample from one lane, through its own drive stage. */
static inline float voice_sample(cr78_engine *e, int v, float raw)
{
    VoiceRt &r = e->rt[v];
    if(r.choke_step < 0.0f)
    {
        r.choke_gain += r.choke_step;
        if(r.choke_gain <= 0.0f) { r.choke_gain = 0.0f; r.choke_step = 0.0f; }
    }
    if(r.choke_gain <= 0.0f) return 0.0f;

    const VoiceSlots &s = e->slot[v];
    /*
     * Trim goes BEFORE the drive stage, not after.
     *
     * The lanes arrive at very different levels, so a trim applied after the
     * shaper would leave Drive meaning something different on every pad.
     * Trimming first makes Drive 64 mean the same thing kit-wide, and it lets
     * the diode stage do what a diode stage is for: catching the spiky
     * voices. The rim shot is almost pure click and would otherwise have had
     * to sit at a huge post-drive number to balance.
     */
    const float trimmed = raw * kVoiceTrim[v];
    const float shaped = cr78_shape_st(trimmed, e->potv[s.drive],
                                       e->env[s.dist], r.crush_st);
    return shaped * e->potv[s.level] * r.hit_gain * r.choke_gain;
}

/*
 * One lane into the dry mix and the two send buses.
 *
 * The sends are POST-FADER — taken from the sample after Drive, Level,
 * velocity and the mute fade — because what you hear is what you should send.
 * A pre-fader send would keep feeding the reverb from a lane you had just
 * turned down, which is not what a send knob means on any desk.
 */
static inline void cr78_add(cr78_engine *e, const int v, const float raw,
                            float *mix, float *send_r, float *send_d)
{
    const float sv = voice_sample(e, v, raw);
    *mix += sv;
    const VoiceSlots &s = e->slot[v];
    if(s.rev >= 0) *send_r += sv * e->potv[s.rev];
    if(s.dly >= 0) *send_d += sv * e->potv[s.dly];
}

/* A silent lane must not cost a process() call. Guarding on the gate gain
 * also means a muted lane keeps rendering until its fade completes, then
 * disappears. */
#define CR78_LANE(vid, obj) \
    do { if(e->rt[vid].choke_gain > 0.0f && e->obj.active()) \
             cr78_add(e, vid, (float)e->obj.process(), &mix, &send_r, &send_d); \
       } while(0)

/* The four lanes that read the shared noise bus. */
#define CR78_NLANE(vid, obj) \
    do { if(e->rt[vid].choke_gain > 0.0f && e->obj.active()) \
             cr78_add(e, vid, (float)e->obj.process(nz), &mix, &send_r, &send_d); \
       } while(0)

void cr78_render(cr78_engine_t *e, float *out, int frames)
{
    const int   mdist  = e->env[e->e_master_dist];
    const float mdrive = e->potv[e->p_master_drive];
    const float vol    = e->potv[e->p_volume];
    const float comp   = e->potv[e->p_comp];

    for(int i = 0; i < frames; ++i)
    {
        float mix = 0.0f, send_r = 0.0f, send_d = 0.0f;

        /*
         * The shared noise source ticks ONCE per sample, always — one Q533 on
         * the VG-11 feeding every lane that wants noise. Ticking it per voice
         * would decorrelate the hi-hat from the snare's snap, which is
         * exactly the thing the hardware does not do.
         */
        const double nz = e->noise.process();

        CR78_LANE (CR78_BD, bdv);
        CR78_NLANE(CR78_SD, sdv);      /* shell + the shared noise's snap */
        CR78_LANE (CR78_RS, rsv);
        CR78_NLANE(CR78_HH, hhv);
        CR78_NLANE(CR78_CY, cyv);
        CR78_NLANE(CR78_MA, mav);
        CR78_LANE (CR78_CL, clv);
        CR78_LANE (CR78_HB, hbv);
        CR78_LANE (CR78_LB, lbv);
        CR78_LANE (CR78_LC, lcv);
        CR78_LANE (CR78_CB, cbv);
        CR78_NLANE(CR78_TB, tbv);
        CR78_LANE (CR78_GU, guv);
        CR78_LANE (CR78_MB, mbv);

        /*
         * The wet returns join the bus BEFORE the master stages, so master
         * distortion and the glue work on the whole picture rather than on a
         * dry kit with the FX bolted on afterwards.
         *
         * Both are ticked unconditionally, never branched around on a zero
         * input: a send turned down while a tail is still ringing has to let
         * that tail finish. Bit-identity with the sends at zero does not come
         * from a bypass — it comes from a silent-state tick fed exactly 0.0
         * returning exactly 0.0, which golden_check is what proves.
         */
        mix += cr78_verb_tick(&e->verb, send_r);
        mix += cr78_dly_tick(&e->dly, send_d, e->sample_rate);

        /* Master stage. Option 0 is Off, so the kit can be left alone. */
        if(mdist > 0) mix = cr78_shape_st(mix, mdrive, mdist - 1, e->crush_master);
        /* Glue after the distortion, before the volume — and skipped entirely
         * at zero, which is the default, so it cannot colour a kit nobody
         * asked it to touch. */
        if(comp > 0.001f) mix = cr78_glue_tick(&e->glue, mix, comp, e->sample_rate);
        mix *= vol;

        if(!(mix > -8.0f && mix < 8.0f)) mix = 0.0f;   /* also catches NaN */
        out[i] = mix;
    }
}

/* ---- parameters ------------------------------------------------------- */

int cr78_set_param(cr78_engine_t *e, const char *key, const char *val)
{
    /* Tempo for the synced delay. A raw key: it lives on no page and in no
     * pot table, because it is the host's business and not the player's. */
    if(!strcmp(key, "dly_bpm"))
    {
        const float bpm = (float)atof(val);
        if(bpm > 20.0f) { e->dly.bpm = bpm; cr78_fx_sync(e); }
        return 1;
    }

    /* "default" resets a control to its FACTORY default — page 30's alignment
     * figure, not pot centre — so a UI that wants a reset gesture must not
     * guess 64; it asks. */
    const int reset = !strcmp(val, "default");
    const int slot = find_pot(key);
    if(slot >= 0)
    {
        int p = reset ? g_cr78_pots[slot].def : (int)(atof(val) + 0.5f);
        if(p < 0) p = 0;
        if(p > 127) p = 127;                 /* clamp, never wrap */
        e->pot[slot]  = p;
        e->potv[slot] = pot_value(slot, p);
        cr78_fx_sync(e);
        return 1;
    }
    const int es = find_enum(key);
    if(es >= 0)
    {
        int v = reset ? g_cr78_enums[es].def : (int)(atof(val) + 0.5f);
        if(v < 0) v = 0;
        if(v >= g_cr78_enums[es].count) v = g_cr78_enums[es].count - 1;
        e->env[es] = v;
        cr78_fx_sync(e);
        return 1;
    }
    return 0;
}

int cr78_get_param(cr78_engine_t *e, const char *key, char *buf, int len)
{
    const int slot = find_pot(key);
    if(slot >= 0) return snprintf(buf, len, "%d", e->pot[slot]);
    const int es = find_enum(key);
    if(es >= 0)   return snprintf(buf, len, "%d", e->env[es]);
    return -1;
}

int cr78_serialize(const cr78_engine_t *e, char *buf, int len)
{
    int n = snprintf(buf, len, "{\"v\":%d,\"pots\":[", CR78_STATE_VERSION);
    for(int i = 0; i < CR78_NUM_POTS && n < len; ++i)
        n += snprintf(buf + n, len - n, i ? ",%d" : "%d", e->pot[i]);
    if(n < len) n += snprintf(buf + n, len - n, "],\"enums\":[");
    for(int i = 0; i < CR78_NUM_ENUMS && n < len; ++i)
        n += snprintf(buf + n, len - n, i ? ",%d" : "%d", e->env[i]);
    if(n < len) n += snprintf(buf + n, len - n, "],\"mutes\":%u}", e->mutes);
    return n;
}

/*
 * Reads the arrays positionally. A blob shorter than the current table is a
 * patch saved before a control was appended — the missing tail keeps its
 * default rather than reading garbage.
 *
 * There is no by-name migration path here and there should not be one yet:
 * the tables have never moved. The day a control is removed from the middle
 * rather than appended to the end, this file grows a frozen key list and a
 * version check, exactly as 8W8's did.
 */
static const char *scan_ints(const char *p, int *dst, int max, int *got)
{
    *got = 0;
    if(!p) return NULL;
    p = strchr(p, '[');
    if(!p) return NULL;
    ++p;
    while(*p && *p != ']' && *got < max)
    {
        while(*p == ' ' || *p == ',') ++p;
        if(*p == ']' || !*p) break;
        char *end = NULL;
        dst[(*got)++] = (int)strtol(p, &end, 10);
        if(end == p) break;      /* not a number: stop, do not spin */
        p = end;
    }
    const char *end = strchr(p, ']');
    return end ? end + 1 : NULL;
}

void cr78_deserialize(cr78_engine_t *e, const char *json)
{
    if(!json || !*json) return;

    enum { kScratch = (CR78_NUM_POTS > CR78_NUM_ENUMS ? CR78_NUM_POTS
                                                      : CR78_NUM_ENUMS) };
    int vals[kScratch];
    int got = 0;

    const char *p = strstr(json, "\"pots\"");
    scan_ints(p, vals, CR78_NUM_POTS, &got);
    for(int i = 0; i < got; ++i)
    {
        const int v = vals[i] < 0 ? 0 : (vals[i] > 127 ? 127 : vals[i]);
        e->pot[i]  = v;
        e->potv[i] = pot_value(i, v);
    }

    const char *q = strstr(json, "\"enums\"");
    scan_ints(q, vals, CR78_NUM_ENUMS, &got);
    for(int i = 0; i < got; ++i)
    {
        int v = vals[i] < 0 ? 0 : vals[i];
        if(v >= g_cr78_enums[i].count) v = g_cr78_enums[i].count - 1;
        e->env[i] = v;
    }
    cr78_fx_sync(e);

    const char *mp = strstr(json, "\"mutes\"");
    if(mp) { mp = strchr(mp, ':'); if(mp) e->mutes = (unsigned)strtoul(mp + 1, NULL, 10)
                                                     & ((1u << CR78_NUM_VOICES) - 1u); }
}

/* Read-only views for tools/kit_check. Not part of the plugin surface. */
extern "C" const float *cr78_debug_trim(void) { return kVoiceTrim; }
extern "C" const float *cr78_debug_vpp(void)  { return kFactoryVpp; }
extern "C" const float *cr78_debug_mix(void)  { return kMixVoicing; }
