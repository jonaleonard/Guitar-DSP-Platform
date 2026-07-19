# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

GLFW / Dear ImGui are fetched via CMake `FetchContent` on first configure.

## Run

```bash
./build/src/guitar_dsp_platform
```

Live audio defaults to **128-frame buffers** with `minimizeLatency` enabled (~2.7 ms buffer period @ 48 kHz). Cabinet convolution uses a **128-sample** partition (~2.7 ms algorithmic latency). If you hear xruns, raise the buffer in `AppContext.cpp`.

### Presets

Soft-mute crossfade on switch. Gain staging targets ~**35–50% peak hold** (Ambient was the headroom reference):

| Preset | Character |
|--------|-----------|
| Clean | Light glue + soft amp/cab |
| Blues | Mild edge-of-breakup, warm mids |
| Crunch | Mid-forward rock grind, dry |
| Metal | Tight boost → amp, moderate scoop, dry |
| Ambient | Chorus / delay / reverb wash |

### Visualizer (Phase 9)

Waveform + log-frequency spectrum drawn from the **same** post-DSP snapshot each frame (time-aligned). Peak hold meter with slow release; CLIP/HOT indicators.

### Profiler (Phase 10)

Per-effect µs / % of buffer budget (EMA) from `EffectGraph`.

### Phase 11 — Automated DSP tests

`phase11_test` covers sine EQ boost, delay/cab impulse, EQ frequency sweep, gate/compressor behavior, and graph latency regression.

```bash
ctest --test-dir build --output-on-failure -R phase11
```

## Status

Phases 1–11 complete.
