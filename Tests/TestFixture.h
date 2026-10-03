#pragma once

#include "App/ResamperApp.h"
#include "Commands/AutomationCommands.h"
#include "Commands/MixerCommands.h"
#include "Commands/PluginCommands.h"
#include "Commands/ProductionCommands.h"
#include "Commands/SessionCommands.h"
#include "UI/Theme/UIFileSource.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper::test
{

/** The run's single headless EngineManager (no audio device). */
EngineManager& getEngineManager();

/** Writes a sine-wave WAV file and returns it. A non-zero acidTempo adds an
    ACID loop chunk, as tempo-tagged sample-library loops carry. A non-zero
    toneSeconds makes only the first toneSeconds a tone, silence after. */
juce::File writeSineWav (const juce::File& file, double seconds, int numChannels = 2, double acidTempo = 0,
                         double toneSeconds = 0);

/** Writes a 16-bit sine-wave FLAC file and returns it: a compressed file, as
    a sample library's might be. */
juce::File writeSineFlac (const juce::File& file, double seconds, int numChannels = 2);

/** A fresh untitled Project in the app exactly as the app builds it, every
    Command registered, except that file choosers and messages are plain
    fields. The Theme is not loaded: call theme.load() before building views. */
struct Fixture
{
    Fixture();
    ~Fixture();

    UIFileSource uiFiles;
    ThemeManager theme { uiFiles, "themes/dark.json" };
    ResamperApp app { getEngineManager(), theme };

    // The app's parts, by their short names.
    ProjectManager& projects = app.projects;
    ApplicationModel& model = app.model;
    Production& production = app.production;
    PluginRack& plugins = app.plugins;
    Mixer& mixer = app.mixer;
    Session& session = app.session;
    Automation& automation = app.automation;
    Shaper& shaper = app.shaper;
    SamplePreview& preview = app.preview;
    CommandRegistry& commands = app.commands;
    AppCommandHost& host = app.host;

    // What the choosers "pick". An invalid File means the user cancelled.
    juce::File audioFileToChoose, projectToOpen, projectSaveLocation, pluginFileToChoose;

    juce::StringArray errors, notifications;

    juce::TemporaryFile scratch { juce::String() };
    juce::File scratchDir() const   { return scratch.getFile(); }

    template <typename Args>
    bool invoke (CommandRef<Args> command, std::type_identity_t<Args> args)   { return commands.invoke (command, std::move (args)); }

    template <typename Args>
    bool invoke (CommandRef<Args> command)                                   { return commands.invoke (command); }

    int numTracks() const                 { return (int) model.getTracks().size(); }
};

/** Renders the whole Edit offline, as it plays (honouring mute and solo), and
    returns the peak level (0 if nothing rendered). */
float renderPeak (Fixture&);

/** A left-button mouse event on c at p, for a gesture that went down at downAt. */
inline juce::MouseEvent mouseEvent (juce::Component& c, juce::Point<int> p, juce::Point<int> downAt, bool dragged)
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(),
             juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
             juce::MouseInputSource::defaultTiltY, &c, &c, now, downAt.toFloat(), now, 1, dragged };
}

/** Dispatches messages until done() or about a minute has passed; returns done(). */
template <typename Predicate>
bool dispatchUntil (Predicate done)
{
    for (int i = 0; i < 6000 && ! done(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

    return done();
}

} // namespace resamper::test
