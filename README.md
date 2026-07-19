# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Project layout

```
src/           Application and engine source
include/       Public headers
tests/         Unit and integration tests
third_party/   Vendored dependencies
```

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Phase 3 — parameter smoothing (success check)

Gain changes are linearly ramped (~20 ms) via shared `SmoothedValue` — no zipper clicks.

### Automated check (no guitar)

```bash
ctest --test-dir build --output-on-failure -R gain_smoothing
# or:
./build/tests/gain_smoothing_test build/gain_smooth.wav
```

This flips gain 0↔1 every 10 ms for 1 s and fails if sample-to-sample jumps look like zipper noise.

### Live guitar check

1. Volt at **48 kHz** in Audio MIDI Setup; guitar → Volt; Mac speakers for monitor.
2. Run:

```bash
./build/src/guitar_dsp_platform
```

3. Hold a sustained note/chord, then type:

```text
a
```

4. For 5 seconds gain flips 0↔1 every 10 ms. **Pass:** level pulses smoothly, **no tick/zipper/click** on each flip. (Volume will pump — that’s expected.)
5. Optional: `g 0` / `g 1` manually — transitions should be smooth, not stepped.

Commands: `g <0..2>`, `a`, `b`, `s`, `h`, `q`

## Offline harnesses

```bash
./build/tests/sine_to_wav_test build/sine_wire.wav
./build/tests/gain_graph_test build/gain_graph.wav
./build/tests/gain_smoothing_test build/gain_smooth.wav
```

## Status

Phase 3 — parameter smoothing (ready to test).
