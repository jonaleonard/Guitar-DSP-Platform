#include "dsp/AmpSimEffect.h"
#include "dsp/CabinetEffect.h"
#include "dsp/ChorusEffect.h"
#include "dsp/CompressorEffect.h"
#include "dsp/DelayEffect.h"
#include "dsp/DspMath.h"
#include "dsp/EffectGraph.h"
#include "dsp/EqualizerEffect.h"
#include "dsp/Fft.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/OverdriveEffect.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/ReverbEffect.h"
#include "dsp/SyntheticIr.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 128;
constexpr double kPi = 3.14159265358979323846;

void processAll(dsp::EffectGraph& graph, std::vector<float>& buf)
{
    for (int i = 0; i < static_cast<int>(buf.size()); i += kBlock) {
        graph.process(buf.data() + i, std::min(kBlock, static_cast<int>(buf.size()) - i));
    }
}

void settle(dsp::Effect& fx, const float seconds = 0.25f)
{
    std::vector<float> z(static_cast<std::size_t>(seconds * kSr), 0.0f);
    for (int i = 0; i < static_cast<int>(z.size()); i += kBlock) {
        fx.process(z.data() + i, std::min(kBlock, static_cast<int>(z.size()) - i));
    }
}

float rmsTail(const std::vector<float>& buf)
{
    const int n = static_cast<int>(buf.size());
    const int start = n / 2;
    double sum = 0.0;
    for (int i = start; i < n; ++i) {
        const double v = buf[static_cast<std::size_t>(i)];
        sum += v * v;
    }
    return static_cast<float>(std::sqrt(sum / std::max(1, n - start)));
}

float measureSineRms(dsp::Effect& fx, const float freqHz, const float seconds)
{
    const int n = static_cast<int>(seconds * kSr);
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        buf[static_cast<std::size_t>(i)] =
            0.4f * static_cast<float>(std::sin(2.0 * kPi * freqHz * (i / kSr)));
    }
    for (int i = 0; i < n; i += kBlock) {
        fx.process(buf.data() + i, std::min(kBlock, n - i));
    }
    return rmsTail(buf);
}

bool testSineEqBoost()
{
    dsp::EqualizerEffect flat;
    flat.prepare(kSr, kBlock);
    settle(flat);

    dsp::EqualizerEffect boosted;
    boosted.prepare(kSr, kBlock);
    boosted.setParameter(dsp::EqualizerEffect::kMidFreqHz, 1000.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidQ, 1.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidGainDb, 12.0f);
    settle(boosted);

    const float a = measureSineRms(flat, 1000.0f, 0.4f);
    boosted.prepare(kSr, kBlock);
    boosted.setParameter(dsp::EqualizerEffect::kMidFreqHz, 1000.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidQ, 1.0f);
    boosted.setParameter(dsp::EqualizerEffect::kMidGainDb, 12.0f);
    settle(boosted);
    const float b = measureSineRms(boosted, 1000.0f, 0.4f);
    const float db = dsp::linToDb(b / std::max(a, 1.0e-8f));
    if (db < 8.0f || db > 15.0f) {
        std::cerr << "Sine EQ boost failed: " << db << " dB\n";
        return false;
    }
    std::cout << "PASS: sine EQ mid boost ≈ " << db << " dB\n";
    return true;
}

