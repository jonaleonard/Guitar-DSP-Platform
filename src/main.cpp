#include <RtAudio.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

namespace {

constexpr unsigned int kSampleRate = 48000;
constexpr unsigned int kBufferFrames = 256;
constexpr const char* kInputDeviceName = "Volt";

int wireCallback(void* outputBuffer,
                 void* inputBuffer,
                 unsigned int nBufferFrames,
                 double /*streamTime*/,
                 RtAudioStreamStatus /*status*/,
                 void* userData)
{
    const auto* channels = static_cast<const unsigned int*>(userData);
    const unsigned int inChannels = channels[0];
    const unsigned int outChannels = channels[1];

    const auto* in = static_cast<const float*>(inputBuffer);
    auto* out = static_cast<float*>(outputBuffer);

    if (inChannels == outChannels) {
        std::memcpy(out, in, nBufferFrames * inChannels * sizeof(float));
        return 0;
    }

    if (inChannels == 1 && outChannels == 2) {
        for (unsigned int i = 0; i < nBufferFrames; ++i) {
            const float sample = in[i];
            out[(i * 2)] = sample;
            out[(i * 2) + 1] = sample;
        }
        return 0;
    }

    const unsigned int copyChannels = std::min(inChannels, outChannels);
    for (unsigned int i = 0; i < nBufferFrames; ++i) {
        for (unsigned int c = 0; c < copyChannels; ++c) {
            out[(i * outChannels) + c] = in[(i * inChannels) + c];
        }
        for (unsigned int c = copyChannels; c < outChannels; ++c) {
            out[(i * outChannels) + c] = 0.0f;
        }
    }

    return 0;
}

bool containsIgnoreCase(const std::string& haystack, const std::string& needle)
{
    const auto it = std::search(
        haystack.begin(),
        haystack.end(),
        needle.begin(),
        needle.end(),
        [](const char a, const char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });

    return it != haystack.end();
}

std::optional<unsigned int> findInputDeviceId(RtAudio& audio, const char* nameSubstr)
{
    for (const unsigned int id : audio.getDeviceIds()) {
        const RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
        if (info.inputChannels > 0 && containsIgnoreCase(info.name, nameSubstr)) {
            return id;
        }
    }

    return std::nullopt;
}

std::optional<unsigned int> findDefaultOutputDeviceId(RtAudio& audio)
{
    const unsigned int defaultId = audio.getDefaultOutputDevice();
    if (defaultId == 0) {
        return std::nullopt;
    }

    const RtAudio::DeviceInfo info = audio.getDeviceInfo(defaultId);
    if (info.outputChannels == 0) {
        return std::nullopt;
    }

    return defaultId;
}

void printDevices(RtAudio& audio)
{
    for (const unsigned int id : audio.getDeviceIds()) {
        const RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
        std::cout << "  [" << id << "] " << info.name << " (in=" << info.inputChannels
                  << ", out=" << info.outputChannels
                  << ", preferred rate=" << info.preferredSampleRate << " Hz)\n";
    }
}

bool openWireStream(RtAudio& audio,
                    RtAudio::StreamParameters& outParams,
                    RtAudio::StreamParameters& inParams,
                    unsigned int& bufferFrames,
                    unsigned int* channelCounts,
                    bool minimizeLatency)
{
    RtAudio::StreamOptions options;
    if (minimizeLatency) {
        options.flags = RTAUDIO_MINIMIZE_LATENCY;
    }

    return audio.openStream(&outParams,
                            &inParams,
                            RTAUDIO_FLOAT32,
                            kSampleRate,
                            &bufferFrames,
                            wireCallback,
                            channelCounts,
                            &options) == 0;
}

} // namespace

int main()
{
    RtAudio audio;

    if (audio.getDeviceIds().empty()) {
        std::cerr << "No audio devices found.\n";
        return 1;
    }

    const std::optional<unsigned int> inputDeviceId =
        findInputDeviceId(audio, kInputDeviceName);
    if (!inputDeviceId.has_value()) {
        std::cerr << "Could not find an input device matching \"" << kInputDeviceName
                  << "\".\n";
        std::cerr << "Available devices:\n";
        printDevices(audio);
        return 1;
    }

    const std::optional<unsigned int> outputDeviceId = findDefaultOutputDeviceId(audio);
    if (!outputDeviceId.has_value()) {
        std::cerr << "Could not find a default output device.\n";
        std::cerr << "Available devices:\n";
        printDevices(audio);
        return 1;
    }

    const RtAudio::DeviceInfo inputInfo = audio.getDeviceInfo(*inputDeviceId);
    const RtAudio::DeviceInfo outputInfo = audio.getDeviceInfo(*outputDeviceId);

    std::cout << "Input device: " << inputInfo.name << " (id " << *inputDeviceId << ")\n";
    std::cout << "Output device: " << outputInfo.name << " (id " << *outputDeviceId << ")\n";

    const unsigned int inChannels = std::min(1u, inputInfo.inputChannels);
    const unsigned int outChannels = std::min(2u, outputInfo.outputChannels);

    RtAudio::StreamParameters inParams;
    inParams.deviceId = *inputDeviceId;
    inParams.nChannels = inChannels;
    inParams.firstChannel = 0;

    RtAudio::StreamParameters outParams;
    outParams.deviceId = *outputDeviceId;
    outParams.nChannels = outChannels;
    outParams.firstChannel = 0;

    unsigned int bufferFrames = kBufferFrames;
    unsigned int channelCounts[2] = {inChannels, outChannels};

    if (!openWireStream(audio, outParams, inParams, bufferFrames, channelCounts, true) &&
        !openWireStream(audio, outParams, inParams, bufferFrames, channelCounts, false)) {
        std::cerr << "Error opening stream: " << audio.getErrorText() << "\n";
        return 1;
    }

    std::cout << "Sample rate: " << kSampleRate << " Hz\n";
    std::cout << "Buffer frames: " << bufferFrames << "\n";
    std::cout << "Stream latency: " << audio.getStreamLatency() << " samples\n";
    std::cout << "Input channels: " << inChannels << ", output channels: " << outChannels << "\n";
    std::cout << "Wire-through running. Guitar -> Volt input, monitor on system output.\n";
    std::cout << "Press Enter to stop.\n";

    if (audio.startStream() != 0) {
        std::cerr << "Error starting stream: " << audio.getErrorText() << "\n";
        audio.closeStream();
        return 1;
    }

    std::cin.get();

    if (audio.isStreamRunning()) {
        audio.stopStream();
    }
    if (audio.isStreamOpen()) {
        audio.closeStream();
    }

    return 0;
}
