#pragma once

#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

namespace dsp {

// Peak noise gate: closes when envelope falls below threshold.
class NoiseGateEffect final : public Effect {
public:
    enum Parameter : int {
        kThresholdDb = 0, // e.g. -60 .. 0
        kAttackMs = 1,    // open time
        kReleaseMs = 2,   // close time
        kRangeDb = 3      // attenuation when closed (e.g. -80 ≈ mute)
    };

    NoiseGateEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

    float envelope() const { return envelope_; }
    float gateGain() const { return gateGain_; }

private:
    void updateCoeffs();

    double sampleRate_ = 48000.0;

    SmoothedValue thresholdDb_;
    SmoothedValue attackMs_;
    SmoothedValue releaseMs_;
    SmoothedValue rangeDb_;

    float envelope_ = 0.0f;
    float gateGain_ = 1.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
};

} // namespace dsp
