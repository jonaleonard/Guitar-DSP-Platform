# Guitar DSP Platform

A real-time modular guitar-processing application for macOS, written in C++17. It provides a fixed ten-stage pedalboard/amp chain, low-latency RtAudio I/O, a Dear ImGui control surface, cabinet convolution, factory presets, signal visualization, and per-effect profiling.

The current hardware target is a Universal Audio Volt interface (developed with a Volt 1), although any CoreAudio device exposed through RtAudio can be selected.

## Current capabilities

- 48 kHz, 64-frame duplex audio by default (about 1.33 ms per callback)
- Mono guitar input with duplicated stereo output
- Lock-free GUI-to-audio command delivery
- Smoothed DSP parameters and soft-muted preset changes
- Ten serial effects: gate, compressor, overdrive, EQ, amp, cabinet, chorus, delay, reverb, and output gain
- Hybrid zero-latency cabinet convolution
- Five JSON factory presets: Clean, Blues, Crunch, Metal, and Ambient
- Post-limiter waveform, spectrum, peak, and clipping visualization
- Per-effect and total callback-time profiling
- Fourteen standalone DSP/integration tests registered with CTest

## Signal flow

The application uses a fixed live signal path:

```text
CoreAudio / RtAudio input
        |
        v
First input channel -> -3.1 dB input trim
        |
        v
Noise Gate -> Compressor -> Overdrive -> Equalizer -> Amp Simulator
        |
        v
Cabinet -> Chorus -> Delay -> Reverb -> Output Gain
        |
        v
Preset mute ramp -> Peak limiter -> Meter/visualizer ring
        |
        v
Mono duplicated to output channels 1 and 2
```

The callback processes into a fixed `4096`-sample mono buffer. Additional output channels are cleared. The engine can open different input and output devices, but using devices with separate clocks may cause drift or crackling because the application does not perform asynchronous sample-rate conversion.

## Audio configuration

Live defaults are defined in `src/app/AppContext.cpp`:

- Sample rate: `48000 Hz`
- Requested buffer size: `64 frames`
- Input channels: `1`
- Output channels: `2`
- Input device match: case-insensitive name containing `Volt`
- Output device: CoreAudio/RtAudio default output
- RtAudio flags: `RTAUDIO_MINIMIZE_LATENCY` and `RTAUDIO_SCHEDULE_REALTIME`
- Low-latency buffer count: `2`; fallback buffer count: `4`
- Input trim: `0.7` linear (about `-3.1 dB`)
- Output limiter: `0.89` ceiling (about `-1.0 dBFS`), `80 ms` release

If the low-latency stream fails to open, the engine retries without the minimize-latency flag. If xruns occur, increase `bufferFrames` to `128`.

## Runtime architecture

`src/main.cpp` creates one `app::AppContext`, calls `setupAudioGraph()`, and then runs the GUI on the main thread.

### Audio engine

`audio::AudioEngine` owns RtAudio, stream setup/teardown, device resolution, xrun counters, and the process callback. The callback:

1. Accounts for input overflow and output underflow.
2. Silences output if no input buffer is available.
3. Drains pending engine parameter messages.
4. Copies the first interleaved input channel into the fixed mono working buffer.
5. Applies the input trim and processes the effect graph.
6. Applies the preset-transition mute ramp.
7. Runs the final peak limiter and absolute `[-1, +1]` safety clamp.
8. Writes post-limiter audio to the visualization ring.
9. Copies mono output to the first two output channels.
10. Publishes callback duration and invocation count through atomics.

### Effect graph and real-time control

`dsp::EffectGraph` owns up to `32` effects and processes active effects serially. The live application populates ten fixed slots, while the graph itself also supports insert, remove, swap, bypass, and parameter commands.

Control changes cross into the audio thread through `dsp::GraphCommandQueue`, a cache-line-aligned SPSC ring with `256` entries (`255` usable). Commands are consumed at the beginning of an audio block. Removed effects are retired on the audio thread and reclaimed later on the control thread so destruction does not occur in the callback.

Effect parameters use `dsp::SmoothedValue`, an allocation-free exponential one-pole ramp. The general smoothing time is `50 ms`; modules use shorter or longer values where appropriate:

- Gain: `80 ms`
- Overdrive: `15–18 ms`
- Cabinet mix/level: `12 ms`

The DSP processing contract prohibits allocation, locks, logging, and exceptions in `process()`.

## DSP modules

All effects process mono, in-place `float` buffers.

### Noise gate

A peak-envelope gate with threshold, attack, release, and closed-range controls.

- Threshold: `-80..0 dB`
- Attack: `>= 0.1 ms`
- Release: `>= 1 ms`
- Closed attenuation: `-100..0 dB`
- No hysteresis or hold stage

The Metal preset uses a fast, deep gate for palm-muted stops.

### Compressor

A peak detector operating in dB with a `6 dB` quadratic soft knee.

