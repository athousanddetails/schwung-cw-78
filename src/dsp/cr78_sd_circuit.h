/*
 * cr78_sd_circuit.h — the snare drum, both halves of it.
 *
 * The only voice on the board that is two circuits at once, and the schematic
 * on page 25 draws them one above the other sharing a trigger.
 *
 *   SHELL (top)   C508 .027 / R506 270k / D506 -> R507 220k
 *                 -> C509 .01, C510 .01, C511 .0056
 *                    with R516 15k, R517 68k, R518 15k
 *                 -> Q505 with R519 1M of feedback, C512 100p
 *                 -> R520 47k -> C513 .022 -> R522 1.5M
 *
 *   SNAP (bottom) D507 / R508 470k -> Q514
 *                 -> R523 820k, R524 10k, D508
 *                 -> R511 1.5M, C514 .018   (the snap envelope)
 *                 -> R512 47k, C515 .0082, R513 470k
 *                 -> Q506 with R514 2.7k, C516 .0056, C517 250p, R515 100k
 *
 * The shell is a bridged-T in a feedback loop — family B, the same idea as
 * the bass drum and the bongos, but with its own network arrangement: R517 is
 * 68 k where the four drum channels all use 15 k, so this one does not share
 * their fitted frequency constant and is not forced to. Page 30 trims it to
 * 340 Hz and 60 ms and that is what it is set to.
 *
 * The snap is family C — the shared noise source through its own envelope.
 * C514 .018 with R511 1.5M is 27 ms, the same figure as the hi-hat's, which
 * is why a CR-78 snare and its hi-hat sit so well together.
 *
 * NOTE ON A FACTORY REVISION: C513 is drawn as 250 p with the value struck
 * out and .022 written in by hand, and R521 33k is struck out entirely. The
 * handwritten values are the later ones and are what is used here.
 *
 * SNAPPY is a balance, and on the hardware it is not a control at all — the
 * mix is fixed by R520/R522 against R515. It is a knob here because every
 * lane in this module has the same five controls, and it moves the snap
 * against the shell without touching either one's decay.
 *
 * GPL-3.0.
 */
#ifndef CR78_SD_CIRCUIT_H
#define CR78_SD_CIRCUIT_H

#include <math.h>

#include "cr78_circuit.h"

namespace cr78 {

/* ---- component values, CR-78 service notes p.25, VG-11A ---------------- */

/* The shell's bridged-T. R516 15k shunt, R517 68k series, C510 .01 and
 * C511 .0056. */
static const double kSD_Rshunt = 15.0e3;
static const double kSD_Rser   = 68.0e3;
static const double kSD_C1     = 10.0e-9;
static const double kSD_C2     = 5.6e-9;

/* 1.06 — a much broader network than the drum channels' 0.61, which is what
 * makes a snare a snare and not a tom. Computed from the two resistors. */
static const double kSD_Q = bridgedTQ(kSD_Rshunt, kSD_Rser);

/* page 30, the alignment targets */
static const double kSD_fFactory   = 340.0;    /* "340 (Drum)"  */
static const double kSD_ShellDecay = 0.060;
static const double kSD_Amp        = 0.40;     /* VR61          */

/* The snap's envelope: C514 .018 through R511 1.5M. DERIVED, and the same
 * 27 ms the hi-hat's C525/R537 gives. */
static const double kSD_SnapRC = 0.018e-6 * 1.5e6;

/* The snap is voiced by C515 .0082 and C516 .0056 either side of Q506.
 * FITTED corner — the notes give the parts, not a response. */
static const double kSD_SnapHpf = 2400.0;

/* Fixed hardware balance, from R520/R522 against R515. The Snappy pot moves
 * around this, and pot centre IS this. */
static const double kSD_SnapMix = 0.55;

static inline double sdDerivedFreq()
{
    return bridgedTFreq(kSD_Rshunt, kSD_Rser, kSD_C1, kSD_C2);
}

class SnareVoice {
public:
    void init(const double _sr)
    {
        sr_ = _sr >= 8000.0 ? _sr : 44100.0;
        front_.init(sr_);
        shell_.reset();
        snapEnv_.reset();
        snapHp_.set(kSD_SnapHpf, sr_);
        fb_ = 0.0; gateSamp_ = 0;
        setTune(1.0);
        setShellDecay(kSD_ShellDecay);
        setSnapDecay(kSD_SnapRC);
        setSnappy(0.5);
    }

