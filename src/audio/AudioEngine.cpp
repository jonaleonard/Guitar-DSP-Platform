#include "audio/AudioEngine.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <optional>

namespace audio {
namespace {

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

std::optional<unsigned int> findInputDeviceId(RtAudio& audio, const std::string& nameSubstr)
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
        std::cerr << "  [" << id << "] " << info.name << " (in=" << info.inputChannels
                  << ", out=" << info.outputChannels
                  << ", preferred rate=" << info.preferredSampleRate << " Hz)\n";
    }
}

bool deviceSupportsSampleRate(const RtAudio::DeviceInfo& info, const unsigned int sampleRate)
{
    if (info.sampleRates.empty()) {
        return true;
    }

    return std::find(info.sampleRates.begin(), info.sampleRates.end(), sampleRate) !=
           info.sampleRates.end();
}

} // namespace

AudioEngine::AudioEngine(const AudioEngineConfig& config)
    : config_(config)
    , audio_(std::make_unique<RtAudio>())
{
    for (std::atomic<float>& value : parameterValues_) {
        value.store(0.0f, std::memory_order_relaxed);
    }
}

AudioEngine::~AudioEngine()
{
    stop();
}

void AudioEngine::setProcessBlockCallback(ProcessBlockCallback callback)
{
    processBlockCallback_ = std::move(callback);
}

ParameterQueue& AudioEngine::parameterQueue()
{
    return parameterQueue_;
}

const ParameterQueue& AudioEngine::parameterQueue() const
{
    return parameterQueue_;
}

bool AudioEngine::start()
{
    if (isRunning()) {
        return true;
    }

    if (!resolveDevices()) {
        return false;
    }

    if (!openStream()) {
        return false;
    }

    if (audio_->startStream() != 0) {
        std::cerr << "Error starting stream: " << audio_->getErrorText() << "\n";
        audio_->closeStream();
        streamOpen_ = false;
        return false;
    }

    return true;
}

void AudioEngine::stop()
{
    if (audio_ == nullptr) {
        return;
    }

    if (audio_->isStreamRunning()) {
        audio_->stopStream();
    }

    if (audio_->isStreamOpen()) {
        audio_->closeStream();
    }

    streamOpen_ = false;
}

bool AudioEngine::isRunning() const
{
    return audio_ != nullptr && audio_->isStreamRunning();
}

void AudioEngine::processOfflineBlock(const float* input, float* output, const int numFrames)
{
    processBlockInternal(input, output, static_cast<unsigned int>(numFrames));
}

unsigned int AudioEngine::sampleRate() const
{
    return actualSampleRate_ != 0 ? actualSampleRate_ : config_.sampleRate;
}

unsigned int AudioEngine::bufferFrames() const
{
    return actualBufferFrames_ != 0 ? actualBufferFrames_ : config_.bufferFrames;
}

unsigned int AudioEngine::streamLatencySamples() const
{
    return streamLatencySamples_;
}

int AudioEngine::inputChannels() const
{
    return static_cast<int>(config_.inputChannels);
}

int AudioEngine::outputChannels() const
{
    return static_cast<int>(config_.outputChannels);
}

const std::string& AudioEngine::inputDeviceName() const
{
    return inputDeviceName_;
}

const std::string& AudioEngine::outputDeviceName() const
{
    return outputDeviceName_;
}

float AudioEngine::getParameter(const int paramId) const
{
    if (paramId < 0 || paramId >= kMaxParameters) {
        return 0.0f;
    }

    return parameterValues_[static_cast<std::size_t>(paramId)].load(std::memory_order_relaxed);
}

std::uint64_t AudioEngine::inputOverflowCount() const
{
    return inputOverflowCount_.load(std::memory_order_relaxed);
}

std::uint64_t AudioEngine::outputUnderflowCount() const
{
    return outputUnderflowCount_.load(std::memory_order_relaxed);
}

