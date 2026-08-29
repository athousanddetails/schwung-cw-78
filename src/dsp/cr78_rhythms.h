/*
 * cr78_rhythms.h — the CR-78's own preset rhythms.
 *
 * Transcribed from pages 27 and 28 of the service notes, which print all of
 * the machine's preset patterns in staff notation across a two-page spread.
 *
 * THE GRID IS THE HARDWARE'S OWN. Page 12 of the notes states the storage
 * format outright:
 *
 *     "Required bit numbers for two measures are:
 *      4 (PROGRAM) x 4 (INSTRUMENT) x 96 steps (48 x 2) = 1536 bits."
 *
 * and page 9 that the 8048 "performs the entire loop once for one cycle of
 * master oscillator and 48 times per measure". So a CR-78 measure is 48
 * steps, two measures are 96, and that is exactly what is stored below. 48
 * per bar is 12 per beat, which puts sixteenths (every 3) and eighth-note
 * triplets (every 4) both exactly on the grid — which is why the machine can
 * play a shuffle and a disco pattern on the same clock.
 *
 * THE NOTATION'S VOICE MAP, read off the legend at the top of page 27. Eleven
 * staff positions, labelled in two alternating rows:
 *
 *     cross below staff   BASS DRUM        notehead, 3rd space  CLAVES
 *     notehead, lowest    LOW CONGA        notehead, 4th line   COW BELL
 *     notehead            LOW BONGO        notehead, 4th space  MARACAS
 *     notehead            HIGH BONGO       triangle, top line   HI-HAT
 *     notehead, 2nd space SNARE DRUM       X above the staff    CYMBAL
 *     X on the middle line  RIM SHOT
 *
 * Two X noteheads at different heights mean different voices, and that is the
 * single easiest thing to get wrong: X on the middle line is the RIM SHOT,
 * X above the staff is the CYMBAL, and the HI-HAT is a hollow triangle rather
 * than an X at all.
 *
 * THE ADD VOICE LANES ARE NOT IN THE PATTERNS. There is no tambourine, guiro
 * or metallic beat anywhere in the notation, because on the hardware those
 * are mixed in by the ADD VOICE sliders on OP-103 rather than programmed.
 * They stay silent here for the same reason, and playing one is still up to
 * the player.
 *
 * ACCENT IS A REAL CHANNEL. The "(>)"marks in parentheses through the middle
 * of every system are the accent pulses, and on a CR-78 accent is one
 * panel-wide control voltage into the BA662 VCA — not a per-voice switch. So
 * they are stored per STEP rather than per hit, and playback sends an
 * accented step at full velocity and everything else lower, which is exactly
 * the path CW-78's Velocity knob controls.
 *
 * HOW GOOD IS THIS TRANSCRIPTION, HONESTLY
 *
 * The source is a 1979 scan of hand-set notation, and it is read visually.
 * What is reliable: the meter, which voices a pattern uses, the bass drum
 * line, the hi-hat / cymbal / maracas line, the snare, and the accent line.
 * Those sit on their own staff positions and are unambiguous.
 *
 * What is best-effort: the exact sixteenth placement of the auxiliary
 * percussion in the busiest Latin patterns, where four voices share the
 * middle of the staff and the noteheads overlap at the scan's resolution.
 *
 * No pattern here is verified against a real CR-78, because there is not one
 * on the bench. They are a careful reading of Roland's own notation, and the
 * bar to clear before treating one as gospel is a recording. Anything that is
 * wrong is wrong by a note, not by a pattern.
 *
 * GPL-3.0.
 */
#ifndef CR78_RHYTHMS_H
#define CR78_RHYTHMS_H

#include "cr78_engine.h"

/* 48 steps to the bar, as the 8048 counts them. */
#define CR78_STEPS_PER_BAR 48
#define CR78_STEPS_3_4     36      /* the waltz is the one 3/4 pattern */

/* Handy step names at 12 ticks to the beat. */
#define B1  0
#define B2 12
#define B3 24
#define B4 36
#define E   6      /* an eighth   */
#define S   3      /* a sixteenth */
#define T   4      /* an eighth-note triplet */

