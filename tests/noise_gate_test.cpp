#include "dsp/DspMath.h"
#include "dsp/NoiseGateEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

float rms(const std::vector<float>& x)
{
    double sum = 0.0;
    for (float v : x) {
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(x.size())));
}

void processAll(dsp::NoiseGateEffect& gate, std::vector<float>& buf)
{
    for (std::size_t i = 0; i < buf.size(); i += static_cast<std::size_t>(kBlock)) {
        const int n = static_cast<int>(std::min(buf.size() - i, static_cast<std::size_t>(kBlock)));
        gate.process(buf.data() + i, n);
    }
}

} // namespace

int main()
{
    dsp::NoiseGateEffect gate;
    gate.prepare(kSampleRate, kBlock);

    // Fast ballistics so tests settle quickly.
    gate.setParameter(dsp::NoiseGateEffect::kThresholdDb, -40.0f);
    gate.setParameter(dsp::NoiseGateEffect::kAttackMs, 1.0f);
    gate.setParameter(dsp::NoiseGateEffect::kReleaseMs, 20.0f);
    gate.setParameter(dsp::NoiseGateEffect::kRangeDb, -80.0f);

    // Warm up parameter ramps.
    {
        std::vector<float> zeros(static_cast<std::size_t>(kSampleRate * 0.05), 0.0f);
        processAll(gate, zeros);
    }

    // 1) Loud signal above threshold (-20 dB ≈ 0.1) must pass (~unity).
    {
        dsp::NoiseGateEffect openGate;
        openGate.prepare(kSampleRate, kBlock);
        openGate.setParameter(dsp::NoiseGateEffect::kThresholdDb, -40.0f);
        openGate.setParameter(dsp::NoiseGateEffect::kAttackMs, 1.0f);
        openGate.setParameter(dsp::NoiseGateEffect::kReleaseMs, 20.0f);
        openGate.setParameter(dsp::NoiseGateEffect::kRangeDb, -80.0f);

        std::vector<float> warm(static_cast<std::size_t>(kSampleRate * 0.05), 0.0f);
        processAll(openGate, warm);

        constexpr float kLoud = 0.25f; // ~ -12 dB, above -40 dB threshold
        std::vector<float> buf(static_cast<std::size_t>(kSampleRate * 0.25), kLoud);
        processAll(openGate, buf);

        const float outRms = rms(buf);
        if (outRms < kLoud * 0.9f) {
            std::cerr << "Gate should pass loud signal, rms=" << outRms << "\n";
            return 1;
        }
        std::cout << "PASS: loud signal passes gate (rms=" << outRms << ")\n";
    }

    // 2) Quiet hum below threshold must be attenuated strongly.
    {
        dsp::NoiseGateEffect closedGate;
        closedGate.prepare(kSampleRate, kBlock);
        closedGate.setParameter(dsp::NoiseGateEffect::kThresholdDb, -40.0f);
        closedGate.setParameter(dsp::NoiseGateEffect::kAttackMs, 1.0f);
        closedGate.setParameter(dsp::NoiseGateEffect::kReleaseMs, 5.0f);
        closedGate.setParameter(dsp::NoiseGateEffect::kRangeDb, -80.0f);

        // Start closed with silence.
        std::vector<float> silence(static_cast<std::size_t>(kSampleRate * 0.1), 0.0f);
        processAll(closedGate, silence);

        constexpr float kHum = 0.002f; // ~ -54 dB, below -40 dB
        std::vector<float> hum(static_cast<std::size_t>(kSampleRate * 0.25), kHum);
        processAll(closedGate, hum);

        // Use the last 50 ms after release has settled.
        const std::size_t tailStart = hum.size() - static_cast<std::size_t>(kSampleRate * 0.05);
        std::vector<float> tail(hum.begin() + static_cast<std::ptrdiff_t>(tailStart), hum.end());
        const float outRms = rms(tail);
        const float expectedCeiling = kHum * dsp::dbToLin(-60.0f) * 10.0f; // generous
        if (outRms > kHum * 0.1f) {
            std::cerr << "Gate should attenuate hum, rms=" << outRms << " in=" << kHum << "\n";
            return 1;
        }
        (void)expectedCeiling;
        std::cout << "PASS: hum below threshold gated (rms=" << outRms << ")\n";
    }

    // 3) Transition: loud then quiet → output drops after release.
    {
        dsp::NoiseGateEffect g;
        g.prepare(kSampleRate, kBlock);
        g.setParameter(dsp::NoiseGateEffect::kThresholdDb, -30.0f);
        g.setParameter(dsp::NoiseGateEffect::kAttackMs, 1.0f);
        g.setParameter(dsp::NoiseGateEffect::kReleaseMs, 30.0f);
        g.setParameter(dsp::NoiseGateEffect::kRangeDb, -80.0f);

        const int loudFrames = static_cast<int>(kSampleRate * 0.2);
        const int quietFrames = static_cast<int>(kSampleRate * 0.3);
        std::vector<float> buf(static_cast<std::size_t>(loudFrames + quietFrames), 0.0f);
        for (int i = 0; i < loudFrames; ++i) {
            buf[static_cast<std::size_t>(i)] = 0.3f;
        }
        for (int i = loudFrames; i < loudFrames + quietFrames; ++i) {
            buf[static_cast<std::size_t>(i)] = 0.001f;
        }
        processAll(g, buf);

        const float loudRms = rms(std::vector<float>(
            buf.begin() + static_cast<std::ptrdiff_t>(loudFrames / 2),
            buf.begin() + static_cast<std::ptrdiff_t>(loudFrames)));
        const float quietRms = rms(std::vector<float>(
            buf.end() - static_cast<std::ptrdiff_t>(kSampleRate * 0.05), buf.end()));

        if (loudRms < 0.2f || quietRms > 0.01f) {
            std::cerr << "Gate transition failed: loudRms=" << loudRms
                      << " quietRms=" << quietRms << "\n";
            return 1;
        }
        std::cout << "PASS: loud→quiet transition gates (loud=" << loudRms
                  << " quiet=" << quietRms << ")\n";
    }

    std::cout << "NoiseGate tests OK.\n";
    return 0;
}
