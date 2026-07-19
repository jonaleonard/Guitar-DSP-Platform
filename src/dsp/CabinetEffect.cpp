#include "dsp/CabinetEffect.h"

#include "dsp/WavFile.h"

#include <algorithm>
#include <cstring>

namespace dsp {

CabinetEffect::CabinetEffect()
{
    mix_.reset(0.0f);
    level_.reset(1.0f);
}

void CabinetEffect::clearHeadState()
{
    headDelay_.fill(0.0f);
    headWrite_ = 0;
}

void CabinetEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    // Fast mix ramps — less dry/wet transition smear.
    mix_.prepare(sampleRate, 12.0f);
    level_.prepare(sampleRate, 12.0f);
    clearHeadState();
    if (tailConvolver_.isReady()) {
        tailConvolver_.reset();
    }
}

void CabinetEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kMix:
        mix_.setTarget(std::clamp(value, 0.0f, 1.0f));
        break;
    case kLevel:
        level_.setTarget(std::clamp(value, 0.0f, 2.0f));
        break;
    default:
        break;
    }
}

bool CabinetEffect::loadImpulseResponse(const float* samples, const int numSamples)
{
    ready_ = false;
    if (samples == nullptr || numSamples <= 0) {
        return false;
    }

    irLength_ = numSamples;
    headLen_ = std::min(kHeadSize, numSamples);
    headIr_.assign(static_cast<std::size_t>(headLen_), 0.0f);
    std::memcpy(headIr_.data(), samples, static_cast<std::size_t>(headLen_) * sizeof(float));
    clearHeadState();

    const int tailLen = numSamples - headLen_;
    if (tailLen > 0) {
        // Tail IR is h[H..]; partitioned latency H makes timing exact with the head FIR:
        // y = head_direct + tail_partitioned → zero overall latency (Gardner hybrid).
        tailConvolver_.setImpulseResponse(samples + headLen_, tailLen, headLen_);
        if (!tailConvolver_.isReady()) {
            ready_ = false;
            return false;
        }
    } else {
        tailConvolver_ = PartitionedConvolver{};
    }

    ready_ = true;
    return true;
}

bool CabinetEffect::loadImpulseResponseFile(const std::string& path)
{
    WavData wav;
    std::string err;
    if (!readWavFile(path, wav, &err)) {
        return false;
    }
    const std::vector<float> mono = toMono(wav);
    return loadImpulseResponse(mono.data(), static_cast<int>(mono.size()));
}

void CabinetEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    const int frames = std::min(numFrames, kMaxBlock);

    if (!ready_) {
        for (int i = 0; i < frames; ++i) {
            mix_.getNext();
            buffer[i] *= level_.getNext();
        }
        return;
    }

    std::memcpy(dryScratch_.data(), buffer, static_cast<std::size_t>(frames) * sizeof(float));

    // Direct FIR head (zero latency).
    for (int i = 0; i < frames; ++i) {
        const float x = dryScratch_[static_cast<std::size_t>(i)];
        headDelay_[static_cast<std::size_t>(headWrite_)] = x;

        float acc = 0.0f;
        for (int k = 0; k < headLen_; ++k) {
            const int idx = (headWrite_ - k + kMaxHead) % kMaxHead;
            acc += headIr_[static_cast<std::size_t>(k)] * headDelay_[static_cast<std::size_t>(idx)];
        }
        wetScratch_[static_cast<std::size_t>(i)] = acc;
        headWrite_ = (headWrite_ + 1) % kMaxHead;
    }

    // Partitioned FFT tail (latency = headLen, aligned with head).
    if (tailConvolver_.isReady()) {
        std::memcpy(buffer, dryScratch_.data(), static_cast<std::size_t>(frames) * sizeof(float));
        tailConvolver_.process(buffer, frames);
        for (int i = 0; i < frames; ++i) {
            wetScratch_[static_cast<std::size_t>(i)] += buffer[i];
        }
    }

    for (int i = 0; i < frames; ++i) {
        const float mix = mix_.getNext();
        const float level = level_.getNext();
        const float dry = dryScratch_[static_cast<std::size_t>(i)];
        const float wet = wetScratch_[static_cast<std::size_t>(i)];
        buffer[i] = (dry * (1.0f - mix) + wet * mix) * level;
    }
}

} // namespace dsp
