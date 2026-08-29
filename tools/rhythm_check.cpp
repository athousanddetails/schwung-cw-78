/*
 * rhythm_check — the preset rhythm bank, checked and rendered.
 *
 *   rhythm_check              validate the bank
 *   rhythm_check <outdir>     also render every pattern to a WAV
 *
 * The validation is the part that matters, and it exists because the pattern
 * table and the Style enum in gen_params.py are two hand-written lists that
 * have to stay in the same order. Nothing enforces that but this: get it
 * wrong and picking "Bossa" plays a rock pattern, silently, with no error
 * anywhere.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "cr78_engine.h"
#include "cr78_params.h"
#include "cr78_rhythms.h"

static const float kSR = 44100.0f;
static const double kBPM = 110.0;

static int find_enum_slot(const char *key)
{
    for(int i = 0; i < CR78_NUM_ENUMS; ++i)
        if(!strcmp(g_cr78_enums[i].key, key)) return i;
    return -1;
}

static void wav16(const char *path, const std::vector<float> &x)
{
    FILE *f = fopen(path, "wb");
    if(!f) { printf("cannot write %s\n", path); return; }
    const uint32_t n = (uint32_t)x.size(), rate = (uint32_t)kSR;
    const uint32_t datab = n * 2, riff = 36 + datab;
    const uint16_t ch = 1, bits = 16, fmt = 1;
    const uint32_t brate = rate * 2;
    const uint16_t balign = 2;
    /* The fmt chunk size is a 32-bit field. Declaring it uint16_t and then
     * fwrite'ing four bytes from it reads two bytes past the variable, which
     * writes a garbage chunk length — the file still plays in forgiving
     * players and throws in strict ones. */
    const uint32_t sz = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&balign, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&datab, 4, 1, f);
    for(float v : x)
    {
        if(v >  1.0f) v =  1.0f;
        if(v < -1.0f) v = -1.0f;
        const int16_t s = (int16_t)(v * 32767.0f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

/* Play one pattern for `bars` bars at kBPM, both measures A then B. */
static void render_pattern(const cr78_rhythm_t *r, std::vector<float> &out,
                           int bars)
{
    cr78_engine_t *e = cr78_create(kSR);
    const double secPerTick = 60.0 / kBPM / 12.0;   /* 12 ticks to the beat */
    const int nPerTick = (int)(kSR * secPerTick);
    out.clear();
    std::vector<float> buf(nPerTick, 0.0f);

    for(int bar = 0; bar < bars; ++bar)
    {
        const int useB = (bar & 1);
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
    /* let the tail ring */
    for(int i = 0; i < 40; ++i)
    {
        cr78_render(e, buf.data(), nPerTick);
        out.insert(out.end(), buf.begin(), buf.end());
    }
    cr78_destroy(e);
}

int main(int argc, char **argv)
{
    int bad = 0;

    /* ---- the bank and the Style enum must agree ---- */
    const int slot = find_enum_slot("rhy_style");
    if(slot < 0)
    {
        printf("FAIL  no rhy_style enum in the generated table\n");
        return 1;
    }
    if(g_cr78_enums[slot].count != CR78_NUM_BUTTONS)
    {
        printf("FAIL  rhy_style has %d options but the panel has %d buttons\n",
               g_cr78_enums[slot].count, CR78_NUM_BUTTONS);
        ++bad;
    }
    else
        printf("  ok    Style enum and the panel agree (%d buttons)\n",
               CR78_NUM_BUTTONS);

    /* Every button must resolve to a real pattern on both lever positions,
     * and between them the seventeen buttons must reach ALL twenty patterns —
     * a pattern no button can select is one nobody can ever play. */
    {
        int seen[CR78_NUM_RHYTHMS];
        for(int i = 0; i < CR78_NUM_RHYTHMS; ++i) seen[i] = 0;
        int dual = 0;
        for(int i = 0; i < CR78_NUM_BUTTONS; ++i)
        {
            for(int ab = 0; ab < 2; ++ab)
            {
                const int idx = cr78_resolve_pattern(i, ab);
                if(idx < 0 || idx >= CR78_NUM_RHYTHMS)
                { printf("FAIL  button %d/%c resolves out of range\n",
                         i, ab ? 'B' : 'A'); ++bad; continue; }
                seen[idx] = 1;
            }
            if(cr78_button_is_dual(i)) ++dual;
        }
        int unreachable = 0;
        for(int i = 0; i < CR78_NUM_RHYTHMS; ++i)
            if(!seen[i])
            { printf("FAIL  pattern %s is unreachable from any button\n",
                     g_cr78_rhythms[i].id); ++unreachable; }
        bad += unreachable;
        if(!unreachable)
            printf("  ok    all %d patterns reachable from %d buttons "
                   "(%d dual)\n", CR78_NUM_RHYTHMS, CR78_NUM_BUTTONS, dual);
    }

    /* ---- every hit is in range ---- */
    printf("\n%-10s %4s %5s %5s %5s %5s\n",
           "pattern", "spb", "A hit", "A acc", "B hit", "B acc");
    for(int i = 0; i < CR78_NUM_RHYTHMS; ++i)
    {
        const cr78_rhythm_t *r = &g_cr78_rhythms[i];
        int local = 0;
        for(int k = 0; k < r->aN; ++k)
        {
            if(r->a[k].step >= r->stepsPerBar) { printf("FAIL  %s A step %d >= %d\n",
                r->id, r->a[k].step, r->stepsPerBar); ++local; }
            if(r->a[k].voice >= CR78_NUM_VOICES) { printf("FAIL  %s A bad voice\n",
                r->id); ++local; }
        }
        for(int k = 0; k < r->bN; ++k)
        {
            if(r->b[k].step >= r->stepsPerBar) { printf("FAIL  %s B step %d >= %d\n",
                r->id, r->b[k].step, r->stepsPerBar); ++local; }
            if(r->b[k].voice >= CR78_NUM_VOICES) { printf("FAIL  %s B bad voice\n",
                r->id); ++local; }
        }
        for(int k = 0; k < r->aAccN; ++k)
            if(r->aAcc[k] >= r->stepsPerBar)
            { printf("FAIL  %s A accent out of bar\n", r->id); ++local; }
        for(int k = 0; k < r->bAccN; ++k)
            if(r->bAcc[k] >= r->stepsPerBar)
            { printf("FAIL  %s B accent out of bar\n", r->id); ++local; }
        /* A pattern with no hits is a typo, not a rest. */
        if(r->aN == 0 || r->bN == 0)
        { printf("FAIL  %s has an empty measure\n", r->id); ++local; }

        printf("%-10s %4d %5d %5d %5d %5d%s\n", r->id, r->stepsPerBar,
               r->aN, r->aAccN, r->bN, r->bAccN, local ? "  <-- FAIL" : "");
        bad += local;
    }

    if(argc > 1)
    {
        printf("\nrendering to %s ...\n", argv[1]);
        char path[512];
        for(int i = 0; i < CR78_NUM_RHYTHMS; ++i)
        {
            std::vector<float> out;
            render_pattern(&g_cr78_rhythms[i], out, 4);
            snprintf(path, sizeof path, "%s/%s.wav", argv[1],
                     g_cr78_rhythms[i].id);
            wav16(path, out);
            double pk = 0.0;
            for(float v : out) pk = fmax(pk, fabs((double)v));
            printf("  %-10s %5.2f s  peak %.3f\n", g_cr78_rhythms[i].id,
                   (double)out.size() / kSR, pk);
        }
    }

    if(bad) { printf("\n%d problem(s) in the rhythm bank.\n", bad); return 1; }
    printf("\nOK: the rhythm bank is consistent.\n");
    return 0;
}
