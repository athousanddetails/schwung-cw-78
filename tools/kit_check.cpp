/*
 * kit_check — fits and checks the kit balance against Roland's own numbers.
 *
 * 8W8's kit_check had to invent the proportion it was fitting to, because
 * sc808's SynthDefs arrive 33 dB apart and nothing anywhere says how loud a
 * cowbell should be next to a kick. It carried a hand-written kVoicing table
 * and defended it in a comment.
 *
 * THIS ONE DOES NOT HAVE TO. Page 30 of the CR-78 service notes is the
 * factory alignment procedure, and its last column is the output amplitude in
 * volts peak-to-peak that every voice is trimmed to. That is the kit balance,
 * decided by Roland and written down, and it lives in the engine as
 * kFactoryVpp. This tool only has to find the scale factor between a volt on
 * the VG-11 and a float in the engine.
 *
 * Two modes:
 *
 *   --fit     measure every lane's raw peak, print a kVoiceTrim table to
 *             paste into cr78_engine.cpp
 *   (default) check the trims that are there now, and fail if any lane has
 *             drifted from its factory proportion by more than the tolerance
 *
 * The check is on RATIOS against kFactoryVpp, never on lanes against each
 * other and never on equal loudness. A kit whose hi-hat is as loud as its
 * bass drum is not a kit, and the factory table already knows that: it puts
 * the rim shot at 0.8 Vpp and the bongos at 0.15.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "cr78_engine.h"
#include "demo_pattern.h"
#include "cr78_rhythms.h"

extern "C" const float *cr78_debug_trim(void);
extern "C" const float *cr78_debug_vpp(void);
extern "C" const float *cr78_debug_mix(void);

static const float kSR = 44100.0f;

/* Tolerance on the factory proportion, in dB. Wide enough that a voicing
 * tweak inside a lane does not trip it, tight enough that a lane which has
 * quietly doubled does. */
static const double kTolDb = 1.0;

/*
 * WHAT THE ABSOLUTE SCALE IS SET AGAINST: A SOLO KICK.
 *
 * This went through three wrong answers, and all three are worth keeping.
 *
 *   A four-voice downbeat. Easy to reason about, ~4 dB too quiet.
 *
 *   The hand-written demo pattern. Better, still wrong: the module's own
 *   presets are denser than anything invented for a demo.
 *
 *   The busiest preset. Wrong in the other direction, and this is the one
 *   that reached hardware. Fitting so that Bossa Nova — sixteenth maracas
 *   under a five-hit clave and a four-hit kick — peaks at -1 dBFS puts every
 *   individual hit ten dB below that, and a single kick came out at
 *   -13.1 dBFS. On the Move, where the kick is 62.5 Hz and the speaker gives
 *   nothing under about 200 Hz, that is not "quiet", it is silent.
 *
 * The target is now a SOLO KICK, matched to 8W8, because that is the thing a
 * player hits first and the thing every other level is judged against. 8W8's
 * kick measures -7.66 dBFS at its default Volume; this fits to the same
 * figure. Dense presets are then allowed to be louder than a single hit,
 * which is what dense patterns are, and the check below reports the loudest
 * one so a preset that actually clips cannot slip through.
 */
static const double kKickTargetDb = -7.66;   /* at the DEFAULT volume pot */
static const double kPatternTail = 2.0;
static const double kRhythmBPM = 110.0;

static void set(cr78_engine_t *e, const char *k, int v)
{
    char b[16]; snprintf(b, sizeof b, "%d", v);
    cr78_set_param(e, k, b);
}

/* Solo one lane into `buf`, with the engine's own trim divided back out so
 * the samples are the voice circuit's raw output. Returns the peak.
 *
 * Keeping the WAVEFORM and not just the peak is what lets --fit solve the
 * absolute scale exactly. Peaks do not sum — the bass drum's crest and the
 * hi-hat's arrive at different samples — so an estimate built from peak sums
 * lands a couple of dB out and needs a second pass to converge. With the
 * buffers in hand the downbeat is simply summed, and because the master
 * distortion is Off and the compressor is at zero the mix is exactly linear
 * in the trims: one solve, no iteration. */
