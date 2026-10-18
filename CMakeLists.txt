cmake_minimum_required(VERSION 3.22)

# Link the runtime statically so the plugin needs no extra Visual C++ installs on the target PC
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(DAWDiscordBridge VERSION 0.2.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(DAWDiscordBridge
    COMPANY_NAME "DAW-Discord-Bridge"
    PLUGIN_MANUFACTURER_CODE Ddbr
    PLUGIN_CODE Ddb2
    FORMATS VST3
    PRODUCT_NAME "DAW-Discord-Bridge"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(DAWDiscordBridge PRIVATE
    Source/OutputEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(DAWDiscordBridge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(DAWDiscordBridge
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
