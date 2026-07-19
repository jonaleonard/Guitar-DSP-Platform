#include "dsp/CabinetEffect.h"

#include "dsp/WavFile.h"

#include <algorithm>
#include <cstring>

namespace dsp {

CabinetEffect::CabinetEffect()
{
    mix_.reset(0.0f); // dry until user raises cab mix
    level_.reset(1.0f);
}

void CabinetEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    mix_.prepare(sampleRate);
    level_.prepare(sampleRate);
    convolver_.reset();
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
    if (samples == nullptr || numSamples <= 0) {
        return false;
    }
    irLength_ = numSamples;
    convolver_.setImpulseResponse(samples, numSamples, PartitionedConvolver::kDefaultPartitionSize);
    convolver_.reset();
    return convolver_.isReady();
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

    if (!convolver_.isReady()) {
        for (int i = 0; i < frames; ++i) {
            mix_.getNext();
            const float level = level_.getNext();
            buffer[i] *= level;
        }
        return;
    }

    std::memcpy(dryScratch_.data(), buffer, static_cast<std::size_t>(frames) * sizeof(float));
    convolver_.process(buffer, frames);

    for (int i = 0; i < frames; ++i) {
        const float mix = mix_.getNext();
        const float level = level_.getNext();
        const float dry = dryScratch_[static_cast<std::size_t>(i)];
        const float wet = buffer[i];
        buffer[i] = (dry * (1.0f - mix) + wet * mix) * level;
    }
}

} // namespace dsp
