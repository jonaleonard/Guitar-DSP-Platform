#include "dsp/EqualizerEffect.h"

#include <algorithm>
#include <cmath>

namespace dsp {

EqualizerEffect::EqualizerEffect()
{
    lowGainDb_.reset(0.0f);
    lowFreqHz_.reset(120.0f);
    midGainDb_.reset(0.0f);
    midFreqHz_.reset(800.0f);
    midQ_.reset(0.7f);
    highGainDb_.reset(0.0f);
    highFreqHz_.reset(4000.0f);
}

void EqualizerEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = std::max(1.0, sampleRate);
    lowGainDb_.prepare(sampleRate_);
    lowFreqHz_.prepare(sampleRate_);
    midGainDb_.prepare(sampleRate_);
    midFreqHz_.prepare(sampleRate_);
    midQ_.prepare(sampleRate_);
    highGainDb_.prepare(sampleRate_);
    highFreqHz_.prepare(sampleRate_);
    low_.reset();
    mid_.reset();
    high_.reset();
    coeffsDirty_ = true;
    updateCoefficients();
}

void EqualizerEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kLowGainDb:
        lowGainDb_.setTarget(std::clamp(value, -18.0f, 18.0f));
        break;
    case kLowFreqHz:
        lowFreqHz_.setTarget(std::clamp(value, 40.0f, 500.0f));
        break;
    case kMidGainDb:
        midGainDb_.setTarget(std::clamp(value, -18.0f, 18.0f));
        break;
    case kMidFreqHz:
        midFreqHz_.setTarget(std::clamp(value, 200.0f, 4000.0f));
        break;
    case kMidQ:
        midQ_.setTarget(std::clamp(value, 0.2f, 8.0f));
        break;
    case kHighGainDb:
        highGainDb_.setTarget(std::clamp(value, -18.0f, 18.0f));
        break;
    case kHighFreqHz:
        highFreqHz_.setTarget(std::clamp(value, 1500.0f, 12000.0f));
        break;
    default:
        break;
    }
    coeffsDirty_ = true;
}

void EqualizerEffect::updateCoefficients()
{
    low_.setCoefficients(Biquad::design(BiquadType::LowShelf,
                                        sampleRate_,
                                        lowFreqHz_.getCurrent(),
                                        0.707f,
                                        lowGainDb_.getCurrent()));
    mid_.setCoefficients(Biquad::design(BiquadType::Peak,
                                        sampleRate_,
                                        midFreqHz_.getCurrent(),
                                        midQ_.getCurrent(),
                                        midGainDb_.getCurrent()));
    high_.setCoefficients(Biquad::design(BiquadType::HighShelf,
                                         sampleRate_,
                                         highFreqHz_.getCurrent(),
                                         0.707f,
                                         highGainDb_.getCurrent()));
    coeffsDirty_ = false;
}

void EqualizerEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        lowGainDb_.getNext();
        lowFreqHz_.getNext();
        midGainDb_.getNext();
        midFreqHz_.getNext();
        midQ_.getNext();
        highGainDb_.getNext();
        highFreqHz_.getNext();

        // Update coeffs occasionally while params move (every sample is fine / cheap).
        if (coeffsDirty_ || lowGainDb_.isSmoothing() || midGainDb_.isSmoothing() ||
            highGainDb_.isSmoothing() || midFreqHz_.isSmoothing() || midQ_.isSmoothing() ||
            lowFreqHz_.isSmoothing() || highFreqHz_.isSmoothing()) {
            updateCoefficients();
        }

        float x = buffer[i];
        x = low_.process(x);
        x = mid_.process(x);
        x = high_.process(x);
        buffer[i] = x;
    }
}

} // namespace dsp
