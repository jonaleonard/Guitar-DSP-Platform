#include "audio/AudioEngine.h"
#include "dsp/AmpSimEffect.h"
#include "dsp/CabinetEffect.h"
#include "dsp/ChorusEffect.h"
#include "dsp/CompressorEffect.h"
#include "dsp/DelayEffect.h"
#include "dsp/EffectGraph.h"
#include "dsp/EqualizerEffect.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/OverdriveEffect.h"
#include "dsp/ReverbEffect.h"
#include "dsp/SyntheticIr.h"
#include "preset/Preset.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {

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
    kGain = 9,
    kNumSlots = preset::kNumSlots
};

struct AudioApp {
    dsp::EffectGraph graph;
    std::array<float, kMaxBlockFrames> mono{};
    std::atomic<bool> running{true};
    std::array<bool, kNumSlots> bypassed{};
    preset::SoftMute mute{};
    std::atomic<bool> presetBusy{false};
    std::atomic<int> currentPreset{-1};
};

void printHelp()
{
    std::cout
        << "\n=== Presets (Phase 7) ===\n"
        << "  p              list presets\n"
        << "  p <n|name>     load preset (e.g. p 1  or  p blues)\n"
        << "  n  or  ]       next preset\n"
        << "  b  or  [       previous preset\n"
        << "  clean          load Clean preset\n\n"
        << "=== Manual edits (auto-enables that FX) ===\n"
        << "  gt/ct/cr/cm/d/dm/el/em/eh/ap/ad/am/cx/chr/chd/chm/dt/df/dx/rr/rd/rx/g\n"
        << "  bg bc bd be ba bb bch bdl br bn   toggle bypass\n"
        << "  s / h / q\n\n";
}

void setBypass(AudioApp& app, const int slot, const bool bypassed, const char* name)
{
    app.bypassed[static_cast<std::size_t>(slot)] = bypassed;
    app.graph.setBypassed(slot, bypassed);
    if (name != nullptr) {
        std::cout << name << (bypassed ? " bypassed\n" : " enabled\n");
    }
}

void enable(AudioApp& app, const int slot, const char* name)
{
    if (app.bypassed[static_cast<std::size_t>(slot)]) {
        setBypass(app, slot, false, name);
    }
}

void listPresets(const preset::PresetBank& bank, const int current)
{
    std::cout << "Presets:\n";
    for (int i = 0; i < bank.size(); ++i) {
        const auto* p = bank.at(i);
        std::cout << "  " << i << ": " << p->name << (i == current ? "  <--" : "") << "\n";
    }
    std::cout << "Switch: p <n|name> | n/] next | b/[ prev\n";
}

bool loadPresetIndex(AudioApp& app,
                     const preset::PresetBank& bank,
                     const int index,
                     const double sampleRate)
{
    const preset::Preset* preset = bank.at(index);
    if (preset == nullptr) {
        std::cerr << "No preset at index " << index << "\n";
        return false;
    }
    if (app.presetBusy.exchange(true)) {
        std::cout << "Preset switch already in progress...\n";
        return false;
    }

    std::cout << "Loading preset [" << index << "] " << preset->name << " ...\n";

    std::thread([&app, &bank, index, sampleRate, name = preset->name]() {
        const int fadeOut = std::max(1, static_cast<int>(std::lround(0.02 * sampleRate)));
        const int fadeIn = std::max(1, static_cast<int>(std::lround(0.03 * sampleRate)));

        app.mute.startFade(0.0f, fadeOut);
        std::this_thread::sleep_for(std::chrono::milliseconds(35));

        const preset::Preset* p = bank.at(index);
        if (p != nullptr) {
            bank.apply(*p, app.graph, app.bypassed);
            app.currentPreset.store(index);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        app.mute.startFade(1.0f, fadeIn);
        app.presetBusy.store(false);
        std::cout << "Loaded: " << name << "  (n/] next, b/[ prev, p list)\n";
    }).detach();

    return true;
}

std::string presetsDir()
{
    // Prefer repo presets/ next to binary's ../../presets or cwd/presets
    namespace fs = std::filesystem;
    const fs::path candidates[] = {
        fs::current_path() / "presets",
        fs::current_path() / ".." / "presets",
        fs::current_path() / ".." / ".." / "presets",
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            return fs::weakly_canonical(p, ec).string();
        }
    }
    const fs::path created = fs::current_path() / "presets";
    std::error_code ec;
    fs::create_directories(created, ec);
    return created.string();
}

} // namespace

