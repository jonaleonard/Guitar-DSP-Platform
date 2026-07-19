#include "audio/AudioEngine.h"
#include "dsp/EffectGraph.h"
#include "dsp/GainEffect.h"
#include "dsp/SmoothedValue.h"
#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 48000;
constexpr int kBlockSize = 256;
constexpr float kRampTimeMs = 80.0f; // match GainEffect

void settleGraph(dsp::EffectGraph& graph, const int framesToSettle)
{
    std::vector<float> silence(static_cast<std::size_t>(kBlockSize), 0.0f);
    int done = 0;
    while (done < framesToSettle) {
        const int n = std::min(kBlockSize, framesToSettle - done);
        graph.process(silence.data(), n);
        done += n;
    }
}

} // namespace

int main(int argc, char** argv)
{
    const std::string outputPath = (argc > 1) ? argv[1] : "gain_smooth.wav";

    // --- Unit: exponential SmoothedValue has small continuous steps ---
    {
        dsp::SmoothedValue smoother;
        smoother.prepare(static_cast<double>(kSampleRate), kRampTimeMs);
        smoother.reset(0.0f);
        smoother.setTarget(1.0f);

        float prev = smoother.getCurrent();
        float maxDelta = 0.0f;
        const int samples = smoother.settleSamples() * 2;
        for (int i = 0; i < samples; ++i) {
            const float v = smoother.getNext();
            maxDelta = std::max(maxDelta, std::fabs(v - prev));
            prev = v;
        }

        // First step of one-pole toward 1 from 0 is (1-coeff) ≈ 1/(tau*sr)
        const float expectedFirstStep =
            1.0f - std::exp(-1.0f / (kRampTimeMs * 0.001f * static_cast<float>(kSampleRate)));
        if (maxDelta > expectedFirstStep * 1.05f + 1.0e-6f) {
            std::cerr << "SmoothedValue step too large: " << maxDelta
                      << " (expected first step ~" << expectedFirstStep << ")\n";
            return 1;
        }
        if (std::fabs(smoother.getCurrent() - 1.0f) > 5.0e-3f) {
            std::cerr << "SmoothedValue failed to settle near target, got "
                      << smoother.getCurrent() << "\n";
            return 1;
        }
        std::cout << "PASS: exponential smoother maxDelta=" << maxDelta << "\n";
    }

    // --- Integration: rapid gain automation every 10ms ---
    auto graph = std::make_shared<dsp::EffectGraph>();
    graph->prepare(static_cast<double>(kSampleRate), kBlockSize);

    auto gain = std::make_unique<dsp::GainEffect>();
    gain->setRampTimeMs(kRampTimeMs);
    if (!graph->insert(std::move(gain), 0)) {
        std::cerr << "Failed to insert Gain.\n";
        return 1;
    }
    graph->flushCommands();
    graph->setParameter(0, dsp::GainEffect::kGain, 0.0f);
    settleGraph(*graph, static_cast<int>(kSampleRate * 0.5));

    constexpr float kAmplitude = 1.0f;
    constexpr int kSweepIntervalMs = 10;
    constexpr int kSweepIntervalFrames =
        static_cast<int>((kSweepIntervalMs / 1000.0) * static_cast<double>(kSampleRate));
    constexpr double kDurationSeconds = 1.0;
    const int totalFrames = static_cast<int>(kDurationSeconds * static_cast<double>(kSampleRate));

    std::vector<float> block(static_cast<std::size_t>(kBlockSize), kAmplitude);
    std::vector<float> wavSamples;
    wavSamples.reserve(static_cast<std::size_t>(totalFrames));

    float prevOut = 0.0f;
    float maxDelta = 0.0f;
    bool havePrev = false;
    float targetGain = 0.0f;
    int framesUntilFlip = kSweepIntervalFrames;
    int framesDone = 0;

    while (framesDone < totalFrames) {
        if (framesUntilFlip <= 0) {
            targetGain = (targetGain < 0.5f) ? 1.0f : 0.0f;
            graph->setParameter(0, dsp::GainEffect::kGain, targetGain);
            framesUntilFlip = kSweepIntervalFrames;
        }

        const int n = std::min({kBlockSize, totalFrames - framesDone, framesUntilFlip});
        std::fill(block.begin(), block.begin() + n, kAmplitude);
        graph->process(block.data(), n);

        for (int i = 0; i < n; ++i) {
            const float sample = block[static_cast<std::size_t>(i)];
            wavSamples.push_back(sample);
            if (havePrev) {
                maxDelta = std::max(maxDelta, std::fabs(sample - prevOut));
            }
            prevOut = sample;
            havePrev = true;
        }

        framesDone += n;
        framesUntilFlip -= n;
    }

    // Unsmoothed jump ≈ 1.0. Exponential 80ms first step ≈ 0.00026.
    constexpr float kZipperFailThreshold = 0.01f;
    if (maxDelta > kZipperFailThreshold) {
        std::cerr << "Zipper detected: max sample delta=" << maxDelta
                  << " (threshold=" << kZipperFailThreshold << ")\n";
        return 1;
    }

    std::vector<float> stereo;
    stereo.reserve(wavSamples.size() * 2);
    for (float s : wavSamples) {
        stereo.push_back(s);
        stereo.push_back(s);
    }

    try {
        WavWriter::writeFloat32(outputPath, stereo, 2, kSampleRate);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    std::cout << "Rapid gain sweep OK: max sample delta=" << maxDelta << "\n";
    std::cout << "Wrote " << wavSamples.size() << " frames to " << outputPath << "\n";
    std::cout << "Phase 3 smoothing harness OK (exponential).\n";
    return 0;
}
