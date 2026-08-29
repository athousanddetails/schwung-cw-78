/*
 * cr78_circuit.h — the blocks the CR-78's voice circuits share.
 *
 * The CR-78 does not have fourteen voice designs. Reading pages 25 and 26 of
 * the service notes, it has three families and a handful of one-offs:
 *
 *   A  a passive LC tank, shock-excited and left to ring   (claves, rim shot)
 *   B  a bridged-T network inside a transistor feedback loop
 *                                  (bass drum, bongos, conga, the snare shell)
 *   C  the shared noise source through a gated RC envelope
 *                                       (hi-hat, cymbal, maracas, snare snap)
 *
 * plus an inverter relaxation bank (metallic beat), a cross-coupled astable
 * (guiro) and a two-tone trimmed pair (cowbell).
 *
 * Every one of the fourteen starts identically: the trigger line through a
 * 0.027 uF capacitor, a 270 k resistor to ground and a steering diode. That
 * is one block, here, once.
 *
 * The frequencies, decay times and amplitudes these blocks are fitted to come
 * from the factory alignment table on page 30 — Roland's own numbers, not
 * ours. docs/CR78-CIRCUIT-ANALYSIS.md records which of them are derived from
 * component values and which are fitted, and this file is expected to keep
 * saying which is which at each site.
 *
 * GPL-3.0.
 */
#ifndef CR78_CIRCUIT_H
#define CR78_CIRCUIT_H

#include <math.h>

namespace cr78 {

static const double kPi = 3.14159265358979323846;

/*
 * DECAY IS QUOTED AS TIME TO ONE TENTH OF PEAK, which is ln(10).
 *
 * This is Roland's own definition and it is drawn, not written: the figure at
 * the foot of page 30 shows a decaying sine with "Amplitude V" at the left,
 * "Decay time" spanning to a point marked "1/10 V", and the trace continuing
 * past it. So every figure in the alignment table — the bass drum's 100 ms,
 * the cymbal's 350, the rim shot's 5 — is time to MINUS TWENTY dB.
 *
 * This file used ln(100) for its first several revisions, i.e. it read those
 * figures as time to 1% and to -40 dB. The arithmetic was self-consistent and
 * every test passed, because voice_check measured to the same wrong
 * threshold. What it meant was that EVERY VOICE IN THE MODULE WAS HALF THE
 * LENGTH OF THE REAL MACHINE'S — and a kit of half-length voices reads as
 * thin and quiet, which is exactly how it reported from hardware.
 *
 * The lesson worth keeping: a measurement convention is part of a
 * specification. "100 ms" is not a number, it is a number and a threshold,
 * and the threshold was sitting in a figure rather than in the table.
 */
static const double kLn10 = 2.302585092994046;

/* ------------------------------------------------------------------ *
 *  Biquad — the generic second-order section.
 *
 *  The send buses need a highpass and the voices need bandpasses and
 *  lowpasses. RBJ cookbook forms, transposed direct form II because the
 *  voice coefficients are time-varying (pitch sweeps) and TDF-II is the
 *  well-behaved form under that.
 *
 *  setHiPass takes the RECIPROCAL of Q, matching the call sites ported from
 *  the framework: Butterworth is rq = sqrt(2), not 1/sqrt(2).
 * ------------------------------------------------------------------ */
class Biquad {
public:
    void reset() { z1_ = z2_ = 0.0; }

    void setHiPass(const double _f, const double _rq, const double _sr)
    {
        const double w0 = 2.0 * kPi * clampf(_f, 10.0, _sr * 0.45) / _sr;
        const double alpha = sin(w0) * 0.5 * _rq;
        const double c = cos(w0), a0 = 1.0 + alpha;
        b0_ =  (1.0 + c) * 0.5 / a0;
        b1_ = -(1.0 + c) / a0;
        b2_ =  b0_;
        a1_ = -2.0 * c / a0;
        a2_ =  (1.0 - alpha) / a0;
    }

