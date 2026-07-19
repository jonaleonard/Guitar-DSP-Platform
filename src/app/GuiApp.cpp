#include "app/AppContext.h"

#include "dsp/Fft.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace app {
namespace {

constexpr int kFftSize = 1024;
constexpr int kWaveDisplaySamples = 512; // ~10.7 ms @ 48 kHz — snappy, synced slice of same buffer

void drawScope(ImDrawList* dl,
               const ImVec2& origin,
               const ImVec2& size,
               const float* samples,
               const int count,
               const float yMin,
               const float yMax,
               const ImU32 lineColor,
               const ImU32 gridColor)
{
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(18, 20, 26, 255));
    dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(55, 60, 72, 255));

    // Mid / clip guides
    const float midY = origin.y + size.y * 0.5f;
    dl->AddLine(ImVec2(origin.x, midY), ImVec2(origin.x + size.x, midY), gridColor, 1.0f);
    const float clipT = origin.y + size.y * ((0.95f - yMax) / (yMin - yMax));
    const float clipB = origin.y + size.y * ((-0.95f - yMax) / (yMin - yMax));
    dl->AddLine(ImVec2(origin.x, clipT), ImVec2(origin.x + size.x, clipT), IM_COL32(90, 50, 50, 120), 1.0f);
    dl->AddLine(ImVec2(origin.x, clipB), ImVec2(origin.x + size.x, clipB), IM_COL32(90, 50, 50, 120), 1.0f);

    if (count < 2 || size.x < 2.0f) {
        return;
    }
    const float range = std::max(1.0e-6f, yMax - yMin);
    ImVec2 prev{};
    for (int i = 0; i < count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        const float x = origin.x + t * size.x;
        float v = samples[i];
        v = std::clamp(v, yMin, yMax);
        const float y = origin.y + (1.0f - (v - yMin) / range) * size.y;
        const ImVec2 pt(x, y);
        if (i > 0) {
            dl->AddLine(prev, pt, lineColor, 1.5f);
        }
        prev = pt;
    }
}

void drawSpectrumBars(ImDrawList* dl,
                      const ImVec2& origin,
                      const ImVec2& size,
                      const float* dbBins,
                      const int numBins,
                      const double sampleRate)
{
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(18, 20, 26, 255));
    dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(55, 60, 72, 255));

    constexpr float kMinDb = -80.0f;
    constexpr float kMaxDb = 0.0f;
    constexpr int kBars = 64;

    // -6 / -12 / -24 dB guides
    for (float dbMark : {-6.0f, -12.0f, -24.0f, -48.0f}) {
        const float t = (dbMark - kMaxDb) / (kMinDb - kMaxDb);
        const float y = origin.y + t * size.y;
        dl->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), IM_COL32(50, 55, 65, 160), 1.0f);
    }

    const float nyquist = static_cast<float>(sampleRate * 0.5);
    for (int b = 0; b < kBars; ++b) {
        // Log-frequency mapping 40 Hz → Nyquist
        const float t0 = static_cast<float>(b) / static_cast<float>(kBars);
        const float t1 = static_cast<float>(b + 1) / static_cast<float>(kBars);
        const float f0 = 40.0f * std::pow(nyquist / 40.0f, t0);
        const float f1 = 40.0f * std::pow(nyquist / 40.0f, t1);
        const int i0 = std::clamp(static_cast<int>(f0 / nyquist * (numBins - 1)), 1, numBins - 1);
        const int i1 = std::clamp(static_cast<int>(f1 / nyquist * (numBins - 1)), i0, numBins - 1);

        float peakDb = kMinDb;
        for (int i = i0; i <= i1; ++i) {
            peakDb = std::max(peakDb, dbBins[i]);
        }
        peakDb = std::clamp(peakDb, kMinDb, kMaxDb);
        const float hNorm = (peakDb - kMinDb) / (kMaxDb - kMinDb);
        const float x0 = origin.x + t0 * size.x + 1.0f;
        const float x1 = origin.x + t1 * size.x - 1.0f;
        const float y1 = origin.y + size.y;
        const float y0 = y1 - hNorm * size.y;
        const float hue = 0.55f - 0.45f * hNorm;
        const ImU32 col = ImColor::HSV(hue, 0.75f, 0.55f + 0.45f * hNorm);
        if (x1 > x0) {
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col);
        }
    }

    // Frequency labels
    dl->AddText(ImVec2(origin.x + 4, origin.y + size.y - 16), IM_COL32(160, 165, 175, 200), "40Hz");
    dl->AddText(ImVec2(origin.x + size.x * 0.5f - 12, origin.y + size.y - 16),
                IM_COL32(160, 165, 175, 200),
                "1k");
    dl->AddText(ImVec2(origin.x + size.x - 36, origin.y + size.y - 16),
                IM_COL32(160, 165, 175, 200),
                "Nyq");
}

