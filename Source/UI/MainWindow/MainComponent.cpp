#include "MainComponent.h"
#include "Commands/AppCommands.h"
#include "Commands/ApplicationCommandTable.h"
#include "UI/Developer/DeveloperCommands.h"
#include "UI/Theme/UIFileSource.h"
#include "UI/State/UIStateStore.h"

#include <melatonin_inspector/melatonin_inspector.h>

namespace resamper
{

MainComponent::MainComponent (ResamperApp& a, juce::ApplicationCommandManager& cm)
    : app (a),
      uiFiles (app.theme.getUIFileSource()),
      audioDeviceDescription (app.engine.describeActiveAudioDevice()),
      commandManager (cm),
      shell (app.uiState.getState ("shell")),
      statusBar (app.theme),
      topBar (app.model, app.commands, app.theme, shell),
      arrangement (app.model, app.commands, app.theme, app.uiState, shell),
      pianoRoll (app.model, app.commands, app.theme, app.uiState),
      browser (app.commands, app.plugins, app.model, app.theme, app.preview,
               Library::defaultRoot()),
      detailView (app.model, app.plugins, app.engine.getPluginHosting(), app.commands, app.theme, shell,
                  app.uiState.getState ("detail")),
      mixerView (app.model, app.mixer, app.plugins, app.engine.getPluginHosting(), app.commands, app.theme,
                 app.uiState.getState ("mixer")),
      sessionPlaceholder (app.theme, "The Session view arrives with M5."),
      editorPlaceholder (app.theme, "The audio Editor arrives with M4. Double-click an audio clip then."),
      pianoRollPlaceholder (app.theme, "Select a MIDI clip, or double-click one, to edit its notes."),
      developerOverlay (app.theme),
      toasts (app.theme),
      pluginWindows (app.model, app.plugins, app.engine.getPluginHosting(), app.commands, app.theme, app.preferences)
{
    // Looked up per call: the platform may fill in host.reportError after the app is built.
    auto reportError = [this] (const juce::String& message) { app.host.reportError (message); };
    registerDeveloperCommands (app.commands, app.theme, reportError);
    registerShellCommands (app.commands, shell);
    registerArrangementZoomCommands();
    registerEscapeCommand();
    registerPianoRollCommands();
    registerPluginWindowCommands();

    if (uiFiles.isDevMode())
        registerDeveloperOverlayCommand();

    topBar.onMenu = [this] (const juce::String& name, juce::Rectangle<int> area) { showMenu (name, area); };

    arrangement.onMidiClipOpened = [this] (const juce::String& id)
    {
        pianoRoll.openClip (id);
        shell.setView (ShellState::View::pianoRoll);
    };

    // The mixer's Track chain row: back to the timeline, the track selected, its chain in view.
    mixerView.onShowDeviceChain = [this] (const juce::String& trackId)
    {
        app.model.selectTrack (trackId);
        shell.setDetailCollapsed (false);
        shell.setView (shell.getLastTimelineView());
        detailView.revealDeviceChain();
    };

    arrangement.onAudioClipOpened = [this] (const juce::String&) { shell.setView (ShellState::View::editor); };

    // Closing the Piano Roll returns to the timeline it was opened from.
    pianoRoll.onOpenStateChanged = [this]
    {
        if (! pianoRoll.isOpen() && shell.getView() == ShellState::View::pianoRoll)
            shell.setView (shell.getLastTimelineView());

        resized();
    };

    // A device from the Browser: a native one takes focus; a plug-in's window
    // opens by the opening rule, through pluginAdded below (§6.2, §9.6).
    arrangement.onDeviceDropped = browser.onInsertDevice = [this] (const juce::String& trackId, const juce::String& path)
    {
        detailView.insertDevice (trackId, path);
    };

    // A card or an insert slot opens its device's window, or brings it forward: a plug-in's
    // window, or a native device floating expanded, under the same window rules (#70).
    detailView.onOpenEditor = mixerView.onOpenPlugin = [this] (const juce::String& id) { pluginWindows.open (id); };

    pluginWindows.onOpenWindowsChanged = [this] { detailView.setOpenWindows (pluginWindows.getOpenPluginIds()); };
    pluginWindows.showToast = [this] (const juce::String& message, std::vector<Toasts::Action> actions)
    {
        toasts.show (message, std::move (actions));
    };

    // A key a plug-in window didn't use is Resamper's, as in the main window.
    pluginWindows.onShortcut = [this] (const juce::KeyPress& key) { return shortcuts.keyPressed (key, this); };

    // A first window centres over the arrangement (the view in front, while that isn't it).
    pluginWindows.getAnchorArea = [this]
    {
        for (auto* view : std::initializer_list<juce::Component*> { &arrangement, &sessionPlaceholder, &mixerView, &pianoRoll,
                                                                    &pianoRollPlaceholder, &editorPlaceholder })
            if (view->isShowing())
                return view->getScreenBounds();

        return isShowing() ? getScreenBounds() : juce::Rectangle<int>();
    };

    // The opening rule (§9.6) applies to every insert, whichever view it came from.
    app.host.pluginAdded = [this] (const juce::String& trackId, const juce::String& pluginId)
    {
        pluginWindows.pluginAdded (trackId, pluginId);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &browser, &detailView,
                                                             &arrangement, &sessionPlaceholder, &pianoRoll, &mixerView,
                                                             &editorPlaceholder, &pianoRollPlaceholder,
                                                             &developerOverlay, &statusBar })
        addChildComponent (c);

