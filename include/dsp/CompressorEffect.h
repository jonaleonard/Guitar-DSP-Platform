#pragma once

#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

namespace dsp {

// Peak compressor with soft knee omitted (hard knee) for easy verification.
class CompressorEffect final : public Effect {
public:
    enum Parameter : int {
        kThresholdDb = 0,
        kRatio = 1,       // e.g. 4 = 4:1
        kAttackMs = 2,
        kReleaseMs = 3,
        kMakeupDb = 4
    };

    CompressorEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

    float envelopeDb() const { return envelopeDb_; }
    float gainLin() const { return gainLin_; }

private:
    double sampleRate_ = 48000.0;

    SmoothedValue thresholdDb_;
    SmoothedValue ratio_;
    SmoothedValue attackMs_;
    SmoothedValue releaseMs_;
    SmoothedValue makeupDb_;

    float envelopeDb_ = -100.0f;
    float gainLin_ = 1.0f;
};

} // namespace dsp
