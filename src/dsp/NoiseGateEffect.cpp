#include "dsp/NoiseGateEffect.h"

#include "dsp/DspMath.h"

#include <algorithm>
#include <cmath>

namespace dsp {

NoiseGateEffect::NoiseGateEffect()
{
    thresholdDb_.reset(-80.0f); // effectively open
    attackMs_.reset(2.0f);
    releaseMs_.reset(80.0f);
    rangeDb_.reset(-80.0f);
}

void NoiseGateEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = std::max(1.0, sampleRate);
    thresholdDb_.prepare(sampleRate_);
    attackMs_.prepare(sampleRate_);
    releaseMs_.prepare(sampleRate_);
    rangeDb_.prepare(sampleRate_);
    envelope_ = 0.0f;
    gateGain_ = 1.0f;
    updateCoeffs();
}

void NoiseGateEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kThresholdDb:
        thresholdDb_.setTarget(std::clamp(value, -80.0f, 0.0f));
        break;
    case kAttackMs:
        attackMs_.setTarget(std::max(0.1f, value));
        break;
    case kReleaseMs:
        releaseMs_.setTarget(std::max(1.0f, value));
        break;
    case kRangeDb:
        rangeDb_.setTarget(std::clamp(value, -100.0f, 0.0f));
        break;
    default:
        break;
    }
}

void NoiseGateEffect::updateCoeffs()
{
    attackCoeff_ = msToCoeff(attackMs_.getCurrent(), sampleRate_);
    releaseCoeff_ = msToCoeff(releaseMs_.getCurrent(), sampleRate_);
}

void NoiseGateEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        const float thresholdDb = thresholdDb_.getNext();
        const float attackMs = attackMs_.getNext();
        const float releaseMs = releaseMs_.getNext();
        const float rangeDb = rangeDb_.getNext();

        // Recalc coeffs cheaply when params move (still O(1) per sample).
        attackCoeff_ = msToCoeff(attackMs, sampleRate_);
        releaseCoeff_ = msToCoeff(releaseMs, sampleRate_);

        const float x = buffer[i];
        const float absx = std::fabs(x);

        // Peak envelope follower.
        if (absx > envelope_) {
            envelope_ = attackCoeff_ * envelope_ + (1.0f - attackCoeff_) * absx;
        } else {
            envelope_ = releaseCoeff_ * envelope_ + (1.0f - releaseCoeff_) * absx;
        }

        const float thresholdLin = dbToLin(thresholdDb);
        const float floorLin = dbToLin(rangeDb);
        const float targetGain = (envelope_ >= thresholdLin) ? 1.0f : floorLin;

        if (targetGain > gateGain_) {
            gateGain_ = attackCoeff_ * gateGain_ + (1.0f - attackCoeff_) * targetGain;
        } else {
            gateGain_ = releaseCoeff_ * gateGain_ + (1.0f - releaseCoeff_) * targetGain;
        }

        buffer[i] = x * gateGain_;
    }
}

} // namespace dsp
