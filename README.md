# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Zipper fix (Phase 3 follow-up)

Gain smoothing is now **exponential (one-pole)** with an **~80 ms** time constant (was linear ~20 ms). Rapid `a` automation should glide without ticks.

Re-check live: hold a note → type `a` → level pumps smoothly, no zipper.

## Phase 5 — EQ / Amp / Cab (success check)

Chain: **Gate → Comp → Drive → EQ → Amp → Cab(IR) → Gain**

### Automated

```bash
ctest --test-dir build --output-on-failure -R "equalizer|amp_sim|cabinet|gain_smoothing"
```

| Test | Proves |
|------|--------|
| `equalizer_test` | Mid +12 dB boost measurable at 1 kHz |
| `amp_sim_test` | Pre-EQ → waveshape → tone stack produces saturated output |
| `cabinet_test` | Impulse through partitioned FFT conv ≈ loaded IR; cab processes tone |
| `gain_smoothing_test` | Rapid gain flips stay below zipper threshold |

### Live guitar

```bash
./build/src/guitar_dsp_platform
```

1. Play — should sound like amp+cab, not just raw distortion.
2. Bypass cab: `bb` — tone gets harsher/fizzier (IR removed).
3. Bypass amp: `ba` — cleaner/less amp-like.
4. Tweak EQ: `el 4`, `em -3`, `eh 3`.
5. Amp: `ad 8`, `am 0.7`, `ab 3`.
6. Cab mix: `cx 0` (dry) vs `cx 1` (full IR).
7. Zipper check: hold note → `a`.

Type `h` for commands.

## Status

Phase 5 — EQ, amp sim, cabinet IR convolution (ready to test).
