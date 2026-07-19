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

## Phase 2 — live Gain graph (guitar check)

1. In **Audio MIDI Setup**, set the Volt to **48 kHz**.
2. Prefer **headphones plugged into the Volt** (same-device I/O — avoids cross-device crackle).
3. Run:

```bash
./build/src/guitar_dsp_platform
```

4. Success check:
   - Hear guitar through the Gain effect
   - `g 0.3` lowers level, `g 1.0` restores
   - `b` toggles bypass (dry vs gained) with no crash
   - Watch for `[xrun]` lines — should stay at 0 if I/O is stable

Commands: `g <0..2>`, `b`, `s`, `h`, `q`

If you still hear crackling with headphones on the Volt, try buffer `1024` in `src/main.cpp` (`config.bufferFrames`).

## Offline harnesses

```bash
./build/tests/sine_to_wav_test build/sine_wire.wav
./build/tests/gain_graph_test build/gain_graph.wav
```

## Status

Phase 2 — effect interface & modular graph (ready to test).
