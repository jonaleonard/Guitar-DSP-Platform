#include "preset/Preset.h"

#include "dsp/AmpSimEffect.h"
#include "dsp/CabinetEffect.h"
#include "dsp/ChorusEffect.h"
#include "dsp/CompressorEffect.h"
#include "dsp/DelayEffect.h"
#include "dsp/EqualizerEffect.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/OverdriveEffect.h"
#include "dsp/ReverbEffect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace preset {
namespace {

std::string trim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::string toLower(std::string s)
{
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

EffectState slot(EffectType type, bool bypassed, std::initializer_list<std::pair<int, float>> params)
{
    EffectState s;
    s.type = type;
    s.bypassed = bypassed;
    for (const auto& p : params) {
        s.parameters[p.first] = p.second;
    }
    return s;
}

} // namespace

const char* effectTypeName(const EffectType type)
{
    switch (type) {
    case EffectType::NoiseGate:
        return "NoiseGate";
    case EffectType::Compressor:
        return "Compressor";
    case EffectType::Overdrive:
        return "Overdrive";
    case EffectType::Equalizer:
        return "Equalizer";
    case EffectType::AmpSim:
        return "AmpSim";
    case EffectType::Cabinet:
        return "Cabinet";
    case EffectType::Chorus:
        return "Chorus";
    case EffectType::Delay:
        return "Delay";
    case EffectType::Reverb:
        return "Reverb";
    case EffectType::Gain:
        return "Gain";
    }
    return "Unknown";
}

EffectType effectTypeFromName(const std::string& name)
{
    const std::string n = toLower(name);
    if (n == "noisegate" || n == "gate") {
        return EffectType::NoiseGate;
    }
    if (n == "compressor" || n == "comp") {
        return EffectType::Compressor;
    }
    if (n == "overdrive" || n == "drive") {
        return EffectType::Overdrive;
    }
    if (n == "equalizer" || n == "eq") {
        return EffectType::Equalizer;
    }
    if (n == "ampsim" || n == "amp") {
        return EffectType::AmpSim;
    }
    if (n == "cabinet" || n == "cab") {
        return EffectType::Cabinet;
    }
    if (n == "chorus") {
        return EffectType::Chorus;
    }
    if (n == "delay") {
        return EffectType::Delay;
    }
    if (n == "reverb" || n == "verb") {
        return EffectType::Reverb;
    }
    return EffectType::Gain;
}

bool isValidEffectTypeName(const std::string& name)
{
    const std::string n = toLower(name);
    return n == "noisegate" || n == "gate" || n == "compressor" || n == "comp" || n == "overdrive" ||
           n == "drive" || n == "equalizer" || n == "eq" || n == "ampsim" || n == "amp" ||
           n == "cabinet" || n == "cab" || n == "chorus" || n == "delay" || n == "reverb" ||
           n == "verb" || n == "gain";
}

void SoftMute::startFade(const float toGain, const int numSamples)
{
    const int n = std::max(1, numSamples);
    const float from = gain.load(std::memory_order_relaxed);
    step.store((toGain - from) / static_cast<float>(n), std::memory_order_relaxed);
    target.store(toGain, std::memory_order_relaxed);
    samplesRemaining.store(n, std::memory_order_release);
}

float SoftMute::nextGain()
{
    int remaining = samplesRemaining.load(std::memory_order_relaxed);
    if (remaining <= 0) {
        return gain.load(std::memory_order_relaxed);
    }

    float g = gain.load(std::memory_order_relaxed);
    g += step.load(std::memory_order_relaxed);
    --remaining;
    if (remaining <= 0) {
        g = target.load(std::memory_order_relaxed);
        remaining = 0;
    }
    gain.store(g, std::memory_order_relaxed);
    samplesRemaining.store(remaining, std::memory_order_relaxed);
    return g;
}

void PresetBank::clear()
{
    presets_.clear();
}

void PresetBank::add(Preset preset)
{
    presets_.push_back(std::move(preset));
}

const Preset* PresetBank::at(const int index) const
{
    if (index < 0 || index >= static_cast<int>(presets_.size())) {
        return nullptr;
    }
    return &presets_[static_cast<std::size_t>(index)];
}

int PresetBank::findByName(const std::string& name) const
{
    const std::string key = toLower(name);
    for (int i = 0; i < static_cast<int>(presets_.size()); ++i) {
        if (toLower(presets_[static_cast<std::size_t>(i)].name) == key) {
            return i;
        }
    }
    return -1;
}

Preset makeFactoryClean()
{
    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;

    // Studio clean: open, smooth, ~-12 dBFS peaks with input trim.
    Preset p;
    p.name = "Clean";
    p.effects = {
        slot(EffectType::NoiseGate, true, {{NG::kThresholdDb, -80.f}, {NG::kAttackMs, 2.f}, {NG::kReleaseMs, 80.f}, {NG::kRangeDb, -80.f}}),
        slot(EffectType::Compressor, false, {{C::kThresholdDb, -28.f}, {C::kRatio, 1.4f}, {C::kAttackMs, 20.f}, {C::kReleaseMs, 150.f}, {C::kMakeupDb, 0.0f}}),
        slot(EffectType::Overdrive, true, {{D::kDrive, 1.f}, {D::kMix, 0.f}, {D::kOutput, 1.f}}),
        slot(EffectType::Equalizer, false, {{E::kLowGainDb, 0.5f}, {E::kMidGainDb, 0.5f}, {E::kHighGainDb, -0.5f}, {E::kMidFreqHz, 900.f}, {E::kMidQ, 0.7f}, {E::kLowFreqHz, 120.f}, {E::kHighFreqHz, 4500.f}}),
        slot(EffectType::AmpSim, false, {{A::kPreGain, 1.0f}, {A::kDrive, 1.25f}, {A::kBassDb, 0.5f}, {A::kMidDb, 0.5f}, {A::kTrebleDb, -0.5f}, {A::kPresenceDb, -1.0f}, {A::kMaster, 0.85f}}),
        slot(EffectType::Cabinet, false, {{Cab::kMix, 0.55f}, {Cab::kLevel, 0.9f}}),
        slot(EffectType::Chorus, true, {{Ch::kRateHz, 0.8f}, {Ch::kDepthMs, 3.f}, {Ch::kMix, 0.f}}),
        slot(EffectType::Delay, true, {{Dl::kTimeMs, 350.f}, {Dl::kFeedback, 0.35f}, {Dl::kMix, 0.f}}),
        slot(EffectType::Reverb, true, {{R::kRoomSize, 0.5f}, {R::kDamping, 0.5f}, {R::kMix, 0.f}}),
        slot(EffectType::Gain, false, {{G::kGain, 0.85f}}),
    };
    return p;
}

Preset makeFactoryBlues()
{
    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;

    // Warm blues breakup — OD mostly wet into amp, studio headroom.
    Preset p;
    p.name = "Blues";
    p.effects = {
        slot(EffectType::NoiseGate, true, {{NG::kThresholdDb, -75.f}, {NG::kAttackMs, 5.f}, {NG::kReleaseMs, 120.f}, {NG::kRangeDb, -80.f}}),
        slot(EffectType::Compressor, false, {{C::kThresholdDb, -30.f}, {C::kRatio, 1.35f}, {C::kAttackMs, 22.f}, {C::kReleaseMs, 180.f}, {C::kMakeupDb, 0.0f}}),
        slot(EffectType::Overdrive, false, {{D::kDrive, 2.4f}, {D::kMix, 0.72f}, {D::kOutput, 0.85f}}),
        slot(EffectType::Equalizer, false, {{E::kLowGainDb, 1.5f}, {E::kMidGainDb, 2.0f}, {E::kHighGainDb, -0.5f}, {E::kMidFreqHz, 700.f}, {E::kMidQ, 0.65f}, {E::kLowFreqHz, 110.f}, {E::kHighFreqHz, 3500.f}}),
        slot(EffectType::AmpSim, false, {{A::kPreGain, 1.2f}, {A::kDrive, 2.6f}, {A::kBassDb, 2.0f}, {A::kMidDb, 2.0f}, {A::kTrebleDb, -0.5f}, {A::kPresenceDb, -1.0f}, {A::kMaster, 0.7f}}),
        slot(EffectType::Cabinet, false, {{Cab::kMix, 0.85f}, {Cab::kLevel, 0.85f}}),
        slot(EffectType::Chorus, true, {{Ch::kRateHz, 0.6f}, {Ch::kDepthMs, 2.5f}, {Ch::kMix, 0.f}}),
        slot(EffectType::Delay, true, {{Dl::kTimeMs, 380.f}, {Dl::kFeedback, 0.2f}, {Dl::kMix, 0.f}}),
        slot(EffectType::Reverb, false, {{R::kRoomSize, 0.2f}, {R::kDamping, 0.6f}, {R::kMix, 0.04f}}),
        slot(EffectType::Gain, false, {{G::kGain, 0.8f}}),
    };
    return p;
}

Preset makeFactoryCrunch()
{
    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;

    // Classic rock crunch — mid-forward, controlled, dry cab.
    Preset p;
    p.name = "Crunch";
    p.effects = {
        slot(EffectType::NoiseGate, true, {{NG::kThresholdDb, -75.f}, {NG::kAttackMs, 2.f}, {NG::kReleaseMs, 80.f}, {NG::kRangeDb, -80.f}}),
        slot(EffectType::Compressor, false, {{C::kThresholdDb, -24.f}, {C::kRatio, 1.6f}, {C::kAttackMs, 12.f}, {C::kReleaseMs, 120.f}, {C::kMakeupDb, 0.0f}}),
        slot(EffectType::Overdrive, false, {{D::kDrive, 3.5f}, {D::kMix, 0.8f}, {D::kOutput, 0.8f}}),
        slot(EffectType::Equalizer, false, {{E::kLowGainDb, 1.0f}, {E::kMidGainDb, 2.5f}, {E::kHighGainDb, 0.5f}, {E::kMidFreqHz, 900.f}, {E::kMidQ, 0.8f}, {E::kLowFreqHz, 110.f}, {E::kHighFreqHz, 3800.f}}),
        slot(EffectType::AmpSim, false, {{A::kPreGain, 1.6f}, {A::kDrive, 4.2f}, {A::kBassDb, 1.0f}, {A::kMidDb, 2.5f}, {A::kTrebleDb, 0.5f}, {A::kPresenceDb, 0.0f}, {A::kMaster, 0.58f}}),
        slot(EffectType::Cabinet, false, {{Cab::kMix, 0.92f}, {Cab::kLevel, 0.82f}}),
        slot(EffectType::Chorus, true, {{Ch::kRateHz, 0.8f}, {Ch::kDepthMs, 3.f}, {Ch::kMix, 0.f}}),
        slot(EffectType::Delay, true, {{Dl::kTimeMs, 300.f}, {Dl::kFeedback, 0.2f}, {Dl::kMix, 0.f}}),
        slot(EffectType::Reverb, true, {{R::kRoomSize, 0.3f}, {R::kDamping, 0.6f}, {R::kMix, 0.f}}),
        slot(EffectType::Gain, false, {{G::kGain, 0.75f}}),
    };
    return p;
}

Preset makeFactoryMetal()
{
    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;

    // Modern metal: OD as almost-wet boost into amp; saturate inside, not the DAC.
    Preset p;
    p.name = "Metal";
    p.effects = {
        slot(EffectType::NoiseGate, false, {{NG::kThresholdDb, -62.f}, {NG::kAttackMs, 1.0f}, {NG::kReleaseMs, 50.f}, {NG::kRangeDb, -80.f}}),
        slot(EffectType::Compressor, false, {{C::kThresholdDb, -22.f}, {C::kRatio, 2.0f}, {C::kAttackMs, 8.f}, {C::kReleaseMs, 90.f}, {C::kMakeupDb, 0.0f}}),
        slot(EffectType::Overdrive, false, {{D::kDrive, 5.0f}, {D::kMix, 0.92f}, {D::kOutput, 0.7f}}),
        slot(EffectType::Equalizer, false, {{E::kLowGainDb, 2.5f}, {E::kMidGainDb, -3.0f}, {E::kHighGainDb, 2.5f}, {E::kMidFreqHz, 800.f}, {E::kMidQ, 1.1f}, {E::kLowFreqHz, 95.f}, {E::kHighFreqHz, 4200.f}}),
        slot(EffectType::AmpSim, false, {{A::kPreGain, 2.2f}, {A::kDrive, 8.5f}, {A::kBassDb, 2.0f}, {A::kMidDb, -2.0f}, {A::kTrebleDb, 1.5f}, {A::kPresenceDb, 1.0f}, {A::kMaster, 0.48f}}),
        slot(EffectType::Cabinet, false, {{Cab::kMix, 0.95f}, {Cab::kLevel, 0.8f}}),
        slot(EffectType::Chorus, true, {{Ch::kRateHz, 0.8f}, {Ch::kDepthMs, 3.f}, {Ch::kMix, 0.f}}),
        slot(EffectType::Delay, true, {{Dl::kTimeMs, 250.f}, {Dl::kFeedback, 0.15f}, {Dl::kMix, 0.f}}),
        slot(EffectType::Reverb, true, {{R::kRoomSize, 0.2f}, {R::kDamping, 0.7f}, {R::kMix, 0.f}}),
        slot(EffectType::Gain, false, {{G::kGain, 0.78f}}),
    };
    return p;
}

Preset makeFactoryAmbient()
{
    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;

    // Ambient wash with controlled wet stack (less echo pile-up / peaking).
    Preset p;
    p.name = "Ambient";
    p.effects = {
        slot(EffectType::NoiseGate, true, {{NG::kThresholdDb, -75.f}, {NG::kAttackMs, 5.f}, {NG::kReleaseMs, 150.f}, {NG::kRangeDb, -80.f}}),
        slot(EffectType::Compressor, false, {{C::kThresholdDb, -26.f}, {C::kRatio, 1.8f}, {C::kAttackMs, 16.f}, {C::kReleaseMs, 180.f}, {C::kMakeupDb, 0.5f}}),
        slot(EffectType::Overdrive, true, {{D::kDrive, 2.f}, {D::kMix, 0.1f}, {D::kOutput, 1.f}}),
        slot(EffectType::Equalizer, false, {{E::kLowGainDb, 1.5f}, {E::kMidGainDb, -0.5f}, {E::kHighGainDb, 2.0f}, {E::kMidFreqHz, 700.f}, {E::kMidQ, 0.6f}, {E::kLowFreqHz, 140.f}, {E::kHighFreqHz, 5000.f}}),
        slot(EffectType::AmpSim, false, {{A::kPreGain, 1.3f}, {A::kDrive, 2.0f}, {A::kBassDb, 1.0f}, {A::kMidDb, -0.5f}, {A::kTrebleDb, 1.5f}, {A::kPresenceDb, 0.5f}, {A::kMaster, 0.75f}}),
        slot(EffectType::Cabinet, false, {{Cab::kMix, 0.7f}, {Cab::kLevel, 0.9f}}),
        slot(EffectType::Chorus, false, {{Ch::kRateHz, 0.28f}, {Ch::kDepthMs, 5.0f}, {Ch::kMix, 0.35f}}),
        slot(EffectType::Delay, false, {{Dl::kTimeMs, 450.f}, {Dl::kFeedback, 0.32f}, {Dl::kMix, 0.22f}}),
        slot(EffectType::Reverb, false, {{R::kRoomSize, 0.72f}, {R::kDamping, 0.4f}, {R::kMix, 0.28f}}),
        slot(EffectType::Gain, false, {{G::kGain, 0.85f}}),
    };
    return p;
}

void PresetBank::loadFactoryPresets()
{
    presets_.clear();
    presets_.push_back(makeFactoryClean());
    presets_.push_back(makeFactoryBlues());
    presets_.push_back(makeFactoryCrunch());
    presets_.push_back(makeFactoryMetal());
    presets_.push_back(makeFactoryAmbient());
}

bool PresetBank::apply(const Preset& preset,
                       dsp::EffectGraph& graph,
                       std::array<bool, kNumSlots>& bypassedFlags) const
{
    if (static_cast<int>(preset.effects.size()) != kNumSlots) {
        return false;
    }
    if (graph.size() < kNumSlots) {
        return false;
    }

    for (int i = 0; i < kNumSlots; ++i) {
        const EffectState& st = preset.effects[static_cast<std::size_t>(i)];
        for (const auto& kv : st.parameters) {
            graph.setParameter(i, kv.first, kv.second);
        }
    }

    for (int i = 0; i < kNumSlots; ++i) {
        const bool bypassed = preset.effects[static_cast<std::size_t>(i)].bypassed;
        bypassedFlags[static_cast<std::size_t>(i)] = bypassed;
        graph.setBypassed(i, bypassed);
    }

    return true;
}

namespace {

void writeJson(std::ostream& out, const Preset& preset)
{
    out << "{\n  \"name\": \"" << preset.name << "\",\n  \"effects\": [\n";
    for (std::size_t i = 0; i < preset.effects.size(); ++i) {
        const EffectState& e = preset.effects[i];
        out << "    {\n";
        out << "      \"type\": \"" << effectTypeName(e.type) << "\",\n";
        out << "      \"bypassed\": " << (e.bypassed ? "true" : "false") << ",\n";
        out << "      \"parameters\": {";
        bool first = true;
        for (const auto& kv : e.parameters) {
            if (!first) {
                out << ", ";
            }
            first = false;
            out << "\"" << kv.first << "\": " << kv.second;
        }
        out << "}\n    }";
        if (i + 1 < preset.effects.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
}

// Minimal parser for our preset JSON schema (not a general JSON library).
bool parsePresetJson(const std::string& text, Preset& out, std::string* error)
{
    out = {};
    auto fail = [&](const char* msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

    const auto namePos = text.find("\"name\"");
    if (namePos == std::string::npos) {
        return fail("missing name");
    }
    const auto nameColon = text.find(':', namePos);
    const auto q1 = text.find('"', nameColon);
    const auto q2 = text.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) {
        return fail("bad name");
    }
    out.name = text.substr(q1 + 1, q2 - q1 - 1);

    std::size_t search = 0;
    while (true) {
        const auto typePos = text.find("\"type\"", search);
        if (typePos == std::string::npos) {
            break;
        }
        const auto typeColon = text.find(':', typePos);
        const auto t1 = text.find('"', typeColon);
        const auto t2 = text.find('"', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) {
            return fail("bad type");
        }
        const std::string typeName = text.substr(t1 + 1, t2 - t1 - 1);
        if (!isValidEffectTypeName(typeName)) {
            return fail("unknown effect type");
        }

        EffectState st;
        st.type = effectTypeFromName(typeName);

        const auto bypassPos = text.find("\"bypassed\"", t2);
        if (bypassPos == std::string::npos) {
            return fail("missing bypassed");
        }
        const auto bypassColon = text.find(':', bypassPos);
        const auto bypassVal = trim(text.substr(bypassColon + 1, 8));
        st.bypassed = bypassVal.rfind("true", 0) == 0;

        const auto paramsPos = text.find("\"parameters\"", bypassPos);
        if (paramsPos == std::string::npos) {
            return fail("missing parameters");
        }
        const auto brace1 = text.find('{', paramsPos);
        const auto brace2 = text.find('}', brace1);
        if (brace1 == std::string::npos || brace2 == std::string::npos) {
            return fail("bad parameters");
        }
        const std::string body = text.substr(brace1 + 1, brace2 - brace1 - 1);
        std::stringstream ss(body);
        std::string token;
        while (std::getline(ss, token, ',')) {
            token = trim(token);
            if (token.empty()) {
                continue;
            }
            const auto colon = token.find(':');
            if (colon == std::string::npos) {
                continue;
            }
            std::string key = trim(token.substr(0, colon));
            std::string val = trim(token.substr(colon + 1));
            if (!key.empty() && key.front() == '"') {
                key = key.substr(1);
            }
            if (!key.empty() && key.back() == '"') {
                key.pop_back();
            }
            try {
                const int id = std::stoi(key);
                const float value = std::stof(val);
                st.parameters[id] = value;
            } catch (...) {
                return fail("bad parameter entry");
            }
        }

        out.effects.push_back(std::move(st));
        search = brace2 + 1;
    }

    if (static_cast<int>(out.effects.size()) != kNumSlots) {
        return fail("expected 10 effects");
    }
    return true;
}

} // namespace

bool PresetBank::saveToFile(const Preset& preset, const std::string& path, std::string* error) const
{
    std::ofstream out(path);
    if (!out) {
        if (error) {
            *error = "failed to open for write: " + path;
        }
        return false;
    }
    writeJson(out, preset);
    return true;
}

bool PresetBank::loadFromFile(const std::string& path, std::string* error)
{
    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "failed to open: " + path;
        }
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    Preset p;
    if (!parsePresetJson(ss.str(), p, error)) {
        return false;
    }
    presets_.push_back(std::move(p));
    return true;
}

int PresetBank::loadDirectory(const std::string& dir)
{
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) {
        return 0;
    }
    int count = 0;
    while (dirent* ent = readdir(d)) {
        const std::string name = ent->d_name;
        if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
            if (loadFromFile(dir + "/" + name)) {
                ++count;
            }
        }
    }
    closedir(d);
    return count;
}

} // namespace preset
