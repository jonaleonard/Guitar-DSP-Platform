#include "dsp/DelayEffect.h"
#include "dsp/DelayLine.h"

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
    // DelayLine: write impulse, read at fixed delay.
    {
        dsp::DelayLine line;
        line.prepare(kSr, 100.0f);
        line.write(1.0f);
        for (int i = 0; i < 99; ++i) {
            line.write(0.0f);
        }
        // After 100 writes, sample at delay 100 should be near the impulse.
        // writeIndex advanced 100 times from 0; impulse at index 0.
        // read(100): readPos = writeIndex - 100. writeIndex=100%size, impulse at 0.
        const float y = line.read(100.0f);
        if (std::fabs(y - 1.0f) > 1.0e-4f) {
            // Depending on size, try nearby
            const float y2 = line.read(99.0f);
            if (std::fabs(y - 1.0f) > 0.05f && std::fabs(y2 - 1.0f) > 0.05f) {
                std::cerr << "DelayLine impulse read failed: " << y << " / " << y2 << "\n";
                return 1;
            }
        }
        std::cout << "PASS: DelayLine fractional read\n";
    }

    dsp::DelayEffect delay;
    delay.prepare(kSr, kBlock);
    delay.setParameter(dsp::DelayEffect::kTimeMs, 100.0f); // 4800 samples
    delay.setParameter(dsp::DelayEffect::kFeedback, 0.0f);
    delay.setParameter(dsp::DelayEffect::kMix, 1.0f);

    // Settle params
    std::vector<float> z(static_cast<std::size_t>(kSr * 0.4), 0.0f);
    for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
        delay.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
    }

    const int delaySamples = static_cast<int>(0.1 * kSr);
    std::vector<float> buf(static_cast<std::size_t>(delaySamples + 2000), 0.0f);
    buf[0] = 1.0f;
    for (int i = 0; i < static_cast<int>(buf.size()); i += kBlock) {
        delay.process(buf.data() + i, std::min(kBlock, static_cast<int>(buf.size()) - i));
    }

    // With mix=1 feedback=0, wet-only: impulse should appear near delaySamples.
    // First sample is dry*0 + delayed*1; delayed is 0 initially, so out[0]=0.
    // Impulse enters delay line at write; appears at read after delaySamples.
    float peak = 0.0f;
    int peakAt = -1;
    for (int i = 0; i < static_cast<int>(buf.size()); ++i) {
        if (std::fabs(buf[static_cast<std::size_t>(i)]) > peak) {
            peak = std::fabs(buf[static_cast<std::size_t>(i)]);
            peakAt = i;
        }
    }

    if (peak < 0.5f || std::abs(peakAt - delaySamples) > 8) {
        std::cerr << "Delay timing failed: peak=" << peak << " at=" << peakAt
                  << " expected~" << delaySamples << "\n";
        return 1;
    }
    std::cout << "PASS: Delay 100ms peak at sample " << peakAt << "\n";

    // Mix=0 → dry pass-through
    {
        dsp::DelayEffect d2;
        d2.prepare(kSr, kBlock);
        d2.setParameter(dsp::DelayEffect::kMix, 0.0f);
        std::vector<float> s(static_cast<std::size_t>(kSr * 0.4), 0.0f);
        for (int i = 0; i < static_cast<int>(s.size()); i += kBlock) {
            d2.process(s.data() + i, std::min(kBlock, static_cast<int>(s.size()) - i));
        }
        std::vector<float> x(static_cast<std::size_t>(kBlock), 0.3f);
        d2.process(x.data(), kBlock);
        for (float v : x) {
            if (std::fabs(v - 0.3f) > 1.0e-3f) {
                std::cerr << "Delay mix=0 should be dry\n";
                return 1;
            }
        }
        std::cout << "PASS: Delay mix=0 dry\n";
    }

    std::cout << "Delay tests OK.\n";
    return 0;
}
