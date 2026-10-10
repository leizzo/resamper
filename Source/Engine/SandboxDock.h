#pragma once

#include "EngineInternal.h"
#ifdef RESAMPER_ENGINE_INTERNAL

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace resamper
{

/** Where a sandboxed plug-in's own UI shows (PluginSandbox): in a panel of
    its sandbox host, laid exactly over the vendor area of its plug-in window
    in Resamper and kept just above that window, and below Resamper's popups.
    The panel never makes the host the front app, so Resamper keeps its menu
    bar and its windows. */
namespace sandboxdock
{
    /** A desktop window, as another process can order its own windows by it. */
    struct WindowRef
    {
        juce::int64 number = 0;   ///< 0: none (not on the desktop)
        int level = 0;            ///< its window level (floating while Resamper is in front)

        bool operator== (const WindowRef&) const = default;
    };

    /** The desktop window a component is in. */
    WindowRef windowOf (juce::Component&);

    /** The sandbox host's panel holding the plug-in's own editor. */
    class Panel
    {
    public:
        /** content goes on the panel's desktop window; onClicked hears a mouse press inside it. */
        Panel (juce::Component& content, std::function<void()> onClicked);
        ~Panel();

        /** Shows the panel over screenArea (logical desktop coordinates), just above
            the given window and at its level, never above floating; or hides it.
            Popups sit at the menu level (orderPopupsAboveSandboxedUi), so a menu,
            dialog, tooltip or toast stays above the panel. */
        void place (juce::Rectangle<int> screenArea, bool visible, WindowRef above);

        /** Puts the panel back just above the window place() last gave, if it shows: something
            may have brought that window to the front since (a click in it). */
        void keepAbove();

        /** Where the panel is on the desktop; empty while hidden. */
        juce::Rectangle<int> getScreenBounds() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;

        JUCE_DECLARE_NON_COPYABLE (Panel)
    };

    /** Puts every popup on the desktop above sandboxed plug-in UI: a temporary
        window (a menu, a tooltip, a toast) or a modal dialog (Save Preset).
        The panel stays at its plug-in window's level; a popup is raised to the
        menu level, which is above that. */
    void orderPopupsAboveSandboxedUi();

    /** Whether popup's window is in front of the on-screen window covering area
        (a sandboxed plug-in's own UI). False if either window isn't on screen. */
    bool isInFrontOf (const juce::Component& popup, juce::Rectangle<int> area);
}

} // namespace resamper
#endif
