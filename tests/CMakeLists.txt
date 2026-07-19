add_executable(sine_to_wav_test
    sine_to_wav_test.cpp
)

target_include_directories(sine_to_wav_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sine_to_wav_test
    PRIVATE
        guitar_dsp_audio
)

add_test(
    NAME sine_to_wav_test
    COMMAND sine_to_wav_test "${CMAKE_BINARY_DIR}/sine_wire.wav"
)

add_executable(gain_graph_test
    gain_graph_test.cpp
)

target_include_directories(gain_graph_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(gain_graph_test
    PRIVATE
        guitar_dsp_audio
)

add_test(
    NAME gain_graph_test
    COMMAND gain_graph_test "${CMAKE_BINARY_DIR}/gain_graph.wav"
)
