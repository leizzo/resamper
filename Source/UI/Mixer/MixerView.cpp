#include "MixerView.h"
#include "Commands/MixerCommands.h"
#include "Commands/PluginCommands.h"
#include "UI/Controls/Menus.h"
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    constexpr int stripsPadding = 12, groupGap = 14, stripGap = 6, masterWidth = 186;
    constexpr int toolbarGap = 14, flowPadX = 10, flowGap = 6, flowIcon = 10;
    const TypeStyle titleStyle { 13.0f, false, 600 }, flowStyle { 10.0f, false, 400 };
    const char* const flowStages[] = { NEEDS_TRANS ("Track chain"), NEEDS_TRANS ("Inserts"), NEEDS_TRANS ("Sends"), NEEDS_TRANS ("Fader") };
}

MixerView::MixerView (const ApplicationModel& m, const Mixer& mx, const PluginRack& p, const PluginHosting& h, CommandRegistry& c, ThemeManager& tm,
                      juce::ValueTree uiState)
    : model (m), mixer (mx), plugins (p), hosting (h), commands (c), themeManager (tm), state (std::move (uiState)),
      meterMode (tm, { TRANS ("Peak"), "RMS", "LUFS" }, Segmented::Style::sunken),
      resetPeaks (tm, TRANS ("Reset Peaks"), Button::Variant::outline, Icon::rotateCcw),
      master (c, tm)
{
    setComponentID (componentId);

    for (auto& chip : sectionChips)
    {
        chip.button = std::make_unique<Chip> (themeManager, TRANS (chip.name));
        chip.button->setShowsLed (true);
        chip.button->setToggleState (! (bool) state.getProperty ("hide_" + juce::String (chip.name), false), juce::dontSendNotification);
        chip.button->onClick = [this, &chip]
        {
            state.setProperty ("hide_" + juce::String (chip.name), ! chip.button->getToggleState(), nullptr);
            applySections();
        };

        // EQ and Comments have no strip section yet.
        chip.button->setEnabled (chip.section.has_value());
        chip.button->setTooltip (chip.section ? tr ("Show or hide %1 on every strip", TRANS (chip.name))
                                              : tr ("%1: not in the strip yet", TRANS (chip.name)));
        addAndMakeVisible (*chip.button);
    }

    meterMode.setTitle (TRANS ("Meter mode"));
    meterMode.setSelectedIndex (juce::jlimit (0, 2, (int) state.getProperty ("meterMode", 0)), juce::dontSendNotification);
    meterMode.onChange = [this] (int index)
    {
        state.setProperty ("meterMode", index, nullptr);
        applyMeterMode();
    };

    resetPeaks.setTooltip (TRANS ("Clear every peak hold"));
    resetPeaks.onClick = [this]
    {
        for (auto& [id, strip] : strips)
            strip->resetPeaks();

        master.resetPeaks();
    };

    for (auto* child : std::initializer_list<juce::Component*> { &meterMode, &resetPeaks, &viewport, &master })
        addAndMakeVisible (child);

    viewport.setViewedComponent (&stripsArea, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (6);

    startTimerHz (30);
    model.addListener (this);
    themeManager.addListener (this);
    refresh();
    applyMeterMode();
}

MixerView::~MixerView()
{
    stopTimer();
    themeManager.removeListener (this);
    model.removeListener (this);
}

void MixerView::showStripMenu (const Strip& strip)
{
    const auto trackId = strip.id;
    const auto isReturn = strip.role == StripRole::returnTrack, isBus = strip.role == StripRole::bus;

    juce::PopupMenu sends, buses;

    // The items call the registry, which outlives this view, never this.
    for (auto& ret : mixer.getReturns())
        sends.addItem (commandItem (commands, cmd::mixerAddSend, { trackId, ret.bus }, ret.name));

    for (auto& bus : mixer.getBuses())
        buses.addItem (commandItem (commands, cmd::mixerMoveToBus, { trackId, bus.trackId }, bus.name));

    // A return doesn't send to returns or join a bus; a Bus sends, but nesting Buses is #37.
    juce::PopupMenu menu;
    menu.addSubMenu (TRANS ("Add Send"), sends, ! isReturn && sends.getNumItems() > 0);

    if (! isBus)
        menu.addSubMenu (TRANS ("Move to Bus"), buses, ! isReturn && buses.getNumItems() > 0);

    menu.addSeparator();
    menu.addItem (commandItem (commands, cmd::mixerAddReturn, { tr ("Return %1", mixer.getReturns().size() + 1) }));
    menu.addItem (commandItem (commands, cmd::mixerAddBus, { tr ("Bus %1", mixer.getBuses().size() + 1) }));
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void MixerView::showEffectPicker (const juce::String& trackId, InsertSlot& slot, const juce::String& replacing)
{
    // Mixer inserts take effects only (PRD §10.6).
    juce::PopupMenu builtIn, external;

    for (auto& info : plugins.getCatalogue())
    {
        if (info.instrument || info.midiEffect)
            continue;

        auto action = [this, trackId, replacing, path = info.path]
        {
            if (replacing.isNotEmpty())
                commands.invoke (cmd::pluginReplace, { trackId, replacing, path });
            else
                commands.invoke (cmd::pluginInsert, { trackId, path, PluginChain::mixer });
        };

        (info.external ? external : builtIn).addItem (info.name, action);
    }

    juce::PopupMenu menu;
    menu.addSectionHeader (replacing.isNotEmpty() ? TRANS ("Replace with") : TRANS ("Add effect"));
    menu.addSubMenu ("Resamper", builtIn);   // the app's own name
    menu.addSubMenu (TRANS ("Plug-Ins"), external, external.getNumItems() > 0);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slot));
}

