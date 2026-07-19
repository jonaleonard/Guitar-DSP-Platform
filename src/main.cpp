#include "audio/AudioEngine.h"

#include <iostream>

int main()
{
    audio::AudioEngineConfig config;
    config.inputDeviceName = "Volt";
    config.sampleRate = 48000;
    config.bufferFrames = 256;
    config.inputChannels = 1;
    config.outputChannels = 2;
    config.minimizeLatency = true;
    config.useDefaultOutputDevice = true;

    audio::AudioEngine engine(config);

    // Default processBlock is wire-through (mono in → stereo out).
    // Phase 2 will replace this with the effect graph.
    if (!engine.start()) {
        return 1;
    }

    const double latencyMs =
        (1000.0 * static_cast<double>(engine.streamLatencySamples())) /
        static_cast<double>(engine.sampleRate());

    std::cout << "Input device: " << engine.inputDeviceName() << "\n";
    std::cout << "Output device: " << engine.outputDeviceName() << "\n";
    std::cout << "Sample rate: " << engine.sampleRate() << " Hz\n";
    std::cout << "Buffer frames: " << engine.bufferFrames() << "\n";
    std::cout << "Stream latency: " << engine.streamLatencySamples() << " samples ("
              << latencyMs << " ms)\n";
    std::cout << "Input channels: " << engine.inputChannels()
              << ", output channels: " << engine.outputChannels() << "\n";
    std::cout << "Wire-through via AudioEngine. Guitar -> Volt input, monitor on system output.\n";
    std::cout << "Press Enter to stop.\n";

    std::cin.get();
    engine.stop();
    return 0;
}
