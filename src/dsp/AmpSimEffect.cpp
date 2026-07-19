#include "dsp/AmpSimEffect.h"

#include "dsp/OverdriveEffect.h"

#include <algorithm>
#include <cmath>

namespace dsp {

AmpSimEffect::AmpSimEffect()
{
    // Neutral / near-clean defaults — coloration comes from user edits.
    preGain_.reset(1.0f);
    drive_.reset(1.0f);
    bassDb_.reset(0.0f);
    midDb_.reset(0.0f);
    trebleDb_.reset(0.0f);
    presenceDb_.reset(0.0f);
    master_.reset(1.0f);
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
    postDriveLp_.reset();
    toneLow_.reset();
    toneMid_.reset();
    toneHigh_.reset();
    presence_.reset();

    // Mild pre emphasis only — keep flat to avoid baked-in harshness.
    preLow_.setCoefficients(Biquad::design(BiquadType::LowShelf, sampleRate_, 100.0f, 0.7f, 0.0f));
    preHigh_.setCoefficients(Biquad::design(BiquadType::HighShelf, sampleRate_, 5000.0f, 0.7f, 0.0f));
    // Soft speaker-ish roll-off after clip stages — kills whistle without muffling mids.
    postDriveLp_.setCoefficients(Biquad::design(BiquadType::LowPass, sampleRate_, 7500.0f, 0.707f, 0.0f));
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
        Biquad::design(BiquadType::HighShelf, sampleRate_, 4200.0f, 0.7f, presenceDb_.getCurrent()));
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

        // Soft clip stages with inter-stage attenuation (studio gain staging).
        x = OverdriveEffect::waveshape(x, drive);
        x *= 0.85f;
        x = OverdriveEffect::waveshape(x, std::max(1.0f, 1.0f + (drive - 1.0f) * 0.22f));
        x = postDriveLp_.process(x);

        // Soft ceiling before tone stack so EQ/presence can't recreate digital clips.
        if (x > 0.9f) {
            x = 0.9f + 0.1f * std::tanh((x - 0.9f) * 4.0f);
        } else if (x < -0.9f) {
            x = -0.9f - 0.1f * std::tanh((-x - 0.9f) * 4.0f);
        }

        x = toneLow_.process(x);
        x = toneMid_.process(x);
        x = toneHigh_.process(x);
        x = presence_.process(x);
        buffer[i] = x * master;
    }
}

} // namespace dsp
