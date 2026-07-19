#pragma once

#include "dsp/EffectGraph.h"

#include <array>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

namespace preset {

constexpr int kNumSlots = 10;

enum class EffectType {
    NoiseGate,
    Compressor,
    Overdrive,
    Equalizer,
    AmpSim,
    Cabinet,
    Chorus,
    Delay,
    Reverb,
    Gain
};

const char* effectTypeName(EffectType type);
EffectType effectTypeFromName(const std::string& name);
bool isValidEffectTypeName(const std::string& name);

struct EffectState {
    EffectType type = EffectType::Gain;
    bool bypassed = true;
    // paramId -> value
    std::unordered_map<int, float> parameters;
};

struct Preset {
    std::string name;
    std::vector<EffectState> effects; // ordered; expected size kNumSlots
};

// Soft-mute helper used by the audio callback during preset switches.
struct SoftMute {
    std::atomic<float> gain{1.0f};
    std::atomic<int> samplesRemaining{0};
    std::atomic<float> step{0.0f};
    std::atomic<float> target{1.0f};

    void startFade(float toGain, int numSamples);
    float nextGain(); // audio thread
};

class PresetBank {
public:
    void clear();
    void add(Preset preset);
    int size() const { return static_cast<int>(presets_.size()); }
    const Preset* at(int index) const;
    int findByName(const std::string& name) const; // -1 if missing

    // Built-in Clean / Blues / Crunch / Metal / Ambient
    void loadFactoryPresets();

    bool loadFromFile(const std::string& path, std::string* error = nullptr);
    bool saveToFile(const Preset& preset, const std::string& path, std::string* error = nullptr) const;
    int loadDirectory(const std::string& dir); // returns count loaded

    // Apply to fixed-topology graph. Parameters ramp via SmoothedValue.
    // Updates bypassedFlags[slot] to match preset.
    // Call after soft-mute reaches ~0 for click-free bypass flips.
    bool apply(const Preset& preset,
               dsp::EffectGraph& graph,
               std::array<bool, kNumSlots>& bypassedFlags) const;

    const std::vector<Preset>& presets() const { return presets_; }

private:
    std::vector<Preset> presets_;
};

Preset makeFactoryClean();
Preset makeFactoryBlues();
Preset makeFactoryCrunch();
Preset makeFactoryMetal();
Preset makeFactoryAmbient();

} // namespace preset