void MixerView::applySections()
{
    for (auto& chip : sectionChips)
        if (chip.section)
            for (auto& [id, strip] : strips)
                strip->setSectionVisible (*chip.section, chip.button->getToggleState());
}

void MixerView::applyMeterMode()
{
    const auto mode = (MeterMode) meterMode.getSelectedIndex();
    mixer.setMeasuringRms (mode != MeterMode::peak);

    for (auto& [id, strip] : strips)
        strip->setMeterMode (mode);

    master.setMeterMode (mode);
}

void MixerView::setFlowStage (int stage)
{
    if (stage != flowStage)
    {
        flowStage = stage;
        repaint (flowIndicator);
    }
}

void MixerView::timerCallback()
{
    // Hidden strips don't meter (PRD §19).
    if (! isShowing())
    {
        lastMeterTime = 0;
        return;
    }

    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto elapsed = lastMeterTime > 0 ? now - lastMeterTime : 0.0;
    lastMeterTime = now;

    for (auto& [id, strip] : strips)
        strip->setLevel (mixer.getTrackLevel (id), elapsed);

    master.setLevel (mixer.getMasterLevel(), elapsed);
}

void MixerView::refresh()
{
    const auto audioInputs = model.getAudioInputs(), midiInputs = model.getMidiInputs();

    trackOrder.clear();
    returnOrder.clear();
    std::map<juce::String, std::unique_ptr<ChannelStrip>> kept;

    for (auto& stripInfo : mixer.getStrips())
    {
        StripState stripState { stripInfo, stripInfo.kind == TrackKind::midi ? midiInputs : audioInputs };
        const auto id = stripInfo.id;
        (stripState.isReturn() ? returnOrder : trackOrder).push_back (id);

        auto existing = strips.find (id);
        auto strip = existing != strips.end() ? std::move (existing->second) : nullptr;

        if (strip == nullptr)
        {
            strip = std::make_unique<ChannelStrip> (commands, hosting, themeManager, stripInfo.role);
            strip->onTrackChainClicked = [this, id] { if (onShowDeviceChain) onShowDeviceChain (id); };
            strip->onShowMenu = [this, s = strip.get()] { showStripMenu (s->getState().strip); };
            strip->onFlowStageHovered = [this] (int stage) { setFlowStage (stage); };
            strip->onOpenPlugin = [this] (const juce::String& pluginId) { if (onOpenPlugin) onOpenPlugin (pluginId); };
            strip->onPickInsert = [this, id] (InsertSlot& slot, const juce::String& replacing)
            {
                showEffectPicker (id, slot, replacing);
            };
            strip->setMeterMode ((MeterMode) meterMode.getSelectedIndex());

            for (auto& chip : sectionChips)
                if (chip.section)
                    strip->setSectionVisible (*chip.section, chip.button->getToggleState());

            stripsArea.addAndMakeVisible (*strip);
        }

        strip->setState (stripState);
        kept[id] = std::move (strip);
    }

    strips = std::move (kept);
    master.setMaster (mixer.getMaster());
    layoutStrips();
}

