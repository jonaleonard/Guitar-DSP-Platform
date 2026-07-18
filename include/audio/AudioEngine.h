#pragma once

#include "audio/ParameterQueue.h"

#include <RtAudio.h>

#include <functional>
#include <memory>
#include <string>

namespace audio {

struct AudioEngineConfig {
    std::string inputDeviceName = "Volt";
    unsigned int inputDeviceId = 0;
    unsigned int outputDeviceId = 0;
    unsigned int sampleRate = 48000;
    unsigned int bufferFrames = 256;
    unsigned int inputChannels = 1;
    unsigned int outputChannels = 2;
    bool minimizeLatency = true;
    bool useDefaultOutputDevice = true;
};

// Interleaved float buffers. Channel counts are fixed for the lifetime of the stream.
using ProcessBlockCallback =
    std::function<void(const float* input, float* output, int numFrames, int inputChannels, int outputChannels)>;

class AudioEngine {
public:
    static constexpr int kMaxParameters = 64;

    explicit AudioEngine(const AudioEngineConfig& config = {});
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    void setProcessBlockCallback(ProcessBlockCallback callback);

    ParameterQueue& parameterQueue();
    const ParameterQueue& parameterQueue() const;

    bool start();
    void stop();

    bool isRunning() const;

    // Offline block processing using the same real-time path as the live callback.
    void processOfflineBlock(const float* input, float* output, int numFrames);

    unsigned int sampleRate() const;
    unsigned int bufferFrames() const;
    unsigned int streamLatencySamples() const;
    int inputChannels() const;
    int outputChannels() const;

    const std::string& inputDeviceName() const;
    const std::string& outputDeviceName() const;

    float getParameter(int paramId) const;

private:
    static int rtAudioCallback(void* outputBuffer,
                               void* inputBuffer,
                               unsigned int numFrames,
                               double streamTime,
                               RtAudioStreamStatus status,
                               void* userData);

    void processBlockInternal(const float* input, float* output, unsigned int numFrames);
    void drainParameterQueue();
    void wireThrough(const float* input, float* output, unsigned int numFrames) const;

    bool resolveDevices();
    bool openStream();

    AudioEngineConfig config_;
    std::unique_ptr<RtAudio> audio_;
    ProcessBlockCallback processBlockCallback_;
    ParameterQueue parameterQueue_;

    std::string inputDeviceName_;
    std::string outputDeviceName_;
    unsigned int resolvedInputDeviceId_ = 0;
    unsigned int resolvedOutputDeviceId_ = 0;
    unsigned int actualBufferFrames_ = 0;
    unsigned int streamLatencySamples_ = 0;
    bool streamOpen_ = false;

    std::array<std::atomic<float>, kMaxParameters> parameterValues_{};
};

} // namespace audio