    void reset()
    {
        front_.reset(); shell_.reset(); snapEnv_.reset(); snapHp_.reset();
        fb_ = 0.0; gateSamp_ = 0;
    }

    void setTune(const double _ratio)
    {
        const double r = _ratio < 0.25 ? 0.25 : (_ratio > 4.0 ? 4.0 : _ratio);
        f0_ = kSD_fFactory * r;
        apply();
    }

    /* One Decay knob drives both halves, because that is what a player means
     * by a snare's decay. The snap keeps its hardware proportion to the
     * shell — 27 ms against 60 ms — so the balance does not walk. */
    void setDecay(const double _seconds)
    {
        setShellDecay(_seconds);
        setSnapDecay(_seconds * (kSD_SnapRC / kSD_ShellDecay));
    }

    void setShellDecay(const double _s) { shellDecay_ = _s; apply(); }
    void setSnapDecay(const double _s)
    { snapEnv_.set((_s < 0.002 ? 0.002 : _s) / kLn10, sr_); }

    /* 0..1 around the hardware's fixed balance at 0.5. */
    void setSnappy(const double _v)
    {
        const double v = _v < 0.0 ? 0.0 : (_v > 1.0 ? 1.0 : _v);
        snapMix_ = kSD_SnapMix * (v * 2.0);
    }

    void trigger(const double _vel)
    {
        gateSamp_ = (int)(0.001 * sr_);
        gateV_ = 0.45 + 0.55 * _vel;
    }

    bool active() const
    { return gateSamp_ > 0 || fabs(fb_) > 1.0e-6 || snapEnv_.value() > 1.0e-5; }

    double process(const double _noise)
    {
        double gate = 0.0;
        if(gateSamp_ > 0) { gate = gateV_; --gateSamp_; }
        const double strike = front_.process(gate);

        /* shell */
        const double sh = shell_.process(strike * 4.0) * g_;
        fb_ = transistorClip(sh);

        /* snap */
        if(strike > 0.0) snapEnv_.strike(strike * 1.7);
        const double sn = snapHp_.process(_noise * snapEnv_.process());

        return fb_ * (1.0 - snapMix_ * 0.45) + sn * snapMix_;
    }

    double amplitude() const { return kSD_Amp; }

private:
    /* Ring from the pole, not from an explicit loop — see the long note in
     * cr78_drum_circuit.h. The one-sample delay in a feedback loop costs Q,
     * and the shell was measuring 17% short because of it. */
    void apply()
    {
        double q = qForRing(f0_, shellDecay_);
        if(q < kSD_Q) q = kSD_Q;
        /* The loop's resonant gain, 1/(1-g) = Q_eff/Q_network — same story
         * as the drum channels, see cr78_drum_circuit.h. The shell had
         * collapsed the same 20-odd dB. */
        g_ = q / kSD_Q;
        shell_.set(f0_, q, sr_);
    }

    TriggerFront front_;
    Resonator    shell_;
    EnvCap       snapEnv_;
    OnePoleHP    snapHp_;
    double sr_ = 44100.0, f0_ = 340.0, shellDecay_ = 0.06;
    double g_ = 0.0, fb_ = 0.0, snapMix_ = 0.55, gateV_ = 1.0;
    int    gateSamp_ = 0;
};

} /* namespace cr78 */

#endif /* CR78_SD_CIRCUIT_H */
