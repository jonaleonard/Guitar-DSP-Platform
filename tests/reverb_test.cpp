#include "dsp/ReverbEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

float energy(const std::vector<float>& x, const int start, const int end)
{
    double s = 0.0;
    for (int i = start; i < end; ++i) {
        const double v = x[static_cast<std::size_t>(i)];
        s += v * v;
    }
    return static_cast<float>(s);
}

} // namespace

int main()
{
    // mix=0 dry
    {
        dsp::ReverbEffect rev;
        rev.prepare(kSr, kBlock);
        rev.setParameter(dsp::ReverbEffect::kMix, 0.0f);
        std::vector<float> z(static_cast<std::size_t>(kSr * 0.3), 0.0f);
        for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
            rev.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
        }
        std::vector<float> x(static_cast<std::size_t>(kBlock), 0.2f);
        rev.process(x.data(), kBlock);
        for (float v : x) {
            if (std::fabs(v - 0.2f) > 1.0e-3f) {
                std::cerr << "Reverb mix=0 should be dry\n";
                return 1;
            }
        }
        std::cout << "PASS: Reverb mix=0 dry\n";
    }

    // Impulse → long tail when wet
    {
        dsp::ReverbEffect rev;
        rev.prepare(kSr, kBlock);
        rev.setParameter(dsp::ReverbEffect::kRoomSize, 0.85f);
        rev.setParameter(dsp::ReverbEffect::kDamping, 0.3f);
        rev.setParameter(dsp::ReverbEffect::kMix, 1.0f);

        std::vector<float> z(static_cast<std::size_t>(kSr * 0.3), 0.0f);
        for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
            rev.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
        }

        std::vector<float> buf(static_cast<std::size_t>(kSr), 0.0f);
        buf[0] = 1.0f;
        for (int i = 0; i < static_cast<int>(buf.size()); i += kBlock) {
            rev.process(buf.data() + i, std::min(kBlock, static_cast<int>(buf.size()) - i));
        }

        const float early = energy(buf, 100, 2000);
        const float late = energy(buf, 20000, 40000);
        if (early < 1.0e-6f) {
            std::cerr << "Reverb early energy too low\n";
            return 1;
        }
        if (late < 1.0e-8f) {
            std::cerr << "Reverb should have a late tail, late=" << late << "\n";
            return 1;
        }
        std::cout << "PASS: Reverb impulse has tail (early=" << early << " late=" << late << ")\n";
    }

    std::cout << "Reverb tests OK.\n";
    return 0;
}
