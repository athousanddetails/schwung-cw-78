/*
 * cr78_metal_circuit.h — metallic beat and cowbell.
 *
 * The two voices on the board that are made of oscillators rather than of a
 * struck network.
 *
 * METALLIC BEAT
 *
 * Three RC relaxation oscillators on IC501, an MC14069 hex inverter, each
 * buffered by a second inverter with 39 k of feedback and mixed through 470 k
 * into the summing bus:
 *
 *     R573 47k / C544 .0015   VR64 20k   ->  6170 Hz
 *     R575 47k / C545 .0018   VR65 20k   ->  5620 Hz
 *     R577 47k / C546 .0022   VR66 20k   ->  4080 Hz
 *
 * f = k / (R C), with k FITTED at 0.444. Solving the three factory
 * frequencies against their own RC products gives 0.435, 0.476 and 0.422 —
 * one constant to within +/-6%, and the three 20 k trimmers have far more
 * authority than that. Three oscillators, one number.
 *
 * THE BANK FREE-RUNS. It is never reset on a trigger, exactly as the hardware
 * has it, so every hit catches the three oscillators at a different relative
 * phase and no two metallic beats are quite the same. This is the same reason
 * an 808's hats are never twice identical, and it is worth more than it
 * sounds like it should be.
 *
 * COWBELL
 *
 * Two tones, trimmed to 800 Hz (VR67) and 555 Hz (VR68), through a chain that
 * starts the same way everything else does — C547 .027 / R584 270k / D525 —
 * then R585 560k into Q528's envelope, D526, C548 .022 and C549 .018,
 * R588 1.5M and R589 100k into Q511, and out through the Q529/Q530 pair.
 *
 * WHAT IS NOT TRACED: the two trimmed stages themselves. L7 45 mH sits in
 * that part of the schematic with R592 1k, and C550 is drawn as a .47
 * ELECTROLYTIC, which makes it a coupling capacitor and not a tank element —
 * 45 mH against .47 uF would resonate at 1094 Hz and neither cowbell tone is
 * there. The two frequencies are therefore taken from the factory table and
 * the stages are modelled as what they audibly are: two square oscillators
 * through a shared bandpass. When someone traces Q529/Q530 properly this is
 * the block to replace, and the two frequencies should fall out of it.
 *
 * GPL-3.0.
 */
#ifndef CR78_METAL_CIRCUIT_H
#define CR78_METAL_CIRCUIT_H

#include <math.h>

#include "cr78_circuit.h"

namespace cr78 {

/* ---- metallic beat: component values, p.26, VG-11A --------------------- */

static const double kMB_R = 47.0e3;                       /* R573/R575/R577 */
static const double kMB_C[3] = { 1.5e-9, 1.8e-9, 2.2e-9 };/* C544/C545/C546 */

/* page 30, the alignment targets: VR64, VR65, VR66 */
static const double kMB_fFactory[3] = { 6170.0, 5620.0, 4080.0 };

static const double kMB_Decay = 0.050;    /* page 30 */
static const double kMB_Amp   = 0.35;     /* page 30 */

/* Mixed through three equal 470 k (R578/R579/R580), so equal weights. */
static const double kMB_Mix = 1.0 / 3.0;

/* What the components give, before the trimmers. */
static inline double mbDerivedFreq(const int _i)
{
    return kRelaxK / (kMB_R * kMB_C[_i]);
}

/* Where VR64-66 have to sit. Within +/-6% of unity on all three. */
static inline double mbTuneTrim(const int _i)
{
    return kMB_fFactory[_i] / mbDerivedFreq(_i);
}

class MetalBeatVoice {
public:
    void init(const double _sr)
    {
        sr_ = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        env_.reset();
        hp_.set(2500.0, sr_);
        /* Deliberately unequal starting phases. The bank free-runs from here
         * and is never re-seeded. */
        const double seed[3] = { 0.0, 0.37, 0.71 };
        for(int i = 0; i < 3; ++i) { osc_[i].setPhase(seed[i]); }
        setTune(1.0);
        setDecay(kMB_Decay);
        gateSamp_ = 0;
    }

