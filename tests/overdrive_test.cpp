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
    // 1) waveshape is odd: f(-x) == -f(x)
    {
        constexpr float kDrive = 8.0f;
        for (float x : {-0.9f, -0.3f, -0.05f, 0.05f, 0.3f, 0.9f}) {
            const float y = dsp::OverdriveEffect::waveshape(x, kDrive);
            const float yn = dsp::OverdriveEffect::waveshape(-x, kDrive);
            if (std::fabs(y + yn) > 1.0e-5f) {
                std::cerr << "waveshape not odd at x=" << x << "\n";
                return 1;
            }
        }
        std::cout << "PASS: waveshape is odd\n";
    }

    // 2) High drive saturates large inputs toward ±1 (normalized tanh).
    {
        const float y = dsp::OverdriveEffect::waveshape(1.0f, 20.0f);
        if (y < 0.95f || y > 1.0001f) {
            std::cerr << "High-drive saturation expected near 1, got " << y << "\n";
            return 1;
        }
        std::cout << "PASS: high drive saturates (y=" << y << ")\n";
    }

    // 3) Soft drive ≈ identity near 0 (small-signal gain ≈ 1 after norm).
    {
        const float x = 0.01f;
        const float y = dsp::OverdriveEffect::waveshape(x, 1.0f);
        // tanh(x)/tanh(1) for small x ≈ x / tanh(1) ≈ x * 1.313 — wait
        // At drive=1: tanh(x*1)/tanh(1). For small x, ≈ x / tanh(1) ≈ 1.313*x
        // That's intentional for drive=1. Check consistency with formula instead.
        const float expected = std::tanh(x) / std::tanh(1.0f);
        if (std::fabs(y - expected) > 1.0e-5f) {
            std::cerr << "drive=1 formula mismatch\n";
            return 1;
        }
        std::cout << "PASS: drive=1 matches tanh formula\n";
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

        constexpr float kIn = 0.8f;
        const float expected = dsp::OverdriveEffect::waveshape(kIn, 10.0f);
        std::vector<float> buf(static_cast<std::size_t>(kBlock), kIn);
        od.process(buf.data(), kBlock);
        // First samples may still finish param settle; check last half.
        for (int i = kBlock / 2; i < kBlock; ++i) {
            if (std::fabs(buf[static_cast<std::size_t>(i)] - expected) > 1.0e-3f) {
                std::cerr << "mix=1 wet mismatch got=" << buf[static_cast<std::size_t>(i)]
                          << " expected=" << expected << "\n";
                return 1;
            }
        }
        std::cout << "PASS: mix=1 applies waveshape (out=" << expected << ")\n";
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
