#include "dsp/EffectGraph.h"

#include <chrono>
#include <utility>

namespace dsp {

EffectGraph::EffectGraph()
{
    active_.fill(nullptr);
    retired_.fill(nullptr);
}

EffectGraph::~EffectGraph()
{
    flushCommands();
    reclaimRetiredEffects();
}

void EffectGraph::prepare(const double sampleRate, const int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    prepared_ = true;

    const int count = activeCount_.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i) {
        if (active_[static_cast<std::size_t>(i)] != nullptr) {
            active_[static_cast<std::size_t>(i)]->prepare(sampleRate_, maxBlockSize_);
        }
    }
}

bool EffectGraph::insert(std::unique_ptr<Effect> effect, const int index)
{
    if (effect == nullptr || activeCount_.load(std::memory_order_acquire) >= kMaxEffects) {
        return false;
    }

    if (prepared_) {
        effect->prepare(sampleRate_, maxBlockSize_);
    }

    Effect* raw = nullptr;
    int ownedIndex = -1;
    for (int i = 0; i < kMaxEffects; ++i) {
        if (owned_[static_cast<std::size_t>(i)] == nullptr) {
            owned_[static_cast<std::size_t>(i)] = std::move(effect);
            raw = owned_[static_cast<std::size_t>(i)].get();
            ownedIndex = i;
            break;
        }
    }

    if (raw == nullptr) {
        return false;
    }

    GraphCommand command;
    command.type = GraphCommandType::Insert;
    command.effect = raw;
    command.index = index;

    if (!commands_.push(command)) {
        owned_[static_cast<std::size_t>(ownedIndex)].reset();
        return false;
    }

    return true;
}

bool EffectGraph::remove(const int index)
{
    GraphCommand command;
    command.type = GraphCommandType::Remove;
    command.index = index;
    return commands_.push(command);
}

bool EffectGraph::swap(const int indexA, const int indexB)
{
    GraphCommand command;
    command.type = GraphCommandType::Swap;
    command.index = indexA;
    command.indexB = indexB;
    return commands_.push(command);
}

bool EffectGraph::setBypassed(const int index, const bool bypassed)
{
    GraphCommand command;
    command.type = GraphCommandType::SetBypass;
    command.index = index;
    command.bypassed = bypassed;
    return commands_.push(command);
}

bool EffectGraph::setParameter(const int index, const int paramId, const float value)
{
    GraphCommand command;
    command.type = GraphCommandType::SetParameter;
    command.index = index;
    command.paramId = paramId;
    command.value = value;
    return commands_.push(command);
}

void EffectGraph::flushCommands()
{
    applyCommands();
}

void EffectGraph::process(float* buffer, const int numFrames)
{
    applyCommands();

    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    const bool profile = profilingEnabled_.load(std::memory_order_relaxed);
    const auto tTotal0 = profile ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};

    const int count = activeCount_.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i) {
        Effect* effect = active_[static_cast<std::size_t>(i)];
        EffectProfileSlot& slot = profile_[static_cast<std::size_t>(i)];

        if (effect == nullptr || effect->isBypassed()) {
            if (profile) {
                slot.lastNanos.store(0, std::memory_order_relaxed);
                slot.processed.store(false, std::memory_order_relaxed);
            }
            continue;
        }

        if (!profile) {
            effect->process(buffer, numFrames);
            continue;
        }

        const auto t0 = std::chrono::steady_clock::now();
        effect->process(buffer, numFrames);
        const auto nanos = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0)
                .count());

        slot.lastNanos.store(nanos, std::memory_order_relaxed);
        slot.processed.store(true, std::memory_order_relaxed);
        const std::uint64_t prev = slot.avgNanos.load(std::memory_order_relaxed);
        const std::uint64_t ema = prev == 0 ? nanos : (prev * 7 + nanos) / 8;
        slot.avgNanos.store(ema, std::memory_order_relaxed);
    }

    if (profile) {
        const auto totalNanos = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                tTotal0)
                .count());
        totalLastNanos_.store(totalNanos, std::memory_order_relaxed);
        const std::uint64_t prev = totalAvgNanos_.load(std::memory_order_relaxed);
        const std::uint64_t ema = prev == 0 ? totalNanos : (prev * 7 + totalNanos) / 8;
        totalAvgNanos_.store(ema, std::memory_order_relaxed);
    }
}

const EffectProfileSlot& EffectGraph::profileSlot(const int index) const
{
    static const EffectProfileSlot kEmpty{};
    if (index < 0 || index >= kMaxEffects) {
        return kEmpty;
    }
    return profile_[static_cast<std::size_t>(index)];
}

std::uint64_t EffectGraph::totalAvgNanos() const
{
    return totalAvgNanos_.load(std::memory_order_relaxed);
}

