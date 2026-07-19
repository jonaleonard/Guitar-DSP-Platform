#pragma once

namespace dsp {

// Linear parameter ramp for real-time use.
// setTarget() from the audio thread (via effect setParameter); getNext() per sample.
// No heap allocation, no locks.
class SmoothedValue {
public:
    static constexpr float kDefaultRampTimeMs = 20.0f;

    SmoothedValue() = default;

    void prepare(double sampleRate, float rampTimeMs = kDefaultRampTimeMs);

    void setRampTimeMs(float rampTimeMs);

    // Snap current and target immediately (no ramp). Use on prepare / init only.
    void reset(float value);

    // Begin a linear ramp from the current value toward target over rampTimeMs.
    void setTarget(float target);

    float getNext();
    float getCurrent() const { return current_; }
    float getTarget() const { return target_; }
    bool isSmoothing() const { return remainingSamples_ > 0; }

    float rampTimeMs() const { return rampTimeMs_; }
    double sampleRate() const { return sampleRate_; }

    // Samples needed for a full ramp at the current settings.
    int rampSamples() const;

private:
    void recomputeStep();

    double sampleRate_ = 48000.0;
    float rampTimeMs_ = kDefaultRampTimeMs;
    float current_ = 0.0f;
    float target_ = 0.0f;
    float step_ = 0.0f;
    int remainingSamples_ = 0;
};

} // namespace dsp
