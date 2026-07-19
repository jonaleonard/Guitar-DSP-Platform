#include "audio/AudioEngine.h"
#include "dsp/CompressorEffect.h"
#include "dsp/EffectGraph.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/OverdriveEffect.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
    kGain = 3
};

struct AudioApp {
    dsp::EffectGraph graph;
    std::array<float, kMaxBlockFrames> mono{};
    std::atomic<bool> running{true};
};

void printHelp()
{
    std::cout << "\nPhase 4 chain: Gate → Comp → Drive → Gain\n"
              << "Commands (type then Enter):\n"
              << "  gt <db>     gate threshold dB (-80..0)\n"
              << "  ct <db>     compressor threshold dB\n"
              << "  cr <n>      compressor ratio (e.g. 4)\n"
              << "  cm <db>     compressor makeup dB\n"
              << "  d <n>       overdrive drive (1..25)\n"
              << "  dm <0..1>   overdrive mix\n"
              << "  g <n>       post gain (0..2)\n"
              << "  bg/bc/bd/bn toggle bypass gate/comp/drive/gain\n"
              << "  a           rapid post-gain automation (Phase 3 check)\n"
              << "  s           status\n"
              << "  h           help\n"
              << "  q           quit\n\n";
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
    gate->setParameter(dsp::NoiseGateEffect::kAttackMs, 2.0f);
    gate->setParameter(dsp::NoiseGateEffect::kReleaseMs, 80.0f);

    auto comp = std::make_unique<dsp::CompressorEffect>();
    comp->setParameter(dsp::CompressorEffect::kThresholdDb, -18.0f);
    comp->setParameter(dsp::CompressorEffect::kRatio, 3.0f);
    comp->setParameter(dsp::CompressorEffect::kAttackMs, 8.0f);
    comp->setParameter(dsp::CompressorEffect::kReleaseMs, 120.0f);
    comp->setParameter(dsp::CompressorEffect::kMakeupDb, 3.0f);

    auto drive = std::make_unique<dsp::OverdriveEffect>();
    drive->setParameter(dsp::OverdriveEffect::kDrive, 3.0f);
    drive->setParameter(dsp::OverdriveEffect::kMix, 0.7f);
    drive->setParameter(dsp::OverdriveEffect::kOutput, 0.8f);

    auto gain = std::make_unique<dsp::GainEffect>();
    gain->setParameter(dsp::GainEffect::kGain, 1.0f);

    if (!app->graph.insert(std::move(gate), kGate) ||
        !app->graph.insert(std::move(comp), kComp) ||
        !app->graph.insert(std::move(drive), kDrive) ||
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

            if (outputChannels == 1) {
                for (int i = 0; i < frames; ++i) {
                    output[i] = app->mono[static_cast<std::size_t>(i)];
                }
            } else {
                for (int i = 0; i < frames; ++i) {
                    const float sample = app->mono[static_cast<std::size_t>(i)];
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

    const double latencyMs =
        (1000.0 * static_cast<double>(engine.streamLatencySamples())) /
        static_cast<double>(engine.sampleRate());

    std::cout << "Input device: " << engine.inputDeviceName() << "\n";
    std::cout << "Output device: " << engine.outputDeviceName() << "\n";
    std::cout << "Sample rate: " << engine.sampleRate() << " Hz\n";
    std::cout << "Buffer frames: " << engine.bufferFrames() << "\n";
    std::cout << "Stream latency: " << engine.streamLatencySamples() << " samples ("
              << latencyMs << " ms)\n";
    std::cout << "Phase 4: Gate → Comp → Drive → Gain → Mac speakers.\n";

    printHelp();

    bool bypassGate = false;
    bool bypassComp = false;
    bool bypassDrive = false;
    bool bypassGain = false;
    std::atomic<float> gainValue{1.0f};
    std::atomic<bool> automationRunning{false};

    float gateThresh = -50.0f;
    float compThresh = -18.0f;
    float compRatio = 3.0f;
    float compMakeup = 3.0f;
    float driveAmt = 3.0f;
    float driveMix = 0.7f;

    std::thread statusThread([app, &engine]() {
        while (app->running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!app->running.load(std::memory_order_relaxed)) {
                break;
            }
            app->graph.reclaimRetiredEffects();
            const auto overflows = engine.inputOverflowCount();
            const auto underflows = engine.outputUnderflowCount();
            if (overflows > 0 || underflows > 0) {
                std::cout << "[xrun] input overflows=" << overflows
                          << " output underflows=" << underflows << "\n";
            }
        }
    });

    std::string line;
    while (app->running.load(std::memory_order_relaxed) && std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "q" || cmd == "quit" || cmd == "exit") {
            break;
        }
        if (cmd == "h" || cmd == "help") {
            printHelp();
            continue;
        }

        auto toggleBypass = [&](const int slot, bool& flag, const char* name) {
            flag = !flag;
            app->graph.setBypassed(slot, flag);
            std::cout << name << " bypass: " << (flag ? "ON" : "OFF") << "\n";
        };

        if (cmd == "bg") {
            toggleBypass(kGate, bypassGate, "Gate");
            continue;
        }
        if (cmd == "bc") {
            toggleBypass(kComp, bypassComp, "Comp");
            continue;
        }
        if (cmd == "bd") {
            toggleBypass(kDrive, bypassDrive, "Drive");
            continue;
        }
        if (cmd == "bn" || cmd == "b") {
            toggleBypass(kGain, bypassGain, "Gain");
            continue;
        }

        if (cmd == "a" || cmd == "auto") {
            if (automationRunning.exchange(true)) {
                std::cout << "Automation already running.\n";
                continue;
            }
            std::cout << "Rapid post-gain 0↔1 every 10ms for 5s...\n";
            std::thread([app, &automationRunning, &gainValue]() {
                constexpr auto kInterval = std::chrono::milliseconds(10);
                constexpr auto kDuration = std::chrono::seconds(5);
                const auto start = std::chrono::steady_clock::now();
                float value = 0.0f;
                while (std::chrono::steady_clock::now() - start < kDuration) {
                    value = (value < 0.5f) ? 1.0f : 0.0f;
                    gainValue.store(value, std::memory_order_relaxed);
                    app->graph.setParameter(kGain, dsp::GainEffect::kGain, value);
                    std::this_thread::sleep_for(kInterval);
                }
                gainValue.store(1.0f, std::memory_order_relaxed);
                app->graph.setParameter(kGain, dsp::GainEffect::kGain, 1.0f);
                automationRunning.store(false);
                std::cout << "Automation done. Gain restored to 1.0\n";
            }).detach();
            continue;
        }

        if (cmd == "gt") {
            float v = gateThresh;
            if (!(iss >> v)) {
                std::cerr << "Usage: gt <-80..0>\n";
                continue;
            }
            gateThresh = std::clamp(v, -80.0f, 0.0f);
            app->graph.setParameter(kGate, dsp::NoiseGateEffect::kThresholdDb, gateThresh);
            std::cout << "Gate threshold " << gateThresh << " dB\n";
            continue;
        }
        if (cmd == "ct") {
            float v = compThresh;
            if (!(iss >> v)) {
                std::cerr << "Usage: ct <db>\n";
                continue;
            }
            compThresh = std::clamp(v, -60.0f, 0.0f);
            app->graph.setParameter(kComp, dsp::CompressorEffect::kThresholdDb, compThresh);
            std::cout << "Comp threshold " << compThresh << " dB\n";
            continue;
        }
        if (cmd == "cr") {
            float v = compRatio;
            if (!(iss >> v)) {
                std::cerr << "Usage: cr <ratio>\n";
                continue;
            }
            compRatio = std::max(1.0f, v);
            app->graph.setParameter(kComp, dsp::CompressorEffect::kRatio, compRatio);
            std::cout << "Comp ratio " << compRatio << ":1\n";
            continue;
        }
        if (cmd == "cm") {
            float v = compMakeup;
            if (!(iss >> v)) {
                std::cerr << "Usage: cm <db>\n";
                continue;
            }
            compMakeup = std::clamp(v, -24.0f, 24.0f);
            app->graph.setParameter(kComp, dsp::CompressorEffect::kMakeupDb, compMakeup);
            std::cout << "Comp makeup " << compMakeup << " dB\n";
            continue;
        }
        if (cmd == "d") {
            float v = driveAmt;
            if (!(iss >> v)) {
                std::cerr << "Usage: d <1..25>\n";
                continue;
            }
            driveAmt = std::clamp(v, 1.0f, 25.0f);
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kDrive, driveAmt);
            std::cout << "Drive " << driveAmt << "\n";
            continue;
        }
        if (cmd == "dm") {
            float v = driveMix;
            if (!(iss >> v)) {
                std::cerr << "Usage: dm <0..1>\n";
                continue;
            }
            driveMix = std::clamp(v, 0.0f, 1.0f);
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kMix, driveMix);
            std::cout << "Drive mix " << driveMix << "\n";
            continue;
        }
        if (cmd == "g" || cmd == "gain") {
            float v = gainValue.load(std::memory_order_relaxed);
            if (!(iss >> v)) {
                std::cerr << "Usage: g <0..2>\n";
                continue;
            }
            v = std::clamp(v, 0.0f, 2.0f);
            gainValue.store(v, std::memory_order_relaxed);
            app->graph.setParameter(kGain, dsp::GainEffect::kGain, v);
            std::cout << "Gain " << v << "\n";
            continue;
        }
        if (cmd == "s" || cmd == "status") {
            std::cout << "gateThresh=" << gateThresh << "dB bypass=" << bypassGate
                      << " | compThresh=" << compThresh << "dB ratio=" << compRatio
                      << " makeup=" << compMakeup << " bypass=" << bypassComp
                      << " | drive=" << driveAmt << " mix=" << driveMix
                      << " bypass=" << bypassDrive
                      << " | gain=" << gainValue.load(std::memory_order_relaxed)
                      << " bypass=" << bypassGain
                      << " | xruns in=" << engine.inputOverflowCount()
                      << " out=" << engine.outputUnderflowCount() << "\n";
            continue;
        }

        std::cerr << "Unknown command. Type h for help.\n";
    }

    app->running.store(false, std::memory_order_relaxed);
    if (statusThread.joinable()) {
        statusThread.join();
    }
    engine.stop();
    app->graph.reclaimRetiredEffects();
    return 0;
}
