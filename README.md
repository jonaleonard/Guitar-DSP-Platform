# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Phase 7 — Presets (easy switching)

Starts on **Clean** (dry wire). Soft-mute crossfade on every preset change (no clicks).

### Live guitar — switch presets while playing

```bash
./build/src/guitar_dsp_platform
```

| Command | Action |
|---------|--------|
| `p` | list presets |
| `p 0` … `p 4` | load by index |
| `p blues` | load by name |
| `n` or `]` | **next** preset |
| `b` or `[` | **previous** preset |
| `clean` | jump to Clean |

Factory presets: **Clean → Blues → Crunch → Metal → Ambient**

JSON copies are written to `presets/*.json` at startup (editable).

### Automated

```bash
ctest --test-dir build --output-on-failure -R preset
```

## Status

Phase 7 — preset system (ready to test).