    addAndMakeVisible (toasts);

    topBar.setVisible (true);

    if (auto dir = uiFiles.getDevDirectory(); dir != juce::File())
    {
        themeWatch = std::make_unique<ThemeWatcher> (dir.getChildFile ("themes"));
        themeWatch->onJsonUpdated = [this] (const juce::File&) { app.commands.invoke (cmd::devReloadTheme); };
    }

    app.model.addListener (this);
    app.theme.addListener (this);
    shell.getState().addListener (this);
    themeChanged();
    pluginWindows.refresh();   // a project's open windows come back
}

MainComponent::~MainComponent()
{
    setInspectorOpen (false);
    app.host.pluginAdded = nullptr;
    shell.getState().removeListener (this);
    app.theme.removeListener (this);
    app.model.removeListener (this);
}

void MainComponent::registerArrangementZoomCommands()
{
    // Zoom is for the Arrangement, so these only act while it shows.
    auto add = [this] (CommandRef<> ref, const char* name, void (ArrangementView::*fn)())
    {
        app.commands.add (ref, { name }, [this, fn]
        {
            if (arrangement.isShowing())
                (arrangement.*fn)();
        });
    };

    add (cmd::arrangeZoomIn, "Zoom In", &ArrangementView::zoomIn);
    add (cmd::arrangeZoomOut, "Zoom Out", &ArrangementView::zoomOut);
    add (cmd::arrangeZoomToSelection, "Zoom to Selection", &ArrangementView::zoomToSelection);
    add (cmd::arrangeZoomToSong, "Zoom to Song", &ArrangementView::zoomToSong);
}

void MainComponent::registerEscapeCommand()
{
    // Esc (PRD §16.1): closes popovers and menus, cancels a drag, clears the selection.
    app.commands.add (cmd::uiEscape, { "Clear Selection" }, [this]
    {
        juce::PopupMenu::dismissAllActiveMenus();
        arrangement.cancelDrag();
        app.commands.invoke (cmd::editDeselectAll);
    });
}

void MainComponent::registerPluginWindowCommands()
{
    auto& prefs = app.preferences;

    app.commands.add (cmd::pluginWindowToggleAll, { "Show / Hide Plug-in Windows" }, [this] { pluginWindows.toggleAll(); });
    app.commands.add (cmd::pluginWindowCloseFocused, { "Close Plug-in Window" }, [this] { pluginWindows.closeFocused(); });

    app.commands.add (cmd::pluginWindowToggleAutoOpen,
                      { "Auto-open Plug-in Window on Insert", {}, [&prefs] { return prefs.getAutoOpenPluginWindows(); } },
                      [&prefs] { prefs.setAutoOpenPluginWindows (! prefs.getAutoOpenPluginWindows()); });

    app.commands.add (cmd::pluginWindowToggleSelectedTrackOnly,
                      { "Show Plug-in Windows for Selected Track Only", {},
                        [&prefs] { return prefs.getPluginWindowsForSelectedTrackOnly(); } },
                      [&prefs] { prefs.setPluginWindowsForSelectedTrackOnly (! prefs.getPluginWindowsForSelectedTrackOnly()); });
}

void MainComponent::registerDeveloperOverlayCommand()
{
    app.commands.add (cmd::devToggleOverlay, { "Developer Overlay" }, [this] { toggleDeveloperOverlay(); });
}

