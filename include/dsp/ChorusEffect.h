#pragma once

#include "dsp/DelayLine.h"
#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

namespace dsp {

class ChorusEffect final : public Effect {
public:
    enum Parameter : int {
        kRateHz = 0,   // 0.05 .. 5
        kDepthMs = 1,  // 0.5 .. 12
        kMix = 2       // 0 .. 1
    };

    ChorusEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

private:
    static constexpr float kBaseDelayMs = 8.0f;
    static constexpr float kMaxDelayMs = 40.0f;

    DelayLine line_;
    SmoothedValue rateHz_;
    SmoothedValue depthMs_;
    SmoothedValue mix_;
    float lfoPhase_ = 0.0f;
    double sampleRate_ = 48000.0;
};

} // namespace dsp
