# Emulon X — a free, open-source sampler inspired by the E-mu Emulator X.
# Licensed under the GNU AGPLv3 (see LICENSE).

cmake_minimum_required(VERSION 3.22)
project(EmulonX VERSION 1.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ship a self-contained plugin on Windows (no Visual C++ redistributable needed).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- JUCE ---------------------------------------------------------------------
# By default JUCE is downloaded automatically. Point EMX_JUCE_DIR at a local
# checkout to build offline.
set(EMX_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(EMX_JUCE_DIR)
    add_subdirectory(${EMX_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- The plugin ---------------------------------------------------------------
set(EMX_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND EMX_FORMATS AU)
endif()

juce_add_plugin(EmulonX
    COMPANY_NAME                "Emulon"
    PRODUCT_NAME                "Emulon X"
    BUNDLE_ID                   "org.emulon.emulonx"
    PLUGIN_MANUFACTURER_CODE    Emln
    PLUGIN_CODE                 EmlX
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Sampler
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/installer/icon-256.png"
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    FORMATS                     ${EMX_FORMATS})

juce_add_binary_data(EmulonXAssets
    HEADER_NAME EmxAssets.h
    NAMESPACE   EmxAssets
    SOURCES
        Assets/images/background.jpg
        Assets/images/frame.png
        Assets/images/knob.png
        Assets/images/slider.png
        Assets/images/logo.png
        Assets/fonts/Saira-500.ttf
        Assets/fonts/Saira-700.ttf
        Assets/fonts/Saira-900.ttf)

set(EMX_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Settings.cpp
    Source/ProcessorFx.cpp
    Source/fx/FxDefs.cpp
    Source/fx/FxDsp.cpp
    Source/fx/FxModel.cpp
    Source/fx/FxEngine.cpp
    Source/engine/Zone.cpp
    Source/engine/Interpolator.cpp
    Source/engine/SamplerEngine.cpp
    Source/engine/SampleLoader.cpp
    Source/engine/Arpeggiator.cpp
    Source/ui/PresetPanel.cpp
    Source/ui/BankPanel.cpp
    Source/ui/Explorer.cpp
    Source/ui/SettingsPanel.cpp
    Source/ui/EditorPage.cpp
    Source/ui/FxPage.cpp
    Source/ui/ArpPage.cpp
    Source/ui/Theme.cpp
    Source/ui/EmxLookAndFeel.cpp
    Source/ui/Widgets.cpp
    Source/ui/MasterBar.cpp
    Source/ui/MainPage.cpp)

set(EMX_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_XINPUT=0
    EMX_VERSION_STRING="${PROJECT_VERSION}")

target_sources(EmulonX PRIVATE ${EMX_SOURCES})
target_include_directories(EmulonX PRIVATE Source)
target_compile_definitions(EmulonX PUBLIC ${EMX_DEFINITIONS})

target_link_libraries(EmulonX
    PRIVATE
        EmulonXAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Developer tool: render the UI to PNG files (off by default) -------------
option(EMX_BUILD_PREVIEW "Build the EmulonXPreview screenshot tool" OFF)
if(EMX_BUILD_PREVIEW)
    juce_add_console_app(EmulonXPreview PRODUCT_NAME "EmulonXPreview")
    target_sources(EmulonXPreview PRIVATE tools/preview/Preview.cpp ${EMX_SOURCES})
    target_include_directories(EmulonXPreview PRIVATE Source)
    target_compile_definitions(EmulonXPreview PRIVATE ${EMX_DEFINITIONS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EmulonXPreview PRIVATE
        EmulonXAssets juce::juce_gui_extra juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# --- Engine tests (no GUI needed; run in CI) ---------------------------------
option(EMX_BUILD_TESTS "Build the EmulonXTests engine test program" OFF)
if(EMX_BUILD_TESTS)
    juce_add_console_app(EmulonXTests PRODUCT_NAME "EmulonXTests")
    target_sources(EmulonXTests PRIVATE
        tools/tests/EngineTests.cpp
        Source/engine/Zone.cpp
        Source/engine/Interpolator.cpp
        Source/engine/SamplerEngine.cpp
        Source/engine/SampleLoader.cpp
        Source/engine/Arpeggiator.cpp
        Source/fx/FxDefs.cpp
        Source/fx/FxDsp.cpp
        Source/fx/FxModel.cpp
        Source/fx/FxEngine.cpp)
    target_include_directories(EmulonXTests PRIVATE Source)
    target_compile_definitions(EmulonXTests PRIVATE EMX_VERSION_STRING="${PROJECT_VERSION}" JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(EmulonXTests PRIVATE
        juce::juce_audio_formats juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
