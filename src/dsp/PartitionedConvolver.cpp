#include "dsp/PartitionedConvolver.h"

#include <algorithm>
#include <cstring>

namespace dsp {

void PartitionedConvolver::reset()
{
    inputFill_ = 0;
    fdlPos_ = 0;
    outWrite_ = 0;
    outRead_ = 0;
    outCount_ = 0;

    if (!previousTail_.empty()) {
        std::fill(previousTail_.begin(), previousTail_.end(), 0.0f);
    }
    for (auto& v : fdlReal_) {
        std::fill(v.begin(), v.end(), 0.0f);
    }
    for (auto& v : fdlImag_) {
        std::fill(v.begin(), v.end(), 0.0f);
    }
    if (!inputBuffer_.empty()) {
        std::fill(inputBuffer_.begin(), inputBuffer_.end(), 0.0f);
    }
    if (!outFifo_.empty()) {
        std::fill(outFifo_.begin(), outFifo_.end(), 0.0f);
    }
}

void PartitionedConvolver::setImpulseResponse(const float* ir,
                                              const int irLength,
                                              const int partitionSize)
{
    ready_ = false;
    if (ir == nullptr || irLength <= 0 || partitionSize < 32 || !Fft::isPowerOfTwo(partitionSize)) {
        return;
    }

    partitionSize_ = partitionSize;
    fftSize_ = partitionSize_ * 2;
    numPartitions_ = (irLength + partitionSize_ - 1) / partitionSize_;
    if (numPartitions_ < 1) {
        numPartitions_ = 1;
    }

    fft_ = std::make_unique<Fft>(fftSize_);

    irReal_.assign(static_cast<std::size_t>(numPartitions_),
                   std::vector<float>(static_cast<std::size_t>(fftSize_), 0.0f));
    irImag_.assign(static_cast<std::size_t>(numPartitions_),
                   std::vector<float>(static_cast<std::size_t>(fftSize_), 0.0f));
    fdlReal_.assign(static_cast<std::size_t>(numPartitions_),
                    std::vector<float>(static_cast<std::size_t>(fftSize_), 0.0f));
    fdlImag_.assign(static_cast<std::size_t>(numPartitions_),
                    std::vector<float>(static_cast<std::size_t>(fftSize_), 0.0f));

    inputBuffer_.assign(static_cast<std::size_t>(partitionSize_), 0.0f);
    previousTail_.assign(static_cast<std::size_t>(partitionSize_), 0.0f);
    fftReal_.assign(static_cast<std::size_t>(fftSize_), 0.0f);
    fftImag_.assign(static_cast<std::size_t>(fftSize_), 0.0f);
    accReal_.assign(static_cast<std::size_t>(fftSize_), 0.0f);
    accImag_.assign(static_cast<std::size_t>(fftSize_), 0.0f);

    // FIFO sized for several partitions of latency headroom.
    const int fifoSize = Fft::nextPowerOfTwo(partitionSize_ * 8);
    outFifo_.assign(static_cast<std::size_t>(fifoSize), 0.0f);
    outWrite_ = outRead_ = outCount_ = 0;

    std::vector<float> time(static_cast<std::size_t>(fftSize_), 0.0f);
    std::vector<float> imag(static_cast<std::size_t>(fftSize_), 0.0f);

    for (int p = 0; p < numPartitions_; ++p) {
        std::fill(time.begin(), time.end(), 0.0f);
        std::fill(imag.begin(), imag.end(), 0.0f);
        const int src = p * partitionSize_;
        const int n = std::min(partitionSize_, std::max(0, irLength - src));
        if (n > 0) {
            std::memcpy(time.data(), ir + src, static_cast<std::size_t>(n) * sizeof(float));
        }
        fft_->forward(time.data(), imag.data());
        irReal_[static_cast<std::size_t>(p)] = time;
        irImag_[static_cast<std::size_t>(p)] = imag;
    }

    inputFill_ = 0;
    fdlPos_ = 0;
    ready_ = true;
}

void PartitionedConvolver::processPartitionIntoFifo()
{
    std::fill(fftReal_.begin(), fftReal_.end(), 0.0f);
    std::fill(fftImag_.begin(), fftImag_.end(), 0.0f);
    std::memcpy(fftReal_.data(),
                inputBuffer_.data(),
                static_cast<std::size_t>(partitionSize_) * sizeof(float));

    fft_->forward(fftReal_.data(), fftImag_.data());

    fdlReal_[static_cast<std::size_t>(fdlPos_)] = fftReal_;
    fdlImag_[static_cast<std::size_t>(fdlPos_)] = fftImag_;

    std::fill(accReal_.begin(), accReal_.end(), 0.0f);
    std::fill(accImag_.begin(), accImag_.end(), 0.0f);

    for (int p = 0; p < numPartitions_; ++p) {
        const int fdlIndex = (fdlPos_ - p + numPartitions_) % numPartitions_;
        const auto& xr = fdlReal_[static_cast<std::size_t>(fdlIndex)];
        const auto& xi = fdlImag_[static_cast<std::size_t>(fdlIndex)];
        const auto& hr = irReal_[static_cast<std::size_t>(p)];
        const auto& hi = irImag_[static_cast<std::size_t>(p)];

        for (int k = 0; k < fftSize_; ++k) {
            const std::size_t kk = static_cast<std::size_t>(k);
            accReal_[kk] += xr[kk] * hr[kk] - xi[kk] * hi[kk];
            accImag_[kk] += xr[kk] * hi[kk] + xi[kk] * hr[kk];
        }
    }

    fft_->inverse(accReal_.data(), accImag_.data());

    const int fifoMask = static_cast<int>(outFifo_.size()) - 1;
    for (int i = 0; i < partitionSize_; ++i) {
        const std::size_t ii = static_cast<std::size_t>(i);
        const float sample = accReal_[ii] + previousTail_[ii];
        previousTail_[ii] = accReal_[static_cast<std::size_t>(i + partitionSize_)];

        outFifo_[static_cast<std::size_t>(outWrite_)] = sample;
        outWrite_ = (outWrite_ + 1) & fifoMask;
        if (outCount_ < static_cast<int>(outFifo_.size())) {
            ++outCount_;
        } else {
            outRead_ = (outRead_ + 1) & fifoMask; // drop oldest on overflow
        }
    }

    fdlPos_ = (fdlPos_ + 1) % numPartitions_;
}

void PartitionedConvolver::process(float* buffer, const int numFrames)
{
    if (!ready_ || buffer == nullptr || numFrames <= 0) {
        return;
    }

    const int fifoMask = static_cast<int>(outFifo_.size()) - 1;

    for (int i = 0; i < numFrames; ++i) {
        inputBuffer_[static_cast<std::size_t>(inputFill_++)] = buffer[i];
        if (inputFill_ == partitionSize_) {
            processPartitionIntoFifo();
            inputFill_ = 0;
        }

        if (outCount_ > 0) {
            buffer[i] = outFifo_[static_cast<std::size_t>(outRead_)];
            outRead_ = (outRead_ + 1) & fifoMask;
            --outCount_;
        } else {
            buffer[i] = 0.0f; // startup latency
        }
    }
}

} // namespace dsp