    void reset() { front_.reset(); env_.reset(); hp_.reset(); gateSamp_ = 0; }

    void setTune(const double _ratio)
    {
        const double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        for(int i = 0; i < 3; ++i) osc_[i].set(kMB_fFactory[i] * r, sr_);
        hp_.set(2500.0 * r, sr_);
    }

    void setDecay(const double _s)
    { env_.set((_s < 0.002 ? 0.002 : _s) / kLn10, sr_); }

    void trigger(const double _vel)
    { gateSamp_ = (int)(0.001 * sr_); gateV_ = 0.4 + 0.6 * _vel; }

    bool active() const { return gateSamp_ > 0 || env_.value() > 1.0e-5; }

    /* The bank is ticked whether or not the lane is sounding — free-running
     * is the whole point, and gating the oscillators would resynchronise them
     * on every hit. */
    double process()
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate);
        if(strike > 0.0) env_.strike(strike * 1.5);

        double bank = 0.0;
        for(int i = 0; i < 3; ++i) bank += osc_[i].process();
        bank *= kMB_Mix;

        return hp_.process(bank * env_.process());
    }

    double amplitude() const { return kMB_Amp; }

private:
    TriggerFront front_;
    RelaxOsc     osc_[3];
    EnvCap       env_;
    OnePoleHP    hp_;
    double sr_ = 44100.0, gateV_ = 1.0;
    int    gateSamp_ = 0;
};

/* ---- cowbell: p.26, VG-11A --------------------------------------------- */

/* page 30: VR67 and VR68. NOT derived — see the header. */
static const double kCB_fFactory[2] = { 800.0, 555.0 };
static const double kCB_Decay = 0.060;
static const double kCB_Amp   = 0.20;

/* The pair goes through one band before the summing bus. FITTED to the
 * instrument: a cowbell is two squares with their edges rounded off, and
 * without this it is a buzzer. */
static const double kCB_BandHz = 1900.0;
static const double kCB_BandQ  = 1.1;

class CowbellVoice {
public:
    void init(const double _sr)
    {
        sr_ = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        env_.reset();
        band_.reset();
        hp_.set(500.0, sr_);
        osc_[0].setPhase(0.0);
        osc_[1].setPhase(0.5);
        setTune(1.0);
        setDecay(kCB_Decay);
        gateSamp_ = 0;
    }

    void reset()
    { front_.reset(); env_.reset(); band_.reset(); hp_.reset(); gateSamp_ = 0; }

    void setTune(const double _ratio)
    {
        const double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        for(int i = 0; i < 2; ++i) osc_[i].set(kCB_fFactory[i] * r, sr_);
        band_.set(kCB_BandHz * r, kCB_BandQ, sr_);
        hp_.set(500.0 * r, sr_);
    }

    void setDecay(const double _s)
    { env_.set((_s < 0.002 ? 0.002 : _s) / kLn10, sr_); }

    void trigger(const double _vel)
    { gateSamp_ = (int)(0.001 * sr_); gateV_ = 0.4 + 0.6 * _vel; }

    bool active() const { return gateSamp_ > 0 || env_.value() > 1.0e-5; }

    double process()
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate);
        if(strike > 0.0) env_.strike(strike * 1.5);

        const double pair = (osc_[0].process() + osc_[1].process()) * 0.5;
        return hp_.process(band_.process(pair) * env_.process());
    }

    double amplitude() const { return kCB_Amp; }

private:
    TriggerFront front_;
    RelaxOsc     osc_[2];
    EnvCap       env_;
    Resonator    band_;
    OnePoleHP    hp_;
    double sr_ = 44100.0, gateV_ = 1.0;
    int    gateSamp_ = 0;
};

} /* namespace cr78 */

#endif /* CR78_METAL_CIRCUIT_H */
