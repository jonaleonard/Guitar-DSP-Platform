#include "audio/AudioEngine.h"
#include "dsp/EffectGraph.h"
#include "dsp/GainEffect.h"
#include "WavWriter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

bool nearlyEqual(const float a, const float b, const float eps = 1.0e-5f)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string outputPath = (argc > 1) ? argv[1] : "gain_graph.wav";

    constexpr unsigned int kSampleRate = 48000;
    constexpr int kBlockSize = 256;
    constexpr float kFrequencyHz = 440.0f;
    constexpr float kAmplitude = 0.5f;
    constexpr float kGain = 0.25f;
    constexpr double kDurationSeconds = 1.0;

    auto graph = std::make_shared<dsp::EffectGraph>();
    graph->prepare(static_cast<double>(kSampleRate), kBlockSize);

    auto gain = std::make_unique<dsp::GainEffect>();
    gain->setParameter(dsp::GainEffect::kGain, kGain);
    if (!graph->insert(std::move(gain), 0)) {
        std::cerr << "Failed to insert Gain.\n";
        return 1;
    }
    graph->flushCommands();

    if (graph->size() != 1) {
        std::cerr << "Expected 1 effect after insert, got " << graph->size() << "\n";
        return 1;
    }

    if (!graph->setBypassed(0, true) || !graph->setBypassed(0, false)) {
        std::cerr << "Failed to queue bypass commands.\n";
        return 1;
    }
    graph->flushCommands();
    if (graph->effectAt(0) == nullptr || graph->effectAt(0)->isBypassed()) {
        std::cerr << "Gain should be active after unbypass.\n";
        return 1;
    }

    std::vector<float> input(static_cast<std::size_t>(kBlockSize), 0.8f);
    std::vector<float> work = input;

    graph->setBypassed(0, true);
    graph->process(work.data(), kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        if (!nearlyEqual(work[static_cast<std::size_t>(i)], 0.8f)) {
            std::cerr << "Bypassed gain should be unity.\n";
            return 1;
        }
    }

    graph->setBypassed(0, false);
    graph->setParameter(0, dsp::GainEffect::kGain, kGain);
    work = input;
    graph->process(work.data(), kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        if (!nearlyEqual(work[static_cast<std::size_t>(i)], 0.8f * kGain)) {
            std::cerr << "Active gain mismatch at sample " << i << "\n";
            return 1;
        }
    }

    auto gain2 = std::make_unique<dsp::GainEffect>();
    gain2->setParameter(dsp::GainEffect::kGain, 2.0f);
    if (!graph->insert(std::move(gain2), 1)) {
        std::cerr << "Failed to insert second Gain.\n";
        return 1;
    }
    graph->flushCommands();
    if (graph->size() != 2) {
        std::cerr << "Expected 2 effects after second insert.\n";
        return 1;
    }

    if (!graph->swap(0, 1)) {
        std::cerr << "Failed to queue swap.\n";
        return 1;
    }
    graph->flushCommands();

    if (!graph->remove(1)) {
        std::cerr << "Failed to queue remove.\n";
        return 1;
    }
    graph->flushCommands();
    graph->reclaimRetiredEffects();
    if (graph->size() != 1) {
        std::cerr << "Expected 1 effect after remove.\n";
        return 1;
    }

    graph->setParameter(0, dsp::GainEffect::kGain, kGain);
    work = input;
    graph->process(work.data(), kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        if (!nearlyEqual(work[static_cast<std::size_t>(i)], 0.8f * kGain)) {
            std::cerr << "Post-remove gain mismatch.\n";
            return 1;
        }
    }

    audio::AudioEngineConfig config;
    config.sampleRate = kSampleRate;
    config.bufferFrames = static_cast<unsigned int>(kBlockSize);
    config.inputChannels = 1;
    config.outputChannels = 2;

    audio::AudioEngine engine(config);
    std::array<float, 4096> monoScratch{};

    engine.setProcessBlockCallback(
        [graph, &monoScratch](const float* in,
                              float* out,
                              const int numFrames,
                              const int /*inCh*/,
                              const int outCh) {
            for (int i = 0; i < numFrames; ++i) {
                monoScratch[static_cast<std::size_t>(i)] = in[i];
            }
            graph->process(monoScratch.data(), numFrames);
            for (int i = 0; i < numFrames; ++i) {
                const float s = monoScratch[static_cast<std::size_t>(i)];
                out[(i * outCh)] = s;
                if (outCh > 1) {
                    out[(i * outCh) + 1] = s;
                }
            }
        });

    const int totalFrames = static_cast<int>(kDurationSeconds * static_cast<double>(kSampleRate));
    std::vector<float> blockIn(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> blockOut(static_cast<std::size_t>(kBlockSize * 2), 0.0f);
    std::vector<float> wavSamples;
    wavSamples.reserve(static_cast<std::size_t>(totalFrames * 2));

    int framesWritten = 0;
    while (framesWritten < totalFrames) {
        const int framesThisBlock = std::min(kBlockSize, totalFrames - framesWritten);
        for (int i = 0; i < framesThisBlock; ++i) {
            const double t =
                static_cast<double>(framesWritten + i) / static_cast<double>(kSampleRate);
            blockIn[static_cast<std::size_t>(i)] =
                kAmplitude *
                static_cast<float>(std::sin(2.0 * kPi * static_cast<double>(kFrequencyHz) * t));
        }

        engine.processOfflineBlock(blockIn.data(), blockOut.data(), framesThisBlock);

        for (int i = 0; i < framesThisBlock; ++i) {
            const float expected = blockIn[static_cast<std::size_t>(i)] * kGain;
            const float left = blockOut[static_cast<std::size_t>(i * 2)];
            const float right = blockOut[static_cast<std::size_t>((i * 2) + 1)];
            if (!nearlyEqual(left, expected) || !nearlyEqual(right, expected)) {
                std::cerr << "Engine+graph mismatch at frame " << (framesWritten + i) << "\n";
                return 1;
            }
            wavSamples.push_back(left);
            wavSamples.push_back(right);
        }
        framesWritten += framesThisBlock;
    }

    try {
        WavWriter::writeFloat32(outputPath, wavSamples, 2, kSampleRate);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    std::cout << "Wrote " << framesWritten << " frames to " << outputPath << "\n";
    std::cout << "Gain graph harness OK (bypass, gain, insert/swap/remove, engine wire).\n";
    return 0;
}
