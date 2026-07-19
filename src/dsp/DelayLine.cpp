#include "dsp/DelayLine.h"

#include <algorithm>
#include <cmath>

namespace dsp {

void DelayLine::prepare(const double sampleRate, const float maxDelayMs)
{
    sampleRate_ = std::max(1.0, sampleRate);
    const int needed =
        std::max(4, static_cast<int>(std::ceil(sampleRate_ * static_cast<double>(maxDelayMs) * 0.001)) + 4);
    buffer_.assign(static_cast<std::size_t>(needed), 0.0f);
    size_ = needed;
    writeIndex_ = 0;
}

void DelayLine::clear()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
}

void DelayLine::write(const float sample)
{
    if (size_ <= 0) {
        return;
    }
    buffer_[static_cast<std::size_t>(writeIndex_)] = sample;
    writeIndex_ = (writeIndex_ + 1) % size_;
}

float DelayLine::read(const float delaySamples) const
{
    if (size_ <= 0) {
        return 0.0f;
    }

    float d = std::clamp(delaySamples, 0.0f, static_cast<float>(size_ - 1));
    float readPos = static_cast<float>(writeIndex_) - d;
    while (readPos < 0.0f) {
        readPos += static_cast<float>(size_);
    }

    const int i0 = static_cast<int>(readPos) % size_;
    const int i1 = (i0 + 1) % size_;
    const float frac = readPos - std::floor(readPos);
    const float a = buffer_[static_cast<std::size_t>(i0)];
    const float b = buffer_[static_cast<std::size_t>(i1)];
    return a + frac * (b - a);
}

float DelayLine::readMs(const float delayMs) const
{
    return read(delayMs * 0.001f * static_cast<float>(sampleRate_));
}

} // namespace dsp