void MainComponent::toggleDeveloperOverlay()
{
    // The status bar and inspector are not part of the design, so they stay hidden until asked for.
    const auto show = ! developerOverlay.isVisible();
    developerOverlay.setVisible (show);
    statusBar.setVisible (show);
    setInspectorOpen (show);
    updateStatusBar();
    resized();
}

void MainComponent::setInspectorOpen (bool open)
{
    if (open == (inspector != nullptr))
        return;

    if (! open)
    {
        removeKeyListener (&shortcuts);
        inspector.reset();
        setWantsKeyboardFocus (false);
        return;
    }

    inspector = std::make_unique<melatonin::Inspector> (*this);

    // JUCE asks the newest key listener first, and the inspector's takes Cmd+I and Escape.
    // Ours goes after it, so Resamper's shortcuts still win while the inspector is open.
    addKeyListener (&shortcuts);

    // Deferred: the inspector calls this from its own close button.
    inspector->onClose = [safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
                safe->setInspectorOpen (false);
        });
    };
}

void MainComponent::showToast (const juce::String& message, bool undoable, bool isError)
{
    toasts.show (message, undoable ? std::function<void()> ([this] { app.commands.invoke (cmd::editUndo); })
                                   : std::function<void()>(),
                 isError);
}

void MainComponent::Placeholder::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.bgDeep);
    drawStyledText (g, themeManager, text, theme.body, getLocalBounds(), juce::Justification::centred, theme.textDim);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (app.theme.getTheme().bgDeep);
}

void MainComponent::resized()
{
    using View = ShellState::View;
    auto& metrics = app.theme.getMetrics();
    auto r = getLocalBounds();
    toasts.followHost();
    topBar.setBounds (r.removeFromTop (metrics.topBarHeight));

    if (statusBar.isVisible())
        statusBar.setBounds (r.removeFromBottom (metrics.statusBarHeight));

    if (developerOverlay.isVisible())
        developerOverlay.setBounds (r.removeFromBottom (metrics.trackControlHeight));

    const auto view = shell.getView();
    const auto timeline = view == View::session || view == View::arrange;
    const auto pianoRollOpen = pianoRoll.isOpen();

    // The Browser and the detail view belong to Session and Arrange (§6.2, §6.3).
    const auto showBrowser = timeline && shell.isBrowserVisible();
    const auto showDetail = timeline && ! shell.isDetailCollapsed();

    browser.setVisible (showBrowser);
    detailView.setVisible (showDetail);
    sessionPlaceholder.setVisible (view == View::session);
    arrangement.setVisible (view == View::arrange);
    mixerView.setVisible (view == View::mixer);
    pianoRoll.setVisible (view == View::pianoRoll && pianoRollOpen);
    pianoRollPlaceholder.setVisible (view == View::pianoRoll && ! pianoRollOpen);
    editorPlaceholder.setVisible (view == View::editor);

    if (showDetail)
    {
        detailView.setBounds (r.removeFromBottom (shell.getDetailHeight()));
    }

    if (showBrowser)
        browser.setBounds (r.removeFromLeft (metrics.browserWidth));

    for (auto* c : std::initializer_list<juce::Component*> { &sessionPlaceholder, &arrangement, &mixerView, &pianoRoll,
                                                             &pianoRollPlaceholder, &editorPlaceholder })
        c->setBounds (r);
}

void MainComponent::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    if (shell.getView() == ShellState::View::pianoRoll && ! pianoRoll.isOpen())
        openPianoRollForSelection();

    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::openPianoRollForSelection()
{
    const auto clipId = app.model.getSelectedClipId();

    if (auto clip = app.model.getClip (clipId); clip && clip->kind == TrackKind::midi)
        pianoRoll.openClip (clipId);
}

void MainComponent::showMenu (const juce::String& name, juce::Rectangle<int> screenArea)
{
    juce::PopupMenu menu;

    if (name.isEmpty())
        for (auto* menuName : getMenuNames())
            menu.addSubMenu (menuName, createCommandMenu (commandManager, menuName));
    else
        menu = createCommandMenu (commandManager, name);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (screenArea));
}

void MainComponent::updateStatusBar()
{
    statusBar.setProject ("Project: " + app.model.getProjectName());
    statusBar.setDevice (audioDeviceDescription);
    statusBar.setMode (uiFiles.isDevMode() ? "Dev UI: source tree" : juce::String());

    if (developerOverlay.isVisible())
        developerOverlay.setStatusText (app.model.getProjectName()
                                        + "   " + juce::String (app.model.getTracks().size()) + " tracks"
                                        + "   " + juce::String (app.model.getTransportPositionSeconds(), 2) + " s");
}

