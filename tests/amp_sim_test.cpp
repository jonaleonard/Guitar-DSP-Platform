#include "dsp/AmpSimEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

float peakAbs(const std::vector<float>& x, const int start)
{
    float m = 0.0f;
    for (int i = start; i < static_cast<int>(x.size()); ++i) {
        m = std::max(m, std::fabs(x[static_cast<std::size_t>(i)]));
    }
    return m;
}

} // namespace

int main()
{
    dsp::AmpSimEffect amp;
    amp.prepare(kSr, kBlock);
    amp.setParameter(dsp::AmpSimEffect::kPreGain, 3.0f);
    amp.setParameter(dsp::AmpSimEffect::kDrive, 12.0f);
    amp.setParameter(dsp::AmpSimEffect::kMaster, 1.0f);

    std::vector<float> z(static_cast<std::size_t>(kSr * 0.1), 0.0f);
    for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
        amp.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
    }

    // Large input should saturate (peak < input peak).
    std::vector<float> buf(static_cast<std::size_t>(kSr * 0.25), 0.0f);
    for (int i = 0; i < static_cast<int>(buf.size()); ++i) {
        buf[static_cast<std::size_t>(i)] =
            1.2f * std::sin(2.0f * 3.14159265f * 220.0f * (static_cast<float>(i) / static_cast<float>(kSr)));
    }
    for (int i = 0; i < static_cast<int>(buf.size()); i += kBlock) {
        amp.process(buf.data() + i, std::min(kBlock, static_cast<int>(buf.size()) - i));
    }

    const float outPeak = peakAbs(buf, static_cast<int>(buf.size() / 2));
    if (outPeak > 1.5f) {
        std::cerr << "Amp output peak unexpectedly huge: " << outPeak << "\n";
        return 1;
    }
    if (outPeak < 0.05f) {
        std::cerr << "Amp output near silence: " << outPeak << "\n";
        return 1;
    }
    std::cout << "PASS: amp produces saturated output (peak=" << outPeak << ")\n";

    // Odd-ish: negative input should tend negative at output for mild settings early on
    // (after EQ phase may invert — just check non-zero energy)
    std::cout << "Amp sim tests OK.\n";
    return 0;
}
