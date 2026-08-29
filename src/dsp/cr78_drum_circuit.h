/*
 * cr78_drum_circuit.h — the CR-78's phase-shift drum channel.
 *
 * Family B: bass drum, low bongo, hi bongo, low conga. Four lanes, ONE
 * circuit, four sets of capacitors. Read off page 25 of the service notes.
 *
 * A trigger through the standard front end strikes a THREE-SECTION RC
 * PHASE-SHIFT LADDER which sits inside a transistor's feedback loop
 * (Q501-Q504, 1.5 M of feedback, a 10 k collector load, a 100 R emitter with
 * a 500 R trimmer across it).
 *
 * Three series capacitors, three 10 k shunt resistors to ground, driven
 * through a 15 k series resistor and loaded by the 1.5 M feedback. The
 * transistor inverts; the ladder supplies the other 180 degrees; the loop
 * rings at the frequency where it does so.
 *
 * DECAY IS LOOP GAIN, NOT AN ENVELOPE. A phase-shift ladder is not a
 * resonator — on its own it does not ring at all, it just attenuates — and
 * everything you hear is the loop failing to lose the signal. That is why the
 * 500 R emitter trimmers are the decay adjustment on the hardware: they set
 * degeneration, which sets loop gain, which sets how long the note lasts.
 * Nothing in this channel is an envelope generator.
 *
 * THE FREQUENCIES ARE DERIVED. THERE IS NO FITTED CONSTANT.
 *
 * An earlier version of this file read the network as a bridged-T and carried
 * a fudge factor of 3.0 to reach the factory table, defended on the grounds
 * that one constant held to +/-6% across a 10:1 span. That was the right
 * suspicion and the wrong circuit: re-read at 420 dpi the network is plainly
 * three series capacitors with three shunt resistors, which is a phase-shift
 * ladder and not a bridged-T at all.
 *
 * Solving the ladder for the frequency at which it turns the phase by 180
 * degrees — an ordinary ABCD chain, done once at init by
 * phaseShiftLadderFreq() below — gives, against page 30:
 *
 *     bass drum   63.0 Hz  vs  62.5   +0.7%
 *     low conga  209.1 Hz  vs 208.0   +0.5%
 *     low bongo  406.6 Hz  vs 400.0   +1.6%
 *     hi bongo   629.6 Hz  vs 600.0   +4.9%
 *
 * Nothing is fitted. Those are the capacitors on the schematic and the 10 k
 * resistors beside them, put through the network equations, landing on
 * numbers Roland measured off hardware and printed in 1979.
 *
 * WHAT IS STILL FITTED, and it is a smaller thing: the equivalent Q used for
 * the ring. The DSP models the ladder's behaviour near its oscillation
 * frequency with a two-pole resonator, which is an approximation, and the
 * resonator's Q is chosen so that the loop-gain formula reproduces page 30's
 * decay times. The frequencies are physics; the Q is a fit to the other
 * column of the same table.
 *
 * GPL-3.0.
 */
#ifndef CR78_DRUM_CIRCUIT_H
#define CR78_DRUM_CIRCUIT_H

#include <math.h>

#include "cr78_circuit.h"