void drawVisualizer(AppContext& ctx,
                    dsp::Fft& fft,
                    std::vector<float>& wave,
                    std::vector<float>& real,
                    std::vector<float>& imag,
                    std::vector<float>& mags,
                    float& clipGlow,
                    float& peakHold)
{
    wave.resize(static_cast<std::size_t>(kFftSize));
    real.resize(static_cast<std::size_t>(kFftSize));
    imag.resize(static_cast<std::size_t>(kFftSize));
    mags.resize(static_cast<std::size_t>(kFftSize / 2));

    // One snapshot drives both scopes — keeps wave/spectrum time-aligned.
    ctx.vizRing.snapshotLatest(wave.data(), static_cast<std::size_t>(kFftSize));

    if (ctx.vizRing.exchangeClipFlag()) {
        clipGlow = 1.0f;
    }
    const float dt = ImGui::GetIO().DeltaTime;
    clipGlow = std::max(0.0f, clipGlow - dt * 1.5f);

    // Smooth peak meter: sample + hold decay (~300 ms visual release)
    const float instant = ctx.vizRing.samplePeak(1.0f - std::exp(-dt * 4.0f));
    peakHold = std::max(peakHold * std::exp(-dt * 1.8f), instant);

    // Spectrum from same buffer (Hann → FFT → dB)
    for (int i = 0; i < kFftSize; ++i) {
        const float w =
            0.5f - 0.5f * std::cos(2.0f * 3.14159265f * static_cast<float>(i) /
                                   static_cast<float>(kFftSize - 1));
        real[static_cast<std::size_t>(i)] = wave[static_cast<std::size_t>(i)] * w;
        imag[static_cast<std::size_t>(i)] = 0.0f;
    }
    fft.forward(real.data(), imag.data());
    for (int i = 0; i < kFftSize / 2; ++i) {
        const float re = real[static_cast<std::size_t>(i)];
        const float im = imag[static_cast<std::size_t>(i)];
        const float mag = std::sqrt(re * re + im * im) * (2.0f / static_cast<float>(kFftSize));
        mags[static_cast<std::size_t>(i)] = 20.0f * std::log10(std::max(1.0e-8f, mag));
    }

    // Waveform uses the newest half of the same snapshot (more “live”)
    const float* waveView = wave.data() + (kFftSize - kWaveDisplaySamples);

    if (ImGui::BeginTable("viz", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Waveform (synced)");
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 size(w, 110.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            drawScope(dl,
                      p,
                      size,
                      waveView,
                      kWaveDisplaySamples,
                      -1.0f,
                      1.0f,
                      IM_COL32(90, 200, 255, 255),
                      IM_COL32(60, 70, 85, 180));
            ImGui::Dummy(size);
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Spectrum (same buffer)");
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 size(w, 110.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            drawSpectrumBars(dl, p, size, mags.data(), kFftSize / 2, ctx.sampleRate);
            ImGui::Dummy(size);
        }
        ImGui::EndTable();
    }

    // Peak meter bar
    const float peakPct = std::clamp(peakHold * 100.0f, 0.0f, 150.0f);
    const ImVec4 clipColor = clipGlow > 0.05f ? ImVec4(0.95f, 0.25f, 0.15f, 1.0f)
                                              : (peakHold > 0.85f ? ImVec4(0.95f, 0.75f, 0.2f, 1.0f)
                                                                 : ImVec4(0.35f, 0.8f, 0.4f, 1.0f));
    ImGui::TextColored(clipColor, clipGlow > 0.05f ? "CLIP" : (peakHold > 0.85f ? "HOT " : "OK  "));
    ImGui::SameLine();
    ImGui::ProgressBar(std::min(1.0f, peakHold), ImVec2(-1, 0), nullptr);
    ImGui::Text("Peak hold: %.0f%% (%.1f dBFS)  — studio target ~-12…-9 dBFS (25–35%%)",
                peakPct,
                peakHold > 1.0e-8f ? 20.0f * std::log10(peakHold) : -120.0f);
}

bool slotEnabled(AppContext& ctx, const int slot)
{
    return !ctx.ui.bypassed[static_cast<std::size_t>(slot)];
}

void drawEnable(AppContext& ctx, const int slot, const char* label)
{
    bool on = slotEnabled(ctx, slot);
    if (ImGui::Checkbox(label, &on)) {
        setSlotBypassed(ctx, slot, !on);
    }
}

void sliderFloat(AppContext& ctx,
                 const int slot,
                 const char* label,
                 float* value,
                 const float minV,
                 const float maxV,
                 const char* fmt = "%.2f")
{
    if (ImGui::SliderFloat(label, value, minV, maxV, fmt)) {
        enableSlot(ctx, slot);
        pushSlotParams(ctx, slot);
    }
}

template <typename Body>
void drawEffectPanel(AppContext& ctx, const int slot, const char* title, Body&& drawBody)
{
    if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID(slot);
        drawEnable(ctx, slot, "Enabled");
        ImGui::BeginDisabled(!slotEnabled(ctx, slot));
        drawBody();
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void drawProfiler(AppContext& ctx)
{
    const double sr = ctx.sampleRate;
    const unsigned frames = ctx.engine != nullptr ? ctx.engine->bufferFrames() : 0;
    const double budgetUs = sr > 0.0 ? (1.0e6 * static_cast<double>(frames) / sr) : 1.0;
    const double totalUs =
        static_cast<double>(ctx.graph.totalAvgNanos()) / 1000.0;
    const double totalPct = budgetUs > 0.0 ? (100.0 * totalUs / budgetUs) : 0.0;

    if (ImGui::BeginTable("profiler",
                          4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Effect");
        ImGui::TableSetupColumn("µs");
        ImGui::TableSetupColumn("% budget");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();

        for (int i = 0; i < kNumSlots; ++i) {
            const auto& slot = ctx.graph.profileSlot(i);
            const double us = static_cast<double>(slot.avgNanos.load(std::memory_order_relaxed)) / 1000.0;
            const double pct = budgetUs > 0.0 ? (100.0 * us / budgetUs) : 0.0;
            const bool on = slot.processed.load(std::memory_order_relaxed);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(slotName(i));
            ImGui::TableNextColumn();
            ImGui::Text("%.1f", us);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", pct);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(on ? "active" : "bypass");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("TOTAL");
        ImGui::TableNextColumn();
        ImGui::Text("%.1f", totalUs);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f", totalPct);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f µs budget", budgetUs);
        ImGui::EndTable();
    }
}

} // namespace

int runGui(AppContext& ctx)
{
    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW.\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1100, 860, "Guitar DSP Platform", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "Failed to create GLFW window.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    dsp::Fft fft(kFftSize);
    std::vector<float> wave;
    std::vector<float> real;
    std::vector<float> imag;
    std::vector<float> mags;
    float clipGlow = 0.0f;
    float peakHold = 0.0f;

    while (!glfwWindowShouldClose(window) && ctx.running.load()) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("Guitar DSP",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::TextUnformatted("Guitar DSP Platform — Phases 8–11");
        ImGui::Separator();

        const double sr = ctx.sampleRate;
        const unsigned frames = ctx.engine != nullptr ? ctx.engine->bufferFrames() : 0;
        const unsigned streamLat =
            ctx.engine != nullptr ? ctx.engine->streamLatencySamples() : 0;
        const double bufMs = sr > 0.0 ? (1000.0 * static_cast<double>(frames) / sr) : 0.0;
        const double streamMs = sr > 0.0 ? (1000.0 * static_cast<double>(streamLat) / sr) : 0.0;
        const auto processUs = ctx.metrics.processMicros.load(std::memory_order_relaxed);
        const double blockUs = sr > 0.0 ? (1.0e6 * static_cast<double>(frames) / sr) : 1.0;
        const double cpuPct = blockUs > 0.0 ? (100.0 * static_cast<double>(processUs) / blockUs) : 0.0;
        const auto xIn = ctx.engine != nullptr ? ctx.engine->inputOverflowCount() : 0;
        const auto xOut = ctx.engine != nullptr ? ctx.engine->outputUnderflowCount() : 0;

        ImGui::Text("Buffer: %.2f ms (%u)  |  Stream: %.2f ms  |  @ %.0f Hz  |  DSP: %.1f%%",
                    bufMs,
                    frames,
                    streamMs,
                    sr,
                    std::min(999.0, cpuPct));
        ImGui::Text("I/O: %s → %s  |  xruns in/out: %llu / %llu",
                    ctx.engine != nullptr ? ctx.engine->inputDeviceName().c_str() : "?",
                    ctx.engine != nullptr ? ctx.engine->outputDeviceName().c_str() : "?",
                    static_cast<unsigned long long>(xIn),
                    static_cast<unsigned long long>(xOut));
        ImGui::Separator();

        ImGui::TextUnformatted("Presets");
        const bool busy = ctx.presetBusy.load();
        ImGui::BeginDisabled(busy);
        for (int i = 0; i < ctx.bank.size(); ++i) {
            const auto* p = ctx.bank.at(i);
            if (p == nullptr) {
                continue;
            }
            const bool selected = ctx.currentPreset.load() == i;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.75f, 1.f));
            }
            if (ImGui::Button(p->name.c_str(), ImVec2(110, 0))) {
                loadPresetAsync(ctx, i);
            }
            if (selected) {
                ImGui::PopStyleColor();
            }
            if (i + 1 < ctx.bank.size()) {
                ImGui::SameLine();
            }
        }
        ImGui::EndDisabled();
        if (busy) {
            ImGui::SameLine();
            ImGui::TextDisabled("switching…");
        }

        ImGui::SameLine();
        ImGui::Dummy(ImVec2(16, 0));
        ImGui::SameLine();
        static char saveName[64] = "Custom";
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("##saveName", saveName, sizeof(saveName));
        ImGui::SameLine();
        if (ImGui::Button("Save JSON")) {
            const std::string name = saveName[0] != '\0' ? saveName : "Custom";
            const preset::Preset snap = captureCurrentPreset(ctx, name);
            const std::string path = ctx.presetsDirectory + "/" + name + ".json";
            if (ctx.bank.saveToFile(snap, path)) {
                std::printf("Saved %s\n", path.c_str());
            } else {
                std::fprintf(stderr, "Failed to save %s\n", path.c_str());
            }
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Visualizer (Phase 9)", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawVisualizer(ctx, fft, wave, real, imag, mags, clipGlow, peakHold);
        }
        if (ImGui::CollapsingHeader("Profiler (Phase 10)", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawProfiler(ctx);
        }

        ImGui::Separator();
        ImGui::BeginChild("effects", ImVec2(0, 0), ImGuiChildFlags_None);

        drawEffectPanel(ctx, kGate, "Noise Gate", [&] {
            sliderFloat(ctx, kGate, "Threshold (dB)", &ctx.ui.gateThresh, -80.f, 0.f, "%.1f");
            sliderFloat(ctx, kGate, "Attack (ms)", &ctx.ui.gateAttack, 0.1f, 50.f, "%.1f");
            sliderFloat(ctx, kGate, "Release (ms)", &ctx.ui.gateRelease, 1.f, 500.f, "%.0f");
        });

        drawEffectPanel(ctx, kComp, "Compressor", [&] {
            sliderFloat(ctx, kComp, "Threshold (dB)", &ctx.ui.compThresh, -60.f, 0.f, "%.1f");
            sliderFloat(ctx, kComp, "Ratio", &ctx.ui.compRatio, 1.f, 20.f, "%.1f");
            sliderFloat(ctx, kComp, "Attack (ms)", &ctx.ui.compAttack, 0.1f, 100.f, "%.1f");
            sliderFloat(ctx, kComp, "Release (ms)", &ctx.ui.compRelease, 1.f, 500.f, "%.0f");
            sliderFloat(ctx, kComp, "Makeup (dB)", &ctx.ui.compMakeup, -12.f, 24.f, "%.1f");
        });

        drawEffectPanel(ctx, kDrive, "Overdrive", [&] {
            sliderFloat(ctx, kDrive, "Drive", &ctx.ui.drive, 1.f, 25.f);
            sliderFloat(ctx, kDrive, "Mix", &ctx.ui.driveMix, 0.f, 1.f);
            sliderFloat(ctx, kDrive, "Output", &ctx.ui.driveOut, 0.f, 2.f);
        });

        drawEffectPanel(ctx, kEq, "EQ", [&] {
            sliderFloat(ctx, kEq, "Low (dB)", &ctx.ui.eqLow, -18.f, 18.f, "%.1f");
            sliderFloat(ctx, kEq, "Mid (dB)", &ctx.ui.eqMid, -18.f, 18.f, "%.1f");
            sliderFloat(ctx, kEq, "High (dB)", &ctx.ui.eqHigh, -18.f, 18.f, "%.1f");
            sliderFloat(ctx, kEq, "Mid Freq (Hz)", &ctx.ui.eqMidFreq, 200.f, 4000.f, "%.0f");
            sliderFloat(ctx, kEq, "Mid Q", &ctx.ui.eqMidQ, 0.2f, 4.f);
        });

        drawEffectPanel(ctx, kAmp, "Amp Sim", [&] {
            sliderFloat(ctx, kAmp, "Pre Gain", &ctx.ui.ampPre, 0.1f, 10.f);
            sliderFloat(ctx, kAmp, "Drive", &ctx.ui.ampDrive, 1.f, 25.f);
            sliderFloat(ctx, kAmp, "Bass (dB)", &ctx.ui.ampBass, -12.f, 12.f, "%.1f");
            sliderFloat(ctx, kAmp, "Mid (dB)", &ctx.ui.ampMid, -12.f, 12.f, "%.1f");
            sliderFloat(ctx, kAmp, "Treble (dB)", &ctx.ui.ampTreble, -12.f, 12.f, "%.1f");
            sliderFloat(ctx, kAmp, "Presence (dB)", &ctx.ui.ampPresence, -12.f, 12.f, "%.1f");
            sliderFloat(ctx, kAmp, "Master", &ctx.ui.ampMaster, 0.f, 2.f);
        });

        drawEffectPanel(ctx, kCab, "Cabinet", [&] {
            sliderFloat(ctx, kCab, "Mix", &ctx.ui.cabMix, 0.f, 1.f);
            sliderFloat(ctx, kCab, "Level", &ctx.ui.cabLevel, 0.f, 2.f);
        });

        drawEffectPanel(ctx, kChorus, "Chorus", [&] {
            sliderFloat(ctx, kChorus, "Rate (Hz)", &ctx.ui.chorusRate, 0.1f, 5.f);
            sliderFloat(ctx, kChorus, "Depth (ms)", &ctx.ui.chorusDepth, 0.5f, 12.f);
            sliderFloat(ctx, kChorus, "Mix", &ctx.ui.chorusMix, 0.f, 1.f);
        });

        drawEffectPanel(ctx, kDelay, "Delay", [&] {
            sliderFloat(ctx, kDelay, "Time (ms)", &ctx.ui.delayTime, 1.f, 1500.f, "%.0f");
            sliderFloat(ctx, kDelay, "Feedback", &ctx.ui.delayFeedback, 0.f, 0.95f);
            sliderFloat(ctx, kDelay, "Mix", &ctx.ui.delayMix, 0.f, 1.f);
        });

        drawEffectPanel(ctx, kReverb, "Reverb", [&] {
            sliderFloat(ctx, kReverb, "Room Size", &ctx.ui.reverbRoom, 0.f, 1.f);
            sliderFloat(ctx, kReverb, "Damping", &ctx.ui.reverbDamp, 0.f, 1.f);
            sliderFloat(ctx, kReverb, "Mix", &ctx.ui.reverbMix, 0.f, 1.f);
        });

        drawEffectPanel(ctx, kGain, "Output Gain", [&] {
            sliderFloat(ctx, kGain, "Gain", &ctx.ui.gain, 0.f, 2.f);
        });

        ImGui::EndChild();
        ImGui::End();

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ctx.running.store(false);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace app
