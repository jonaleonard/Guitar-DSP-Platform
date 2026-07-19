#include "dsp/CompressorEffect.h"

#include "dsp/DspMath.h"

#include <algorithm>
#include <cmath>

namespace dsp {

CompressorEffect::CompressorEffect()
{
    thresholdDb_.reset(-20.0f);
    ratio_.reset(4.0f);
    attackMs_.reset(10.0f);
    releaseMs_.reset(100.0f);
    makeupDb_.reset(0.0f);
}

void CompressorEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = std::max(1.0, sampleRate);
    thresholdDb_.prepare(sampleRate_);
    ratio_.prepare(sampleRate_);
    attackMs_.prepare(sampleRate_);
    releaseMs_.prepare(sampleRate_);
    makeupDb_.prepare(sampleRate_);
    envelopeDb_ = -100.0f;
    gainLin_ = 1.0f;
}

void CompressorEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kThresholdDb:
        thresholdDb_.setTarget(std::clamp(value, -60.0f, 0.0f));
        break;
    case kRatio:
        ratio_.setTarget(std::max(1.0f, value));
        break;
    case kAttackMs:
        attackMs_.setTarget(std::max(0.1f, value));
        break;
    case kReleaseMs:
        releaseMs_.setTarget(std::max(1.0f, value));
        break;
    case kMakeupDb:
        makeupDb_.setTarget(std::clamp(value, -24.0f, 24.0f));
        break;
    default:
        break;
    }
}

void CompressorEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        const float thresholdDb = thresholdDb_.getNext();
        const float ratio = std::max(1.0f, ratio_.getNext());
        const float attackMs = attackMs_.getNext();
        const float releaseMs = releaseMs_.getNext();
        const float makeupDb = makeupDb_.getNext();

        const float attackCoeff = msToCoeff(attackMs, sampleRate_);
        const float releaseCoeff = msToCoeff(releaseMs, sampleRate_);

        const float x = buffer[i];
        const float levelDb = linToDb(std::fabs(x));

        // Peak envelope in dB.
        if (levelDb > envelopeDb_) {
            envelopeDb_ = attackCoeff * envelopeDb_ + (1.0f - attackCoeff) * levelDb;
        } else {
            envelopeDb_ = releaseCoeff * envelopeDb_ + (1.0f - releaseCoeff) * levelDb;
        }

        float gainDb = 0.0f;
        if (envelopeDb_ > thresholdDb) {
            const float excess = envelopeDb_ - thresholdDb;
            gainDb = -excess * (1.0f - (1.0f / ratio));
        }

        const float targetGainLin = dbToLin(gainDb + makeupDb);

        if (targetGainLin < gainLin_) {
            // Gain reduction → attack
            gainLin_ = attackCoeff * gainLin_ + (1.0f - attackCoeff) * targetGainLin;
        } else {
            // Gain recovery → release
            gainLin_ = releaseCoeff * gainLin_ + (1.0f - releaseCoeff) * targetGainLin;
        }

        buffer[i] = x * gainLin_;
    }
}

} // namespace dsp
