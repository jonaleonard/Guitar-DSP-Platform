#include "dsp/Biquad.h"
#include "dsp/DspMath.h"
#include "dsp/EqualizerEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;
constexpr double kPi = 3.14159265358979323846;

float measureRmsAtFreq(dsp::EqualizerEffect& eq, const float freqHz, const float seconds)
{
    const int n = static_cast<int>(seconds * kSr);
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        buf[static_cast<std::size_t>(i)] =
            0.5f * static_cast<float>(std::sin(2.0 * kPi * freqHz * (i / kSr)));
    }
    for (int i = 0; i < n; i += kBlock) {
        eq.process(buf.data() + i, std::min(kBlock, n - i));
    }
    // Tail RMS after settling
    const int start = n / 2;
    double sum = 0.0;
    for (int i = start; i < n; ++i) {
        const double v = buf[static_cast<std::size_t>(i)];
        sum += v * v;
    }
    return static_cast<float>(std::sqrt(sum / (n - start)));
}

} // namespace

int main()
{
    // Peak filter at 1 kHz +12 dB should boost a 1 kHz sine vs flat.
    dsp::EqualizerEffect flat;
    flat.prepare(kSr, kBlock);

    dsp::EqualizerEffect boosted;
    boosted.prepare(kSr, kBlock);
    boosted.setParameter(dsp::EqualizerEffect::kMidFreqHz, 1000.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidQ, 1.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidGainDb, 12.0f);

    // Settle param ramps
    std::vector<float> z(static_cast<std::size_t>(kSr * 0.1), 0.0f);
    for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
        flat.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
        boosted.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
    }

    const float rmsFlat = measureRmsAtFreq(flat, 1000.0f, 0.5f);
    // Reset boosted state by re-preparing and settling gains again
    boosted.prepare(kSr, kBlock);
    boosted.setParameter(dsp::EqualizerEffect::kMidFreqHz, 1000.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidQ, 1.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidGainDb, 12.0f);
    for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
        boosted.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
    }
    const float rmsBoost = measureRmsAtFreq(boosted, 1000.0f, 0.5f);

    const float ratio = rmsBoost / std::max(rmsFlat, 1.0e-8f);
    const float ratioDb = dsp::linToDb(ratio);
    // Expect roughly +12 dB (±3 dB tolerance for smoothing / Q)
    if (ratioDb < 8.0f || ratioDb > 15.0f) {
        std::cerr << "EQ mid boost mismatch: ratioDb=" << ratioDb << " (flat=" << rmsFlat
                  << " boost=" << rmsBoost << ")\n";
        return 1;
    }
    std::cout << "PASS: mid +12 dB boost ≈ " << ratioDb << " dB at 1 kHz\n";

    // Unity coeffs at 0 dB: biquad peak 0 dB ≈ pass-through for steady sine
    const auto c = dsp::Biquad::design(dsp::BiquadType::Peak, kSr, 1000.0f, 0.7f, 0.0f);
    if (std::fabs(c.b0 - 1.0f) > 0.05f) {
        std::cerr << "0 dB peak b0 unexpected: " << c.b0 << "\n";
        return 1;
    }
    std::cout << "PASS: 0 dB peak design near unity\n";

    std::cout << "Equalizer tests OK.\n";
    return 0;
}
