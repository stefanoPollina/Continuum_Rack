cmake_minimum_required(VERSION 3.24)
project(ContinuumRack VERSION 0.2.0 LANGUAGES C CXX)
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build the Windows x64 target (-A x64).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(RACK_BUILD_PLUGIN "Build the JUCE plugin and processor integration tests" ON)
option(RACK_BUILD_STANDALONE "Also build the standalone app" ON)
set(RACK_JUCE_PATH "" CACHE PATH "Optional local JUCE 8.0.12 source directory")
# The web rack view needs a WebView: WebView2 on Windows (the build script fetches its SDK
# package), WKWebView on macOS, WebKitGTK 4.1 dev packages on Linux.
option(RACK_WEB_UI "Include the web rack view (the Continuum Rack page in a WebView)" ON)
set(RACK_WEBVIEW2_PACKAGE_DIR "" CACHE PATH "Folder holding the Microsoft.Web.WebView2 NuGet package (Windows)")

include(CTest)

# ── JUCE-independent audio core ─────────────────────────────────────────────────────
add_library(RackDSP STATIC Source/RackModel.cpp Source/Wavetables.cpp Source/RackEngine.cpp)
target_include_directories(RackDSP PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
set_target_properties(RackDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(RackDSP PRIVATE /W4 /utf-8 /fp:fast)
else()
    target_compile_options(RackDSP PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(RackDSPTests Tests/DSPTests.cpp Tests/AllocationWatch.cpp)
    target_link_libraries(RackDSPTests PRIVATE RackDSP)
    if(MSVC)
        target_compile_options(RackDSPTests PRIVATE /utf-8)
    endif()
    # The Mosaic table checks read the compressed bank through zlib where it is installed
    # (Linux/macOS). Without it they are skipped here; the JUCE processor tests cover the bank.
    find_package(ZLIB QUIET)
    if(ZLIB_FOUND)
        target_link_libraries(RackDSPTests PRIVATE ZLIB::ZLIB)
        target_compile_definitions(RackDSPTests PRIVATE RACK_HAVE_ZLIB=1)
    endif()
    add_test(NAME Rack.DSP COMMAND RackDSPTests "${CMAKE_CURRENT_SOURCE_DIR}/Resources/wavetables.bin.gz")
endif()

if(NOT RACK_BUILD_PLUGIN)
    return()
endif()

# ── JUCE plugin ────────────────────────────────────────────────────────────────────
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
if(RACK_JUCE_PATH)
    if(NOT EXISTS "${RACK_JUCE_PATH}/CMakeLists.txt")
        message(FATAL_ERROR "RACK_JUCE_PATH must be a JUCE source directory.")
    endif()
    add_subdirectory("${RACK_JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce")
else()
    include(FetchContent)
    # JUCE 8.0.12, the same immutable commit the Mosaic Matrix FX build pins.
    FetchContent_Declare(JUCE
        URL https://github.com/juce-framework/JUCE/archive/29396c22c93392d6738e021b83196283d6e4d850.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(WIN32 AND RACK_WEB_UI AND RACK_WEBVIEW2_PACKAGE_DIR)
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${RACK_WEBVIEW2_PACKAGE_DIR}" CACHE PATH "" FORCE)
endif()
set(RACK_WEB_ARGS)
if(RACK_WEB_UI)
    if(WIN32)
        set(RACK_WEB_ARGS NEEDS_WEBVIEW2 TRUE)
    elseif(UNIX AND NOT APPLE)
        set(RACK_WEB_ARGS NEEDS_WEB_BROWSER TRUE)
    endif()
endif()

set(RACK_FORMATS VST3)
if(APPLE)
    list(APPEND RACK_FORMATS AU)
endif()
if(RACK_BUILD_STANDALONE)
    list(APPEND RACK_FORMATS Standalone)
endif()

juce_add_plugin(ContinuumRack
    COMPANY_NAME "Continuum"
    BUNDLE_ID "com.continuum.rack"
    PLUGIN_MANUFACTURER_CODE Ctum
    PLUGIN_CODE Crak
    FORMATS ${RACK_FORMATS}
    PRODUCT_NAME "Continuum Rack"
    DESCRIPTION "8 banks x 8 engines modular rack synthesizer with a 64-step sequencer per bank"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    ${RACK_WEB_ARGS})

juce_add_binary_data(RackResources SOURCES
    Resources/scenes.json.gz
    Resources/wavetables.bin.gz
    Resources/engine-patches.json
    Resources/web/index.html.gz
    Resources/web/plugin-bridge.js)
set_target_properties(RackResources PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(rack_configure target)
    target_sources(${target} PRIVATE
        Source/RackPatch.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Source/WebBridge.cpp Source/WebRackView.cpp Source/NativeRack.cpp)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    if(RACK_WEB_UI)
        target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=1)
        if(WIN32)
            target_compile_definitions(${target} PRIVATE JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
        endif()
    else()
        target_compile_definitions(${target} PRIVATE JUCE_WEB_BROWSER=0)
    endif()
    target_link_libraries(${target} PRIVATE juce::juce_gui_extra)
    target_compile_definitions(${target} PRIVATE
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target} PRIVATE
        RackDSP RackResources
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
    if(RACK_BUILD_STANDALONE)
        target_link_libraries(${target} PRIVATE juce::juce_audio_utils)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()
rack_configure(ContinuumRack)

if(BUILD_TESTING)
    juce_add_console_app(RackProcessorTests PRODUCT_NAME "Continuum Rack Processor Tests" ${RACK_WEB_ARGS})
    rack_configure(RackProcessorTests)
    target_sources(RackProcessorTests PRIVATE Tests/ProcessorTests.cpp)
    target_compile_definitions(RackProcessorTests PRIVATE JucePlugin_Name="Continuum Rack"
        RACK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures")
    add_test(NAME Rack.Processor COMMAND RackProcessorTests)
endif()

# Optional: load the built VST3 through JUCE's hosting code, as a DAW would.
option(RACK_BUILD_HOST_CHECK "Build a small VST3 host that loads the built plugin" OFF)
if(RACK_BUILD_HOST_CHECK)
    juce_add_console_app(RackHostLoadCheck PRODUCT_NAME "Continuum Rack Host Check")
    target_sources(RackHostLoadCheck PRIVATE Tests/HostLoadCheck.cpp)
    target_compile_definitions(RackHostLoadCheck PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(RackHostLoadCheck PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    add_dependencies(RackHostLoadCheck ContinuumRack_VST3)
    if(BUILD_TESTING)
        add_test(NAME Rack.VST3Host COMMAND RackHostLoadCheck
            "$<TARGET_FILE_DIR:ContinuumRack_VST3>/../../../Continuum Rack.vst3")
    endif()
endif()
