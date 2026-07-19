#include "dsp/GainEffect.h"

#include <algorithm>

namespace dsp {

GainEffect::GainEffect() = default;

void GainEffect::prepare(double /*sampleRate*/, int /*maxBlockSize*/)
{
    // Stateless gain — nothing to allocate or reset.
}

void GainEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    const float gain = gain_;
    for (int i = 0; i < numFrames; ++i) {
        buffer[i] *= gain;
    }
}

void GainEffect::setParameter(const int paramId, const float value)
{
    if (paramId == kGain) {
        gain_ = std::max(0.0f, value);
    }
}

} // namespace dsp
