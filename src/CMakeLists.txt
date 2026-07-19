add_library(guitar_dsp_audio
    audio/AudioEngine.cpp
    audio/ParameterQueue.cpp
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
