#pragma once

namespace dsp {

// One-pole (exponential) parameter smoother for real-time use.
// Continuous trajectory — no slope discontinuities when the target jumps mid-ramp
// (linear ramps zipper under rapid automation; exponential does not).
// setTarget() from the audio thread (via effect setParameter); getNext() per sample.
// No heap allocation, no locks.
class SmoothedValue {
public:
    static constexpr float kDefaultRampTimeMs = 50.0f;

    SmoothedValue() = default;

    void prepare(double sampleRate, float rampTimeMs = kDefaultRampTimeMs);

    void setRampTimeMs(float rampTimeMs);

    // Snap current and target immediately (no ramp). Use on prepare / init only.
    void reset(float value);

    // Glide exponentially toward target with the configured time constant.
    void setTarget(float target);

    float getNext();
    float getCurrent() const { return current_; }
    float getTarget() const { return target_; }
    bool isSmoothing() const;

    float rampTimeMs() const { return rampTimeMs_; }
    double sampleRate() const { return sampleRate_; }

    // Approximate samples to settle within ~60 dB (useful for tests).
    int settleSamples() const;

private:
    void updateCoeff();

    double sampleRate_ = 48000.0;
    float rampTimeMs_ = kDefaultRampTimeMs;
    float current_ = 0.0f;
    float target_ = 0.0f;
    float coeff_ = 0.0f; // current = coeff*current + (1-coeff)*target
};

} // namespace dsp
