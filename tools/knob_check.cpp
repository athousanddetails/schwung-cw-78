/*
 * knob_check — every control must actually DO something.
 *
 * WHY THIS EXISTS, and it is the gap the rest of the suite left open.
 *
 * voice_check asserts the voices match page 30. kit_check asserts the mix.
 * golden_check asserts nothing MOVED. rhythm_check asserts the pattern bank
 * lines up with the Style enum. loadtest asserts every generated key resolves
 * and the module survives a host. Between them they check ranges, names, page
 * layout, storage order and key resolution —
 *
 *   AND A KNOB WIRED TO NOTHING PASSES EVERY ONE OF THEM.
 *
 * It resolves, it stores, it round-trips, it draws, it reads back what you
 * wrote. It simply never reaches the voice. 8W8 shipped exactly that for
 * months: its Maracas Attack fed a code path that was deleted with an engine
 * switch, and the pot went on resolving while doing nothing at all — found by
 * measuring, not by reading. CW-78 is a worse risk than 8W8 was, because its
 * drum channels were rebuilt three times over (the loop folded into the pole,
 * the resonant gain restored, the strike feedthrough added), and every one of
 * those rewrites moved what the knobs feed.
 *
 * The probe: for each pot, render its lane solo at pot 0 and at pot 127 and
 * hash both. Same for each enum across its options. If the hashes match, the
 * control did nothing to the audio and this fails and names it.
 *
 * MUTATION-TESTED, AND THE MUTATION HAS TO BE EXACT. Stub sd_snappy so the
 * pot resolves but never reaches the voice:
 *
 *     e->sdv.setSnappy(64.0f / 127.0f);     // its exact default position
 *
 *     knob_check     FAIL — sd_snappy does nothing
 *     golden_check   PASS — 35 renders bit-identical
 *     voice_check    PASS      kit_check   PASS
 *     rhythm/state/sends/choke/render   all PASS
 *
 * knob_check is the ONLY check here that sees it. The exactness matters: a
 * first attempt hard-wired 0.55f, which is near the default but not it, and
 * golden_check caught that — which would have "proved" the suite already
 * covered this class of bug when it does not. 8W8 hit the same trap and
 * flagged it. A mutation that changes the DEFAULT sound tests the golden
 * baseline; only one that leaves the default untouched tests this.
 *
 * A control legitimately silent in one context is declared below with the
 * reason, never skipped quietly — an unexplained skip is how the next dead
 * knob hides.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "cr78_engine.h"
#include "cr78_params.h"

static const float kSR = 44100.0f;

/*
 * A CHOKE NEEDS A GESTURE, NOT A CHORD. Choke is the one control whose effect
 * is not visible in a single strike: something has to be RINGING before the
 * cut can be heard. Triggering every lane on the same sample happens to
 * change the render (they choke each other at t=0), so the control passes —
 * but it passes for the wrong reason, and would go on passing if the choke
 * only ever worked at zero delay. So Choke is measured as it is played:
 * maracas, let it ring, then tambourine 120 ms later. Approach from the 8W8
 * session, which found the same hole in its hat choke.
 *
 * AND THE FIRST LANE HAS TO STILL BE SOUNDING. The maracas' factory decay is
 * about 20 ms, so at a 120 ms gap the choke is cutting silence and all three
 * options render alike — the sequential version FAILED where the naive
 * simultaneous one passed. That is the probe working: the honest gesture is
 * the one that exposes what has to be true. The context opens ma_decay right
 * up first, which is both the real reason anyone reaches for a choke and
 * exactly what this module's decay-range mod exists to allow.
 */
static const int kChokeFirst  = CR78_MA;
static const int kChokeSecond = CR78_TB;
static bool is_choke(const char *key) { return !strcmp(key, "hat_choke"); }

static uint64_t hash_render(const char *key, const char *val, int lane,
                            const std::string ctx[2][2], int nctx, int vel)
{
    cr78_engine_t *e = cr78_create(kSR);
    if(lane >= 0)
        cr78_set_mutes(e, ~(1u << lane) & ((1u << CR78_NUM_VOICES) - 1u));
    for(int i = 0; i < nctx; ++i)
        cr78_set_param(e, ctx[i][0].c_str(), ctx[i][1].c_str());
    if(key) cr78_set_param(e, key, val);

    std::vector<float> b((size_t)(kSR * 2.0f), 0.0f);

    if(key && is_choke(key))
    {
        const size_t gap = (size_t)(kSR * 0.120f);
        cr78_trigger(e, kChokeFirst, vel);
        cr78_render(e, b.data(), (int)gap);
        cr78_trigger(e, kChokeSecond, vel);
        cr78_render(e, b.data() + gap, (int)(b.size() - gap));
        cr78_destroy(e);
    }
    else
    {
    if(lane >= 0) cr78_trigger(e, lane, vel);
    else for(int v = 0; v < CR78_NUM_VOICES; ++v) cr78_trigger(e, v, vel);

    cr78_render(e, b.data(), (int)b.size());
    cr78_destroy(e);
    }

    uint64_t h = 1469598103934665603ULL;
    for(float x : b)
    { uint32_t u; memcpy(&u, &x, 4);
      for(int i = 0; i < 4; ++i) { h ^= (u >> (i*8)) & 0xFF; h *= 1099511628211ULL; } }
    return h;
}

