#pragma once

#include <cstddef>
#include <vector>

namespace dsp {

// In-place radix-2 complex FFT / inverse FFT.
// length must be a power of two. Arrays are interleaved? We use separate real/imag.
class Fft {
public:
    explicit Fft(int size);

    int size() const { return size_; }

    // Forward: time -> freq (real/imag length = size_)
    void forward(float* real, float* imag) const;
    // Inverse (includes 1/N scaling)
    void inverse(float* real, float* imag) const;

    static bool isPowerOfTwo(int n);
    static int nextPowerOfTwo(int n);

private:
    void transform(float* real, float* imag, bool inverse) const;

    int size_ = 0;
    std::vector<float> cosTable_;
    std::vector<float> sinTable_;
    std::vector<int> bitrev_;
};

} // namespace dsp
