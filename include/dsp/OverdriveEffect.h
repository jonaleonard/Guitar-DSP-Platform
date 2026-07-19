#pragma once

#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

namespace dsp {

// Soft-clip overdrive via tanh waveshaping.
class OverdriveEffect final : public Effect {
public:
    enum Parameter : int {
        kDrive = 0,   // 1 .. 25
        kMix = 1,     // 0 .. 1 (dry/wet)
        kOutput = 2   // post level 0 .. 2
    };

    OverdriveEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

    static float waveshape(float x, float drive);

private:
    SmoothedValue drive_;
    SmoothedValue mix_;
    SmoothedValue output_;
};

} // namespace dsp