/* key prefix -> lane, so a per-voice control is measured on its own voice. */
static int lane_of(const char *key)
{
    for(int v = 0; v < CR78_NUM_VOICES; ++v)
    {
        const char *id = cr78_voice_id(v);
        const size_t n = strlen(id);
        if(!strncmp(key, id, n) && key[n] == '_') return v;
    }
    return -1;                       /* global: measured on the whole kit */
}

/*
 * CONTEXT: what else must be true for a control to be able to do anything.
 *
 * The first version of this probe reported twenty-seven dead controls and all
 * but two were the probe's fault. A distortion TYPE cannot change a sound
 * while that voice's Drive is 0 — and Drive defaults to 0, because a CR-78
 * has no drive stage. A reverb's Tone cannot change a sound while nothing is
 * sent to the reverb. Both were behaving exactly as designed.
 *
 * That failure mode matters more than the bug it was hunting: a probe that
 * cries wolf twenty-seven times gets its output skimmed, and the two real
 * findings in it die with the noise. So each control is measured WHERE IT IS
 * SUPPOSED TO WORK, and the enabling context is written down here rather than
 * discovered again by whoever next reads a red line.
 *
 * The context is itself a claim about the design ("Tone does nothing until
 * you send it something"), so getting one wrong shows up as a dead control
 * rather than hiding one.
 */
/*
 * VELOCITY IS CONTEXT TOO. Velocity depth sets how far a SOFT hit falls below
 * a full one, so at velocity 127 it is designed to do nothing: vn = 1 -
 * depth*(1 - 127/127) = 1, whatever the pot says. Probed only at full
 * velocity it looks dead, and "the control that shapes dynamics" is a bad
 * thing to wave through on a false alarm. Measured at 64 it has to prove it
 * changes a soft hit, which is the claim the panel actually makes.
 */
static const int kSoftVel = 64;
static int vel_for(const char *key)
{ return !strcmp(key, "vel_depth") ? kSoftVel : 127; }

struct Ctx { const char *suffix; const char *k1, *v1; const char *k2, *v2; };
static const Ctx kContext[] = {
    /* a distortion type needs drive open on that voice */
    { "_dist_type", "@_drive", "127", NULL, NULL },
    /* the master distortion needs the master drive open, and vice versa:
     * master_dist option 0 is "Off", so Drive alone distorts nothing */
    { "master_dist",  "master_drive", "127", NULL, NULL },
    { "master_drive", "master_dist",  "1",   NULL, NULL },
    /*
     * A bus control needs something arriving on that bus — sent from the
     * SNARE, not the kick. The kick has no send knobs at all: it stays dry by
     * design (reverb on a kick is mud), so seeding the context from bd_rev
     * set a key that does not exist and left the bus silent, which made all
     * eight bus controls look dead. The one lane in the module that cannot
     * feed a bus is the one this reached for first.
     */
    { "rev_",  "sd_rev", "127", NULL, NULL },
    { "dly_",  "sd_dly", "127", NULL, NULL },
    /* a choke needs a long-ringing lane to cut — see the note above */
    { "hat_choke", "ma_decay", "127", NULL, NULL },
};

/* Fill ctx[] with the prerequisites for `key`, "@" standing for its lane id. */
static int context_for(const char *key, const char *lane,
                       std::string out[2][2])
{
    int n = 0;
    for(const Ctx &c : kContext)
    {
        const size_t sl = strlen(c.suffix), kl = strlen(key);
        const bool tail   = sl < kl && !strcmp(key + kl - sl, c.suffix);
        const bool head   = !strncmp(key, c.suffix, sl);
        const bool exact  = !strcmp(key, c.suffix);
        if(!(tail || exact || (c.suffix[sl-1] == '_' && head))) continue;
        for(int i = 0; i < 2; ++i)
        {
            const char *k = i ? c.k2 : c.k1, *v = i ? c.v2 : c.v1;
            if(!k) break;
            std::string kk(k);
            if(!kk.empty() && kk[0] == '@') kk = std::string(lane) + kk.substr(1);
            out[n][0] = kk; out[n][1] = v; ++n;
        }
        break;
    }
    return n;
}