    void setLoPass(const double _f, const double _rq, const double _sr)
    {
        const double w0 = 2.0 * kPi * clampf(_f, 10.0, _sr * 0.45) / _sr;
        const double alpha = sin(w0) * 0.5 * _rq;
        const double c = cos(w0), a0 = 1.0 + alpha;
        b0_ = (1.0 - c) * 0.5 / a0;
        b1_ = (1.0 - c) / a0;
        b2_ = b0_;
        a1_ = -2.0 * c / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    double process(const double _x)
    {
        const double y = b0_ * _x + z1_;
        z1_ = b1_ * _x - a1_ * y + z2_;
        z2_ = b2_ * _x - a2_ * y;
        return y;
    }

private:
    static double clampf(double v, double lo, double hi)
    { return v < lo ? lo : (v > hi ? hi : v); }
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, a1_ = 0.0, a2_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0;
};

/* ------------------------------------------------------------------ *
 *  The trigger front end — C 0.027 uF, R 270 k to ground, steering diode.
 *
 *  Identical on all fourteen lanes: C500/R500/D500 (claves), C590/R503/D501
 *  (rim shot), C567/R601/D502 (hi bongo), C573/R611/D503 (low bongo),
 *  C579/R621/D504 (low conga), C584/R631/D505 (bass drum), C508/R506/D506
 *  (snare), C518/R525/D510 (cymbal), C524/R533/D513 (hi-hat),
 *  C526/R538/D519 (maracas), C547/R584/D525 (cowbell), and so on.
 *
 *  The corner is 1/(2 pi R C) = 21.8 Hz, which is far below the 8048's ~1 ms
 *  trigger pulse — so unlike the 808's high-shelf shaper this passes the
 *  pulse very nearly whole and the DIODE is what shapes it. One polarity
 *  survives, so there is ONE strike per trigger and no second spike on the
 *  falling edge. That is a real difference in character and not a
 *  simplification: it is why a CR-78 lane hits once and cleanly.
 * ------------------------------------------------------------------ */
class TriggerFront {
public:
    void init(const double _sampleRate,
              const double _r = 270.0e3,
              const double _c = 0.027e-6)
    {
        const double sr = _sampleRate >= 8000.0 ? _sampleRate : 44100.0;
        a_ = exp(-2.0 * kPi * (1.0 / (2.0 * kPi * _r * _c)) / sr);
        z_ = 0.0; y_ = 0.0;
    }

    void reset() { z_ = y_ = 0.0; }

    /* `_gate` is the trigger voltage while the pulse is high, 0 otherwise. */
    double process(const double _gate)
    {
        /* AC couple */
        y_ = a_ * (y_ + _gate - z_);
        z_ = _gate;
        /* 1S1588 silicon: nothing below a diode drop gets through, and the
         * knee is soft rather than a hard corner. */
        if(y_ <= 0.0) return 0.0;
        const double kVf = 0.55;
        return y_ > kVf ? y_ - kVf : (y_ * y_) / (2.0 * kVf);
    }

private:
    double a_ = 0.0, z_ = 0.0, y_ = 0.0;
};

/* ------------------------------------------------------------------ *
 *  A two-pole resonator, unity gain at f0.
 *
 *  Serves both family A (an LC tank ringing on its own losses) and family B
 *  (a bridged-T inside a loop). Normalising the peak to unity is what lets a
 *  feedback gain wrapped around this mean exactly "loop gain"; it also throws
 *  away the network's real forward gain, so every caller puts that back on
 *  the input side.
 * ------------------------------------------------------------------ */
class Resonator {
public:
    void reset() { z1_ = z2_ = 0.0; }

    void set(const double _f0, const double _q, const double _sr)
    {
        const double f = _f0 < 20.0 ? 20.0 : (_f0 > _sr * 0.45 ? _sr * 0.45 : _f0);
        const double q = _q < 0.3 ? 0.3 : (_q > 200.0 ? 200.0 : _q);
        const double w0 = 2.0 * kPi * f / _sr;
        const double alpha = sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0_ =  alpha / a0;
        a1_ = -2.0 * cos(w0) / a0;
        a2_ = (1.0 - alpha) / a0;
    }