bool testImpulseDelayAndCab()
{
    // Delay wet-only impulse timing
    dsp::DelayEffect delay;
    delay.prepare(kSr, kBlock);
    delay.setParameter(dsp::DelayEffect::kTimeMs, 50.0f);
    delay.setParameter(dsp::DelayEffect::kFeedback, 0.0f);
    delay.setParameter(dsp::DelayEffect::kMix, 1.0f);
    settle(delay, 0.35f);

    const int expect = static_cast<int>(0.05 * kSr);
    std::vector<float> buf(static_cast<std::size_t>(expect + 2000), 0.0f);
    buf[0] = 1.0f;
    for (int i = 0; i < static_cast<int>(buf.size()); i += kBlock) {
        delay.process(buf.data() + i, std::min(kBlock, static_cast<int>(buf.size()) - i));
    }
    int peakAt = 0;
    float peak = 0.0f;
    for (int i = 0; i < static_cast<int>(buf.size()); ++i) {
        const float a = std::fabs(buf[static_cast<std::size_t>(i)]);
        if (a > peak) {
            peak = a;
            peakAt = i;
        }
    }
    if (std::abs(peakAt - expect) > 64 || peak < 0.2f) {
        std::cerr << "Delay IR peak at " << peakAt << " expected ~" << expect << " peak=" << peak
                  << "\n";
        return false;
    }
    std::cout << "PASS: delay impulse peak @ " << peakAt << " samples\n";

    // Cabinet IR energy
    dsp::CabinetEffect cab;
    cab.prepare(kSr, kBlock);
    const auto ir = dsp::makeSyntheticCabIr(1024, static_cast<unsigned>(kSr));
    if (!cab.loadImpulseResponse(ir.data(), static_cast<int>(ir.size()))) {
        std::cerr << "Cab IR load failed\n";
        return false;
    }
    cab.setParameter(dsp::CabinetEffect::kMix, 1.0f);
    cab.setParameter(dsp::CabinetEffect::kLevel, 1.0f);
    settle(cab, 0.1f);

    std::vector<float> tone(static_cast<std::size_t>(kSr / 5), 0.0f);
    for (int i = 0; i < static_cast<int>(tone.size()); ++i) {
        tone[static_cast<std::size_t>(i)] =
            0.4f * static_cast<float>(std::sin(2.0 * kPi * 440.0 * (i / kSr)));
    }
    for (int i = 0; i < static_cast<int>(tone.size()); i += kBlock) {
        cab.process(tone.data() + i, std::min(kBlock, static_cast<int>(tone.size()) - i));
    }
    double energy = 0.0;
    for (float v : tone) {
        energy += static_cast<double>(v) * v;
    }
    if (energy < 1.0e-4) {
        std::cerr << "Cab tone energy too low: " << energy << "\n";
        return false;
    }
    std::cout << "PASS: cabinet IR processes tone (energy=" << energy << ")\n";
    return true;
}

bool testFrequencyResponseSweep()
{
    // Sweep a few probe frequencies through a +9 dB mid EQ @ 1 kHz; peak near 1 kHz.
    dsp::EqualizerEffect eq;
    eq.prepare(kSr, kBlock);
    eq.setParameter(dsp::EqualizerEffect::kMidFreqHz, 1000.0f);
    eq.setParameter(dsp::EqualizerEffect::kMidQ, 1.2f);
    eq.setParameter(dsp::EqualizerEffect::kMidGainDb, 9.0f);
    settle(eq);

    const float freqs[] = {200.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f};
    float bestDb = -999.0f;
    float bestF = 0.0f;
    float ref = 0.0f;

    // Flat reference at 1 kHz
    dsp::EqualizerEffect flat;
    flat.prepare(kSr, kBlock);
    settle(flat);
    ref = measureSineRms(flat, 1000.0f, 0.3f);

    for (float f : freqs) {
        eq.prepare(kSr, kBlock);
        eq.setParameter(dsp::EqualizerEffect::kMidFreqHz, 1000.0f);
        eq.setParameter(dsp::EqualizerEffect::kMidQ, 1.2f);
        eq.setParameter(dsp::EqualizerEffect::kMidGainDb, 9.0f);
        settle(eq);
        const float rms = measureSineRms(eq, f, 0.3f);
        const float db = dsp::linToDb(rms / std::max(ref, 1.0e-8f));
        if (db > bestDb) {
            bestDb = db;
            bestF = f;
        }
    }
    if (bestF < 500.0f || bestF > 2000.0f || bestDb < 4.0f) {
        std::cerr << "Freq response peak misplaced: f=" << bestF << " db=" << bestDb << "\n";
        return false;
    }
    std::cout << "PASS: EQ sweep peaks near " << bestF << " Hz (" << bestDb << " dB)\n";
    return true;
}

