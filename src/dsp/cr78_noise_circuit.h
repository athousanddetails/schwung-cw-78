/*
 * cr78_noise_circuit.h — the gated-noise voices: hi-hat, cymbal, maracas.
 *
 * Family C. One noise source for the whole machine, three lanes tapping it
 * through their own gate, their own envelope capacitor and their own filter.
 *
 * The lanes are IDENTICAL but for one capacitor each:
 *
 *   cymbal   C518 .027 / R525 270k / D510 -> R526 270k -> Q515
 *            envelope C520 .12  through R530 4.7M
 *            -> R531 33k -> Q507 -> L3 45mH || C521 .0068 -> C522/C523 470p
 *
 *   hi-hat   C524 .027 / R533 270k / D513 -> R534 330k -> Q516
 *            envelope C525 .018 through R537 1.5M -> C528 .0082 out
 *
 *   maracas  C526 .027 / R538 270k / D519 -> R539 560k -> Q517
 *            envelope C527 .0082 through R542 1M -> C527 .0082 out
 *
 * THE NOISE SOURCE IS SHARED AND SO IS ITS OUTPUT. Q533 is a 2SC828-R that
 * the parts list explicitly calls out as "(NZ) for noise" — a reverse-biased
 * junction avalanching — amplified by Q525 and bussed to all three lanes plus
 * the snare's snap through R566 470k and the VR60-63 trimmers. One source, so
 * a hat and a maracas landing on the same sixteenth are hearing the SAME
 * noise, correlated exactly as the hardware has them. The engine ticks it
 * once per sample and hands the same number to every lane.
 *
 * WHAT IS DERIVED AND WHAT IS FITTED
 *
 * Derived: the cymbal's filter. L3 45 mH with C521 .0068 resonates at
 * 9098 Hz, straight from the parts list and the schematic.
 *
 * Derived: the ORDERING and relative spacing of the three decays. The
 * envelope RCs are .12 x 4.7M = 564 ms, .018 x 1.5M = 27 ms and
 * .0082 x 1M = 8.2 ms, and the factory table asks for 350 / 60 / 20 ms in the
 * same order.
 *
 * FITTED: the absolute mapping from RC to audible decay. It is not the same
 * ratio on all three (1.6 / 0.45 / 0.41), and the reason is that the audible
 * tail ends where the envelope falls below the gating transistor's conduction
 * threshold, NOT where the capacitor reaches 1% of its charge. The service
 * notes give no operating point for Q515-Q517, so that threshold cannot be
 * derived and is not guessed at here: the Decay pot is seconds to inaudible,
 * its default is the factory figure, and the envelope is a plain RC scaled to
 * hit it. If an operating point ever turns up, this is the thing to replace.
 *
 * FITTED: the hi-hat's and maracas' filter corners. Neither has a tank on the
 * schematic — the character comes from the noise source's own colour and the
 * coupling caps — so the corners here are voiced to the instrument and say so.
 *
 * GPL-3.0.
 */
#ifndef CR78_NOISE_CIRCUIT_H
#define CR78_NOISE_CIRCUIT_H

#include <math.h>

#include "cr78_circuit.h"