int main()
{
    audio::AudioEngineConfig config;
    config.inputDeviceName = "Volt";
    config.sampleRate = 48000;
    config.bufferFrames = 512;
    config.inputChannels = 1;
    config.outputChannels = 2;
    config.minimizeLatency = false;
    config.preferSameDeviceOutput = false;
    config.useDefaultOutputDevice = true;
    config.scheduleRealtime = true;

    auto app = std::make_shared<AudioApp>();
    app->bypassed.fill(true);
    app->bypassed[kGain] = false;

    preset::PresetBank bank;
    bank.loadFactoryPresets();

    const std::string dir = presetsDir();
    // Write factory JSON files for inspection / hand-editing.
    for (int i = 0; i < bank.size(); ++i) {
        const auto* p = bank.at(i);
        const std::string path = dir + "/" + p->name + ".json";
        bank.saveToFile(*p, path);
    }
    std::cout << "Factory presets written to: " << dir << "\n";

    audio::AudioEngine engine(config);
    app->graph.prepare(static_cast<double>(config.sampleRate),
                       static_cast<int>(config.bufferFrames));

    auto gate = std::make_unique<dsp::NoiseGateEffect>();
    auto comp = std::make_unique<dsp::CompressorEffect>();
    auto drive = std::make_unique<dsp::OverdriveEffect>();
    auto eq = std::make_unique<dsp::EqualizerEffect>();
    auto amp = std::make_unique<dsp::AmpSimEffect>();
    auto cab = std::make_unique<dsp::CabinetEffect>();
    const auto ir = dsp::makeSyntheticCabIr(2048, config.sampleRate);
    if (!cab->loadImpulseResponse(ir.data(), static_cast<int>(ir.size()))) {
        std::cerr << "Failed to load cabinet IR.\n";
        return 1;
    }
    auto chorus = std::make_unique<dsp::ChorusEffect>();
    auto delay = std::make_unique<dsp::DelayEffect>();
    auto reverb = std::make_unique<dsp::ReverbEffect>();
    auto gain = std::make_unique<dsp::GainEffect>();

    if (!app->graph.insert(std::move(gate), kGate) || !app->graph.insert(std::move(comp), kComp) ||
        !app->graph.insert(std::move(drive), kDrive) || !app->graph.insert(std::move(eq), kEq) ||
        !app->graph.insert(std::move(amp), kAmp) || !app->graph.insert(std::move(cab), kCab) ||
        !app->graph.insert(std::move(chorus), kChorus) ||
        !app->graph.insert(std::move(delay), kDelay) ||
        !app->graph.insert(std::move(reverb), kReverb) ||
        !app->graph.insert(std::move(gain), kGain)) {
        std::cerr << "Failed to build graph.\n";
        return 1;
    }
    app->graph.flushCommands();

    // Start on Clean preset (no soft-mute needed before audio starts).
    bank.apply(*bank.at(0), app->graph, app->bypassed);
    app->graph.flushCommands();
    app->currentPreset.store(0);

    engine.setProcessBlockCallback(
        [app](const float* input,
              float* output,
              const int numFrames,
              const int inputChannels,
              const int outputChannels) {
            if (input == nullptr || output == nullptr || numFrames <= 0) {
                return;
            }
            const int frames = std::min(numFrames, kMaxBlockFrames);
            if (inputChannels == 1) {
                for (int i = 0; i < frames; ++i) {
                    app->mono[static_cast<std::size_t>(i)] = input[i];
                }
            } else {
                for (int i = 0; i < frames; ++i) {
                    app->mono[static_cast<std::size_t>(i)] = input[i * inputChannels];
                }
            }

            app->graph.process(app->mono.data(), frames);

            for (int i = 0; i < frames; ++i) {
                const float g = app->mute.nextGain();
                const float s = app->mono[static_cast<std::size_t>(i)] * g;
                if (outputChannels == 1) {
                    output[i] = s;
                } else {
                    output[i * outputChannels] = s;
                    output[i * outputChannels + 1] = s;
                    for (int c = 2; c < outputChannels; ++c) {
                        output[i * outputChannels + c] = 0.0f;
                    }
                }
            }
        });

    if (!engine.start()) {
        return 1;
    }
    app->graph.prepare(static_cast<double>(engine.sampleRate()),
                       static_cast<int>(engine.bufferFrames()));
    // Re-apply Clean after prepare resets filter state.
    bank.apply(*bank.at(0), app->graph, app->bypassed);

    std::cout << "Input: " << engine.inputDeviceName() << "\n";
    std::cout << "Output: " << engine.outputDeviceName() << "\n";
    std::cout << "Phase 7: presets ready. Starting on Clean (dry).\n";
    listPresets(bank, 0);
    printHelp();

    std::atomic<float> gainValue{1.0f};

    std::thread statusThread([app, &engine]() {
        while (app->running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!app->running.load()) {
                break;
            }
            app->graph.reclaimRetiredEffects();
            const auto o = engine.inputOverflowCount();
            const auto u = engine.outputUnderflowCount();
            if (o > 0 || u > 0) {
                std::cout << "[xrun] overflows=" << o << " underflows=" << u << "\n";
            }
        }
    });

    const double sr = static_cast<double>(engine.sampleRate());

    std::string line;
    while (app->running.load() && std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        auto rd = [&](float& dest, float lo, float hi) -> bool {
            float v = dest;
            if (!(iss >> v)) {
                return false;
            }
            dest = std::clamp(v, lo, hi);
            return true;
        };

        if (cmd == "q" || cmd == "quit") {
            break;
        }
        if (cmd == "h" || cmd == "help") {
            printHelp();
            listPresets(bank, app->currentPreset.load());
            continue;
        }

        // --- Preset switching ---
        if (cmd == "p" || cmd == "preset") {
            std::string arg;
            if (!(iss >> arg)) {
                listPresets(bank, app->currentPreset.load());
                continue;
            }
            int idx = -1;
            try {
                idx = std::stoi(arg);
            } catch (...) {
                idx = bank.findByName(arg);
            }
            if (idx < 0) {
                std::cerr << "Unknown preset \"" << arg << "\"\n";
                listPresets(bank, app->currentPreset.load());
                continue;
            }
            loadPresetIndex(*app, bank, idx, sr);
            continue;
        }
        if (cmd == "n" || cmd == "]" || cmd == "next") {
            const int cur = std::max(0, app->currentPreset.load());
            const int next = (cur + 1) % bank.size();
            loadPresetIndex(*app, bank, next, sr);
            continue;
        }
        if (cmd == "[" || cmd == "prev" || cmd == "bp") {
            // note: plain "b" is also previous — but "b" alone might conflict; use [ and bp
            const int cur = std::max(0, app->currentPreset.load());
            const int prev = (cur - 1 + bank.size()) % bank.size();
            loadPresetIndex(*app, bank, prev, sr);
            continue;
        }
        if (cmd == "b") {
            // previous preset (easy left-hand key)
            const int cur = std::max(0, app->currentPreset.load());
            const int prev = (cur - 1 + bank.size()) % bank.size();
            loadPresetIndex(*app, bank, prev, sr);
            continue;
        }
        if (cmd == "clean") {
            loadPresetIndex(*app, bank, 0, sr);
            continue;
        }

        auto toggle = [&](int slot, const char* name) {
            setBypass(*app, slot, !app->bypassed[static_cast<std::size_t>(slot)], name);
        };
        if (cmd == "bg") {
            toggle(kGate, "Gate");
            continue;
        }
        if (cmd == "bc") {
            toggle(kComp, "Comp");
            continue;
        }
        if (cmd == "bd") {
            toggle(kDrive, "Drive");
            continue;
        }
        if (cmd == "be") {
            toggle(kEq, "EQ");
            continue;
        }
        if (cmd == "ba") {
            toggle(kAmp, "Amp");
            continue;
        }
        if (cmd == "bb") {
            toggle(kCab, "Cab");
            continue;
        }
        if (cmd == "bch") {
            toggle(kChorus, "Chorus");
            continue;
        }
        if (cmd == "bdl") {
            toggle(kDelay, "Delay");
            continue;
        }
        if (cmd == "br") {
            toggle(kReverb, "Reverb");
            continue;
        }
        if (cmd == "bn") {
            toggle(kGain, "Gain");
            continue;
        }

        // Keep a small set of useful manual edits
        float tmp = 0.0f;
        if (cmd == "d" && rd(tmp, 1.0f, 25.0f)) {
            enable(*app, kDrive, "Drive");
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kDrive, tmp);
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kMix, 0.7f);
            std::cout << "Drive " << tmp << "\n";
            continue;
        }
        if (cmd == "ad" && rd(tmp, 1.0f, 25.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kDrive, tmp);
            std::cout << "Amp drive " << tmp << "\n";
            continue;
        }
        if (cmd == "cx" && rd(tmp, 0.0f, 1.0f)) {
            enable(*app, kCab, "Cab");
            app->graph.setParameter(kCab, dsp::CabinetEffect::kMix, tmp);
            std::cout << "Cab mix " << tmp << "\n";
            continue;
        }
        if (cmd == "dx" && rd(tmp, 0.0f, 1.0f)) {
            enable(*app, kDelay, "Delay");
            app->graph.setParameter(kDelay, dsp::DelayEffect::kMix, tmp);
            std::cout << "Delay mix " << tmp << "\n";
            continue;
        }
        if (cmd == "rx" && rd(tmp, 0.0f, 1.0f)) {
            enable(*app, kReverb, "Reverb");
            app->graph.setParameter(kReverb, dsp::ReverbEffect::kMix, tmp);
            std::cout << "Reverb mix " << tmp << "\n";
            continue;
        }
        if (cmd == "chm" && rd(tmp, 0.0f, 1.0f)) {
            enable(*app, kChorus, "Chorus");
            app->graph.setParameter(kChorus, dsp::ChorusEffect::kMix, tmp);
            std::cout << "Chorus mix " << tmp << "\n";
            continue;
        }
        if (cmd == "g" && rd(tmp, 0.0f, 2.0f)) {
            gainValue.store(tmp);
            enable(*app, kGain, nullptr);
            app->graph.setParameter(kGain, dsp::GainEffect::kGain, tmp);
            std::cout << "Gain " << tmp << "\n";
            continue;
        }
        if (cmd == "s") {
            const int cur = app->currentPreset.load();
            const auto* p = bank.at(cur);
            std::cout << "preset=" << (p ? p->name : "?") << " (" << cur << ") enabled:";
            const char* names[] = {"gate", "comp", "drive", "eq", "amp", "cab",
                                   "chorus", "delay", "reverb", "gain"};
            for (int i = 0; i < kNumSlots; ++i) {
                if (!app->bypassed[static_cast<std::size_t>(i)]) {
                    std::cout << " " << names[i];
                }
            }
            std::cout << "\n";
            continue;
        }

        std::cerr << "Unknown command. Type h for help, p to list presets, n/b to switch.\n";
    }

    app->running.store(false);
    if (statusThread.joinable()) {
        statusThread.join();
    }
    // Wait briefly if a preset thread is mid-switch
    for (int i = 0; i < 50 && app->presetBusy.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    engine.stop();
    app->graph.reclaimRetiredEffects();
    return 0;
}
