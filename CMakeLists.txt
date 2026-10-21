cmake_minimum_required(VERSION 3.22)

# Universal binary and minimum macOS version must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
    endif()
endif()

project(Contrail VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CONTRAIL_BUILD_PLUGIN "Build the JUCE plugin (VST3, AU, Standalone)" ON)
option(CONTRAIL_BUILD_TESTS "Build the DSP unit tests" ON)
option(CONTRAIL_BUILD_RENDER "Build the tool that renders audio previews (development only)" OFF)
option(CONTRAIL_BUILD_SNAPSHOT "Build a tool that renders the panel to PNG (development only)" OFF)

include(FetchContent)

# ---------------------------------------------------------------------------
# DSP library: plain C++, no JUCE, so it can be tested on any platform.
# ---------------------------------------------------------------------------
add_library(contrail_dsp STATIC
    source/dsp/TapeEngine.cpp)
target_include_directories(contrail_dsp PUBLIC source)
# The library is linked into plugin bundles, which are shared libraries.
set_target_properties(contrail_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(contrail_dsp PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(contrail_dsp PRIVATE /W4)
else()
    target_compile_options(contrail_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(CONTRAIL_BUILD_PLUGIN)
    set(CONTRAIL_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout (otherwise downloaded)")
    if(CONTRAIL_JUCE_DIR)
        add_subdirectory(${CONTRAIL_JUCE_DIR} juce EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.10
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    # The panel's two typefaces (SIL Open Font License, see resources/fonts).
    juce_add_binary_data(ContrailAssets
        HEADER_NAME ContrailAssets.h
        NAMESPACE ContrailAssets
        SOURCES
            resources/fonts/CormorantGaramond-Medium.ttf
            resources/fonts/CormorantGaramond-SemiBold.ttf
            resources/fonts/Jost-Regular.ttf
            resources/fonts/Jost-Medium.ttf)
    set_target_properties(ContrailAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

    set(CONTRAIL_UI_SOURCES
        source/PluginEditor.cpp
        source/PresetManager.cpp
        source/ui/CloudSky.cpp
        source/ui/Controls.cpp
        source/ui/Theme.cpp)
    if(APPLE)
        list(APPEND CONTRAIL_UI_SOURCES source/ui/ReducedMotion.mm)
    else()
        list(APPEND CONTRAIL_UI_SOURCES source/ui/ReducedMotion.cpp)
    endif()

    juce_add_plugin(Contrail
        PRODUCT_NAME "Contrail"
        COMPANY_NAME "Overcast Audio"
        BUNDLE_ID "com.overcastaudio.contrail"
        PLUGIN_MANUFACTURER_CODE Ovca
        PLUGIN_CODE Ctr1
        FORMATS VST3 AU Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Modulation
        COPY_PLUGIN_AFTER_BUILD ${APPLE})

    target_sources(Contrail PRIVATE
        source/PluginProcessor.cpp
        source/Parameters.cpp
        ${CONTRAIL_UI_SOURCES})
    target_include_directories(Contrail PRIVATE source)

    target_compile_definitions(Contrail PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Contrail
        PRIVATE
            contrail_dsp
            ContrailAssets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    if(CONTRAIL_BUILD_SNAPSHOT)
        juce_add_console_app(ContrailSnapshot PRODUCT_NAME "ContrailSnapshot")
        target_sources(ContrailSnapshot PRIVATE
            tools/Snapshot.cpp
            source/PluginProcessor.cpp
            source/Parameters.cpp
            ${CONTRAIL_UI_SOURCES})
        target_include_directories(ContrailSnapshot PRIVATE source)
        target_compile_definitions(ContrailSnapshot PRIVATE
            JucePlugin_Name="Contrail"
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0)
        target_link_libraries(ContrailSnapshot PRIVATE
            contrail_dsp
            ContrailAssets
            juce::juce_audio_utils
            juce::juce_recommended_config_flags)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(CONTRAIL_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(contrail_tests
        tests/BlockTests.cpp
        tests/HysteresisTests.cpp
        tests/WowFlutterTests.cpp
        tests/TransportTests.cpp
        tests/EngineTests.cpp
        tests/MemoryTests.cpp
        tests/VintageTests.cpp)
    target_link_libraries(contrail_tests PRIVATE contrail_dsp Catch2::Catch2WithMain)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(contrail_tests)
endif()

# ---------------------------------------------------------------------------
# Audio previews: renders a short test clip through every format to WAV files.
# ---------------------------------------------------------------------------
if(CONTRAIL_BUILD_RENDER)
    add_executable(contrail_render tools/Render.cpp)
    target_link_libraries(contrail_render PRIVATE contrail_dsp)
endif()