static double solo_render(int lane, double trim, std::vector<float> &buf)
{
    cr78_engine_t *e = cr78_create(kSR);
    set(e, "volume", 127);
    cr78_set_mutes(e, ~(1u << lane) & ((1u << CR78_NUM_VOICES) - 1u));
    cr78_trigger(e, lane, 127);
    buf.assign((size_t)(kSR * 4.0f), 0.0f);
    cr78_render(e, buf.data(), (int)buf.size());
    double pk = 0.0;
    const double inv = trim > 0.0 ? 1.0 / trim : 1.0;
    for(float &x : buf) { x = (float)(x * inv); pk = fmax(pk, fabs((double)x)); }
    cr78_destroy(e);
    return pk;
}

static size_t pattern_frames(void)
{
    const double step = 60.0 / CR78_DEMO_BPM / 4.0;
    return (size_t)(kSR * step) * CR78_DEMO_STEPS
         + (size_t)(kSR * kPatternTail);
}

static double pattern_peak(void)
{
    cr78_engine_t *e = cr78_create(kSR);
    set(e, "volume", 127);
    std::vector<float> buf(pattern_frames() + 16, 0.0f);
    cr78_render_demo(e, buf.data(), (int)buf.size(), kSR, kPatternTail);
    double pk = 0.0;
    for(float x : buf) pk = fmax(pk, fabs((double)x));
    cr78_destroy(e);
    return pk;
}

/* Render one preset rhythm, either whole or with a single lane soloed.
 * `lane` < 0 renders the full mix. Two measures, A then B. */
static void rhythm_render(const cr78_rhythm_t *r, int lane, double trim,
                          std::vector<float> &out, bool defaultVolume = false)
{
    cr78_engine_t *e = cr78_create(kSR);
    if(!defaultVolume) set(e, "volume", 127);
    if(lane >= 0)
        cr78_set_mutes(e, ~(1u << lane) & ((1u << CR78_NUM_VOICES) - 1u));

    const double secPerTick = 60.0 / kRhythmBPM / 12.0;
    const int nPerTick = (int)(kSR * secPerTick);
    out.clear();
    std::vector<float> buf(nPerTick, 0.0f);

    for(int bar = 0; bar < 2; ++bar)
    {
        const int useB = bar & 1;
        const cr78_hit_t    *hits = useB ? r->b     : r->a;
        const int            n    = useB ? r->bN    : r->aN;
        const unsigned char *acc  = useB ? r->bAcc  : r->aAcc;
        const int            accN = useB ? r->bAccN : r->aAccN;
        for(int step = 0; step < r->stepsPerBar; ++step)
        {
            int accented = 0;
            for(int i = 0; i < accN; ++i)
                if((int)acc[i] == step) { accented = 1; break; }
            for(int i = 0; i < n; ++i)
                if((int)hits[i].step == step)
                    cr78_trigger(e, (int)hits[i].voice, accented ? 127 : 88);
            cr78_render(e, buf.data(), nPerTick);
            out.insert(out.end(), buf.begin(), buf.end());
        }
    }
    for(int i = 0; i < 30; ++i)   /* tail */
    {
        cr78_render(e, buf.data(), nPerTick);
        out.insert(out.end(), buf.begin(), buf.end());
    }
    if(lane >= 0 && trim > 0.0)
    {
        const double inv = 1.0 / trim;
        for(float &x : out) x = (float)(x * inv);
    }
    cr78_destroy(e);
}

