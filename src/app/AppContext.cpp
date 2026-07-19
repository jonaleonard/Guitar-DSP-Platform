#include "app/AppContext.h"

#include "dsp/AmpSimEffect.h"
#include "dsp/CabinetEffect.h"
#include "dsp/ChorusEffect.h"
#include "dsp/CompressorEffect.h"
#include "dsp/DelayEffect.h"
#include "dsp/EqualizerEffect.h"
#include "dsp/GainEffect.h"
#include "dsp/NoiseGateEffect.h"
#include "dsp/OverdriveEffect.h"
#include "dsp/PeakLimiter.h"
#include "dsp/ReverbEffect.h"
#include "dsp/SyntheticIr.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>

namespace app {
namespace {

float getParam(const preset::EffectState& st, int id, float fallback)
{
    const auto it = st.parameters.find(id);
    return it != st.parameters.end() ? it->second : fallback;
}

} // namespace

bool setupAudioGraph(AppContext& ctx)
{
    audio::AudioEngineConfig config;
    config.inputDeviceName = "Volt";
    config.sampleRate = 48000;
    config.bufferFrames = 64;
    config.inputChannels = 1;
    config.outputChannels = 2;
    config.minimizeLatency = true;
    config.preferSameDeviceOutput = false;
    config.useDefaultOutputDevice = true;
    config.scheduleRealtime = true;

    ctx.engine = std::make_unique<audio::AudioEngine>(config);
    ctx.sampleRate = static_cast<double>(config.sampleRate);
    ctx.graph.prepare(ctx.sampleRate, static_cast<int>(config.bufferFrames));
    ctx.bank.loadFactoryPresets();

    namespace fs = std::filesystem;
    const fs::path candidates[] = {
        fs::current_path() / "presets",
        fs::current_path() / ".." / "presets",
        fs::current_path() / ".." / ".." / "presets",
    };
    ctx.presetsDirectory = (fs::current_path() / "presets").string();
    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            ctx.presetsDirectory = fs::weakly_canonical(p, ec).string();
            break;
        }
    }
    std::error_code ec;
    fs::create_directories(ctx.presetsDirectory, ec);
    for (int i = 0; i < ctx.bank.size(); ++i) {
        const auto* p = ctx.bank.at(i);
        ctx.bank.saveToFile(*p, ctx.presetsDirectory + "/" + p->name + ".json");
    }

    auto gate = std::make_unique<dsp::NoiseGateEffect>();
    auto comp = std::make_unique<dsp::CompressorEffect>();
    auto drive = std::make_unique<dsp::OverdriveEffect>();
    auto eq = std::make_unique<dsp::EqualizerEffect>();
    auto amp = std::make_unique<dsp::AmpSimEffect>();
    auto cab = std::make_unique<dsp::CabinetEffect>();
    const auto ir = dsp::makeSyntheticCabIr(1024, config.sampleRate);
    if (!cab->loadImpulseResponse(ir.data(), static_cast<int>(ir.size()))) {
        std::cerr << "Failed to load cabinet IR.\n";
        return false;
    }
    auto chorus = std::make_unique<dsp::ChorusEffect>();
    auto delay = std::make_unique<dsp::DelayEffect>();
    auto reverb = std::make_unique<dsp::ReverbEffect>();
    auto gain = std::make_unique<dsp::GainEffect>();

    if (!ctx.graph.insert(std::move(gate), kGate) || !ctx.graph.insert(std::move(comp), kComp) ||
        !ctx.graph.insert(std::move(drive), kDrive) || !ctx.graph.insert(std::move(eq), kEq) ||
        !ctx.graph.insert(std::move(amp), kAmp) || !ctx.graph.insert(std::move(cab), kCab) ||
        !ctx.graph.insert(std::move(chorus), kChorus) || !ctx.graph.insert(std::move(delay), kDelay) ||
        !ctx.graph.insert(std::move(reverb), kReverb) || !ctx.graph.insert(std::move(gain), kGain)) {
        std::cerr << "Failed to build effect graph.\n";
        return false;
    }
    ctx.graph.flushCommands();

    syncUiFromPreset(ctx, *ctx.bank.at(0));
    ctx.bank.apply(*ctx.bank.at(0), ctx.graph, ctx.ui.bypassed);
    ctx.graph.flushCommands();
    ctx.currentPreset.store(0);
    // ~-1 dBFS ceiling — loud amp/pedal feel with DAC protection.
    ctx.outputLimiter.prepare(ctx.sampleRate, 0.89f, 80.0f);

    AppContext* raw = &ctx;
    ctx.engine->setProcessBlockCallback(
        [raw](const float* input,
              float* output,
              const int numFrames,
              const int inputChannels,
              const int outputChannels) {
            if (input == nullptr || output == nullptr || numFrames <= 0) {
                return;
            }

            const auto t0 = std::chrono::steady_clock::now();
            const int frames = std::min(numFrames, kMaxBlockFrames);

            if (inputChannels == 1) {
                for (int i = 0; i < frames; ++i) {
                    raw->mono[static_cast<std::size_t>(i)] = input[i] * kInputTrim;
                }
            } else {
                for (int i = 0; i < frames; ++i) {
                    raw->mono[static_cast<std::size_t>(i)] = input[i * inputChannels] * kInputTrim;
                }
            }

            raw->graph.process(raw->mono.data(), frames);

            for (int i = 0; i < frames; ++i) {
                const float g = raw->mute.nextGain();
                raw->mono[static_cast<std::size_t>(i)] *= g;
            }

            // Limit first, then meter — scopes/CLIP reflect what you actually hear / send to DAC.
            for (int i = 0; i < frames; ++i) {
                raw->mono[static_cast<std::size_t>(i)] =
                    raw->outputLimiter.process(raw->mono[static_cast<std::size_t>(i)]);
            }
            raw->vizRing.write(raw->mono.data(), frames);

            for (int i = 0; i < frames; ++i) {
                const float s = raw->mono[static_cast<std::size_t>(i)];
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

            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - t0)
                                .count();
            raw->metrics.processMicros.store(static_cast<std::uint64_t>(us),
                                             std::memory_order_relaxed);
            raw->metrics.callbackCount.fetch_add(1, std::memory_order_relaxed);
        });

    if (!ctx.engine->start()) {
        return false;
    }

    ctx.sampleRate = static_cast<double>(ctx.engine->sampleRate());
    ctx.graph.prepare(ctx.sampleRate, static_cast<int>(ctx.engine->bufferFrames()));
    ctx.outputLimiter.prepare(ctx.sampleRate, 0.89f, 80.0f);
    syncUiFromPreset(ctx, *ctx.bank.at(0));
    ctx.bank.apply(*ctx.bank.at(0), ctx.graph, ctx.ui.bypassed);

    std::cout << "Input: " << ctx.engine->inputDeviceName() << "\n";
    std::cout << "Output: " << ctx.engine->outputDeviceName() << "\n";
    std::cout << "Buffer: " << ctx.engine->bufferFrames() << " @ " << ctx.engine->sampleRate()
              << " Hz (minimizeLatency on)\n";
    std::cout << "GUI ready — Phases 8–11.\n";
    return true;
}

