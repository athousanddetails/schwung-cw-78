/*
 * golden_check — proves a change did not move the kit.
 *
 *   golden_check --bless <file>       write a baseline
 *   golden_check <file>               compare exactly; fail on any difference
 *   golden_check --tol <dB> <file>    compare within a tolerance
 *
 * Renders every lane solo at its FACTORY DEFAULTS, plus the demo pattern and
 * every preset rhythm, and reduces each to a hash of the raw float bits with
 * the peak and RMS alongside. A single sample out of place changes the hash;
 * the peak and RMS are there so a failure says roughly WHAT moved rather than
 * only that something did.
 *
 * WHY THIS EXISTS. Widening a pot's range, retuning a filter, reordering a
 * sum — all of these can be argued to leave "the same sound", and all of them
 * have quietly not. This is the thing that makes "the original sound is
 * untouched" a claim with a test behind it instead of an intention.
 *
 * It deliberately runs at DEFAULTS ONLY. The point is not to freeze every
 * knob position — a mod is allowed, and expected, to change what the far end
 * of a knob does. The point is that a fresh patch, which is a correctly
 * aligned CR-78, is bit-for-bit what it was.
 *
 * THE BASELINE IS ARCHITECTURE-SPECIFIC, and that is not a flaw in it.
 *
 * Blessed on the x86-64 build host and then run against the same source
 * cross-compiled for the Move's aarch64, this reports 13 of 35 entries
 * "changed" — every one of them by less than 0.005 dB in both peak and RMS,
 * which is the last bit of a float. glibc's powf, expf and tanhf are not
 * required to be correctly rounded and are not the same implementation on the
 * two architectures, and the compiler is free to contract a multiply-add on
 * one and not the other. The engine solves loop gains and filter coefficients
 * with exactly those functions, so a handful of samples land one ULP apart.
 *
 * So: EXACT mode is for the host, where it catches real changes. --tol is for
 * asking a different and also useful question — does the device build produce
 * the same kit? At --tol 0.05 the aarch64 build passes all 35, which is the
 * answer worth having. Do not "fix" the exact mode by loosening it; the two
 * modes are for two different questions.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "cr78_engine.h"
#include "cr78_rhythms.h"
#include "demo_pattern.h"

static const float kSR = 44100.0f;

/* FNV-1a over the raw bits. Not a cryptographic hash and does not need to be:
 * it needs to change when any sample changes, which it does. */
static uint64_t hash_buf(const std::vector<float> &v)
{
    uint64_t h = 1469598103934665603ULL;
    for(float x : v)
    {
        uint32_t b; memcpy(&b, &x, 4);
        for(int i = 0; i < 4; ++i)
        { h ^= (b >> (i * 8)) & 0xFF; h *= 1099511628211ULL; }
    }
    return h;
}

static void stats(const std::vector<float> &v, double *pk, double *rms)
{
    double p = 0.0, s = 0.0;
    for(float x : v) { p = fmax(p, fabs((double)x)); s += (double)x * x; }
    *pk = p; *rms = v.empty() ? 0.0 : sqrt(s / (double)v.size());
}

struct Row { char name[32]; uint64_t h; double pk, rms; };

static void add(std::vector<Row> &out, const char *name,
                const std::vector<float> &v)
{
    Row r; snprintf(r.name, sizeof r.name, "%s", name);
    r.h = hash_buf(v); stats(v, &r.pk, &r.rms);
    out.push_back(r);
}