namespace cr78 {

/* ---- component values, CR-78 service notes p.25-26, VG-11A ------------- */

/* The cymbal's tank: L3 45 mH (coil 022-030) || C521 .0068. DERIVED. */
static const double kCY_L = 45.0e-3;
static const double kCY_C = 6.8e-9;

/* The three envelope capacitors and their discharge resistors, kept here
 * because they are what makes these three lanes different from each other. */
static const double kCY_RC = 0.12e-6 * 4.7e6;   /* 564 ms */
static const double kHH_RC = 0.018e-6 * 1.5e6;  /*  27 ms */
static const double kM_RC  = 0.0082e-6 * 1.0e6; /* 8.2 ms */

struct NoiseSpec {
    const char *id;
    double decay;       /* page 30, seconds                              */
    double amp;         /* page 30, Vpp                                  */
    double tankHz;      /* 0 = no tank (hi-hat, maracas)                 */
    double tankQ;
    double hpf;         /* coupling / voicing corner                     */
    double drive;       /* the lane's series resistor, as relative gain  */
};

/*  id   decay   amp   tank Hz   Q     hpf     drive
 *  ------------------------------------------------------------------
 *  cy   350ms   0.4    9098   derived  1200   R526 270k
 *  hh    60ms   0.4      —      —      6500   R534 330k
 *  ma    20ms   0.4      —      —      3200   R539 560k
 */
static const NoiseSpec kNOISE_CY = { "cy", 0.350, 0.40, 0.0, 1.8, 1200.0, 1.00 };
static const NoiseSpec kNOISE_HH = { "hh", 0.060, 0.40, 0.0, 0.0, 6500.0, 0.82 };
static const NoiseSpec kNOISE_MA = { "ma", 0.020, 0.40, 0.0, 0.0, 3200.0, 0.48 };

/* The cymbal's tank frequency, from the parts list. Not a constant in the
 * struct because it is DERIVED and should read as such at the call site. */
static inline double cymbalTankFreq() { return lcFreq(kCY_L, kCY_C); }

class NoiseVoice {
public:
    /* `_tankHz` overrides the spec — the cymbal passes cymbalTankFreq() so
     * the derivation stays visible in the engine rather than being copied
     * into a table. */
    void init(const NoiseSpec &_spec, const double _sr, const double _tankHz = 0.0)
    {
        spec_ = &_spec;
        sr_   = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        env_.reset();
        hp_.set(_spec.hpf, sr_);
        tankHz_ = _tankHz > 0.0 ? _tankHz : _spec.tankHz;
        baseTank_ = tankHz_;
        if(tankHz_ > 0.0) { res_.reset(); res_.set(tankHz_, _spec.tankQ, sr_); }
        setDecay(_spec.decay);
        gateSamp_ = 0;
    }

    void reset()
    { front_.reset(); env_.reset(); hp_.reset(); res_.reset(); gateSamp_ = 0; }

    /* Tune is a RATIO on the filter, not a note — these lanes have no pitch. */
    void setTune(const double _ratio)
    {
        const double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        hp_.set(spec_->hpf * r, sr_);
        if(baseTank_ > 0.0)
        {
            tankHz_ = baseTank_ * r;
            res_.set(tankHz_, spec_->tankQ, sr_);
        }
    }

    /* Seconds to inaudible. See the header on why this is fitted. */
    void setDecay(const double _seconds)
    {
        decay_ = _seconds < 0.002 ? 0.002 : _seconds;
        env_.set(decay_ / kLn10, sr_);
    }

    /* Velocity is the charge put on the envelope capacitor — a trigger
     * voltage, as everywhere else on this board. */
    void trigger(const double _vel)
    {
        gateSamp_ = (int)(0.001 * sr_);
        gateV_    = 0.35 + 0.65 * _vel;
    }

    /* Choke: the hardware shares one metal source between the hats, so a
     * closed hat lands on top of an open one. Here it dumps the capacitor. */
    void choke() { env_.reset(); }

    bool active() const { return gateSamp_ > 0 || env_.value() > 1.0e-5; }

    /* `_noise` is the SHARED source, ticked once per sample by the engine. */
    double process(const double _noise)
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate);
        if(strike > 0.0) env_.strike(strike * 1.6);

        const double e = env_.process();
        double y = _noise * e * spec_->drive;
        if(tankHz_ > 0.0) y = res_.process(y);
        return hp_.process(y);
    }

    double amplitude() const { return spec_->amp; }

private:
    const NoiseSpec *spec_ = 0;
    TriggerFront front_;
    EnvCap       env_;
    Resonator    res_;
    OnePoleHP    hp_;
    double sr_ = 44100.0, decay_ = 0.06;
    double tankHz_ = 0.0, baseTank_ = 0.0, gateV_ = 1.0;
    int    gateSamp_ = 0;
};

} /* namespace cr78 */

#endif /* CR78_NOISE_CIRCUIT_H */
