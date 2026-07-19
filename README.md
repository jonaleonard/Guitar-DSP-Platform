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

## Phase 8 — GUI

```bash
./build/src/guitar_dsp_platform
```

Opens an ImGui window (GUI thread separate from audio). All parameter / bypass / preset changes go through the lock-free graph command queue — the GUI never touches DSP objects on the audio thread.

| UI | Action |
|----|--------|
| Preset buttons | Soft-mute crossfade load (Clean → Blues → Crunch → Metal → Ambient) |
| Effect panels | Enable toggle + sliders; editing a slider auto-enables that effect |
| Top bar | Buffer latency (ms), sample rate, rough DSP load % |

Factory preset character:

- **Clean** — dry wire (gain only)
- **Blues** — soft OD/amp, tiny room; no gate, no delay
- **Crunch** — mid-forward rock grind + cab; gate/delay/reverb off
- **Metal** — high-gain scoop + cab; very light gate; delay/reverb off
- **Ambient** — chorus / delay / reverb wash

JSON copies are written to `presets/*.json` at startup.

### Automated

```bash
ctest --test-dir build --output-on-failure -R preset
```

## Status

Phase 8 — Dear ImGui control surface (ready to test).
