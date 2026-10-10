#include "SandboxDock.h"

#import <AppKit/AppKit.h>

/** A borderless panel that takes keys without activating its app. */
@interface ResamperSandboxDockPanel : NSPanel
@end

@implementation ResamperSandboxDockPanel
- (BOOL) canBecomeKeyWindow    { return YES; }
- (BOOL) canBecomeMainWindow   { return NO; }
@end

namespace resamper::sandboxdock
{

namespace
{
    /** AppKit's desktop is bottom-up from the main display's bottom; JUCE's top-down from its top. */
    CGFloat mainDisplayHeight()
    {
        auto* screens = [NSScreen screens];
        return [screens count] > 0 ? NSMaxY ([[screens objectAtIndex: 0] frame]) : 0;
    }
}

WindowRef windowOf (juce::Component& component)
{
    auto* peer = component.getPeer();
    auto* view = peer != nullptr ? (NSView*) peer->getNativeHandle() : nil;
    auto* window = view != nil ? [view window] : nil;

    if (window == nil)
        return {};

    return { (juce::int64) [window windowNumber], (int) [window level] };
}

struct Panel::Impl
{
    juce::Component* content = nullptr;
    NSPanel* panel = nil;
    id monitor = nil;
    juce::int64 above = 0;   ///< the window place() put the panel just above
};

Panel::Panel (juce::Component& content, std::function<void()> onClicked)
    : impl (std::make_unique<Impl>())
{
    auto* panel = [[ResamperSandboxDockPanel alloc] initWithContentRect: NSMakeRect (0, 0, content.getWidth(), content.getHeight())
                                                              styleMask: NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                                backing: NSBackingStoreBuffered
                                                                  defer: NO];
    [panel setReleasedWhenClosed: NO];
    [panel setHidesOnDeactivate: NO];   // its app is never active
    [panel setFloatingPanel: NO];
    [panel setBecomesKeyOnlyIfNeeded: NO];
    [panel setHasShadow: NO];
    [panel setCollectionBehavior: NSWindowCollectionBehaviorFullScreenAuxiliary];
    impl->panel = panel;
    impl->content = &content;

    content.setTopLeftPosition (0, 0);
    content.setVisible (true);
    content.addToDesktop (0, (void*) [panel contentView]);

    impl->monitor = [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown
                                                          handler: ^NSEvent* (NSEvent* event)
    {
        if ([event window] == panel && onClicked)
            onClicked();

        return event;
    }];
}

Panel::~Panel()
{
    [NSEvent removeMonitor: impl->monitor];
    impl->content->removeFromDesktop();
    [impl->panel orderOut: nil];
    [impl->panel release];
}

void Panel::place (juce::Rectangle<int> area, bool visible, WindowRef above)
{
    auto* panel = impl->panel;

    if (! visible || area.isEmpty() || above.number == 0)
    {
        [panel orderOut: nil];
        return;
    }

    [panel setFrame: NSMakeRect (area.getX(), mainDisplayHeight() - area.getBottom(), area.getWidth(), area.getHeight())
            display: YES];

    // Menus, dialogs, tooltips and toasts sit at the menu level. The panel stays
    // at its plug-in window's level, and never above floating, so a popup stays above it.
    const auto level = juce::jmin (above.level, (int) NSFloatingWindowLevel);
    [panel setLevel: level];
    [panel orderWindow: NSWindowAbove relativeTo: (NSInteger) above.number];
    impl->above = above.number;
}

void Panel::keepAbove()
{
    if ([impl->panel isVisible] && impl->above != 0)
        [impl->panel orderWindow: NSWindowAbove relativeTo: (NSInteger) impl->above];
}

juce::Rectangle<int> Panel::getScreenBounds() const
{
    auto* panel = impl->panel;

    if (! [panel isVisible])
        return {};

    const auto frame = [panel frame];
    return juce::Rectangle<double> (frame.origin.x, mainDisplayHeight() - NSMaxY (frame), frame.size.width, frame.size.height)
               .toNearestInt();
}

void raisePopup (juce::Component& component)
{
    auto* peer = component.getPeer();
    auto* view = peer != nullptr ? (NSView*) peer->getNativeHandle() : nil;
    auto* window = view != nil ? [view window] : nil;

    if (window == nil)
        return;

    // Already at the menu level: leave it. Raising orders it to the front of that level,
    // above the panel, which never goes above floating.
    if ([window level] >= NSPopUpMenuWindowLevel)
        return;

    [window setLevel: NSPopUpMenuWindowLevel];
}

void watchForPopups()
{
    static id becameKey = nil;

    if (becameKey != nil)
        return;

    // A dialog becomes key once it is modal. Tooltips and toasts are not key
    // windows; the editor's timer raises those.
    becameKey = [[NSNotificationCenter defaultCenter] addObserverForName: NSWindowDidBecomeKeyNotification
                                                                   object: nil
                                                                    queue: nil
                                                               usingBlock: ^void (NSNotification*) { orderPopupsAboveSandboxedUi(); }];
}

namespace
{
    bool near (int a, int b) { return a - b <= 4 && b - a <= 4; }

