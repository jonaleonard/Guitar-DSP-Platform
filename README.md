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

## Phase 1 — live wire (guitar test)

Plug in the Volt 1 and guitar, then:

```bash
./build/src/guitar_dsp_platform
```

You should hear unprocessed guitar through your headphones/monitors. Press Enter to stop.

Baseline to note: sample rate, buffer frames, and stream latency printed at startup.

## Offline harness (no hardware)

```bash
./build/tests/sine_to_wav_test build/sine_wire.wav
```

Writes a 1-second 440 Hz mono→stereo wire-through WAV and validates the parameter queue path.

## Status

Phase 1 — real-time audio engine core (ready to test).
