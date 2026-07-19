#pragma once

#include <algorithm>
#include <cmath>

namespace dsp {

// Fast peak limiter: protects the DAC while allowing loud amp/pedal listening levels.
class PeakLimiter {
public:
    void prepare(const double sampleRate, const float ceiling = 0.89f, const float releaseMs = 80.0f)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        ceiling_ = std::clamp(ceiling, 0.05f, 0.99f);
        const float rel = std::max(1.0f, releaseMs);
        releaseCoeff_ = std::exp(-1.0f / (rel * 0.001f * static_cast<float>(sampleRate_)));
        // ~0.3 ms attack — catches picks without the click of a hard sample clamp.
        attackCoeff_ = std::exp(-1.0f / (0.0003f * static_cast<float>(sampleRate_)));
        envelope_ = 0.0f;
        gain_ = 1.0f;
    }

    void setCeiling(const float ceiling)
    {
        ceiling_ = std::clamp(ceiling, 0.05f, 0.99f);
    }

    float process(const float x)
    {
        const float a = x >= 0.0f ? x : -x;
        if (a > envelope_) {
            envelope_ = attackCoeff_ * envelope_ + (1.0f - attackCoeff_) * a;
        } else {
            envelope_ = releaseCoeff_ * envelope_ + (1.0f - releaseCoeff_) * a;
        }

        // Soft knee into the ceiling (~1.5 dB).
        float targetGain = 1.0f;
        const float softStart = ceiling_ * 0.85f;
        if (envelope_ > softStart && envelope_ > 1.0e-8f) {
            if (envelope_ >= ceiling_) {
                targetGain = ceiling_ / envelope_;
            } else {
                const float t = (envelope_ - softStart) / std::max(1.0e-6f, ceiling_ - softStart);
                const float full = ceiling_ / envelope_;
                targetGain = 1.0f + t * t * (full - 1.0f);
            }
        }

        // Faster when reducing gain (protect), slower when recovering (less ducking pump).
        if (targetGain < gain_) {
            gain_ = 0.55f * gain_ + 0.45f * targetGain;
        } else {
            gain_ = 0.92f * gain_ + 0.08f * targetGain;
        }

        float y = x * gain_;
        // Absolute DAC safety — never write |sample| > 1.
        if (y > 1.0f) {
            y = 1.0f;
        } else if (y < -1.0f) {
            y = -1.0f;
        }
        return y;
    }

    float currentGain() const { return gain_; }
    float ceiling() const { return ceiling_; }

private:
    double sampleRate_ = 48000.0;
    float ceiling_ = 0.89f;
    float releaseCoeff_ = 0.0f;
    float attackCoeff_ = 0.0f;
    float envelope_ = 0.0f;
    float gain_ = 1.0f;
};

} // namespace dsp
