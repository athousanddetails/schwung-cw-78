#!/usr/bin/env bash
#
# Everything that can be checked without a Move plugged in.
#
#   ./test/all.sh
#
# What is NOT here, and cannot be: cr78_loadtest and cr78_bench are
# cross-compiled for aarch64 and run ON the device. scripts/build.sh builds
# them; deploy and run them there.
#
# THE CENTRAL TEST IS voice_check. 8W8's was a null test against sc808, which
# can only prove a transcription matches its source — it cannot tell you the
# source was right. This module has something better to check against:
# page 30 of the CR-78 service notes is Roland's own factory alignment table,
# giving the frequency and decay time every voice is trimmed to. The models
# are built from the schematic; the table was measured off hardware. The two
# were arrived at independently, so agreement between them means something.
#
# GPL-3.0.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
cd "$ROOT"
mkdir -p build-native

CXX="${CXX:-c++}"
CC_="${CC:-cc}"
FLAGS="-std=c++14 -O2 -Isrc/dsp -Isrc/host -Isrc/tools"
fails=0
skips=0
step()    { printf '\n\033[1m== %s\033[0m\n' "$1"; }
verdict() { if [ "$1" -eq 0 ]; then echo "   PASS"; else echo "   FAIL"; fails=$((fails+1)); fi; }
# A step that could not run is NOT a step that passed. Counted separately and
# reported at the end, because a green run on a machine missing half the
# toolchain used to look exactly like a green run on the build host.
skip()    { echo "   SKIP ($1)"; skips=$((skips+1)); }
have()    { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------------------
step "generator is in sync with the checked-in header"
# gen_params.py is the single source of truth for the pot table, chain_params
# and the page hierarchy. If someone edits the generated header by hand,
# everything downstream silently disagrees with it.
if ! have python3; then
  skip "no python3"
else
  cp src/dsp/cr78_params.h build-native/params.before
  python3 scripts/gen_params.py >/dev/null
  if diff -q build-native/params.before src/dsp/cr78_params.h >/dev/null
  then verdict 0; else
    echo "   cr78_params.h differs from what gen_params.py emits"
    verdict 1
  fi
fi

# ---------------------------------------------------------------------------
step "the engine and the plugin compile clean"
# Warning-free AND actually built. Grepping a log for "warning" says PASS on a
# host where the compiler was never found and nothing was produced at all, so
# the exit code is checked first and the artefact is checked after.
log=build-native/compile.log
: >"$log"
rc=0
$CXX $FLAGS -Wall -Wextra -c src/dsp/cr78_engine.cpp -o build-native/engine.o >>"$log" 2>&1 || rc=1
$CXX $FLAGS -Wall -Wextra -c src/dsp/cr78_plugin.cpp -o build-native/plugin.o >>"$log" 2>&1 || rc=1
$CC_ -O2 -Isrc/dsp -Isrc/host -Isrc/tools -Wall -Wextra -c src/tools/loadtest.c \
     -o build-native/loadtest.o >>"$log" 2>&1 || rc=1
warns=$(grep -ciE '\bwarning\b' "$log" || true)
if [ "$rc" -ne 0 ]; then
  echo "   compilation failed:"; sed -n '1,25p' "$log"; verdict 1
elif [ ! -s build-native/engine.o ] || [ ! -s build-native/plugin.o ]; then
  echo "   compiler produced no object files"; verdict 1
elif [ "$warns" -ne 0 ]; then
  echo "   $warns warning(s):"; grep -iE '\bwarning\b' "$log" | head -10; verdict 1
else
  verdict 0
fi

# ---------------------------------------------------------------------------
step "every voice matches the CR-78 factory alignment table (page 30)"
if $CXX $FLAGS -o build-native/voice_check tools/voice_check.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/voice_check
  verdict $?
else
  skip "voice_check did not build"
fi

# ---------------------------------------------------------------------------
step "kit balance and the level anchor"
if $CXX $FLAGS -o build-native/kit_check tools/kit_check.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/kit_check
  verdict $?
else
  skip "kit_check did not build"
fi

# ---------------------------------------------------------------------------
step "the preset rhythm bank is consistent"
# The pattern table in cr78_rhythms.h and the Style enum in gen_params.py are
# two hand-written lists that must stay in the same order. Nothing enforces
# that but this check: get it wrong and picking "Bossa" plays a rock pattern,
# silently, with no error anywhere.
if $CXX $FLAGS -o build-native/rhythm_check tools/rhythm_check.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/rhythm_check | tail -5
  verdict ${PIPESTATUS[0]:-$?}
else
  skip "rhythm_check did not build"
fi

# ---------------------------------------------------------------------------
step "the sends are silent at zero"
# Load-bearing, and not a matter of taste: with both send amounts down the FX
# ticks are fed exactly 0.0 from silent state and must return exactly 0.0, so
# putting them in the mix cannot change a single sample of the kit. It is NOT
# a bypass — the ticks still run, because a send turned down while a tail is
# ringing has to let that tail finish.
cat > build-native/sendzero.cpp <<'EOF'
#include <math.h>
#include <stdio.h>
#include <vector>
#include "cr78_engine.h"
#include "demo_pattern.h"
int main(void)
{
    const float sr = 44100.0f;
    const size_t n = (size_t)(sr * 8.0f);
    std::vector<float> a(n, 0.0f), b(n, 0.0f);

    cr78_engine_t *e = cr78_create(sr);
    cr78_render_demo(e, a.data(), (int)n, sr, 2.0);
    cr78_destroy(e);

    /* Same again, with the reverb and delay wound right up. Every send amount
     * is still zero, so nothing should reach either bus. */
    e = cr78_create(sr);
    cr78_set_param(e, "rev_level", "127");
    cr78_set_param(e, "dly_level", "127");
    cr78_set_param(e, "rev_decay", "127");
    cr78_set_param(e, "dly_fdbk",  "127");
    cr78_render_demo(e, b.data(), (int)n, sr, 2.0);
    cr78_destroy(e);

    size_t diff = 0;
    for(size_t i = 0; i < n; ++i) if(a[i] != b[i]) ++diff;
    if(diff) { printf("   %zu of %zu samples differ\n", diff, n); return 1; }
    printf("   %zu samples bit-identical with the buses wound up\n", n);
    return 0;
}
EOF
if $CXX $FLAGS -o build-native/sendzero build-native/sendzero.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/sendzero
  verdict $?
else
  skip "sendzero did not build"
fi

# ---------------------------------------------------------------------------
step "the hat choke group"
# A MOD, so it gets a test proving both halves: that it works when asked, and
# that OFF is genuinely inert. The second half matters more — the whole claim
# of this module is that a fresh patch is the machine.
cat > build-native/choke.cpp <<'EOF'
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "cr78_engine.h"

static const float SR = 44100.0f;
static void set(cr78_engine_t *e, const char *k, const char *v)
{ cr78_set_param(e, k, v); }

/*
 * Ring `first` for 120 ms, hit `second`, and measure what is left of the
 * FIRST lane's tail.
 *
 * The second lane is silenced with its LEVEL, not with a mute. That is not a
 * detail: cr78_trigger returns early on a muted lane, so a muted lane never
 * reaches the choke logic and never chokes anything — which is correct (a
 * mute means the lane does not play) and made the first version of this test
 * report no choking at all.
 */
static const char *kLevelKey[CR78_NUM_VOICES] = {
    "bd_level","sd_level","rs_level","hh_level","cy_level","ma_level",
    "cl_level","hb_level","lb_level","lc_level","cb_level","tb_level",
    "gu_level","mb_level" };

static double tail_after(const char *choke, int first, int second)
{
    cr78_engine_t *e = cr78_create(SR);
    set(e, "hat_choke", choke);
    /* long tails so there is something to cut */
    set(e, "tb_decay", "110");
    set(e, "ma_decay", "110");
    set(e, "hh_decay", "110");
    if(second != first) set(e, kLevelKey[second], "0");   /* silent, still triggers */
    cr78_trigger(e, first, 110);
    std::vector<float> b((size_t)(SR * 0.12f), 0.0f);
    cr78_render(e, b.data(), (int)b.size());
    cr78_trigger(e, second, 110);          /* muted, but still chokes */
    std::vector<float> t((size_t)(SR * 0.35f), 0.0f);
    cr78_render(e, t.data(), (int)t.size());
    cr78_destroy(e);
    double s = 0.0;
    for(float x : t) s += (double)x * x;
    return sqrt(s / (double)t.size());
}

int main(void)
{
    int bad = 0;
    struct C { const char *mode; int a, b; const char *an, *bn; int shouldCut; };
    const C cases[] = {
        { "0", CR78_TB, CR78_MA, "TB", "MA", 0 },   /* Off   — no cut     */
        { "1", CR78_TB, CR78_MA, "TB", "MA", 1 },   /* MA/TB — cuts       */
        { "1", CR78_TB, CR78_HH, "TB", "HH", 0 },   /* MA/TB — HH is out  */
        { "2", CR78_TB, CR78_HH, "TB", "HH", 1 },   /* All3  — cuts       */
        { "2", CR78_HH, CR78_MA, "HH", "MA", 1 },   /* All3  — cuts       */
        { "2", CR78_BD, CR78_MA, "BD", "MA", 0 },   /* All3  — kick unaffected */
    };
    const double open = tail_after("0", CR78_TB, CR78_MA);
    for(size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i)
    {
        const C &c = cases[i];
        const double ref = tail_after("0", c.a, c.b);
        const double got = tail_after(c.mode, c.a, c.b);
        const double ratio = ref > 1e-9 ? got / ref : 1.0;
        const int cut = ratio < 0.25;
        const char *mn = c.mode[0]=='0' ? "Off  " : (c.mode[0]=='1' ? "MA/TB" : "All3 ");
        if(cut != c.shouldCut)
        {
            printf("   FAIL  %s: %s then %s -> tail %.1f%% of open, expected %s\n",
                   mn, c.an, c.bn, 100.0 * ratio, c.shouldCut ? "cut" : "kept");
            ++bad;
        }
        else
            printf("   ok    %s: %s then %s -> %s (tail %.0f%% of open)\n",
                   mn, c.an, c.bn, c.shouldCut ? "cut " : "kept", 100.0 * ratio);
    }
    (void)open;
    /* A lane must never choke ITSELF on a retrigger. */
    {
        const double ref = tail_after("0", CR78_MA, CR78_MA);
        const double got = tail_after("2", CR78_MA, CR78_MA);
        const double ratio = ref > 1e-9 ? got / ref : 1.0;
        if(ratio < 0.9)
        { printf("   FAIL  All3: MA retrigger chokes itself (tail %.0f%%)\n",
                 100.0 * ratio); ++bad; }
        else
            printf("   ok    All3: a retrigger does not choke its own lane\n");
    }
    return bad ? 1 : 0;
}
EOF
if $CXX $FLAGS -o build-native/choke build-native/choke.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/choke
  verdict $?
else
  skip "choke test did not build"
fi

# ---------------------------------------------------------------------------
step "state round-trips"
cat > build-native/stateio.cpp <<'EOF'
#include <stdio.h>
#include <string.h>
#include <vector>
#include "cr78_engine.h"
#include "cr78_params.h"
int main(void)
{
    cr78_engine_t *a = cr78_create(44100.0f);
    /* Move something on every pot and every enum, so a table that silently
     * shrank or shifted cannot round-trip by accident. */
    for(int i = 0; i < CR78_NUM_POTS; ++i)
    {
        char v[8]; snprintf(v, sizeof v, "%d", (i * 7 + 3) % 128);
        cr78_set_param(a, g_cr78_pots[i].key, v);
    }
    for(int i = 0; i < CR78_NUM_ENUMS; ++i)
    {
        char v[8]; snprintf(v, sizeof v, "%d", i % g_cr78_enums[i].count);
        cr78_set_param(a, g_cr78_enums[i].key, v);
    }
    cr78_set_mutes(a, 0x1555u);

    std::vector<char> blob(65536, 0);
    cr78_serialize(a, blob.data(), (int)blob.size());

    cr78_engine_t *b = cr78_create(44100.0f);
    cr78_deserialize(b, blob.data());

    int bad = 0;
    char x[32], y[32];
    for(int i = 0; i < CR78_NUM_POTS; ++i)
    {
        cr78_get_param(a, g_cr78_pots[i].key, x, sizeof x);
        cr78_get_param(b, g_cr78_pots[i].key, y, sizeof y);
        if(strcmp(x, y)) { printf("   pot %s: %s != %s\n", g_cr78_pots[i].key, x, y); ++bad; }
    }
    for(int i = 0; i < CR78_NUM_ENUMS; ++i)
    {
        cr78_get_param(a, g_cr78_enums[i].key, x, sizeof x);
        cr78_get_param(b, g_cr78_enums[i].key, y, sizeof y);
        if(strcmp(x, y)) { printf("   enum %s: %s != %s\n", g_cr78_enums[i].key, x, y); ++bad; }
    }
    if(cr78_get_mutes(a) != cr78_get_mutes(b))
    { printf("   mutes differ\n"); ++bad; }
    cr78_destroy(a); cr78_destroy(b);
    if(bad) return 1;
    printf("   %d pots, %d enums and the mute mask survive a round trip\n",
           CR78_NUM_POTS, CR78_NUM_ENUMS);
    return 0;
}
EOF
if $CXX $FLAGS -o build-native/stateio build-native/stateio.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/stateio
  verdict $?
else
  skip "stateio did not build"
fi

# ---------------------------------------------------------------------------
step "the voices contract holds in both note maps"
# Schwung 0.13: pad_layout plus a note per voice, asserted against the blobs
# the header actually ships rather than what the generator meant. Both failures
# this guards against happened in 9W9 and neither raised an error: a positional
# note map, and a level_of[] naming pages that are not emitted — which stopped
# four of eleven voices following the pad, silently, for months.
if have python3; then
  python3 tools/voices_check.py >>"$log" 2>&1
  verdict $?
else
  skip "no python3"
fi

# ---------------------------------------------------------------------------
step "every control actually does something"
# The gap the rest of this suite left open: a knob wired to nothing passes
# every other check here. It resolves, stores, round-trips and draws — it just
# never reaches the voice. 8W8 shipped exactly that for months. Each control is
# rendered at both ends of its range, in the context where it is SUPPOSED to
# work (a distortion type needs Drive open; a bus control needs a send), and
# the two renders must differ.
if $CXX $FLAGS -o build-native/knob_check tools/knob_check.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/knob_check >>"$log" 2>&1
  verdict $?
else
  skip "knob_check did not build"
fi

# ---------------------------------------------------------------------------
step "the kit is bit-identical to the golden baseline"
# Every lane at factory defaults, the demo pattern and all twenty presets,
# hashed. This is what makes "the mod did not touch the original sound" a
# claim with a test behind it. A deliberate change re-blesses; anything else
# is a regression.
if $CXX $FLAGS -o build-native/golden_check tools/golden_check.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  build-native/golden_check test/golden/default.txt
  verdict $?
else
  skip "golden_check did not build"
fi

# ---------------------------------------------------------------------------
step "the kit renders"
if $CXX $FLAGS -o build-native/render src/tools/render.cpp \
        src/dsp/cr78_engine.cpp >>"$log" 2>&1; then
  mkdir -p build-native/wav
  build-native/render build-native/wav >/dev/null
  n=$(ls build-native/wav/*.wav 2>/dev/null | wc -l | tr -d ' ')
  # 14 lanes plus the pattern.
  if [ "$n" -eq 15 ]; then echo "   15 WAVs in build-native/wav"; verdict 0
  else echo "   expected 15 WAVs, got $n"; verdict 1; fi
else
  skip "render did not build"
fi

# ---------------------------------------------------------------------------
printf '\n\033[1m== summary\033[0m\n'
if [ "$skips" -ne 0 ]; then echo "   $skips step(s) SKIPPED — this is not a full pass"; fi
if [ "$fails" -ne 0 ]; then echo "   $fails step(s) FAILED"; exit 1; fi
if [ "$skips" -ne 0 ]; then echo "   everything that ran, passed"; exit 0; fi
echo "   all steps passed"
