#include "dsp/SmoothedValue.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

constexpr float kSnapEpsilon = 1.0e-7f;

} // namespace

void SmoothedValue::prepare(const double sampleRate, const float rampTimeMs)
{
    sampleRate_ = std::max(1.0, sampleRate);
    setRampTimeMs(rampTimeMs);
}

void SmoothedValue::setRampTimeMs(const float rampTimeMs)
{
    rampTimeMs_ = std::max(0.0f, rampTimeMs);
    updateCoeff();
}

void SmoothedValue::reset(const float value)
{
    current_ = value;
    target_ = value;
}

void SmoothedValue::setTarget(const float target)
{
    target_ = target;
}

float SmoothedValue::getNext()
{
    if (rampTimeMs_ <= 0.0f || coeff_ <= 0.0f) {
        current_ = target_;
        return current_;
    }

    current_ = coeff_ * current_ + (1.0f - coeff_) * target_;

    if (std::fabs(current_ - target_) < kSnapEpsilon) {
        current_ = target_;
    }

    return current_;
}

bool SmoothedValue::isSmoothing() const
{
    return std::fabs(current_ - target_) >= kSnapEpsilon;
}

int SmoothedValue::settleSamples() const
{
    // ~6.9 time-constants ≈ 60 dB of settling for a one-pole.
    if (rampTimeMs_ <= 0.0f || sampleRate_ <= 0.0) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(6.9 * (rampTimeMs_ * 0.001) * sampleRate_)));
}

void SmoothedValue::updateCoeff()
{
    if (rampTimeMs_ <= 0.0f || sampleRate_ <= 0.0) {
        coeff_ = 0.0f;
        return;
    }

    coeff_ = std::exp(-1.0f / (rampTimeMs_ * 0.001f * static_cast<float>(sampleRate_)));
}

} // namespace dsp