std::uint64_t EffectGraph::totalLastNanos() const
{
    return totalLastNanos_.load(std::memory_order_relaxed);
}

void EffectGraph::setProfilingEnabled(const bool enabled)
{
    profilingEnabled_.store(enabled, std::memory_order_relaxed);
}

bool EffectGraph::profilingEnabled() const
{
    return profilingEnabled_.load(std::memory_order_relaxed);
}

void EffectGraph::reclaimRetiredEffects()
{
    const int count = retiredCount_.exchange(0, std::memory_order_acq_rel);
    for (int i = 0; i < count; ++i) {
        Effect* effect = retired_[static_cast<std::size_t>(i)];
        retired_[static_cast<std::size_t>(i)] = nullptr;
        if (effect == nullptr) {
            continue;
        }

        const int ownedIndex = findOwnedIndex(effect);
        if (ownedIndex >= 0) {
            owned_[static_cast<std::size_t>(ownedIndex)].reset();
        }
    }
}

int EffectGraph::size() const
{
    return activeCount_.load(std::memory_order_acquire);
}

Effect* EffectGraph::effectAt(const int index) const
{
    const int count = activeCount_.load(std::memory_order_acquire);
    if (index < 0 || index >= count) {
        return nullptr;
    }
    return active_[static_cast<std::size_t>(index)];
}

void EffectGraph::applyCommands()
{
    GraphCommand command;
    while (commands_.pop(command)) {
        switch (command.type) {
        case GraphCommandType::Insert:
            applyInsert(command.effect, command.index);
            break;
        case GraphCommandType::Remove:
            applyRemove(command.index);
            break;
        case GraphCommandType::Swap:
            applySwap(command.index, command.indexB);
            break;
        case GraphCommandType::SetBypass:
            applySetBypass(command.index, command.bypassed);
            break;
        case GraphCommandType::SetParameter:
            applySetParameter(command.index, command.paramId, command.value);
            break;
        }
    }
}

bool EffectGraph::applyInsert(Effect* effect, int index)
{
    const int count = activeCount_.load(std::memory_order_relaxed);
    if (effect == nullptr || count >= kMaxEffects) {
        return false;
    }

    if (index < 0 || index > count) {
        index = count;
    }

    for (int i = count; i > index; --i) {
        active_[static_cast<std::size_t>(i)] = active_[static_cast<std::size_t>(i - 1)];
    }
    active_[static_cast<std::size_t>(index)] = effect;
    activeCount_.store(count + 1, std::memory_order_release);
    return true;
}

bool EffectGraph::applyRemove(const int index)
{
    const int count = activeCount_.load(std::memory_order_relaxed);
    if (index < 0 || index >= count) {
        return false;
    }

    Effect* removed = active_[static_cast<std::size_t>(index)];
    for (int i = index; i < count - 1; ++i) {
        active_[static_cast<std::size_t>(i)] = active_[static_cast<std::size_t>(i + 1)];
    }
    active_[static_cast<std::size_t>(count - 1)] = nullptr;
    activeCount_.store(count - 1, std::memory_order_release);

    retire(removed);
    return true;
}

bool EffectGraph::applySwap(const int indexA, const int indexB)
{
    const int count = activeCount_.load(std::memory_order_relaxed);
    if (indexA < 0 || indexB < 0 || indexA >= count || indexB >= count) {
        return false;
    }

    std::swap(active_[static_cast<std::size_t>(indexA)], active_[static_cast<std::size_t>(indexB)]);
    return true;
}

bool EffectGraph::applySetBypass(const int index, const bool bypassed)
{
    const int count = activeCount_.load(std::memory_order_relaxed);
    if (index < 0 || index >= count || active_[static_cast<std::size_t>(index)] == nullptr) {
        return false;
    }

    active_[static_cast<std::size_t>(index)]->setBypassed(bypassed);
    return true;
}

bool EffectGraph::applySetParameter(const int index, const int paramId, const float value)
{
    const int count = activeCount_.load(std::memory_order_relaxed);
    if (index < 0 || index >= count || active_[static_cast<std::size_t>(index)] == nullptr) {
        return false;
    }

    active_[static_cast<std::size_t>(index)]->setParameter(paramId, value);
    return true;
}

void EffectGraph::retire(Effect* effect)
{
    if (effect == nullptr) {
        return;
    }

    const int slot = retiredCount_.load(std::memory_order_relaxed);
    if (slot >= kMaxEffects) {
        return;
    }

    retired_[static_cast<std::size_t>(slot)] = effect;
    retiredCount_.store(slot + 1, std::memory_order_release);
}

int EffectGraph::findOwnedIndex(Effect* effect) const
{
    for (int i = 0; i < kMaxEffects; ++i) {
        if (owned_[static_cast<std::size_t>(i)].get() == effect) {
            return i;
        }
    }
    return -1;
}

} // namespace dsp
