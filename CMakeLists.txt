cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version minimale de macOS")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")   # pas de DLL Visual C++ a installer

project(MJ7VocalChain VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MJ7_BUILD_TESTS "Compile le test d'integration (hote de test en ligne de commande)" OFF)
set(MJ7_JUCE_PATH "" CACHE PATH "Dossier JUCE local (sinon JUCE est telecharge automatiquement)")

if(MJ7_JUCE_PATH)
    add_subdirectory("${MJ7_JUCE_PATH}" juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MJ7_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(MJ7_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# Les codes ci-dessous identifient le plugin dans FL Studio : ne jamais les changer,
# sinon les projets existants ne retrouvent plus le plugin.
juce_add_plugin(MJ7VocalChain
    PRODUCT_NAME "MJ7 Vocal Chain"
    COMPANY_NAME "MJ7"
    BUNDLE_ID com.mj7.vocalchain
    PLUGIN_MANUFACTURER_CODE Mjse
    PLUGIN_CODE Mj7v
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MJ7VocalChain PRIVATE ${MJ7_SOURCES})
target_include_directories(MJ7VocalChain PRIVATE Source)
target_compile_definitions(MJ7VocalChain PUBLIC ${MJ7_DEFS})
target_link_libraries(MJ7VocalChain
    PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
if(MSVC)
    target_compile_options(MJ7VocalChain PUBLIC /utf-8)   # les textes de l'interface sont en UTF-8
endif()

if(MJ7_BUILD_TESTS)
    juce_add_console_app(MJ7HostTest PRODUCT_NAME "MJ7HostTest")
    target_sources(MJ7HostTest PRIVATE tests/host_test.cpp ${MJ7_SOURCES})
    target_include_directories(MJ7HostTest PRIVATE Source)
    target_compile_definitions(MJ7HostTest PRIVATE ${MJ7_DEFS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MJ7HostTest
        PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(MJ7HostTest PUBLIC /utf-8)
    endif()
endif()
