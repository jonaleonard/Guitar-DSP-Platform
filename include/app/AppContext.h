#pragma once

#include "audio/AudioEngine.h"
#include "audio/AudioRingBuffer.h"
#include "dsp/EffectGraph.h"
#include "preset/Preset.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace app {

constexpr int kNumSlots = preset::kNumSlots;
constexpr int kMaxBlockFrames = 4096;

enum Slot : int {
    kGate = 0,
    kComp = 1,
    kDrive = 2,
    kEq = 3,
    kAmp = 4,
    kCab = 5,
    kChorus = 6,
    kDelay = 7,
    kReverb = 8,
    kGain = 9
};

inline const char* slotName(const int slot)
{
    switch (slot) {
    case kGate:
        return "Gate";
    case kComp:
        return "Comp";
    case kDrive:
        return "Drive";
    case kEq:
        return "EQ";
    case kAmp:
        return "Amp";
    case kCab:
        return "Cab";
    case kChorus:
        return "Chorus";
    case kDelay:
        return "Delay";
    case kReverb:
        return "Reverb";
    case kGain:
        return "Gain";
    default:
        return "?";
    }
}

// Mirrored UI / control-thread parameter snapshot (not read on the audio thread).
struct UiState {
    std::array<bool, kNumSlots> bypassed{};

    float gateThresh = -80.f;
    float gateAttack = 2.f;
    float gateRelease = 80.f;

    float compThresh = 0.f;
    float compRatio = 1.f;
    float compAttack = 10.f;
    float compRelease = 100.f;
    float compMakeup = 0.f;

    float drive = 1.f;
    float driveMix = 0.f;
    float driveOut = 1.f;

    float eqLow = 0.f;
    float eqMid = 0.f;
    float eqHigh = 0.f;
    float eqMidFreq = 800.f;
    float eqMidQ = 0.7f;

    float ampPre = 1.f;
    float ampDrive = 1.f;
    float ampBass = 0.f;
    float ampMid = 0.f;
    float ampTreble = 0.f;
    float ampPresence = 0.f;
    float ampMaster = 1.f;

    float cabMix = 0.f;
    float cabLevel = 1.f;

    float chorusRate = 0.8f;
    float chorusDepth = 3.f;
    float chorusMix = 0.f;

    float delayTime = 350.f;
    float delayFeedback = 0.35f;
    float delayMix = 0.f;

    float reverbRoom = 0.5f;
    float reverbDamp = 0.5f;
    float reverbMix = 0.f;

    float gain = 1.f;
};

struct EngineMetrics {
    std::atomic<std::uint64_t> processMicros{0};
    std::atomic<std::uint64_t> callbackCount{0};
};

struct AppContext {
    std::unique_ptr<audio::AudioEngine> engine;
    dsp::EffectGraph graph;
    preset::PresetBank bank;
    preset::SoftMute mute;
    UiState ui{};
    EngineMetrics metrics{};
    audio::AudioRingBuffer vizRing{};
    std::array<float, kMaxBlockFrames> mono{};
    std::atomic<bool> running{true};
    std::atomic<bool> presetBusy{false};
    std::atomic<int> currentPreset{0};
    double sampleRate = 48000.0;
    std::string presetsDirectory;
};

bool setupAudioGraph(AppContext& ctx);
void syncUiFromPreset(AppContext& ctx, const preset::Preset& preset);
bool loadPresetAsync(AppContext& ctx, int index);
void setSlotBypassed(AppContext& ctx, int slot, bool bypassed);
void enableSlot(AppContext& ctx, int slot);
void pushSlotParams(AppContext& ctx, int slot);
preset::Preset captureCurrentPreset(const AppContext& ctx, const std::string& name);
int runGui(AppContext& ctx);

} // namespace app
