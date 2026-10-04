#include "PluginWindows.h"
#include "NativeDeviceWindow.h"

#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"
#include "Commands/TrackCommands.h"

#include <algorithm>

namespace resamper
{

namespace
{
    constexpr int displayCheckMs = 1000;
    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));
}

PluginWindows::PluginWindows (const ApplicationModel& m, const PluginRack& r, const PluginHosting& h, CommandRegistry& c, ThemeManager& tm,
                              Preferences& p)
    : model (m), rack (r), hosting (h), commands (c), themeManager (tm), preferences (p)
{
    model.addListener (this);
    hosting.addListener (this);
    preferences.getState().addListener (this);
    startTimer (displayCheckMs);
}

PluginWindows::~PluginWindows()
{
    preferences.getState().removeListener (this);
    hosting.removeListener (this);
    model.removeListener (this);
}

juce::String PluginWindows::trackNameOf (const juce::String& trackId) const
{
    for (auto& track : model.getTracks())
        if (track.id == trackId)
            return track.name;

    return "Track";
}

std::unique_ptr<FloatingDeviceWindow> PluginWindows::createWindow (const PluginInfo& info)
{
    const auto id = info.id;
    std::unique_ptr<FloatingDeviceWindow> window;

    // A plug-in gets its host-chromed window; a native device its card, floating expanded (#70).
    if (info.external)
    {
        auto plugInWindow = std::make_unique<PluginWindow> (rack, commands, themeManager, info, hosting.getState (id),
                                                            trackNameOf (info.trackId));

        // The error state's Run in-process: this instance leaves its sandbox, saved with the project.
        plugInWindow->onRunInProcess = [this, id] { commands.invoke (cmd::pluginSetRunInProcess, { id, true }); };

        window = std::move (plugInWindow);
    }
    else
    {
        window = std::make_unique<NativeDeviceWindow> (rack, commands, themeManager, info, trackNameOf (info.trackId));
    }

    // A window is deleted after the callback that closes it has returned.
    window->onCloseRequested = [this, id]
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<PluginWindows> (this), id]
        {
            if (safe != nullptr)
                safe->close (id);
        });
    };

    window->onActivated = [this, id]
    {
        if (auto* w = getDeviceWindow (id))
            selectTrackOf (*w);
    };

    window->onMoved = [this, id]
    {
        if (auto* w = getDeviceWindow (id))
            saveState (*w, true);
    };

    window->onPinChanged = [this, id] (bool pinned) { setPinned (id, pinned); };

    window->onUiScaleChanged = [this, id] (int)
    {
        if (auto* w = getDeviceWindow (id))
            saveState (*w, true);
    };

    window->onToggleAll = [this] { toggleAll(); };
    window->onUnhandledKey = [this] (const juce::KeyPress& key) { return onShortcut != nullptr && onShortcut (key); };

    return window;
}

void PluginWindows::addWindow (const PluginInfo& info)
{
    auto window = createWindow (info);
    const auto saved = rack.getWindowState (info.id);
    window->setPinned (saved.pinned);
    window->setUiScale (saved.uiScale);
    window->addToDesktop (0);
    window->setFramePosition (placementFor (*window, saved));
    windows[info.id].window = std::move (window);
}

void PluginWindows::open (const juce::String& pluginId, bool focus)
{
    const auto info = rack.getPlugin (pluginId);

    // A missing plug-in has nothing to show; a crashed one is reloaded from its card first.
    if (const auto kind = hosting.getState (pluginId).kind;
        ! info.has_value() || kind == HostingState::Kind::missing || kind == HostingState::Kind::crashed)
        return;

    if (auto existing = windows.find (pluginId); existing != windows.end())
    {
        // One per instance: re-opening brings it forward.
        existing->second.hiddenByUser = false;
        auto& window = *existing->second.window;
        selectTrackOf (window);
        window.setVisible (true);
        window.toFront (focus);

        if (focus)
            window.grabKeyboardFocus();

        return;
    }

    addWindow (*info);
    auto& entry = windows[pluginId];

    // Its track is selected, so the selected-track rule shows it.
    selectTrackOf (*entry.window);
    entry.window->setVisible (true);
    entry.window->toFront (focus);

    if (focus)
        announceAndFocus (*entry.window);

    saveState (*entry.window, true);
    openWindowsChanged();
}

void PluginWindows::announceAndFocus (FloatingDeviceWindow& window)
{
    window.grabKeyboardFocus();
    juce::AccessibilityHandler::postAnnouncement (window.getName() + " window opened",
                                                  juce::AccessibilityHandler::AnnouncementPriority::medium);
}

