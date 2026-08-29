#!/usr/bin/env python3
"""Single source of truth for CW-78's parameter surface.

Emits src/dsp/cr78_params.h, which carries THREE things that must never
drift apart:

  * chain_params   — types/ranges/options the Shadow UI needs (JSON)
  * ui_pages       — the page hierarchy (JSON; served under "ui_pages" so
                     ui_chain.js loads it, see the comment in ui_chain.js)
  * the pot table  — key -> real engineering range + curve + default

and src/movy_config.json from the same dict.

The generator itself is 6W6's, inherited through 8W8 (GPL-3.0) — the module
shell is deliberately identical across the kits so a control means the same
thing on all of them. What is NOT inherited is a single default: every one
below comes from the CR-78's own service notes.

module.json is capped at 8 KB by Schwung's loader, so the JSON payloads are
served dynamically from the DSP via get_param().
"""
import json, pathlib

LIN, EXP = 0, 1

# Every continuous control is a 0..127 pot, exactly like a hardware panel.
# Nobody dials a CR-78 in milliseconds. The DSP maps each pot to its real
# range with a musical curve, so the UI only ever shows a pot position.
#
# DEFAULTS ARE ROLAND'S OWN FACTORY ALIGNMENT FIGURES, converted to pot
# positions. Page 30 of the service notes is the CR-78's factory setup
# procedure and gives, for every voice, the frequency, the decay time and the
# output amplitude the unit is trimmed to. Pot centre is NOT the default here
# and is not meant to be: the default is "a correctly aligned CR-78".
#
# The amplitude column is not on this page at all — it is the per-lane trim
# table in the engine, because it is a kit balance and not a control.


def P(key, label, lo, hi, curve, default):
    return dict(kind="pot", key=key, name=label, min=lo, max=hi,
                curve=curve, default=default)


def E(key, label, options, default=0):
    return dict(kind="enum", key=key, name=label, options=options,
                default=default)


def pot_for(value, lo, hi, curve):
    """The pot position whose mapped value is `value`.

    Decays are written as the SECONDS page 30 asks for — the bass drum's 100
    ms, the rim shot's 5 — and converted here. Writing pot numbers by hand and
    hoping they land on the right milliseconds is how a kit drifts away from
    the machine it is supposed to be.
    """
    import math
    value = max(lo, min(hi, value))
    if curve == EXP and lo > 0:
        t = math.log(value / lo) / math.log(hi / lo)
    else:
        t = (value - lo) / (hi - lo)
    return int(round(t * 127))


def PV(key, label, lo, hi, curve, value):
    """Like P, but the default is given as an engineering value."""
    return P(key, label, lo, hi, curve, pot_for(value, lo, hi, curve))


# Post-voice drive stage. The CR-78's own nonlinearities live inside the
# voices where the circuit puts them — the transistor clip in the drum
# channels, the diode front end on all fourteen lanes. THIS is the panel's
# Drive/Distortion, which the hardware never had at all: the CR-78 has a
# summing amp, a BA662 VCA and a volume knob.
#
# The same SEVEN flavours as 9W9, 6W6 and 8W8, so a player who knows what BFZ
# at 90 does on one knows what it does here.
#
# ORDER IS STORAGE ORDER. Option text is sized for the stock grid's enum box:
# TWO LINES OF THREE CHARACTERS (font5x3.mjs enumSquareLines). These read
# DIO/DE, CLI/P, SAT, BFZ, PDI/ST, FOL/D, CRU/SH. Do not rename them.
DIST = ["Diode", "Clip", "SAT", "BFZ", "PDIST", "Fold", "Crush"]


# Drive is LINEAR 0..10 and DEFAULTS TO 0, which cr78_shape.h treats as a
# bit-exact bypass. The CR-78 had no drive stage; a fresh patch gets none
# either, and the knob adds saturation rather than level.
def DRIVE(v):  return P(f"{v}_drive", "Drive", 0.0, 10.0, LIN, 0)
def DTYPE(v):  return E(f"{v}_dist_type", "Distortion", DIST)
def LEVEL(v):  return P(f"{v}_level", "Level", 0.0, 2.0, LIN, 64)   # pot 64 == 1.0

