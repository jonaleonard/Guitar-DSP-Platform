#include "audio/AudioEngine.h"
#include "dsp/AmpSimEffect.h"
#include "dsp/CabinetEffect.h"
#include "dsp/CompressorEffect.h"
#include "dsp/EffectGraph.h"
#include "dsp/EqualizerEffect.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/OverdriveEffect.h"
#include "dsp/SyntheticIr.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kMaxBlockFrames = 4096;

enum Slot : int {
    kGate = 0,
    kComp = 1,
    kDrive = 2,
    kEq = 3,
    kAmp = 4,
    kCab = 5,
    kGain = 6
};

struct AudioApp {
    dsp::EffectGraph graph;
    std::array<float, kMaxBlockFrames> mono{};
    std::atomic<bool> running{true};
};

void printHelp()
{
    std::cout << "\nPhase 5 chain: Gate → Comp → Drive → EQ → Amp → Cab → Gain\n"
              << "Commands:\n"
              << "  gt/ct/cr/cm/d/dm/g     gate/comp/drive/gain (same as Phase 4)\n"
              << "  el/em/eh <db>         EQ low/mid/high gain dB\n"
              << "  ap/ad/am <n>          amp preGain / drive / master\n"
              << "  ab/ami/at/apr <db>    amp bass/mid/treble/presence\n"
              << "  cx <0..1>             cab mix\n"
              << "  bg bc bd be ba bb bn  bypass gate/comp/drive/eq/amp/cab/gain\n"
              << "  a                     rapid post-gain automation (zipper check)\n"
              << "  s / h / q\n\n";
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
    audio::AudioEngine engine(config);

    app->graph.prepare(static_cast<double>(config.sampleRate),
                       static_cast<int>(config.bufferFrames));

    auto gate = std::make_unique<dsp::NoiseGateEffect>();
    gate->setParameter(dsp::NoiseGateEffect::kThresholdDb, -50.0f);

    auto comp = std::make_unique<dsp::CompressorEffect>();
    comp->setParameter(dsp::CompressorEffect::kThresholdDb, -18.0f);
    comp->setParameter(dsp::CompressorEffect::kRatio, 3.0f);
    comp->setParameter(dsp::CompressorEffect::kMakeupDb, 2.0f);

    auto drive = std::make_unique<dsp::OverdriveEffect>();
    drive->setParameter(dsp::OverdriveEffect::kDrive, 2.0f);
    drive->setParameter(dsp::OverdriveEffect::kMix, 0.4f);
    drive->setParameter(dsp::OverdriveEffect::kOutput, 0.85f);

    auto eq = std::make_unique<dsp::EqualizerEffect>();
    eq->setParameter(dsp::EqualizerEffect::kLowGainDb, 1.0f);
    eq->setParameter(dsp::EqualizerEffect::kMidGainDb, -1.0f);
    eq->setParameter(dsp::EqualizerEffect::kHighGainDb, 2.0f);

    auto amp = std::make_unique<dsp::AmpSimEffect>();
    amp->setParameter(dsp::AmpSimEffect::kPreGain, 2.5f);
    amp->setParameter(dsp::AmpSimEffect::kDrive, 5.0f);
    amp->setParameter(dsp::AmpSimEffect::kBassDb, 2.0f);
    amp->setParameter(dsp::AmpSimEffect::kMidDb, 0.0f);
    amp->setParameter(dsp::AmpSimEffect::kTrebleDb, 1.5f);
    amp->setParameter(dsp::AmpSimEffect::kMaster, 0.55f);

    auto cab = std::make_unique<dsp::CabinetEffect>();
    const auto ir = dsp::makeSyntheticCabIr(2048, config.sampleRate);
    if (!cab->loadImpulseResponse(ir.data(), static_cast<int>(ir.size()))) {
        std::cerr << "Failed to load synthetic cabinet IR.\n";
        return 1;
    }
    cab->setParameter(dsp::CabinetEffect::kMix, 1.0f);
    cab->setParameter(dsp::CabinetEffect::kLevel, 1.0f);

    auto gain = std::make_unique<dsp::GainEffect>();
    gain->setParameter(dsp::GainEffect::kGain, 1.0f);

    if (!app->graph.insert(std::move(gate), kGate) || !app->graph.insert(std::move(comp), kComp) ||
        !app->graph.insert(std::move(drive), kDrive) || !app->graph.insert(std::move(eq), kEq) ||
        !app->graph.insert(std::move(amp), kAmp) || !app->graph.insert(std::move(cab), kCab) ||
        !app->graph.insert(std::move(gain), kGain)) {
        std::cerr << "Failed to build effect graph.\n";
        return 1;
    }
    app->graph.flushCommands();

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
                const float sample = app->mono[static_cast<std::size_t>(i)];
                if (outputChannels == 1) {
                    output[i] = sample;
                } else {
                    output[(i * outputChannels)] = sample;
                    output[(i * outputChannels) + 1] = sample;
                    for (int c = 2; c < outputChannels; ++c) {
                        output[(i * outputChannels) + c] = 0.0f;
                    }
                }
            }
        });

    if (!engine.start()) {
        return 1;
    }

    app->graph.prepare(static_cast<double>(engine.sampleRate()),
                       static_cast<int>(engine.bufferFrames()));

    std::cout << "Input: " << engine.inputDeviceName() << "\n";
    std::cout << "Output: " << engine.outputDeviceName() << "\n";
    std::cout << "Sample rate: " << engine.sampleRate()
              << " Hz, buffer: " << engine.bufferFrames() << "\n";
    std::cout << "Phase 5: Gate→Comp→Drive→EQ→Amp→Cab(IR)→Gain\n";
    std::cout << "Cab IR: synthetic 2048-sample IR loaded.\n";
    std::cout << "Zipper fix: exponential gain smoothing (~80ms).\n";

    printHelp();

    bool bypassGate = false, bypassComp = false, bypassDrive = false, bypassEq = false;
    bool bypassAmp = false, bypassCab = false, bypassGain = false;
    std::atomic<float> gainValue{1.0f};
    std::atomic<bool> automationRunning{false};

    float gateThresh = -50.0f, compThresh = -18.0f, compRatio = 3.0f, compMakeup = 2.0f;
    float driveAmt = 2.0f, driveMix = 0.4f;
    float eqLow = 1.0f, eqMid = -1.0f, eqHigh = 2.0f;
    float ampPre = 2.5f, ampDrive = 5.0f, ampMaster = 0.55f;
    float ampBass = 2.0f, ampMid = 0.0f, ampTreble = 1.5f, ampPresence = 0.0f;
    float cabMix = 1.0f;

    std::thread statusThread([app, &engine]() {
        while (app->running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!app->running.load(std::memory_order_relaxed)) {
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

    auto toggle = [&](int slot, bool& flag, const char* name) {
        flag = !flag;
        app->graph.setBypassed(slot, flag);
        std::cout << name << " bypass: " << (flag ? "ON" : "OFF") << "\n";
    };

    std::string line;
    while (app->running.load(std::memory_order_relaxed) && std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "q" || cmd == "quit") {
            break;
        }
        if (cmd == "h" || cmd == "help") {
            printHelp();
            continue;
        }
        if (cmd == "bg") {
            toggle(kGate, bypassGate, "Gate");
            continue;
        }
        if (cmd == "bc") {
            toggle(kComp, bypassComp, "Comp");
            continue;
        }
        if (cmd == "bd") {
            toggle(kDrive, bypassDrive, "Drive");
            continue;
        }
        if (cmd == "be") {
            toggle(kEq, bypassEq, "EQ");
            continue;
        }
        if (cmd == "ba") {
            toggle(kAmp, bypassAmp, "Amp");
            continue;
        }
        if (cmd == "bb") {
            toggle(kCab, bypassCab, "Cab");
            continue;
        }
        if (cmd == "bn" || cmd == "b") {
            toggle(kGain, bypassGain, "Gain");
            continue;
        }

        if (cmd == "a") {
            if (automationRunning.exchange(true)) {
                std::cout << "Automation already running.\n";
                continue;
            }
            std::cout << "Rapid post-gain 0↔1 every 10ms for 5s (should be zipper-free)...\n";
            std::thread([app, &automationRunning, &gainValue]() {
                const auto start = std::chrono::steady_clock::now();
                float value = 0.0f;
                while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
                    value = (value < 0.5f) ? 1.0f : 0.0f;
                    gainValue.store(value, std::memory_order_relaxed);
                    app->graph.setParameter(kGain, dsp::GainEffect::kGain, value);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                gainValue.store(1.0f, std::memory_order_relaxed);
                app->graph.setParameter(kGain, dsp::GainEffect::kGain, 1.0f);
                automationRunning.store(false);
                std::cout << "Automation done.\n";
            }).detach();
            continue;
        }

        auto readFloat = [&](float& dest, float lo, float hi) -> bool {
            float v = dest;
            if (!(iss >> v)) {
                return false;
            }
            dest = std::clamp(v, lo, hi);
            return true;
        };

        if (cmd == "gt" && readFloat(gateThresh, -80.0f, 0.0f)) {
            app->graph.setParameter(kGate, dsp::NoiseGateEffect::kThresholdDb, gateThresh);
            std::cout << "Gate thresh " << gateThresh << " dB\n";
            continue;
        }
        if (cmd == "ct" && readFloat(compThresh, -60.0f, 0.0f)) {
            app->graph.setParameter(kComp, dsp::CompressorEffect::kThresholdDb, compThresh);
            std::cout << "Comp thresh " << compThresh << " dB\n";
            continue;
        }
        if (cmd == "cr" && readFloat(compRatio, 1.0f, 20.0f)) {
            app->graph.setParameter(kComp, dsp::CompressorEffect::kRatio, compRatio);
            std::cout << "Comp ratio " << compRatio << "\n";
            continue;
        }
        if (cmd == "cm" && readFloat(compMakeup, -24.0f, 24.0f)) {
            app->graph.setParameter(kComp, dsp::CompressorEffect::kMakeupDb, compMakeup);
            std::cout << "Comp makeup " << compMakeup << " dB\n";
            continue;
        }
        if (cmd == "d" && readFloat(driveAmt, 1.0f, 25.0f)) {
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kDrive, driveAmt);
            std::cout << "Drive " << driveAmt << "\n";
            continue;
        }
        if (cmd == "dm" && readFloat(driveMix, 0.0f, 1.0f)) {
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kMix, driveMix);
            std::cout << "Drive mix " << driveMix << "\n";
            continue;
        }
        if (cmd == "el" && readFloat(eqLow, -18.0f, 18.0f)) {
            app->graph.setParameter(kEq, dsp::EqualizerEffect::kLowGainDb, eqLow);
            std::cout << "EQ low " << eqLow << " dB\n";
            continue;
        }
        if (cmd == "em" && readFloat(eqMid, -18.0f, 18.0f)) {
            app->graph.setParameter(kEq, dsp::EqualizerEffect::kMidGainDb, eqMid);
            std::cout << "EQ mid " << eqMid << " dB\n";
            continue;
        }
        if (cmd == "eh" && readFloat(eqHigh, -18.0f, 18.0f)) {
            app->graph.setParameter(kEq, dsp::EqualizerEffect::kHighGainDb, eqHigh);
            std::cout << "EQ high " << eqHigh << " dB\n";
            continue;
        }
        if (cmd == "ap" && readFloat(ampPre, 0.0f, 10.0f)) {
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kPreGain, ampPre);
            std::cout << "Amp pre " << ampPre << "\n";
            continue;
        }
        if (cmd == "ad" && readFloat(ampDrive, 1.0f, 25.0f)) {
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kDrive, ampDrive);
            std::cout << "Amp drive " << ampDrive << "\n";
            continue;
        }
        if (cmd == "am" && readFloat(ampMaster, 0.0f, 2.0f)) {
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kMaster, ampMaster);
            std::cout << "Amp master " << ampMaster << "\n";
            continue;
        }
        if (cmd == "ab" && readFloat(ampBass, -12.0f, 12.0f)) {
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kBassDb, ampBass);
            std::cout << "Amp bass " << ampBass << " dB\n";
            continue;
        }
        if (cmd == "ami" && readFloat(ampMid, -12.0f, 12.0f)) {
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kMidDb, ampMid);
            std::cout << "Amp mid " << ampMid << " dB\n";
            continue;
        }
        if (cmd == "at" && readFloat(ampTreble, -12.0f, 12.0f)) {
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kTrebleDb, ampTreble);
            std::cout << "Amp treble " << ampTreble << " dB\n";
            continue;
        }
        if (cmd == "apr" && readFloat(ampPresence, -12.0f, 12.0f)) {
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kPresenceDb, ampPresence);
            std::cout << "Amp presence " << ampPresence << " dB\n";
            continue;
        }
        if (cmd == "cx" && readFloat(cabMix, 0.0f, 1.0f)) {
            app->graph.setParameter(kCab, dsp::CabinetEffect::kMix, cabMix);
            std::cout << "Cab mix " << cabMix << "\n";
            continue;
        }
        if (cmd == "g" || cmd == "gain") {
            float v = gainValue.load();
            if (!(iss >> v)) {
                std::cerr << "Usage: g <0..2>\n";
                continue;
            }
            v = std::clamp(v, 0.0f, 2.0f);
            gainValue.store(v);
            app->graph.setParameter(kGain, dsp::GainEffect::kGain, v);
            std::cout << "Gain " << v << " (smoothed)\n";
            continue;
        }
        if (cmd == "s") {
            std::cout << "eq L/M/H=" << eqLow << "/" << eqMid << "/" << eqHigh
                      << " amp pre/drive/master=" << ampPre << "/" << ampDrive << "/" << ampMaster
                      << " cabMix=" << cabMix << " gain=" << gainValue.load()
                      << " xruns in/out=" << engine.inputOverflowCount() << "/"
                      << engine.outputUnderflowCount() << "\n";
            continue;
        }

        std::cerr << "Unknown command. Type h for help.\n";
    }

    app->running.store(false);
    if (statusThread.joinable()) {
        statusThread.join();
    }
    engine.stop();
    app->graph.reclaimRetiredEffects();
    return 0;
}