static void collect(std::vector<Row> &out)
{
    /* every lane, solo, at defaults */
    for(int lane = 0; lane < CR78_NUM_VOICES; ++lane)
    {
        cr78_engine_t *e = cr78_create(kSR);
        cr78_set_mutes(e, ~(1u << lane) & ((1u << CR78_NUM_VOICES) - 1u));
        cr78_trigger(e, lane, 127);
        std::vector<float> b((size_t)(kSR * 3.0f), 0.0f);
        cr78_render(e, b.data(), (int)b.size());
        cr78_destroy(e);
        char n[32]; snprintf(n, sizeof n, "lane:%s", cr78_voice_id(lane));
        add(out, n, b);
    }

    /* the demo pattern, whole kit */
    {
        cr78_engine_t *e = cr78_create(kSR);
        std::vector<float> b((size_t)(kSR * 8.0f), 0.0f);
        cr78_render_demo(e, b.data(), (int)b.size(), kSR, 2.0);
        cr78_destroy(e);
        add(out, "demo", b);
    }

    /* every preset rhythm, two measures */
    for(int i = 0; i < CR78_NUM_RHYTHMS; ++i)
    {
        const cr78_rhythm_t *r = &g_cr78_rhythms[i];
        cr78_engine_t *e = cr78_create(kSR);
        const int nPerTick = (int)(kSR * (60.0 / 110.0 / 12.0));
        std::vector<float> out2, buf(nPerTick, 0.0f);
        for(int bar = 0; bar < 2; ++bar)
        {
            const int useB = bar & 1;
            const cr78_hit_t    *hits = useB ? r->b     : r->a;
            const int            n    = useB ? r->bN    : r->aN;
            const unsigned char *acc  = useB ? r->bAcc  : r->aAcc;
            const int            accN = useB ? r->bAccN : r->aAccN;
            for(int step = 0; step < r->stepsPerBar; ++step)
            {
                int a = 0;
                for(int k = 0; k < accN; ++k) if((int)acc[k] == step) { a = 1; break; }
                for(int k = 0; k < n; ++k)
                    if((int)hits[k].step == step)
                        cr78_trigger(e, (int)hits[k].voice, a ? 127 : 88);
                cr78_render(e, buf.data(), nPerTick);
                out2.insert(out2.end(), buf.begin(), buf.end());
            }
        }
        cr78_destroy(e);
        char n[32]; snprintf(n, sizeof n, "rhy:%s", r->id);
        add(out, n, out2);
    }
}

int main(int argc, char **argv)
{
    bool bless = false;
    double tol = -1.0;            /* < 0 means exact */
    const char *path = NULL;
    for(int i = 1; i < argc; ++i)
    {
        if(!strcmp(argv[i], "--bless")) bless = true;
        else if(!strcmp(argv[i], "--tol") && i + 1 < argc) tol = atof(argv[++i]);
        else path = argv[i];
    }
    if(!path)
    {
        printf("usage: golden_check [--bless] [--tol <dB>] <file>\n");
        return 2;
    }

    std::vector<Row> now;
    collect(now);

    if(bless)
    {
        FILE *f = fopen(path, "w");
        if(!f) { printf("cannot write %s\n", path); return 2; }
        fprintf(f, "# CW-78 golden baseline — every lane at factory defaults,\n"
                   "# the demo pattern, and all %d preset rhythms.\n"
                   "# name hash peak rms\n", CR78_NUM_RHYTHMS);
        for(const Row &r : now)
            fprintf(f, "%s %llu %.9f %.9f\n", r.name,
                    (unsigned long long)r.h, r.pk, r.rms);
        fclose(f);
        printf("blessed %d entries into %s\n", (int)now.size(), path);
        return 0;
    }

    FILE *f = fopen(path, "r");
    if(!f)
    {
        printf("no baseline at %s — run with --bless first\n", path);
        return 2;
    }
    std::vector<Row> was;
    char line[256];
    while(fgets(line, sizeof line, f))
    {
        if(line[0] == '#' || line[0] == '\n') continue;
        Row r; unsigned long long h;
        if(sscanf(line, "%31s %llu %lf %lf", r.name, &h, &r.pk, &r.rms) == 4)
        { r.h = h; was.push_back(r); }
    }
    fclose(f);

    int bad = 0, within = 0;
    if(was.size() != now.size())
    {
        printf("  baseline has %d entries, this build has %d — "
               "a lane or a pattern was added or removed\n",
               (int)was.size(), (int)now.size());
        ++bad;
    }
    const size_t n = was.size() < now.size() ? was.size() : now.size();
    for(size_t i = 0; i < n; ++i)
    {
        if(strcmp(was[i].name, now[i].name))
        { printf("  entry %d: baseline is %s, this build is %s\n",
                 (int)i, was[i].name, now[i].name); ++bad; continue; }
        if(was[i].h == now[i].h) continue;
        const double dpk = 20.0 * log10((now[i].pk  + 1e-12) / (was[i].pk  + 1e-12));
        const double drm = 20.0 * log10((now[i].rms + 1e-12) / (was[i].rms + 1e-12));
        if(tol >= 0.0 && fabs(dpk) <= tol && fabs(drm) <= tol)
        { ++within; continue; }
        printf("  %-18s CHANGED   peak %+.3f dB   rms %+.3f dB\n",
               now[i].name, dpk, drm);
        ++bad;
    }

    if(bad)
    {
        printf("\n%d entry(ies) moved. If that was deliberate, re-bless:\n"
               "    golden_check --bless %s\n", bad, path);
        return 1;
    }
    if(tol >= 0.0)
        printf("  ok    %d entries within %.3f dB of the baseline "
               "(%d differ in the last float bits)\n",
               (int)now.size(), tol, within);
    else
        printf("  ok    %d entries bit-identical to the baseline\n",
               (int)now.size());
    return 0;
}