# Send amounts, one pair per voice. POST-FADER — what you hear is what you
# send — and 0 by default, which is load-bearing: at zero the FX ticks see
# exactly 0.0 and return exactly 0.0, so the kit stays bit-identical to one
# with no FX at all. tools/golden_check holds that.
#
# THE BASS DRUM GETS NONE, which is 9W9's call and it is the player's:
# reverb on a kick is mud, and the low end is exactly what the send highpass
# is there to keep out of the wet path.
def SENDS(v):  return [P(f"{v}_rev", "Rev", 0.0, 1.0, LIN, 0),
                       P(f"{v}_dly", "Dly", 0.0, 1.0, LIN, 0)]

# Pitch as a SEMITONE OFFSET around the voice's own factory frequency, so pot
# 64 is always "a correctly aligned CR-78" whatever that lane is tuned to.
# Stands in for the hardware's own tune trimmer — VR57 on the bass drum,
# VR51/53/55 on the bongos and conga, VR67/68 on the cowbell.
def TUNE(v):   return P(f"{v}_tune", "Tune", -12.0, 12.0, LIN, 64)

# Six lanes have no note at all — the hi-hat, cymbal and maracas are filtered
# noise, the metallic beat is three fixed oscillators, the guiro is a scrape
# rate and the tambourine is a tank with jingles under it. Their Tune is a
# RATIO on the whole voice, unity at pot centre.
def RATIO(v):  return P(f"{v}_tune", "Tune", 0.5, 2.0, EXP, 64)