void syncUiFromPreset(AppContext& ctx, const preset::Preset& preset)
{
    if (static_cast<int>(preset.effects.size()) != kNumSlots) {
        return;
    }

    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;

    for (int i = 0; i < kNumSlots; ++i) {
        ctx.ui.bypassed[static_cast<std::size_t>(i)] = preset.effects[static_cast<std::size_t>(i)].bypassed;
    }

    const auto& g0 = preset.effects[kGate];
    ctx.ui.gateThresh = getParam(g0, NG::kThresholdDb, ctx.ui.gateThresh);
    ctx.ui.gateAttack = getParam(g0, NG::kAttackMs, ctx.ui.gateAttack);
    ctx.ui.gateRelease = getParam(g0, NG::kReleaseMs, ctx.ui.gateRelease);

    const auto& c0 = preset.effects[kComp];
    ctx.ui.compThresh = getParam(c0, C::kThresholdDb, ctx.ui.compThresh);
    ctx.ui.compRatio = getParam(c0, C::kRatio, ctx.ui.compRatio);
    ctx.ui.compAttack = getParam(c0, C::kAttackMs, ctx.ui.compAttack);
    ctx.ui.compRelease = getParam(c0, C::kReleaseMs, ctx.ui.compRelease);
    ctx.ui.compMakeup = getParam(c0, C::kMakeupDb, ctx.ui.compMakeup);

    const auto& d0 = preset.effects[kDrive];
    ctx.ui.drive = getParam(d0, D::kDrive, ctx.ui.drive);
    ctx.ui.driveMix = getParam(d0, D::kMix, ctx.ui.driveMix);
    ctx.ui.driveOut = getParam(d0, D::kOutput, ctx.ui.driveOut);

    const auto& e0 = preset.effects[kEq];
    ctx.ui.eqLow = getParam(e0, E::kLowGainDb, ctx.ui.eqLow);
    ctx.ui.eqMid = getParam(e0, E::kMidGainDb, ctx.ui.eqMid);
    ctx.ui.eqHigh = getParam(e0, E::kHighGainDb, ctx.ui.eqHigh);
    ctx.ui.eqMidFreq = getParam(e0, E::kMidFreqHz, ctx.ui.eqMidFreq);
    ctx.ui.eqMidQ = getParam(e0, E::kMidQ, ctx.ui.eqMidQ);

    const auto& a0 = preset.effects[kAmp];
    ctx.ui.ampPre = getParam(a0, A::kPreGain, ctx.ui.ampPre);
    ctx.ui.ampDrive = getParam(a0, A::kDrive, ctx.ui.ampDrive);
    ctx.ui.ampBass = getParam(a0, A::kBassDb, ctx.ui.ampBass);
    ctx.ui.ampMid = getParam(a0, A::kMidDb, ctx.ui.ampMid);
    ctx.ui.ampTreble = getParam(a0, A::kTrebleDb, ctx.ui.ampTreble);
    ctx.ui.ampPresence = getParam(a0, A::kPresenceDb, ctx.ui.ampPresence);
    ctx.ui.ampMaster = getParam(a0, A::kMaster, ctx.ui.ampMaster);

    const auto& cab0 = preset.effects[kCab];
    ctx.ui.cabMix = getParam(cab0, Cab::kMix, ctx.ui.cabMix);
    ctx.ui.cabLevel = getParam(cab0, Cab::kLevel, ctx.ui.cabLevel);

    const auto& ch0 = preset.effects[kChorus];
    ctx.ui.chorusRate = getParam(ch0, Ch::kRateHz, ctx.ui.chorusRate);
    ctx.ui.chorusDepth = getParam(ch0, Ch::kDepthMs, ctx.ui.chorusDepth);
    ctx.ui.chorusMix = getParam(ch0, Ch::kMix, ctx.ui.chorusMix);

    const auto& dl0 = preset.effects[kDelay];
    ctx.ui.delayTime = getParam(dl0, Dl::kTimeMs, ctx.ui.delayTime);
    ctx.ui.delayFeedback = getParam(dl0, Dl::kFeedback, ctx.ui.delayFeedback);
    ctx.ui.delayMix = getParam(dl0, Dl::kMix, ctx.ui.delayMix);

    const auto& r0 = preset.effects[kReverb];
    ctx.ui.reverbRoom = getParam(r0, R::kRoomSize, ctx.ui.reverbRoom);
    ctx.ui.reverbDamp = getParam(r0, R::kDamping, ctx.ui.reverbDamp);
    ctx.ui.reverbMix = getParam(r0, R::kMix, ctx.ui.reverbMix);

    ctx.ui.gain = getParam(preset.effects[kGain], G::kGain, ctx.ui.gain);
}

