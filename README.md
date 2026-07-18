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
ctest --test-dir build
```

## Status

Phase 0 — environment and tooling setup (in progress).
