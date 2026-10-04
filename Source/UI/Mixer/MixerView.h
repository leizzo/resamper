#pragma once

#include "ChannelStrip.h"
#include "MasterStrip.h"
#include "Engine/ApplicationModel.h"
#include "UI/Theme/ThemeManager.h"

#include <map>
#include <memory>
#include <vector>

namespace resamper
{

class CommandRegistry;
class Mixer;

/** The Mixer view (PRD §10.1): the 40 px toolbar, then the strips area — the
    track and Bus Strips in signal-flow order, the return strips, and the
    master strip — scrolling sideways when they don't fit. Meters refresh at 30 Hz while the mixer
    shows, and pause while it's hidden (§19).

    Toolbar: the title, an add menu (return, bus, send, move to bus), section
    chips that show or hide a section on every strip (§10.7), the signal-flow
    indicator (the stage under the pointer in lime), the meter mode
    (Peak | RMS | LUFS) and Reset Peaks. Chips and meter mode are UI State,
    saved with the project. */
class MixerView : public juce::Component,
                  private ApplicationModel::Listener,
                  private ThemeManager::Listener,
                  private juce::Timer
{
public:
    static constexpr const char* componentId = "mixer";

    MixerView (const ApplicationModel&, const Mixer&, const PluginRack&, const PluginHosting&, CommandRegistry&, ThemeManager&, juce::ValueTree uiState);
    ~MixerView() override;

    /** A strip's Track chain row was clicked. */
    std::function<void (const juce::String& trackId)> onShowDeviceChain;

    /** A filled insert slot was clicked: open its device's window (a
        plug-in's window, or a native device's floating Expanded editor). */
    std::function<void (const juce::String& pluginId)> onOpenPlugin;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct StripsArea : juce::Component {};

    const ApplicationModel& model;
    const Mixer& mixer;
    const PluginRack& plugins;
    const PluginHosting& hosting;
    CommandRegistry& commands;
    ThemeManager& themeManager;

    juce::ValueTree state;

    struct SectionChip
    {
        const char* name;
        std::optional<ChannelStrip::Section> section;
        std::unique_ptr<Chip> button;
    };

    std::array<SectionChip, 6> sectionChips { {
        { "I/O", ChannelStrip::Section::io, {} }, { "Inserts", ChannelStrip::Section::inserts, {} },
        { "Sends", ChannelStrip::Section::sends, {} }, { "EQ", std::nullopt, {} },
        { "Fader", ChannelStrip::Section::fader, {} }, { "Comments", std::nullopt, {} },
    } };

    Segmented meterMode;
    Button resetPeaks;
    juce::Rectangle<int> titleArea, titleDivider, flowIndicator;
    int flowWidth() const;
    int flowStage = -1;
    juce::Viewport viewport;
    StripsArea stripsArea;
    std::vector<juce::String> trackOrder, returnOrder;
    std::map<juce::String, std::unique_ptr<ChannelStrip>> strips;
    MasterStrip master;
    double lastMeterTime = 0;

    void refresh();
    void layoutStrips();
    void showStripMenu (const Strip&);
    void showEffectPicker (const juce::String& trackId, InsertSlot&, const juce::String& replacing);
    void applySections();
    void applyMeterMode();
    void setFlowStage (int);
    void timerCallback() override;

    void modelChanged() override   { refresh(); }
    void themeChanged() override   { repaint(); }
};

} // namespace resamper