    bool sameArea (juce::Rectangle<int> a, juce::Rectangle<int> b)
    {
        return near (a.getX(), b.getX()) && near (a.getY(), b.getY())
            && near (a.getWidth(), b.getWidth()) && near (a.getHeight(), b.getHeight());
    }

    juce::Rectangle<int> rectOf (CGRect bounds)
    {
        return juce::Rectangle<double> (bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height).toNearestInt();
    }

    /** Quartz lists windows in points or pixels, top-left or bottom-left. Any of those can match a JUCE area. */
    bool covers (CGRect bounds, juce::Rectangle<int> area)
    {
        const auto cg = rectOf (bounds);
        const auto height = juce::roundToInt (mainDisplayHeight());

        const auto flipped = [] (juce::Rectangle<int> r, int displayHeight)
        {
            return juce::Rectangle<int> (r.getX(), displayHeight - r.getBottom(), r.getWidth(), r.getHeight());
        };

        if (sameArea (cg, area) || sameArea (flipped (cg, height), area))
            return true;

        for (int scale : { 2, 3 })
        {
            const auto scaled = juce::Rectangle<int> (area.getX() * scale, area.getY() * scale,
                                                      area.getWidth() * scale, area.getHeight() * scale);

            if (sameArea (cg, scaled) || sameArea (flipped (cg, height * scale), scaled))
                return true;
        }

        return false;
    }
}

bool isInFrontOf (const juce::Component& popup, juce::Rectangle<int> area)
{
    const auto popupWindow = windowOf (const_cast<juce::Component&> (popup)).number;

    if (popupWindow == 0 || area.isEmpty() || ! popup.isShowing())
        return false;

    struct Listed
    {
        CFArrayRef windows = nullptr;
        Listed() = default;
        ~Listed() { if (windows != nullptr) CFRelease (windows); }
        Listed (const Listed&) = delete;
        Listed& operator= (const Listed&) = delete;
    };

    Listed list;
    list.windows = CGWindowListCopyWindowInfo (kCGWindowListOptionOnScreenOnly, kCGNullWindowID);

    if (list.windows == nullptr)
        return false;

    int popupIndex = -1, coveredIndex = -1;
    const auto count = CFArrayGetCount (list.windows);

    for (CFIndex i = 0; i < count; ++i)
    {
        auto* dict = (CFDictionaryRef) CFArrayGetValueAtIndex (list.windows, i);
        auto* numberValue = (CFNumberRef) CFDictionaryGetValue (dict, kCGWindowNumber);
        juce::int64 number = 0;

        if (numberValue == nullptr || ! CFNumberGetValue (numberValue, kCFNumberSInt64Type, &number))
            continue;

        if (number == popupWindow)
            popupIndex = (int) i;

        auto* boundsValue = (CFDictionaryRef) CFDictionaryGetValue (dict, kCGWindowBounds);
        CGRect bounds;

        // The front-most window with the plug-in UI's bounds is the panel. Later copies are behind it.
        if (coveredIndex < 0 && boundsValue != nullptr
            && CGRectMakeWithDictionaryRepresentation (boundsValue, &bounds) && covers (bounds, area) && number != popupWindow)
            coveredIndex = (int) i;
    }

    return popupIndex >= 0 && coveredIndex >= 0 && popupIndex < coveredIndex;
}

} // namespace resamper::sandboxdock
