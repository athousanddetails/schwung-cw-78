/*
 * cr78_tank_circuit.h — the two passive LC voices: claves and rim shot.
 *
 * Family A, and the simplest thing on the board. There is no transistor in
 * either of these voices at all. The trigger goes through the standard front
 * end into a series resistor, kicks a parallel LC tank, and the tank rings
 * down on its own losses. That is the entire circuit.
 *
 *   claves    C500 .027 / R500 270k / D500 / R501 22k / R502 47k
 *             -> C501 .0047, C502 .0022 -> L1 || C506 .0047
 *             -> C503 250p, C505 .0056, C504 .001 -> out
 *
 *   rim shot  C590 .027 / R503 270k / D501 / R504 47k / R505 47k
 *             -> L2 || C591 .015 || C592 .0015 -> C593 470p -> out
 *
 * THE RIM SHOT IS THE CLEANEST RESULT IN THIS WHOLE PROJECT.
 *
 * L1 and L2 are both marked "3R" on the schematic, which the parts list on
 * page 31 resolves to 022-033, "coil no.33 3R 700mH". With the rim shot's
 * C591 + C592 = 16.5 nF:
 *
 *     f = 1 / (2 pi sqrt(0.7 * 16.5e-9)) = 1481 Hz
 *
 * and page 30's factory alignment table says the rim shot is trimmed to
 * 1480 Hz. Four figures, from the parts list and the schematic alone, with
 * nothing fitted. That agreement is also what confirms the 700 mH reading
 * for L1 — the claves share the coil.
 *
 * The claves are the same derivation and do not land as cleanly: L1 with
 * C506 .0047 alone gives 2774 Hz against a factory 2630. The tank is loaded
 * by C502, C503 and C505 around it, and adding C503's 250 p alone brings it
 * to 2703. The loading is real; the factory number is the target.
 *
 * DECAY IS Q, not an envelope and not a loop — there is nothing here to
 * sustain anything. The Decay pot moves the tank's Q directly, and the
 * factory decays (rim shot 5 ms, claves 18 ms) are what the defaults ask for.
 *
 * GPL-3.0.
 */
#ifndef CR78_TANK_CIRCUIT_H
#define CR78_TANK_CIRCUIT_H

#include <math.h>

#include "cr78_circuit.h"

namespace cr78 {

/* ---- component values, CR-78 service notes p.25, VG-11A ---------------- */

/* Coil 022-033, "no.33 3R 700mH", shared by both voices as L1 and L2. */
static const double kTANK_L = 0.700;

/* Rim shot: C591 .015 + C592 .0015 in parallel across L2. */
static const double kRS_C = 15.0e-9 + 1.5e-9;

/* Claves: C506 .0047 across L1, plus C503's 250 p of the loading that pulls
 * the tank down toward the factory figure. */
static const double kC_C  = 4.7e-9 + 250.0e-12;

struct TankSpec {
    const char *id;
    double l, c;            /* the tank itself                          */
    double fFactory;        /* page 30, the alignment target            */
    double decay;           /* page 30, seconds to a tenth of peak (page 30's definition)           */
    double amp;             /* page 30, Vpp                             */
    double hpf;             /* the output coupling cap, as a corner      */
};

/*  id      L        C      f factory  decay   amp    hpf
 *  ------------------------------------------------------------------
 *  rs   700 mH   16.5 nF     1480    5 ms   0.8   C593 470p
 *  cl   700 mH    4.95 nF    2630   18 ms   0.15  C504 .001
 */
static const TankSpec kTANK_RS = { "rs", kTANK_L, kRS_C, 1480.0, 0.005, 0.80, 900.0 };
static const TankSpec kTANK_CL = { "cl", kTANK_L, kC_C,  2630.0, 0.018, 0.15, 1400.0 };

/* What the coil and the capacitors give, before any loading. */
static inline double tankDerivedFreq(const TankSpec &s)
{
    return lcFreq(s.l, s.c);
}

/*
 * The strike. A passive tank has no gain stage to compress the attack, so
 * these two voices are the sharpest transients in the kit — which is exactly
 * what a rim shot and a pair of claves are. The rim shot's 0.8 Vpp is the
 * loudest figure in the factory table and its 5 ms decay the shortest: it is
 * almost pure click.
 */
static const double kTANK_Strike = 6.0;

class TankVoice {
public:
    void init(const TankSpec &_spec, const double _sr)
    {
        spec_ = &_spec;
        sr_   = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        res_.reset();
        hp_.set(_spec.hpf, sr_);
        setTune(1.0);
        setDecay(_spec.decay);
        gateSamp_ = 0;
        ring_ = 0.0;
    }

    void reset()
    { front_.reset(); res_.reset(); hp_.reset(); gateSamp_ = 0; ring_ = 0.0; }

    void setTune(const double _ratio)
    {
        const double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        f0_ = spec_->fFactory * r;
        apply();
    }

    /* Seconds to a tenth of peak — page 30's own definition. On a passive tank this IS the Q. */
    void setDecay(const double _seconds) { decay_ = _seconds; apply(); }

    void trigger(const double _vel)
    {
        gateSamp_ = (int)(0.001 * sr_);
        gateV_    = 0.4 + 0.6 * _vel;
    }

    bool active() const { return gateSamp_ > 0 || fabs(ring_) > 1.0e-6; }

    double process()
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate) * kTANK_Strike;
        ring_ = res_.process(strike);
        return hp_.process(ring_);
    }

    double amplitude() const { return spec_->amp; }

private:
    void apply()
    {
        if(!spec_) return;
        /* The tank's losses, expressed as the ring time the panel asks for. */
        res_.set(f0_, qForRing(f0_, decay_), sr_);
    }

    const TankSpec *spec_ = 0;
    TriggerFront front_;
    Resonator    res_;
    OnePoleHP    hp_;
    double sr_ = 44100.0, f0_ = 1480.0, decay_ = 0.005;
    double gateV_ = 1.0, ring_ = 0.0;
    int    gateSamp_ = 0;
};

} /* namespace cr78 */

#endif /* CR78_TANK_CIRCUIT_H */
