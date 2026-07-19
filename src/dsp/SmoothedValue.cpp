#include "dsp/SmoothedValue.h"

#include <algorithm>
#include <cmath>

namespace dsp {

void SmoothedValue::prepare(const double sampleRate, const float rampTimeMs)
{
    sampleRate_ = std::max(1.0, sampleRate);
    setRampTimeMs(rampTimeMs);
    // Keep current value; recompute any in-flight ramp for the new rate.
    if (remainingSamples_ > 0) {
        recomputeStep();
    }
}

void SmoothedValue::setRampTimeMs(const float rampTimeMs)
{
    rampTimeMs_ = std::max(0.0f, rampTimeMs);
    if (remainingSamples_ > 0) {
        recomputeStep();
    }
}

void SmoothedValue::reset(const float value)
{
    current_ = value;
    target_ = value;
    step_ = 0.0f;
    remainingSamples_ = 0;
}

void SmoothedValue::setTarget(const float target)
{
    target_ = target;

    if (rampTimeMs_ <= 0.0f || sampleRate_ <= 0.0) {
        current_ = target_;
        step_ = 0.0f;
        remainingSamples_ = 0;
        return;
    }

    if (std::fabs(target_ - current_) < 1.0e-8f) {
        current_ = target_;
        step_ = 0.0f;
        remainingSamples_ = 0;
        return;
    }

    recomputeStep();
}

float SmoothedValue::getNext()
{
    if (remainingSamples_ <= 0) {
        current_ = target_;
        return current_;
    }

    current_ += step_;
    --remainingSamples_;

    if (remainingSamples_ <= 0) {
        current_ = target_;
        step_ = 0.0f;
        remainingSamples_ = 0;
    }

    return current_;
}

int SmoothedValue::rampSamples() const
{
    if (rampTimeMs_ <= 0.0f || sampleRate_ <= 0.0) {
        return 0;
    }

    return std::max(1, static_cast<int>(std::lround((rampTimeMs_ * 0.001) * sampleRate_)));
}

void SmoothedValue::recomputeStep()
{
    const int samples = rampSamples();
    if (samples <= 0) {
        current_ = target_;
        step_ = 0.0f;
        remainingSamples_ = 0;
        return;
    }

    remainingSamples_ = samples;
    step_ = (target_ - current_) / static_cast<float>(samples);
}

} // namespace dsp