# ---- Pages. One per voice, in CR-78 panel order. ---------------------------
#
# FOURTEEN voices, which is what the machine has: bass drum, snare, rim shot,
# hi-hat, cymbal, maracas, claves, two bongos, ONE conga, cowbell, tambourine,
# guiro and metallic beat. There is no high conga on a CR-78.
#
# Fourteen leaves room in Move's left 4x4 pad block for a MASTER PAD and one
# spare, which 8W8's sixteen voices did not:
#
#     row 3 (92-95)   GU  MB  MST --
#     row 2 (84-87)   LB  LC  CB  TB
#     row 1 (76-79)   CY  MA  CL  HB
#     row 0 (68-71)   BD  SD  RS  HH
#
# Every decay default below is the figure from page 30's alignment table.
PAGES = [
    ("bd", "Bass Drum", [
        # Bridged-T in a feedback loop, Q501-Q504's family. VR57 tunes it to
        # 62.5 Hz and VR58 sets the loop gain that rings for 100 ms.
        TUNE("bd"),
        PV("bd_decay", "Decay", 0.03, 1.0, EXP, 0.100),
        DRIVE("bd"), DTYPE("bd"), LEVEL("bd"),
    ]),
    ("sd", "Snare", [
        # Two circuits sharing a trigger: a 340 Hz bridged-T shell and the
        # shared noise source through C514/R511's 27 ms envelope.
        TUNE("sd"),
        PV("sd_decay", "Decay", 0.02, 0.6, EXP, 0.060),
        # Not a control on the hardware — the balance is fixed by R520/R522
        # against R515 — so pot centre IS the hardware's own mix.
        P("sd_snappy", "Snappy", 0.0, 1.0, LIN, 64),
        DRIVE("sd"), DTYPE("sd"), LEVEL("sd"),
    ]),
    ("rs", "Rim Shot", [
        # A 700 mH coil and 16.5 nF, struck and left alone. 1480 Hz, 5 ms,
        # and the loudest thing in the factory table at 0.8 Vpp.
        TUNE("rs"),
        PV("rs_decay", "Decay", 0.002, 0.08, EXP, 0.005),
        DRIVE("rs"), DTYPE("rs"), LEVEL("rs"),
    ]),
    ("hh", "Hi-Hat", [
        # Shared noise through C525 .018 / R537 1.5M. There is only ONE hat on
        # a CR-78, so there is no choke control anywhere in this module.
        RATIO("hh"),
        PV("hh_decay", "Decay", 0.01, 0.6, EXP, 0.060),
        DRIVE("hh"), DTYPE("hh"), LEVEL("hh"),
    ]),
    ("cy", "Cymbal", [
        # Shared noise through C520 .12 / R530 4.7M into L3 45mH || C521
        # .0068 — a tank at 9098 Hz, derived from the parts list.
        RATIO("cy"),
        PV("cy_decay", "Decay", 0.05, 3.0, EXP, 0.350),
        DRIVE("cy"), DTYPE("cy"), LEVEL("cy"),
    ]),
    ("ma", "Maracas", [
        # The shortest lane on the board: C527 .0082 / R542 1M, 20 ms.
        RATIO("ma"),
        # MOD — see the note above RHY_STYLES. Range extended to the hi-hat's
        # own ceiling so the maracas can be played long; factory 20 ms
        # unmoved.
        PV("ma_decay", "Decay", 0.005, 0.6, EXP, 0.020),
        DRIVE("ma"), DTYPE("ma"), LEVEL("ma"),
    ]),
    ("cl", "Claves", [
        # The rim shot's twin — the same 700 mH coil, a smaller capacitor.
        TUNE("cl"),
        PV("cl_decay", "Decay", 0.005, 0.2, EXP, 0.018),
        DRIVE("cl"), DTYPE("cl"), LEVEL("cl"),
    ]),
    ("hb", "Hi Bongo", [
        TUNE("hb"),
        PV("hb_decay", "Decay", 0.01, 0.4, EXP, 0.040),
        DRIVE("hb"), DTYPE("hb"), LEVEL("hb"),
    ]),
    ("lb", "Low Bongo", [
        TUNE("lb"),
        PV("lb_decay", "Decay", 0.01, 0.4, EXP, 0.040),
        DRIVE("lb"), DTYPE("lb"), LEVEL("lb"),
    ]),
    ("lc", "Low Conga", [
        # The longest of the four drum channels — 150 ms against the bongos'
        # 40, from VR56 and the .022s.
        TUNE("lc"),
        PV("lc_decay", "Decay", 0.03, 1.2, EXP, 0.150),
        DRIVE("lc"), DTYPE("lc"), LEVEL("lc"),
    ]),
    ("cb", "Cowbell", [
        # Two tones, VR67 at 800 Hz and VR68 at 555. Tune moves the pair.
        TUNE("cb"),
        PV("cb_decay", "Decay", 0.02, 0.8, EXP, 0.060),
        DRIVE("cb"), DTYPE("cb"), LEVEL("cb"),
    ]),
    ("tb", "Tambourine", [
        RATIO("tb"),
        # MOD — floor lowered to the hi-hat's so the tambourine can be played
        # as a tick; factory 220 ms unmoved.
        PV("tb_decay", "Decay", 0.013, 1.5, EXP, 0.220),
        DRIVE("tb"), DTYPE("tb"), LEVEL("tb"),
    ]),
    ("gu", "Guiro", [
        RATIO("gu"),
        PV("gu_decay", "Decay", 0.05, 1.0, EXP, 0.200),
        # THE SCRAPE RATE, not a pitch. The hardware has two fixed rates —
        # 125 Hz and 77 Hz off one VR59 — and this walks between them. Pot
        # centre is halfway, because neither of the two is more "correct".
        P("gu_rate", "Rate", 0.0, 1.0, LIN, 64),
        DRIVE("gu"), DTYPE("gu"), LEVEL("gu"),
    ]),
    ("mb", "Metal Beat", [
        # Three inverter oscillators on IC501 at 6170/5620/4080 Hz. The bank
        # FREE-RUNS, so no two hits are quite the same.
        RATIO("mb"),
        PV("mb_decay", "Decay", 0.01, 0.5, EXP, 0.050),
        DRIVE("mb"), DTYPE("mb"), LEVEL("mb"),
    ]),
]

