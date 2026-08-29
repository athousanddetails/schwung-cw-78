cmake_minimum_required(VERSION 3.15)
project(schwung-cw78 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
# No -ffast-math: it lets the compiler assume no NaN/Inf, which would remove
# the safety of the clamps on the realtime path. It also matters here for a
# second reason — the voice circuits solve loop gains and resonator
# coefficients from component values, and a compiler free to reassociate that
# arithmetic does not reproduce the factory alignment table voice_check holds
# them to.
set(CMAKE_C_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(CR78_SOURCES
    src/dsp/cr78_plugin.cpp
    src/dsp/cr78_engine.cpp
)

# The Move module: dlopen'd by Schwung's chain host as dsp.so
add_library(cr78 SHARED ${CR78_SOURCES})
target_include_directories(cr78 PRIVATE src src/dsp src/host)
target_link_libraries(cr78 PRIVATE m)
set_target_properties(cr78 PROPERTIES OUTPUT_NAME "dsp" PREFIX "" SUFFIX ".so")
# libstdc++ static: the Move ships 6.0.29 and we will not gamble on it.
target_link_options(cr78 PRIVATE -static-libstdc++ -static-libgcc)

# End-to-end loader test: dlopens the real dsp.so like the chain host does.
# Cross-compiled and run ON the Move.
add_executable(cr78_loadtest src/tools/loadtest.c)
target_include_directories(cr78_loadtest PRIVATE src src/dsp src/host)
target_link_libraries(cr78_loadtest PRIVATE dl m)

# CPU gate: renders each voice in isolation and reports realtime factor.
add_executable(cr78_bench src/tools/bench.cpp src/dsp/cr78_engine.cpp)
target_include_directories(cr78_bench PRIVATE src src/dsp src/host src/tools)
target_link_libraries(cr78_bench PRIVATE m)
target_link_options(cr78_bench PRIVATE -static-libstdc++ -static-libgcc)

# Offline renderer: same engine, writes WAVs so the kit can be heard and A/B'd
# against CR-78 recordings without a device in the loop.
add_executable(cr78_render src/tools/render.cpp src/dsp/cr78_engine.cpp)
target_include_directories(cr78_render PRIVATE src src/dsp src/host src/tools)
target_link_libraries(cr78_render PRIVATE m)
target_link_options(cr78_render PRIVATE -static-libstdc++ -static-libgcc)

# THE test: every lane against page 30 of the service notes — Roland's own
# factory alignment frequencies and decay times. Native only; it is a test.
add_executable(cr78_voice_check tools/voice_check.cpp src/dsp/cr78_engine.cpp)
target_include_directories(cr78_voice_check PRIVATE src src/dsp src/host)
target_link_libraries(cr78_voice_check PRIVATE m)

# The preset rhythm bank: checks the pattern table against the Style enum and
# renders every pattern. Native only.
add_executable(cr78_rhythm_check tools/rhythm_check.cpp src/dsp/cr78_engine.cpp)
target_include_directories(cr78_rhythm_check PRIVATE src src/dsp src/host)
target_link_libraries(cr78_rhythm_check PRIVATE m)

# Golden baseline: every lane at defaults, the demo pattern and every preset,
# hashed, so a change that was not meant cannot ship. Native only.
add_executable(cr78_golden_check tools/golden_check.cpp src/dsp/cr78_engine.cpp)
target_include_directories(cr78_golden_check PRIVATE src src/dsp src/host src/tools)
target_link_libraries(cr78_golden_check PRIVATE m)

# Kit balance: fits the per-lane trims to page 30's amplitude column and
# checks them. Native only — it is a design tool.
add_executable(cr78_kit_check tools/kit_check.cpp src/dsp/cr78_engine.cpp)
target_include_directories(cr78_kit_check PRIVATE src src/dsp src/host src/tools)
target_link_libraries(cr78_kit_check PRIVATE m)