void setSlotBypassed(AppContext& ctx, const int slot, const bool bypassed)
{
    ctx.ui.bypassed[static_cast<std::size_t>(slot)] = bypassed;
    ctx.graph.setBypassed(slot, bypassed);
}

void enableSlot(AppContext& ctx, const int slot)
{
    if (ctx.ui.bypassed[static_cast<std::size_t>(slot)]) {
        setSlotBypassed(ctx, slot, false);
    }
}

void pushSlotParams(AppContext& ctx, const int slot)
{
    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;

    switch (slot) {
    case kGate:
        ctx.graph.setParameter(slot, NG::kThresholdDb, ctx.ui.gateThresh);
        ctx.graph.setParameter(slot, NG::kAttackMs, ctx.ui.gateAttack);
        ctx.graph.setParameter(slot, NG::kReleaseMs, ctx.ui.gateRelease);
        break;
    case kComp:
        ctx.graph.setParameter(slot, C::kThresholdDb, ctx.ui.compThresh);
        ctx.graph.setParameter(slot, C::kRatio, ctx.ui.compRatio);
        ctx.graph.setParameter(slot, C::kAttackMs, ctx.ui.compAttack);
        ctx.graph.setParameter(slot, C::kReleaseMs, ctx.ui.compRelease);
        ctx.graph.setParameter(slot, C::kMakeupDb, ctx.ui.compMakeup);
        break;
    case kDrive:
        ctx.graph.setParameter(slot, D::kDrive, ctx.ui.drive);
        ctx.graph.setParameter(slot, D::kMix, ctx.ui.driveMix);
        ctx.graph.setParameter(slot, D::kOutput, ctx.ui.driveOut);
        break;
    case kEq:
        ctx.graph.setParameter(slot, E::kLowGainDb, ctx.ui.eqLow);
        ctx.graph.setParameter(slot, E::kMidGainDb, ctx.ui.eqMid);
        ctx.graph.setParameter(slot, E::kHighGainDb, ctx.ui.eqHigh);
        ctx.graph.setParameter(slot, E::kMidFreqHz, ctx.ui.eqMidFreq);
        ctx.graph.setParameter(slot, E::kMidQ, ctx.ui.eqMidQ);
        break;
    case kAmp:
        ctx.graph.setParameter(slot, A::kPreGain, ctx.ui.ampPre);
        ctx.graph.setParameter(slot, A::kDrive, ctx.ui.ampDrive);
        ctx.graph.setParameter(slot, A::kBassDb, ctx.ui.ampBass);
        ctx.graph.setParameter(slot, A::kMidDb, ctx.ui.ampMid);
        ctx.graph.setParameter(slot, A::kTrebleDb, ctx.ui.ampTreble);
        ctx.graph.setParameter(slot, A::kPresenceDb, ctx.ui.ampPresence);
        ctx.graph.setParameter(slot, A::kMaster, ctx.ui.ampMaster);
        break;
    case kCab:
        ctx.graph.setParameter(slot, Cab::kMix, ctx.ui.cabMix);
        ctx.graph.setParameter(slot, Cab::kLevel, ctx.ui.cabLevel);
        break;
    case kChorus:
        ctx.graph.setParameter(slot, Ch::kRateHz, ctx.ui.chorusRate);
        ctx.graph.setParameter(slot, Ch::kDepthMs, ctx.ui.chorusDepth);
        ctx.graph.setParameter(slot, Ch::kMix, ctx.ui.chorusMix);
        break;
    case kDelay:
        ctx.graph.setParameter(slot, Dl::kTimeMs, ctx.ui.delayTime);
        ctx.graph.setParameter(slot, Dl::kFeedback, ctx.ui.delayFeedback);
        ctx.graph.setParameter(slot, Dl::kMix, ctx.ui.delayMix);
        break;
    case kReverb:
        ctx.graph.setParameter(slot, R::kRoomSize, ctx.ui.reverbRoom);
        ctx.graph.setParameter(slot, R::kDamping, ctx.ui.reverbDamp);
        ctx.graph.setParameter(slot, R::kMix, ctx.ui.reverbMix);
        break;
    case kGain:
        ctx.graph.setParameter(slot, G::kGain, ctx.ui.gain);
        break;
    default:
        break;
    }
}

