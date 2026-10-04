#pragma once

#include "Commands/AppCommandHost.h"
#include "Commands/CommandRegistry.h"
#include "UI/Arrangement/ArrangementView.h"
#include "UI/Mixer/MixerView.h"
#include "UI/PianoRoll/PianoRollView.h"
#include "UI/Detail/DetailView.h"
#include "UI/Browser/Browser.h"
#include "UI/Plugins/PluginWindows.h"
#include "UI/Developer/DeveloperOverlay.h"
#include "UI/Developer/StatusBar.h"
#include "UI/Developer/ThemeWatcher.h"
#include "UI/State/ShellState.h"
#include "AppUpdatePrompt.h"
#include "Toasts.h"
#include "TopBar.h"

namespace melatonin
{
    class Inspector;
}

namespace resamper
{

struct ResamperApp;
class UIFileSource;

/** The Commands of the window's views: they act on the view in front. */
namespace cmd
{
    inline constexpr CommandRef<> uiEscape { "ui.escape" };
    inline constexpr CommandRef<> devToggleOverlay { "dev.toggleOverlay" };
    inline constexpr CommandRef<> arrangeZoomIn { "arrange.zoomIn" };
    inline constexpr CommandRef<> arrangeZoomOut { "arrange.zoomOut" };
    inline constexpr CommandRef<> arrangeZoomToSelection { "arrange.zoomToSelection" };
    inline constexpr CommandRef<> arrangeZoomToSong { "arrange.zoomToSong" };
    inline constexpr CommandRef<> pianoRollQuantize { "pianoRoll.quantize" };
    inline constexpr CommandRef<int> pianoRollTranspose { "pianoRoll.transpose" };   ///< semitones
    inline constexpr CommandRef<> pianoRollSelectAll { "pianoRoll.selectAll" };
    inline constexpr CommandRef<> pluginWindowToggleAll { "pluginWindow.toggleAll" };           ///< Mod+Alt+P
    inline constexpr CommandRef<> pluginWindowCloseFocused { "pluginWindow.closeFocused" };     ///< Mod+W
    inline constexpr CommandRef<> pluginWindowToggleAutoOpen { "pluginWindow.toggleAutoOpen" }; ///< a preference
    inline constexpr CommandRef<> pluginWindowToggleSelectedTrackOnly { "pluginWindow.toggleSelectedTrackOnly" }; ///< a preference
    inline constexpr CommandRef<> uiLanguageSystem { "ui.language.system" };     ///< the UI Language: a preference
    inline constexpr CommandRef<> uiLanguageEnglish { "ui.language.en" };        ///< "en"
    inline constexpr CommandRef<> uiLanguageTurkish { "ui.language.tr" };        ///< "tr"
}

/** The MainWindow's content (PRD §5–6): the top bar, then the view the shell
    shows. Session and Arrange sit between the Browser (left) and the detail
    view (bottom); Mixer, Piano Roll and Editor fill the window. Views are kept
    alive while hidden, so each keeps its scroll and zoom. In Developer Mode,
    dev.toggleOverlay shows a status bar and the developer overlay at the
    bottom and opens the component inspector (melatonin_inspector) in its own
    window; they are off by default, as the design has none of them. Also the
    ApplicationCommandTarget that routes menus and keyboard shortcuts into the
    Command registry. */
class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::DragAndDropContainer,
                      private ApplicationModel::Listener,
                      private ThemeManager::Listener,
                      private juce::ValueTree::Listener
{
public:
    /** Builds the views over the app and registers their Commands in its registry. */
    MainComponent (ResamperApp&, juce::ApplicationCommandManager&);

    /** Resamper's keyboard shortcuts, the one place a key becomes a Command:
        first those JUCE's key mappings can't dispatch (one view's, fired only
        while it shows, and those that pass an argument), then the menus' key
        mappings. Every window's keys end here: the main window's, and those a
        plug-in window (or its plug-in's own UI) didn't use. */
    juce::KeyListener& getShortcutListener() noexcept   { return shortcuts; }

    /** A toast at the bottom centre (PRD §16.7). undoable offers Undo (edit.undo). */
    void showToast (const juce::String& message, bool undoable, bool isError = false);

    /** The update button and the completion dialog follow this check. */
    void bindUpdateCheck (UpdateCheck&);

    /** The first launch of a version opens the completion dialog. `then` runs
        when it closes or when there is nothing to show. Restart runs onRelaunch. */
    void presentLaunchNotes (const juce::String& current, const std::function<void()>& then);
    ~MainComponent() override;

    /** Quits and opens the app again: the completion dialog's Restart, and
        Relaunch now after a UI Language change. */
    std::function<void()> onRelaunch;

    /** The plug-in windows and their rules (PRD §9.6). */
    PluginWindows& getPluginWindows() noexcept   { return pluginWindows; }

    void paint (juce::Graphics&) override;
    void resized() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override   { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

private:
    /** The engine facades the window may read. A change to the Edit goes
        through commands; these references cannot call a setter. */
    const ApplicationModel& model;
    const Mixer& mixer;
    const PluginRack& plugins;
    const PluginHosting& hosting;
    SamplePreview& preview;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    UIStateStore& uiState;
    Preferences& preferences;
    AppCommandHost& host;

    const UIFileSource& uiFiles;
    const juce::String audioDeviceDescription;
    juce::ApplicationCommandManager& commandManager;

    ShellState shell;
    StatusBar statusBar;
    TopBar topBar;
    ArrangementView arrangement;
    PianoRollView pianoRoll;
    Browser browser;
    DetailView detailView;
    MixerView mixerView;

    /** A view that isn't built yet, or has nothing to show. */
    struct Placeholder : juce::Component
    {
        Placeholder (ThemeManager& tm, juce::String t) : themeManager (tm), text (std::move (t)) {}
        void paint (juce::Graphics&) override;
        ThemeManager& themeManager;
        juce::String text;
    };

    Placeholder sessionPlaceholder, editorPlaceholder, pianoRollPlaceholder;
    DeveloperOverlay developerOverlay;
    Toasts toasts;
    AppUpdatePrompt updatePrompt;

    struct ShortcutListener : juce::KeyListener
    {
        explicit ShortcutListener (MainComponent& o) : owner (o) {}
        bool keyPressed (const juce::KeyPress&, juce::Component*) override;
        bool keyStateChanged (bool isKeyDown, juce::Component*) override;

        /** A view's shortcut, or one with an argument; false if the key is the menus' or nobody's. */
        bool viewShortcut (const juce::KeyPress&);

        MainComponent& owner;
    };

    ShortcutListener shortcuts { *this };
    int currentShortcutContext() const;
    void registerPianoRollCommands();
    // Its own window, so a tip over a plug-in window's chrome stays above that plug-in's sandboxed UI.
    juce::TooltipWindow tooltips { nullptr, 600 };
    std::unique_ptr<ThemeWatcher> themeWatch;
    PluginWindows pluginWindows;
    std::unique_ptr<melatonin::Inspector> inspector;

    void updateStatusBar();
    void registerPluginWindowCommands();
    void registerLanguageCommands();
    void chooseLanguage (const juce::String& preference);
    void registerArrangementZoomCommands();
    void registerEscapeCommand();
    void registerDeveloperOverlayCommand();
    void toggleDeveloperOverlay();
    void setInspectorOpen (bool);
    void showMenu (const juce::String& name, juce::Rectangle<int> screenArea);
    void openPianoRollForSelection();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    void modelChanged() override;
    void themeChanged() override;
};

} // namespace resamper
