cmake_minimum_required(VERSION 3.22)

project(Resamper VERSION 0.2.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # clang-tidy in scripts/lint.sh reads build/compile_commands.json

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

# ------------------------------------------------------------------------------
# Pinned dependencies (git submodules, see README.md)
#   external/tracktion_engine  Tracktion Engine 3.5.0 (develop 964583ee: the 3.5.0
#                              release plus an EditSnapshot crash fix needed for Save)
#     -> modules/juce          JUCE 8.0.13-7-g37c894f8, as pinned by Tracktion
#   external/gin               GIN ea795541
#   external/melatonin_inspector  melatonin_inspector 9c483f86 (module 1.4.0)
# ------------------------------------------------------------------------------
set(TE_ADD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/tracktion_engine/modules/juce EXCLUDE_FROM_ALL)
add_subdirectory(external/tracktion_engine EXCLUDE_FROM_ALL)

# Only the GIN modules the code uses are registered (today: gin, for
# gin::FileSystemWatcher). Add a module here and to resamper_engine when
# code starts using it.
juce_add_module(external/gin/modules/gin)

# The Developer Mode component inspector.
juce_add_module(external/melatonin_inspector)

# ------------------------------------------------------------------------------
# resamper_engine: the only target that sees Tracktion headers.
# It compiles every JUCE/Tracktion/GIN module exactly once; all other targets
# link it and borrow its include paths with the Tracktion path filtered out,
# which turns "UI never includes Tracktion headers" into a compile error.
# ------------------------------------------------------------------------------
add_library(resamper_engine STATIC
    Source/Engine/EngineManager.cpp
    Source/Engine/ProjectManager.cpp
    Source/Engine/EngineUndo.cpp
    Source/Engine/EditTracks.cpp
    Source/Engine/ApplicationModel.cpp
    Source/Engine/ClipWaveform.cpp
    Source/Engine/Production.cpp
    Source/Engine/PluginRack.cpp
    Source/Engine/PluginHosting.cpp
    Source/Engine/PluginSandbox.cpp
    Source/Engine/PluginScanner.cpp
    Source/Engine/SandboxDock.cpp
    Source/Engine/NativeDevicePlugins.cpp
    Source/Engine/NativeDevices.cpp
    Source/Engine/Mixer.cpp
    Source/Engine/Session.cpp
    Source/Engine/Automation.cpp
    Source/Engine/Shaper.cpp
    Source/Engine/SamplePreview.cpp
    Source/Engine/Render.cpp)

target_compile_definitions(resamper_engine PUBLIC
    JUCE_STANDALONE_APPLICATION=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    # Phase 6 hosts VST3 everywhere and AU on macOS (brief §24).
    JUCE_PLUGINHOST_AU=$<IF:$<BOOL:$<PLATFORM_ID:Darwin>>,1,0>
    JUCE_PLUGINHOST_VST3=1
    # Tempo-tagged loops (e.g. ACID WAVs) are time-stretched to the Edit tempo;
    # without a stretcher their clips are silent and have no waveform.
    # Signalsmith Stretch ships with Tracktion (MIT, header-only).
    TRACKTION_ENABLE_TIMESTRETCH_SIGNALSMITH=1)

target_link_libraries(resamper_engine
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
        gin
        melatonin_inspector
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

target_include_directories(resamper_engine PUBLIC Source)
target_compile_definitions(resamper_engine PRIVATE RESAMPER_ENGINE_INTERNAL=1)
set_target_properties(resamper_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    target_compile_options(resamper_engine PRIVATE -fno-aligned-allocation)
    target_sources(resamper_engine PRIVATE Source/Engine/SandboxDock_mac.mm)
endif()

set(resamper_module_includes $<TARGET_PROPERTY:resamper_engine,INCLUDE_DIRECTORIES>)
set(resamper_module_definitions $<TARGET_PROPERTY:resamper_engine,COMPILE_DEFINITIONS>)
set(resamper_module_includes_without_tracktion
    $<FILTER:${resamper_module_includes},EXCLUDE,tracktion_engine/modules$>)

# Links a target against resamper_engine with JUCE/GIN headers visible and
# Tracktion headers hidden.
function(resamper_use_engine_without_tracktion target)
    target_include_directories(${target} PRIVATE ${resamper_module_includes_without_tracktion})
    target_compile_definitions(${target} PRIVATE ${resamper_module_definitions})
    target_link_libraries(${target} PRIVATE
        resamper_engine
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endfunction()

# ------------------------------------------------------------------------------
# UI resources: themes and fonts embedded for production builds.
# Debug builds read the same files from the source tree (dev-mode override).
# ------------------------------------------------------------------------------
juce_add_binary_data(resamper_resources
    HEADER_NAME ResamperResources.h
    NAMESPACE ResamperResources
    SOURCES
        UI/themes/dark.json
        UI/themes/light.json
        UI/fonts/Inter-Regular.ttf
        UI/fonts/Inter-SemiBold.ttf
        UI/fonts/Inter-Bold.ttf
        UI/fonts/IBMPlexMono-Regular.ttf
        UI/fonts/IBMPlexMono-Medium.ttf
        UI/fonts/IBMPlexMono-SemiBold.ttf
        CHANGELOG.md)

set_target_properties(resamper_resources PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# Commands + UI: compiled without Tracktion on the include path.
# ------------------------------------------------------------------------------
add_library(resamper_commands STATIC
    Source/Commands/CommandRegistry.cpp
    Source/Commands/AppCommands.cpp
    Source/Commands/ClipCommands.cpp
    Source/Commands/EditCommands.cpp
    Source/Commands/NoteCommands.cpp
    Source/Commands/ProjectCommands.cpp
    Source/Commands/TrackCommands.cpp
    Source/Commands/TransportCommands.cpp
    Source/Commands/ApplicationCommandTable.cpp
    Source/Commands/PluginCommands.cpp
    Source/Commands/MixerCommands.cpp
    Source/Commands/SessionCommands.cpp
    Source/Commands/AutomationCommands.cpp)
resamper_use_engine_without_tracktion(resamper_commands)

add_library(resamper_ui STATIC
    Source/UI/State/UIStateStore.cpp
    Source/UI/State/ArrangementViewState.cpp
    Source/UI/State/ShellState.cpp
    Source/UI/State/Preferences.cpp
    Source/UI/Theme/ThemeManager.cpp
    Source/UI/Theme/Interaction.cpp
    Source/UI/Controls/ValueFormat.cpp
    Source/UI/Controls/ContinuousValue.cpp
    Source/UI/Controls/ContinuousControl.cpp
    Source/UI/Controls/Controls.cpp
    Source/UI/Controls/Icons.cpp
    Source/UI/Controls/Menus.cpp
    Source/UI/MainWindow/Toasts.cpp
    Source/UI/Theme/UIFileSource.cpp
    Source/UI/Developer/DeveloperCommands.cpp
    Source/UI/Developer/DeveloperOverlay.cpp
    Source/UI/Developer/StatusBar.cpp
    Source/UI/Developer/ThemeWatcher.cpp
    Source/Commands/ProductionCommands.cpp
    Source/UI/Browser/Library.cpp
    Source/UI/Browser/Browser.cpp
    Source/UI/Detail/DeviceCard.cpp
    Source/UI/Detail/NativeDeviceCard.cpp
    Source/UI/Detail/DeviceBody.cpp
    Source/UI/Detail/EqEightDevice.cpp
    Source/UI/Detail/CompressorDevice.cpp
    Source/UI/Detail/PluginDeviceCard.cpp
    Source/UI/Detail/DetailView.cpp
    Source/UI/Plugins/FloatingDeviceWindow.cpp
    Source/UI/Plugins/NativeDeviceWindow.cpp
    Source/UI/Plugins/PluginWindow.cpp
    Source/UI/Plugins/PluginWindows.cpp
    Source/UI/Mixer/Metering.cpp
    Source/UI/Mixer/StripParts.cpp
    Source/UI/Mixer/InsertSlot.cpp
    Source/UI/Mixer/ChannelStrip.cpp
    Source/UI/Mixer/MasterStrip.cpp
    Source/UI/Mixer/MixerView.cpp
    Source/UI/Arrangement/ArrangementView.cpp
    Source/UI/Arrangement/TimelineHeader.cpp
    Source/UI/Arrangement/TrackList.cpp
    Source/UI/Arrangement/TrackHeader.cpp
    Source/UI/Arrangement/TrackLanes.cpp
    Source/UI/Arrangement/ClipComponent.cpp
    Source/UI/Arrangement/Playhead.cpp
    Source/UI/PianoRoll/NoteComponent.cpp
    Source/UI/PianoRoll/PianoKeyboard.cpp
    Source/UI/PianoRoll/BeatRuler.cpp
    Source/UI/PianoRoll/NoteGrid.cpp
    Source/UI/PianoRoll/VelocityEditor.cpp
    Source/UI/PianoRoll/PianoRollView.cpp
    Source/UI/MainWindow/TopBar.cpp
    Source/UI/MainWindow/AppUpdatePrompt.cpp
    Source/UI/MainWindow/MainComponent.cpp
    Source/UI/MainWindow/MainWindow.cpp
    # The headless app the window is built from; tests build it too.
    Source/App/ResamperApp.cpp
    Source/App/AppUpdate.cpp)
resamper_use_engine_without_tracktion(resamper_ui)
target_link_libraries(resamper_ui PUBLIC resamper_commands PRIVATE resamper_resources)
target_compile_definitions(resamper_ui PRIVATE
    $<$<CONFIG:Debug>:RESAMPER_DEV_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/UI">)

# ------------------------------------------------------------------------------
# The app
# ------------------------------------------------------------------------------
juce_add_gui_app(Resamper
    PRODUCT_NAME "Resamper"
    BUNDLE_ID "com.resamper.Resamper"
    COMPANY_NAME "Resamper"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Resamper records audio from your inputs onto armed tracks."
    # Release builds are signed with the hardened runtime (.github/workflows/release.yml):
    # it needs audio input to record, and plug-ins signed by other teams to load.
    HARDENED_RUNTIME_ENABLED TRUE
    HARDENED_RUNTIME_OPTIONS
        com.apple.security.device.audio-input
        com.apple.security.cs.disable-library-validation
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE)

target_sources(Resamper PRIVATE Source/App/Main.cpp)
target_compile_definitions(Resamper PRIVATE "RESAMPER_VERSION=\"${PROJECT_VERSION}\"")
resamper_use_engine_without_tracktion(Resamper)
target_link_libraries(Resamper PRIVATE resamper_ui)

# ------------------------------------------------------------------------------
# Headless tests over the Command registry / Application Model seam.
# ------------------------------------------------------------------------------
enable_testing()

juce_add_console_app(ResamperTests PRODUCT_NAME "ResamperTests")

target_sources(ResamperTests PRIVATE
    Tests/TestMain.cpp
    Tests/TestFixture.cpp
    Tests/TestPluginFormat.cpp
    Tests/EngineManagerTests.cpp
    Tests/ProjectTests.cpp
    Tests/TrackCommandTests.cpp
    Tests/TrackChannelTests.cpp
    Tests/TransportCommandTests.cpp
    Tests/ClipCommandTests.cpp
    Tests/ClipEditingTests.cpp
    Tests/RecordingTests.cpp
    Tests/RenderTests.cpp
    Tests/MidiTrackTests.cpp
    Tests/NoteEditingTests.cpp
    Tests/ProductionTests.cpp
    Tests/PluginRackTests.cpp
    Tests/PluginSandboxTests.cpp
    Tests/MixerTests.cpp
    Tests/SessionTests.cpp
    Tests/AutomationTests.cpp
    Tests/ShaperTests.cpp
    Tests/ThemeTests.cpp
    Tests/IconTests.cpp
    Tests/ControlTests.cpp
    Tests/ControlViewTests.cpp
    Tests/DeviceChainTests.cpp
    Tests/DeviceCardTests.cpp
    Tests/PluginWindowTests.cpp
    Tests/NativeDeviceTests.cpp
    Tests/DeviceMeasurementTests.cpp
    Tests/EngineUndoTests.cpp
    Tests/ShellTests.cpp
    Tests/BrowserTests.cpp
    Tests/ClipOperationTests.cpp
    Tests/ShortcutTests.cpp
    Tests/AppTests.cpp
    Tests/AppUpdateTests.cpp
    Tests/WaveformTests.cpp
    Tests/ClipViewTests.cpp
    Tests/AudioImportTests.cpp
    Tests/Snapshots.cpp)

# Tests may reach into the engine to verify rendered audio, so they get the
# full include path, Tracktion included.
target_include_directories(ResamperTests PRIVATE ${resamper_module_includes} Tests)
target_compile_definitions(ResamperTests PRIVATE
    ${resamper_module_definitions}
    RESAMPER_ENGINE_INTERNAL=1)
target_link_libraries(ResamperTests PRIVATE
    resamper_ui
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

add_test(NAME ResamperTests COMMAND ResamperTests)

