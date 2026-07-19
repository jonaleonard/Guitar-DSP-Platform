#include "audio/AudioEngine.h"
#include "dsp/AmpSimEffect.h"
#include "dsp/CabinetEffect.h"
#include "dsp/ChorusEffect.h"
#include "dsp/CompressorEffect.h"
#include "dsp/DelayEffect.h"
#include "dsp/EffectGraph.h"
#include "dsp/EqualizerEffect.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/OverdriveEffect.h"
#include "dsp/ReverbEffect.h"
#include "dsp/SyntheticIr.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {

constexpr int kMaxBlockFrames = 4096;

enum Slot : int {
    kGate = 0,
    kComp = 1,
    kDrive = 2,
    kEq = 3,
    kAmp = 4,
    kCab = 5,
    kChorus = 6,
    kDelay = 7,
    kReverb = 8,
    kGain = 9,
    kNumSlots = 10
};

struct AudioApp {
    dsp::EffectGraph graph;
    std::array<float, kMaxBlockFrames> mono{};
    std::atomic<bool> running{true};
    std::array<bool, kNumSlots> bypassed{};
};

void printHelp()
{
    std::cout
        << "\nStarts CLEAN (wire-through). Editing a parameter enables that effect.\n"
        << "Chain: Gate→Comp→Drive→EQ→Amp→Cab→Chorus→Delay→Reverb→Gain\n\n"
        << "  gt <db>   gate threshold          ct/cr/cm  comp thresh/ratio/makeup\n"
        << "  d/dm      drive / mix              el/em/eh  EQ low/mid/high dB\n"
        << "  ap/ad/am  amp pre/drive/master    ab/ami/at/apr  amp tone\n"
        << "  cx <0..1> cab mix\n"
        << "  chr/chd/chm   chorus rate/depth/mix\n"
        << "  dt/df/dx      delay time_ms/feedback/mix\n"
        << "  rr/rd/rx      reverb room/damping/mix\n"
        << "  g <0..2>  output gain\n"
        << "  bg bc bd be ba bb bch bdl br bn   toggle bypass per effect\n"
        << "  clean     bypass all color FX again (back to wire)\n"
        << "  a / s / h / q\n\n";
}

void setBypass(AudioApp& app, const int slot, const bool bypassed, const char* name)
{
    app.bypassed[static_cast<std::size_t>(slot)] = bypassed;
    app.graph.setBypassed(slot, bypassed);
    if (name != nullptr) {
        std::cout << name << (bypassed ? " bypassed (off)\n" : " enabled\n");
    }
}

void enable(AudioApp& app, const int slot, const char* name)
{
    if (app.bypassed[static_cast<std::size_t>(slot)]) {
        setBypass(app, slot, false, name);
    }
}

} // namespace

