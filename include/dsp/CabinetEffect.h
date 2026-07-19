#pragma once

#include "dsp/Effect.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/SmoothedValue.h"

#include <array>
#include <string>
#include <vector>

namespace dsp {

class CabinetEffect final : public Effect {
public:
    static constexpr int kMaxBlock = 4096;

    enum Parameter : int {
        kMix = 0,
        kLevel = 1
    };

    CabinetEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

    bool loadImpulseResponse(const float* samples, int numSamples);
    bool loadImpulseResponseFile(const std::string& path);

    bool hasIr() const { return convolver_.isReady(); }
    int irLength() const { return irLength_; }
    int latencySamples() const { return convolver_.latencySamples(); }

private:
    PartitionedConvolver convolver_;
    SmoothedValue mix_;
    SmoothedValue level_;
    int irLength_ = 0;
    std::array<float, kMaxBlock> dryScratch_{};
};

} // namespace dsp
