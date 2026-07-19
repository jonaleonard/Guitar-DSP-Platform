#include "dsp/SyntheticIr.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace dsp {

std::vector<float> makeSyntheticCabIr(const int length, const unsigned int sampleRate)
{
    std::vector<float> ir(static_cast<std::size_t>(std::max(1, length)), 0.0f);
    const float sr = static_cast<float>(std::max(1u, sampleRate));

    // Studio-ish cab: warm body, controlled presence, early HF roll-off (SM57-on-cone taste).
    // Avoid bright 4–5 kHz spikes that make DI+cab sims harsh on laptop speakers.
    for (int i = 0; i < length; ++i) {
        const float t = static_cast<float>(i) / sr;
        const float env = std::exp(-t * 22.0f); // longer, softer decay
        float s = 0.0f;
        s += 0.55f * std::sin(2.0f * 3.14159265f * 100.0f * t);  // thump
        s += 0.70f * std::sin(2.0f * 3.14159265f * 420.0f * t);  // body
        s += 0.35f * std::sin(2.0f * 3.14159265f * 1100.0f * t); // mid
        s += 0.12f * std::sin(2.0f * 3.14159265f * 2200.0f * t); // mild presence
        s += 0.04f * std::sin(2.0f * 3.14159265f * 3800.0f * t); // soft air (was harsh)
        // Gentle cone LP via time-varying HF damp
        const float hfDamp = std::exp(-t * 80.0f);
        s = s * (0.65f + 0.35f * hfDamp);
        ir[static_cast<std::size_t>(i)] = s * env;
    }

    // RMS normalize, then cap sample peak ≤ 1.
    double energy = 0.0;
    for (float v : ir) {
        energy += static_cast<double>(v) * static_cast<double>(v);
    }
    const float rms = static_cast<float>(std::sqrt(energy / std::max(1, length)));
    const float scale = rms > 1.0e-8f ? (0.35f / rms) : 1.0f;
    float peak = 1.0e-8f;
    for (float& v : ir) {
        v *= scale;
        peak = std::max(peak, std::fabs(v));
    }
    if (peak > 1.0f) {
        const float invPeak = 1.0f / peak;
        for (float& v : ir) {
            v *= invPeak;
        }
    }

    // Cap strongest frequency gain near unity — sine-sum IRs otherwise resonate
    // at +40 dB and force the "sounds quiet / meters hot" crest-factor trap.
    float maxMag = 1.0e-8f;
    constexpr float kProbeHz[] = {90.f, 100.f, 200.f, 420.f, 600.f, 800.f,
                                  1100.f, 1600.f, 2200.f, 3000.f, 3800.f};
    for (float f : kProbeHz) {
        double re = 0.0;
        double im = 0.0;
        const double w = 2.0 * 3.14159265358979323846 * static_cast<double>(f) / static_cast<double>(sr);
        for (int n = 0; n < length; ++n) {
            const double ang = w * static_cast<double>(n);
            const double v = static_cast<double>(ir[static_cast<std::size_t>(n)]);
            re += v * std::cos(ang);
            im -= v * std::sin(ang);
        }
        maxMag = std::max(maxMag, static_cast<float>(std::sqrt(re * re + im * im)));
    }
    // Allow mild speaker resonance (~+3 dB), then flatten the rest.
    constexpr float kMaxResp = 1.4f;
    if (maxMag > kMaxResp) {
        const float inv = kMaxResp / maxMag;
        for (float& v : ir) {
            v *= inv;
        }
    }
    return ir;
}

} // namespace dsp
