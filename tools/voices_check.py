#!/usr/bin/env python3
"""voices_check — Schwung 0.13's voices contract, asserted against the header.

Reads the two ui_pages blobs straight out of src/dsp/cr78_params.h and the
level_of[] table out of the plugin, so what is checked is what ships rather
than what the generator meant.

The two failures this exists for both happened in 9W9 and neither raised an
error anywhere: a positional note map that mislabelled the voices whose nav
order and trigger order disagree, and a level_of[] naming pages the generator
does not emit — which silently stopped four of eleven voices following the pad,
for months. Neither is visible by reading the code; both are one assertion.
"""
import json, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HDR  = (ROOT / "src/dsp/cr78_params.h").read_text()
PLUG = (ROOT / "src/dsp/cr78_plugin.cpp").read_text()

def blob(name):
    """Pull a C string-literal array out of the header and un-escape it."""
    m = re.search(r'static const char %s\[\] =\n(.*?);\n' % name, HDR, re.S)
    if not m: sys.exit(f"FAIL  {name} is not in the header")
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    raw = "".join(parts)
    return json.loads(raw.encode().decode("unicode_escape"))

fails = []
def check(ok, msg):
    print(("  ok    " if ok else "  FAIL  ") + msg)
    if not ok: fails.append(msg)

rack = blob("cr78_ui_pages_json")
gm   = blob("cr78_ui_pages_gm_json")

# the voices, in trigger order, from the plugin's own table
m = re.search(r"kLevelOf\[CR78_NUM_VOICES\] = \{(.*?)\};", PLUG, re.S)
if not m: sys.exit("FAIL  kLevelOf is not in the plugin")
level_of = re.findall(r'"([a-z0-9_]+)"', m.group(1))

NON_VOICE = {"rhy", "rev", "dly", "root"}

for tag, h in (("rack", rack), ("gm", gm)):
    check(h.get("pad_layout") == "drums", f"{tag}: pad_layout is drums")
    check(h.get("focus_param") == "ui_focus_level",
          f"{tag}: focus_param names the key the plugin publishes")
    lv = h["levels"]
    voiced = {k: v["note"] for k, v in lv.items() if "note" in v}
    check(len(voiced) == len(level_of),
          f"{tag}: {len(voiced)} levels carry a note, for {len(level_of)} voices")
    check(set(voiced) == set(level_of),
          f"{tag}: the noted levels are exactly the voices")
    silent = sorted(k for k in NON_VOICE if k in lv and "note" in lv[k])
    check(not silent,
          f"{tag}: no note on a level that makes no sound"
          + (f" — but {silent} have one" if silent else ""))
    check(len(set(voiced.values())) == len(voiced),
          f"{tag}: no two voices claim the same note")
    for pid in level_of:
        if pid not in lv:
            check(False, f"{tag}: level_of names '{pid}', which is not a level")

# the drum rack is 36 + trigger index, with no transpositions
want = {pid: 36 + i for i, pid in enumerate(level_of)}
got  = {k: v["note"] for k, v in rack["levels"].items() if "note" in v}
check(got == want, "rack: every voice sits at 36 + its trigger index")

# GM notes are real GM
GM_TRUTH = {"bd": 36, "sd": 38, "hh": 42, "cy": 49, "ma": 70, "cl": 75,
            "hb": 60, "lb": 61, "cb": 56, "tb": 54}
gmn = {k: v["note"] for k, v in gm["levels"].items() if "note" in v}
bad = {k: (gmn.get(k), n) for k, n in GM_TRUTH.items() if gmn.get(k) != n}
check(not bad, "gm: the notes with a real GM meaning use it"
      + (f" — {bad}" if bad else ""))
check(all(0 <= n <= 127 for n in gmn.values()), "gm: every note is in range")

# the pages a voice declares must still be pages
for tag, h in (("rack", rack), ("gm", gm)):
    for pid in level_of:
        v = h["levels"].get(pid, {})
        check(bool(v.get("knobs")), f"{tag}: voice '{pid}' is a real page")

print()
if fails:
    print(f"{len(fails)} problem(s) with the voices contract.")
    sys.exit(1)
print("OK: the voices contract holds in both note maps.")
