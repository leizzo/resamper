#pragma once

#include "PluginWindow.h"
#include "Engine/ApplicationModel.h"
#include "UI/MainWindow/Toasts.h"
#include "UI/State/Preferences.h"

#include <map>

namespace resamper
{

/** The device windows of the Edit, and their rules (PRD §9.6 window
    behaviour): a plug-in's window (PluginWindow), or a native device's
    floating Expanded editor (NativeDeviceWindow), which follows the same
    rules (#70). open() makes whichever the device needs.

    - One per instance: open() on a plug-in with a window brings it forward.
    - Placement: the first window centres over the anchor area (the
      arrangement); each further one cascades windowCascade px down-right. A
      window remembers its position on the plug-in, saved with the project; a
      position on a display that is gone comes back to the main display.
    - Pin: pinned windows stay on top and stay up whatever is selected.
      Unpinned windows hide while their track isn't selected (the preference
      "Show plug-in windows for selected track only") and come back when it
      is. Switching views hides nothing.
    - Selecting (clicking) a window selects its track; opening one does too.
    - closeFocused() (Esc on the chrome, Mod+W) and toggleAll() (Mod+Alt+P).
    - The opening rule: pluginAdded() opens an external plug-in's window,
      focused, when the user adds one (if the "Auto-open window on insert"
      preference is on), and offers the toast with Undo and that preference.
    - Each window's open / pinned / position / UI scale is written through
      plugin.setWindow; a window is never left over once its plug-in is gone,
      and a project's open windows come back when it loads.
    - Hosting State (Plug-in Hosting pushes it): each plug-in window shows
      its plug-in's. On the transition into Crashed, the window closes and a
      toast explains, offering Reload. Run in-process (the error state's)
      takes that instance out of its sandbox, through plugin.setRunInProcess.

    The rules are kept apart from what a window shows: windows are made in
    one place (createWindow), and the rules only use a FloatingDeviceWindow's
    position, pin, focus and callbacks, never what it holds. Whether a
    plug-in runs in or out of process (the sandbox, #69) only changes what
    the window reports (its Hosting State) and what onRunInProcess does. */
class PluginWindows : private ApplicationModel::Listener,
                      private PluginHosting::Listener,
                      private juce::ValueTree::Listener,
                      private juce::Timer
{
public:
    PluginWindows (const ApplicationModel&, const PluginRack&, const PluginHosting&, CommandRegistry&, ThemeManager&, Preferences&);
    ~PluginWindows() override;

    /** The desktop area a first window centres over (the arrangement). The main display's when unset. */
    std::function<juce::Rectangle<int>()> getAnchorArea;

    /** Shows a toast; set by the window that hosts the toasts. */
    std::function<void (const juce::String& message, std::vector<Toasts::Action>)> showToast;

    /** Called after windows open or close (the cards' "Window open · focus"). */
    std::function<void()> onOpenWindowsChanged;

    /** A key a window didn't use: Resamper's shortcuts, as the main window has them. True if one took it. */
    std::function<bool (const juce::KeyPress&)> onShortcut;

    /** Opens the device's window (a plug-in's window, a native device
        floating expanded), or brings it forward; focused unless focus is
        false. Does nothing for an unknown or missing plug-in. */
    void open (const juce::String& pluginId, bool focus = true);

    /** Closes a window (saved as closed). */
    void close (const juce::String& pluginId);

    /** Closes the plug-in window that has keyboard focus; false if none has. */
    bool closeFocused();

    /** Hides every plug-in window if any shows, else shows them all again. */
    void toggleAll();

    /** The opening rule: a plug-in the user just added. */
    void pluginAdded (const juce::String& trackId, const juce::String& pluginId);

    void setPinned (const juce::String& pluginId, bool);

    /** Devices with a window, shown or hidden. */
    juce::StringArray getOpenPluginIds() const;
    bool isOpen (const juce::String& pluginId) const;
    bool isShowing (const juce::String& pluginId) const;

    /** The device's window, of either kind; nullptr if it has none. */
    FloatingDeviceWindow* getDeviceWindow (const juce::String& pluginId) const;

    /** A plug-in's window; nullptr if it has none or the device is native. */
    PluginWindow* getWindow (const juce::String& pluginId) const;

    /** Applies the rules now (the model notifies asynchronously). */
    void refresh();

private:
    struct Entry
    {
        std::unique_ptr<FloatingDeviceWindow> window;
        bool hiddenByUser = false;   ///< toggleAll hid it
    };

    const ApplicationModel& model;
    const PluginRack& rack;
    const PluginHosting& hosting;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    Preferences& preferences;
    std::map<juce::String, Entry> windows;
    bool refreshing = false, refreshAgain = false;

    std::unique_ptr<FloatingDeviceWindow> createWindow (const PluginInfo&);
    void addWindow (const PluginInfo&);
    juce::String trackNameOf (const juce::String& trackId) const;
    juce::Point<int> placementFor (const FloatingDeviceWindow&, const PluginWindowState&) const;
    static bool isOnADisplay (juce::Rectangle<int> frame);
    bool shouldShow (const Entry&) const;
    void saveState (const FloatingDeviceWindow&, bool open);
    void selectTrackOf (const FloatingDeviceWindow&);
    void announceAndFocus (FloatingDeviceWindow&);
    void openWindowsChanged();
    bool isMissing (const juce::String& pluginId) const;

    void modelChanged() override;
    /** The plug-in crashed: its window closes, and a toast offers Reload. */
    void closeCrashed (const juce::String& pluginId);
    void hostingStateChanged (const juce::String& pluginId, const HostingState&) override;
    void pluginUiClicked (const juce::String& pluginId) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void timerCallback() override;

    JUCE_DECLARE_WEAK_REFERENCEABLE (PluginWindows)
    JUCE_DECLARE_NON_COPYABLE (PluginWindows)
};

} // namespace resamper