int main()
{
    audio::AudioEngineConfig config;
    config.inputDeviceName = "Volt";
    config.sampleRate = 48000;
    config.bufferFrames = 512;
    config.inputChannels = 1;
    config.outputChannels = 2;
    config.minimizeLatency = false;
    config.preferSameDeviceOutput = false;
    config.useDefaultOutputDevice = true;
    config.scheduleRealtime = true;

    auto app = std::make_shared<AudioApp>();
    app->bypassed.fill(true);
    app->bypassed[kGain] = false;

    audio::AudioEngine engine(config);
    app->graph.prepare(static_cast<double>(config.sampleRate),
                       static_cast<int>(config.bufferFrames));

    // Neutral constructors + bypassed = clean wire until you edit.
    auto gate = std::make_unique<dsp::NoiseGateEffect>();
    auto comp = std::make_unique<dsp::CompressorEffect>();
    auto drive = std::make_unique<dsp::OverdriveEffect>();
    auto eq = std::make_unique<dsp::EqualizerEffect>();
    auto amp = std::make_unique<dsp::AmpSimEffect>();
    auto cab = std::make_unique<dsp::CabinetEffect>();
    const auto ir = dsp::makeSyntheticCabIr(2048, config.sampleRate);
    if (!cab->loadImpulseResponse(ir.data(), static_cast<int>(ir.size()))) {
        std::cerr << "Failed to load cabinet IR.\n";
        return 1;
    }
    auto chorus = std::make_unique<dsp::ChorusEffect>();
    auto delay = std::make_unique<dsp::DelayEffect>();
    auto reverb = std::make_unique<dsp::ReverbEffect>();
    auto gain = std::make_unique<dsp::GainEffect>();

    if (!app->graph.insert(std::move(gate), kGate) || !app->graph.insert(std::move(comp), kComp) ||
        !app->graph.insert(std::move(drive), kDrive) || !app->graph.insert(std::move(eq), kEq) ||
        !app->graph.insert(std::move(amp), kAmp) || !app->graph.insert(std::move(cab), kCab) ||
        !app->graph.insert(std::move(chorus), kChorus) ||
        !app->graph.insert(std::move(delay), kDelay) ||
        !app->graph.insert(std::move(reverb), kReverb) ||
        !app->graph.insert(std::move(gain), kGain)) {
        std::cerr << "Failed to build graph.\n";
        return 1;
    }
    app->graph.flushCommands();

    // Apply startup bypass (everything but gain).
    for (int i = 0; i < kNumSlots; ++i) {
        app->graph.setBypassed(i, app->bypassed[static_cast<std::size_t>(i)]);
    }
    app->graph.flushCommands();

    engine.setProcessBlockCallback(
        [app](const float* input,
              float* output,
              const int numFrames,
              const int inputChannels,
              const int outputChannels) {
            if (input == nullptr || output == nullptr || numFrames <= 0) {
                return;
            }
            const int frames = std::min(numFrames, kMaxBlockFrames);
            if (inputChannels == 1) {
                for (int i = 0; i < frames; ++i) {
                    app->mono[static_cast<std::size_t>(i)] = input[i];
                }
            } else {
                for (int i = 0; i < frames; ++i) {
                    app->mono[static_cast<std::size_t>(i)] = input[i * inputChannels];
                }
            }
            app->graph.process(app->mono.data(), frames);
            for (int i = 0; i < frames; ++i) {
                const float s = app->mono[static_cast<std::size_t>(i)];
                if (outputChannels == 1) {
                    output[i] = s;
                } else {
                    output[i * outputChannels] = s;
                    output[i * outputChannels + 1] = s;
                    for (int c = 2; c < outputChannels; ++c) {
                        output[i * outputChannels + c] = 0.0f;
                    }
                }
            }
        });

    if (!engine.start()) {
        return 1;
    }
    app->graph.prepare(static_cast<double>(engine.sampleRate()),
                       static_cast<int>(engine.bufferFrames()));

    std::cout << "Input: " << engine.inputDeviceName() << "\n";
    std::cout << "Output: " << engine.outputDeviceName() << "\n";
    std::cout << "Buffer: " << engine.bufferFrames() << " @ " << engine.sampleRate() << " Hz\n";
    std::cout << "Phase 6: CLEAN start — guitar wire-through until you edit params.\n";
    printHelp();

    std::atomic<float> gainValue{1.0f};
    std::atomic<bool> automationRunning{false};

    float gateThresh = -80.0f, compThresh = 0.0f, compRatio = 1.0f, compMakeup = 0.0f;
    float driveAmt = 1.0f, driveMix = 0.0f;
    float eqLow = 0.0f, eqMid = 0.0f, eqHigh = 0.0f;
    float ampPre = 1.0f, ampDrive = 1.0f, ampMaster = 1.0f;
    float ampBass = 0.0f, ampMid = 0.0f, ampTreble = 0.0f, ampPresence = 0.0f;
    float cabMix = 0.0f;
    float chRate = 0.8f, chDepth = 3.0f, chMix = 0.0f;
    float delTime = 350.0f, delFb = 0.35f, delMix = 0.0f;
    float revRoom = 0.5f, revDamp = 0.5f, revMix = 0.0f;

    std::thread statusThread([app, &engine]() {
        while (app->running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!app->running.load(std::memory_order_relaxed)) {
                break;
            }
            app->graph.reclaimRetiredEffects();
            const auto o = engine.inputOverflowCount();
            const auto u = engine.outputUnderflowCount();
            if (o > 0 || u > 0) {
                std::cout << "[xrun] overflows=" << o << " underflows=" << u << "\n";
            }
        }
    });

    std::string line;
    while (app->running.load(std::memory_order_relaxed) && std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        auto rd = [&](float& dest, float lo, float hi) -> bool {
            float v = dest;
            if (!(iss >> v)) {
                return false;
            }
            dest = std::clamp(v, lo, hi);
            return true;
        };

        if (cmd == "q" || cmd == "quit") {
            break;
        }
        if (cmd == "h" || cmd == "help") {
            printHelp();
            continue;
        }
        if (cmd == "clean") {
            for (int i = 0; i < kGain; ++i) {
                setBypass(*app, i, true, nullptr);
            }
            setBypass(*app, kGain, false, nullptr);
            std::cout << "Clean: all color FX bypassed.\n";
            continue;
        }

        auto toggle = [&](int slot, const char* name) {
            const bool next = !app->bypassed[static_cast<std::size_t>(slot)];
            setBypass(*app, slot, next, name);
        };
        if (cmd == "bg") {
            toggle(kGate, "Gate");
            continue;
        }
        if (cmd == "bc") {
            toggle(kComp, "Comp");
            continue;
        }
        if (cmd == "bd") {
            toggle(kDrive, "Drive");
            continue;
        }
        if (cmd == "be") {
            toggle(kEq, "EQ");
            continue;
        }
        if (cmd == "ba") {
            toggle(kAmp, "Amp");
            continue;
        }
        if (cmd == "bb") {
            toggle(kCab, "Cab");
            continue;
        }
        if (cmd == "bch") {
            toggle(kChorus, "Chorus");
            continue;
        }
        if (cmd == "bdl") {
            toggle(kDelay, "Delay");
            continue;
        }
        if (cmd == "br") {
            toggle(kReverb, "Reverb");
            continue;
        }
        if (cmd == "bn") {
            toggle(kGain, "Gain");
            continue;
        }

        if (cmd == "a") {
            if (automationRunning.exchange(true)) {
                std::cout << "Automation already running.\n";
                continue;
            }
            enable(*app, kGain, "Gain");
            std::thread([app, &automationRunning, &gainValue]() {
                const auto start = std::chrono::steady_clock::now();
                float value = 0.0f;
                while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
                    value = (value < 0.5f) ? 1.0f : 0.0f;
                    gainValue.store(value);
                    app->graph.setParameter(kGain, dsp::GainEffect::kGain, value);
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                gainValue.store(1.0f);
                app->graph.setParameter(kGain, dsp::GainEffect::kGain, 1.0f);
                automationRunning.store(false);
                std::cout << "Automation done.\n";
            }).detach();
            continue;
        }

        if (cmd == "gt" && rd(gateThresh, -80.0f, 0.0f)) {
            enable(*app, kGate, "Gate");
            app->graph.setParameter(kGate, dsp::NoiseGateEffect::kThresholdDb, gateThresh);
            std::cout << "Gate thresh " << gateThresh << " dB\n";
            continue;
        }
        if (cmd == "ct" && rd(compThresh, -60.0f, 0.0f)) {
            enable(*app, kComp, "Comp");
            app->graph.setParameter(kComp, dsp::CompressorEffect::kThresholdDb, compThresh);
            std::cout << "Comp thresh " << compThresh << " dB\n";
            continue;
        }
        if (cmd == "cr" && rd(compRatio, 1.0f, 20.0f)) {
            enable(*app, kComp, "Comp");
            app->graph.setParameter(kComp, dsp::CompressorEffect::kRatio, compRatio);
            std::cout << "Comp ratio " << compRatio << "\n";
            continue;
        }
        if (cmd == "cm" && rd(compMakeup, -24.0f, 24.0f)) {
            enable(*app, kComp, "Comp");
            app->graph.setParameter(kComp, dsp::CompressorEffect::kMakeupDb, compMakeup);
            std::cout << "Comp makeup " << compMakeup << " dB\n";
            continue;
        }
        if (cmd == "d" && rd(driveAmt, 1.0f, 25.0f)) {
            enable(*app, kDrive, "Drive");
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kDrive, driveAmt);
            if (driveMix < 0.05f) {
                driveMix = 0.7f;
                app->graph.setParameter(kDrive, dsp::OverdriveEffect::kMix, driveMix);
            }
            std::cout << "Drive " << driveAmt << " mix " << driveMix << "\n";
            continue;
        }
        if (cmd == "dm" && rd(driveMix, 0.0f, 1.0f)) {
            enable(*app, kDrive, "Drive");
            app->graph.setParameter(kDrive, dsp::OverdriveEffect::kMix, driveMix);
            std::cout << "Drive mix " << driveMix << "\n";
            continue;
        }
        if (cmd == "el" && rd(eqLow, -18.0f, 18.0f)) {
            enable(*app, kEq, "EQ");
            app->graph.setParameter(kEq, dsp::EqualizerEffect::kLowGainDb, eqLow);
            std::cout << "EQ low " << eqLow << " dB\n";
            continue;
        }
        if (cmd == "em" && rd(eqMid, -18.0f, 18.0f)) {
            enable(*app, kEq, "EQ");
            app->graph.setParameter(kEq, dsp::EqualizerEffect::kMidGainDb, eqMid);
            std::cout << "EQ mid " << eqMid << " dB\n";
            continue;
        }
        if (cmd == "eh" && rd(eqHigh, -18.0f, 18.0f)) {
            enable(*app, kEq, "EQ");
            app->graph.setParameter(kEq, dsp::EqualizerEffect::kHighGainDb, eqHigh);
            std::cout << "EQ high " << eqHigh << " dB\n";
            continue;
        }
        if (cmd == "ap" && rd(ampPre, 0.0f, 10.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kPreGain, ampPre);
            std::cout << "Amp pre " << ampPre << "\n";
            continue;
        }
        if (cmd == "ad" && rd(ampDrive, 1.0f, 25.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kDrive, ampDrive);
            std::cout << "Amp drive " << ampDrive << "\n";
            continue;
        }
        if (cmd == "am" && rd(ampMaster, 0.0f, 2.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kMaster, ampMaster);
            std::cout << "Amp master " << ampMaster << "\n";
            continue;
        }
        if (cmd == "ab" && rd(ampBass, -12.0f, 12.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kBassDb, ampBass);
            std::cout << "Amp bass " << ampBass << "\n";
            continue;
        }
        if (cmd == "ami" && rd(ampMid, -12.0f, 12.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kMidDb, ampMid);
            std::cout << "Amp mid " << ampMid << "\n";
            continue;
        }
        if (cmd == "at" && rd(ampTreble, -12.0f, 12.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kTrebleDb, ampTreble);
            std::cout << "Amp treble " << ampTreble << "\n";
            continue;
        }
        if (cmd == "apr" && rd(ampPresence, -12.0f, 12.0f)) {
            enable(*app, kAmp, "Amp");
            app->graph.setParameter(kAmp, dsp::AmpSimEffect::kPresenceDb, ampPresence);
            std::cout << "Amp presence " << ampPresence << "\n";
            continue;
        }
        if (cmd == "cx" && rd(cabMix, 0.0f, 1.0f)) {
            enable(*app, kCab, "Cab");
            app->graph.setParameter(kCab, dsp::CabinetEffect::kMix, cabMix);
            std::cout << "Cab mix " << cabMix << "\n";
            continue;
        }
        if (cmd == "chr" && rd(chRate, 0.05f, 5.0f)) {
            enable(*app, kChorus, "Chorus");
            app->graph.setParameter(kChorus, dsp::ChorusEffect::kRateHz, chRate);
            if (chMix < 0.05f) {
                chMix = 0.5f;
                app->graph.setParameter(kChorus, dsp::ChorusEffect::kMix, chMix);
            }
            std::cout << "Chorus rate " << chRate << " Hz\n";
            continue;
        }
        if (cmd == "chd" && rd(chDepth, 0.5f, 12.0f)) {
            enable(*app, kChorus, "Chorus");
            app->graph.setParameter(kChorus, dsp::ChorusEffect::kDepthMs, chDepth);
            if (chMix < 0.05f) {
                chMix = 0.5f;
                app->graph.setParameter(kChorus, dsp::ChorusEffect::kMix, chMix);
            }
            std::cout << "Chorus depth " << chDepth << " ms\n";
            continue;
        }
        if (cmd == "chm" && rd(chMix, 0.0f, 1.0f)) {
            enable(*app, kChorus, "Chorus");
            app->graph.setParameter(kChorus, dsp::ChorusEffect::kMix, chMix);
            std::cout << "Chorus mix " << chMix << "\n";
            continue;
        }
        if (cmd == "dt" && rd(delTime, 1.0f, 2000.0f)) {
            enable(*app, kDelay, "Delay");
            app->graph.setParameter(kDelay, dsp::DelayEffect::kTimeMs, delTime);
            if (delMix < 0.05f) {
                delMix = 0.35f;
                app->graph.setParameter(kDelay, dsp::DelayEffect::kMix, delMix);
            }
            std::cout << "Delay time " << delTime << " ms\n";
            continue;
        }
        if (cmd == "df" && rd(delFb, 0.0f, 0.95f)) {
            enable(*app, kDelay, "Delay");
            app->graph.setParameter(kDelay, dsp::DelayEffect::kFeedback, delFb);
            if (delMix < 0.05f) {
                delMix = 0.35f;
                app->graph.setParameter(kDelay, dsp::DelayEffect::kMix, delMix);
            }
            std::cout << "Delay feedback " << delFb << "\n";
            continue;
        }
        if (cmd == "dx" && rd(delMix, 0.0f, 1.0f)) {
            enable(*app, kDelay, "Delay");
            app->graph.setParameter(kDelay, dsp::DelayEffect::kMix, delMix);
            std::cout << "Delay mix " << delMix << "\n";
            continue;
        }
        if (cmd == "rr" && rd(revRoom, 0.0f, 1.0f)) {
            enable(*app, kReverb, "Reverb");
            app->graph.setParameter(kReverb, dsp::ReverbEffect::kRoomSize, revRoom);
            if (revMix < 0.05f) {
                revMix = 0.3f;
                app->graph.setParameter(kReverb, dsp::ReverbEffect::kMix, revMix);
            }
            std::cout << "Reverb room " << revRoom << "\n";
            continue;
        }
        if (cmd == "rd" && rd(revDamp, 0.0f, 1.0f)) {
            enable(*app, kReverb, "Reverb");
            app->graph.setParameter(kReverb, dsp::ReverbEffect::kDamping, revDamp);
            if (revMix < 0.05f) {
                revMix = 0.3f;
                app->graph.setParameter(kReverb, dsp::ReverbEffect::kMix, revMix);
            }
            std::cout << "Reverb damping " << revDamp << "\n";
            continue;
        }
        if (cmd == "rx" && rd(revMix, 0.0f, 1.0f)) {
            enable(*app, kReverb, "Reverb");
            app->graph.setParameter(kReverb, dsp::ReverbEffect::kMix, revMix);
            std::cout << "Reverb mix " << revMix << "\n";
            continue;
        }
        if (cmd == "g" || cmd == "gain") {
            float v = gainValue.load();
            if (!(iss >> v)) {
                std::cerr << "Usage: g <0..2>\n";
                continue;
            }
            v = std::clamp(v, 0.0f, 2.0f);
            gainValue.store(v);
            enable(*app, kGain, nullptr);
            app->graph.setParameter(kGain, dsp::GainEffect::kGain, v);
            std::cout << "Gain " << v << "\n";
            continue;
        }
        if (cmd == "s") {
            std::cout << "enabled:";
            const char* names[] = {"gate", "comp", "drive", "eq", "amp", "cab",
                                   "chorus", "delay", "reverb", "gain"};
            for (int i = 0; i < kNumSlots; ++i) {
                if (!app->bypassed[static_cast<std::size_t>(i)]) {
                    std::cout << " " << names[i];
                }
            }
            std::cout << " | chMix=" << chMix << " delMix=" << delMix << " revMix=" << revMix
                      << " cabMix=" << cabMix << " gain=" << gainValue.load() << "\n";
            continue;
        }

        std::cerr << "Unknown command. Type h for help.\n";
    }

    app->running.store(false);
    if (statusThread.joinable()) {
        statusThread.join();
    }
    engine.stop();
    app->graph.reclaimRetiredEffects();
    return 0;
}