# ---- MODS ------------------------------------------------------------------
#
# Things this module does that a CR-78 does not. They are listed here in one
# place, and each one is marked "MOD" at its site, because the whole point of
# the rest of this project is that the defaults ARE the machine — and a change
# nobody can find is how that stops being true.
#
# 1. DECAY RANGE ON THE MARACAS AND THE TAMBOURINE.
#
#    A CR-78 has ONE hi-hat, so in practice the maracas and the tambourine are
#    what you reach for when you want a second and third one. Their decay
#    ranges were drawn tight around the factory figures, which made that
#    awkward: the maracas topped out at 200 ms — plenty for its own 20 ms
#    voice, nowhere near an open hat — and the tambourine bottomed out at
#    30 ms, too long for a tick.
#
#    Both now span what the HI-HAT spans, so the three noise lanes cover the
#    same ground and can be played as a set:
#
#        hi-hat       10 ms .. 600 ms   (unchanged, factory  60 ms)
#        maracas       5 ms .. 600 ms   (was ..200 ms, factory 20 ms)
#        tambourine   13 ms .. 1.5 s    (was 30 ms.., factory 220 ms)
#
#    WHAT THIS COSTS THE DEFAULT, EXACTLY. Defaults are written here as
#    engineering values and PV() solves the pot position, so the factory
#    figure itself does not move — but the pot is 128 steps, and re-spanning
#    the range re-quantises where 20 ms and 220 ms land. Measured:
#
#        maracas     20.159 ms -> 20.170 ms   (+0.056%)
#        tambourine 222.164 ms -> 222.836 ms  (+0.303%)
#
#    Put that in proportion: one pot step on the OLD maracas range was 2.9%
#    and on the old tambourine range 3.1%. So both shifts are a small fraction
#    of a SINGLE STEP of the control — the pot could never represent 20 ms or
#    220 ms exactly in the first place, and these land no further from them
#    than they already did. The endpoints were chosen for that: 0.6 and 0.013
#    are the round values whose rounding falls closest (0.5 would have cost
#    1.6%, 0.010 on the tambourine 2.3%).
#
#    Nothing else changed — not the envelope, not the filter, not the noise
#    source, not one component value. Only how far the knob goes.
#    tools/golden_check renders every lane at defaults, the demo pattern and
#    all twenty presets and holds the lot to the bit, so any FUTURE change
#    here has to be deliberate and re-blessed.
#
# 2. A HAT CHOKE GROUP.
#
#    A CR-78 has one hi-hat and no open/closed pair, so it has nothing to
#    choke and no choke control — this module said so, at length, and it was
#    right. This is the mod that changes it, and it is a mod rather than a
#    correction: the hardware does not do this.
#
#    What makes it worth having is the decay mod above. Once the maracas can
#    ring for 600 ms and the tambourine for 13, those two plus the hi-hat are
#    a set of three noise lanes covering the same ground — which is a hat
#    section, and a hat section wants a choke.
#
#        Off     nothing chokes anything. THE DEFAULT, and bit-identical to
#                a build without this control at all.
#        MA/TB   maracas and tambourine choke each other. The hi-hat is left
#                alone, because at 60 ms it is already the short one.
#        All3    hi-hat, maracas and tambourine are one exclusive group —
#                whichever you hit last is the only one still ringing.
#
#    The choke is a 2 ms fade and a dump of the envelope capacitor, not a hard
#    stop: cutting a ringing tambourine dead puts a click on the front of
#    whatever cut it.
#
# ---- the preset rhythms ----------------------------------------------------
#
# The CR-78's own patterns, transcribed from pages 27-28 of the service notes.
# Three controls, exactly as the hardware splits them:
#
#   Style   which BUTTON, not which pattern. Seventeen of them, exactly the
#           seventeen on the panel.
#   A / B   the RHYTHM lever, and it does what the hardware's does.
#
# THE LEVER IS NOT A MEASURE SELECTOR, and getting that wrong is easy. Three
# of the CR-78's buttons carry TWO rhythms — they are printed "A-FOX TROT /
# B-TANGO", "A-MAMBO / B-CHA CHA", "A-BEGUINE / B-RHUMBA" — and the RHYTHM
# A/B lever on the right of the panel is what picks between them. On the
# fourteen single-label buttons it does nothing at all, exactly as on the
# hardware.
#
# That is also why the numbers work out: seventeen buttons, three of which
# are doubled by the lever, is twenty patterns. The score's own "A" and "B"
# bar marks are a different thing entirely — those are the two measures of
# each pattern, and they alternate on their own as the machine plays.
#
# Mode defaults to OFF, and that is load-bearing: with it off the module
# behaves exactly as it did before the rhythms existed, and nothing fires that
# the player did not ask for.
#
# Option text is sized for the stock grid's enum box (two lines of three
# characters), which is why the names are abbreviated the way they are. Order
# must match g_cr78_rhythms in cr78_rhythms.h; test/all.sh asserts it.
# The seventeen buttons, in panel order. Must match g_cr78_buttons in
# cr78_rhythms.h; tools/rhythm_check asserts it.
RHY_STYLES = ["Waltz", "Shufl", "SloRck", "Swing", "FoxTng",
              "Boogie", "Enka",
              "Bossa", "Samba", "MamCha", "BegRhu",
              "Rock1", "Rock2", "Rock3", "Rock4", "Disco1", "Disco2"]

