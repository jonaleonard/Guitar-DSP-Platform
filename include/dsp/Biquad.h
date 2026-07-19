#pragma once

#include <cmath>

namespace dsp {

enum class BiquadType {
    LowShelf,
    HighShelf,
    Peak,
    LowPass,
    HighPass
};

struct BiquadCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

// Direct Form I biquad. Real-time safe process().
class Biquad {
public:
    void setCoefficients(const BiquadCoeffs& c);
    void reset();
    float process(float x);

    // RBJ Audio EQ Cookbook
    static BiquadCoeffs design(BiquadType type,
                              double sampleRate,
                              float freqHz,
                              float q,
                              float gainDb);

private:
    BiquadCoeffs c_{};
    float x1_ = 0.0f;
    float x2_ = 0.0f;
    float y1_ = 0.0f;
    float y2_ = 0.0f;
};

} // namespace dsp