bool testClippingAndGate()
{
    // Gate: loud signal open, quiet closed
    dsp::NoiseGateEffect gate;
    gate.prepare(kSr, kBlock);
    gate.setParameter(dsp::NoiseGateEffect::kThresholdDb, -30.0f);
    gate.setParameter(dsp::NoiseGateEffect::kAttackMs, 1.0f);
    gate.setParameter(dsp::NoiseGateEffect::kReleaseMs, 20.0f);
    gate.setParameter(dsp::NoiseGateEffect::kRangeDb, -80.0f);
    settle(gate);

    std::vector<float> loud(static_cast<std::size_t>(kSr * 0.2), 0.0f);
    for (int i = 0; i < static_cast<int>(loud.size()); ++i) {
        loud[static_cast<std::size_t>(i)] = 0.5f; // ~ -6 dBFS
    }
    for (int i = 0; i < static_cast<int>(loud.size()); i += kBlock) {
        gate.process(loud.data() + i, std::min(kBlock, static_cast<int>(loud.size()) - i));
    }
    if (rmsTail(loud) < 0.2f) {
        std::cerr << "Gate closed on loud signal\n";
        return false;
    }

    gate.prepare(kSr, kBlock);
    gate.setParameter(dsp::NoiseGateEffect::kThresholdDb, -30.0f);
    gate.setParameter(dsp::NoiseGateEffect::kAttackMs, 1.0f);
    gate.setParameter(dsp::NoiseGateEffect::kReleaseMs, 20.0f);
    gate.setParameter(dsp::NoiseGateEffect::kRangeDb, -80.0f);
    settle(gate);
    std::vector<float> quiet(static_cast<std::size_t>(kSr * 0.3), 0.0f);
    for (int i = 0; i < static_cast<int>(quiet.size()); ++i) {
        quiet[static_cast<std::size_t>(i)] = 0.005f; // well below -30 dB
    }
    for (int i = 0; i < static_cast<int>(quiet.size()); i += kBlock) {
        gate.process(quiet.data() + i, std::min(kBlock, static_cast<int>(quiet.size()) - i));
    }
    if (rmsTail(quiet) > 0.002f) {
        std::cerr << "Gate failed to attenuate quiet signal: " << rmsTail(quiet) << "\n";
        return false;
    }
    std::cout << "PASS: noise gate open/closed behavior\n";

    // Compressor should reduce crest of a hot sine vs unity
    dsp::CompressorEffect comp;
    comp.prepare(kSr, kBlock);
    comp.setParameter(dsp::CompressorEffect::kThresholdDb, -20.0f);
    comp.setParameter(dsp::CompressorEffect::kRatio, 8.0f);
    comp.setParameter(dsp::CompressorEffect::kAttackMs, 1.0f);
    comp.setParameter(dsp::CompressorEffect::kReleaseMs, 50.0f);
    comp.setParameter(dsp::CompressorEffect::kMakeupDb, 0.0f);
    settle(comp);

    const float hotIn = 0.7f;
    std::vector<float> hot(static_cast<std::size_t>(kSr * 0.4), 0.0f);
    for (int i = 0; i < static_cast<int>(hot.size()); ++i) {
        hot[static_cast<std::size_t>(i)] =
            hotIn * static_cast<float>(std::sin(2.0 * kPi * 440.0 * (i / kSr)));
    }
    for (int i = 0; i < static_cast<int>(hot.size()); i += kBlock) {
        comp.process(hot.data() + i, std::min(kBlock, static_cast<int>(hot.size()) - i));
    }
    const float outRms = rmsTail(hot);
    if (outRms > hotIn * 0.65f) {
        std::cerr << "Compressor did not reduce hot signal enough: " << outRms << "\n";
        return false;
    }
    std::cout << "PASS: compressor reduces hot sine (rms=" << outRms << ")\n";
    return true;
}

