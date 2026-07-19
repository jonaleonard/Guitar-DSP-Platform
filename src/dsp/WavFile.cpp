#include "dsp/WavFile.h"

#include <cstring>
#include <fstream>

namespace dsp {
namespace {

bool readExact(std::ifstream& in, void* dst, const std::size_t n)
{
    in.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(in.gcount()) == n;
}

std::uint16_t readU16(std::ifstream& in)
{
    unsigned char b[2];
    readExact(in, b, 2);
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t readU32(std::ifstream& in)
{
    unsigned char b[4];
    readExact(in, b, 4);
    return static_cast<std::uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

} // namespace

bool readWavFile(const std::string& path, WavData& out, std::string* error)
{
    out = {};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) {
            *error = "Failed to open " + path;
        }
        return false;
    }

    char riff[4];
    if (!readExact(in, riff, 4) || std::memcmp(riff, "RIFF", 4) != 0) {
        if (error) {
            *error = "Not a RIFF file";
        }
        return false;
    }
    readU32(in); // file size
    char wave[4];
    if (!readExact(in, wave, 4) || std::memcmp(wave, "WAVE", 4) != 0) {
        if (error) {
            *error = "Not a WAVE file";
        }
        return false;
    }

    bool gotFmt = false;
    bool gotData = false;
    std::uint16_t audioFormat = 0;
    std::uint16_t numChannels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<char> dataBytes;

    while (in && (!gotFmt || !gotData)) {
        char chunkId[4];
        if (!readExact(in, chunkId, 4)) {
            break;
        }
        const std::uint32_t chunkSize = readU32(in);

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            audioFormat = readU16(in);
            numChannels = readU16(in);
            sampleRate = readU32(in);
            readU32(in); // byte rate
            readU16(in); // block align
            bitsPerSample = readU16(in);
            if (chunkSize > 16) {
                in.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
            }
            gotFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataBytes.resize(chunkSize);
            if (chunkSize > 0 && !readExact(in, dataBytes.data(), chunkSize)) {
                if (error) {
                    *error = "Truncated data chunk";
                }
                return false;
            }
            gotData = true;
        } else {
            in.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
        }

        // Chunks are word-aligned
        if ((chunkSize & 1u) != 0u) {
            in.seekg(1, std::ios::cur);
        }
    }

    if (!gotFmt || !gotData || numChannels == 0 || sampleRate == 0) {
        if (error) {
            *error = "Missing fmt/data";
        }
        return false;
    }

    out.numChannels = static_cast<int>(numChannels);
    out.sampleRate = sampleRate;

    if (audioFormat == 3 && bitsPerSample == 32) {
        const int totalSamples = static_cast<int>(dataBytes.size() / sizeof(float));
        out.numFrames = totalSamples / out.numChannels;
        out.samples.resize(static_cast<std::size_t>(totalSamples));
        std::memcpy(out.samples.data(), dataBytes.data(), dataBytes.size());
        return true;
    }

    if (audioFormat == 1 && bitsPerSample == 16) {
        const int totalSamples = static_cast<int>(dataBytes.size() / sizeof(std::int16_t));
        out.numFrames = totalSamples / out.numChannels;
        out.samples.resize(static_cast<std::size_t>(totalSamples));
        for (int i = 0; i < totalSamples; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(dataBytes.data()) + i * 2;
            const auto s =
                static_cast<std::int16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
            out.samples[static_cast<std::size_t>(i)] = static_cast<float>(s) / 32768.0f;
        }
        return true;
    }

    if (error) {
        *error = "Unsupported WAV format (need PCM16 or float32)";
    }
    return false;
}

std::vector<float> toMono(const WavData& wav)
{
    std::vector<float> mono(static_cast<std::size_t>(wav.numFrames), 0.0f);
    if (wav.numChannels <= 0 || wav.numFrames <= 0) {
        return mono;
    }
    for (int i = 0; i < wav.numFrames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < wav.numChannels; ++c) {
            sum += wav.samples[static_cast<std::size_t>(i * wav.numChannels + c)];
        }
        mono[static_cast<std::size_t>(i)] = sum / static_cast<float>(wav.numChannels);
    }
    return mono;
}

} // namespace dsp
