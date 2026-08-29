# CW-78 — where we are

## 2026-08-29 afternoon: the five field reports, all fixed and redeployed

1. **Volume vs 9W9/8W8** — mix raised ~3 dB toward the kick; velocity
   double-dip removed (velocity fed both the trigger voltage AND the VCA
   gain; the gain now stays at unity above the voltage floor). On-device,
   vel 110: kick −9.13 dBFS vs 8W8's −8.88.
2. **Pad 15 → Main** — ui_chain.js still carried 8W8's sixteen-drum pad map
   (pad 3 pointed at "lt", 15 at "oh", 16 at "cy" — pages that do not exist),
   so the screen never followed most pads. Rewired to the CW-78 roster;
   pad 15 is MAIN and navigates even under the main-page lock.
3. **Pad 16 → FX** — alternates Reverb / Delay on each press, in the editor
   and in the plugin's ui_focus (15=rev, 16=dly) for the remote panel.
4. **Kick no power** — two causes. The pole-fold had kept the ring's LENGTH
   but dropped the loop's resonant gain (1/(1−g) ≈ 14×): restored, ring went
   from 0.083 to 1.16. And the ladder's strike feedthrough (it is a highpass
   network — the click leaks through) was missing: added, HP'd at 700 Hz.
   On the Move's speaker the click is most of what a kick is.
5. **Pads 3/4 "don't change"** — same stale pad map as #2. They always
   played the right drum; the screen never followed. Fixed with #2.


*(Renamed from 78W on 2026-08-28. The repo directory still says `schwung-78W`;
the DSP sources keep their `cr78_` prefix, which names the Roland machine being
modelled — the same split 8W8 has with `sc808_`.)*

**Complete, passing, BUILT and DEPLOYED. Verified on the Move itself.**

Installed at `/data/UserData/schwung/modules/sound_generators/cw78/` — all six
files, md5 verified, and the manager lists it (`data-installed="yes"`).

**Run ON the hardware, not emulated:**

| | |
|---|---|
| `cr78_loadtest` against the deployed `dsp.so` | **0 failures** |
| `cr78_voice_check` | every lane matches page 30, same numbers as the host |
| `cr78_bench` | **1.4 % of one core** on a busy pattern |

The CPU figure was the last thing nothing could answer. For scale, 6W6 shipped
at 22 % of a core. Worst single lane is the low conga at 2.0 %; all fourteen
firing every sixteenth — which no pattern does — is 5.9 %.

**Not yet loaded in a slot.** Slots 0, 2 and 3 are empty and slot 1 holds
tablor, and `set_param synth:module` does nothing on an empty slot — putting a
module into one is a device-UI action. Pick CW-78 on the Move and it loads the
file already there.

`dist/cw78-module.tar.gz` is a real aarch64 build off the Trash VPS
(`ssh vps`, Docker, ubuntu:22.04). Max glibc symbol **2.29**, well under the
Move's 2.35. Zero warnings.

`cr78_loadtest` — which dlopens the real `dsp.so` exactly as Schwung's chain
host does — **passes with 0 failures under aarch64 emulation** on that host
(qemu binfmt via `tonistiigi/binfmt`). That covers every pad sounding, the
Master pad and the empty sixteenth staying silent, every generated key
resolving, mutes, the send buses and a state round-trip. It is not the Move,
but it is the real shared object through the real host API.

Still unmeasured on real hardware: **CPU**. `cr78_bench` on this Mac reports
688x realtime on a busy pattern (0.15% of a core) and 216x on the worst single
lane, but Apple Silicon is not the Move's ARM and emulation cannot answer it.

## What it is

A Roland CR-78 for Schwung on Ableton Move. Fourteen voices, all circuit
models built from the June 1979 service notes, plus the machine's own preset
rhythms. The module shell — pad gestures, page/knob UX, the seven distortion
characters, the Rev/Dly send buses, the one-knob comp, velocity, the LFO
picker naming, the parameter generator and the test harness — comes from
9W9/6W6/8W8 so the kits feel identical under the hands. **No voice DSP is
shared with any of them.**

## The evidence

`./test/all.sh` — 10 steps, all PASS:

| Step | What it proves |
|---|---|
| generator in sync | the checked-in header is what `gen_params.py` emits |
| **golden_check** | every lane at defaults, the demo pattern and all 20 presets, held to the bit |
| **hat choke** | all three modes cut what they should and nothing they should not; Off is inert |
| compiles clean | engine, plugin and loadtest, `-Wall -Wextra`, zero warnings |
| **voice_check** | every lane against page 30's factory alignment table |
| **kit_check** | the balance against page 30's amplitude column; absolute level fitted to the busiest factory preset |
| **rhythm_check** | the pattern bank against the Style enum, every hit in range |
| sends silent at zero | 352 800 samples bit-identical with both buses wound up |
| state round-trips | 96 pots, 21 enums and the mute mask |
| the kit renders | 15 WAVs |

    lane |    meas f   page 30   err % |  meas dec   page 30   err %
    bd   |     65.01     62.50   +4.0  |    0.0966    0.1000   -3.4
    sd   |    336.27    340.00   -1.1  |    0.0586    0.0600   -2.3
    rs   |   1519.59   1480.00   +2.7  |    0.0051    0.0050   +2.5
    cl   |   2649.07   2630.00   +0.7  |    0.0182    0.0180   +0.9
    hb   |    579.25    600.00   -3.5  |    0.0383    0.0400   -4.3
    lb   |    394.41    400.00   -1.4  |    0.0398    0.0400   -0.6
    lc   |    207.33    208.00   -0.3  |    0.1413    0.1500   -5.8
    (hh/cy/ma/cb/tb/gu/mb: decay only — the table gives them no frequency)

