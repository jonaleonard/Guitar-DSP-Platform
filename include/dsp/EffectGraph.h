#pragma once

#include "dsp/Effect.h"
#include "dsp/GraphCommandQueue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>

namespace dsp {

// Ordered effect chain. Graph topology mutations go through GraphCommandQueue and are
// applied at the start of process() on the audio thread (no vector realloc / no delete there).
class EffectGraph {
public:
    static constexpr int kMaxEffects = 32;

    EffectGraph();
    ~EffectGraph();

    EffectGraph(const EffectGraph&) = delete;
    EffectGraph& operator=(const EffectGraph&) = delete;

    void prepare(double sampleRate, int maxBlockSize);

    // Control thread: takes ownership. Effect should already be prepared (or call prepare() first).
    bool insert(std::unique_ptr<Effect> effect, int index);
    bool remove(int index);
    bool swap(int indexA, int indexB);
    bool setBypassed(int index, bool bypassed);
    bool setParameter(int index, int paramId, float value);

    // Apply any pending commands without processing audio (call only when the audio thread
    // is not concurrently in process()).
    void flushCommands();

    // Audio thread: drain commands, then process active (non-bypassed) effects in order.
    void process(float* buffer, int numFrames);

    // Control thread: destroy effects that the audio thread has retired after Remove.
    void reclaimRetiredEffects();

    int size() const;
    Effect* effectAt(int index) const;

private:
    void applyCommands();
    bool applyInsert(Effect* effect, int index);
    bool applyRemove(int index);
    bool applySwap(int indexA, int indexB);
    bool applySetBypass(int index, bool bypassed);
    bool applySetParameter(int index, int paramId, float value);
    void retire(Effect* effect);
    int findOwnedIndex(Effect* effect) const;

    GraphCommandQueue commands_;
    std::array<Effect*, kMaxEffects> active_{};
    std::atomic<int> activeCount_{0};

    std::array<std::unique_ptr<Effect>, kMaxEffects> owned_{};

    std::array<Effect*, kMaxEffects> retired_{};
    std::atomic<int> retiredCount_{0};

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 512;
    bool prepared_ = false;
};

} // namespace dsp