juce::Point<int> PluginWindows::placementFor (const FloatingDeviceWindow& window, const PluginWindowState& saved) const
{
    const auto frame = window.getFrameScreenBounds();
    const auto size = juce::Point<int> (frame.getWidth(), frame.getHeight());

    if (saved.placed && isOnADisplay ({ saved.position.x, saved.position.y, size.x, size.y }))
        return saved.position;

    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto anchor = getAnchorArea ? getAnchorArea() : juce::Rectangle<int>();
    const auto* mainDisplay = displays.getPrimaryDisplay();

    // A saved position on a display that is gone, or no anchor yet: the main display.
    if (anchor.isEmpty() || (saved.placed && mainDisplay != nullptr))
        anchor = mainDisplay != nullptr ? mainDisplay->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1280, 800);

    // The first window centres; each further one steps down-right to the first spot no other window holds.
    const auto step = themeManager.getMetrics().windowCascade;
    auto position = anchor.getCentre() - juce::Point<int> (size.x / 2, size.y / 2);

    for (size_t i = 0; i < windows.size(); ++i)
    {
        const auto taken = std::any_of (windows.begin(), windows.end(), [&] (auto& e)
        {
            return e.second.window.get() != &window && e.second.window->getFrameScreenBounds().getPosition() == position;
        });

        if (! taken)
            break;

        position += juce::Point<int> (step, step);
    }

    return position;
}

bool PluginWindows::isOnADisplay (juce::Rectangle<int> frame)
{
    // At least the title bar's middle must be on some display, so it can be dragged.
    const auto grabPoint = juce::Point<int> (frame.getCentreX(), frame.getY() + 10);

    for (auto& display : juce::Desktop::getInstance().getDisplays().displays)
        if (display.userBounds.toNearestInt().contains (grabPoint))
            return true;

    return false;
}

void PluginWindows::close (const juce::String& pluginId)
{
    auto found = windows.find (pluginId);

    if (found == windows.end())
        return;

    auto window = std::move (found->second.window);
    windows.erase (found);

    if (rack.contains (pluginId))
        saveState (*window, false);

    window.reset();
    openWindowsChanged();
}

bool PluginWindows::closeFocused()
{
    for (auto& [id, entry] : windows)
    {
        if (entry.window->isVisible() && entry.window->hasFocusInside())
        {
            close (juce::String (id));
            return true;
        }
    }

    return false;
}

void PluginWindows::toggleAll()
{
    const auto anyShowing = std::any_of (windows.begin(), windows.end(),
                                         [] (auto& e) { return e.second.window->isVisible(); });

    for (auto& [id, entry] : windows)
        entry.hiddenByUser = anyShowing;

    refresh();
}

void PluginWindows::setPinned (const juce::String& pluginId, bool pinned)
{
    if (auto* window = getDeviceWindow (pluginId))
    {
        window->setPinned (pinned);
        saveState (*window, true);
        refresh();
    }
}

void PluginWindows::pluginUiClicked (const juce::String& pluginId)
{
    // A click in a sandboxed plug-in's own UI is a click in its window.
    if (auto* window = getDeviceWindow (pluginId))
        selectTrackOf (*window);
}

void PluginWindows::hostingStateChanged (const juce::String& pluginId, const HostingState& state)
{
    if (state.kind == HostingState::Kind::crashed)
        closeCrashed (pluginId);
    else if (auto* window = getWindow (pluginId))
        window->setHostingState (state);
    else if (state.kind == HostingState::Kind::loading)
        refresh();   // a Missing plug-in, found, starts: its window comes back if it was saved open
}

bool PluginWindows::isMissing (const juce::String& pluginId) const
{
    return hosting.getState (pluginId).kind == HostingState::Kind::missing;
}

void PluginWindows::closeCrashed (const juce::String& pluginId)
{
    const auto info = rack.getPlugin (pluginId);

    if (! info.has_value())
        return;

    // Its window goes (§9.6 Crash); the card turns red with Reload.
    if (isOpen (pluginId))
        close (pluginId);

    if (! showToast)
        return;

    std::vector<Toasts::Action> actions;
    actions.push_back ({ "Reload", [this, trackId = info->trackId, pluginId] (bool)
    {
        commands.invoke (cmd::pluginReload, { trackId, pluginId });
    }, std::nullopt });

    showToast (info->name + " crashed on " + trackNameOf (info->trackId) + " " + middleDot
                   + " its audio is bypassed; the rest plays on",
               std::move (actions));
}

