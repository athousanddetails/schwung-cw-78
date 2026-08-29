# CR-78 — circuit analysis from the service notes

Source: *Roland CR-78 Service Notes*, June 20 1979 (printed Jul '82 E-2), 34 pages,
`reference/papers/Roland_CR78_Service_Notes.pdf`. Scanned, no text layer — every
number below was read off the page images at 220 dpi.

The DSP here is entirely new. 8W8 and 9W9 contribute the module shell — pad
gestures, page/knob UX, distortion characters, send buses, comp, velocity, the
parameter generator and the test harness — and nothing about how a voice sounds.

This is the pre-build study. It records **what the schematic says**, **what is
derived from it**, and **what is still a guess** — in that order, and labelled,
because 8W8's `sc808_tom_circuit.h` was rewritten once for exactly the failure of
not keeping those three apart.

---

## 1. Where the sound lives

The CR-78 splits cleanly in two, and only half of it matters here.

| Board | What it does | Relevant? |
|---|---|---|
| **GL-9 / GL-9A / GL-9B** | µPD8048 microcomputer, ROM, RAM, decoders, latches, switch matrix, master oscillator, fade, accent | **No** — this is the sequencer. Move's own sequencer replaces it. |
| **VG-11 / VG-11A** | Every voice, the noise source, the summing amp, the BA662 VCA | **Yes. This is the whole job.** |
| OP-100/103/104, RS-14/15/17 | Panel switches, jacks, Add-Voice sliders | Level/panel only |

The voicing schematic is **pages 25 and 26** (sheet numbers 12 and 13),
*"VOICING & POWER SUPPLY CIRCUIT DIAGRAM"*. The VG-11 connector on page 6 names
the lanes: `CB, GuP, GuT, TbH, TbL, SD, CY, HH, M, RS, C, BD, LB, LC, HB`.

Two board revisions exist. **VG-11A (S/N 780700+) is the reference** — page 24
notes VG-11A replaces VG-11, and page 26 is drawn as VG-11A.

---

## 2. The factory voicing table — the ground truth

Page 30 is the single most valuable page in the document. It is Roland's own
factory alignment procedure: for every voice, the **frequency, the decay time and
the output amplitude** the unit is trimmed to, with the trimmer that sets each.

This is the fitting target. A model that reproduces this table is right; one that
does not is wrong, whatever the schematic maths says.

| Voice | Freq (Hz) | Decay (ms) | Amp (Vpp) | Tune trim | Decay trim | Level trim |
|---|---|---|---|---|---|---|
| BD  Bass Drum      | 62.5 | 100 | 0.4  | VR57 | VR58 | — |
| SD  Snare Drum     | 340  | 60  | 0.4  | —    | —    | VR61 |
| RS  Rim Shot       | 1480 | 5   | 0.8  | —    | —    | — |
| HH  Hi-Hat         | —    | 60  | 0.4  | —    | —    | — |
| CY  Cymbal         | —    | 350 | 0.4  | —    | —    | VR60 |
| M   Maracas        | —    | 20  | 0.4  | —    | —    | — |
| C   Claves         | 2630 | 18  | 0.15 | —    | —    | — |
| HB  Hi Bongo       | 600  | 40  | 0.15 | VR51 | VR52 | — |
| LB  Low Bongo      | 400  | 40  | 0.15 | VR53 | VR54 | — |
| LC  Low Conga      | 208  | 150 | 0.3  | VR55 | VR56 | — |
| CB  Cowbell hi     | 800  | 60  | 0.2  | VR67 | —    | — |
| CB  Cowbell lo     | 555  | 60  | 0.2  | VR68 | —    | — |
| Tb  Tambourine     | —    | 220 | 0.25 | —    | —    | VR62 |
| GU  Guiro hi       | 125  | —   | 0.3  | VR59 | —    | VR63 |
| GU  Guiro lo       | 77   | —   | 0.3  | VR59 | —    | VR63 |
| MB  Metallic hi    | 6170 | 50  | 0.35 | VR64 | —    | — |
| MB  Metallic mid   | 5620 | 50  | 0.35 | VR65 | —    | — |
| MB  Metallic lo    | 4080 | 50  | 0.35 | VR66 | —    | — |

Page 30 also gives scope traces for BD (0.4 V, decaying sine) and SD
(0.7 V peak, 0.4 V after 20 ms) — attack/decay shape references.

Page 34 (Manual Change Information) corrects page 29's adjustment steps:
step 1-2 `T = 10ms → 200ms`, step 2-1 `4 to 55 → 4 to 5`. Sequencer-side only.

**Fourteen voices.** Move's left 4×4 pad block holds sixteen, so unlike 8W8 there
is room for a Master pad and one spare. There is no high conga on a CR-78 — the
roster is two bongos and one conga.

---

## 3. Three topology families, not fourteen voice designs

Exactly like the 808, the CR-78 builds everything from a few blocks. Every voice
starts the same way: a trigger line through a **0.027 µF cap, a 270 k resistor to
ground and a steering diode** — an identical front end on all fourteen lanes
(C500/R500/D500, C590/R503/D501, C567/R601/D502, C573/R611/D503, C579/R621/D504,
C584/R631/D505, C508/R506/D506, C518/R525/D510, C524/R533/D513, C526/R538/D519,
C534/R554, C547/R584/D525 …). That is one `PulseShaper`, shared.

### Family A — passive shock-excited LC tank (Claves, Rim Shot)

No transistor at all. The diode-steered pulse kicks a parallel LC and it rings
down on its own losses.

**Rim shot** — `L2` (700 mH) ∥ `C591 .015` ∥ `C592 .0015` = 16.5 nF:

    f = 1 / (2π √(0.7 × 16.5e-9)) = 1481 Hz

The table says **1480 Hz**. That is not a fit — that is the schematic and the
factory table agreeing to four figures, and it confirms `L1`/`L2` (marked "3R")
are the 700 mH coil `022-033` from the parts list on page 31.

**Claves** — `L1` (700 mH) ∥ `C506 .0047`:

    f = 1 / (2π √(0.7 × 4.7e-9)) = 2774 Hz

Table says 2630 Hz, 5 % low. The tank is loaded by `C502 .0022`, `C503 250p`,
`C505 .0056` and `C504 .001` around it; adding C503 alone gives 2703 Hz. **Derived
2774, target 2630** — the loading is real and must be modelled, not trimmed away.

Ring time is set by tank Q alone: RS 5 ms, C 18 ms. Both very short — no feedback,
nothing to sustain them.

### Family B — three-section RC phase-shift ladder in a transistor feedback loop (BD, LB, HB, LC, SD-shell)

> **Corrected.** This section first called the network a bridged-T and carried
> a fitted constant of 3.0 to reach the factory table. Re-read at 420 dpi it is
> plainly three series capacitors with three 10 k shunt resistors — a
> phase-shift ladder, not a bridged-T. Solving the real chain for its 180°
> point reproduces all four factory frequencies with **nothing fitted**. The
> constant is gone. What follows is the corrected reading.

A bridged-T in a feedback loop is a common analogue drum idiom — the 808 uses one
too — so the generic `BridgedT` + loop-gain *primitive* carries over. Nothing
else does: the network values, the tuning, the decays and the voicing are the
CR-78's own and share nothing with an 808 lane. A trigger fires a T-network of series caps with shunt
resistors, wrapped around a transistor (`Q501–Q504`) with a 1.5 M feedback
resistor. Decay is loop gain, not an envelope.

Per-channel values, read off page 25:

| | BD | LC | LB | HB |
|---|---|---|---|---|
| bridging cap | C585 .082 | C580 .027 | C574 .018 | C568 .0082 |
| series caps | C586 .082, C587 .068, C588 .068 | C581 .022, C582 .022, C583 .022 | C575 .012, C576 .012, C577 .01 | C569 .0082, C570 .0068, C571 .0068 |
| shunt R | R635 10k | R625 10k | R615 10k | R605 10k |
| series R | R636 15k | R626 15k | R616 15k | R606 15k |
| input R | R634 15k | R624 15k | R614 15k | R604 15k |
| **Tune trim** | VR57 10k(B) | VR55 10k(B) | VR53 10k(B) | VR51 10k(B) |
| **Decay trim** | VR58 500(B) | VR56 500(B) | VR54 500(B) | VR52 500(B) |
| emitter R | R637 100 | R627 100 | R617 100 | R607 100 |
| feedback R | R638 1.5M | R628 1.5M | R618 1.5M | R608 1.5M |
| collector R | R639 10k | R629 10k | R619 10k | R609 10k |
| output R | R640 10k | R630 560k | R620 680k | R610 1.5M |
| transistor | Q504 | Q503 | Q502 | Q501 |
| front end | C584 .027 / R631 270k / D505 | C579 / R621 / D504 | C573 / R611 / D503 | C567 / R601 / D502 |
| **target f0** | 62.5 Hz | 208 Hz | 400 Hz | 600 Hz |
| **target decay** | 100 ms | 150 ms | 40 ms | 40 ms |

**The network, traced.** Input through a 15 k series resistor, then three
(series capacitor → 10 k shunt resistor) sections, loaded at the output end by
the 1.5 M collector-to-base feedback. The transistor inverts; the ladder
supplies the other 180°; the loop rings where it does so.

Solving that chain — an ordinary ABCD cascade, bisected on phase — gives:

| | derived f0 | table f0 | error |
|---|---|---|---|
| BD | 62.96 Hz | 62.5 | **+0.7 %** |
| LC | 209.10 Hz | 208 | **+0.5 %** |
| LB | 406.57 Hz | 400 | **+1.6 %** |
| HB | 629.60 Hz | 600 | **+4.9 %** |

**Nothing is fitted.** Those are the capacitors on the schematic and the 10 k
resistors beside them, put through the network equations, landing on numbers
Roland measured off hardware in 1979. The implied tune-trimmer settings are
0.993 / 0.995 / 0.984 / 0.953 — all within 5 % of unity, which is what tune
trimmers are for.

The textbook `1/(2πRC√6)` does *not* work here and is worth saying why: it
assumes three identical sections, an ideal source and no load, and this network
has none of the three. Solving the real chain is what removes the correction
factor.

**Snare** is this family plus a second path. The shell (`C509 .01, C510 .01,
C511 .0056, R516 15k, R517 68k, R518 15k, R519 1M, Q505, C512 100p, R520 47k,
C513 .022, R522 1.5M`) is a bridged-T ringing at 340 Hz. The snappy path
(`D507, R508 470k, Q514, R523 820k, R524 10k, D508, R511 1.5M, C514 .018,
R512 47k, C515 .0082, R513 470k, Q506, R514 2.7k, C516 .0056, C517 250p,
R515 100k`) is a noise gate with its own envelope. `C513` is marked as changed
from 250 p to .022, and `R521 33k` is struck out — a factory revision, take the
handwritten values.

### Family C — noise through a gated envelope (HH, CY, M, and SD's snap)

One shared noise source: `Q533` (a 2SC828-R selected for noise, per the parts
list "for noise"), `C602 .01`, `R564 100k`, `Q525`, `R565 2.2M`, bussed out
through `R566 470k` and the four `VR60–VR63` 50 k(B) trimmers.

Each lane is the same shape with one cap changed — the **envelope cap sets the
decay, and its RC matches the factory table**:

| Voice | envelope cap | discharge R | τ = RC | table decay | filter |
|---|---|---|---|---|---|
| CY | C520 .12 | R530 4.7M | 564 ms | 350 ms | `L3` 45 mH ∥ `C521 .0068` → **9098 Hz**, then C522/C523 470p |
| HH | C525 .018 | R537 1.5M | 27 ms | 60 ms | C528 .0082 out |
| M  | C527 .0082 | R542 1M | 8.2 ms | 20 ms | — |

The τ:table ratio is 1.61 / 0.45 / 0.41 — CY is the outlier because its 350 ms is
measured to a different threshold on a much louder tail. The *ordering and the
relative spacing* come straight out of the caps.

### The metal voices

**Metallic Beat** is three RC relaxation oscillators on an `MC14069` hex inverter
(`IC501`), each buffered by a second inverter with 39 k feedback and mixed through
470 k — a plain inverter relaxation bank — the generic block is familiar, the three
frequencies and the mixing network are the CR-78's:

| | R | C | trim | target |
|---|---|---|---|---|
| MB hi | R573 47k | C544 .0015 | VR64 20k | 6170 Hz |
| MB mid | R575 47k | C545 .0018 | VR65 20k | 5620 Hz |
| MB lo | R577 47k | C546 .0022 | VR66 20k | 4080 Hz |

Fitting `f = k/(RC)` gives k = 0.435 / 0.476 / 0.422 — **mean 0.444, ±6 %**, again
inside the trimmers' range. One constant, three oscillators.

**Cowbell** — trigger → `Q527`, `C547 .027`, `D525`, `R585 560k` → `Q528`
envelope → `D526`, `C548 .022`/`C549 .018`, `R588 1.5M`, `R589 100k` → `Q511`
with `L7 45 mH` and `R592 1k` → then two trimmed stages `Q529` (VR67, 800 Hz) and
`Q530` (VR68, 555 Hz) with `C553/C554 .0082×3`, `C555 .018`, `C557`, `C558`.
Two-tone, as expected. **The two oscillator stages need tracing** — `C550 .47`
is drawn as an electrolytic and is almost certainly a coupling cap, not the tank.

**Guiro** is the odd one: a cross-coupled **astable multivibrator**
(`Q520`/`Q52`, `C529 .068`, `C530 .068`, `R543 82k`, `R544 56k`, `R545 56k`,
`VR54 100k(B)`, `R674 15k`, `R675 10k`, `D520`) running at 125 Hz (hi) or 77 Hz
(lo) — the scrape rate, not a pitch — driving `Q508` and an `L4 45 mH ∥ C532 .015`
tank at **6126 Hz**. A rasp, not a drum.

**Tambourine** — two trigger paths of different weight (`D521/R557 270k` and
`D522/R558 820k`) into `Q524`, `C536 .056`, `R560 2.2M`, `C537 .01`, `R561 47k`,
`Q509`, tank `L5` ("1R" coil, value not in the parts list beyond the designation)
∥ `C538 .033`, out through `C539 250p`. 220 ms decay. **L5's inductance is
unknown** — the parts list gives `022-031 coil no.31 1R` with no henries. Solve it
from the 220 ms/whatever-frequency target, and say so in the header.

---

## 4. Output chain

Summing amp `IC502` (µPC4558) → **BA662 VCA** (`IC503`, the same OTA the 808 and
303 use) under the fade/accent control voltage → `R656 820k` → `Q513` → Volume →
OP-103/OP-104, High out (220 k, 3.5 Vpp) and Low out (10 k, 5.5 Vpp).

**Accent is a control voltage into the VCA**, panel-wide, not per voice — same
family as 8W8's "velocity is a trigger voltage" decision. `VR104 50k(B)` sets
depth. There are also **"Add Voice" sliders** (Guiro, Tambourine, Metallic Beat,
Cowbell) on OP-103 via `R701/R702/R703 33k` + `VR20/21/22 10k(B)` — per-voice
level, panel-side.

A **"Sound Killer"** mutes the output during power on/off transients (`Q512`,
`C558`, `Q535`, `Q532`). Not modelled — it exists to suppress a click the
software will never make.

---

## 5. What is derived, what is fitted, what is unknown

**Derived from the schematic, confirmed by the factory table:**
- rim shot 1481 Hz vs 1480 — exact
- the four bridged-T channels' component values and their ≈3.0 common ratio
- MB's three oscillators and their ≈0.444 common constant
- the three noise envelope caps and their decay ordering
- CY's 9098 Hz tank, GU's 6126 Hz tank

**Fitted, and must say so in the source:**
- the drum channels' two-pole equivalent **Q** (0.612), fitted to page 30's
  *decay* column — the frequencies are physics, the Q is a fit to the other
  half of the same table
- claves' tank loading (2774 derived → 2630 target)
- CY's envelope threshold (τ 564 ms → 350 ms table)
- every loop gain: the 500 Ω decay trimmers set it, and the schematic does not
  give the transistor's operating point

**Unknown, needs a decision recorded in the header:**
- `L5` (tambourine) inductance — not in the parts list
- the cowbell's two oscillator stages
- the guiro's rasp waveform shape out of the multivibrator into the tank

---

## 6. Page index

| Pages | Contents |
|---|---|
| 1 | Panel exploded view, part numbers |
| 2 | Logic IC datasheets |
| 3–4 | Block diagram |
| 5–6 | Specifications, chassis layout, inter-board wiring, **VG-11 pinout**, fuses |
| 7–13 | 8048 timing, flow chart, circuit description, function detail (sequencer) |
| 14 | Memory/VCA IC datasheets |
| 15–20 | GL-9 / GL-9A / GL-9B board layouts |
| 21–23 | **Logic & rhythm switch schematic** |
| 24 | OP-104A, OP-103A, **VG-11A board layout + semiconductor list** |
| **25–26** | **VOICING & POWER SUPPLY SCHEMATIC — the whole job** |
| 27–28 | Rhythm pattern notation (all 34 presets) |
| 29–30 | Adjustment & checking — **the factory voicing table** |
| 31–32 | Parts list |
| 33–34 | Battery change, manual change information |
