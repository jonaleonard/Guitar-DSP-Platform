#pragma once

#include "dsp/Effect.h"

namespace dsp {

class GainEffect final : public Effect {
public:
    enum Parameter : int {
        kGain = 0
    };

    GainEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

    float gain() const { return gain_; }

private:
    float gain_ = 1.0f;
};

} // namespace dsp
