#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dsp {

struct WavData {
    std::vector<float> samples; // interleaved
    int numChannels = 0;
    unsigned int sampleRate = 0;
    int numFrames = 0;
};

// Minimal WAV reader: PCM16 or float32, little-endian.
bool readWavFile(const std::string& path, WavData& out, std::string* error = nullptr);

// Downmix to mono (average channels).
std::vector<float> toMono(const WavData& wav);

} // namespace dsp
