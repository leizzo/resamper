#include "SandboxDock.h"

namespace resamper::sandboxdock
{

void raisePopup (juce::Component&);
void watchForPopups();

namespace
{
    bool isPopupWindow (juce::Component& component)
    {
        auto* peer = component.getPeer();

        if (peer == nullptr || ! component.isShowing())
            return false;

        // A menu, a tooltip, a toast: its own temporary window. A dialog (Save Preset): modal.
        if ((peer->getStyleFlags() & juce::ComponentPeer::windowIsTemporary) != 0)
            return true;

        return component.isCurrentlyModal();
    }
}

void orderPopupsAboveSandboxedUi()
{
    static bool raising = false;

    if (raising)
        return;

    const juce::ScopedValueSetter<bool> guard (raising, true);
    watchForPopups();

    auto& desktop = juce::Desktop::getInstance();

    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* component = desktop.getComponent (i); component != nullptr && isPopupWindow (*component))
            raisePopup (*component);
}

} // namespace resamper::sandboxdock

// macOS: SandboxDock_mac.mm. Elsewhere a plain borderless window over the
// area; it can't be ordered by another process's window, only kept on top.
#if ! JUCE_MAC

namespace resamper::sandboxdock
{

void watchForPopups() {}

void raisePopup (juce::Component& component)
{
    component.setAlwaysOnTop (true);
    component.toFront (false);
}

bool isInFrontOf (const juce::Component&, juce::Rectangle<int>)
{
    // The host panel is another process's window, so this can't see it.
    // Check by hand that the preset menu, Save Preset, a tooltip on the chrome,
    // and a toast each stay above the vendor area.
    return false;
}

WindowRef windowOf (juce::Component& component)
{
    auto* top = component.getTopLevelComponent();
    auto* peer = component.getPeer();

    if (peer == nullptr)
        return {};

    return { (juce::int64) (juce::pointer_sized_int) peer->getNativeHandle(), top->isAlwaysOnTop() ? 1 : 0 };
}

struct Panel::Impl : juce::MouseListener
{
    Impl (juce::Component& c, std::function<void()> clicked) : content (c), onClicked (std::move (clicked)) {}

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClicked)
            onClicked();
    }

    juce::Component& content;
    std::function<void()> onClicked;
};

Panel::Panel (juce::Component& content, std::function<void()> onClicked)
    : impl (std::make_unique<Impl> (content, std::move (onClicked)))
{
    content.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    content.addMouseListener (impl.get(), true);
}

Panel::~Panel()
{
    impl->content.removeMouseListener (impl.get());
    impl->content.removeFromDesktop();
}

void Panel::place (juce::Rectangle<int> area, bool visible, WindowRef above)
{
    auto& content = impl->content;
    const auto show = visible && ! area.isEmpty() && above.number != 0;

    if (show)
    {
        content.setBounds (area);
        content.setAlwaysOnTop (above.level != 0);
        content.toFront (false);
    }

    content.setVisible (show);
}

void Panel::keepAbove()
{
    // Here the panel is always on top of the window it covers.
}

juce::Rectangle<int> Panel::getScreenBounds() const
{
    return impl->content.isVisible() ? impl->content.getScreenBounds() : juce::Rectangle<int>();
}

} // namespace resamper::sandboxdock

#endif