namespace cr78 {

/* ---- component values, CR-78 service notes p.25, VG-11A ---------------- */

/*
 * Shared by all four channels; only the capacitors change.
 *
 *   Rshunt  the three ladder resistors to ground. One of the three is the
 *           lane's tune trimmer (VR51/53/55/57, 10 k B), which is why they
 *           are 10 k and why the panel's Tune knob stands in for it.
 *   Rin     the series resistor feeding the ladder (R604/R614/R624/R634).
 *   Rfb     the collector-to-base feedback (R608/R618/R628/R638), which also
 *           loads the ladder's output end.
 */
static const double kDRUM_Rshunt = 10.0e3;
static const double kDRUM_Rin    = 15.0e3;
static const double kDRUM_Rfb    = 1.5e6;

/*
 * The two-pole equivalent Q for the ring. FITTED — see the header. It is the
 * one number in this file that does not come off the schematic, and it is
 * fitted to page 30's DECAY column rather than chosen by ear.
 */
static const double kDRUM_Q = 0.612;

struct DrumSpec {
    const char *id;
    double c1, c2, c3;      /* the three series caps of the ladder */
    double fFactory;        /* page 30, the alignment target                */
    double decay;           /* page 30, seconds to a tenth of peak (page 30's definition)               */
    double amp;             /* page 30, Vpp — this IS the kit balance       */
    double strike;          /* input drive, from the front-end series R     */
};

/*
 * The four channels. `strike` is 390 k / R for the channel's own front-end
 * series resistor (R602/R612/R622/R632 are all 390 k on these four, so the
 * bass drum's 190 k reads as the harder strike it is).
 */
static const DrumSpec kDRUM_BD = { "bd", 82.0e-9, 68.0e-9, 68.0e-9,  62.5, 0.100, 0.40, 2.05 };
static const DrumSpec kDRUM_LC = { "lc", 22.0e-9, 22.0e-9, 22.0e-9, 208.0, 0.150, 0.30, 1.00 };
static const DrumSpec kDRUM_LB = { "lb", 12.0e-9, 12.0e-9, 10.0e-9, 400.0, 0.040, 0.15, 1.00 };
static const DrumSpec kDRUM_HB = { "hb",  8.2e-9,  6.8e-9,  6.8e-9, 600.0, 0.040, 0.15, 1.00 };

/*
 * The frequency the three-section ladder actually oscillates at: where it
 * turns the phase by 180 degrees, so that with the transistor's own inversion
 * the loop closes in phase.
 *
 * An ABCD chain — source resistance, then (series C, shunt R) three times,
 * then the feedback resistor as the load — bisected on phase. Run ONCE per
 * voice at init(), never on the audio path; Tune afterwards is a plain ratio,
 * because every frequency in this network scales as 1/RC.
 *
 * Written out rather than reduced to a closed form on purpose. The textbook
 * 1/(2 pi R C sqrt 6) assumes three identical sections, an ideal source and
 * no load, and this network has none of those three: the capacitors differ
 * per lane, R604 feeds it from 15 k and R608 loads it with 1.5 M. Solving the
 * real chain is what makes the answer land on the factory table without a
 * correction factor.
 */
static inline double phaseShiftLadderFreq(const double _rIn,
                                          const double _c1, const double _c2,
                                          const double _c3, const double _rSh,
                                          const double _rLoad)
{
    /* phase of the ladder's transfer at f, unwrapped into (0, 2pi) — three
     * highpass sections start near 270 degrees and fall monotonically */
    struct Ph {
        static double at(double f, double rIn, double c1, double c2, double c3,
                         double rSh, double rLoad)
        {
            const double w = 2.0 * kPi * f;
            const double c[3] = { c1, c2, c3 };
            /* ABCD: [[a,b],[c,d]] */
            double ar = 1, ai = 0, br = 0, bi = 0, cr = 0, ci = 0, dr = 1, di = 0;
            #define SER(zr, zi) do {                                    \
                const double nbr = br + (ar * (zr) - ai * (zi));         \
                const double nbi = bi + (ar * (zi) + ai * (zr));         \
                const double ndr = dr + (cr * (zr) - ci * (zi));         \
                const double ndi = di + (cr * (zi) + ci * (zr));         \
                br = nbr; bi = nbi; dr = ndr; di = ndi;                  \
            } while(0)
            #define SHU(y) do {                                         \
                ar = ar + br * (y);                                     \
                ai = ai + bi * (y);                                     \
                cr = cr + dr * (y);                                     \
                ci = ci + di * (y);                                     \
            } while(0)
            SER(rIn, 0.0);
            for(int i = 0; i < 3; ++i)
            {
                SER(0.0, -1.0 / (w * c[i]));   /* 1/(jwC) = -j/(wC) */
                SHU(1.0 / rSh);
            }
            SHU(1.0 / rLoad);
            /* H = 1/A, so phase(H) = -phase(A) */
            double p = -atan2(ai, ar);
            while(p < 0.0)         p += 2.0 * kPi;
            while(p > 2.0 * kPi)   p -= 2.0 * kPi;
            return p;
            #undef SER
            #undef SHU
        }
    };
    double lo = 1.0, hi = 20000.0;
    for(int i = 0; i < 200; ++i)
    {
        const double m = sqrt(lo * hi);
        if(Ph::at(m, _rIn, _c1, _c2, _c3, _rSh, _rLoad) > kPi) lo = m; else hi = m;
    }
    return sqrt(lo * hi);
}

