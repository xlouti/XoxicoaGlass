cmake_minimum_required(VERSION 3.22)

# On a Mac, default to a universal (Apple Silicon + Intel) build.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures")
    endif()
    # 10.15 so the Intel slice loads on Catalina. The arm64 slice is raised to 11.0 by the linker.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(XoxicoaGlass VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(XG_BUILD_TESTS "Build the offline knob test / demo / UI snapshot tools" OFF)
set(XG_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout (otherwise fetched from GitHub)")

if(XG_JUCE_DIR)
    add_subdirectory(${XG_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(XoxicoaGlass
    COMPANY_NAME              "Xoxicoa"
    PRODUCT_NAME              "Xoxicoa Glass"
    BUNDLE_ID                 com.xoxicoa.xoxicoaglass
    PLUGIN_MANUFACTURER_CODE  Xoxi
    PLUGIN_CODE               XxGl
    FORMATS                   AU VST3 Standalone
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Fx EQ Dynamics
    AU_MAIN_TYPE              kAudioUnitType_Effect)

set(XG_SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_sources(XoxicoaGlass PRIVATE ${XG_SOURCES})
target_include_directories(XoxicoaGlass PRIVATE Source)

set(XG_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_compile_definitions(XoxicoaGlass PUBLIC ${XG_DEFS})

target_link_libraries(XoxicoaGlass
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(APPLE)
    target_link_libraries(XoxicoaGlass PUBLIC juce::juce_recommended_lto_flags)
    # The 15 SDK records macOS 11 os_workgroup imports as strong. ld -U does not
    # weaken a symbol the SDK provides, so mark the nlist entries weak after link.
    foreach(xg_fmt AU VST3 Standalone)
        if(TARGET XoxicoaGlass_${xg_fmt})
            add_custom_command(TARGET XoxicoaGlass_${xg_fmt} POST_BUILD
                COMMAND /usr/bin/python3 "${CMAKE_SOURCE_DIR}/cmake/weak_os_workgroup.py"
                        "$<TARGET_FILE:XoxicoaGlass_${xg_fmt}>"
                VERBATIM)
        endif()
    endforeach()
endif()

if(XG_BUILD_TESTS)
    foreach(tool XoxicoaGlassAudit XoxicoaGlassUISnapshot)
        juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
        target_include_directories(${tool} PRIVATE Source tests)
        target_compile_definitions(${tool} PRIVATE ${XG_DEFS} JucePlugin_Name="Xoxicoa Glass" PROC_CLASS=XoxicoaGlassProcessor JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_audio_formats
                              PUBLIC juce::juce_recommended_config_flags)
    endforeach()
    target_sources(XoxicoaGlassAudit PRIVATE tests/Audit.cpp ${XG_SOURCES})
    target_sources(XoxicoaGlassUISnapshot PRIVATE tests/UISnapshot.cpp ${XG_SOURCES})
endif()
