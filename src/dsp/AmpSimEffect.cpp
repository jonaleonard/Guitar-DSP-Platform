#include "dsp/AmpSimEffect.h"

#include "dsp/OverdriveEffect.h"

#include <algorithm>
#include <cmath>

namespace dsp {

AmpSimEffect::AmpSimEffect()
{
    preGain_.reset(2.0f);
    drive_.reset(6.0f);
    bassDb_.reset(2.0f);
    midDb_.reset(0.0f);
    trebleDb_.reset(1.0f);
    presenceDb_.reset(0.0f);
    master_.reset(0.6f);
}

void AmpSimEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = std::max(1.0, sampleRate);
    preGain_.prepare(sampleRate_);
    drive_.prepare(sampleRate_);
    bassDb_.prepare(sampleRate_);
    midDb_.prepare(sampleRate_);
    trebleDb_.prepare(sampleRate_);
    presenceDb_.prepare(sampleRate_);
    master_.prepare(sampleRate_);

    preLow_.reset();
    preHigh_.reset();
    toneLow_.reset();
    toneMid_.reset();
    toneHigh_.reset();
    presence_.reset();

    // Fixed mild pre-EQ: scoop a little mud, tame extreme highs before drive.
    preLow_.setCoefficients(Biquad::design(BiquadType::LowShelf, sampleRate_, 100.0f, 0.7f, -2.0f));
    preHigh_.setCoefficients(Biquad::design(BiquadType::HighShelf, sampleRate_, 5000.0f, 0.7f, -3.0f));
    updateTone();
}

void AmpSimEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kPreGain:
        preGain_.setTarget(std::clamp(value, 0.0f, 10.0f));
        break;
    case kDrive:
        drive_.setTarget(std::clamp(value, 1.0f, 25.0f));
        break;
    case kBassDb:
        bassDb_.setTarget(std::clamp(value, -12.0f, 12.0f));
        break;
    case kMidDb:
        midDb_.setTarget(std::clamp(value, -12.0f, 12.0f));
        break;
    case kTrebleDb:
        trebleDb_.setTarget(std::clamp(value, -12.0f, 12.0f));
        break;
    case kPresenceDb:
        presenceDb_.setTarget(std::clamp(value, -12.0f, 12.0f));
        break;
    case kMaster:
        master_.setTarget(std::clamp(value, 0.0f, 2.0f));
        break;
    default:
        break;
    }
}

void AmpSimEffect::updateTone()
{
    toneLow_.setCoefficients(
        Biquad::design(BiquadType::LowShelf, sampleRate_, 120.0f, 0.7f, bassDb_.getCurrent()));
    toneMid_.setCoefficients(
        Biquad::design(BiquadType::Peak, sampleRate_, 800.0f, 0.8f, midDb_.getCurrent()));
    toneHigh_.setCoefficients(
        Biquad::design(BiquadType::HighShelf, sampleRate_, 3500.0f, 0.7f, trebleDb_.getCurrent()));
    presence_.setCoefficients(
        Biquad::design(BiquadType::HighShelf, sampleRate_, 4500.0f, 0.7f, presenceDb_.getCurrent()));
}

void AmpSimEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        const float preGain = preGain_.getNext();
        const float drive = drive_.getNext();
        bassDb_.getNext();
        midDb_.getNext();
        trebleDb_.getNext();
        presenceDb_.getNext();
        const float master = master_.getNext();

        if (bassDb_.isSmoothing() || midDb_.isSmoothing() || trebleDb_.isSmoothing() ||
            presenceDb_.isSmoothing()) {
            updateTone();
        }

        float x = buffer[i];
        x = preLow_.process(x);
        x = preHigh_.process(x);
        x *= preGain;

        // Two soft stages for a bit more amp-like saturation.
        x = OverdriveEffect::waveshape(x, drive);
        x = OverdriveEffect::waveshape(x, std::max(1.0f, drive * 0.5f));

        x = toneLow_.process(x);
        x = toneMid_.process(x);
        x = toneHigh_.process(x);
        x = presence_.process(x);
        buffer[i] = x * master;
    }
}

} // namespace dsp