/* What the components give, with nothing fitted. */
static inline double drumDerivedFreq(const DrumSpec &s)
{
    return phaseShiftLadderFreq(kDRUM_Rin, s.c1, s.c2, s.c3,
                                kDRUM_Rshunt, kDRUM_Rfb);
}

/* Where VR51/53/55/57 sit to reach the factory table. Now within 5% of unity
 * on every lane, against trimmers with far more authority than that — which
 * is the whole point: the trimmers exist to absorb component tolerance, not
 * to rescue a wrong model. */
static inline double drumTuneTrim(const DrumSpec &s)
{
    return s.fFactory / drumDerivedFreq(s);
}

/*
 * The strike does not arrive as a clean impulse. The bass drum's scope trace
 * on page 30 shows the note reaching 0.4 V and settling into a decaying sine
 * — the loop takes a few cycles to build, which is what gives these lanes
 * their soft front. StrikeDrive pushes the strike in hard enough that the
 * transistor clip catches the first swings, so the attack compresses instead
 * of clicking.
 */
static const double kDRUM_StrikeDrive = 3.2;

/*
 * THE STRIKE FEEDTHROUGH — the kick's attack, and it is not a sweetener.
 *
 * A three-section series-C / shunt-R ladder is a HIGHPASS network: at high
 * frequency the capacitors are short circuits and the trigger's edge walks
 * straight through to the transistor. So the real channel's output is the
 * ring PLUS a leaked copy of the strike — a brief broadband tick on the front
 * of every note.
 *
 * The two-pole resonator that models the ring is a bandpass and has no such
 * path, and for the first several builds nothing put it back. On monitors
 * nobody noticed: a 62.5 Hz fundamental carries the note. On the Move's
 * speaker, which reproduces nothing down there, the tick is most of what a
 * kick IS — and without it the lane was reported from hardware as "no power,
 * nothing", which was exactly right.
 *
 * The bleed is highpassed at 700 Hz (the ladder's leak is spectrally rising;
 * a flat bleed just thumps) and scaled per-lane by the same strike factor as
 * the ring, then clipped WITH the ring so a hard hit compresses both
 * together, as the transistor would.
 */
static const double kDRUM_ClickBleed = 0.55;
static const double kDRUM_ClickHpf   = 700.0;

/*
 * A small pitch lift at the onset. The bridged-T's own components shift as
 * the transistor comes out of cutoff and its junction capacitance changes,
 * which every drum of this family does to some degree. FITTED — the service
 * notes give no figure for it, and page 30's scope trace is too coarse to
 * measure one off. Kept small deliberately: an audible sweep here would be a
 * caricature, and the CR-78's drums are not sweepy.
 */
static const double kDRUM_PitchLift = 0.035;
static const double kDRUM_PitchTau  = 0.030;

class DrumVoice {
public:
    void init(const DrumSpec &_spec, const double _sr)
    {
        spec_ = &_spec;
        sr_   = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        res_.reset();
        clickHp_.set(kDRUM_ClickHpf, sr_);
        fb_ = 0.0;
        env_ = 0.0;
        gateSamp_ = 0;
        lift_ = 0.0;
        liftA_ = exp(-1.0 / (kDRUM_PitchTau * sr_));
        setTune(1.0);
        setDecay(_spec.decay);
        applyFreq();
    }

    void reset()
    {
        front_.reset(); res_.reset(); clickHp_.reset();
        fb_ = 0.0; env_ = 0.0; gateSamp_ = 0; lift_ = 0.0;
    }

    /* `_ratio` multiplies the factory frequency — the panel's Tune knob
     * standing in for the hardware's tune trimmer. */
    void setTune(const double _ratio)
    {
        double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        f0_ = spec_->fFactory * r;
        applyFreq();
    }

    /* Seconds to a tenth of peak — page 30's own definition. The circuit solves the loop gain that produces
     * it at the CURRENT pitch, so the knob keeps its meaning when Tune moves. */
    void setDecay(const double _seconds)
    {
        decay_ = _seconds;
        applyFreq();
    }

