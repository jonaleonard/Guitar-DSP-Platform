#include "dsp/SyntheticIr.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dsp {

std::vector<float> makeSyntheticCabIr(const int length, const unsigned int sampleRate)
{
    std::vector<float> ir(static_cast<std::size_t>(std::max(1, length)), 0.0f);
    const float sr = static_cast<float>(std::max(1u, sampleRate));

    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / sr;
        const float env = std::exp(-t * 28.0f);
        float s = 0.0f;
        s += 0.70f * std::sin(2.0f * 3.14159265f * 120.0f * t);
        s += 0.40f * std::sin(2.0f * 3.14159265f * 450.0f * t);
        s += 0.20f * std::sin(2.0f * 3.14159265f * 1200.0f * t);
        s += 0.08f * std::sin(2.0f * 3.14159265f * 2500.0f * t);
        ir[static_cast<std::size_t>(i)] = s * env * 0.35f;
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
