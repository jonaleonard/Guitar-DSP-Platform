#pragma once

namespace dsp {

// Framework-agnostic effect base. Real-time rules for process():
// no heap allocation, no locks, no logging, no exceptions.
class Effect {
public:
    virtual ~Effect() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void process(float* buffer, int numFrames) = 0;
    virtual void setParameter(int paramId, float value) = 0;

    void setBypassed(bool bypassed) { bypassed_ = bypassed; }
    bool isBypassed() const { return bypassed_; }

protected:
    bool bypassed_ = false;
};

} // namespace dsp
