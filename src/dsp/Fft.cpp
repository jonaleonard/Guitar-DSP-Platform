#include "dsp/Fft.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace dsp {

Fft::Fft(const int size)
    : size_(size)
{
    if (!isPowerOfTwo(size) || size < 2) {
        throw std::invalid_argument("Fft size must be power of two >= 2");
    }

    cosTable_.resize(static_cast<std::size_t>(size_ / 2));
    sinTable_.resize(static_cast<std::size_t>(size_ / 2));
    bitrev_.resize(static_cast<std::size_t>(size_));

    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < size_ / 2; ++i) {
        const double angle = -2.0 * kPi * static_cast<double>(i) / static_cast<double>(size_);
        cosTable_[static_cast<std::size_t>(i)] = static_cast<float>(std::cos(angle));
        sinTable_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(angle));
    }

    int j = 0;
    bitrev_[0] = 0;
    for (int i = 1; i < size_; ++i) {
        int bit = size_ >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        bitrev_[static_cast<std::size_t>(i)] = j;
    }
}

bool Fft::isPowerOfTwo(const int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

int Fft::nextPowerOfTwo(int n)
{
    if (n <= 1) {
        return 2;
    }
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

void Fft::forward(float* real, float* imag) const
{
    transform(real, imag, false);
}

void Fft::inverse(float* real, float* imag) const
{
    transform(real, imag, true);
    const float scale = 1.0f / static_cast<float>(size_);
    for (int i = 0; i < size_; ++i) {
        real[i] *= scale;
        imag[i] *= scale;
    }
}

void Fft::transform(float* real, float* imag, const bool inverse) const
{
    for (int i = 0; i < size_; ++i) {
        const int j = bitrev_[static_cast<std::size_t>(i)];
        if (j > i) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    for (int len = 2; len <= size_; len <<= 1) {
        const int half = len >> 1;
        const int tableStep = size_ / len;
        for (int i = 0; i < size_; i += len) {
            for (int j = 0; j < half; ++j) {
                const int idx = j * tableStep;
                float wr = cosTable_[static_cast<std::size_t>(idx)];
                float wi = sinTable_[static_cast<std::size_t>(idx)];
                if (inverse) {
                    wi = -wi;
                }

                const int i0 = i + j;
                const int i1 = i0 + half;
                const float tr = wr * real[i1] - wi * imag[i1];
                const float ti = wr * imag[i1] + wi * real[i1];
                real[i1] = real[i0] - tr;
                imag[i1] = imag[i0] - ti;
                real[i0] += tr;
                imag[i0] += ti;
            }
        }
    }
}

} // namespace dsp
