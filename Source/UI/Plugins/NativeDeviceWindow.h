#pragma once

#include "FloatingDeviceWindow.h"

namespace resamper
{

class CommandRegistry;
class NativeDeviceCard;

/** A native device's floating Expanded editor (PRD §9.2.1a, #66): its card,
    always expanded, under the host title bar (Track › Device, Pin, Close).

    Opened from the card's Open in Window or a click on a native mixer insert
    (#70: no inline popover), it follows the plug-in window's rules
    (PluginWindows): one per instance, Pin, hidden while its track isn't
    selected unless pinned. Every parameter is edited on the card, through
    Commands. */
class NativeDeviceWindow : public FloatingDeviceWindow
{
public:
    NativeDeviceWindow (const PluginRack&, CommandRegistry&, ThemeManager&, const PluginInfo&, const juce::String& trackName);
    ~NativeDeviceWindow() override;

    /** The width the floating card fills at least. */
    static constexpr int expandedWidth = 720;

    void setState (const PluginInfo&, const juce::String& trackName) override;

    void resized() override;

private:
    std::unique_ptr<NativeDeviceCard> card;

    void updateSize();
    void paintTitle (juce::Graphics&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NativeDeviceWindow)
};

} // namespace resamper
