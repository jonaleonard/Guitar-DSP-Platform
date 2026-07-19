#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <algorithm>

namespace audio {

// Lock-free SPSC float ring for audio → GUI visualization (Phase 9).
// Audio thread: write(). GUI thread: snapshotLatest() / peak/clip atomics.
class AudioRingBuffer {
public:
    static constexpr std::size_t kCapacity = 8192; // power of two
    static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be power of two");

    AudioRingBuffer() = default;

    // Audio thread only. Prefer writing the post-limiter output so CLIP matches what you hear.
    void write(const float* samples, int numFrames)
    {
        if (samples == nullptr || numFrames <= 0) {
            return;
        }
        std::size_t w = writeIndex_.load(std::memory_order_relaxed);
        float blockPeak = 0.0f;
        for (int i = 0; i < numFrames; ++i) {
            const float s = samples[i];
            data_[w & kMask] = s;
            ++w;
            const float a = s >= 0.0f ? s : -s;
            if (a > blockPeak) {
                blockPeak = a;
            }
            if (a >= 0.95f) {
                clipFlag_.store(true, std::memory_order_relaxed);
            }
        }
        writeIndex_.store(w, std::memory_order_release);

        // Keep a rolling max the GUI can sample without zeroing every frame.
        float cur = peak_.load(std::memory_order_relaxed);
        while (blockPeak > cur &&
               !peak_.compare_exchange_weak(cur, blockPeak, std::memory_order_relaxed)) {
        }
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

    // GUI: read and decay the peak toward zero (smooth meter, not frame-flash).
    float samplePeak(float decayTowardZero)
    {
        float cur = peak_.load(std::memory_order_relaxed);
        const float next = cur * std::max(0.0f, 1.0f - decayTowardZero);
        peak_.store(next, std::memory_order_relaxed);
        return cur;
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
