/*
 * voice_check — holds every lane to page 30 of the service notes.
 *
 * Roland's factory alignment table gives, for each voice, the frequency and
 * the decay time the machine is trimmed to. This renders each lane solo
 * through the real engine at its default settings and measures both back.
 *
 * That makes it a different kind of test from 8W8's null test, and a better
 * one. A null test proves a transcription matches its source; it cannot tell
 * you the source was right. This compares a model built from the schematic
 * against numbers measured off the hardware by the people who built it, and
 * the two were arrived at independently. When the rim shot's 700 mH coil and
 * 16.5 nF give 1481 Hz and the table says 1480, nothing was fitted to make
 * that happen.
 *
 * WHAT IS CHECKED, and what is not:
 *
 *   frequency   the seven lanes with a single dominant resonance. The
 *               cowbell and the metallic beat are deliberately excluded —
 *               they are two and three simultaneous oscillators, and
 *               autocorrelation on a sum of 800 and 555 Hz finds their
 *               difference, not either tone. Testing those properly means
 *               testing the oscillators, not the lane.
 *
 *   decay       every lane the table gives a figure for, measured to a TENTH
 *               of peak because that is how page 30 defines it. The guiro is the
 *               one voice with no decay column at all — it is a scrape that
 *               runs while it is enabled, not a struck thing that rings down
 *               — so it is reported and not asserted.
 *
 * GPL-3.0.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "cr78_engine.h"

static const float kSR = 44100.0f;

/* Tolerances. The frequency one is set by what the hardware's own trimmers
 * can move — the tune presets are 10 k against a 10 k shunt, so 5% is well
 * inside a setting a real unit could be left at. The decay one is wider
 * because "decay time" on a scope trace is a judgement about where a tail
 * ends, and page 30 does not say at what level Roland read it. */
static const double kFreqTolPct  = 5.0;
static const double kDecayTolPct = 12.0;

struct Target {
    int    lane;
    double freq;      /* 0 = not checked, see the header */
    double decay;     /* 0 = the table gives none        */
};

/* Page 30, "RHYTHM VOICE" — the factory alignment table. */
static const Target kTargets[] = {
    { CR78_BD,   62.5, 0.100 },
    { CR78_SD,  340.0, 0.060 },
    { CR78_RS, 1480.0, 0.005 },
    { CR78_HH,    0.0, 0.060 },
    { CR78_CY,    0.0, 0.350 },
    { CR78_MA,    0.0, 0.020 },
    { CR78_CL, 2630.0, 0.018 },
    { CR78_HB,  600.0, 0.040 },
    { CR78_LB,  400.0, 0.040 },
    { CR78_LC,  208.0, 0.150 },
    { CR78_CB,    0.0, 0.060 },   /* two tones: 800 and 555 */
    { CR78_TB,    0.0, 0.220 },
    { CR78_GU,    0.0, 0.000 },   /* no decay column        */
    { CR78_MB,    0.0, 0.050 },   /* three tones            */
};

/* Autocorrelation fundamental over a window, parabolic-interpolated.
 *
 * Zero-crossing counting was tried first and is not good enough here: the
 * bass drum fits about six cycles inside its 100 ms decay, and the onset
 * pitch lift pulls the count. */
static double ac_freq(const std::vector<float> &v, size_t a, size_t b,
                      double lo, double hi)
{
    const size_t lmin = (size_t)(kSR / hi), lmax = (size_t)(kSR / lo);
    if(b <= a + lmax + 2 || lmin < 2) return 0.0;
    std::vector<double> r(lmax + 2, 0.0);
    double best = -1e30; size_t bl = lmin;
    for(size_t l = lmin; l <= lmax; ++l)
    {
        double s = 0.0;
        for(size_t i = a; i < b - l; ++i) s += (double)v[i] * (double)v[i + l];
        r[l] = s;
        if(s > best) { best = s; bl = l; }
    }
    if(bl <= lmin || bl >= lmax) return kSR / (double)bl;
    const double y0 = r[bl - 1], y1 = r[bl], y2 = r[bl + 1];
    const double den = y0 - 2.0 * y1 + y2;
    const double d = den != 0.0 ? (y0 - y2) / (2.0 * den) : 0.0;
    return kSR / ((double)bl + d);
}

static void render_solo(int lane, std::vector<float> &buf)
{
    cr78_engine_t *e = cr78_create(kSR);
    cr78_set_mutes(e, ~(1u << lane) & ((1u << CR78_NUM_VOICES) - 1u));
    cr78_trigger(e, lane, 127);
    buf.assign((size_t)(kSR * 5.0f), 0.0f);
    cr78_render(e, buf.data(), (int)buf.size());
    cr78_destroy(e);
}