- Threshold: `-60..0 dB`
- Ratio: `>= 1:1`
- Attack: `>= 0.1 ms`
- Release: `>= 1 ms`
- Makeup: `-24..+24 dB`

Envelope and gain-reduction transitions use separate attack/release behavior.

### Overdrive

A Tube Screamer-inspired front end intended both as an audible overdrive and as a tight boost into the amp:

```text
320 Hz high-pass
-> 720 Hz, +5 dB, Q 0.85 mid boost
-> 6.5 kHz low-pass
-> asymmetric normalized tanh clipper
-> drive-dependent level compensation
-> 5.2 kHz low-pass
```

The nonlinear transfer adds a small quadratic term before `tanh`:

```text
y = tanh((x + 0.08*x^2) * drive) / tanh(drive)
```

Controls are drive (`1..25`), dry/wet mix (`0..1`), and output (`0..2`).

### Equalizer

A three-band RBJ biquad EQ:

- Low shelf: gain `±18 dB`, frequency `40..500 Hz`
- Mid peak: gain `±18 dB`, frequency `200..4000 Hz`, Q `0.2..8`
- High shelf: gain `±18 dB`, frequency `1500..12000 Hz`

The internal `Biquad` implementation also supplies low-pass and high-pass filters to other effects.

### Amp simulator

The amp uses drive-adaptive voicing so low-gain presets stay warm while high-gain settings move toward a tight Metal 2000/5150-style response.

At increasing drive, it:

- Raises the preamp high-pass corner from roughly `55 Hz` to `150 Hz`
- Cuts more low-frequency content before clipping
- Feeds more high/upper-mid content into the nonlinear stages
- Opens the anti-fizz low-pass from about `5.6 kHz` toward `8 kHz`
- Reduces sag to preserve high-gain pick attack
- Adds a third saturation stage
- Moves and narrows the mid control toward the modern-metal scoop region
- Raises the soft ceiling slightly for punch

The signal path contains adaptive pre-EQ, a `12/55 ms` sag envelope, two always-active nonlinear stages, an additional high-gain stage, anti-fizz filtering, a soft power-stage ceiling, and bass/mid/treble/presence filters.

Controls:

- Pre-gain: `0..10`
- Drive: `1..25`
- Bass, mid, treble, presence: `±12 dB`
- Master: `0..2`

The nonlinear stages currently run at the session sample rate; there is no oversampling.

### Cabinet simulator

The cabinet uses a Gardner-style hybrid convolution:

- First `64` IR taps: direct FIR for immediate response
- Remaining taps: partitioned overlap-add FFT convolution
- Default partition: `64` samples
- FFT size: `128`
- Tail partition latency aligns with the FIR head, giving zero overall algorithmic latency

IR loading and FFT preparation allocate memory and are therefore control-thread operations. Steady-state processing is allocation-free.

The runtime currently loads a generated `1024`-sample cabinet IR with resonances at approximately `100`, `420`, `1100`, `2200`, and `3800 Hz`. It applies exponential decay, an RMS target, a peak cap, and a sampled frequency-response cap to avoid extreme resonant gain.

External WAV IR loading supports little-endian PCM16 and float32 files and averages multichannel files to mono. IR sample rates are not currently resampled to the live stream rate.

### Chorus

A sine-LFO-modulated, linearly interpolated delay.

- Base delay: `8 ms`
- Rate: `0.05..5 Hz`
- Depth: `0.5..12 ms`
- Mix: `0..1`
- Delay storage: up to `40 ms`

### Delay

A linearly interpolated circular delay:

- Time: `1..2000 ms`
- Feedback: `0..0.95`
- Mix: `0..1`

### Reverb

A mono Freeverb-style topology with eight parallel damped comb filters followed by four serial all-pass filters. Delay lengths are based on the 44.1 kHz Freeverb tunings and scaled for the active sample rate.

- Room size maps feedback approximately from `0.28` to `0.98`
- Damping contributes up to `0.4`
- Fixed storage limits: `2048` samples per comb and `512` per all-pass

### Output limiter

The final limiter provides:

- Approximately `0.3 ms` detector attack
- `80 ms` release
- Soft knee beginning at `85%` of the `0.89` ceiling
- Faster gain reduction than recovery
- Absolute sample clamp at `±1.0`

The limiter is applied before metering, so the waveform, peak display, and CLIP indicator represent the signal actually written to the DAC.

## Factory presets

Factory presets live in `src/preset/Preset.cpp` and are exported to `presets/*.json`.

- **Clean**: open mic'd-amp response with light compression and room
- **Blues**: warm, edge-of-breakup drive with forward mids
- **Crunch**: classic-rock grind and a stronger mid push
- **Metal**: Fender Mustang Metal 2000-inspired chain: fast gate, low-gain/high-level Tube Screamer boost, graphic-style mid scoop, high-gain adaptive amp, high treble/presence, and full cabinet
- **Ambient**: clean body with chorus, delay, and reverb wash

