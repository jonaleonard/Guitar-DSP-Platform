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
| Cabinet | **Hybrid zero-latency** convolution (FIR head + partitioned FFT tail) |
| Input trim | **−3 dB** pad before the chain |
| Output | Peak limiter ceiling **≈ −1 dBFS** (0.89), hard-capped at ±1 |

If you hear xruns, raise `bufferFrames` in `AppContext.cpp` to 128.

### Listening level / metering

Scopes and the CLIP light show **post-limiter output** (what you hear / send to the DAC).
- Factory presets are staged for amp/pedal loudness in headphones or monitors
- Healthy playing sits around **−6…−1 dBFS**
- CLIP means you’re near the DAC ceiling — use **Output Gain** for volume, not by slamming the chain into red

### Tone engine

- **Overdrive** — Tube Screamer–style mid focus (tight boost into a high-gain amp)
- **Amp sim** — drive-adaptive voicing: warm at low gain, opens into 5150-style tightness at high drive
- **Cabinet** — synthetic IR with peak + frequency-response normalization (no runaway resonances)
- Soft-knee compressor, almost-full cab wetness (less dry DI bleed)

### Presets

| Preset | Character |
|--------|-----------|
| Clean | Open mic’d-amp feel, light room |
| Blues | Edge-of-breakup, warm mids |
| Crunch | Mid-forward rock grind |
| Metal | **Mustang Metal 2000 / 5150**: fast gate → TS boost → mid scoop → high-gain amp + presence |
| Ambient | Soft chorus / delay / reverb wash |

### Phases 9–11

Visualizer (synced wave/spectrum), profiler, and `phase11_test` automated DSP suite.

## Status

Phases 1–11 complete — low-latency hybrid cab, amp-loud listening path, Metal 2000–style high-gain preset.