int AudioEngine::rtAudioCallback(void* outputBuffer,
                                 void* inputBuffer,
                                 const unsigned int numFrames,
                                 double /*streamTime*/,
                                 RtAudioStreamStatus status,
                                 void* userData)
{
    auto* engine = static_cast<AudioEngine*>(userData);

    if ((status & RTAUDIO_INPUT_OVERFLOW) != 0) {
        engine->inputOverflowCount_.fetch_add(1, std::memory_order_relaxed);
    }
    if ((status & RTAUDIO_OUTPUT_UNDERFLOW) != 0) {
        engine->outputUnderflowCount_.fetch_add(1, std::memory_order_relaxed);
    }

    auto* out = static_cast<float*>(outputBuffer);
    if (out == nullptr) {
        return 0;
    }

    const auto* in = static_cast<const float*>(inputBuffer);
    if (in == nullptr) {
        engine->silenceOutput(out, numFrames);
        return 0;
    }

    engine->processBlockInternal(in, out, numFrames);
    return 0;
}

void AudioEngine::processBlockInternal(const float* input, float* output, const unsigned int numFrames)
{
    drainParameterQueue();

    if (processBlockCallback_) {
        processBlockCallback_(input,
                              output,
                              static_cast<int>(numFrames),
                              inputChannels(),
                              outputChannels());
        return;
    }

    wireThrough(input, output, numFrames);
}

