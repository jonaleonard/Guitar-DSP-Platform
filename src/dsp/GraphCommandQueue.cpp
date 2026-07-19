#include "dsp/GraphCommandQueue.h"

namespace dsp {

GraphCommandQueue::GraphCommandQueue() = default;

bool GraphCommandQueue::push(const GraphCommand& command)
{
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t nextTail = (tail + 1) & kMask;

    if (nextTail == head_.load(std::memory_order_acquire)) {
        return false;
    }

    slots_[tail] = command;
    tail_.store(nextTail, std::memory_order_release);
    return true;
}

bool GraphCommandQueue::pop(GraphCommand& command)
{
    const std::size_t head = head_.load(std::memory_order_relaxed);

    if (head == tail_.load(std::memory_order_acquire)) {
        return false;
    }

    command = slots_[head];
    head_.store((head + 1) & kMask, std::memory_order_release);
    return true;
}

void GraphCommandQueue::clear()
{
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

} // namespace dsp
