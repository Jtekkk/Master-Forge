cmake_minimum_required(VERSION 3.22)

project(MasterForge VERSION 0.3.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# JUCE
#
# JUCE is fetched automatically so the project is self-contained. Override the
# version with -DJUCE_GIT_TAG=8.0.4 (any tag/branch) if you prefer a different
# release, or point JUCE_SOURCE_DIR at a local checkout to skip the download.
# ---------------------------------------------------------------------------
set(JUCE_GIT_TAG "7.0.12" CACHE STRING "JUCE git tag/branch to fetch")
set(JUCE_SOURCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_SOURCE_DIR)
    message(STATUS "Master Forge: using local JUCE at ${JUCE_SOURCE_DIR}")
    add_subdirectory(${JUCE_SOURCE_DIR} juce_build)
else()
    include(FetchContent)
    message(STATUS "Master Forge: fetching JUCE ${JUCE_GIT_TAG} via FetchContent")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${JUCE_GIT_TAG}
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
option(MASTERFORGE_BUILD_STANDALONE "Also build a Standalone app (handy for testing without a DAW)" OFF)
option(MASTERFORGE_COPY_AFTER_BUILD "Copy the built plugin into the system plugin folder after building" ON)
set(MASTERFORGE_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (enables the AAX build)")

set(MASTERFORGE_FORMATS VST3)

# Audio Unit — only valid on macOS.
if(APPLE)
    list(APPEND MASTERFORGE_FORMATS AU)
endif()

# AAX (Pro Tools) — needs the Avid AAX SDK, supplied by the user.
if(MASTERFORGE_AAX_SDK_PATH)
    juce_set_aax_sdk_path("${MASTERFORGE_AAX_SDK_PATH}")
    list(APPEND MASTERFORGE_FORMATS AAX)
    message(STATUS "Master Forge: AAX enabled (SDK: ${MASTERFORGE_AAX_SDK_PATH})")
endif()

if(MASTERFORGE_BUILD_STANDALONE)
    list(APPEND MASTERFORGE_FORMATS Standalone)
endif()

message(STATUS "Master Forge: building formats: ${MASTERFORGE_FORMATS}")

# When cross-compiling (e.g. MinGW from Linux) the VST3 manifest helper is built
# for the target and can't run on the build host, so skip the optional
# moduleinfo.json generation in that case.
set(MASTERFORGE_VST3_MANIFEST TRUE)
if(CMAKE_CROSSCOMPILING)
    set(MASTERFORGE_VST3_MANIFEST FALSE)
endif()

juce_add_plugin(MasterForge
    COMPANY_NAME            "Forge Audio"
    BUNDLE_ID               com.forgeaudio.masterforge
    PLUGIN_MANUFACTURER_CODE Forg
    PLUGIN_CODE             Mfrg
    FORMATS                 ${MASTERFORGE_FORMATS}
    PRODUCT_NAME            "Master Forge"
    DESCRIPTION             "Mastering chain: EQ, compression, saturation, stereo width and a brickwall limiter with LUFS metering"
    VST3_CATEGORIES         "Fx" "Mastering" "Dynamics" "EQ"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_AUTO_MANIFEST ${MASTERFORGE_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD ${MASTERFORGE_COPY_AFTER_BUILD})

juce_generate_juce_header(MasterForge)

target_sources(MasterForge PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/PresetManager.cpp
    source/gui/ForgeLookAndFeel.cpp)

target_compile_definitions(MasterForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(MasterForge PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# DSP smoke tests (no host required): cmake -B build -DMASTERFORGE_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(MASTERFORGE_BUILD_TESTS "Build the DSP smoke-test console app" OFF)
if(MASTERFORGE_BUILD_TESTS)
    juce_add_console_app(MasterForgeTests PRODUCT_NAME "MasterForgeTests")
    juce_generate_juce_header(MasterForgeTests)
    target_sources(MasterForgeTests PRIVATE tests/dsp_test.cpp)
    target_include_directories(MasterForgeTests PRIVATE source)
    target_compile_definitions(MasterForgeTests PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MasterForgeTests PRIVATE
        juce::juce_dsp
        juce::juce_audio_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # End-to-end test: instantiate the processor + editor and run audio through
    # the whole chain (multiband, true-peak limiter, presets) without a host.
    juce_add_console_app(MasterForgeIntegration PRODUCT_NAME "MasterForgeIntegration")
    juce_generate_juce_header(MasterForgeIntegration)
    target_sources(MasterForgeIntegration PRIVATE
        tests/integration_test.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/PresetManager.cpp
        source/gui/ForgeLookAndFeel.cpp)
    target_include_directories(MasterForgeIntegration PRIVATE source)
    target_compile_definitions(MasterForgeIntegration PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
    target_link_libraries(MasterForgeIntegration PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Offline audio analyzer (THD / frequency response).
    juce_add_console_app(MasterForgeAnalyze PRODUCT_NAME "MasterForgeAnalyze")
    juce_generate_juce_header(MasterForgeAnalyze)
    target_sources(MasterForgeAnalyze PRIVATE
        tools/analyze_audio.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/PresetManager.cpp
        source/gui/ForgeLookAndFeel.cpp)
    target_include_directories(MasterForgeAnalyze PRIVATE source)
    target_compile_definitions(MasterForgeAnalyze PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
    target_link_libraries(MasterForgeAnalyze PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Offline UI screenshot tool (renders the editor to a PNG, no host/display).
    juce_add_console_app(MasterForgeRender PRODUCT_NAME "MasterForgeRender")
    juce_generate_juce_header(MasterForgeRender)
    target_sources(MasterForgeRender PRIVATE
        tools/render_ui.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/PresetManager.cpp
        source/gui/ForgeLookAndFeel.cpp)
    target_include_directories(MasterForgeRender PRIVATE source)
    target_compile_definitions(MasterForgeRender PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
    target_link_libraries(MasterForgeRender PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
