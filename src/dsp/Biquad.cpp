#include "dsp/Biquad.h"

#include "dsp/DspMath.h"

#include <algorithm>

namespace dsp {

void Biquad::setCoefficients(const BiquadCoeffs& c)
{
    c_ = c;
}

void Biquad::reset()
{
    x1_ = x2_ = y1_ = y2_ = 0.0f;
}

float Biquad::process(const float x)
{
    const float y = c_.b0 * x + c_.b1 * x1_ + c_.b2 * x2_ - c_.a1 * y1_ - c_.a2 * y2_;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    return y;
}

BiquadCoeffs Biquad::design(const BiquadType type,
                            const double sampleRate,
                            const float freqHz,
                            const float q,
                            const float gainDb)
{
    const double sr = std::max(1.0, sampleRate);
    const double f = std::clamp(static_cast<double>(freqHz), 20.0, sr * 0.45);
    const double Q = std::max(0.05, static_cast<double>(q));
    const double A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
    const double w0 = 2.0 * 3.14159265358979323846 * f / sr;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / (2.0 * Q);

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type) {
    case BiquadType::LowShelf: {
        const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAalpha);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAalpha);
        a0 = (A + 1.0) + (A - 1.0) * cosw + twoSqrtAalpha;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
        a2 = (A + 1.0) + (A - 1.0) * cosw - twoSqrtAalpha;
        break;
    }
    case BiquadType::HighShelf: {
        const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtAalpha);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAalpha);
        a0 = (A + 1.0) - (A - 1.0) * cosw + twoSqrtAalpha;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
        a2 = (A + 1.0) - (A - 1.0) * cosw - twoSqrtAalpha;
        break;
    }
    case BiquadType::Peak: {
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosw;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha / A;
        break;
    }
    case BiquadType::LowPass: {
        b0 = (1.0 - cosw) * 0.5;
        b1 = 1.0 - cosw;
        b2 = (1.0 - cosw) * 0.5;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha;
        break;
    }
    }

    BiquadCoeffs c;
    c.b0 = static_cast<float>(b0 / a0);
    c.b1 = static_cast<float>(b1 / a0);
    c.b2 = static_cast<float>(b2 / a0);
    c.a1 = static_cast<float>(a1 / a0);
    c.a2 = static_cast<float>(a2 / a0);
    return c;
}

} // namespace dsp