RHYTHM_PAGES = [
    ("rhy", "Rhythm", [
        # No Bank control. It shipped as one, did nothing for a day, briefly
        # jumped Style around, and was cut on request: seventeen buttons need
        # no chaperone, and the panel's colour groups already say where the
        # ballroom ends and the rock begins.
        E("rhy_mode",  "Mode",  ["Off", "Play"], 0),
        E("rhy_ab",    "A/B",   ["A", "B"], 0),
        E("rhy_style", "Style", RHY_STYLES, 11),    # Rock 1
    ]),
]

# Tempo-synced delay: Time is a note DIVISION, not milliseconds, so it is an
# enum. Order must match kCR78DlyBeats in cr78_fx.h.
DIVS = ["1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.",
        "1/4", "1/2T", "1/4.", "1/2", "1/2."]

# The two send buses get a page each, reached by the jog.
FX_PAGES = [
    ("rev", "Reverb", [
        P("rev_decay", "Decay",  0.2,  0.93, LIN, 73),      # 0.62
        P("rev_tone",  "Tone",   0.0,  1.0,  LIN, 57),      # 0.45
        P("rev_hpf",   "HPF",   30.0, 800.0, EXP, 62),      # 150 Hz
        P("rev_level", "Level",  0.0,  1.2,  LIN, 85),      # 0.80
    ]),
    ("dly", "Delay", [
        E("dly_time",  "Time", DIVS, 7),                    # dotted eighth
        P("dly_fdbk",  "Fdbk",   0.0,  0.85, LIN, 52),      # 0.35
        P("dly_tone",  "Tone",   0.0,  1.0,  LIN, 51),      # 0.40
        P("dly_hpf",   "HPF",   30.0, 800.0, EXP, 62),      # 150 Hz
        P("dly_level", "Level",  0.0,  1.2,  LIN, 85),      # 0.80
    ]),
]

GLOBALS = [
    E("master_dist", "Master Dist", ["Off"] + DIST),
    P("master_drive", "Master Drive", 0.0, 10.0, LIN, 0),   # 0 = bypass
    # One-knob bus glue, ported from 9W9. NOT CR-78 circuitry and honest about
    # it: at zero the stage is not in the path at all (bit-identical).
    P("comp", "Comp", 0.0, 1.0, LIN, 0),
    P("volume", "Volume", 0.0, 1.0, LIN, 100),
    # No Accent pot, and on this machine that is a closer fit than it was on
    # the 808: the CR-78's ACCENT is a single panel-wide control voltage into
    # the BA662 VCA (VR104 50k(B)), not a per-voice switch. Velocity replaced
    # it — a full-velocity hit reaches the level an accented one always did,
    # so nothing gets quieter — and this knob is how far BELOW that a soft hit
    # falls. It only ever carves down.
    P("vel_depth", "Velocity", 0.0, 1.0, LIN, 127),
    # No Choke enum. The CR-78 has one hi-hat, not a closed/open pair, so
    # there is nothing to choke and adding a control for it would be a lie.
    E("note_map", "Note Map", ["Rack 36", "GM"]),            # RAC/36 in the box
    # MOD — see the note above the preset rhythms. Appended at the END of
    # GLOBALS on purpose: registration order is storage order and it is
    # append-only, so a new control goes last however it reads on the panel.
    E("hat_choke", "Choke", ["Off", "MA/TB", "All3"], 0),
]

# ---------------------------------------------------------------------------


def viz_for(p):
    """Honest viz declarations for the 0.12.x param-pages renderer.

    Levels draw as faders. Nothing on this module is a click level pretending
    to be an envelope time, so unlike 8W8 there is no viz:false case — but the
    hook stays, because the next voice that needs one will need it here.
    """
    k = p["key"]
    if k.endswith("_level") or k == "volume":
        return {"kind": "fader"}
    return None


