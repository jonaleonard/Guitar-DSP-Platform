#pragma once

#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

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

    // Current (possibly mid-ramp) gain heard by the audio thread.
    float gain() const { return gain_.getCurrent(); }
    float gainTarget() const { return gain_.getTarget(); }

    void setRampTimeMs(float rampTimeMs);

private:
    SmoothedValue gain_;
};

} // namespace dsp