void MainComponent::modelChanged()
{
    updateStatusBar();
    commandManager.commandStatusChanged();   // undo/redo enablement
}

void MainComponent::themeChanged()
{
    if (auto* top = getTopLevelComponent())
        top->sendLookAndFeelChange();

    repaint();
}

//==============================================================================
void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    for (auto& entry : getApplicationCommandTable())
        if (app.commands.contains (entry.commandId))
            ids.add (entry.applicationCommandID);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    auto* entry = findApplicationCommand (id);
    auto* command = entry != nullptr ? app.commands.find (entry->commandId) : nullptr;

    if (command == nullptr)
        return;

    info.setInfo (command->getName(), command->getName(), entry->menu, 0);

    // Global shortcuts belong to the menus; a view's own go through the ShortcutListener.
    for (auto& binding : getKeyBindings())
        if (juce::String (binding.commandId) == entry->commandId && binding.contexts == ShortcutContext::anyView
            && binding.argument == KeyBinding::noArgument)
            info.addDefaultKeypress (binding.keyCode, juce::ModifierKeys (binding.modifiers));

    info.setActive (command->isEnabled());
    info.setTicked (command->isTicked());
}

bool MainComponent::perform (const InvocationInfo& invocation)
{
    if (auto* entry = findApplicationCommand (invocation.commandID))
        return app.commands.invokeById (entry->commandId);

    return false;
}

int MainComponent::currentShortcutContext() const
{
    switch (shell.getView())
    {
        case ShellState::View::session:    return ShortcutContext::sessionView;
        case ShellState::View::arrange:    return ShortcutContext::arrangeView;
        case ShellState::View::mixer:      return ShortcutContext::mixerView;
        case ShellState::View::pianoRoll:  return ShortcutContext::pianoRollView;
        case ShellState::View::editor:     return ShortcutContext::editorView;
    }

    return ShortcutContext::anyView;
}

bool MainComponent::ShortcutListener::keyPressed (const juce::KeyPress& key, juce::Component* origin)
{
    return viewShortcut (key) || owner.commandManager.getKeyMappings()->keyPressed (key, origin);
}

bool MainComponent::ShortcutListener::keyStateChanged (bool isKeyDown, juce::Component* origin)
{
    return owner.commandManager.getKeyMappings()->keyStateChanged (isKeyDown, origin);
}

bool MainComponent::ShortcutListener::viewShortcut (const juce::KeyPress& key)
{
    auto* binding = findBinding (key, owner.currentShortcutContext());

    if (binding == nullptr)
        return false;

    // A plain global shortcut on a menu Command is the ApplicationCommandManager's.
    const auto inMenus = std::any_of (getApplicationCommandTable().begin(), getApplicationCommandTable().end(),
                                      [binding] (auto& e) { return juce::String (e.commandId) == binding->commandId; });

    if (binding->contexts == ShortcutContext::anyView && binding->argument == KeyBinding::noArgument && inMenus)
        return false;

    if (! owner.app.commands.contains (binding->commandId))
        return false;

    owner.app.commands.invokeById (binding->commandId, bindingArgs (*binding));
    return true;
}

void MainComponent::registerPianoRollCommands()
{
    // The Piano Roll's keys act on the clip it has open, while it shows.
    auto openClip = [this]
    {
        return pianoRoll.isShowing() ? pianoRoll.openClipId() : juce::String();
    };

    app.commands.add (cmd::pianoRollQuantize, { "Quantize" }, [this, openClip]
    {
        if (auto clipId = openClip(); clipId.isNotEmpty())
            app.commands.invoke (cmd::noteQuantize, { clipId, "1/16" });
    });

    app.commands.add (cmd::pianoRollTranspose, { "Transpose" }, [this, openClip] (const int& semitones)
    {
        if (auto clipId = openClip(); clipId.isNotEmpty())
            app.commands.invoke (cmd::noteTransposeSelected, { clipId, semitones });
    });

    app.commands.add (cmd::pianoRollSelectAll, { "Select All Notes" }, [this, openClip]
    {
        if (auto clipId = openClip(); clipId.isNotEmpty())
            app.commands.invoke (cmd::noteSelectAll, { clipId });
    });
}

} // namespace resamper