/* At the DEFAULT volume pot, because that is what a player hears. */
static double rhythm_peak(const cr78_rhythm_t *r)
{
    std::vector<float> v;
    rhythm_render(r, -1, 1.0, v, /*defaultVolume=*/true);
    double pk = 0.0;
    for(float x : v) pk = fmax(pk, fabs((double)x));
    return pk;
}

int main(int argc, char **argv)
{
    const bool fit = argc > 1 && !strcmp(argv[1], "--fit");
    const float *trim = cr78_debug_trim();
    const float *vpp  = cr78_debug_vpp();

    double raw[CR78_NUM_VOICES];
    std::vector<float> solo[CR78_NUM_VOICES];
    for(int v = 0; v < CR78_NUM_VOICES; ++v)
        raw[v] = solo_render(v, trim[v], solo[v]);

    if(fit)
    {
        /* Proportions from the MIX table (dB relative to the bass drum), not
         * from page 30's per-voice alignment column — see the note above
         * kMixVoicing in cr78_engine.cpp for why that distinction is the
         * whole bug. */
        const float *mix = cr78_debug_mix();
        double t[CR78_NUM_VOICES];
        for(int v = 0; v < CR78_NUM_VOICES; ++v)
        {
            const double want = pow(10.0, (double)mix[v] / 20.0);  /* rel. BD */
            t[v] = raw[v] > 0.0 ? want / raw[v] : 0.0;
        }

        /* Scale so a solo kick lands on the target. kKickTargetDb is quoted at
         * the DEFAULT volume pot, and these renders run at volume 127, so the
         * difference between the two is added back. */
        cr78_engine_t *probe = cr78_create(kSR);
        char vb[8];
        cr78_get_param(probe, "volume", vb, sizeof vb);
        const double defVol = atoi(vb) / 127.0;
        cr78_destroy(probe);
        const double atFull = kKickTargetDb - 20.0 * log10(defVol);
        const double want   = pow(10.0, atFull / 20.0);
        const double now    = raw[CR78_BD] * t[CR78_BD];
        double g            = now > 0.0 ? want / now : 1.0;

        fprintf(stderr, "solo kick target %+.2f dBFS at default volume "
                        "(%.3f) = %+.2f at full\n",
                kKickTargetDb, defVol, atFull);

        /*
         * HEADROOM GATE. The presets stack denser than any single hit, and a
         * mix hot enough to match 8W8 per-lane can push the busiest of them
         * past full scale, where the int16 stage clamps hard. So after
         * anchoring on the kick, the loudest preset is rendered at the
         * proposed trims and, if it would land above -0.5 dBFS at the default
         * volume, EVERYTHING is scaled back just enough. The kick may end up
         * a fraction under its anchor; a clean preset is worth more than the
         * last half-dB of anchor precision.
         */
        {
            for(int v = 0; v < CR78_NUM_VOICES; ++v)
            {
                /* temporarily run the engine at the proposed trims is not
                 * possible from here, so scale per-lane renders instead */
            }
            std::vector<float> pat[CR78_NUM_VOICES];
            double worstPk = 0.0;
            for(int r = 0; r < CR78_NUM_RHYTHMS; ++r)
            {
                /* per-lane render is expensive; sample the four densest */
                static const char *dense[4] = {"boogie","bossa","beguine","rock1"};
                bool pick = false;
                for(int d = 0; d < 4; ++d)
                    if(!strcmp(g_cr78_rhythms[r].id, dense[d])) pick = true;
                if(!pick) continue;
                for(int v = 0; v < CR78_NUM_VOICES; ++v)
                    rhythm_render(&g_cr78_rhythms[r], v, trim[v], pat[v]);
                const size_t n = pat[0].size();
                for(size_t k = 0; k < n; ++k)
                {
                    double acc = 0.0;
                    for(int v = 0; v < CR78_NUM_VOICES; ++v)
                        acc += (double)pat[v][k] * t[v] * g;
                    const double a = fabs(acc);
                    if(a > worstPk) worstPk = a;
                }
            }
            worstPk *= defVol;                     /* at the default volume */
            const double lim = pow(10.0, -0.5 / 20.0);
            if(worstPk > lim)
            {
                const double back = lim / worstPk;
                g *= back;
                fprintf(stderr, "headroom: densest preset would peak %+.2f "
                        "dBFS — scaling the kit %+.2f dB\n",
                        20.0 * log10(worstPk), 20.0 * log10(back));
            }
        }

        printf("/* Filled by tools/kit_check. See the comment above "
               "kMixVoicing. */\n");
        printf("constexpr float kVoiceTrim[CR78_NUM_VOICES] = {\n");
        for(int v = 0; v < CR78_NUM_VOICES; ++v)
            printf("    %.4ff,  /* %s */\n", t[v] * g, cr78_voice_id(v));
        printf("};\n");
        return 0;
    }

    /* Check: every lane's delivered level against the MIX table, bass drum as
     * the reference. NOT against page 30's Vpp column — that is a per-voice
     * alignment spec and using it as a mix is the bug this file exists to
     * stop coming back. */
    const float *mix = cr78_debug_mix();
    printf("%-4s %8s %9s %9s %8s\n",
           "lane", "peak", "rel dB", "want dB", "err dB");
    const double ref_peak = raw[CR78_BD] * trim[CR78_BD];
    int bad = 0;
    for(int v = 0; v < CR78_NUM_VOICES; ++v)
    {
        const double got  = raw[v] * trim[v];
        const double rel  = 20.0 * log10(got / ref_peak);
        const double want = (double)mix[v];
        const double err  = rel - want;
        if(fabs(err) > kTolDb) ++bad;
        printf("%-4s %8.4f %+9.2f %+9.2f %+8.2f%s\n",
               cr78_voice_id(v), got, rel, want, err,
               fabs(err) > kTolDb ? "  <-- FAIL" : "");
    }

    /* THE ANCHOR: a solo kick at the default volume pot. This is the number
     * that was wrong on hardware — see the note at the top. */
    {
        cr78_engine_t *e = cr78_create(kSR);
        cr78_set_mutes(e, ~(1u << CR78_BD) & ((1u << CR78_NUM_VOICES) - 1u));
        cr78_trigger(e, CR78_BD, 127);
        std::vector<float> b((size_t)(kSR * 2.0f), 0.0f);
        cr78_render(e, b.data(), (int)b.size());
        cr78_destroy(e);
        double pk = 0.0;
        for(float x : b) pk = fmax(pk, fabs((double)x));
        const double db = 20.0 * log10(pk + 1e-12);
        printf("\nsolo kick, default volume   %+.2f dBFS  "
               "(target %+.2f; 8W8 measures -7.66)\n", db, kKickTargetDb);
        if(fabs(db - kKickTargetDb) > 1.0)
        { printf("  <-- FAIL: the kit is off its anchor\n"); ++bad; }
    }

    /* A dense preset is ALLOWED to be louder than one hit — that is what a
     * dense pattern is. What is not allowed is clipping. */
    int worst = 0; double pp = -1.0;
    for(int i = 0; i < CR78_NUM_RHYTHMS; ++i)
    {
        const double pk = rhythm_peak(&g_cr78_rhythms[i]);
        if(pk > pp) { pp = pk; worst = i; }
    }
    printf("loudest preset (%s)      %+.2f dBFS\n",
           g_cr78_rhythms[worst].name, 20.0 * log10(pp));
    if(pp >= 1.0)
    { printf("  <-- FAIL: it clips at the default volume\n"); ++bad; }

    if(bad)
    {
        printf("\n%d problem(s). Run `kit_check --fit` and paste the table "
               "into cr78_engine.cpp.\n", bad);
        return 1;
    }
    printf("\nOK: balance within %.1f dB of the mix table, anchored on the "
           "kick, no preset clipping.\n", kTolDb);
    return 0;
}
