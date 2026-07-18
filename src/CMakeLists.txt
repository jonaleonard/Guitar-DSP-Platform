add_executable(guitar_dsp_platform main.cpp)

target_include_directories(guitar_dsp_platform
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
