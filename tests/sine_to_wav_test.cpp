#include "audio/AudioEngine.h"
#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kParamGainId = 0;

bool nearlyEqual(const float a, const float b, const float eps = 1.0e-5f)
{
    return std::fabs(a - b) <= eps;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string outputPath = (argc > 1) ? argv[1] : "sine_wire.wav";

    audio::AudioEngineConfig config;
    config.sampleRate = 48000;
    config.bufferFrames = 256;
    config.inputChannels = 1;
    config.outputChannels = 2;

    audio::AudioEngine engine(config);

    // Prove GUI→audio parameter path: push before processing, drain happens in processOfflineBlock.
    if (!engine.parameterQueue().push(kParamGainId, 0.75f)) {
        std::cerr << "Failed to push parameter into queue.\n";
        return 1;
    }

    constexpr float kFrequencyHz = 440.0f;
    constexpr float kAmplitude = 0.5f;
    constexpr double kDurationSeconds = 1.0;

    const unsigned int sampleRate = engine.sampleRate();
    const int blockSize = static_cast<int>(config.bufferFrames);
    const int totalFrames = static_cast<int>(kDurationSeconds * static_cast<double>(sampleRate));
    const int inChannels = engine.inputChannels();
    const int outChannels = engine.outputChannels();

    std::vector<float> input(static_cast<std::size_t>(blockSize * inChannels), 0.0f);
    std::vector<float> output(static_cast<std::size_t>(blockSize * outChannels), 0.0f);
    std::vector<float> wavSamples;
    wavSamples.reserve(static_cast<std::size_t>(totalFrames * outChannels));

    int framesWritten = 0;
    while (framesWritten < totalFrames) {
        const int framesThisBlock = std::min(blockSize, totalFrames - framesWritten);

        for (int i = 0; i < framesThisBlock; ++i) {
            const double t = static_cast<double>(framesWritten + i) / static_cast<double>(sampleRate);
            input[static_cast<std::size_t>(i)] =
                kAmplitude * static_cast<float>(std::sin(2.0 * kPi * static_cast<double>(kFrequencyHz) * t));
        }

        engine.processOfflineBlock(input.data(), output.data(), framesThisBlock);

        // Validate mono→stereo wire-through for this block.
        for (int i = 0; i < framesThisBlock; ++i) {
            const float expected = input[static_cast<std::size_t>(i)];
            const float left = output[static_cast<std::size_t>(i * outChannels)];
            const float right = output[static_cast<std::size_t>((i * outChannels) + 1)];

            if (!nearlyEqual(left, expected) || !nearlyEqual(right, expected)) {
                std::cerr << "Wire-through mismatch at frame " << (framesWritten + i) << "\n";
                return 1;
            }

            wavSamples.push_back(left);
            wavSamples.push_back(right);
        }

        framesWritten += framesThisBlock;
    }

    if (!nearlyEqual(engine.getParameter(kParamGainId), 0.75f)) {
        std::cerr << "Parameter queue was not drained into atomic parameter store.\n";
        return 1;
    }

    try {
        WavWriter::writeFloat32(outputPath, wavSamples, outChannels, sampleRate);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    std::cout << "Wrote " << framesWritten << " frames (" << outChannels << " ch, " << sampleRate
              << " Hz) to " << outputPath << "\n";
    std::cout << "Parameter " << kParamGainId << " = " << engine.getParameter(kParamGainId) << "\n";
    std::cout << "Offline sine→WAV harness OK.\n";
    return 0;
}
