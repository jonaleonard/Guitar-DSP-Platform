#pragma once

#include "dsp/Biquad.h"
#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

namespace dsp {

// 3-band EQ: low shelf + mid peak + high shelf (RBJ).
class EqualizerEffect final : public Effect {
public:
    enum Parameter : int {
        kLowGainDb = 0,
        kLowFreqHz = 1,
        kMidGainDb = 2,
        kMidFreqHz = 3,
        kMidQ = 4,
        kHighGainDb = 5,
        kHighFreqHz = 6
    };

    EqualizerEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

private:
    void updateCoefficients();

    double sampleRate_ = 48000.0;
    bool coeffsDirty_ = true;

    SmoothedValue lowGainDb_;
    SmoothedValue lowFreqHz_;
    SmoothedValue midGainDb_;
    SmoothedValue midFreqHz_;
    SmoothedValue midQ_;
    SmoothedValue highGainDb_;
    SmoothedValue highFreqHz_;

    Biquad low_;
    Biquad mid_;
    Biquad high_;
};

} // namespace dsp
