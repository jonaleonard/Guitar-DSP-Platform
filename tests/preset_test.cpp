#include "preset/Preset.h"

#include "dsp/EffectGraph.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/CompressorEffect.h"
#include "dsp/OverdriveEffect.h"
#include "dsp/EqualizerEffect.h"
#include "dsp/AmpSimEffect.h"
#include "dsp/CabinetEffect.h"
#include "dsp/ChorusEffect.h"
#include "dsp/DelayEffect.h"
#include "dsp/ReverbEffect.h"
#include "dsp/SyntheticIr.h"

#include <filesystem>
#include <iostream>
#include <memory>

int main()
{
    preset::PresetBank bank;
    bank.loadFactoryPresets();

    if (bank.size() != 5) {
        std::cerr << "Expected 5 factory presets, got " << bank.size() << "\n";
        return 1;
    }

    const char* names[] = {"Clean", "Blues", "Crunch", "Metal", "Ambient"};
    for (int i = 0; i < 5; ++i) {
        if (bank.at(i)->name != names[i]) {
            std::cerr << "Preset name mismatch at " << i << "\n";
            return 1;
        }
        if (static_cast<int>(bank.at(i)->effects.size()) != preset::kNumSlots) {
            std::cerr << "Preset " << names[i] << " has wrong slot count\n";
            return 1;
        }
    }
    std::cout << "PASS: factory presets present\n";

    // Round-trip JSON
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "guitar_dsp_preset_test.json";
    if (!bank.saveToFile(*bank.at(1), tmp.string())) {
        std::cerr << "Failed to save preset JSON\n";
        return 1;
    }
    preset::PresetBank loaded;
    std::string err;
    if (!loaded.loadFromFile(tmp.string(), &err)) {
        std::cerr << "Failed to load preset JSON: " << err << "\n";
        return 1;
    }
    if (loaded.at(0)->name != "Blues") {
        std::cerr << "Loaded name mismatch\n";
        return 1;
    }
    std::cout << "PASS: JSON save/load round-trip\n";
    fs::remove(tmp);

    // Apply to a live graph
    dsp::EffectGraph graph;
    graph.prepare(48000.0, 256);
    graph.insert(std::make_unique<dsp::NoiseGateEffect>(), 0);
    graph.insert(std::make_unique<dsp::CompressorEffect>(), 1);
    graph.insert(std::make_unique<dsp::OverdriveEffect>(), 2);
    graph.insert(std::make_unique<dsp::EqualizerEffect>(), 3);
    graph.insert(std::make_unique<dsp::AmpSimEffect>(), 4);
    auto cab = std::make_unique<dsp::CabinetEffect>();
    const auto ir = dsp::makeSyntheticCabIr(512, 48000);
    cab->loadImpulseResponse(ir.data(), static_cast<int>(ir.size()));
    graph.insert(std::move(cab), 5);
    graph.insert(std::make_unique<dsp::ChorusEffect>(), 6);
    graph.insert(std::make_unique<dsp::DelayEffect>(), 7);
    graph.insert(std::make_unique<dsp::ReverbEffect>(), 8);
    graph.insert(std::make_unique<dsp::GainEffect>(), 9);
    graph.flushCommands();

    std::array<bool, preset::kNumSlots> bypass{};
    if (!bank.apply(*bank.at(0), graph, bypass)) {
        std::cerr << "Failed to apply Clean\n";
        return 1;
    }
    graph.flushCommands();
    // Clean: smooth chain (comp/eq/amp/cab/gain) — no grit FX
    int enabled = 0;
    for (bool b : bypass) {
        if (!b) {
            ++enabled;
        }
    }
    if (enabled < 4 || bypass[1] || bypass[4] || bypass[9]) {
        std::cerr << "Clean should enable Comp/Amp/Gain (smooth clean chain)\n";
        return 1;
    }
    std::cout << "PASS: Clean applies smooth chain\n";

    if (!bank.apply(*bank.at(3), graph, bypass)) {
        std::cerr << "Failed to apply Metal\n";
        return 1;
    }
    graph.flushCommands();
    if (bypass[4] || bypass[5]) {
        std::cerr << "Metal should enable Amp and Cab\n";
        return 1;
    }
    std::cout << "PASS: Metal enables Amp+Cab\n";

    // Soft mute smoke
    preset::SoftMute mute;
    mute.startFade(0.0f, 10);
    float last = 1.0f;
    for (int i = 0; i < 10; ++i) {
        last = mute.nextGain();
    }
    if (last > 0.05f) {
        std::cerr << "SoftMute failed to reach ~0\n";
        return 1;
    }
    mute.startFade(1.0f, 10);
    for (int i = 0; i < 10; ++i) {
        last = mute.nextGain();
    }
    if (last < 0.95f) {
        std::cerr << "SoftMute failed to reach ~1\n";
        return 1;
    }
    std::cout << "PASS: SoftMute fade\n";

    std::cout << "Preset tests OK.\n";
    return 0;
}
