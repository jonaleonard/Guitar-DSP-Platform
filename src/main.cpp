#include "audio/AudioEngine.h"
#include "dsp/EffectGraph.h"
#include "dsp/GainEffect.h"

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
constexpr int kGainSlot = 0;

struct AudioApp {
    dsp::EffectGraph graph;
    std::array<float, kMaxBlockFrames> mono{};
    std::atomic<bool> running{true};
};

void printHelp()
{
    std::cout << "\nCommands (type then Enter):\n"
              << "  g <value>   set gain (0.0 .. 2.0), e.g. g 0.5\n"
              << "  a           Phase 3 check: rapid gain 0↔1 every 10ms for 5s\n"
              << "  b           toggle bypass on the Gain effect\n"
              << "  s           print status (gain, bypass, xruns)\n"
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
    // Volt for guitar input; MacBook speakers (system default) for monitoring.
    config.preferSameDeviceOutput = false;
    config.useDefaultOutputDevice = true;
    config.scheduleRealtime = true;

    auto app = std::make_shared<AudioApp>();

    audio::AudioEngine engine(config);

    auto gain = std::make_unique<dsp::GainEffect>();
    gain->setParameter(dsp::GainEffect::kGain, 1.0f);

    app->graph.prepare(static_cast<double>(config.sampleRate),
                       static_cast<int>(config.bufferFrames));
    if (!app->graph.insert(std::move(gain), 0)) {
        std::cerr << "Failed to insert Gain effect.\n";
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

            // Graph processes mono in-place; guitar input is mono.
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

    // Re-prepare against the actual negotiated buffer size.
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
    std::cout << "Input channels: " << engine.inputChannels()
              << ", output channels: " << engine.outputChannels() << "\n";
    std::cout << "Phase 3: smoothed Gain — guitar → graph → Mac speakers.\n";
    std::cout << "Tip: type 'a' while holding a note to verify no zipper clicks.\n";
    std::cout << "If crackling (I/O), raise bufferFrames or use Volt headphones.\n";

    printHelp();

    bool bypassed = false;
    std::atomic<float> gainValue{1.0f};
    std::atomic<bool> automationRunning{false};

    // Periodic xrun / reclaim reporter on a low-priority side thread.
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

        if (cmd == "a" || cmd == "auto" || cmd == "automate") {
            if (automationRunning.exchange(true)) {
                std::cout << "Automation already running.\n";
                continue;
            }

            std::cout << "Rapid gain automation 0↔1 every 10ms for 5 seconds...\n"
                      << "Hold a sustained note / chord and listen for zipper clicks "
                         "(should be none).\n";

            std::thread([app, &automationRunning, &gainValue]() {
                constexpr auto kInterval = std::chrono::milliseconds(10);
                constexpr auto kDuration = std::chrono::seconds(5);
                const auto start = std::chrono::steady_clock::now();
                float value = 0.0f;

                while (std::chrono::steady_clock::now() - start < kDuration) {
                    value = (value < 0.5f) ? 1.0f : 0.0f;
                    gainValue.store(value, std::memory_order_relaxed);
                    app->graph.setParameter(kGainSlot, dsp::GainEffect::kGain, value);
                    std::this_thread::sleep_for(kInterval);
                }

                gainValue.store(1.0f, std::memory_order_relaxed);
                app->graph.setParameter(kGainSlot, dsp::GainEffect::kGain, 1.0f);
                automationRunning.store(false);
                std::cout << "Automation done. Gain restored to 1.0\n";
            }).detach();
            continue;
        }

        if (cmd == "b" || cmd == "bypass") {
            bypassed = !bypassed;
            if (!app->graph.setBypassed(kGainSlot, bypassed)) {
                std::cerr << "Failed to queue bypass command.\n";
                continue;
            }
            std::cout << "Gain bypass: " << (bypassed ? "ON (dry)" : "OFF (gain active)") << "\n";
            continue;
        }

        if (cmd == "g" || cmd == "gain") {
            float value = gainValue.load(std::memory_order_relaxed);
            if (!(iss >> value)) {
                std::cerr << "Usage: g <0.0..2.0>\n";
                continue;
            }
            if (value < 0.0f) {
                value = 0.0f;
            }
            if (value > 2.0f) {
                value = 2.0f;
            }
            gainValue.store(value, std::memory_order_relaxed);
            if (!app->graph.setParameter(kGainSlot, dsp::GainEffect::kGain, value)) {
                std::cerr << "Failed to queue gain command.\n";
                continue;
            }
            std::cout << "Gain set to " << value << " (smoothed ~20ms)\n";
            continue;
        }

        if (cmd == "s" || cmd == "status") {
            std::cout << "gain=" << gainValue.load(std::memory_order_relaxed)
                      << " bypass=" << (bypassed ? "on" : "off")
                      << " effects=" << app->graph.size()
                      << " overflows=" << engine.inputOverflowCount()
                      << " underflows=" << engine.outputUnderflowCount() << "\n";
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
