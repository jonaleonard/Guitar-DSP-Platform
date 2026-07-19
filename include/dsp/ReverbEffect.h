#pragma once

#include "dsp/Effect.h"
#include "dsp/SmoothedValue.h"

#include <array>

namespace dsp {

// Mono Freeverb-style reverb: parallel combs → series allpasses.
class ReverbEffect final : public Effect {
public:
    enum Parameter : int {
        kRoomSize = 0, // 0 .. 1
        kDamping = 1,  // 0 .. 1
        kMix = 2       // 0 .. 1 (wet amount; dry = 1-mix)
    };

    ReverbEffect();

    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int numFrames) override;
    void setParameter(int paramId, float value) override;

private:
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 4;
    static constexpr int kMaxCombSize = 2048;
    static constexpr int kMaxAllpassSize = 512;

    struct Comb {
        std::array<float, kMaxCombSize> buffer{};
        int size = 0;
        int index = 0;
        float filterStore = 0.0f;
        float feedback = 0.5f;
        float damp1 = 0.0f;
        float damp2 = 1.0f;

        void setSize(int n);
        void clear();
        float process(float input);
    };

    struct Allpass {
        std::array<float, kMaxAllpassSize> buffer{};
        int size = 0;
        int index = 0;
        float feedback = 0.5f;

        void setSize(int n);
        void clear();
        float process(float input);
    };

    void updateCombs();

    double sampleRate_ = 48000.0;
    SmoothedValue roomSize_;
    SmoothedValue damping_;
    SmoothedValue mix_;

    std::array<Comb, kNumCombs> combs_{};
    std::array<Allpass, kNumAllpasses> allpasses_{};
};

} // namespace dsp
