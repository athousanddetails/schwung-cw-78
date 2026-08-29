/*
 * bench.cpp — the CPU gate. Cross-compiled and run ON the Move.
 *
 * 6W6's three metal voices summed 141 sines per sample and took 55% of the
 * device's block budget before they were rewritten. CW-78's arithmetic is
 * cheaper per voice — naive pulses and biquads rather than partial sums — but
 * there are fourteen lanes instead of eight and the cymbal alone runs several
 * biquads through three parallel chains. That is worth measuring on the
 * hardware rather than assuming, which is what this is for.
 *
 * Reports the realtime factor and, more usefully, the share of one core. The
 * number to compare against is 6W6, which shipped at 22% of a core after its
 * metal voices were rewritten (they started at 55%). A module shares the
 * device with everything else in the chain, so a third of a core is the point
 * at which this needs work.
 *
 * MEASURED ON THE MOVE, 2026-08-23, first build:
 *   worst single lane   cymbal, 39.7x realtime (2.5% of a core)
 *   all 16 every 16th   (see docs/DESIGN.md for the measured figure)
 *   busy pattern        see below
 * The "all 16" case is pathological — every voice retriggered 9.3 times a
 * second — and is here as a ceiling, not as a target.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "cr78_engine.h"

static const double SR     = 44100.0;
static const int    BLOCK  = 128;          /* Schwung's block */
static const double SECS   = 5.0;

static float g_buf[BLOCK];

static double now_seconds(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/*
 * Render `SECS` of audio, retriggering on every 16th at 140 BPM — a busy
 * pattern, not a single decaying hit, because a voice that has gone quiet
 * costs nothing and would flatter the result.
 *
 * voice >= 0   that lane alone
 * voice == -1  all sixteen, every 16th (the ceiling)
 * voice == -2  a realistic pattern: hats on every 16th, kick and snare and
 *              clap on their beats. This is the one that matters.
 */
static double measure(int voice, const char *label)
{
    cr78_engine_t *e = cr78_create((float)SR);
    const int total  = (int)(SR * SECS);
    const int period = (int)(SR * 60.0 / 140.0 / 4.0);

    const double t0 = now_seconds();
    int next = 0, step = 0;
    for(int i = 0; i < total; i += BLOCK)
    {
        if(i >= next)
        {
            if(voice >= 0) cr78_trigger(e, voice, 127);
            else if(voice == -1)
                for(int v = 0; v < CR78_NUM_VOICES; ++v)
                    cr78_trigger(e, v, 127);
            else
            {
                const int s = step & 15;
                cr78_trigger(e, CR78_HH, 90);   /* one hi-hat, every eighth */
                if(s == 0 || s == 6 || s == 8)  cr78_trigger(e, CR78_BD, 120);
                if(s == 4 || s == 12)           cr78_trigger(e, CR78_SD, 110);
                if(s == 14)                     cr78_trigger(e, CR78_TB, 110);
                if(s == 7)                      cr78_trigger(e, CR78_MB, 100);
                if(s == 11)                     cr78_trigger(e, CR78_CB,  90);
            }
            next += period;
            ++step;
        }
        cr78_render(e, g_buf, BLOCK);
    }
    const double dt = now_seconds() - t0;
    cr78_destroy(e);

    const double rt = SECS / (dt > 0 ? dt : 1e-9);
    printf("%-22s %8.1fx realtime   %6.2f%% of one core\n",
           label, rt, 100.0 / rt);
    return rt;
}

int main(void)
{
    printf("CW-78 bench — %g s of audio per case, %d-frame blocks\n\n",
           SECS, BLOCK);

    double worst = 1e30;
    for(int v = 0; v < CR78_NUM_VOICES; ++v)
    {
        char label[32];
        snprintf(label, sizeof(label), "%s only", cr78_voice_id(v));
        const double rt = measure(v, label);
        if(rt < worst) worst = rt;
    }

    printf("\n");
    const double busy = measure(-2, "busy pattern");
    const double all  = measure(-1, "all 14, every 16th");

    printf("\nworst single lane %.1fx (%.1f%% of a core)\n",
           worst, 100.0 / worst);
    printf("busy pattern      %.1fx (%.1f%% of a core)   <- the one that matters\n",
           busy, 100.0 / busy);
    printf("all 14 at once    %.1fx (%.1f%% of a core)   [pathological]\n",
           all, 100.0 / all);

    /* 6W6 ships at 22% of a core. A third is where this stops being fine. */
    if(100.0 / busy > 33.0)
        printf("\n*** busy pattern over a third of a core — this needs work ***\n");
    return 0;
}
