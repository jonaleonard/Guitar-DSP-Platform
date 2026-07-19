#include "dsp/DelayEffect.h"

#include <algorithm>

namespace dsp {

DelayEffect::DelayEffect()
{
    timeMs_.reset(350.0f);
    feedback_.reset(0.35f);
    mix_.reset(0.0f); // dry until engaged
}

void DelayEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    line_.prepare(sampleRate, kMaxDelayMs);
    line_.clear();
    timeMs_.prepare(sampleRate);
    feedback_.prepare(sampleRate);
    mix_.prepare(sampleRate);
}

void DelayEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kTimeMs:
        timeMs_.setTarget(std::clamp(value, 1.0f, kMaxDelayMs));
        break;
    case kFeedback:
        feedback_.setTarget(std::clamp(value, 0.0f, 0.95f));
        break;
    case kMix:
        mix_.setTarget(std::clamp(value, 0.0f, 1.0f));
        break;
    default:
        break;
    }
}

void DelayEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        const float timeMs = timeMs_.getNext();
        const float feedback = feedback_.getNext();
        const float mix = mix_.getNext();

        const float dry = buffer[i];
        const float delayed = line_.readMs(timeMs);
        line_.write(dry + delayed * feedback);
        buffer[i] = dry * (1.0f - mix) + delayed * mix;
    }
}

} // namespace dsp
