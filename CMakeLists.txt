cmake_minimum_required(VERSION 3.22)

project(ChordText VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at libs/JUCE. See README.md for the clone command.")
endif()

add_subdirectory(libs/JUCE)

set(THEORY_SOURCES
    Source/Theory/Note.cpp
    Source/Theory/Key.cpp
    Source/Theory/Chord.cpp
    Source/Theory/ChordSymbolParser.cpp)
set(PLUGIN_SOURCES Source/Plugin/PluginProcessor.cpp)

juce_add_plugin(ChordText
    COMPANY_NAME "jirwo"
    PLUGIN_MANUFACTURER_CODE Jirw
    PLUGIN_CODE Chtx
    PRODUCT_NAME "Chord Text"
    FORMATS VST3
    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ChordText PRIVATE ${PLUGIN_SOURCES} ${THEORY_SOURCES})
target_include_directories(ChordText PRIVATE Source)
target_compile_definitions(ChordText PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ChordText PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

juce_add_console_app(ProcessorTests PRODUCT_NAME "ProcessorTests")
target_sources(ProcessorTests PRIVATE
    Tests/ProcessorTest.cpp
    ${PLUGIN_SOURCES}
    ${THEORY_SOURCES})
target_include_directories(ProcessorTests PRIVATE Source)
target_compile_definitions(ProcessorTests PRIVATE
    "JucePlugin_Name=\"Chord Text\""
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(ProcessorTests PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(ProcessorTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/Tests")

add_executable(TheoryTests
    Tests/TheoryTest.cpp
    ${THEORY_SOURCES})
target_include_directories(TheoryTests PRIVATE Source)
target_compile_features(TheoryTests PRIVATE cxx_std_17)
set_target_properties(TheoryTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY                 "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG           "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE         "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL      "${CMAKE_BINARY_DIR}/Tests"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO  "${CMAKE_BINARY_DIR}/Tests")