void AudioEngine::drainParameterQueue()
{
    int paramId = 0;
    float value = 0.0f;

    while (parameterQueue_.pop(paramId, value)) {
        if (paramId >= 0 && paramId < kMaxParameters) {
            parameterValues_[static_cast<std::size_t>(paramId)].store(value, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::wireThrough(const float* input, float* output, const unsigned int numFrames) const
{
    const unsigned int inChannels = config_.inputChannels;
    const unsigned int outChannels = config_.outputChannels;

    if (inChannels == outChannels) {
        std::memcpy(output, input, numFrames * inChannels * sizeof(float));
        return;
    }

    if (inChannels == 1 && outChannels == 2) {
        for (unsigned int i = 0; i < numFrames; ++i) {
            const float sample = input[i];
            output[(i * 2)] = sample;
            output[(i * 2) + 1] = sample;
        }
        return;
    }

    const unsigned int copyChannels = std::min(inChannels, outChannels);
    for (unsigned int i = 0; i < numFrames; ++i) {
        for (unsigned int c = 0; c < copyChannels; ++c) {
            output[(i * outChannels) + c] = input[(i * inChannels) + c];
        }
        for (unsigned int c = copyChannels; c < outChannels; ++c) {
            output[(i * outChannels) + c] = 0.0f;
        }
    }
}

void AudioEngine::silenceOutput(float* output, const unsigned int numFrames) const
{
    if (output == nullptr || numFrames == 0) {
        return;
    }

    std::memset(output, 0, static_cast<std::size_t>(numFrames) * config_.outputChannels * sizeof(float));
}

bool AudioEngine::resolveDevices()
{
    if (audio_->getDeviceIds().empty()) {
        std::cerr << "No audio devices found.\n";
        return false;
    }

    if (config_.inputDeviceId != 0) {
        resolvedInputDeviceId_ = config_.inputDeviceId;
        inputDeviceName_ = audio_->getDeviceInfo(resolvedInputDeviceId_).name;
    } else {
        const std::optional<unsigned int> inputDeviceId =
            findInputDeviceId(*audio_, config_.inputDeviceName);
        if (!inputDeviceId.has_value()) {
            std::cerr << "Could not find an input device matching \"" << config_.inputDeviceName
                      << "\".\n";
            std::cerr << "Available devices:\n";
            printDevices(*audio_);
            return false;
        }

        resolvedInputDeviceId_ = *inputDeviceId;
        inputDeviceName_ = audio_->getDeviceInfo(resolvedInputDeviceId_).name;
    }

    const RtAudio::DeviceInfo inputInfo = audio_->getDeviceInfo(resolvedInputDeviceId_);

    if (config_.outputDeviceId != 0) {
        resolvedOutputDeviceId_ = config_.outputDeviceId;
        outputDeviceName_ = audio_->getDeviceInfo(resolvedOutputDeviceId_).name;
    } else if (config_.preferSameDeviceOutput && inputInfo.outputChannels > 0) {
        // Same physical device for in+out avoids cross-device clock mismatch crackle.
        resolvedOutputDeviceId_ = resolvedInputDeviceId_;
        outputDeviceName_ = inputInfo.name;
        std::cerr << "Using same-device output on \"" << outputDeviceName_
                  << "\" (plug headphones into the interface if needed).\n";
    } else if (config_.useDefaultOutputDevice) {
        const std::optional<unsigned int> outputDeviceId = findDefaultOutputDeviceId(*audio_);
        if (!outputDeviceId.has_value()) {
            std::cerr << "Could not find a default output device.\n";
            std::cerr << "Available devices:\n";
            printDevices(*audio_);
            return false;
        }

        resolvedOutputDeviceId_ = *outputDeviceId;
        outputDeviceName_ = audio_->getDeviceInfo(resolvedOutputDeviceId_).name;

        if (resolvedOutputDeviceId_ != resolvedInputDeviceId_) {
            std::cerr << "Warning: input and output are different devices ("
                      << inputDeviceName_ << " → " << outputDeviceName_
                      << "). Cross-device duplex often causes crackling on macOS.\n"
                      << "Prefer headphones into the Volt, or create an Aggregate Device "
                         "in Audio MIDI Setup.\n";
        }
    } else {
        std::cerr << "No output device configured.\n";
        return false;
    }

    const RtAudio::DeviceInfo outputInfo = audio_->getDeviceInfo(resolvedOutputDeviceId_);

    if (!deviceSupportsSampleRate(inputInfo, config_.sampleRate) ||
        !deviceSupportsSampleRate(outputInfo, config_.sampleRate)) {
        std::cerr << "Warning: requested sample rate " << config_.sampleRate
                  << " Hz may not be listed for one of the devices "
                     "(input preferred="
                  << inputInfo.preferredSampleRate << ", output preferred="
                  << outputInfo.preferredSampleRate << ").\n"
                  << "Set both devices to " << config_.sampleRate
                  << " Hz in Audio MIDI Setup if you hear crackling.\n";
    }

    config_.inputChannels = std::min(config_.inputChannels, inputInfo.inputChannels);
    config_.outputChannels = std::min(config_.outputChannels, outputInfo.outputChannels);

    if (config_.inputChannels == 0 || config_.outputChannels == 0) {
        std::cerr << "Resolved zero input or output channels.\n";
        return false;
    }

    return true;
}

bool AudioEngine::openStream()
{
    RtAudio::StreamParameters inParams;
    inParams.deviceId = resolvedInputDeviceId_;
    inParams.nChannels = config_.inputChannels;
    inParams.firstChannel = 0;

    RtAudio::StreamParameters outParams;
    outParams.deviceId = resolvedOutputDeviceId_;
    outParams.nChannels = config_.outputChannels;
    outParams.firstChannel = 0;

    actualBufferFrames_ = config_.bufferFrames;

    auto tryOpen = [&](const bool minimizeLatency) {
        RtAudio::StreamOptions options;
        options.flags = 0;
        if (minimizeLatency) {
            options.flags |= RTAUDIO_MINIMIZE_LATENCY;
        }
        if (config_.scheduleRealtime) {
            options.flags |= RTAUDIO_SCHEDULE_REALTIME;
        }
        // Give CoreAudio a little extra buffering when not minimizing latency.
        options.numberOfBuffers = minimizeLatency ? 2 : 4;

        actualBufferFrames_ = config_.bufferFrames;
        return audio_->openStream(&outParams,
                                  &inParams,
                                  RTAUDIO_FLOAT32,
                                  config_.sampleRate,
                                  &actualBufferFrames_,
                                  &AudioEngine::rtAudioCallback,
                                  this,
                                  &options) == 0;
    };

    if (!tryOpen(config_.minimizeLatency) && !tryOpen(false)) {
        std::cerr << "Error opening stream: " << audio_->getErrorText() << "\n";
        return false;
    }

    streamOpen_ = true;
    streamLatencySamples_ = audio_->getStreamLatency();
    actualSampleRate_ = audio_->getStreamSampleRate();
    if (actualSampleRate_ == 0) {
        actualSampleRate_ = config_.sampleRate;
    }

    return true;
}

} // namespace audio
