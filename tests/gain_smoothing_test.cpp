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

constexpr double kPi = 3.14159265358979323846;
constexpr unsigned int kSampleRate = 48000;
constexpr int kBlockSize = 256;
constexpr float kRampTimeMs = dsp::SmoothedValue::kDefaultRampTimeMs;

bool nearlyEqual(const float a, const float b, const float eps = 1.0e-4f)
{
    return std::fabs(a - b) <= eps;
}

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

int settleFrames()
{
    return static_cast<int>(std::lround((kRampTimeMs * 0.001) * static_cast<double>(kSampleRate))) +
           kBlockSize;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string outputPath = (argc > 1) ? argv[1] : "gain_smooth.wav";

    // --- Unit: SmoothedValue ramps linearly without jumps ---
    {
        dsp::SmoothedValue smoother;
        smoother.prepare(static_cast<double>(kSampleRate), kRampTimeMs);
        smoother.reset(0.0f);
        smoother.setTarget(1.0f);

        float prev = smoother.getCurrent();
        float maxDelta = 0.0f;
        const int samples = smoother.rampSamples();
        for (int i = 0; i < samples + 4; ++i) {
            const float v = smoother.getNext();
            maxDelta = std::max(maxDelta, std::fabs(v - prev));
            prev = v;
        }

        const float expectedMaxStep = 1.0f / static_cast<float>(std::max(1, samples));
        if (maxDelta > expectedMaxStep * 1.01f + 1.0e-6f) {
            std::cerr << "SmoothedValue step too large: " << maxDelta
                      << " (expected ~" << expectedMaxStep << ")\n";
            return 1;
        }
        if (!nearlyEqual(smoother.getCurrent(), 1.0f)) {
            std::cerr << "SmoothedValue failed to reach target.\n";
            return 1;
        }
    }

    // --- Integration: rapid gain automation every 10ms, measure zipper ---
    auto graph = std::make_shared<dsp::EffectGraph>();
    graph->prepare(static_cast<double>(kSampleRate), kBlockSize);

    auto gain = std::make_unique<dsp::GainEffect>();
    gain->setRampTimeMs(kRampTimeMs);
    // Snap initial gain through prepare reset path: construct already at 1, then target 0.
    if (!graph->insert(std::move(gain), 0)) {
        std::cerr << "Failed to insert Gain.\n";
        return 1;
    }
    graph->flushCommands();
    graph->setParameter(0, dsp::GainEffect::kGain, 0.0f);
    settleGraph(*graph, settleFrames());

    constexpr float kAmplitude = 1.0f; // constant DC — makes gain jumps fully audible as steps
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

    // Without smoothing, a 0↔1 gain jump on DC=1 produces delta ≈ 1.0.
    // With a 20 ms linear ramp, max step ≈ 1 / rampSamples ≈ 0.00104.
    const int rampSamples =
        std::max(1, static_cast<int>(std::lround((kRampTimeMs * 0.001) * static_cast<double>(kSampleRate))));
    const float smoothedCeiling = (1.0f / static_cast<float>(rampSamples)) * 2.0f; // margin
    constexpr float kZipperFailThreshold = 0.05f; // still far below unsmoothed click (~1.0)

    if (maxDelta > kZipperFailThreshold) {
        std::cerr << "Zipper detected: max sample delta=" << maxDelta
                  << " (threshold=" << kZipperFailThreshold
                  << ", smoothed ceiling~" << smoothedCeiling << ")\n";
        return 1;
    }

    // Also run through AudioEngine offline path and write mono WAV (duplicated to stereo).
    audio::AudioEngineConfig config;
    config.sampleRate = kSampleRate;
    config.bufferFrames = static_cast<unsigned int>(kBlockSize);
    config.inputChannels = 1;
    config.outputChannels = 2;

    audio::AudioEngine engine(config);
    auto sharedGraph = graph;
    std::vector<float> monoScratch(static_cast<std::size_t>(kBlockSize), 0.0f);

    engine.setProcessBlockCallback(
        [sharedGraph, &monoScratch](const float* in,
                                    float* out,
                                    const int numFrames,
                                    const int /*inCh*/,
                                    const int outCh) {
            for (int i = 0; i < numFrames; ++i) {
                monoScratch[static_cast<std::size_t>(i)] = in[i];
            }
            sharedGraph->process(monoScratch.data(), numFrames);
            for (int i = 0; i < numFrames; ++i) {
                const float s = monoScratch[static_cast<std::size_t>(i)];
                out[(i * outCh)] = s;
                if (outCh > 1) {
                    out[(i * outCh) + 1] = s;
                }
            }
        });

    // Replay the recorded mono sweep as a stereo WAV for listening.
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

    std::cout << "Rapid gain sweep OK: max sample delta=" << maxDelta
              << " (fail if > " << kZipperFailThreshold << ")\n";
    std::cout << "Wrote " << wavSamples.size() << " frames to " << outputPath << "\n";
    std::cout << "Phase 3 smoothing harness OK.\n";
    return 0;
}
