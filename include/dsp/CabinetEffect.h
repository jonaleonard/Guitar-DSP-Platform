#pragma once

#include "dsp/Effect.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/SmoothedValue.h"

#include <array>
#include <string>
#include <vector>

namespace dsp {

// Hybrid zero-latency cabinet: direct FIR for IR head + partitioned FFT for tail
// (Gardner-style). Overall algorithmic latency = 0 samples.
class CabinetEffect final : public Effect {
public:
    static constexpr int kMaxBlock = 4096;
    static constexpr int kHeadSize = 64; // must match partition size for exact timing
    static constexpr int kMaxHead = 128;

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

    bool hasIr() const { return ready_; }
    int irLength() const { return irLength_; }
    // Hybrid path is zero-latency; kept for API/tests.
    int latencySamples() const { return 0; }

private:
    void clearHeadState();

    PartitionedConvolver tailConvolver_;
    SmoothedValue mix_;
    SmoothedValue level_;

    std::vector<float> headIr_;
    std::array<float, kMaxHead> headDelay_{};
    int headWrite_ = 0;
    int headLen_ = 0;

    int irLength_ = 0;
    bool ready_ = false;

    std::array<float, kMaxBlock> dryScratch_{};
    std::array<float, kMaxBlock> wetScratch_{};
};

} // namespace dsp
