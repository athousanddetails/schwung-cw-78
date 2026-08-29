/*
 * demo_pattern.h — two bars of CW-78, as data.
 *
 * Shared by src/tools/render.cpp, which writes it out so the kit can be
 * heard, and tools/kit_check.cpp, which fits the kit's absolute level to it.
 *
 * THOSE TWO WANTING THE SAME PATTERN IS NOT A CONVENIENCE. The level has to
 * be fitted to what a pattern ACTUALLY peaks at, and 8W8 found every cheaper
 * proxy wrong in turn: a four-voice downbeat left its pattern clipping at
 * +0.7 dBFS, and a six-voice full-velocity hit — which no pattern ever
 * produces — left it 8.6 dB quieter than it needed to be. Fit the thing you
 * are going to listen to. CW-78's kit_check started with the four-voice
 * downbeat anyway and this is the correction.
 *
 * Deliberately plain: a normal CR-78 pattern, not a showcase. A pattern that
 * avoided the awkward simultaneous hits would not answer the question.
 *
 * IT IS REPRESENTATIVE, NOT TRANSCRIBED. Pages 27 and 28 of the service notes
 * carry all thirty-four of the machine's own preset patterns in staff
 * notation — Waltz through Disco-2 and the fills — and reading exact note
 * placements off a 1979 scan would be guessing dressed up as provenance. This
 * is written in that style, using the voices the machine has, and says so.
 *
 * GPL-3.0.
 */
#ifndef CR78_DEMO_PATTERN_H
#define CR78_DEMO_PATTERN_H

#include "cr78_engine.h"

/* Named for the demo specifically: cr78_rhythms.h owns `cr78_hit_t` for the
 * factory presets, and the two are different shapes — this one carries a
 * velocity per hit, the presets carry accent per step, as the hardware does. */
typedef struct { int step, voice, vel; } cr78_demo_hit_t;

/* 32 sixteenths — two bars at 120 BPM. Velocity is a straight line with no
 * threshold, so these are simply loud hits and quieter ones; 127 is the top
 * of the range. */
static const cr78_demo_hit_t kDemoHits[] = {
    /* ---- bar 1: the machine's own idiom — hats, bongos and a cowbell ---- */
    { 0, CR78_BD, 127}, { 4, CR78_SD, 110}, { 6, CR78_BD,  90},
    { 8, CR78_BD, 100}, {12, CR78_SD, 110},
    { 0, CR78_HH,  90}, { 2, CR78_HH,  80}, { 4, CR78_HH,  90},
    { 6, CR78_HH,  80}, { 8, CR78_HH,  90}, {10, CR78_HH,  80},
    {12, CR78_HH,  90}, {14, CR78_HH,  80},
    { 3, CR78_RS,  90}, {11, CR78_CB,  90}, { 7, CR78_CL,  85},
    { 5, CR78_HB,  95}, {13, CR78_LB,  95},
    /* ---- bar 2: the Add Voice lanes, which is where a CR-78 gives ------- */
    {16, CR78_BD, 127}, {20, CR78_SD, 110}, {22, CR78_BD,  90},
    {24, CR78_BD, 100}, {28, CR78_SD, 110},
    {16, CR78_HH,  90}, {18, CR78_HH,  80}, {20, CR78_HH,  90},
    {22, CR78_HH,  80}, {24, CR78_HH,  90}, {26, CR78_HH,  80},
    {16, CR78_CY, 100},
    {25, CR78_LB,  95}, {26, CR78_HB,  95}, {27, CR78_LC,  90},
    {19, CR78_MA,  85}, {23, CR78_MA,  85},
    {29, CR78_TB,  90}, {30, CR78_MB,  95}, {31, CR78_GU,  90},
};

#define CR78_DEMO_STEPS 32
#define CR78_DEMO_BPM   120.0
#define CR78_DEMO_HITS  ((int)(sizeof(kDemoHits) / sizeof(kDemoHits[0])))

/*
 * Render the pattern into `out`, plus `tailSeconds` of ring-out. Returns the
 * number of frames written, or 0 if the buffer is too small.
 */
static inline int cr78_render_demo(cr78_engine_t *e, float *out,
                                   const int capacity, const double sampleRate,
                                   const double tailSeconds)
{
    const double step = 60.0 / CR78_DEMO_BPM / 4.0;
    const int n = (int)(sampleRate * step);
    const int total = n * CR78_DEMO_STEPS + (int)(sampleRate * tailSeconds);
    if(total > capacity) return 0;

    int done = 0;
    for(int s = 0; s < CR78_DEMO_STEPS; ++s)
    {
        for(int k = 0; k < CR78_DEMO_HITS; ++k)
            if(kDemoHits[k].step == s)
                cr78_trigger(e, kDemoHits[k].voice, kDemoHits[k].vel);
        cr78_render(e, out + done, n);
        done += n;
    }
    if(total > done) { cr78_render(e, out + done, total - done); done = total; }
    return done;
}

#endif /* CR78_DEMO_PATTERN_H */
