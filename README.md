# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Phase 4 — Gate / Comp / Overdrive (success check)

Chain: **NoiseGate → Compressor → Overdrive → Gain**

### Automated (required — measures math, not vibes)

```bash
ctest --test-dir build --output-on-failure -R "noise_gate|compressor|overdrive"
```

| Test | What it proves |
|------|----------------|
| `noise_gate_test` | Loud signal passes; hum below threshold is attenuated; loud→quiet closes |
| `compressor_test` | Steady-state gain matches peak hard-knee formula; makeup scales; below thresh ≈ unity |
| `overdrive_test` | Odd waveshape; high-drive saturation; mix dry/wet; peak reduction |

### Live guitar check

```bash
./build/src/guitar_dsp_platform
```

1. Play muted strings / noise floor — gate should quiet hum (`gt -45` etc.).
2. Dig in hard — compressor should even dynamics (`ct -18`, `cr 4`, `cm 3`).
3. Raise drive — hear soft clipping (`d 8`, `dm 1`).
4. Bypass each stage: `bg` / `bc` / `bd` / `bn`.

Useful commands: `gt`, `ct`, `cr`, `cm`, `d`, `dm`, `g`, `bg`/`bc`/`bd`/`bn`, `s`, `h`, `q`.

## Status

Phase 4 — first real DSP modules (ready to test).