void MixerView::layoutStrips()
{
    auto& metrics = themeManager.getMetrics();
    const auto height = juce::jmax (0, viewport.getHeight() - viewport.getScrollBarThickness());
    auto x = stripsPadding;

    auto place = [&] (const std::vector<juce::String>& ids)
    {
        for (auto& id : ids)
            if (auto strip = strips.find (id); strip != strips.end())
            {
                const auto width = strip->second->getState().isBus() ? metrics.stripBusWidth : metrics.stripWidth;
                strip->second->setBounds (x, stripsPadding, width, juce::jmax (0, height - 2 * stripsPadding));
                x += width + stripGap;
            }
    };

    place (trackOrder);

    if (! returnOrder.empty())
    {
        x += groupGap - stripGap;
        place (returnOrder);
    }

    stripsArea.setSize (juce::jmax (viewport.getWidth(), x - stripGap + stripsPadding), height);
}

void MixerView::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.bgDeep);

    auto toolbar = getLocalBounds().removeFromTop (themeManager.getMetrics().toolbarHeight);
    g.setColour (theme.bgPanel);
    g.fillRect (toolbar);
    g.setColour (theme.borderSoft);
    g.fillRect (toolbar.removeFromBottom (1));

    drawStyledText (g, themeManager, TRANS ("Mixer"), titleStyle, titleArea, juce::Justification::centredLeft, theme.textPrimary);
    g.setColour (theme.border);
    g.fillRect (titleDivider);

    // Signal flow: Track chain › Inserts › Sends › Fader in a bg-slot well, the stage under the pointer in lime.
    if (flowIndicator.isEmpty())
        return;

    g.setColour (theme.bgSlot);
    g.fillRoundedRectangle (flowIndicator.toFloat(), 5.0f);
    g.setColour (theme.borderSoft);
    g.drawRoundedRectangle (flowIndicator.toFloat().reduced (0.5f), 5.0f, 1.0f);

    const auto font = themeManager.font (flowStyle);
    g.setFont (font);
    auto r = flowIndicator.reduced (flowPadX, 0);

    for (int i = 0; i < (int) std::size (flowStages); ++i)
    {
        const auto text = TRANS (flowStages[i]);
        g.setColour (i == flowStage ? theme.accent : theme.textSecondary);
        g.drawText (text, r.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (font, text) + 1),
                    juce::Justification::centredLeft, false);

        if (i < (int) std::size (flowStages) - 1)
        {
            r.removeFromLeft (flowGap);
            drawIcon (g, Icon::chevronRight, r.removeFromLeft (flowIcon).withSizeKeepingCentre (flowIcon, flowIcon).toFloat(), theme.textDim);
            r.removeFromLeft (flowGap);
        }
    }
}

int MixerView::flowWidth() const
{
    const auto font = themeManager.font (flowStyle);
    auto width = 2 * flowPadX + ((int) std::size (flowStages) - 1) * (flowIcon + 2 * flowGap);

    for (auto* stage : flowStages)
        width += juce::GlyphArrangement::getStringWidthInt (font, TRANS (stage)) + 1;

    return width;
}

void MixerView::resized()
{
    auto& metrics = themeManager.getMetrics();
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop (metrics.toolbarHeight).reduced (metrics.space2xl, 0);
    const auto rowHeight = metrics.controlMd;
    auto centred = [&] (juce::Rectangle<int> area) { return area.withSizeKeepingCentre (area.getWidth(), rowHeight); };

    // Left: title | section chips, 14 apart; the chips 4 apart.
    titleArea = toolbar.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (themeManager.font (titleStyle), TRANS ("Mixer")) + 1);
    toolbar.removeFromLeft (toolbarGap);
    titleDivider = toolbar.removeFromLeft (1).withSizeKeepingCentre (1, 18);
    toolbar.removeFromLeft (toolbarGap);

    for (auto& chip : sectionChips)
    {
        chip.button->setBounds (centred (toolbar.removeFromLeft (chip.button->getIdealWidth())));
        toolbar.removeFromLeft (metrics.spaceXs);
    }

    resetPeaks.setBounds (centred (toolbar.removeFromRight (resetPeaks.getIdealWidth())));
    toolbar.removeFromRight (metrics.spaceXl);
    meterMode.setBounds (centred (toolbar.removeFromRight (meterMode.getIdealWidth())));
    toolbar.removeFromRight (metrics.spaceXl);

    const auto flow = flowWidth();
    flowIndicator = flow <= toolbar.getWidth() ? centred (toolbar.removeFromRight (flow)) : juce::Rectangle<int>();

    master.setBounds (r.removeFromRight (masterWidth + stripsPadding).reduced (0, stripsPadding).withTrimmedRight (stripsPadding));
    viewport.setBounds (r);
    layoutStrips();
}

} // namespace resamper
