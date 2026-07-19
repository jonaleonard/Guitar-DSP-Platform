#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace audio {

// Lock-free SPSC float ring for audio → GUI visualization (Phase 9).
// Audio thread: write(). GUI thread: snapshotLatest() / writeIndex().
class AudioRingBuffer {
public:
    static constexpr std::size_t kCapacity = 8192; // power of two
    static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be power of two");

    AudioRingBuffer() = default;

    // Audio thread only.
    void write(const float* samples, int numFrames)
    {
        if (samples == nullptr || numFrames <= 0) {
            return;
        }
        std::size_t w = writeIndex_.load(std::memory_order_relaxed);
        for (int i = 0; i < numFrames; ++i) {
            const float s = samples[i];
            data_[w & kMask] = s;
            ++w;
            if (s > peak_.load(std::memory_order_relaxed)) {
                peak_.store(s, std::memory_order_relaxed);
            } else if (-s > peak_.load(std::memory_order_relaxed)) {
                peak_.store(-s, std::memory_order_relaxed);
            }
            if (s >= 0.98f || s <= -0.98f) {
                clipFlag_.store(true, std::memory_order_relaxed);
            }
        }
        writeIndex_.store(w, std::memory_order_release);
    }

    std::size_t writeIndex() const
    {
        return writeIndex_.load(std::memory_order_acquire);
    }

    // Copy the most recent `count` samples into dst (oldest → newest). GUI thread.
    void snapshotLatest(float* dst, std::size_t count) const
    {
        if (dst == nullptr || count == 0) {
            return;
        }
        if (count > kCapacity) {
            count = kCapacity;
        }
        const std::size_t w = writeIndex_.load(std::memory_order_acquire);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t idx = (w - count + i) & kMask;
            dst[i] = data_[idx];
        }
    }

    float exchangePeak()
    {
        return peak_.exchange(0.0f, std::memory_order_relaxed);
    }

    bool exchangeClipFlag()
    {
        return clipFlag_.exchange(false, std::memory_order_relaxed);
    }

private:
    static constexpr std::size_t kMask = kCapacity - 1;

    std::array<float, kCapacity> data_{};
    alignas(64) std::atomic<std::size_t> writeIndex_{0};
    std::atomic<float> peak_{0.0f};
    std::atomic<bool> clipFlag_{false};
};

} // namespace audio