typedef struct { unsigned char step, voice; } cr78_hit_t;

typedef struct {
    const char       *id;
    const char       *name;
    int               stepsPerBar;
    const cr78_hit_t *a;      int aN;
    const unsigned char *aAcc; int aAccN;
    const cr78_hit_t *b;      int bN;
    const unsigned char *bAcc; int bAccN;
} cr78_rhythm_t;

/* ---- BALLROOM ---------------------------------------------------------- */

/* WALTZ. 3/4, so 36 steps. Cymbal on 1 and 3, bass drum on 1, a bongo
 * answering on 2 and 3. Bar B adds the pickup eighth before 3. */
static const cr78_hit_t kW_A[] = {
    {0,CR78_CY},{0,CR78_BD},{12,CR78_LB},{24,CR78_CY},{24,CR78_LB} };
static const unsigned char kW_AA[] = { 24 };
static const cr78_hit_t kW_B[] = {
    {0,CR78_CY},{0,CR78_BD},{12,CR78_LB},{18,CR78_LB},
    {24,CR78_CY},{24,CR78_LB},{30,CR78_CY} };
static const unsigned char kW_BA[] = { 24 };

/* SHUFFLE. Triplet eighths on the cymbal, the middle of each triplet left
 * out — the shuffle's long-short. Snare on 2 and 4. */
static const cr78_hit_t kSh_A[] = {
    {0,CR78_CY},{8,CR78_CY},{12,CR78_CY},{20,CR78_CY},
    {24,CR78_CY},{32,CR78_CY},{36,CR78_CY},{44,CR78_CY},
    {0,CR78_BD},{24,CR78_BD},{12,CR78_SD},{36,CR78_SD} };
static const unsigned char kSh_AA[] = { 0,12,24,36 };
static const cr78_hit_t kSh_B[] = {
    {0,CR78_CY},{8,CR78_CY},{12,CR78_CY},{20,CR78_CY},
    {24,CR78_CY},{32,CR78_CY},{36,CR78_CY},{44,CR78_CY},
    {0,CR78_BD},{20,CR78_BD},{24,CR78_BD},{12,CR78_SD},{36,CR78_SD} };
static const unsigned char kSh_BA[] = { 0,12,24,36 };

/* SLOW ROCK. 12/8 — the hi-hat plays all twelve triplet eighths, which is
 * what the four beamed groups of three in the notation are. */
static const cr78_hit_t kSR_A[] = {
    {0,CR78_HH},{4,CR78_HH},{8,CR78_HH},{12,CR78_HH},{16,CR78_HH},{20,CR78_HH},
    {24,CR78_HH},{28,CR78_HH},{32,CR78_HH},{36,CR78_HH},{40,CR78_HH},{44,CR78_HH},
    {0,CR78_BD},{24,CR78_BD},{12,CR78_SD},{36,CR78_SD} };
static const unsigned char kSR_AA[] = { 0,12,24,36 };
static const cr78_hit_t kSR_B[] = {
    {0,CR78_HH},{4,CR78_HH},{8,CR78_HH},{12,CR78_HH},{16,CR78_HH},{20,CR78_HH},
    {24,CR78_HH},{28,CR78_HH},{32,CR78_HH},{36,CR78_HH},{40,CR78_HH},{44,CR78_HH},
    {0,CR78_BD},{20,CR78_BD},{24,CR78_BD},{44,CR78_BD},
    {12,CR78_SD},{36,CR78_SD} };
static const unsigned char kSR_BA[] = { 0,12,24,36 };

/* SWING. The ride figure: beat, then the last triplet of the beat. */
static const cr78_hit_t kSw_A[] = {
    {0,CR78_CY},{12,CR78_CY},{20,CR78_CY},{24,CR78_CY},{36,CR78_CY},{44,CR78_CY},
    {0,CR78_BD},{24,CR78_BD} };
