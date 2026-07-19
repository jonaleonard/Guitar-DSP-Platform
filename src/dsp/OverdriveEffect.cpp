#include "dsp/OverdriveEffect.h"

#include "dsp/Biquad.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

// Asymmetric soft clip — even harmonics, more "tube pedal" than pure tanh.
float musicalWaveshape(const float x, const float drive)
{
    const float d = std::max(1.0f, drive);
    const float biased = x + 0.08f * x * x;
    const float shaped = std::tanh(biased * d);
    const float norm = std::tanh(d);
    if (norm < 1.0e-8f) {
        return x;
    }
    return shaped / norm;
}

} // namespace

OverdriveEffect::OverdriveEffect()
{
    drive_.reset(1.0f);
    mix_.reset(0.0f);
    output_.reset(1.0f);
}

void OverdriveEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    drive_.prepare(sampleRate, 18.0f);
    mix_.prepare(sampleRate, 15.0f);
    output_.prepare(sampleRate, 18.0f);
    preHp_.reset();
    preLp_.reset();
    midBump_.reset();
    postLp_.reset();
    // Tube Screamer DNA: cut mud, push upper-mids into the diodes, tame fizz after.
    // Low-shelf (not brick HPF) keeps some body for blues while still tightening metal boosts.
    preHp_.setCoefficients(Biquad::design(BiquadType::HighPass, sampleRate, 320.0f, 0.707f, 0.0f));
    preLp_.setCoefficients(Biquad::design(BiquadType::LowPass, sampleRate, 6500.0f, 0.7f, 0.0f));
    midBump_.setCoefficients(Biquad::design(BiquadType::Peak, sampleRate, 720.0f, 0.85f, 5.0f));
    postLp_.setCoefficients(Biquad::design(BiquadType::LowPass, sampleRate, 5200.0f, 0.7f, 0.0f));
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
    return musicalWaveshape(x, drive);
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
        // Classic TS signal path: HP → mid bump → soft clip → post LP.
        float wet = preHp_.process(dry);
        wet = midBump_.process(wet);
        wet = preLp_.process(wet);
        wet = musicalWaveshape(wet, drive);
        // Light pad only at high drive — low-gain / high-level = real boost into the amp.
        const float wetLevel = 1.0f / (1.0f + 0.06f * (drive - 1.0f));
        wet = postLp_.process(wet * wetLevel);

        const float dryPad = 1.0f / (1.0f + 0.2f * (drive - 1.0f) * mix);
        buffer[i] = (dry * (1.0f - mix) * dryPad + wet * mix) * output;
    }
}

} // namespace dsp
