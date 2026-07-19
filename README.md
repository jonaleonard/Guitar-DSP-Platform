# Guitar DSP Platform

Real-time modular guitar DSP platform built with C++17, RtAudio, Dear ImGui, and CMake.

Target hardware: macOS + Volt 1.

## Build

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Clean start (important)

The app starts as **clean wire-through** (all color FX bypassed).  
Editing a parameter **auto-enables** that effect. Type `clean` to mute them all again.

## Phase 6 — Chorus / Delay / Reverb

Chain: **Gate → Comp → Drive → EQ → Amp → Cab → Chorus → Delay → Reverb → Gain**

### Automated

```bash
ctest --test-dir build --output-on-failure -R "delay|chorus|reverb"
```

### Live guitar

```bash
./build/src/guitar_dsp_platform
```

1. Play — should sound like dry guitar (clean).
2. Delay: `dx 0.4` then `dt 400`
3. Chorus: `chm 0.5` then `chr 1.2`
4. Reverb: `rx 0.35` then `rr 0.7`
5. Amp/cab: `ad 6`, `cx 1`
6. `clean` — back to dry wire

Type `h` for all commands.

## Status

Phase 6 — time-based effects (ready to test). Starts clean.