static const unsigned char kSw_AA[] = { 0,24 };
static const cr78_hit_t kSw_B[] = {
    {0,CR78_CY},{12,CR78_CY},{20,CR78_CY},{24,CR78_CY},{36,CR78_CY},{44,CR78_CY},
    {0,CR78_BD},{24,CR78_BD},{12,CR78_LB},{36,CR78_HB} };
static const unsigned char kSw_BA[] = { 0,24 };

/* FOX TROT. The sparsest thing on the machine — the notation gives it one
 * bar and a repeat sign. */
static const cr78_hit_t kFT_A[] = {
    {0,CR78_CY},{12,CR78_CY},{24,CR78_CY},{36,CR78_CY},
    {0,CR78_BD},{24,CR78_BD},{12,CR78_SD},{36,CR78_SD} };
static const unsigned char kFT_AA[] = { 0,24 };
static const cr78_hit_t kFT_B[] = {
    {0,CR78_CY},{12,CR78_CY},{24,CR78_CY},{36,CR78_CY},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{12,CR78_SD},{36,CR78_SD} };
static const unsigned char kFT_BA[] = { 0,24 };

/* TANGO. The habanera figure on the bass drum. */
static const cr78_hit_t kTg_A[] = {
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{36,CR78_BD},
    {0,CR78_CL},{24,CR78_CL},{36,CR78_RS} };
static const unsigned char kTg_AA[] = { 0,24 };
static const cr78_hit_t kTg_B[] = {
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{36,CR78_BD},
    {0,CR78_CL},{24,CR78_CL},{36,CR78_RS},{42,CR78_RS} };
static const unsigned char kTg_BA[] = { 0,24 };

/* BOOGIE. Shuffle feel with a walking bass drum. */
static const cr78_hit_t kBg_A[] = {
    {0,CR78_CY},{8,CR78_CY},{12,CR78_CY},{20,CR78_CY},
    {24,CR78_CY},{32,CR78_CY},{36,CR78_CY},{44,CR78_CY},
    {0,CR78_BD},{12,CR78_BD},{24,CR78_BD},{36,CR78_BD},
    {12,CR78_SD},{36,CR78_SD} };
static const unsigned char kBg_AA[] = { 0,12,24,36 };
static const cr78_hit_t kBg_B[] = {
    {0,CR78_CY},{8,CR78_CY},{12,CR78_CY},{20,CR78_CY},
    {24,CR78_CY},{32,CR78_CY},{36,CR78_CY},{44,CR78_CY},
    {0,CR78_BD},{12,CR78_BD},{24,CR78_BD},{36,CR78_BD},{44,CR78_BD},
    {12,CR78_SD},{36,CR78_SD} };
static const unsigned char kBg_BA[] = { 0,12,24,36 };

/* ENKA. The Japanese ballad rhythm the CR-78 shipped with and no western
 * machine ever did — a slow two with the bongos answering. */
static const cr78_hit_t kEn_A[] = {
    {0,CR78_BD},{24,CR78_BD},{12,CR78_SD},{36,CR78_SD},
    {6,CR78_LB},{18,CR78_HB},{30,CR78_LB},{42,CR78_HB} };
static const unsigned char kEn_AA[] = { 0,24 };
static const cr78_hit_t kEn_B[] = {
    {0,CR78_BD},{24,CR78_BD},{12,CR78_SD},{36,CR78_SD},
    {6,CR78_LB},{18,CR78_HB},{30,CR78_LB},{42,CR78_HB},{45,CR78_HB} };
static const unsigned char kEn_BA[] = { 0,24 };

/* ---- LATIN ------------------------------------------------------------- */

/* BOSSA NOVA. Sixteenths on the maracas the whole way through — the four
 * beamed groups of four in the notation — with the rim shot playing the
 * clave across the two bars, which is why A and B are not interchangeable
 * here the way they are on the rock patterns. */
