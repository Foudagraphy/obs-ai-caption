cmake_minimum_required(VERSION 3.16)
project(obs-ai-caption VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(ENABLE_QT "Build Qt UI dialog" ON)

# Find OBS Studio
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

if(ENABLE_QT)
    find_package(Qt6 COMPONENTS Core Widgets REQUIRED)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_AUTORCC ON)
endif()

# Source files
set(SOURCES
    src/obs_ai_caption_plugin.cpp
    src/PluginManager.cpp
    src/AudioCaptureSession.cpp
    src/AudioSegmenter.cpp
    src/GroqWhisperClient.cpp
    src/GeminiClient.cpp
    src/SrtWriter.cpp
    src/TextOutputWriter.cpp
)

set(HEADERS
    src/PluginManager.h
    src/PluginSettings.h
    src/AudioCaptureSession.h
    src/AudioSegmenter.h
    src/GroqWhisperClient.h
    src/GeminiClient.h
    src/WavEncoder.h
    src/SrtWriter.h
    src/TextOutputWriter.h
)

if(ENABLE_QT)
    list(APPEND SOURCES
        src/ui/CaptionSettingsDialog.cpp
    )
    list(APPEND HEADERS
        src/ui/CaptionSettingsDialog.h
    )
endif()

# Plugin Target
add_library(obs-ai-caption MODULE
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(obs-ai-caption PRIVATE
    src
    src/ui
)

target_link_libraries(obs-ai-caption PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
)

if(ENABLE_QT)
    target_link_libraries(obs-ai-caption PRIVATE
        Qt6::Core
        Qt6::Widgets
    )
endif()

if(WIN32)
    # Link native Windows networking for HTTPS calls to Groq API (Zero external dependencies!)
    target_link_libraries(obs-ai-caption PRIVATE
        winhttp
        ws2_32
    )
    set_target_properties(obs-ai-caption PROPERTIES
        PREFIX ""
        OUTPUT_NAME "obs-ai-caption"
    )
endif()

# Installation target
install(TARGETS obs-ai-caption
    LIBRARY DESTINATION "${OBS_PLUGIN_DESTINATION}"
    RUNTIME DESTINATION "${OBS_PLUGIN_DESTINATION}"
)