    double process(const double _x)
    {
        const double y = b0_ * _x + z1_;
        z1_ = -a1_ * y + z2_;          /* b1 is zero for a bandpass */
        z2_ = -b0_ * _x - a2_ * y;
        return y;
    }

private:
    double b0_ = 0.0, a1_ = 0.0, a2_ = 0.0, z1_ = 0.0, z2_ = 0.0;
};

/* ------------------------------------------------------------------ *
 *  Component-value helpers.
 * ------------------------------------------------------------------ */

/* A parallel LC tank's resonance. This is the one derivation the service
 * notes confirm to four figures: the rim shot's L2 = 700 mH (coil 022-033,
 * marked "3R") with C591 .015 + C592 .0015 gives 1481 Hz, and page 30's
 * factory table says 1480. */
static inline double lcFreq(const double _l, const double _c)
{
    return 1.0 / (2.0 * kPi * sqrt(_l * _c));
}

/* A bridged-T's two numbers, from the shunt arm R1 and the series arm R2.
 *
 *   f0 = 1 / (2 pi sqrt(R1 R2 C1 C2))
 *   Q  = (1/2) sqrt(R2 / R1)
 */
static inline double bridgedTFreq(const double _r1, const double _r2,
                                  const double _c1, const double _c2)
{
    return 1.0 / (2.0 * kPi * sqrt(_r1 * _r2 * _c1 * _c2));
}

static inline double bridgedTQ(const double _r1, const double _r2)
{
    return 0.5 * sqrt(_r2 / _r1);
}

/* The Q a passive tank needs to ring for `_t` seconds (to a TENTH of peak,
 * page 30's own definition) at
 * `_f0`. Family A voices have no feedback, so their decay IS their Q and the
 * Decay pot moves it directly. */
static inline double qForRing(const double _f0, const double _t)
{
    const double t = _t < 1.0e-4 ? 1.0e-4 : _t;
    return kPi * _f0 * t / kLn10;
}

/* The loop gain a family-B resonator of quality `_q` needs to ring for `_t`
 * seconds at `_f0`. Inverse of qForRing through the loop:
 *
 *     g = 1 - ln(10) Q / (pi f0 t)
 *
 * so the Decay pot keeps its meaning in seconds when Tune moves. Clamped at
 * zero — below the network's natural ring the loop cannot push it shorter —
 * and below unity, or the loop is an oscillator and not a drum.
 */
static inline double loopGainForRing(const double _f0, const double _q,
                                     const double _t, const double _max = 0.995)
{
    const double t = _t < 1.0e-4 ? 1.0e-4 : _t;
    double g = 1.0 - kLn10 * _q / (kPi * _f0 * t);
    if(g < 0.0) g = 0.0;
    if(g > _max) g = _max;
    return g;
}

/* ------------------------------------------------------------------ *
 *  Transistor saturation.
 *
 *  With a decay trimmer up, a family-B loop gain approaches unity and a
 *  linear loop would run away. On the hardware the 2SC1815 stage simply
 *  cannot swing past its rail, so the note grows until the clip stops it and
 *  then decays. The CR-78 runs its voice board on +15/-5, so the swing is
 *  asymmetric — it clips harder on the negative half, which is part of why
 *  its drums have the edge they do.
 * ------------------------------------------------------------------ */
static inline double transistorClip(const double _v,
                                    const double _hi = 12.0,
                                    const double _lo = 4.5)
{
    return _v >= 0.0 ? _hi * tanh(_v / _hi) : _lo * tanh(_v / _lo);
}

/* ------------------------------------------------------------------ *
 *  An RC discharge envelope — the family-C decay.
 *
 *  Charged instantly by the trigger, then bled off through a resistor. The
 *  three noise lanes are the same circuit with one capacitor changed, and the
 *  caps are the whole story: cymbal C520 .12 through R530 4.7M, hi-hat
 *  C525 .018 through R537 1.5M, maracas C527 .0082 through R542 1M.
 * ------------------------------------------------------------------ */
class EnvCap {
public:
    void set(const double _tau, const double _sr)
    {
        const double t = _tau < 1.0e-4 ? 1.0e-4 : _tau;
        a_ = exp(-1.0 / (t * _sr));
    }
    void reset() { v_ = 0.0; }
    void strike(const double _v) { if(_v > v_) v_ = _v; }
    double process() { const double v = v_; v_ *= a_; return v; }
    double value() const { return v_; }
private:
    double a_ = 0.0, v_ = 0.0;
};

/* ------------------------------------------------------------------ *
 *  The shared noise source — Q533, a 2SC828-R selected for noise.
 *
 *  The parts list on page 31 lists 017-046 "2SC828-R (NZ) for noise", which
 *  is a reverse-biased base-emitter junction avalanching. One instance for
 *  the whole machine, bussed to the hi-hat, cymbal, maracas and the snare's
 *  snap through R566 470k and the four VR60-63 trimmers — so the lanes are
 *  CORRELATED, exactly as the hardware has them, and a hat and a maracas on
 *  the same sixteenth are hearing the same noise.
 *
 *  Ticked once per sample by the engine, never per voice.
 * ------------------------------------------------------------------ */
class NoiseSource {
public:
    void init(unsigned _seed = 0x1978u) { s_ = _seed ? _seed : 0x1978u; }
    /* xorshift32, scaled to +/-1. Cheap and white; the analogue source's own
     * colour comes from the RC network each lane taps it through, not from
     * here. */
    double process()
    {
        s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5;
        return (double)(int)s_ * (1.0 / 2147483648.0);
    }
private:
    unsigned s_ = 0x1978u;
};

/* ------------------------------------------------------------------ *
 *  An inverter relaxation oscillator — the metallic beat bank.
 *
 *  Three of these on IC501 (MC14069 hex inverter): R573 47k / C544 .0015,
 *  R575 47k / C545 .0018, R577 47k / C546 .0022, each trimmed by a 20 k
 *  preset and buffered by a second inverter with 39 k of feedback.
 *
 *  f = k / (R C) with k FITTED at 0.444: solving the three factory
 *  frequencies (6170, 5620, 4080 Hz) against their RC products gives
 *  0.435 / 0.476 / 0.422, a spread of +/-6% which is well inside what the
 *  three 20 k trimmers can move. One constant, three oscillators.
 *
 *  Free-running, like the hardware — it is never reset on a trigger, so every
 *  hit catches the bank at a different phase and no two are quite the same.
 * ------------------------------------------------------------------ */
static const double kRelaxK = 0.444;

class RelaxOsc {
public:
    void set(const double _f, const double _sr)
    { inc_ = (_f > 0.0 ? _f : 1.0) / _sr; }
    /* A CMOS inverter's output is a square between the rails. */
    double process()
    {
        p_ += inc_;
        if(p_ >= 1.0) p_ -= 1.0;
        return p_ < 0.5 ? 1.0 : -1.0;
    }
    void setPhase(const double _p) { p_ = _p; }
private:
    double inc_ = 0.0, p_ = 0.0;
};

/* ------------------------------------------------------------------ *
 *  A cross-coupled astable multivibrator — the guiro's scrape.
 *
 *  Q520 and Q52 with C529 .068, C530 .068, R543 82k, R544 56k, R545 56k and
 *  VR54 100k(B), running at 125 Hz (high) or 77 Hz (low) per the factory
 *  table. That is the SCRAPE RATE, not a pitch — each edge kicks the L4/C532
 *  tank at 6126 Hz and what you hear is a rasp.
 *
 *  Emits an impulse on each edge rather than a square, because it is the
 *  edges the tank responds to.
 * ------------------------------------------------------------------ */
class Astable {
public:
    void set(const double _f, const double _sr)
    { inc_ = (_f > 0.0 ? _f : 1.0) / _sr; }
    void reset() { p_ = 0.0; last_ = 0; }
    /* returns non-zero on the samples where the multivibrator flips */
    double process()
    {
        p_ += inc_;
        if(p_ >= 1.0) p_ -= 1.0;
        const int half = p_ < 0.5 ? 0 : 1;
        const double e = half != last_ ? (half ? 1.0 : -0.85) : 0.0;
        last_ = half;
        return e;
    }
private:
    double inc_ = 0.0, p_ = 0.0;
    int last_ = 0;
};

/* one-pole high pass */
class OnePoleHP {
public:
    void set(const double _f, const double _sr)
    { a_ = exp(-2.0 * kPi * _f / _sr); z_ = y_ = 0.0; }
    double process(const double _x)
    { y_ = a_ * (y_ + _x - z_); z_ = _x; return y_; }
    void reset() { z_ = y_ = 0.0; }
private:
    double a_ = 0, z_ = 0, y_ = 0;
};

/* one-pole low pass */
class OnePoleLP {
public:
    void set(const double _f, const double _sr)
    { a_ = exp(-2.0 * kPi * _f / _sr); y_ = 0.0; }
    double process(const double _x) { y_ = _x + a_ * (y_ - _x); return y_; }
    void reset() { y_ = 0.0; }
private:
    double a_ = 0, y_ = 0;
};

} /* namespace cr78 */

#endif /* CR78_CIRCUIT_H */