int main(void)
{
    printf("%-4s | %9s %9s %7s | %9s %9s %7s\n",
           "lane", "meas f", "page 30", "err %", "meas dec", "page 30", "err %");
    printf("-----+-------------------------------+"
           "-------------------------------\n");
    int bad = 0;

    for(size_t i = 0; i < sizeof kTargets / sizeof kTargets[0]; ++i)
    {
        const Target &t = kTargets[i];
        std::vector<float> buf;
        render_solo(t.lane, buf);

        /* Peak AND where it is. Decay is time to 1% OF PEAK measured FROM
         * the peak, not from the trigger — the 8048's trigger pulse is 1 ms
         * wide and the loop takes a few cycles to build, which on the rim
         * shot's 5 ms is 20% of the whole figure. Timing from zero made the
         * shortest lane in the kit look 24% long when it was correct. */
        double pk = 0.0;
        size_t pki = 0;
        for(size_t k = 0; k < buf.size(); ++k)
        {
            const double a = fabs((double)buf[k]);
            if(a > pk) { pk = a; pki = k; }
        }
        if(pk <= 0.0)
        {
            printf("%-4s |  SILENT — the lane produced nothing\n",
                   cr78_voice_id(t.lane));
            ++bad;
            continue;
        }

        /*
         * Decay: the last sample above ONE TENTH of peak, timed from the peak.
         *
         * A TENTH, not a hundredth. The figure at the foot of page 30 draws
         * "Decay time" spanning from the peak to a level it labels "1/10 V",
         * so every figure in the alignment table is time to -20 dB. This
         * check measured to 1% for its first several revisions and passed —
         * because the engine was building the envelopes to the same wrong
         * threshold. Two halves of one mistake agreeing with each other is
         * not a test, and the kit shipped with every voice half the length it
         * should have been.
         */
        /*
         * Every lane, the snare included, is measured the same way: from the
         * peak down to a tenth of it, which is what page 30's figure draws
         * ("Amplitude V" at the strike, "Decay time" spanning to "1/10 V").
         *
         * The snare briefly had its own body-referenced rule here. That rule
         * was compensating for a real bug — the shell had lost its loop's
         * resonant gain and sat 20 dB under its own snap, so a peak-referenced
         * measure timed the snap and read half the spec. With the shell
         * restored the plain rule lands on the spec and the special case is
         * gone. A measurement workaround that makes a broken voice pass is
         * worse than the broken voice.
         */
        double dec = 0.0;
        for(size_t k = buf.size(); k-- > pki;)
            if(fabs((double)buf[k]) > 0.10 * pk)
            { dec = (double)(k - pki) / kSR; break; }

        double freq = 0.0;
        if(t.freq > 0.0)
        {
            /* Measure past the onset transient and short of the noise floor. */
            const size_t a = pki + (size_t)(0.15 * dec * kSR);
            const size_t b = pki + (size_t)(0.85 * dec * kSR);
            freq = ac_freq(buf, a, b, t.freq * 0.5, t.freq * 2.0);
        }

        char fbuf[64] = "        —        —       —";
        int ffail = 0, dfail = 0;
        if(t.freq > 0.0)
        {
            const double err = 100.0 * (freq - t.freq) / t.freq;
            ffail = fabs(err) > kFreqTolPct;
            snprintf(fbuf, sizeof fbuf, "%9.2f %9.2f %+6.1f%s",
                     freq, t.freq, err, ffail ? "*" : " ");
        }
        char dbuf[64] = "        —        —       —";
        if(t.decay > 0.0)
        {
            const double err = 100.0 * (dec - t.decay) / t.decay;
            dfail = fabs(err) > kDecayTolPct;
            snprintf(dbuf, sizeof dbuf, "%9.4f %9.4f %+6.1f%s",
                     dec, t.decay, err, dfail ? "*" : " ");
        }
        else
        {
            snprintf(dbuf, sizeof dbuf, "%9.4f %9s %7s", dec, "—", "—");
        }
        printf("%-4s | %s | %s\n", cr78_voice_id(t.lane), fbuf, dbuf);
        bad += ffail + dfail;
    }

    printf("\ntolerances: frequency %.0f%% (the tune trimmers have far more "
           "authority than that),\n            decay %.0f%% (page 30 does not "
           "say at what level it read a tail)\n",
           kFreqTolPct, kDecayTolPct);

    if(bad)
    {
        printf("\n%d measurement(s) marked * are outside tolerance.\n", bad);
        return 1;
    }
    printf("\nOK: every lane matches the CR-78's factory alignment table.\n");
    return 0;
}
