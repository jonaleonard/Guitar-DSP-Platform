# Real-Time Modular Guitar DSP Platform — Implementation Plan

**Stack:** C++17/20, RtAudio (audio I/O), Dear ImGui (GUI/viz), CMake, macOS + Volt 1
**Method:** You direct, AI (Claude/Claude Code) generates code phase by phase. Don't skip ahead — each phase should run and be tested before the next starts.

---

## Phase 0 — Environment & Tooling Setup

**Goal:** A working C++ build environment on macOS that can talk to the Volt 1.

1. Install Xcode Command Line Tools (`xcode-select --install`).
2. Install Homebrew, then:
   - `cmake`
   - `rtaudio` (or build from source — you'll likely want the source anyway to read/patch it)
   - `pkg-config`
3. Confirm the Volt 1 shows up in **Audio MIDI Setup.app** as an input/output device. Set sample rate here (44.1kHz or 48kHz — pick 48kHz, it's the more common pro-audio default) and note the buffer size options.
4. Set up a git repo with a CMake skeleton: `src/`, `include/`, `tests/`, `third_party/`.
5. Write the smallest possible RtAudio program: open a stream on the Volt 1, and inside the callback, do nothing but copy input straight to output (a "wire"). No processing yet.
6. **Success check:** plug in the guitar, hear it monitored through headphones with no processing, no crashes, no audible clicks, and acceptably low latency (RtAudio will report the actual buffer size/latency — write it down as a baseline).

This phase is 100% about de-risking the hardware/OS/driver path before any DSP exists. Don't let AI generate DSP code yet — get the wire working first.

---

## Phase 1 — Real-Time Audio Engine Core

**Goal:** A stable audio engine class that owns the RtAudio stream and exposes a clean callback boundary, decoupled from any specific effect.

1. Design an `AudioEngine` class:
   - Owns the RtAudio instance, device selection, sample rate, buffer size.
   - Exposes a single function pointer / `std::function` or virtual callback: `processBlock(float* in, float* out, int numFrames)`.
   - No STL containers, no `new`/`malloc`, no locks, no logging inside the real-time callback — this rule matters and should be explicit in your prompts to the AI. Real-time audio code has a hard rule: **never allocate, lock, or block on the audio thread.**
2. Add basic buffer/format handling (mono in, stereo or mono out — decide based on how you'll monitor).
3. Add a simple **thread-safe parameter passing mechanism** now, even before you have parameters — e.g. atomics or a lock-free ring buffer for GUI-thread → audio-thread communication. This is much easier to bolt in now than to retrofit after 10 effects exist.
4. Write a test harness that feeds a synthetic sine wave through the engine (no hardware) and dumps output to a WAV file, so you can validate DSP without touching the guitar every time.

**Success check:** wire-through still works via the new `AudioEngine` abstraction; the sine-wave-to-WAV test harness runs standalone.

---

## Phase 2 — Effect Interface & Modular Graph

**Goal:** The core architectural piece — an `Effect` base class and a `Graph` that chains them, with no framework-specific types.

1. Define the interface, deliberately framework-agnostic (so it drops into Daisy Seed later):
   ```cpp
   class Effect {
   public:
       virtual ~Effect() = default;
       virtual void prepare(double sampleRate, int maxBlockSize) = 0;
       virtual void process(float* buffer, int numFrames) = 0;
       virtual void setParameter(int paramId, float value) = 0;
       virtual void setBypassed(bool bypassed) = 0;
   };
   ```
2. Build an `EffectGraph` that holds an ordered list of `Effect*` (or `std::unique_ptr<Effect>`), and calls `process()` on each in sequence.
3. Support: bypass (per-effect), reorder, insert, remove — all via a message queue from GUI/control thread, applied at a safe point in the audio callback (never mutate the vector directly from another thread mid-callback).
4. Implement one trivial effect first — a **Gain** effect — purely to validate the graph plumbing, not because gain is interesting DSP.
5. Wire the graph into the `AudioEngine` from Phase 1.

**Success check:** guitar signal passes through the graph with a single Gain module; toggling bypass and changing gain live (via a hardcoded test, no GUI yet) works with no clicks or crashes.

---

## Phase 3 — Parameter Smoothing (build into the engine, not per-effect)

**Goal:** Avoid zipper noise/clicks on any parameter change, engine-wide.

1. Build a small `SmoothedValue` utility (linear or exponential ramp over N milliseconds) that every effect can use for any parameter.
2. Retrofit the Gain effect to use it.
3. This becomes a shared utility all future effects use — establish the pattern now while there's only one effect to update.

**Success check:** rapid gain automation (e.g. a test sweeping gain from 0 to 1 every 10ms) produces no audible zipper/clicking artifact.

---

## Phase 4 — First Real DSP Modules

**Goal:** Noise Gate, Compressor, basic Overdrive/Distortion — the "front of the signal chain" blocks, chosen because they're simple enough to validate correctness against known math (thresholds, ratios, waveshaping).

Order matters — build and fully test one at a time:

1. **Noise Gate** — threshold + attack/release. Easy to verify: feed silence + hum, confirm it gates.
2. **Compressor** — threshold, ratio, attack, release, makeup gain. Verify against a known reference (e.g. compare RMS/peak behavior on a step input).
3. **Overdrive/Distortion** — simple waveshaping (tanh or soft clip) to start. Don't reach for amp-modeling complexity yet.

Each module should have:
- Its own header/cpp, inheriting `Effect`.
- Unit tests (see Phase 10, but start writing them now, per-module, not at the end).
- A place in the graph, addable/removable.

**Success check:** each module individually demonstrably does what it claims on synthetic test signals (not just "sounds right" — measure it).

---

## Phase 5 — EQ, Amp Sim, Cabinet Sim (IR Convolution)

**Goal:** The harder DSP — this is where most of the "amp simulator" character lives.

1. **Equalizer** — start with a few biquad filter stages (shelving + peaking), standard RBJ cookbook coefficients. This is well-trodden ground for AI-generated code, but verify frequency response against expectation (see testing phase).
2. **Amp Simulation** — start simple: pre-EQ → nonlinear waveshaping stage(s) → tone stack (another EQ stage) → post-gain. Don't aim for a physically-modeled tube amp yet; a good waveshaper + EQ chain gets you 80% of the character for 20% of the effort.
3. **Cabinet Simulation via IR convolution**:
   - Source or generate a few guitar cabinet impulse responses (WAV files, mono, short — a few hundred ms).
   - Implement (or use a lightweight open-source) FFT-based partitioned convolution — this is the first place where naive time-domain convolution will blow your CPU budget, so plan for FFT-based block convolution from the start.
   - This module benefits from a dedicated correctness test: convolving an impulse through your engine should reproduce the loaded IR almost exactly.

**Success check:** you can load an IR file, hear the cabinet-simulated tone, and the amp sim/EQ/cab chain sounds like a plausible guitar tone, not just "distorted."

---

## Phase 6 — Time-Based Effects: Chorus, Delay, Reverb

**Goal:** Modulation and time-domain effects — these introduce circular buffers and (for chorus) interpolated delay lines, both new techniques versus Phases 4–5.

1. **Delay** — circular buffer, feedback, wet/dry mix, tempo-independent to start (add tap-tempo/MIDI sync later as a stretch goal).
2. **Chorus** — modulated short delay line with LFO, needs fractional-sample (interpolated) delay reads. This is a good forcing function to build a reusable `DelayLine` utility with interpolation, which the Delay effect can also be refactored to use.
3. **Reverb** — start with a well-known freely-available algorithm structure (e.g. Schroeder/Freeverb-style comb+allpass network) rather than trying to invent one; convolution reverb is a stretch goal, not a phase-6 goal.

**Success check:** all three effects sound correct in isolation and in combination with the rest of the chain; no CPU spikes or dropouts with the full effect chain active (Gate → Comp → Drive → EQ → Amp → Cab → Chorus → Delay → Reverb) at your target buffer size.

---

## Phase 7 — Preset System

**Goal:** Save/load the full graph state (which effects, order, bypass state, all parameter values) without clicks.

1. Define a serialization format — JSON is fine and easy for AI to generate reliably (nlohmann/json is a good pick, header-only, easy to integrate).
2. Preset = ordered list of `{effectType, bypassed, parameters{}}`.
3. **Smooth preset changes**: when a preset loads, don't slam parameters — ramp into new values using the Phase 3 smoothing utility, or crossfade if effects are being added/removed from the graph (this is the trickiest bit — swapping in/out actual effect instances without a click usually means a brief crossfade between old-graph-output and new-graph-output).
4. Ship 4–5 presets matching the doc's suggestions: Clean, Blues, Crunch, Metal, Ambient.

**Success check:** switching presets live, mid-play, produces no pop/click/glitch.

---

## Phase 8 — GUI (Dear ImGui)

**Goal:** A functional control surface — not polished yet, just usable.

1. Set up an ImGui + backend (GLFW + Metal or OpenGL) window on macOS, separate render loop from the audio thread entirely — GUI thread only ever posts parameter changes into the lock-free queue from Phase 1/2, never touches DSP objects directly.
2. Per-effect panel: enable/disable toggle, knobs/sliders bound to parameters.
3. Preset load/save UI.
4. CPU/latency readout (feed from Phase 9's profiler).

**Success check:** you can control the whole chain live from the GUI while playing guitar, with the same no-click guarantees as before.

---

## Phase 9 — Visualization

**Goal:** Waveform, FFT/spectrum, clipping indicators.

1. Tap the audio buffer post-processing (read-only, into a small ring buffer the GUI thread can safely read — again, no locks on the audio thread; use a lock-free SPSC ring buffer).
2. Waveform: straightforward — draw the ring buffer contents as a line.
3. Spectrum: run an FFT (e.g. `kissfft` or `pffft`, both lightweight and easy to integrate) on windowed chunks of that same ring buffer, draw magnitude in dB.
4. Clipping indicator: track a "samples over threshold" flag, decay it visually over time so brief clips are visible.

**Success check:** waveform and spectrum visibly and correctly respond to what you're playing, in real time, without affecting audio stability.

---

## Phase 10 — Performance Profiler

**Goal:** Per-effect and total processing time, matching the doc's example table.

1. Wrap each effect's `process()` call in high-resolution timing (`std::chrono::steady_clock` or platform high-res timer) inside the graph's dispatch loop.
2. Accumulate rolling averages (don't print every callback — that itself would be a real-time violation; store into atomics/ring buffer, read by GUI thread).
3. Surface in the GUI: per-effect ms, total ms, and % of your available budget (buffer size / sample rate).

**Success check:** you get a live-updating table like the one in the project doc, and it changes sensibly when you bypass expensive effects (reverb/cab sim should visibly cost more than gain).

---

## Phase 11 — Testing

**Goal:** Objective, automated DSP correctness — not "it sounds fine."

Retroactively (or ideally incrementally per-phase, as noted above) build:

1. **Sine wave tests** — feed known frequencies, verify expected gain/attenuation per effect (e.g. EQ boost at target frequency measurable via FFT of output).
2. **Impulse response tests** — verify cab sim reproduces the loaded IR; verify delay/reverb impulse response has expected timing/decay.
3. **Frequency response tests** — sweep a chirp or step through sine sweeps, capture magnitude response, compare against expected filter curves.
4. **Clipping/limiting tests** — feed signals designed to hit clip thresholds, verify gate/compressor/limiter behavior against known math.
5. **Latency measurement** — measure round-trip latency through the whole graph, log it as a regression check (a new effect silently adding latency should be visible in CI/test output, not discovered by ear).

Use a C++ test framework (Catch2 or GoogleTest) so these run as a normal `ctest` suite, ideally on every change.

**Success check:** `ctest` (or equivalent) passes, and gives you actual numeric confidence, not vibes.

---

## Phase 12 — Stretch Goals (pick opportunistically, not required for the "done" demo)

- MIDI control / footswitch input (for live preset switching and expression pedal control of parameters).
- Oversampling for the distortion/amp stages (reduces aliasing on nonlinear stages — worth doing once you're happy with the base sound).
- Convolution reverb (swap in as an alternative to the Schroeder reverb).
- Drag-and-drop graph reordering in the GUI.
- Plugin format wrapper (VST3/AU) if you ever want to use this inside a DAW — note this would be a genuinely separate effort layered on top of the same `Effect`/`EffectGraph` core, which is exactly why keeping that core framework-agnostic pays off.
- Raspberry Pi deployment — good intermediate step before full custom-hardware/Daisy Seed embedded work, since Pi still runs a full OS/RTAudio-like stack, unlike bare-metal Daisy.
- Daisy Seed port — at this point you'd swap `AudioEngine`'s RtAudio backend for `libDaisy`'s audio callback, and (ideally) drop your `Effect`/DSP classes in with zero changes, since none of them should depend on anything but plain C++.

---

## Working Norms While Using AI to Generate Code

A few practices worth locking in from Phase 0 onward, since you're generating all code via AI rather than hand-typing it:

- **State the real-time constraints in every DSP-related prompt**: no heap allocation, no locks, no logging/printf, no exceptions on the audio thread. AI-generated audio code frequently violates this by default (e.g. reaching for `std::vector::push_back` or `std::cout` inside `process()`) unless explicitly told not to.
- **Test each module before moving to the next.** Don't let three unverified effects stack up — a bug in the Compressor will be much harder to isolate once Distortion and EQ are also unverified.
- **Keep DSP classes framework-agnostic** (plain float buffers in/out) even where it's slightly more convenient to reach into RtAudio or ImGui types directly — this is the one architectural discipline that pays for itself at the Daisy Seed stage.
- **Commit after each phase**, not each session — gives you a clean rollback point if an AI-generated refactor introduces a regression you don't catch immediately.