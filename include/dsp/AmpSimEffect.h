#pragma once

#include "dsp/Biquad.h"
#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

namespace dsp {

// Simple amp sim: pre-EQ → tanh stages → anti-squeal LP → tone stack → master.
class AmpSimEffect final : public Effect {
public:
    enum Parameter : int {
        kPreGain = 0,     // 0 .. 10
        kDrive = 1,       // 1 .. 25
        kBassDb = 2,      // tone stack
        kMidDb = 3,
        kTrebleDb = 4,
        kPresenceDb = 5,  // high shelf after tone
        kMaster = 6       // 0 .. 2
    };

    AmpSimEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

private:
    void updateTone();

    double sampleRate_ = 48000.0;

    SmoothedValue preGain_;
    SmoothedValue drive_;
    SmoothedValue bassDb_;
    SmoothedValue midDb_;
    SmoothedValue trebleDb_;
    SmoothedValue presenceDb_;
    SmoothedValue master_;

    Biquad preLow_;
    Biquad preHigh_;
    Biquad postDriveLp_;
    Biquad toneLow_;
    Biquad toneMid_;
    Biquad toneHigh_;
    Biquad presence_;
};

} // namespace dsp
