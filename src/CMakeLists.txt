add_library(guitar_dsp_audio
    audio/AudioEngine.cpp
    audio/ParameterQueue.cpp
    dsp/AmpSimEffect.cpp
    dsp/Biquad.cpp
    dsp/CabinetEffect.cpp
    dsp/ChorusEffect.cpp
    dsp/CompressorEffect.cpp
    dsp/DelayEffect.cpp
    dsp/DelayLine.cpp
    dsp/EffectGraph.cpp
    dsp/EqualizerEffect.cpp
    dsp/Fft.cpp
    dsp/GainEffect.cpp
    dsp/GraphCommandQueue.cpp
    dsp/NoiseGateEffect.cpp
    dsp/OverdriveEffect.cpp
    dsp/PartitionedConvolver.cpp
    dsp/ReverbEffect.cpp
    dsp/SmoothedValue.cpp
    dsp/SyntheticIr.cpp
    dsp/WavFile.cpp
)

target_include_directories(guitar_dsp_audio
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(guitar_dsp_audio
    PUBLIC
        RtAudio::rtaudio
)

add_executable(guitar_dsp_platform main.cpp)

target_link_libraries(guitar_dsp_platform
    PRIVATE
        guitar_dsp_audio
)
