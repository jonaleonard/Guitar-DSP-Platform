#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace audio {

struct ParameterChange {
    int paramId;
    float value;
};

// Single-producer / single-consumer lock-free queue for GUI -> audio thread.
// push() from the control thread; pop() from the real-time audio thread only.
class ParameterQueue {
public:
    static constexpr std::size_t kCapacity = 256;

    ParameterQueue();

    bool push(int paramId, float value);
    bool pop(int& paramId, float& value);

    void clear();

private:
    static constexpr std::size_t kMask = kCapacity - 1;

    std::array<ParameterChange, kCapacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace audio
