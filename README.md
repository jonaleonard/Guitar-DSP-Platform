# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```bash
./build/src/guitar_dsp_platform
```

### Low-latency audio path

| Stage | Setting |
|-------|---------|
| I/O buffer | **64 frames** (~1.3 ms @ 48 kHz) + `minimizeLatency` |
| Cabinet | **Hybrid zero-latency** convolution (direct FIR head + partitioned FFT tail) |
| Input trim | **−6 dB** studio pad before the chain |
| Output | Peak limiter ceiling **≈ −9 dBFS** (0.35 linear) |

If you hear xruns, raise `bufferFrames` in `AppContext.cpp` to 128.

### Studio gain staging

Presets are staged like a tracked guitar part: unsaturated OD dry is padded as drive rises, amp has inter-stage attenuation + soft ceiling, and a final limiter prevents DAC clipping. Aim for peak hold around **−12…−9 dBFS** (25–35%). Turning speaker volume down does not change digital peaks — the limiter/trim do.

### Presets

| Preset | Character |
|--------|-----------|
| Clean | Open, smooth amp/cab |
| Blues | Soft breakup, warm mids |
| Crunch | Mid-forward rock grind |
| Metal | Wet boost → tight amp scoop |
| Ambient | Controlled chorus/delay/reverb wash |

### Phases 9–11

Visualizer (synced wave/spectrum), profiler, and `phase11_test` automated DSP suite.

## Status

Phases 1–11 complete — low-latency hybrid cab + studio headroom path.
