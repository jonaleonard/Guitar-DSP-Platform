# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

GLFW and Dear ImGui are fetched automatically via CMake `FetchContent` on first configure.

## Run

```bash
./build/src/guitar_dsp_platform
```

### Presets

Soft-mute crossfade on every switch. Factory character:

| Preset | Intent |
|--------|--------|
| **Clean** | Light glue + soft amp/cab — smooth, no grit |
| **Blues** | Mild edge-of-breakup, warm mids, tiny room |
| **Crunch** | Mid-forward rock grind, controlled level, dry |
| **Metal** | Tight boost → amp, moderate scoop, dry cab |
| **Ambient** | Chorus/delay/reverb wash with a bit more punch |

### Phase 9 — Visualizer

Waveform + spectrum (Hann-windowed FFT on GUI thread) and a decaying clip indicator. Audio thread only writes a lock-free SPSC ring.

### Phase 10 — Profiler

Per-effect µs / % of buffer budget table (EMA), plus total. Timing is taken inside `EffectGraph::process` and published via atomics.

All GUI parameter / bypass / preset changes still go only through the lock-free graph command queue.

## Status

Phases 8–10 ready to test.
