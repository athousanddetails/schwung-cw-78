/*
 * cr78_perc_circuit.h — guiro and tambourine.
 *
 * The two Add Voice lanes that are not made like anything else on the board.
 *
 * GUIRO
 *
 * A cross-coupled ASTABLE MULTIVIBRATOR — Q520 and Q52 with C529 .068,
 * C530 .068, R543 82k, R544 56k, R545 56k, R674 15k, R675 10k, D520 and
 * VR54 100k(B) — running at 125 Hz or 77 Hz depending on which of the two
 * trigger lines fired (VR59 sets both, VR63 the level).
 *
 * THOSE ARE NOT PITCHES. They are the SCRAPE RATE: the guiro's teeth. Each
 * edge of the multivibrator kicks the L4 45 mH || C532 .015 tank, which
 * resonates at 6126 Hz — derived from the parts list and the schematic — and
 * what you hear is a rasp of about a dozen ticks, not a note. Modelling this
 * as an oscillator at 125 Hz would give a buzz and would be wrong.
 *
 * The factory table gives the guiro no decay figure at all, which fits: it is
 * a scrape that lasts as long as the multivibrator is enabled rather than a
 * struck thing that rings down.
 *
 * TAMBOURINE
 *
 * Two trigger paths of deliberately different weight — D521 through R557 270k
 * and D522 through R558 820k, a hard tap and a soft one — into Q524 with
 * C536 .056, then R560 2.2M, C537 .01 and R561 47k into Q509 and the
 * L5 || C538 .033 tank, out through C539 250p.
 *
 * L5 IS UNKNOWN AND THAT IS A REAL GAP. The schematic marks it "1R"; the
 * parts list on page 31 has 022-031 "coil no.31 1R" and gives no inductance,
 * where it gives 45 mH for no.30 and 700 mH for no.33. So the tank frequency
 * cannot be derived. It is SOLVED here instead: 62.7 mH is the value that
 * puts the tank at 3.5 kHz with C538's .033, which is where a tambourine's
 * jingles live, and it sits sensibly between the two coils the parts list
 * does give. If anyone measures a real L5, this one number replaces the fit
 * and the frequency stops being a choice.
 *
 * GPL-3.0.
 */
#ifndef CR78_PERC_CIRCUIT_H
#define CR78_PERC_CIRCUIT_H

#include <math.h>

#include "cr78_circuit.h"

namespace cr78 {

/* ---- guiro: component values, p.26, VG-11A ----------------------------- */

/* The tank: L4 45 mH (coil 022-030) || C532 .015. DERIVED -> 6126 Hz. */
static const double kGU_L = 45.0e-3;
static const double kGU_C = 15.0e-9;

/* page 30: the two scrape rates, VR59. */
static const double kGU_RateHi = 125.0;
static const double kGU_RateLo =  77.0;

static const double kGU_Amp = 0.30;     /* page 30, VR63 */

/* The scrape's length. The factory table gives no figure — the multivibrator
 * simply runs while it is enabled — so this is the length that makes the
 * lane playable from a pad, and it is FITTED. At 125 Hz it is about
 * twenty-five teeth. */
static const double kGU_Decay = 0.200;

/* The tank's losses. FITTED: a guiro tick is a short, hard rap, not a ring. */
static const double kGU_TankQ = 9.0;

static inline double guiroTankFreq() { return lcFreq(kGU_L, kGU_C); }

class GuiroVoice {
public:
    void init(const double _sr)
    {
        sr_ = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        env_.reset();
        res_.reset();
        hp_.set(1200.0, sr_);
        baseTank_ = guiroTankFreq();
        setTune(1.0);
        setDecay(kGU_Decay);
        setRate(1.0);
        gateSamp_ = 0;
    }

    void reset()
    { front_.reset(); env_.reset(); res_.reset(); hp_.reset(); ast_.reset();
      gateSamp_ = 0; }

    void setTune(const double _ratio)
    {
        const double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        res_.set(baseTank_ * r, kGU_TankQ, sr_);
        hp_.set(1200.0 * r, sr_);
    }

    /* 0..1 walks the scrape between the hardware's own two rates — the low
     * and high guiro are one circuit with one trimmer, so this is the same
     * control the machine has, made continuous. */
    void setRate(const double _v)
    {
        const double v = _v < 0.0 ? 0.0 : (_v > 1.0 ? 1.0 : _v);
        ast_.set(kGU_RateLo + (kGU_RateHi - kGU_RateLo) * v, sr_);
    }

    void setDecay(const double _s)
    { env_.set((_s < 0.005 ? 0.005 : _s) / kLn10, sr_); }