# ---- two audiences, two spellings of the same control ----------------------
#
# The knob grid draws a param under a page that already says COWBELL, so there
# it wants to read "Decay". Schwung's LFO and mod pickers draw one FLAT list
# across the whole module, where FOURTEEN voices each contribute a "Decay" and
# nothing says which pad you are about to automate.
#
# param_meta.mjs merges the two sources as {...inlineHierarchy, ...chainParams}
# and resolves `meta.label || meta.name`. chain_params spells the display
# string `name`; hierarchy entries spell it `label` — so a `label` on the
# hierarchy entry survives the merge AND wins the fallback. That is the seam:
# prefix `name` for the picker, keep `label` bare for the page.
#
# THE TAGS ARE THE MACHINE'S OWN ABBREVIATIONS, not the page ids uppercased.
#
# The CR-78's INSTRUMENTS SELECTOR rotary is engraved with them, and Behringer
# kept the same set on the RD-78's TRACK INSTRUMENT selector:
#
#     BD 1   SD 2   RS 3   CP 4   HH 5   CY 6   M 7
#     8 C/TB   9 HB/G   10 LB/MB   11 LC/CB   12 AC
#
# So the maracas are M, the claves are C and the guiro is G — one letter each,
# because that is what is printed on the panel. Deriving these from the page
# ids instead gives MA, CL and GU, which are perfectly readable and are not
# what the machine says. (CP is the clap, which the RD-78 adds and a CR-78
# does not have; AC is the accent, which lives on Master here.)
#
# The two send buses and the rhythm page are qualified the same way, and they
# have to be: Tone, HPF and Level exist on both buses, and Mode/Bank/Style
# would otherwise be bare.
PICKER_TAG = {
    "bd": "BD", "sd": "SD", "rs": "RS", "hh": "HH", "cy": "CY",
    "ma": "M",  "cl": "C",  "hb": "HB", "lb": "LB", "lc": "LC",
    "cb": "CB", "tb": "TB", "gu": "G",  "mb": "MB",
    "rev": "REV", "dly": "DLY", "rhy": "RHY",
}

# Every page must have a tag, or its controls go into the flat picker bare and
# collide with another voice's. Checked rather than trusted.
_untagged = [pid for pid, _, _ in PAGES + RHYTHM_PAGES + FX_PAGES
             if pid not in PICKER_TAG]
if _untagged:
    raise SystemExit("no picker tag for page(s): " + ", ".join(_untagged))


def picker_name(key, name):
    head = key.split("_", 1)[0]
    tag = PICKER_TAG.get(head)
    return f"{tag} {name}" if tag else name


def chain_param(p):
    if p["kind"] == "enum":
        d = {"key": p["key"], "name": picker_name(p["key"], p["name"]),
             "type": "enum", "options": p["options"]}
    else:
        d = {"key": p["key"], "name": picker_name(p["key"], p["name"]),
             "type": "int", "min": 0, "max": 127}
    # The factory default, so a reset gesture (stock Mute+knob, Movy, the web
    # panel's double-click) lands on an aligned CR-78 and not on a guessed 64.
    d["default"] = p["default"]
    v = viz_for(p)
    if v is not None:
        d["viz"] = v
    return d


cp, levels, root, pots, enums, seen = [], {}, [], [], [], set()


def register(p):
    if p["key"] in seen:
        return
    seen.add(p["key"])
    cp.append(chain_param(p))
    (pots if p["kind"] == "pot" else enums).append(p)


# THE BASS DRUM IS DRY, on purpose — see SENDS.
PAGE_SENDS = {pid: ([] if pid == "bd" else SENDS(pid)) for pid, _, _ in PAGES}

# REGISTRATION ORDER IS STORAGE ORDER for the state blob, and it is
# APPEND-ONLY. Registering in page order instead would interleave the sends
# into the middle of the pot table and renumber every patch ever saved.
for pid, label, params in PAGES:
    for p in params:
        register(p)
for p in GLOBALS:
    register(p)
for pid, _, params in PAGES:
    for p in PAGE_SENDS[pid]:
        register(p)
for pid, label, params in FX_PAGES:
    for p in params:
        register(p)
for pid, label, params in RHYTHM_PAGES:
    for p in params:
        register(p)

# Now the pages, which may reference anything registered above.
for pid, label, params in PAGES:
    full = params + PAGE_SENDS[pid]
    if len(full) > 8:
        raise SystemExit(f"page {pid} has {len(full)} params — max 8 knobs")
    levels[pid] = {"name": label,
                   "knobs": [p["key"] for p in full],
                   "params": [{"key": p["key"], "label": p["name"]}
                              for p in full]}
    root.append({"level": pid, "label": label})

# The rhythm page sits directly AFTER the instruments and before the send
# buses, because that is the order you reach for them in: pick a kit, pick a
# pattern, then decide how wet it is.
for pid, label, params in RHYTHM_PAGES:
    levels[pid] = {"name": label,
                   "knobs": [p["key"] for p in params],
                   "params": [{"key": p["key"], "label": p["name"]}
                              for p in params]}
    root.append({"level": pid, "label": label})

