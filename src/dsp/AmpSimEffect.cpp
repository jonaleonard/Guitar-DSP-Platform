#include "dsp/AmpSimEffect.h"

#include "dsp/OverdriveEffect.h"

#include <algorithm>
#include <cmath>

namespace dsp {

AmpSimEffect::AmpSimEffect()
{
    preGain_.reset(1.0f);
    drive_.reset(1.0f);
    bassDb_.reset(0.0f);
    midDb_.reset(0.0f);
    trebleDb_.reset(0.0f);
    presenceDb_.reset(-1.0f);
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

    preHp_.reset();
    preLow_.reset();
    preHigh_.reset();
    postDriveLp_.reset();
    toneLow_.reset();
    toneMid_.reset();
    toneHigh_.reset();
    presence_.reset();
    sagEnv_ = 0.0f;
    lastVoicingT_ = -1.0f;

    updateVoicing();
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

void AmpSimEffect::updateVoicing()
{
    // 0 = warm/clean path, 1 = Metal 2000 / 5150-style high-gain path.
    const float t = std::clamp((drive_.getCurrent() - 3.0f) / 12.0f, 0.0f, 1.0f);
    if (std::fabs(t - lastVoicingT_) < 0.02f && lastVoicingT_ >= 0.0f) {
        return;
    }
    lastVoicingT_ = t;

    // Tight HPF opens with gain (chugs stay defined, not muddy).
    const float hpHz = 55.0f + t * 95.0f;
    preHp_.setCoefficients(Biquad::design(BiquadType::HighPass, sampleRate_, hpHz, 0.707f, 0.0f));

    // Mild low cut into the clipper at high gain (5150 tightness).
    const float lowDb = -0.5f - t * 2.0f;
    preLow_.setCoefficients(Biquad::design(BiquadType::LowShelf, sampleRate_, 90.0f, 0.7f, lowDb));

    // Feed the clipper more upper-mid/air as drive rises (clarity under saturation).
    const float highDb = -1.5f + t * 3.0f;
    const float highHz = 4200.0f + t * 1200.0f;
    preHigh_.setCoefficients(Biquad::design(BiquadType::HighShelf, sampleRate_, highHz, 0.7f, highDb));

    // Anti-fizz LP, but don't smother metal presence.
    const float lpHz = 5600.0f + t * 2400.0f;
    postDriveLp_.setCoefficients(Biquad::design(BiquadType::LowPass, sampleRate_, lpHz, 0.7f, 0.0f));
}

void AmpSimEffect::updateTone()
{
    const float t = std::clamp((drive_.getCurrent() - 3.0f) / 12.0f, 0.0f, 1.0f);
    const float midHz = 720.0f + t * 180.0f; // ~900 Hz scoop zone for modern metal
    const float midQ = 0.7f + t * 0.45f;
    const float trebleHz = 3200.0f + t * 800.0f;
    const float presenceHz = 4000.0f + t * 1200.0f;

    toneLow_.setCoefficients(
        Biquad::design(BiquadType::LowShelf, sampleRate_, 110.0f + t * 30.0f, 0.7f, bassDb_.getCurrent()));
    toneMid_.setCoefficients(
        Biquad::design(BiquadType::Peak, sampleRate_, midHz, midQ, midDb_.getCurrent()));
    toneHigh_.setCoefficients(
        Biquad::design(BiquadType::HighShelf, sampleRate_, trebleHz, 0.7f, trebleDb_.getCurrent()));
    presence_.setCoefficients(
        Biquad::design(BiquadType::HighShelf, sampleRate_, presenceHz, 0.7f, presenceDb_.getCurrent()));
}

void AmpSimEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    const float sagAttack = std::exp(-1.0f / (0.012f * static_cast<float>(sampleRate_)));
    const float sagRelease = std::exp(-1.0f / (0.055f * static_cast<float>(sampleRate_)));

    for (int i = 0; i < numFrames; ++i) {
        float preGain = preGain_.getNext();
        const float drive = drive_.getNext();
        bassDb_.getNext();
        midDb_.getNext();
        trebleDb_.getNext();
        presenceDb_.getNext();
        const float master = master_.getNext();

        if (drive_.isSmoothing()) {
            updateVoicing();
        }
        if (bassDb_.isSmoothing() || midDb_.isSmoothing() || trebleDb_.isSmoothing() ||
            presenceDb_.isSmoothing() || drive_.isSmoothing()) {
            updateTone();
        }

        const float t = std::clamp((drive - 3.0f) / 12.0f, 0.0f, 1.0f);

        float x = buffer[i];
        x = preHp_.process(x);
        x = preLow_.process(x);
        x = preHigh_.process(x);

        // Less tube sag at high gain — 5150 stays aggressive and tight.
        const float level = std::fabs(x);
        if (level > sagEnv_) {
            sagEnv_ = sagAttack * sagEnv_ + (1.0f - sagAttack) * level;
        } else {
            sagEnv_ = sagRelease * sagEnv_ + (1.0f - sagRelease) * level;
        }
        const float sagAmt = 0.55f - t * 0.35f;
        const float sag = 1.0f / (1.0f + sagAmt * sagEnv_ * preGain);
        x *= preGain * sag;

        // Stage 1: main preamp grind.
        x = OverdriveEffect::waveshape(x, drive);
        x *= 0.62f + 0.08f * (1.0f - t);

        // Stage 2: power-amp / extra gain stages — harder as drive rises.
        const float stage2 = std::max(1.0f, 1.0f + (drive - 1.0f) * (0.18f + t * 0.22f));
        x = OverdriveEffect::waveshape(x, stage2);
        x *= 0.78f;

        // Stage 3 (high-gain only): extra saturation for Metal 2000 density.
        if (t > 0.25f) {
            const float stage3 = 1.0f + (drive - 1.0f) * 0.08f * t;
            x = OverdriveEffect::waveshape(x, stage3);
            x *= 0.85f + 0.1f * (1.0f - t);
        }

        x = postDriveLp_.process(x);

        // Soft ceiling — a bit harder/higher for high-gain punch.
        const float kCeil = 0.58f + t * 0.12f;
        const float soft = 0.42f - t * 0.1f;
        if (x > kCeil) {
            x = kCeil + soft * std::tanh((x - kCeil) * (2.2f + t));
        } else if (x < -kCeil) {
            x = -kCeil - soft * std::tanh((-x - kCeil) * (2.2f + t));
        }

        x = toneLow_.process(x);
        x = toneMid_.process(x);
        x = toneHigh_.process(x);
        x = presence_.process(x);
        buffer[i] = x * master;
    }
}

} // namespace dsp