static const cr78_hit_t kBN_A[] = {
    {0,CR78_MA},{3,CR78_MA},{6,CR78_MA},{9,CR78_MA},
    {12,CR78_MA},{15,CR78_MA},{18,CR78_MA},{21,CR78_MA},
    {24,CR78_MA},{27,CR78_MA},{30,CR78_MA},{33,CR78_MA},
    {36,CR78_MA},{39,CR78_MA},{42,CR78_MA},{45,CR78_MA},
    {0,CR78_RS},{9,CR78_RS},{24,CR78_RS},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD} };
static const unsigned char kBN_AA[] = { 0,24 };
static const cr78_hit_t kBN_B[] = {
    {0,CR78_MA},{3,CR78_MA},{6,CR78_MA},{9,CR78_MA},
    {12,CR78_MA},{15,CR78_MA},{18,CR78_MA},{21,CR78_MA},
    {24,CR78_MA},{27,CR78_MA},{30,CR78_MA},{33,CR78_MA},
    {36,CR78_MA},{39,CR78_MA},{42,CR78_MA},{45,CR78_MA},
    {6,CR78_RS},{18,CR78_RS},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD} };
static const unsigned char kBN_BA[] = { 0,24 };

/* SAMBA. The fast two, with the low conga on the second half of each beat. */
static const cr78_hit_t kSa_A[] = {
    {0,CR78_MA},{3,CR78_MA},{6,CR78_MA},{9,CR78_MA},
    {12,CR78_MA},{15,CR78_MA},{18,CR78_MA},{21,CR78_MA},
    {24,CR78_MA},{27,CR78_MA},{30,CR78_MA},{33,CR78_MA},
    {36,CR78_MA},{39,CR78_MA},{42,CR78_MA},{45,CR78_MA},
    {0,CR78_BD},{9,CR78_BD},{24,CR78_BD},{33,CR78_BD},
    {6,CR78_LC},{18,CR78_LC},{30,CR78_LC},{42,CR78_LC},
    {12,CR78_RS},{36,CR78_RS} };
static const unsigned char kSa_AA[] = { 0,24 };
static const cr78_hit_t kSa_B[] = {
    {0,CR78_MA},{3,CR78_MA},{6,CR78_MA},{9,CR78_MA},
    {12,CR78_MA},{15,CR78_MA},{18,CR78_MA},{21,CR78_MA},
    {24,CR78_MA},{27,CR78_MA},{30,CR78_MA},{33,CR78_MA},
    {36,CR78_MA},{39,CR78_MA},{42,CR78_MA},{45,CR78_MA},
    {0,CR78_BD},{9,CR78_BD},{24,CR78_BD},{33,CR78_BD},
    {6,CR78_LC},{18,CR78_LC},{30,CR78_LC},{42,CR78_LC},
    {12,CR78_RS},{27,CR78_RS},{36,CR78_RS} };
static const unsigned char kSa_BA[] = { 0,24 };

/* MAMBO. Cowbell on the beat, congas answering. */
static const cr78_hit_t kMb_A[] = {
    {0,CR78_CB},{12,CR78_CB},{24,CR78_CB},{36,CR78_CB},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD},
    {6,CR78_LC},{30,CR78_LC},{18,CR78_HB},{42,CR78_HB} };
static const unsigned char kMb_AA[] = { 0,24 };
static const cr78_hit_t kMb_B[] = {
    {0,CR78_CB},{12,CR78_CB},{24,CR78_CB},{36,CR78_CB},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD},
    {6,CR78_LC},{30,CR78_LC},{18,CR78_HB},{42,CR78_HB},{45,CR78_LB} };
static const unsigned char kMb_BA[] = { 0,24 };

/* CHA CHA. The claves' three-two, and the conga on the "and" of four. */
static const cr78_hit_t kCC_A[] = {
    {0,CR78_CL},{18,CR78_CL},{24,CR78_CL},
    {0,CR78_BD},{24,CR78_BD},
    {12,CR78_CB},{36,CR78_CB},
    {36,CR78_LC},{42,CR78_LC},{45,CR78_HB} };
