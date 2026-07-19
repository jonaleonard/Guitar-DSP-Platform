#include "dsp/ReverbEffect.h"

#include <algorithm>
#include <cmath>

namespace dsp {
namespace {

// Freeverb tuning (scaled from 44100).
constexpr int kCombTunings[] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllpassTunings[] = {556, 441, 341, 225};

int scaleTuning(const int tuning, const double sampleRate)
{
    return std::max(2, static_cast<int>(std::lround(static_cast<double>(tuning) * sampleRate / 44100.0)));
}

} // namespace

void ReverbEffect::Comb::setSize(const int n)
{
    size = std::clamp(n, 2, kMaxCombSize);
    index = 0;
}

void ReverbEffect::Comb::clear()
{
    buffer.fill(0.0f);
    index = 0;
    filterStore = 0.0f;
}

float ReverbEffect::Comb::process(const float input)
{
    float output = buffer[static_cast<std::size_t>(index)];
    filterStore = (output * damp2) + (filterStore * damp1);
    buffer[static_cast<std::size_t>(index)] = input + filterStore * feedback;
    index = (index + 1) % size;
    return output;
}

void ReverbEffect::Allpass::setSize(const int n)
{
    size = std::clamp(n, 2, kMaxAllpassSize);
    index = 0;
}

void ReverbEffect::Allpass::clear()
{
    buffer.fill(0.0f);
    index = 0;
}

float ReverbEffect::Allpass::process(const float input)
{
    const float buffered = buffer[static_cast<std::size_t>(index)];
    const float output = -input + buffered;
    buffer[static_cast<std::size_t>(index)] = input + buffered * feedback;
    index = (index + 1) % size;
    return output;
}

ReverbEffect::ReverbEffect()
{
    roomSize_.reset(0.5f);
    damping_.reset(0.5f);
    mix_.reset(0.0f);
}

void ReverbEffect::prepare(const double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = std::max(1.0, sampleRate);
    roomSize_.prepare(sampleRate_);
    damping_.prepare(sampleRate_);
    mix_.prepare(sampleRate_);

    for (int i = 0; i < kNumCombs; ++i) {
        combs_[static_cast<std::size_t>(i)].setSize(scaleTuning(kCombTunings[i], sampleRate_));
        combs_[static_cast<std::size_t>(i)].clear();
    }
    for (int i = 0; i < kNumAllpasses; ++i) {
        allpasses_[static_cast<std::size_t>(i)].setSize(scaleTuning(kAllpassTunings[i], sampleRate_));
        allpasses_[static_cast<std::size_t>(i)].clear();
        allpasses_[static_cast<std::size_t>(i)].feedback = 0.5f;
    }
    updateCombs();
}

void ReverbEffect::setParameter(const int paramId, const float value)
{
    switch (paramId) {
    case kRoomSize:
        roomSize_.setTarget(std::clamp(value, 0.0f, 1.0f));
        break;
    case kDamping:
        damping_.setTarget(std::clamp(value, 0.0f, 1.0f));
        break;
    case kMix:
        mix_.setTarget(std::clamp(value, 0.0f, 1.0f));
        break;
    default:
        break;
    }
}

void ReverbEffect::updateCombs()
{
    // Freeverb-ish mapping.
    const float room = roomSize_.getCurrent();
    const float damp = damping_.getCurrent();
    const float feedback = 0.28f + room * 0.70f;
    const float damp1 = damp * 0.4f;
    const float damp2 = 1.0f - damp1;

    for (auto& c : combs_) {
        c.feedback = feedback;
        c.damp1 = damp1;
        c.damp2 = damp2;
    }
}

void ReverbEffect::process(float* buffer, const int numFrames)
{
    if (buffer == nullptr || numFrames <= 0) {
        return;
    }

    for (int i = 0; i < numFrames; ++i) {
        roomSize_.getNext();
        damping_.getNext();
        const float mix = mix_.getNext();

        if (roomSize_.isSmoothing() || damping_.isSmoothing()) {
            updateCombs();
        }

        const float dry = buffer[i];
        float wet = 0.0f;
        for (auto& c : combs_) {
            wet += c.process(dry);
        }
        wet *= (1.0f / static_cast<float>(kNumCombs));

        for (auto& a : allpasses_) {
            wet = a.process(wet);
        }

        buffer[i] = dry * (1.0f - mix) + wet * mix;
    }
}

} // namespace dsp
