#include "dsp/CabinetEffect.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/SyntheticIr.h"
#include "WavWriter.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr unsigned int kSr = 48000;
constexpr int kBlock = 256;

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b, const int n)
{
    float m = 0.0f;
    for (int i = 0; i < n; ++i) {
        m = std::max(m, std::fabs(a[static_cast<std::size_t>(i)] - b[static_cast<std::size_t>(i)]));
    }
    return m;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string irPath = (argc > 1) ? argv[1] : "synthetic_cab.wav";

    constexpr int kIrLen = 2048;
    const std::vector<float> ir = dsp::makeSyntheticCabIr(kIrLen, kSr);

    try {
        WavWriter::writeFloat32(irPath, ir, 1, kSr);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
    std::cout << "Wrote synthetic IR to " << irPath << "\n";

    dsp::PartitionedConvolver conv;
    conv.setImpulseResponse(ir.data(), kIrLen, 256);

    const int latency = conv.latencySamples();
    const int total = latency + kIrLen + 512;
    std::vector<float> buf(static_cast<std::size_t>(total), 0.0f);
    buf[0] = 1.0f;

    for (int i = 0; i < total; i += kBlock) {
        conv.process(buf.data() + i, std::min(kBlock, total - i));
    }

    // First wet sample is emitted when the first partition fills (index partitionSize-1).
    const int start = latency - 1;
    std::vector<float> got(static_cast<std::size_t>(kIrLen), 0.0f);
    for (int i = 0; i < kIrLen; ++i) {
        got[static_cast<std::size_t>(i)] = buf[static_cast<std::size_t>(start + i)];
    }

    const float err = maxAbsDiff(ir, got, kIrLen);
    if (err > 5.0e-3f) {
        std::cerr << "IR reconstruction error too high: " << err << "\n";
        return 1;
    }
    std::cout << "PASS: impulse→IR reconstruction maxErr=" << err << "\n";

    dsp::CabinetEffect cab;
    cab.prepare(static_cast<double>(kSr), kBlock);
    if (!cab.loadImpulseResponseFile(irPath)) {
        std::cerr << "Failed to load IR file\n";
        return 1;
    }
    cab.setParameter(dsp::CabinetEffect::kMix, 1.0f);
    cab.setParameter(dsp::CabinetEffect::kLevel, 1.0f);

    std::vector<float> tone(static_cast<std::size_t>(kSr / 10), 0.0f);
    for (int i = 0; i < static_cast<int>(tone.size()); ++i) {
        tone[static_cast<std::size_t>(i)] =
            0.4f * std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / kSr));
    }
    for (int i = 0; i < static_cast<int>(tone.size()); i += kBlock) {
        cab.process(tone.data() + i, std::min(kBlock, static_cast<int>(tone.size()) - i));
    }
    double energy = 0.0;
    for (float v : tone) {
        energy += static_cast<double>(v) * v;
    }
    if (energy < 1.0e-6) {
        std::cerr << "Cabinet output energy too low\n";
        return 1;
    }
    std::cout << "PASS: cabinet effect processes tone (energy=" << energy << ")\n";
    std::cout << "Cabinet / convolution tests OK.\n";
    return 0;
}