## What got finished this round

**The fitted frequency constant is gone.** The four drum channels were read as
a bridged-T with a fudge factor of 3.0. Re-read at 420 dpi the network is
three series capacitors with three 10 k shunt resistors — a **phase-shift
ladder**, not a bridged-T. Solving the real ABCD chain for its 180° point:

    bass drum   62.96 Hz  vs  62.5   +0.7%
    low conga  209.10 Hz  vs 208.0   +0.5%
    low bongo  406.57 Hz  vs 400.0   +1.6%
    hi bongo   629.60 Hz  vs 600.0   +4.9%

Nothing fitted. The solve runs once per voice at init and is in
`phaseShiftLadderFreq()`.

**The preset rhythms.** Twenty patterns transcribed from pages 27–28, on the
machine's own 48-steps-to-the-bar clock (page 12 states the format: *"96 steps
(48 x 2)"*). Bank / A-B / Style controls on their own page after the
instruments. Accent is a real per-step channel, as the hardware has it.

**A real bug found by a test.** `voice_check` caught the tambourine 22 % short
on decay. The envelope was correct the whole time — the strike impulse was
dominating the lane's peak, so the decay was being measured down from a spike.
Swept and fixed.

**The kit level is now fitted to the busiest factory preset** (Bossa Nova),
not to a hand-written demo. At the old level Beguine peaked at +3.8 dBFS —
clipping, on a factory preset, out of the box.

## Mods

Two, both listed in the README and marked `MOD` at their site. Both default to
the machine's behaviour, and `golden_check` holds the default kit to the bit.

**Decay range on the Maracas and the Tambourine.** The machine has one hi-hat,
so those two are what you reach for when you want a second and a third. Their
ranges were drawn tight around the factory decays; all three noise lanes now
span the same ground (MA 5 ms–600 ms, TB 13 ms–1.5 s, HH unchanged).

The factory values did not move, and `golden_check` is what proves it. The pot
is 128 steps, so re-spanning a range re-quantises where 20 ms and 220 ms land:
+0.056 % and +0.303 %, both a small fraction of one step of a control whose own
resolution was 2.9 % and 3.1 %. The round endpoints were picked for exactly
that (0.5 would have cost 1.6 %, a 10 ms tambourine floor 2.3 %).

**A hat choke group**, on the Main page — Off / MA-TB / All3. It exists
*because* of the decay mod: once those two lanes reach hi-hat territory the
three of them are a hat section, and a hat section wants a choke. A 2 ms fade
and an envelope dump, never a hard cut; a lane never chokes itself on a
retrigger. **Off is the default and is inert** — the golden baseline did not
move by a bit when this was added.

A note from writing its test: a MUTED lane never chokes anything, because
`cr78_trigger` returns early on a muted lane. That is correct — a mute means
the lane does not play — and it is why the test silences with Level rather
than with a mute. The first version of the test used a mute and reported no
choking at all.

## A finding worth keeping

**The golden baseline is architecture-specific.** Blessed on x86-64 and run
against the same source cross-compiled for aarch64, 13 of 35 entries differ —
every one by under 0.005 dB, which is the last bit of a float. glibc's `powf`,
`expf` and `tanhf` are not correctly rounded and are not the same
implementation on the two architectures. So `golden_check` has two modes:
exact (host, catches real changes) and `--tol` (device, asks whether the ARM
build makes the same kit). At `--tol 0.05` the aarch64 build passes all 35.

## A bug found while deploying

`scripts/reload_slot.py` fired three websocket messages and printed *"the new
dsp.so is now the running one"* **unconditionally, without ever reading a
reply**. A reload that did nothing looked exactly like one that worked — which
is the worst possible failure for a script whose entire job is to prove the
running code is the deployed code.

It now subscribes, reads `slot_info` back, and verifies the slot actually
holds the module. It also detects the empty-slot case and says so rather than
claiming success. (8W8 and 6W6 carry the original; worth porting the fix.)

## Next

1. **Pick CW-78 in a slot on the Move** — then the remote panel at
   `move.local:7700` → Remote UI is checkable end to end.
2. **Listen.** `renders/rhythms/` has all twenty patterns and
   `renders/rhythm-montage.wav` is a tour. The voicing choices marked FITTED
   in the source are the ones most likely to want an ear.
4. **Screenshots** for the README, and a look at the remote panel in a browser
   — it is written and key-checked but has not been rendered.

## Open, and flagged in the source

- **L5**, the tambourine coil. Re-checked at 260 dpi: the parts list really
  does read `022-031  no.31 1R` with no inductance, where it gives 45 mH for
  no.30 and 700 mH for no.33. A web search turned up nothing better. Solved at
  62.7 mH to put the tank at 3.5 kHz; measuring a real one replaces exactly
  one number.
- The cowbell's two trimmed stages (Q529/Q530) are not traced; its two
  frequencies come from the factory table rather than the schematic.
- The noise lanes' RC-to-audible-decay mapping is fitted — the notes give no
  operating point for the gating transistors Q515–Q517.
- **The rhythm transcriptions are a visual reading of a 1979 scan**, not
  verified against hardware. Meter, voice selection, the bass drum line, the
  hat/cymbal/maracas line, the snare and the accent line are reliable; exact
  sixteenth placement of auxiliary percussion in the busiest Latin patterns is
  best-effort. `tools/rhythm_check <dir>` renders every pattern for checking
  by ear.
