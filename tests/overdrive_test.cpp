#include "dsp/OverdriveEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

void settle(dsp::OverdriveEffect& od)
{
    std::vector<float> zeros(static_cast<std::size_t>(kSampleRate * 0.5), 0.0f);
    for (std::size_t i = 0; i < zeros.size(); i += static_cast<std::size_t>(kBlock)) {
        const int n = static_cast<int>(std::min(zeros.size() - i, static_cast<std::size_t>(kBlock)));
        od.process(zeros.data() + i, n);
    }
}

} // namespace

int main()
{
    // 1) Mostly odd: small even-harmonic bias is intentional (tube-ish warmth).
    {
        constexpr float kDrive = 8.0f;
        for (float x : {-0.9f, -0.3f, -0.05f, 0.05f, 0.3f, 0.9f}) {
            const float y = dsp::OverdriveEffect::waveshape(x, kDrive);
            const float yn = dsp::OverdriveEffect::waveshape(-x, kDrive);
            // Allow mild asymmetry from quadratic bias; still roughly odd.
            if (std::fabs(y + yn) > 0.15f) {
                std::cerr << "waveshape too asymmetric at x=" << x << " sum=" << (y + yn) << "\n";
                return 1;
            }
        }
        std::cout << "PASS: waveshape roughly odd (mild even bias ok)\n";
    }

    // 2) High drive saturates large inputs toward ±1 (normalized).
    {
        const float y = dsp::OverdriveEffect::waveshape(1.0f, 20.0f);
        if (y < 0.85f || y > 1.05f) {
            std::cerr << "High-drive saturation expected near 1, got " << y << "\n";
            return 1;
        }
        std::cout << "PASS: high drive saturates (y=" << y << ")\n";
    }

    // 3) Soft drive is continuous and gentle near zero.
    {
        const float x = 0.01f;
        const float y = dsp::OverdriveEffect::waveshape(x, 1.0f);
        if (std::fabs(y) > 0.05f || std::fabs(y) < 1.0e-6f) {
            std::cerr << "drive=1 small-signal unexpected: " << y << "\n";
            return 1;
        }
        std::cout << "PASS: drive=1 small-signal ok\n";
    }

    // 4) Effect process: mix=0 → dry * output; mix=1 → wet * output
    {
        dsp::OverdriveEffect od;
        od.prepare(kSampleRate, kBlock);
        od.setParameter(dsp::OverdriveEffect::kDrive, 10.0f);
        od.setParameter(dsp::OverdriveEffect::kMix, 0.0f);
        od.setParameter(dsp::OverdriveEffect::kOutput, 1.0f);
        settle(od);

        std::vector<float> buf(static_cast<std::size_t>(kBlock), 0.5f);
        od.process(buf.data(), kBlock);
        for (float v : buf) {
            if (std::fabs(v - 0.5f) > 1.0e-3f) {
                std::cerr << "mix=0 should be dry, got " << v << "\n";
                return 1;
            }
        }
        std::cout << "PASS: mix=0 passes dry signal\n";
    }

    {
        dsp::OverdriveEffect od;
        od.prepare(kSampleRate, kBlock);
        od.setParameter(dsp::OverdriveEffect::kDrive, 10.0f);
        od.setParameter(dsp::OverdriveEffect::kMix, 1.0f);
        od.setParameter(dsp::OverdriveEffect::kOutput, 1.0f);
        settle(od);

        // Sine (not DC) — wet path has HPF/mid filters that reject constants.
        constexpr int kFrames = kBlock * 8;
        std::vector<float> buf(static_cast<std::size_t>(kFrames), 0.0f);
        for (int i = 0; i < kFrames; ++i) {
            buf[static_cast<std::size_t>(i)] =
                0.8f * std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / static_cast<float>(kSampleRate)));
        }
        for (int i = 0; i < kFrames; i += kBlock) {
            od.process(buf.data() + i, std::min(kBlock, kFrames - i));
        }
        float peak = 0.0f;
        for (int i = kFrames / 2; i < kFrames; ++i) {
            peak = std::max(peak, std::fabs(buf[static_cast<std::size_t>(i)]));
        }
        if (peak < 0.15f || peak > 1.2f) {
            std::cerr << "mix=1 wet peak unexpected: " << peak << "\n";
            return 1;
        }
        std::cout << "PASS: mix=1 applies saturated wet (peak=" << peak << ")\n";
    }

    // 5) Peak reduction: |wet(large)| < |in| for high drive (saturation).
    {
        constexpr float kIn = 1.5f;
        const float wet = dsp::OverdriveEffect::waveshape(kIn, 15.0f);
        if (!(std::fabs(wet) < std::fabs(kIn))) {
            std::cerr << "Expected peak reduction, wet=" << wet << "\n";
            return 1;
        }
        std::cout << "PASS: overdrive reduces peaks (|wet|=" << std::fabs(wet) << " < |in|)\n";
    }

    std::cout << "Overdrive tests OK.\n";
    return 0;
}
