#include "dsp/ChorusEffect.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;

} // namespace

int main()
{
    // mix=0 dry
    {
        dsp::ChorusEffect ch;
        ch.prepare(kSr, kBlock);
        ch.setParameter(dsp::ChorusEffect::kMix, 0.0f);
        std::vector<float> z(static_cast<std::size_t>(kSr * 0.4), 0.0f);
        for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
            ch.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
        }
        std::vector<float> x(static_cast<std::size_t>(kBlock), 0.25f);
        ch.process(x.data(), kBlock);
        for (float v : x) {
            if (std::fabs(v - 0.25f) > 1.0e-3f) {
                std::cerr << "Chorus mix=0 should be dry\n";
                return 1;
            }
        }
        std::cout << "PASS: Chorus mix=0 dry\n";
    }

    // mix=1 modulates — output should differ from constant input over time
    {
        dsp::ChorusEffect ch;
        ch.prepare(kSr, kBlock);
        ch.setParameter(dsp::ChorusEffect::kRateHz, 2.0f);
        ch.setParameter(dsp::ChorusEffect::kDepthMs, 6.0f);
        ch.setParameter(dsp::ChorusEffect::kMix, 1.0f);

        std::vector<float> z(static_cast<std::size_t>(kSr * 0.4), 0.0f);
        for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
            ch.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
        }

        std::vector<float> buf(static_cast<std::size_t>(kSr / 2), 0.0f);
        for (int i = 0; i < static_cast<int>(buf.size()); ++i) {
            buf[static_cast<std::size_t>(i)] = 0.4f; // DC into chorus → LFO-varying delay of DC is still DC-ish
            // Use a sine so modulation creates audible difference from dry
            buf[static_cast<std::size_t>(i)] =
                0.4f * std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / static_cast<float>(kSr)));
        }
        std::vector<float> dry = buf;
        for (int i = 0; i < static_cast<int>(buf.size()); i += kBlock) {
            ch.process(buf.data() + i, std::min(kBlock, static_cast<int>(buf.size()) - i));
        }

        double diff = 0.0;
        for (int i = static_cast<int>(buf.size() / 4); i < static_cast<int>(buf.size()); ++i) {
            diff += std::fabs(buf[static_cast<std::size_t>(i)] - dry[static_cast<std::size_t>(i)]);
        }
        if (diff < 1.0) {
            std::cerr << "Chorus should modulate signal, diffEnergy=" << diff << "\n";
            return 1;
        }
        std::cout << "PASS: Chorus modulates (diffEnergy=" << diff << ")\n";
    }

    std::cout << "Chorus tests OK.\n";
    return 0;
}
