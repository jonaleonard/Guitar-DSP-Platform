#pragma once

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal PCM float WAV writer for offline DSP validation.
class WavWriter {
public:
    static void writeFloat32(const std::string& path,
                             const float* interleavedSamples,
                             int numFrames,
                             int numChannels,
                             unsigned int sampleRate)
    {
        if (numFrames <= 0 || numChannels <= 0) {
            throw std::invalid_argument("WavWriter: invalid frame/channel count");
        }

        const std::uint32_t dataBytes =
            static_cast<std::uint32_t>(numFrames) * static_cast<std::uint32_t>(numChannels) *
            static_cast<std::uint32_t>(sizeof(float));

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("WavWriter: failed to open " + path);
        }

        auto writeU16 = [&](std::uint16_t value) {
            out.put(static_cast<char>(value & 0xFF));
            out.put(static_cast<char>((value >> 8) & 0xFF));
        };
        auto writeU32 = [&](std::uint32_t value) {
            out.put(static_cast<char>(value & 0xFF));
            out.put(static_cast<char>((value >> 8) & 0xFF));
            out.put(static_cast<char>((value >> 16) & 0xFF));
            out.put(static_cast<char>((value >> 24) & 0xFF));
        };

        // RIFF header
        out.write("RIFF", 4);
        writeU32(36u + dataBytes);
        out.write("WAVE", 4);

        // fmt chunk (IEEE float)
        out.write("fmt ", 4);
        writeU32(16);
        writeU16(3); // WAVE_FORMAT_IEEE_FLOAT
        writeU16(static_cast<std::uint16_t>(numChannels));
        writeU32(sampleRate);
        writeU32(sampleRate * static_cast<std::uint32_t>(numChannels) *
                 static_cast<std::uint32_t>(sizeof(float)));
        writeU16(static_cast<std::uint16_t>(numChannels * static_cast<int>(sizeof(float))));
        writeU16(32);

        // data chunk
        out.write("data", 4);
        writeU32(dataBytes);
        out.write(reinterpret_cast<const char*>(interleavedSamples),
                  static_cast<std::streamsize>(dataBytes));

        if (!out) {
            throw std::runtime_error("WavWriter: failed while writing " + path);
        }
    }

    static void writeFloat32(const std::string& path,
                             const std::vector<float>& interleavedSamples,
                             int numChannels,
                             unsigned int sampleRate)
    {
        const int numFrames =
            static_cast<int>(interleavedSamples.size() / static_cast<std::size_t>(numChannels));
        writeFloat32(path, interleavedSamples.data(), numFrames, numChannels, sampleRate);
    }
};
