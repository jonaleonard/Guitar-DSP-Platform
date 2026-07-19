#include "dsp/SyntheticIr.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dsp {

std::vector<float> makeSyntheticCabIr(const int length, const unsigned int sampleRate)
{
    std::vector<float> ir(static_cast<std::size_t>(std::max(1, length)), 0.0f);
    const float sr = static_cast<float>(std::max(1u, sampleRate));

    // Open cabinet: controlled lows, strong midrange, enough HF for clarity (less muffling).
    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / sr;
        const float env = std::exp(-t * 32.0f);
        float s = 0.0f;
        s += 0.45f * std::sin(2.0f * 3.14159265f * 110.0f * t);
        s += 0.55f * std::sin(2.0f * 3.14159265f * 480.0f * t);
        s += 0.35f * std::sin(2.0f * 3.14159265f * 1350.0f * t);
        s += 0.22f * std::sin(2.0f * 3.14159265f * 2800.0f * t);
        s += 0.12f * std::sin(2.0f * 3.14159265f * 4500.0f * t);
        ir[static_cast<std::size_t>(i)] = s * env * 0.32f;
    }

    float peak = 1.0e-8f;
    for (float v : ir) {
        peak = std::max(peak, std::fabs(v));
    }
    for (float& v : ir) {
        v /= peak;
    }
    return ir;
}

} // namespace dsp
