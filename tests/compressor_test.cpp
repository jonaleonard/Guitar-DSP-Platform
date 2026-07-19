#include "dsp/CompressorEffect.h"
#include "dsp/DspMath.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;

void processAll(dsp::CompressorEffect& comp, std::vector<float>& buf)
{
    for (std::size_t i = 0; i < buf.size(); i += static_cast<std::size_t>(kBlock)) {
        const int n = static_cast<int>(std::min(buf.size() - i, static_cast<std::size_t>(kBlock)));
        comp.process(buf.data() + i, n);
    }
}

float meanAbsTail(const std::vector<float>& buf, const double tailSeconds)
{
    const std::size_t n = static_cast<std::size_t>(tailSeconds * kSampleRate);
    const std::size_t start = buf.size() > n ? buf.size() - n : 0;
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = start; i < buf.size(); ++i) {
        sum += std::fabs(buf[i]);
        ++count;
    }
    return count == 0 ? 0.0f : static_cast<float>(sum / static_cast<double>(count));
}

} // namespace

int main()
{
    constexpr float kThresholdDb = -12.0f; // ≈ 0.2512 linear
    constexpr float kRatio = 4.0f;
    constexpr float kInput = 0.5f; // ≈ -6 dB, 6 dB above threshold

    // Expected steady-state (peak, hard knee, no makeup):
    // excess = (-6) - (-12) = 6 dB
    // gainDb = -6 * (1 - 1/4) = -4.5 dB
    // out = 0.5 * 10^(-4.5/20) ≈ 0.5 * 0.5957 ≈ 0.2978
    const float inputDb = dsp::linToDb(kInput);
    const float excess = inputDb - kThresholdDb;
    const float expectedGainDb = -excess * (1.0f - (1.0f / kRatio));
    const float expectedOut = kInput * dsp::dbToLin(expectedGainDb);

    dsp::CompressorEffect comp;
    comp.prepare(kSampleRate, kBlock);
    comp.setParameter(dsp::CompressorEffect::kThresholdDb, kThresholdDb);
    comp.setParameter(dsp::CompressorEffect::kRatio, kRatio);
    comp.setParameter(dsp::CompressorEffect::kAttackMs, 1.0f);
    comp.setParameter(dsp::CompressorEffect::kReleaseMs, 50.0f);
    comp.setParameter(dsp::CompressorEffect::kMakeupDb, 0.0f);

    // Settle parameter ramps on silence, then apply constant step.
    std::vector<float> silence(static_cast<std::size_t>(kSampleRate * 0.05), 0.0f);
    processAll(comp, silence);

    std::vector<float> step(static_cast<std::size_t>(kSampleRate * 0.5), kInput);
    processAll(comp, step);

    const float measured = meanAbsTail(step, 0.1);
    const float relErr = std::fabs(measured - expectedOut) / expectedOut;
    if (relErr > 0.08f) {
        std::cerr << "Compressor steady-state mismatch: measured=" << measured
                  << " expected=" << expectedOut << " relErr=" << relErr << "\n";
        return 1;
    }
    std::cout << "PASS: steady-state gain matches formula (out=" << measured
              << " expected=" << expectedOut << ")\n";

    // Makeup gain: +6 dB should scale steady-state by ~2.
    {
        dsp::CompressorEffect c2;
        c2.prepare(kSampleRate, kBlock);
        c2.setParameter(dsp::CompressorEffect::kThresholdDb, kThresholdDb);
        c2.setParameter(dsp::CompressorEffect::kRatio, kRatio);
        c2.setParameter(dsp::CompressorEffect::kAttackMs, 1.0f);
        c2.setParameter(dsp::CompressorEffect::kReleaseMs, 50.0f);
        c2.setParameter(dsp::CompressorEffect::kMakeupDb, 6.0f);

        std::vector<float> z(static_cast<std::size_t>(kSampleRate * 0.05), 0.0f);
        processAll(c2, z);
        std::vector<float> s(static_cast<std::size_t>(kSampleRate * 0.5), kInput);
        processAll(c2, s);
        const float withMakeup = meanAbsTail(s, 0.1);
        const float expectedMakeup = expectedOut * dsp::dbToLin(6.0f);
        const float err = std::fabs(withMakeup - expectedMakeup) / expectedMakeup;
        if (err > 0.08f) {
            std::cerr << "Makeup mismatch: got=" << withMakeup << " expected=" << expectedMakeup
                      << "\n";
            return 1;
        }
        std::cout << "PASS: makeup +6 dB (out=" << withMakeup << ")\n";
    }

    // Below threshold: near-unity (no reduction).
    {
        dsp::CompressorEffect c3;
        c3.prepare(kSampleRate, kBlock);
        c3.setParameter(dsp::CompressorEffect::kThresholdDb, -6.0f);
        c3.setParameter(dsp::CompressorEffect::kRatio, 8.0f);
        c3.setParameter(dsp::CompressorEffect::kAttackMs, 1.0f);
        c3.setParameter(dsp::CompressorEffect::kReleaseMs, 50.0f);
        c3.setParameter(dsp::CompressorEffect::kMakeupDb, 0.0f);

        std::vector<float> z(static_cast<std::size_t>(kSampleRate * 0.05), 0.0f);
        processAll(c3, z);
        constexpr float kQuiet = 0.1f; // -20 dB, below -6 dB threshold
        std::vector<float> s(static_cast<std::size_t>(kSampleRate * 0.4), kQuiet);
        processAll(c3, s);
        const float out = meanAbsTail(s, 0.1);
        if (std::fabs(out - kQuiet) / kQuiet > 0.05f) {
            std::cerr << "Below-threshold should be unity: out=" << out << "\n";
            return 1;
        }
        std::cout << "PASS: below threshold ≈ unity (out=" << out << ")\n";
    }

    std::cout << "Compressor tests OK.\n";
    return 0;
}
