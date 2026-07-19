#pragma once

#include "dsp/Fft.h"

#include <memory>
#include <vector>

namespace dsp {

// Uniform partitioned convolution (overlap-add) for mono IR.
// Introduces partitionSize samples of latency.
// setImpulseResponse() is NOT real-time safe (allocates).
// process() is real-time safe (no alloc).
class PartitionedConvolver {
public:
    static constexpr int kDefaultPartitionSize = 128;

    PartitionedConvolver() = default;

    void reset();

    void setImpulseResponse(const float* ir, int irLength, int partitionSize = kDefaultPartitionSize);

    void process(float* buffer, int numFrames);

    int partitionSize() const { return partitionSize_; }
    int numPartitions() const { return numPartitions_; }
    int latencySamples() const { return partitionSize_; }
    bool isReady() const { return ready_; }

private:
    void processPartitionIntoFifo();

    bool ready_ = false;
    int partitionSize_ = kDefaultPartitionSize;
    int fftSize_ = 0;
    int numPartitions_ = 0;

    std::unique_ptr<Fft> fft_;

    std::vector<std::vector<float>> irReal_;
    std::vector<std::vector<float>> irImag_;
    std::vector<std::vector<float>> fdlReal_;
    std::vector<std::vector<float>> fdlImag_;
    int fdlPos_ = 0;

    std::vector<float> inputBuffer_;
    int inputFill_ = 0;

    std::vector<float> previousTail_;
    std::vector<float> fftReal_;
    std::vector<float> fftImag_;
    std::vector<float> accReal_;
    std::vector<float> accImag_;

    // Output FIFO (power-of-two ring)
    std::vector<float> outFifo_;
    int outWrite_ = 0;
    int outRead_ = 0;
    int outCount_ = 0;
};

} // namespace dsp
