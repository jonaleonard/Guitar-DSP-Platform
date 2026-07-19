#include "dsp/GainEffect.h"

#include <algorithm>

namespace dsp {

GainEffect::GainEffect()
{
    gain_.reset(1.0f);
}

void GainEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    const float current = gain_.getCurrent();
    gain_.prepare(sampleRate, SmoothedValue::kDefaultRampTimeMs);
    gain_.reset(current);
}

void GainEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        buffer[i] *= gain_.getNext();
    }
}

void GainEffect::setParameter(const int paramId, const float value)
{
    if (paramId == kGain) {
        gain_.setTarget(std::max(0.0f, value));
    }
}

void GainEffect::setRampTimeMs(const float rampTimeMs)
{
    gain_.setRampTimeMs(rampTimeMs);
}

} // namespace dsp