preset::Preset captureCurrentPreset(const AppContext& ctx, const std::string& name)
{
    using NG = dsp::NoiseGateEffect;
    using C = dsp::CompressorEffect;
    using D = dsp::OverdriveEffect;
    using E = dsp::EqualizerEffect;
    using A = dsp::AmpSimEffect;
    using Cab = dsp::CabinetEffect;
    using Ch = dsp::ChorusEffect;
    using Dl = dsp::DelayEffect;
    using R = dsp::ReverbEffect;
    using G = dsp::GainEffect;
    using EffectType = preset::EffectType;

    const auto& u = ctx.ui;
    auto make = [&](EffectType type, int slot, std::initializer_list<std::pair<int, float>> params) {
        preset::EffectState s;
        s.type = type;
        s.bypassed = u.bypassed[static_cast<std::size_t>(slot)];
        for (const auto& p : params) {
            s.parameters[p.first] = p.second;
        }
        return s;
    };

    preset::Preset p;
    p.name = name;
    p.effects = {
        make(EffectType::NoiseGate, kGate,
             {{NG::kThresholdDb, u.gateThresh}, {NG::kAttackMs, u.gateAttack}, {NG::kReleaseMs, u.gateRelease}, {NG::kRangeDb, -80.f}}),
        make(EffectType::Compressor, kComp,
             {{C::kThresholdDb, u.compThresh}, {C::kRatio, u.compRatio}, {C::kAttackMs, u.compAttack},
              {C::kReleaseMs, u.compRelease}, {C::kMakeupDb, u.compMakeup}}),
        make(EffectType::Overdrive, kDrive,
             {{D::kDrive, u.drive}, {D::kMix, u.driveMix}, {D::kOutput, u.driveOut}}),
        make(EffectType::Equalizer, kEq,
             {{E::kLowGainDb, u.eqLow}, {E::kMidGainDb, u.eqMid}, {E::kHighGainDb, u.eqHigh},
              {E::kMidFreqHz, u.eqMidFreq}, {E::kMidQ, u.eqMidQ}, {E::kLowFreqHz, 120.f}, {E::kHighFreqHz, 4000.f}}),
        make(EffectType::AmpSim, kAmp,
             {{A::kPreGain, u.ampPre}, {A::kDrive, u.ampDrive}, {A::kBassDb, u.ampBass}, {A::kMidDb, u.ampMid},
              {A::kTrebleDb, u.ampTreble}, {A::kPresenceDb, u.ampPresence}, {A::kMaster, u.ampMaster}}),
        make(EffectType::Cabinet, kCab, {{Cab::kMix, u.cabMix}, {Cab::kLevel, u.cabLevel}}),
        make(EffectType::Chorus, kChorus,
             {{Ch::kRateHz, u.chorusRate}, {Ch::kDepthMs, u.chorusDepth}, {Ch::kMix, u.chorusMix}}),
        make(EffectType::Delay, kDelay,
             {{Dl::kTimeMs, u.delayTime}, {Dl::kFeedback, u.delayFeedback}, {Dl::kMix, u.delayMix}}),
        make(EffectType::Reverb, kReverb,
             {{R::kRoomSize, u.reverbRoom}, {R::kDamping, u.reverbDamp}, {R::kMix, u.reverbMix}}),
        make(EffectType::Gain, kGain, {{G::kGain, u.gain}}),
    };
    return p;
}

bool loadPresetAsync(AppContext& ctx, const int index)
{
    const preset::Preset* preset = ctx.bank.at(index);
    if (preset == nullptr) {
        return false;
    }
    if (ctx.presetBusy.exchange(true)) {
        return false;
    }

    // Soft-mute on the GUI/control thread (short sleeps) so UI state stays single-threaded.
    const int fadeOut = std::max(1, static_cast<int>(std::lround(0.02 * ctx.sampleRate)));
    const int fadeIn = std::max(1, static_cast<int>(std::lround(0.03 * ctx.sampleRate)));

    ctx.mute.startFade(0.0f, fadeOut);
    std::this_thread::sleep_for(std::chrono::milliseconds(35));

    syncUiFromPreset(ctx, *preset);
    ctx.bank.apply(*preset, ctx.graph, ctx.ui.bypassed);
    ctx.currentPreset.store(index);

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    ctx.mute.startFade(1.0f, fadeIn);
    ctx.presetBusy.store(false);
    std::cout << "Loaded preset: " << preset->name << "\n";
    return true;
}

} // namespace app