bool testGraphLatency()
{
    dsp::EffectGraph graph;
    graph.prepare(kSr, kBlock);
    graph.insert(std::make_unique<dsp::NoiseGateEffect>(), 0);
    graph.insert(std::make_unique<dsp::CompressorEffect>(), 1);
    graph.insert(std::make_unique<dsp::OverdriveEffect>(), 2);
    graph.insert(std::make_unique<dsp::EqualizerEffect>(), 3);
    graph.insert(std::make_unique<dsp::AmpSimEffect>(), 4);
    auto cab = std::make_unique<dsp::CabinetEffect>();
    const auto ir = dsp::makeSyntheticCabIr(1024, static_cast<unsigned>(kSr));
    cab->loadImpulseResponse(ir.data(), static_cast<int>(ir.size()));
    graph.insert(std::move(cab), 5);
    graph.insert(std::make_unique<dsp::ChorusEffect>(), 6);
    graph.insert(std::make_unique<dsp::DelayEffect>(), 7);
    graph.insert(std::make_unique<dsp::ReverbEffect>(), 8);
    graph.insert(std::make_unique<dsp::GainEffect>(), 9);
    graph.flushCommands();

    // Enable only Gain + Cab (hybrid cab is zero-latency; gain path too).
    for (int i = 0; i < 10; ++i) {
        graph.setBypassed(i, i != 5 && i != 9);
    }
    graph.setParameter(9, dsp::GainEffect::kGain, 1.0f);
    graph.setParameter(5, dsp::CabinetEffect::kMix, 1.0f);
    graph.setParameter(5, dsp::CabinetEffect::kLevel, 1.0f);
    graph.flushCommands();

    std::vector<float> warm(static_cast<std::size_t>(kSr * 0.5), 0.0f);
    processAll(graph, warm);

    const int n = 4096;
    std::vector<float> buf(static_cast<std::size_t>(n), 0.0f);
    buf[0] = 1.0f;
    processAll(graph, buf);

    // Hybrid cab: first energy should appear immediately (zero algorithmic latency).
    int first = -1;
    for (int i = 0; i < n; ++i) {
        if (std::fabs(buf[static_cast<std::size_t>(i)]) > 1.0e-4f) {
            first = i;
            break;
        }
    }
    if (first < 0 || first > 2) {
        std::cerr << "Hybrid cab should be ~zero-latency, first=" << first << "\n";
        return false;
    }
    std::cout << "PASS: hybrid cab zero-latency (first energy @ " << first << ")\n";

    // Ensure dry gain-only path has ~0 algorithmic latency
    for (int i = 0; i < 10; ++i) {
        graph.setBypassed(i, i != 9);
    }
    graph.flushCommands();
    std::vector<float> dry(256, 0.0f);
    dry[0] = 0.5f;
    processAll(graph, dry);
    if (std::fabs(dry[0] - 0.5f) > 1.0e-4f) {
        std::cerr << "Gain-only path not zero-latency: " << dry[0] << "\n";
        return false;
    }
    std::cout << "PASS: gain-only path is zero algorithmic latency\n";
    return true;
}

} // namespace

int main()
{
    int fails = 0;
    if (!testSineEqBoost()) {
        ++fails;
    }
    if (!testImpulseDelayAndCab()) {
        ++fails;
    }
    if (!testFrequencyResponseSweep()) {
        ++fails;
    }
    if (!testClippingAndGate()) {
        ++fails;
    }
    if (!testGraphLatency()) {
        ++fails;
    }

    if (fails != 0) {
        std::cerr << "Phase 11: " << fails << " failure(s)\n";
        return 1;
    }
    std::cout << "Phase 11 tests OK.\n";
    return 0;
}