/*
 * Controls with NO audio path at all, each with its reason. Nothing goes in
 * this list to make a failure go away — a control here is one whose effect
 * lives somewhere this probe cannot see, and the reason says where.
 */
struct Excuse { const char *key; const char *why; };
static bool kExcuseUsed[8];
static const Excuse kExcused[] = {
    /* ui_focus and mutes were listed here and never fired: they are plugin
     * state, not engine params, so they are not in the tables this walks. An
     * excuse for a control that does not exist explains nothing and reads as
     * though it does — the coverage check at the end now fails on it. */
    { "note_map", "maps MIDI notes to lanes in the plugin — changes WHICH voice a note plays, not how any voice sounds" },
    { "rhy_mode",  "rhythm playback lives in the plugin's block loop, not the engine" },
    { "rhy_style", "as rhy_mode" },
    { "rhy_ab",    "as rhy_mode" },
};
static const char *excuse_for(const char *key)
{
    for(size_t i = 0; i < sizeof kExcused / sizeof kExcused[0]; ++i)
        if(!strcmp(kExcused[i].key, key)) { kExcuseUsed[i] = true; return kExcused[i].why; }
    return NULL;
}

int main(void)
{
    int dead = 0, checked = 0, excused = 0;
    printf("%-16s %-6s %s\n", "control", "lane", "verdict");
    printf("---------------- ------ -------------------------------------------\n");

    for(int i = 0; i < CR78_NUM_POTS; ++i)
    {
        const char *key = g_cr78_pots[i].key;
        const char *why = excuse_for(key);
        if(why) { printf("%-16s %-6s excused: %s\n", key, "-", why); ++excused; continue; }
        const int lane = lane_of(key);
        const char *lid = lane >= 0 ? cr78_voice_id(lane) : "";
        std::string ctx[2][2]; const int nc = context_for(key, lid, ctx);
        const int vel = vel_for(key);
        const uint64_t lo = hash_render(key, "0",   lane, ctx, nc, vel);
        const uint64_t hi = hash_render(key, "127", lane, ctx, nc, vel);
        ++checked;
        if(lo == hi)
        { printf("%-16s %-6s DEAD — pot 0 and pot 127 render identically\n",
                 key, lane >= 0 ? cr78_voice_id(lane) : "kit"); ++dead; }
    }

    for(int i = 0; i < CR78_NUM_ENUMS; ++i)
    {
        const char *key = g_cr78_enums[i].key;
        const char *why = excuse_for(key);
        if(why) { printf("%-16s %-6s excused: %s\n", key, "-", why); ++excused; continue; }
        const int lane = lane_of(key);
        const char *lid = lane >= 0 ? cr78_voice_id(lane) : "";
        std::string ctx[2][2]; const int nc = context_for(key, lid, ctx);
        const int n = g_cr78_enums[i].count;
        const uint64_t base = hash_render(key, "0", lane, ctx, nc, 127);
        bool moved = false;
        for(int o = 1; o < n && !moved; ++o)
        { char vo[8]; snprintf(vo, sizeof vo, "%d", o);
          if(hash_render(key, vo, lane, ctx, nc, 127) != base) moved = true; }
        ++checked;
        if(!moved)
        { printf("%-16s %-6s DEAD — all %d options render identically\n",
                 key, lane >= 0 ? cr78_voice_id(lane) : "kit", n); ++dead; }
    }

    /*
     * COVERAGE. Every control in the generated tables is walked by
     * construction — this file names no control to measure, so one added
     * tomorrow is measured tomorrow without touching this file. What CAN rot
     * is the excuse list: an entry naming a control that no longer exists
     * goes on looking like a considered exemption. The 8W8 session made its
     * coverage enforced rather than assumed after finding the same class of
     * hole; this is that idea in the shape this probe needs.
     */
    const int total = CR78_NUM_POTS + CR78_NUM_ENUMS;
    if(checked + excused != total)
    { printf("\nFAIL: %d of %d controls accounted for — the walk missed some\n",
             checked + excused, total); ++dead; }
    for(size_t i = 0; i < sizeof kExcused / sizeof kExcused[0]; ++i)
        if(!kExcuseUsed[i])
        { printf("\nFAIL: '%s' is excused but is not a control — stale excuse\n",
                 kExcused[i].key); ++dead; }

    printf("\n%d control(s) measured, %d excused with a reason, "
           "%d of %d accounted for.\n", checked, excused, checked + excused, total);
    if(dead)
    { printf("%d DEAD CONTROL(S) — each resolves and stores but never reaches "
             "the audio.\n", dead); return 1; }
    printf("OK: every control changes the sound it claims to.\n");
    return 0;
}
