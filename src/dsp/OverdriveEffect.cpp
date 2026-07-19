#include "dsp/OverdriveEffect.h"

#include <algorithm>
#include <cmath>

namespace dsp {

OverdriveEffect::OverdriveEffect()
{
    drive_.reset(4.0f);
    mix_.reset(1.0f);
    output_.reset(0.7f);
}

void OverdriveEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    drive_.prepare(sampleRate);
    mix_.prepare(sampleRate);
    output_.prepare(sampleRate);
}

void OverdriveEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kDrive:
        drive_.setTarget(std::clamp(value, 1.0f, 25.0f));
        break;
    case kMix:
        mix_.setTarget(std::clamp(value, 0.0f, 1.0f));
        break;
    case kOutput:
        output_.setTarget(std::clamp(value, 0.0f, 2.0f));
        break;
    default:
        break;
    }
}

float OverdriveEffect::waveshape(const float x, const float drive)
{
    const float d = std::max(1.0f, drive);
    const float norm = std::tanh(d);
    if (norm < 1.0e-8f) {
        return x;
    }
    return std::tanh(x * d) / norm;
}

void OverdriveEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        const float drive = drive_.getNext();
        const float mix = mix_.getNext();
        const float output = output_.getNext();

        const float dry = buffer[i];
        const float wet = waveshape(dry, drive);
        buffer[i] = (dry * (1.0f - mix) + wet * mix) * output;
    }
}

} // namespace dsp