static const unsigned char kCC_AA[] = { 0,24 };
static const cr78_hit_t kCC_B[] = {
    {12,CR78_CL},{36,CR78_CL},
    {0,CR78_BD},{24,CR78_BD},
    {12,CR78_CB},{36,CR78_CB},
    {36,CR78_LC},{42,CR78_LC},{45,CR78_HB} };
static const unsigned char kCC_BA[] = { 0,24 };

/* BEGUINE. The densest pattern in the notation — sixteenths across three
 * voices at once, which is what the stacked beams on page 27 are. */
static const cr78_hit_t kBe_A[] = {
    {0,CR78_MA},{3,CR78_MA},{6,CR78_MA},{9,CR78_MA},
    {12,CR78_MA},{15,CR78_MA},{18,CR78_MA},{21,CR78_MA},
    {24,CR78_MA},{27,CR78_MA},{30,CR78_MA},{33,CR78_MA},
    {36,CR78_MA},{39,CR78_MA},{42,CR78_MA},{45,CR78_MA},
    {0,CR78_RS},{9,CR78_RS},{18,CR78_RS},{27,CR78_RS},{36,CR78_RS},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD},
    {12,CR78_LC},{36,CR78_HB} };
static const unsigned char kBe_AA[] = { 0,24 };
static const cr78_hit_t kBe_B[] = {
    {0,CR78_MA},{3,CR78_MA},{6,CR78_MA},{9,CR78_MA},
    {12,CR78_MA},{15,CR78_MA},{18,CR78_MA},{21,CR78_MA},
    {24,CR78_MA},{27,CR78_MA},{30,CR78_MA},{33,CR78_MA},
    {36,CR78_MA},{39,CR78_MA},{42,CR78_MA},{45,CR78_MA},
    {6,CR78_RS},{15,CR78_RS},{24,CR78_RS},{33,CR78_RS},{42,CR78_RS},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD},
    {12,CR78_LC},{36,CR78_HB} };
static const unsigned char kBe_BA[] = { 0,24 };

/* RHUMBA. The beguine's slower cousin, claves instead of rim shot. */
static const cr78_hit_t kRh_A[] = {
    {0,CR78_MA},{6,CR78_MA},{12,CR78_MA},{18,CR78_MA},
    {24,CR78_MA},{30,CR78_MA},{36,CR78_MA},{42,CR78_MA},
    {0,CR78_CL},{9,CR78_CL},{24,CR78_CL},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD},
    {12,CR78_LC},{36,CR78_LC} };
static const unsigned char kRh_AA[] = { 0,24 };
static const cr78_hit_t kRh_B[] = {
    {0,CR78_MA},{6,CR78_MA},{12,CR78_MA},{18,CR78_MA},
    {24,CR78_MA},{30,CR78_MA},{36,CR78_MA},{42,CR78_MA},
    {6,CR78_CL},{18,CR78_CL},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD},
    {12,CR78_LC},{36,CR78_LC},{45,CR78_HB} };
static const unsigned char kRh_BA[] = { 0,24 };

/* ---- ROCK -------------------------------------------------------------- */

/*
 * ROCK-1. The one everybody knows — hi-hat on all eight eighths, snare on
 * two and four, bass drum on one, the "and" of two, three and four. This is
 * the pattern that ended up on records, and it is the clearest system in the
 * whole score: nothing about it needed guessing.
 */