for pid, label, params in FX_PAGES:
    levels[pid] = {"name": label,
                   "knobs": [p["key"] for p in params],
                   "params": [{"key": p["key"], "label": p["name"]}
                              for p in params]}
    root.append({"level": pid, "label": label})

root += [{"key": p["key"], "label": p["name"]} for p in GLOBALS]
levels["root"] = {"name": "CW-78",
                  "knobs": ["master_dist", "master_drive", "comp", "volume",
                            "vel_depth", "hat_choke"],
                  "params": root}

# Two plugin-level keys that live on NO page but must be in chain_params: the
# remote-UI manager seeds and periodically re-reads exactly the keys listed
# here, and a key it does not know about never reaches the browser.
#
# ui_focus is the page the on-device editor is showing: 0-13 the voices,
# 14 Master (which unlike 8W8 HAS A PAD here), 15 Reverb, 16 Delay — the last
# two are what pad 16's alternating FX press publishes.
cp.append({"key": "ui_focus", "name": "Focus", "type": "int",
           "min": 0, "max": 16, "default": 0})
cp.append({"key": "mutes", "name": "Mutes", "type": "int",
           "min": 0, "max": 16383, "default": 0})    # fourteen lanes, 14 bits

# The point of the prefixes is that no two entries in the flat picker read the
# same. Assert it rather than trusting it: a duplicate here is invisible — the
# picker simply shows two identical lines and automating either is a coin
# flip. ui_focus and mutes are plumbing and never reach the picker.
_picker = [e for e in cp if e["key"] not in ("ui_focus", "mutes")]
_dupes = {}
for e in _picker:
    _dupes.setdefault(e["name"], []).append(e["key"])
_clash = {n: k for n, k in _dupes.items() if len(k) > 1}
if _clash:
    raise SystemExit("two controls would read the same in the LFO picker: "
                     + "; ".join(f"{n!r} <- {', '.join(k)}" for n, k in _clash.items()))

cpj = json.dumps(cp, separators=(",", ":"))
uhj = json.dumps({"levels": levels}, separators=(",", ":"))

# ---- the host's parameter channel ------------------------------------------
#
# module.json is capped at 8 KB by Schwung's loader, which is why these two
# payloads are served from the DSP instead. The DSP's ceiling is different and
# much higher: shadow_constants.h sets
#
#     #define SHADOW_PARAM_VALUE_LEN 65536
#
# and get_param() in cr78_plugin.cpp REFUSES to truncate — it returns -1
# rather than hand back half a JSON document. So overrunning this does not
# corrupt anything; it makes the entire parameter surface vanish, and the
# editor comes up with nothing on it. Caught here instead.
HOST_PARAM_MAX = 65536
for name, payload in (("chain_params", cpj), ("ui_pages", uhj)):
    if len(payload) > HOST_PARAM_MAX // 2:
        raise SystemExit(
            f"{name} is {len(payload)} B — over half the host's "
            f"{HOST_PARAM_MAX} B parameter buffer (SHADOW_PARAM_VALUE_LEN). "
            f"get_param refuses to truncate, so the editor would come up empty.")


def cstr(s):
    q, b = chr(34), chr(92)
    return "\n".join(
        f'    "{s[k:k+100].replace(b, b*2).replace(q, b+q)}"'
        for k in range(0, len(s), 100))


pot_rows = "\n".join(
    f'    {{ "{p["key"]}", {p["min"]:>10.4f}f, {p["max"]:>10.4f}f, '
    f'{"CR78_EXP" if p["curve"] == EXP else "CR78_LIN"}, {p["default"]:>3} }},'
    for p in pots)
enum_rows = "\n".join(
    f'    {{ "{p["key"]}", {len(p["options"]):>2}, {p["default"]:>2} }},'
    for p in enums)

