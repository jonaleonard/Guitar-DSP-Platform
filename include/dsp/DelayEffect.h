#pragma once

#include "dsp/DelayLine.h"
#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

namespace dsp {

class DelayEffect final : public Effect {
public:
    enum Parameter : int {
        kTimeMs = 0,    // 1 .. 2000
        kFeedback = 1,  // 0 .. 0.95
        kMix = 2        // 0 .. 1
    };

    DelayEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

private:
    static constexpr float kMaxDelayMs = 2000.0f;

    DelayLine line_;
    SmoothedValue timeMs_;
    SmoothedValue feedback_;
    SmoothedValue mix_;
};

} // namespace dsp