void PluginWindows::pluginAdded (const juce::String& trackId, const juce::String& pluginId)
{
    const auto info = rack.getPlugin (pluginId);

    // Only a plug-in gets a window; a native device's card takes focus instead (§9.2.3).
    if (! info.has_value() || ! info->external || isMissing (pluginId))
        return;

    const auto autoOpen = preferences.getAutoOpenPluginWindows();

    if (autoOpen)
        open (pluginId, true);

    if (! showToast)
        return;

    auto message = info->name + " added to " + trackNameOf (trackId);

    if (autoOpen)
        message << " " << middleDot << " Plug-in window opened automatically";

    std::vector<Toasts::Action> actions;
    actions.push_back ({ "Undo", [this, trackId, pluginId] (bool)
    {
        commands.invoke (cmd::pluginUndoInsert, { trackId, pluginId });
    }, std::nullopt });
    actions.push_back ({ "Auto-open window on insert", [this] (bool on)
    {
        preferences.setAutoOpenPluginWindows (on);
    }, autoOpen });

    showToast (message, std::move (actions));
}

juce::StringArray PluginWindows::getOpenPluginIds() const
{
    juce::StringArray ids;

    for (auto& [id, entry] : windows)
        ids.add (id);

    return ids;
}

bool PluginWindows::isOpen (const juce::String& pluginId) const
{
    return windows.find (pluginId) != windows.end();
}

bool PluginWindows::isShowing (const juce::String& pluginId) const
{
    auto* window = getDeviceWindow (pluginId);
    return window != nullptr && window->isVisible();
}

FloatingDeviceWindow* PluginWindows::getDeviceWindow (const juce::String& pluginId) const
{
    auto found = windows.find (pluginId);
    return found != windows.end() ? found->second.window.get() : nullptr;
}

PluginWindow* PluginWindows::getWindow (const juce::String& pluginId) const
{
    return dynamic_cast<PluginWindow*> (getDeviceWindow (pluginId));
}

bool PluginWindows::shouldShow (const Entry& entry) const
{
    if (entry.hiddenByUser)
        return false;

    return entry.window->isPinned() || ! preferences.getPluginWindowsForSelectedTrackOnly()
        || entry.window->getTrackId() == model.getSelectedTrackId();
}

void PluginWindows::saveState (const FloatingDeviceWindow& window, bool open)
{
    PluginWindowState state;
    state.open = open;
    state.pinned = window.isPinned();
    state.placed = true;
    state.position = window.getFrameScreenBounds().getPosition();
    state.uiScale = window.getUiScale();
    commands.invoke (cmd::pluginSetWindow, { window.getPluginId(), state });
}

void PluginWindows::selectTrackOf (const FloatingDeviceWindow& window)
{
    if (window.getTrackId().isNotEmpty() && model.getSelectedTrackId() != window.getTrackId())
        commands.invoke (cmd::trackSelect, { window.getTrackId() });
}

void PluginWindows::openWindowsChanged()
{
    if (onOpenWindowsChanged)
        onOpenWindowsChanged();
}

void PluginWindows::refresh()
{
    // Showing a window may ask for a refresh of its own (a preference flipped from it): it runs after this one.
    if (std::exchange (refreshing, true))
    {
        refreshAgain = true;
        return;
    }

    bool changed = false;

    // A window never outlives its plug-in (deleted, its insert undone, its track or Project gone).
    for (auto it = windows.begin(); it != windows.end();)
    {
        const auto info = rack.getPlugin (it->first);

        if (! info.has_value() || isMissing (it->first))
        {
            it = windows.erase (it);
            changed = true;
            continue;
        }

        it->second.window->setState (*info, trackNameOf (info->trackId));
        ++it;
    }

    // A project's open windows come back when it loads, where they were, unfocused.
    for (auto& info : rack.getAllPlugins())
    {
        if (isOpen (info.id) || isMissing (info.id) || ! rack.getWindowState (info.id).open)
            continue;

        addWindow (info);
        changed = true;
    }

    for (auto& [id, entry] : windows)
    {
        const auto show = shouldShow (entry);

        if (entry.window->isVisible() != show)
        {
            entry.window->setVisible (show);

            if (show)
                entry.window->toFront (false);
        }
    }

    refreshing = false;

    if (changed)
        openWindowsChanged();

    if (std::exchange (refreshAgain, false))
        refresh();
}

void PluginWindows::modelChanged()
{
    refresh();
}

void PluginWindows::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    refresh();
}

void PluginWindows::timerCallback()
{
    // A window left on a display that was disconnected comes back to the main display.
    for (auto& [id, entry] : windows)
    {
        auto& window = *entry.window;

        if (window.isVisible() && ! isOnADisplay (window.getFrameScreenBounds()))
        {
            PluginWindowState lost;
            lost.placed = true;
            lost.position = window.getFrameScreenBounds().getPosition();
            window.setFramePosition (placementFor (window, lost));
            saveState (window, true);
        }
    }
}

} // namespace resamper
