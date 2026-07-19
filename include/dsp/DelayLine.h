#pragma once

#include <vector>

namespace dsp {

// Circular delay line with linear interpolation. Allocate only in prepare()/setMaxDelay.
class DelayLine {
public:
    void prepare(double sampleRate, float maxDelayMs);
    void clear();

    void write(float sample);
    float read(float delaySamples) const;
    float readMs(float delayMs) const;

    int size() const { return size_; }
    double sampleRate() const { return sampleRate_; }

private:
    std::vector<float> buffer_;
    int size_ = 0;
    int writeIndex_ = 0;
    double sampleRate_ = 48000.0;
};

} // namespace dsp