root_dir = pathlib.Path(__file__).resolve().parent.parent
(root_dir / "src/dsp/cr78_params.h").write_text(f"""\
/* Generated by scripts/gen_params.py — DO NOT EDIT BY HAND.
 *
 * module.json is capped at 8 KB by Schwung's loader, so chain_params and the
 * page hierarchy are served dynamically from the DSP via get_param().
 *
 * The pot and enum tables below define storage order for the state blob.
 * Appending is safe; reordering breaks every saved patch.
 */
#ifndef CR78_PARAMS_H
#define CR78_PARAMS_H

typedef enum {{ CR78_LIN = 0, CR78_EXP = 1 }} cr78_curve_t;

typedef struct {{
    const char   *key;
    float         min;
    float         max;
    cr78_curve_t  curve;
    int           def;      /* default POT position, 0..127 */
}} cr78_pot_t;

typedef struct {{
    const char *key;
    int         count;      /* number of options */
    int         def;
}} cr78_enum_t;

#define CR78_NUM_POTS  {len(pots)}
#define CR78_NUM_ENUMS {len(enums)}

static const cr78_pot_t g_cr78_pots[CR78_NUM_POTS] = {{
{pot_rows}
}};

static const cr78_enum_t g_cr78_enums[CR78_NUM_ENUMS] = {{
{enum_rows}
}};

#define CR78_CHAIN_PARAMS_LEN {len(cpj)}
static const char cr78_chain_params_json[] =
{cstr(cpj)};

#define CR78_UI_PAGES_LEN {len(uhj)}
static const char cr78_ui_pages_json[] =
{cstr(uhj)};

#endif /* CR78_PARAMS_H */
""")

# ---- movy_config.json: same source, Movy's shape. --------------------------
# HARD RULE (cost a debugging session on Tablor): a Movy bank is EXACTLY ONE
# PAGE. buildConfigPages keys bankGroups per BANK but the UI indexes per PAGE,
# so a multi-row bank shifts every following page's label. One row per bank.
SHORT = {"Tune": "TUNE", "Decay": "DECAY", "Attack": "ATTK", "Tone": "TONE",
         "Drive": "DRIVE", "Distortion": "DIST", "Level": "LEVEL",
         "Snappy": "SNAPY", "Rate": "RATE", "Rev": "REV", "Dly": "DLY",
         "Fdbk": "FDBK", "HPF": "HPF", "Time": "TIME", "Comp": "COMP",
         "Master Dist": "MDIST", "Master Drive": "MDRV",
         "Volume": "VOL", "Velocity": "VEL", "Note Map": "NMAP",
         "Choke": "CHOKE",
         "Mode": "MODE", "Bank": "BANK", "A/B": "A/B", "Style": "STYLE"}
MOVY_NAME = {"bd": "Kick", "sd": "Snare", "rs": "Rim", "hh": "Hi Hat",
             "cy": "Cymbal", "ma": "Maracas", "cl": "Claves",
             "hb": "Hi Bngo", "lb": "Lo Bngo", "lc": "Lo Cnga",
             "cb": "Cowbell", "tb": "Tambrn", "gu": "Guiro", "mb": "Mtl Bt"}


def movy_slot(p):
    d = {"key": p["key"], "short": SHORT[p["name"]], "full": p["name"]}
    if p["kind"] == "enum":
        d["type"] = "enum"
        d["options"] = list(p["options"])   # already sized for a 32 px cell
    else:
        d["type"] = "int"; d["min"] = 0; d["max"] = 127
    return d


banks = []
for pid, label, params in PAGES:
    full = params + PAGE_SENDS[pid]
    row = [movy_slot(p) for p in full] + [None] * (8 - len(full))
    banks.append({"name": MOVY_NAME[pid], "rows": [row]})
for pid, label, params in RHYTHM_PAGES:
    row = [movy_slot(p) for p in params] + [None] * (8 - len(params))
    banks.append({"name": label, "rows": [row]})
for pid, label, params in FX_PAGES:
    row = [movy_slot(p) for p in params] + [None] * (8 - len(params))
    banks.append({"name": label, "rows": [row]})
banks.append({"name": "Master", "global": True,
              "rows": [[movy_slot(p) for p in GLOBALS] + [None] * (8 - len(GLOBALS))]})
movy = {"id": "cw78", "name": "CW-78",
        "drum": {"padCount": 14, "padNoteStart": 36, "rawMidi": False},
        "banks": banks}
(root_dir / "src/movy_config.json").write_text(json.dumps(movy, indent=2) + "\n")

print(f"chain_params {len(cpj)}B  ui_pages {len(uhj)}B  "
      f"(host buffer {HOST_PARAM_MAX}B)  movy banks={len(banks)}  "
      f"pages={len(levels)}  pots={len(pots)}  enums={len(enums)}  "
      f"params={len(cp)}")