    void trigger(const double _vel)
    { gateSamp_ = (int)(0.001 * sr_); gateV_ = 0.4 + 0.6 * _vel; }

    bool active() const { return gateSamp_ > 0 || env_.value() > 1.0e-5; }

    double process()
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate);
        if(strike > 0.0) env_.strike(strike * 1.5);

        const double e = env_.process();
        /* the multivibrator runs free; the envelope only says how hard each
         * tooth hits the tank */
        const double tooth = ast_.process() * e;
        return hp_.process(res_.process(tooth));
    }

    double amplitude() const { return kGU_Amp; }

private:
    TriggerFront front_;
    Astable      ast_;
    EnvCap       env_;
    Resonator    res_;
    OnePoleHP    hp_;
    double sr_ = 44100.0, baseTank_ = 6126.0, gateV_ = 1.0;
    int    gateSamp_ = 0;
};

/* ---- tambourine: p.26, VG-11A ------------------------------------------ */

/* C538 .033 is on the schematic. L5 is NOT — see the header. SOLVED to put
 * the tank at 3.5 kHz, and written as an inductance rather than a frequency
 * so that measuring a real coil replaces exactly one number. */
static const double kTB_C = 33.0e-9;
static const double kTB_L = 62.7e-3;

static const double kTB_Decay = 0.220;   /* page 30 */
static const double kTB_Amp   = 0.25;    /* page 30, VR62 */

/* Jingles are many small sources, so the tank is broad and there is noise
 * under it. Both FITTED. */
static const double kTB_TankQ  = 2.2;
static const double kTB_NoiseMix = 0.62;

/*
 * How much of the strike goes straight into the tank alongside the noise —
 * the initial jingle crash before the shimmer.
 *
 * This is NOT a taste setting, and it was 8.0 until voice_check caught it.
 * The strike is an impulse, so a large one dominates the lane's PEAK; the
 * decay is then measured down from that spike rather than from the envelope,
 * and the tail reads short. At 8.0 the lane measured 171 ms against page 30's
 * 220 — a 22% error that was entirely this number and nothing to do with the
 * envelope, which was correct the whole time.
 *
 * Swept against the factory decay: 8.0 -> -22%, 4.0 -> -17%, 2.5 -> -9.6%,
 * 1.5 -> -3.6%, 0.8 -> -4.9%. The turn at the bottom is the noise's own
 * crest wandering, not the model. 1.5 is where the envelope governs the peak
 * and the strike is still audible as a crash.
 */
static const double kTB_StrikeMix = 1.5;

static inline double tambTankFreq() { return lcFreq(kTB_L, kTB_C); }

class TambourineVoice {
public:
    void init(const double _sr)
    {
        sr_ = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        env_.reset();
        res_.reset();
        hp_.set(2000.0, sr_);
        baseTank_ = tambTankFreq();
        setTune(1.0);
        setDecay(kTB_Decay);
        gateSamp_ = 0;
    }

    void reset()
    { front_.reset(); env_.reset(); res_.reset(); hp_.reset(); gateSamp_ = 0; }

    void setTune(const double _ratio)
    {
        const double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        res_.set(baseTank_ * r, kTB_TankQ, sr_);
        hp_.set(2000.0 * r, sr_);
    }

    void setDecay(const double _s)
    { env_.set((_s < 0.005 ? 0.005 : _s) / kLn10, sr_); }

    /* Dump the envelope capacitor. Used by the hat choke group, which is a
     * MOD — the hardware has nothing that does this. Same shape as
     * NoiseVoice::choke() so the engine can treat the three noise lanes
     * alike. */
    void choke() { env_.reset(); }

    /* The hardware's two trigger paths, R557 270k and R558 820k, are a hard
     * tap and a soft one. Velocity picks between them continuously. */
    void trigger(const double _vel)
    {
        gateSamp_ = (int)(0.001 * sr_);
        gateV_ = 0.33 + 0.67 * _vel;
    }

    bool active() const { return gateSamp_ > 0 || env_.value() > 1.0e-5; }

    double process(const double _noise)
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate);
        if(strike > 0.0) env_.strike(strike * 1.6);

        const double e = env_.process();
        const double src = _noise * kTB_NoiseMix + strike * kTB_StrikeMix;
        return hp_.process(res_.process(src * e));
    }

    double amplitude() const { return kTB_Amp; }

private:
    TriggerFront front_;
    EnvCap       env_;
    Resonator    res_;
    OnePoleHP    hp_;
    double sr_ = 44100.0, baseTank_ = 3500.0, gateV_ = 1.0;
    int    gateSamp_ = 0;
};

} /* namespace cr78 */

#endif /* CR78_PERC_CIRCUIT_H */
