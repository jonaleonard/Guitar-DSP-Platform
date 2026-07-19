#pragma once

#include <algorithm>
#include <cmath>

namespace dsp {

// Lookahead-free peak limiter for studio headroom.
// Targets a ceiling in linear amplitude (e.g. 0.35 ≈ -9 dBFS).
class PeakLimiter {
public:
    void prepare(const double sampleRate, const float ceiling = 0.35f, const float releaseMs = 60.0f)
    {
        sampleRate_ = std::max(1.0, sampleRate);
        ceiling_ = std::clamp(ceiling, 0.05f, 0.99f);
        const float rel = std::max(1.0f, releaseMs);
        releaseCoeff_ = std::exp(-1.0f / (rel * 0.001f * static_cast<float>(sampleRate_)));
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
            envelope_ = a; // instant attack
        } else {
            envelope_ = releaseCoeff_ * envelope_ + (1.0f - releaseCoeff_) * a;
        }

        float targetGain = 1.0f;
        if (envelope_ > ceiling_ && envelope_ > 1.0e-8f) {
            targetGain = ceiling_ / envelope_;
        }

        // Smooth gain changes slightly to avoid zipper (still faster than envelope release).
        gain_ = 0.85f * gain_ + 0.15f * targetGain;
        return x * gain_;
    }

    float currentGain() const { return gain_; }
    float ceiling() const { return ceiling_; }

private:
    double sampleRate_ = 48000.0;
    float ceiling_ = 0.35f;
    float releaseCoeff_ = 0.0f;
    float envelope_ = 0.0f;
    float gain_ = 1.0f;
};

} // namespace dsp