static const cr78_hit_t kR1_A[] = {
    {0,CR78_HH},{6,CR78_HH},{12,CR78_HH},{18,CR78_HH},
    {24,CR78_HH},{30,CR78_HH},{36,CR78_HH},{42,CR78_HH},
    {12,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{36,CR78_BD} };
static const unsigned char kR1_AA[] = { 0,12,24,36 };
static const cr78_hit_t kR1_B[] = {
    {0,CR78_HH},{6,CR78_HH},{12,CR78_HH},{18,CR78_HH},
    {24,CR78_HH},{30,CR78_HH},{36,CR78_HH},{42,CR78_HH},
    {12,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{18,CR78_BD},{21,CR78_BD},{24,CR78_BD},{36,CR78_BD} };
static const unsigned char kR1_BA[] = { 0,12,24,36 };

/* ROCK-2. Same hat, a lazier kick. */
static const cr78_hit_t kR2_A[] = {
    {0,CR78_HH},{6,CR78_HH},{12,CR78_HH},{18,CR78_HH},
    {24,CR78_HH},{30,CR78_HH},{36,CR78_HH},{42,CR78_HH},
    {12,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{24,CR78_BD},{30,CR78_BD} };
static const unsigned char kR2_AA[] = { 0,12,24,36 };
static const cr78_hit_t kR2_B[] = {
    {0,CR78_HH},{6,CR78_HH},{12,CR78_HH},{18,CR78_HH},
    {24,CR78_HH},{30,CR78_HH},{36,CR78_HH},{42,CR78_HH},
    {12,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{24,CR78_BD},{30,CR78_BD},{42,CR78_BD} };
static const unsigned char kR2_BA[] = { 0,12,24,36 };

/* ROCK-3. Sixteenths on the hat. */
static const cr78_hit_t kR3_A[] = {
    {0,CR78_HH},{3,CR78_HH},{6,CR78_HH},{9,CR78_HH},
    {12,CR78_HH},{15,CR78_HH},{18,CR78_HH},{21,CR78_HH},
    {24,CR78_HH},{27,CR78_HH},{30,CR78_HH},{33,CR78_HH},
    {36,CR78_HH},{39,CR78_HH},{42,CR78_HH},{45,CR78_HH},
    {12,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD} };
static const unsigned char kR3_AA[] = { 0,12,24,36 };
static const cr78_hit_t kR3_B[] = {
    {0,CR78_HH},{3,CR78_HH},{6,CR78_HH},{9,CR78_HH},
    {12,CR78_HH},{15,CR78_HH},{18,CR78_HH},{21,CR78_HH},
    {24,CR78_HH},{27,CR78_HH},{30,CR78_HH},{33,CR78_HH},
    {36,CR78_HH},{39,CR78_HH},{42,CR78_HH},{45,CR78_HH},
    {12,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{18,CR78_BD},{24,CR78_BD},{42,CR78_BD} };
static const unsigned char kR3_BA[] = { 0,12,24,36 };

/* ROCK-4. The busiest of the four, with the kick doubling up. */
static const cr78_hit_t kR4_A[] = {
    {0,CR78_HH},{3,CR78_HH},{6,CR78_HH},{9,CR78_HH},
    {12,CR78_HH},{15,CR78_HH},{18,CR78_HH},{21,CR78_HH},
    {24,CR78_HH},{27,CR78_HH},{30,CR78_HH},{33,CR78_HH},
    {36,CR78_HH},{39,CR78_HH},{42,CR78_HH},{45,CR78_HH},
    {12,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{9,CR78_BD},{18,CR78_BD},{24,CR78_BD},{33,CR78_BD} };
static const unsigned char kR4_AA[] = { 0,12,24,36 };
static const cr78_hit_t kR4_B[] = {
    {0,CR78_HH},{3,CR78_HH},{6,CR78_HH},{9,CR78_HH},
    {12,CR78_HH},{15,CR78_HH},{18,CR78_HH},{21,CR78_HH},
    {24,CR78_HH},{27,CR78_HH},{30,CR78_HH},{33,CR78_HH},
    {36,CR78_HH},{39,CR78_HH},{42,CR78_HH},{45,CR78_HH},
    {12,CR78_SD},{30,CR78_SD},{36,CR78_SD},
    {0,CR78_BD},{9,CR78_BD},{18,CR78_BD},{24,CR78_BD},{33,CR78_BD} };
static const unsigned char kR4_BA[] = { 0,12,24,36 };

/* DISCO-1. Four on the floor with maracas eighths over it. */
static const cr78_hit_t kD1_A[] = {
    {0,CR78_MA},{6,CR78_MA},{12,CR78_MA},{18,CR78_MA},
    {24,CR78_MA},{30,CR78_MA},{36,CR78_MA},{42,CR78_MA},
    {0,CR78_BD},{12,CR78_BD},{24,CR78_BD},{36,CR78_BD},
    {12,CR78_SD},{36,CR78_SD},{6,CR78_CY} };
static const unsigned char kD1_AA[] = { 0,12,24,36 };
static const cr78_hit_t kD1_B[] = {
    {0,CR78_MA},{6,CR78_MA},{12,CR78_MA},{18,CR78_MA},
    {24,CR78_MA},{30,CR78_MA},{36,CR78_MA},{42,CR78_MA},
    {0,CR78_BD},{12,CR78_BD},{24,CR78_BD},{36,CR78_BD},{42,CR78_BD},
    {12,CR78_SD},{36,CR78_SD},{6,CR78_CY},{42,CR78_CY} };
static const unsigned char kD1_BA[] = { 0,12,24,36 };

/* DISCO-2. The open-hat-and-cymbal one, with the kick pushed off the beat. */
static const cr78_hit_t kD2_A[] = {
    {0,CR78_CY},{18,CR78_CY},{24,CR78_CY},{42,CR78_CY},
    {0,CR78_BD},{12,CR78_BD},{24,CR78_BD},{36,CR78_BD},
    {12,CR78_SD},{36,CR78_SD},
    {6,CR78_MA},{18,CR78_MA},{30,CR78_MA},{42,CR78_MA} };
static const unsigned char kD2_AA[] = { 0,12,24,36 };
static const cr78_hit_t kD2_B[] = {
    {0,CR78_CY},{18,CR78_CY},{24,CR78_CY},{42,CR78_CY},
    {0,CR78_BD},{12,CR78_BD},{24,CR78_BD},{36,CR78_BD},{45,CR78_BD},
    {12,CR78_SD},{36,CR78_SD},
    {6,CR78_MA},{18,CR78_MA},{30,CR78_MA},{42,CR78_MA} };
static const unsigned char kD2_BA[] = { 0,12,24,36 };

/* ------------------------------------------------------------------------ */

#define CR78_RHY(id, name, spb, x) \
    { id, name, spb, k##x##_A, (int)(sizeof k##x##_A / sizeof k##x##_A[0]), \
      k##x##_AA, (int)(sizeof k##x##_AA), \
      k##x##_B, (int)(sizeof k##x##_B / sizeof k##x##_B[0]), \
      k##x##_BA, (int)(sizeof k##x##_BA) }

/*
 * The bank, in the order the Style enum lists them. The three groups are
 * where the Bank control jumps to, and they follow the machine's own panel:
 * the ballroom row and the latin row live on the RS-14 switch board, the
 * rock and disco buttons on RS-17.
 */
static const cr78_rhythm_t g_cr78_rhythms[] = {
    /* -- bank 0: BALLROOM -- */
    CR78_RHY("waltz",  "Waltz",      CR78_STEPS_3_4, W),
    CR78_RHY("shuffle","Shuffle",    CR78_STEPS_PER_BAR, Sh),
    CR78_RHY("slowrck","Slow Rock",  CR78_STEPS_PER_BAR, SR),
    CR78_RHY("swing",  "Swing",      CR78_STEPS_PER_BAR, Sw),
    CR78_RHY("foxtrot","Fox Trot",   CR78_STEPS_PER_BAR, FT),
    CR78_RHY("tango",  "Tango",      CR78_STEPS_PER_BAR, Tg),
    CR78_RHY("boogie", "Boogie",     CR78_STEPS_PER_BAR, Bg),
    CR78_RHY("enka",   "Enka",       CR78_STEPS_PER_BAR, En),
    /* -- bank 1: LATIN -- */
    CR78_RHY("bossa",  "Bossa Nova", CR78_STEPS_PER_BAR, BN),
    CR78_RHY("samba",  "Samba",      CR78_STEPS_PER_BAR, Sa),
    CR78_RHY("mambo",  "Mambo",      CR78_STEPS_PER_BAR, Mb),
    CR78_RHY("chacha", "Cha Cha",    CR78_STEPS_PER_BAR, CC),
    CR78_RHY("beguine","Beguine",    CR78_STEPS_PER_BAR, Be),
    CR78_RHY("rhumba", "Rhumba",     CR78_STEPS_PER_BAR, Rh),
    /* -- bank 2: ROCK -- */
    CR78_RHY("rock1",  "Rock 1",     CR78_STEPS_PER_BAR, R1),
    CR78_RHY("rock2",  "Rock 2",     CR78_STEPS_PER_BAR, R2),
    CR78_RHY("rock3",  "Rock 3",     CR78_STEPS_PER_BAR, R3),
    CR78_RHY("rock4",  "Rock 4",     CR78_STEPS_PER_BAR, R4),
    CR78_RHY("disco1", "Disco 1",    CR78_STEPS_PER_BAR, D1),
    CR78_RHY("disco2", "Disco 2",    CR78_STEPS_PER_BAR, D2),
};

#define CR78_NUM_RHYTHMS ((int)(sizeof g_cr78_rhythms / sizeof g_cr78_rhythms[0]))

/* ------------------------------------------------------------------------ *
 *  The panel's SEVENTEEN BUTTONS.
 *
 *  Style selects a button, not a pattern, because that is what the hardware
 *  has. Three of the seventeen carry two rhythms each — the panel prints them
 *  "A-FOX TROT / B-TANGO", "A-MAMBO / B-CHA CHA", "A-BEGUINE / B-RHUMBA" —
 *  and the RHYTHM A/B lever picks between them. On the other fourteen the
 *  lever does nothing, exactly as on the machine.
 *
 *  Seventeen buttons, three of them doubled, is twenty patterns.
 *
 *  Do not confuse this lever with the "A" and "B" bar marks in the score.
 *  Those are the two MEASURES of each pattern and they alternate on their own
 *  as it plays; this is a different control on a different part of the panel.
 * ------------------------------------------------------------------------ */
typedef struct {
    const char *label;      /* what the button cap says            */
    int         a, b;       /* index into g_cr78_rhythms; b == a if
                             * the button carries only one rhythm  */
} cr78_button_t;

static const cr78_button_t g_cr78_buttons[] = {
    { "Waltz",              0,  0 },
    { "Shuffle",            1,  1 },
    { "Slow Rock",          2,  2 },
    { "Swing",              3,  3 },
    { "A-Fox Trot/B-Tango", 4,  5 },
    { "Boogie",             6,  6 },
    { "Enka",               7,  7 },
    { "Bossa Nova",         8,  8 },
    { "Samba",              9,  9 },
    { "A-Mambo/B-Cha Cha", 10, 11 },
    { "A-Beguine/B-Rhumba",12, 13 },
    { "Rock-1",            14, 14 },
    { "Rock-2",            15, 15 },
    { "Rock-3",            16, 16 },
    { "Rock-4",            17, 17 },
    { "Disco-1",           18, 18 },
    { "Disco-2",           19, 19 },
};

#define CR78_NUM_BUTTONS ((int)(sizeof g_cr78_buttons / sizeof g_cr78_buttons[0]))

/* Button + lever -> pattern. The one place that mapping lives. */
static inline int cr78_resolve_pattern(int _button, int _ab)
{
    if(_button < 0 || _button >= CR78_NUM_BUTTONS) _button = 0;
    const cr78_button_t *b = &g_cr78_buttons[_button];
    const int idx = _ab ? b->b : b->a;
    return (idx >= 0 && idx < CR78_NUM_RHYTHMS) ? idx : 0;
}

/* Does the lever do anything on this button? The panel only prints A-/B- on
 * three of them, and the UI greys the lever out on the rest. */
static inline int cr78_button_is_dual(int _button)
{
    if(_button < 0 || _button >= CR78_NUM_BUTTONS) return 0;
    return g_cr78_buttons[_button].a != g_cr78_buttons[_button].b;
}


#undef B1
#undef B2
#undef B3
#undef B4
#undef E
#undef S
#undef T

#endif /* CR78_RHYTHMS_H */
