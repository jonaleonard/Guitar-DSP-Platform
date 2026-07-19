#include "dsp/ChorusEffect.h"

#include <algorithm>
#include <cmath>

namespace dsp {

ChorusEffect::ChorusEffect()
{
    rateHz_.reset(0.8f);
    depthMs_.reset(3.0f);
    mix_.reset(0.0f);
}

void ChorusEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = std::max(1.0, sampleRate);
    line_.prepare(sampleRate_, kMaxDelayMs);
    line_.clear();
    rateHz_.prepare(sampleRate_);
    depthMs_.prepare(sampleRate_);
    mix_.prepare(sampleRate_);
    lfoPhase_ = 0.0f;
}

void ChorusEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kRateHz:
        rateHz_.setTarget(std::clamp(value, 0.05f, 5.0f));
        break;
    case kDepthMs:
        depthMs_.setTarget(std::clamp(value, 0.5f, 12.0f));
        break;
    case kMix:
        mix_.setTarget(std::clamp(value, 0.0f, 1.0f));
        break;
    default:
        break;
    }
}

void ChorusEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    constexpr float kTwoPi = 6.28318530718f;

    for (int i = 0; i < numFrames; ++i) {
        const float rate = rateHz_.getNext();
        const float depth = depthMs_.getNext();
        const float mix = mix_.getNext();

        const float lfo = 0.5f + 0.5f * std::sin(lfoPhase_);
        const float delayMs = kBaseDelayMs + depth * lfo;

        const float dry = buffer[i];
        line_.write(dry);
        const float wet = line_.readMs(delayMs);
        buffer[i] = dry * (1.0f - mix) + wet * mix;

        lfoPhase_ += kTwoPi * rate / static_cast<float>(sampleRate_);
        if (lfoPhase_ >= kTwoPi) {
            lfoPhase_ -= kTwoPi;
        }
    }
}

} // namespace dsp
