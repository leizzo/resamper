#include "NativeDeviceWindow.h"

#include "UI/Detail/NativeDeviceCard.h"

namespace resamper
{

NativeDeviceWindow::NativeDeviceWindow (const PluginRack& rack, CommandRegistry& commands, ThemeManager& tm, const PluginInfo& info,
                                        const juce::String& track)
    : FloatingDeviceWindow (tm, info, track, "NativeDeviceWindow"),
      card (std::make_unique<NativeDeviceCard> (commands, rack, tm, info.trackId, info))
{
    card->setFloating (true);
    addAndMakeVisible (*card);
    setState (info, track);
}

NativeDeviceWindow::~NativeDeviceWindow() = default;

void NativeDeviceWindow::setState (const PluginInfo& info, const juce::String& track)
{
    plugin = info;
    trackName = track;
    setName (plugin.name);
    setTitle (plugin.name + " device window");
    setDescription (trackName);
    card->setState (info);
    updateSize();
    repaint();
}

void NativeDeviceWindow::updateSize()
{
    setFrameSize (card->getPreferredWidth (expandedWidth), themeManager.getMetrics().pluginTitleBarHeight + DeviceCard::height);
}

void NativeDeviceWindow::resized()
{
    layoutTitleBar();
    card->setBounds (frame().withTrimmedTop (titleBar().getHeight()));
}

void NativeDeviceWindow::paintTitle (juce::Graphics& g)
{
    auto title = titleArea();
    drawTrackAndName (g, title);
}

} // namespace resamper
