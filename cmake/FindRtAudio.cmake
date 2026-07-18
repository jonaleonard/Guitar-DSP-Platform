find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    if(DEFINED ENV{PKG_CONFIG_PATH})
        set(_rtaudio_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
    else()
        set(_rtaudio_pkg_config_path "")
    endif()

    foreach(_prefix IN ITEMS /opt/homebrew /usr/local)
        if(EXISTS "${_prefix}/lib/pkgconfig/rtaudio.pc")
            if(_rtaudio_pkg_config_path STREQUAL "")
                set(_rtaudio_pkg_config_path "${_prefix}/lib/pkgconfig")
            else()
                set(_rtaudio_pkg_config_path "${_prefix}/lib/pkgconfig:${_rtaudio_pkg_config_path}")
            endif()
        endif()
    endforeach()

    if(NOT _rtaudio_pkg_config_path STREQUAL "")
        set(ENV{PKG_CONFIG_PATH} "${_rtaudio_pkg_config_path}")
    endif()

    pkg_check_modules(RTAUDIO QUIET rtaudio)
endif()

if(NOT RTAUDIO_FOUND)
    find_path(RTAUDIO_INCLUDE_DIRS
        NAMES RtAudio.h
        PATHS /opt/homebrew/include/rtaudio /usr/local/include/rtaudio
    )
    find_library(RTAUDIO_LIBRARY
        NAMES rtaudio
        PATHS /opt/homebrew/lib /usr/local/lib
    )

    if(RTAUDIO_INCLUDE_DIRS AND RTAUDIO_LIBRARY)
        set(RTAUDIO_FOUND TRUE)
        set(RTAUDIO_LIBRARIES "${RTAUDIO_LIBRARY}")
    endif()
endif()

if(RTAUDIO_FOUND AND NOT TARGET RtAudio::rtaudio)
    add_library(RtAudio::rtaudio INTERFACE IMPORTED)
    set_target_properties(RtAudio::rtaudio PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${RTAUDIO_INCLUDE_DIRS}"
    )

    if(RTAUDIO_LIBRARY_DIRS)
        set_property(TARGET RtAudio::rtaudio APPEND PROPERTY
            INTERFACE_LINK_DIRECTORIES "${RTAUDIO_LIBRARY_DIRS}"
        )
    endif()

    set_property(TARGET RtAudio::rtaudio APPEND PROPERTY
        INTERFACE_LINK_LIBRARIES "${RTAUDIO_LIBRARIES}"
    )

    if(RTAUDIO_CFLAGS_OTHER)
        set_property(TARGET RtAudio::rtaudio APPEND PROPERTY
            INTERFACE_COMPILE_OPTIONS "${RTAUDIO_CFLAGS_OTHER}"
        )
    endif()

    if(APPLE)
        set_property(TARGET RtAudio::rtaudio APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES "-framework CoreAudio;-framework CoreFoundation"
        )
        set_property(TARGET RtAudio::rtaudio APPEND PROPERTY
            INTERFACE_COMPILE_DEFINITIONS "__MACOSX_CORE__"
        )
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RtAudio DEFAULT_MSG RTAUDIO_LIBRARIES RTAUDIO_INCLUDE_DIRS)
