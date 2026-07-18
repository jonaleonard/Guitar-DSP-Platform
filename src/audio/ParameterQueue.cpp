#include "audio/ParameterQueue.h"

namespace audio {

ParameterQueue::ParameterQueue() = default;

bool ParameterQueue::push(const int paramId, const float value)
{
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t nextTail = (tail + 1) & kMask;

    if (nextTail == head_.load(std::memory_order_acquire)) {
        return false;
    }

    slots_[tail] = ParameterChange{paramId, value};
    tail_.store(nextTail, std::memory_order_release);
    return true;
}

bool ParameterQueue::pop(int& paramId, float& value)
{
    const std::size_t head = head_.load(std::memory_order_relaxed);

    if (head == tail_.load(std::memory_order_acquire)) {
        return false;
    }

    paramId = slots_[head].paramId;
    value = slots_[head].value;
    head_.store((head + 1) & kMask, std::memory_order_release);
    return true;
}

void ParameterQueue::clear()
{
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

} // namespace audio
