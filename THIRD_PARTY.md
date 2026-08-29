# Third-party material in CW-78

CW-78 is GPL-3.0. This file records what came from where, because "modelled from
the service notes" and "copied from another project" are different claims and
the difference matters.

## The voice DSP: nothing third-party

Every file under `src/dsp/cr78_*_circuit.h` and `src/dsp/cr78_circuit.h` was
written for this project from the Roland CR-78 service notes. **No voice code
is shared with 8W8, 9W9, 6W6, sc808, or any other drum machine model.** CW-78
contains no TR-808 or TR-909 circuitry.

Where a generic analogue building block appears in more than one of these
projects — a bridged-T in a feedback loop, an RC relaxation oscillator — that
is because the hardware designs share the idiom, not because the code was
copied. The component values, the tunings, the decays and the voicing here are
the CR-78's own.

## The service notes

**Roland CR-78 Service Notes**, June 20 1979 (printed Japan, July 1982, E-2).
34 pages, retrieved from
[synthfool.com](https://synthfool.com/docs/Roland/Roland_CR78_Service_Notes_BW_medium.pdf).
Not vendored in this repository — Roland holds the copyright in the scan — but
every derived number in the source cites the page it came from, so the claims
can be checked against the document directly.

Roland Corporation holds the copyright in this document. It is included as the
reference the models were built from, so that any claim in the source can be
checked against the page it came from. Every derived number in the code cites
its components and its page.

Facts read out of it — component values, topologies, and the factory alignment
figures on page 30 — are not themselves copyrightable, and the models built
from them are original work.

## The module shell

Ported from **[9W9](https://github.com/athousanddetails/schwung-9W9)** by way
of **[6W6](https://github.com/athousanddetails/schwung-6W6)** and
**[8W8](https://github.com/athousanddetails/schwung-8W8)**, all GPL-3.0 and by
the same author. Deliberately kept identical so a control means the same thing
on every kit in the family:

| File | What was taken |
|---|---|
| `src/dsp/cr78_shape.h` | the seven distortion characters, verbatim |
| `src/dsp/cr78_fx.h` | the reverb, the tempo-synced delay and the one-knob bus glue, verbatim — including three fixed bugs that are called out at each site so nobody reintroduces them |
| `src/ui_chain.js` | the on-device editor: pad gestures, page navigation, the Main-page lock |
| `scripts/gen_params.py` | the generator's machinery. **None of its defaults** — every one here is a CR-78 factory alignment figure |
| `scripts/build.sh`, `scripts/deploy.sh`, `scripts/Dockerfile`, `cmake/` | the cross-build |
| `src/tools/loadtest.c`, `bench.cpp`, `render.cpp` | the device tools, adapted to the fourteen-lane roster |
| `src/host/plugin_api_v1.h` | Schwung's host API header |

`tools/kit_check.cpp` and `tools/voice_check.cpp` were written for this
project, though kit_check's structure follows 8W8's and its header says what it
inherited — including the lesson about fitting the absolute level to a real
pattern rather than a synthetic downbeat.

## Platform

- **[Schwung](https://github.com/charlesvestal/schwung)** by Charles Vestal and
  contributors — the module platform and the shared `param_pages` knob grid.
- **[Movy](https://github.com/DimaDake/schwung-movy)** by DimaDake — the page
  model `movy_config.json` targets.

## Trademarks

CR-78 and CompuRhythm are trademarks of Roland Corporation. Ableton and Move
are trademarks of Ableton AG. Neither company is affiliated with this project,
and both names are used only to describe what the software does and what it
runs on.
