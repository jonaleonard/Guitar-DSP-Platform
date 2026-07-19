#pragma once

#include "dsp/Effect.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace dsp {

enum class GraphCommandType {
    SetParameter,
    SetBypass,
    Insert,
    Remove,
    Swap
};

struct GraphCommand {
    GraphCommandType type = GraphCommandType::SetParameter;
    Effect* effect = nullptr;
    int index = 0;
    int indexB = 0;
    int paramId = 0;
    float value = 0.0f;
    bool bypassed = false;
};

// Single-producer / single-consumer lock-free queue for control → audio thread.
// push() from the control thread; pop() from the real-time audio thread only.
class GraphCommandQueue {
public:
    static constexpr std::size_t kCapacity = 256;

    GraphCommandQueue();

    bool push(const GraphCommand& command);
    bool pop(GraphCommand& command);
    void clear();

private:
    static constexpr std::size_t kMask = kCapacity - 1;

    std::array<GraphCommand, kCapacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace dsp