    /* `_vel` is the trigger voltage, 0..1. On this channel velocity is a
     * VOLTAGE into the front end's diode and then into the transistor clip,
     * not a gain — a harder hit is a slightly different sound and not only a
     * louder one, which is what the circuit actually does. */
    void trigger(const double _vel)
    {
        gateSamp_ = (int)(0.001 * sr_);        /* the 8048's ~1 ms pulse */
        gateV_ = 0.5 + 0.5 * _vel;
        lift_  = kDRUM_PitchLift;
    }

    bool active() const { return gateSamp_ > 0 || fabs(fb_) > 1.0e-6; }

    double process()
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate) * spec_->strike * kDRUM_StrikeDrive;

        if(lift_ > 1.0e-6)
        {
            lift_ *= liftA_;
            res_.set(f0_ * (1.0 + lift_), qEff_, sr_);
        }

        const double y = res_.process(strike) * loopGain_;
        const double click = clickHp_.process(strike) * kDRUM_ClickBleed;
        fb_ = transistorClip(y + click);
        return fb_;
    }

    double amplitude() const { return spec_->amp; }

private:
    /*
     * THE RING IS SET BY THE RESONATOR'S Q, NOT BY AN EXPLICIT FEEDBACK LOOP.
     *
     * The circuit is a loop and this file says so throughout — that part is
     * unchanged and it is why Decay behaves the way it does. What changed is
     * the numerics. An explicit `y = H(strike + g*fb); fb = clip(y)` closes
     * the loop through a ONE-SAMPLE DELAY, and at 44.1 kHz that is 4.9 degrees
     * of phase at 600 Hz. A high-Q loop cannot tolerate that: the feedback no
     * longer arrives in phase at f0, the pole lands short of where the gain
     * says it should, and the ring comes out too brief — measured 28% short on
     * the hi bongo, 22% on the conga, worst at the top of the range, which is
     * the signature of a fixed per-sample error.
     *
     * Folding the loop into the pole is exact and cheaper: a loop of gain g
     * around a resonator of quality Q has the pole of a resonator of quality
     * Q/(1-g), and that is precisely qForRing() at the requested decay. The
     * transistor clip stays — it is real, it is what stops the note growing
     * without bound and what compresses the attack — but it now sits on the
     * OUTPUT rather than inside the delay, so it colours the sound without
     * costing the ring.
     */
    void applyFreq()
    {
        if(!spec_) return;
        qEff_ = qForRing(f0_, decay_);
        if(qEff_ < kDRUM_Q) qEff_ = kDRUM_Q;   /* never damp below the network */
        /*
         * THE LOOP'S RESONANT GAIN, and losing it is what gutted the kick.
         *
         * A regenerative loop does two things at once: it stretches the ring
         * (pole Q becomes Q/(1-g)) and it AMPLIFIES at f0 by 1/(1-g) — at the
         * kick's factory decay that is a factor of ~14. The first pole-fold
         * kept the stretch and normalised the gain to unity, so the ring
         * collapsed 23 dB and the lane came off the device as "no power,
         * nothing" while every offline check still passed, because the trims
         * had been re-fitted onto the wreckage.
         *
         * Q_eff / Q_network IS 1/(1-g), so this one ratio restores exactly
         * what the loop provided. It also makes the Decay pot behave the way
         * a regenerative circuit does: longer decay rings LOUDER, not just
         * longer — turn VR58 up on the hardware and the note swells the same
         * way.
         */
        loopGain_ = qEff_ / kDRUM_Q;
        res_.set(f0_, qEff_, sr_);
    }

    const DrumSpec *spec_ = 0;
    TriggerFront front_;
    Resonator    res_;
    OnePoleHP    clickHp_;
    double sr_ = 44100.0, f0_ = 100.0, decay_ = 0.1;
    double qEff_ = 0.6, loopGain_ = 1.0, fb_ = 0.0, env_ = 0.0;
    double gateV_ = 1.0, lift_ = 0.0, liftA_ = 0.0;
    int    gateSamp_ = 0;
};

} /* namespace cr78 */

#endif /* CR78_DRUM_CIRCUIT_H */
