#pragma once

#include <algorithm>
#include <cmath>

namespace dsp {

inline float dbToLin(const float db)
{
    return std::pow(10.0f, db * 0.05f);
}

inline float linToDb(const float lin)
{
    return 20.0f * std::log10(std::max(lin, 1.0e-8f));
}

// One-pole smoothing coefficient for time constant in milliseconds.
inline float msToCoeff(const float timeMs, const double sampleRate)
{
    if (timeMs <= 0.0f || sampleRate <= 0.0) {
        return 0.0f;
    }
    return std::exp(-1.0f / (timeMs * 0.001f * static_cast<float>(sampleRate)));
}

} // namespace dsp