The preset format stores exactly ten ordered effects. Each item contains its type, bypass state, and a map from numeric parameter ID strings to values. Serialization uses the project's own minimal JSON reader/writer rather than a third-party JSON dependency.

Preset application queues parameter changes followed by bypass states. A `20 ms` fade-out and `30 ms` fade-in protect live preset changes from clicks.

## GUI, visualization, and profiling

The UI is a Dear ImGui window using GLFW and OpenGL 3. It provides:

- Bypass controls and parameter sliders for every live effect
- Buttons for all five factory presets
- JSON preset saving
- Current sample rate, buffer size, stream latency, devices, and xruns
- Per-effect and total audio processing time
- Post-limiter waveform, spectrum, output peak, HOT, and CLIP indicators

Visualization details:

- Lock-free `8192`-sample audio-to-GUI ring
- `1024`-sample Hann-windowed FFT
- `64` logarithmic spectrum bars
- Frequency display from `40 Hz` to Nyquist
- Magnitude display from `-80` to `0 dB`
- `512`-sample waveform (about `10.7 ms` at 48 kHz)
- HOT threshold: `0.82`
- CLIP threshold: `0.95`

Profiling uses `std::chrono::steady_clock` around each effect and the complete graph. Results are published atomically and smoothed with an exponential moving average:

```text
average = (previous * 7 + current) / 8
```

## Build and dependencies

Requirements:

- macOS
- CMake `3.20+`
- A C++17 compiler (Apple Clang is expected)
- RtAudio installed system-wide
- `pkg-config` recommended for RtAudio discovery
- Internet access during first configure so CMake can fetch GLFW and Dear ImGui

Homebrew setup:

```bash
brew install cmake pkg-config rtaudio
```

Configure, build, and test:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run:

```bash
./build/src/guitar_dsp_platform
```

CMake builds:

- `guitar_dsp_audio`: audio, DSP, effect graph, WAV, and preset library
- `imgui_glfw_gl3`: Dear ImGui with GLFW/OpenGL backends
- `guitar_dsp_platform`: desktop application

RtAudio is found through `pkg-config` first, then common Homebrew prefixes. GLFW `3.4` and Dear ImGui `1.91.8` are fetched during configuration. On Apple platforms the executable links CoreAudio, CoreFoundation, Cocoa, IOKit, and CoreVideo.

## Test suite

Tests are standalone executables that return nonzero on failure; no external test framework is used.

Run all tests:

```bash
ctest --test-dir build --output-on-failure
```

Registered tests:

1. `sine_to_wav_test` — offline wire-through and engine parameter queue
2. `gain_graph_test` — graph gain, bypass, insert, swap, remove, and ownership
3. `gain_smoothing_test` — step bounds and rapid automation
4. `noise_gate_test`
5. `compressor_test`
6. `overdrive_test`
7. `equalizer_test`
8. `amp_sim_test`
9. `cabinet_test` — hybrid convolution reconstruction and WAV loading
10. `delay_test`
11. `chorus_test`
12. `reverb_test`
13. `preset_test` — factory presets, JSON round trip, graph application, and soft mute
14. `phase11_test` — EQ response, delay timing, cabinet energy/latency, gate, compressor, and gain latency

Some tests write float WAV files for offline inspection; generated WAV files and build artifacts are ignored by Git.

## Source layout

```text
include/audio/     RtAudio engine, parameter queue, visualization ring
include/dsp/       Effect interface, graph, queues, DSP declarations/utilities
include/app/       Application context and fixed slot definitions
include/preset/    Preset structures and bank API
src/audio/         Audio stream implementation
src/dsp/           DSP, FFT, convolution, delay line, and WAV implementation
src/app/           Graph setup and Dear ImGui application
src/preset/        Factory presets and JSON serialization
presets/           Exported factory preset JSON files
tests/             Standalone DSP and integration tests
cmake/             RtAudio discovery and fetched GUI dependencies
```

## Current constraints

- Live processing is mono; stereo input processing is not implemented.
- The application uses a fixed ten-slot topology even though the graph supports structural edits.
- The GUI does not currently expose drag-and-drop graph ordering.
- Manual bypass changes occur at block boundaries; preset changes receive the dedicated mute ramp.
- Preset switching currently includes short synchronous sleeps on the GUI thread.
- Factory preset JSON files are rewritten at startup; saved custom JSON files are not automatically imported into the runtime bank.
- Serialized effect type is informational during application; slot position determines the destination effect.
- External cabinet IRs are not sample-rate converted.
- Audio and cabinet scratch buffers support callbacks up to `4096` frames.
- Overdrive and amp saturation are not oversampled, so high-drive aliasing remains possible.
- Per-effect profiling calls a high-resolution clock from the audio callback; this is practical on the current macOS target but is not a hard-real-time guarantee in portable C++.

## Status

The project implements phases 1–11 of the original plan: real-time audio, a modular graph, smoothing, dynamics/distortion/EQ/amp/cab DSP, modulation/time effects, presets, GUI, visualization, profiling, and automated DSP tests.
