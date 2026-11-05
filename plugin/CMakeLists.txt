# JUCE VST3 plugin shell. All music logic lives in chordcore; this directory only adapts it to JUCE.

include(FetchContent)

# Pinned JUCE tag. JUCE is GPLv3 / commercially licensed: see README ("Licensing").
set(HC_JUCE_TAG "8.0.17-security" CACHE STRING "JUCE git tag to fetch")
set(HC_JUCE_SOURCE_DIR "" CACHE PATH "Use an existing JUCE checkout instead of fetching one")

if(HC_JUCE_SOURCE_DIR)
    add_subdirectory(${HC_JUCE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/_juce EXCLUDE_FROM_ALL)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ${HC_JUCE_TAG}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ChordSketch
    COMPANY_NAME "ChordSketch"
    PRODUCT_NAME "ChordSketch"
    PLUGIN_NAME "ChordSketch"
    DESCRIPTION "Chord progression sequencer with MIDI export"
    PLUGIN_MANUFACTURER_CODE Chsk
    PLUGIN_CODE Chs1
    FORMATS VST3
    VST3_CATEGORIES Instrument Synth
    # Ableton Live has no slot for third-party VST3 MIDI effects, so the plugin is declared as an
    # instrument that produces MIDI (route its output with another track's "MIDI From"). To be confirmed in Live.
    IS_SYNTH TRUE
    IS_MIDI_EFFECT FALSE
    # Live refuses an instrument that has no MIDI (event) input bus ("plugin has instrument category,
    # but no valid event input bus"), so the input bus must exist even though M1 does not use it.
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(ChordSketch PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/GridComponent.cpp
    Source/GridComponent.h
    Source/UiKit.h)

target_compile_definitions(ChordSketch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ChordSketch
    PRIVATE
        chordcore
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
