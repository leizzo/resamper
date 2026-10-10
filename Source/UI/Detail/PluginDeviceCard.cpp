#include "PluginDeviceCard.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"
#include "UI/Controls/ContinuousControl.h"
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    // Design: DeviceCard/Plugin. Title bar padding 6 8, gap 7; body padding 8, gap 6;
    // status padding 5 8, gap 8 (3 between an icon and its value).
    constexpr int titleHeight = 35, statusHeight = 20, titlePadding = 8, titleGap = 7, bodyPadding = 8, bodyGap = 6,
                  buttonHeight = 24, pinnedHeaderHeight = 9, rowHeight = 11, rowGap = 6, statusGap = 8, statusIconGap = 3,
                  missingBadgeWidth = 48, cpuPollMs = 500;

    const TypeStyle nameStyle { 10.5f, false, 700 }, vendorStyle { 8.5f, false, 400 }, badgeStyle { 7.5f, true, 600 },
                    buttonStyle { 10.0f, false, 600 }, pinnedHeaderStyle { 7.5f, false, 600, true, 0.6f },
                    rowNameStyle { 9.0f, false, 400 }, rowValueStyle { 8.5f, true, 400 }, statusStyle { 8.0f, true, 400 };

    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));

    void drawDashedOutline (juce::Graphics& g, juce::Rectangle<float> bounds, float radius, juce::Colour colour)
    {
        juce::Path outline, dashed;
        outline.addRoundedRectangle (bounds, radius);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (colour);
        g.fillPath (dashed);
    }
}

//==============================================================================
/** The card's buttons. Framed: bg-elevated, a border, radius 5, an optional
    12 px icon and a 10 / 600 label; on (the window open) it turns accent on a
    10 % accent wash, its icon external-link. Pin: the Pinned Parameters
    header's 9 px pin, accent while learning. */
class PluginDeviceCard::CardButton : public ThemedButton
{
public:
    enum class Kind { framed, pin };

    CardButton (ThemeManager& tm, const juce::String& text, Kind k, std::optional<Icon> i = {})
        : ThemedButton (tm, text), kind (k), icon (i)
    {
        setButtonText (text);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto& theme = themeManager.getTheme();
        const auto on = getToggleState();

        if (kind == Kind::pin)
        {
            const auto colour = on ? theme.accent : highlighted || down ? theme.textSecondary : theme.textDim;
            drawIcon (g, Icon::pin, getLocalBounds().toFloat().withSizeKeepingCentre (9.0f, 9.0f), colour);
            paintFocus (g, 3.0f);
            return;
        }

        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (on ? theme.accent.withAlpha (0.1f) : highlighted || down ? theme.bgHover : theme.bgElevated);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (on ? theme.accentDim : theme.border);
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

        const auto font = themeManager.font (buttonStyle);
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (font, getButtonText());
        const auto iconWidth = icon ? 12 + 6 : 0;
        auto content = getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth(), iconWidth + textWidth + 1), getHeight());

        if (icon)
            drawIcon (g, on ? Icon::externalLink : *icon, content.removeFromLeft (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f),
                      on ? theme.accent : theme.textSecondary);

        content.removeFromLeft (icon ? 6 : 0);
        drawStyledText (g, themeManager, getButtonText(), buttonStyle, content, juce::Justification::centredLeft,
                        on ? theme.accent : theme.textPrimary);
        paintFocus (g, 5.0f);
    }

private:
    Kind kind;
    std::optional<Icon> icon;
};

//==============================================================================
/** One pinned parameter: its name (52 wide), an 82 px mini bar and its mono
    value. Drags horizontally; clicking the value types one. */
class PluginDeviceCard::PinnedParameter : public ContinuousControl
{
public:
    PinnedParameter (ThemeManager& tm, ContinuousValue::Spec spec, juce::String parameterName)
        : ContinuousControl (tm, std::move (spec), Axis::horizontal), name (std::move (parameterName))
    {
        setComponentID ("pinned");
        setTitle (name);
    }

    juce::String parameterId;

    void paint (juce::Graphics& g) override
    {
        auto& theme = themeManager.getTheme();
        auto r = getLocalBounds();
        drawStyledText (g, themeManager, name, rowNameStyle, r.removeFromLeft (nameWidth), juce::Justification::centredLeft,
                        theme.textSecondary);
        r.removeFromLeft (gap);

        const auto bar = r.removeFromLeft (barWidth).toFloat().withSizeKeepingCentre ((float) barWidth, 4.0f);
        g.setColour (theme.bgSlot);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (isMouseOverOrDragging() ? theme.textPrimary : theme.textSecondary);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) getModel().getProportion()), 2.0f);

        if (! isEditingText())
            drawNumber (g, themeManager, getModel().getText(), rowValueStyle, getReadoutBounds(),
                        juce::Justification::centredRight, theme.textPrimary);
    }

protected:
    juce::Rectangle<int> getReadoutBounds() const override
    {
        return getLocalBounds().withTrimmedLeft (nameWidth + gap + barWidth + gap);
    }

private:
    static constexpr int nameWidth = 52, barWidth = 82, gap = 6;
    juce::String name;
};

//==============================================================================
PluginDeviceCard::PluginDeviceCard (CommandRegistry& c, const PluginRack& r, const PluginHosting& h, ThemeManager& tm,
                                    const juce::String& track, const PluginInfo& info)
    : DeviceCard (c, r, tm, track, info),
      hosting (h),
      hostingState (h.getState (info.id)),
      power (tm, DevicePowerButton::Style::plugin),
      openWindow (std::make_unique<CardButton> (tm, TRANS ("Open plug-in window"), CardButton::Kind::framed, Icon::appWindow)),
      locate (std::make_unique<CardButton> (tm, TRANS ("Locate"), CardButton::Kind::framed)),
      replace (std::make_unique<CardButton> (tm, TRANS ("Replace"), CardButton::Kind::framed)),
      reload (std::make_unique<CardButton> (tm, TRANS ("Reload"), CardButton::Kind::framed)),
      pinLearn (std::make_unique<CardButton> (tm, TRANS ("Pin"), CardButton::Kind::pin))
{
    setComponentID ("DeviceCard/Plugin");
    openWindow->setComponentID ("openWindow");
    locate->setComponentID ("locate");
    replace->setComponentID ("replace");
    reload->setComponentID ("reload");
    pinLearn->setComponentID ("pinLearn");
    locate->setTooltip (TRANS ("Point at the plug-in's file"));
    replace->setTooltip (TRANS ("Put another plug-in in its place"));
    reload->setTooltip (TRANS ("Start the plug-in again, from its last saved state"));
    pinLearn->setTooltip (TRANS ("Pin parameters: touch them in the plug-in's window"));

    power.onClick = [this] { toggleBypass(); };
    openWindow->onClick = [this] { if (onOpenEditor) onOpenEditor(); };
    locate->onClick = [this] { commands.invoke (cmd::pluginLocate, { trackId, plugin.id }); };
    replace->onClick = [this] { showReplaceMenu(); };
    reload->onClick = [this] { commands.invoke (cmd::pluginReload, { trackId, plugin.id }); };
    pinLearn->onClick = [this] { setLearningPins (! isLearningPins()); };

    addAndMakeVisible (power);

    for (auto* b : { openWindow.get(), locate.get(), replace.get(), reload.get(), pinLearn.get() })
        addChildComponent (b);

    hosting.addListener (this);
    timerCallback();
    startTimer (cpuPollMs);
}

PluginDeviceCard::~PluginDeviceCard()
{
    hosting.removeListener (this);
}

void PluginDeviceCard::hostingStateChanged (const juce::String& pluginId, const HostingState& state)
{
    if (pluginId != plugin.id)
        return;

    hostingState = state;
    setState (plugin);
}

void PluginDeviceCard::setState (const PluginInfo& info)
{
    plugin = info;
    setTitle (plugin.name);
    // Never colour-only (§18): the vendor and the format are always said.
    const auto vendor = plugin.manufacturer.isNotEmpty() ? plugin.manufacturer : TRANS ("Unknown vendor");
    setDescription (vendor + " " + middleDot + " "
                    + (isMissing() ? tr ("%1 plug-in, missing", plugin.formatBadge())
                       : isCrashed() ? tr ("%1 plug-in, crashed", plugin.formatBadge())
                                     : tr ("%1 plug-in", plugin.formatBadge())));
    setAlpha (plugin.enabled ? 1.0f : 0.5f);
    power.setToggleState (plugin.enabled, juce::dontSendNotification);

    openWindow->setVisible (! isMissing() && ! isCrashed());
    pinLearn->setVisible (! isMissing() && ! isCrashed());
    locate->setVisible (isMissing());
    replace->setVisible (isMissing());
    reload->setVisible (isCrashed());

    if (isMissing() || isCrashed() || plugin.pinnedParameters.size() >= PluginRack::maxPinnedParameters)
        setLearningPins (false);

    rebuildPins();
    resized();
    repaint();
}

void PluginDeviceCard::setLearningPins (bool learn)
{
    touchWatch.reset();

    if (learn && ! isMissing() && ! isCrashed() && plugin.pinnedParameters.size() < PluginRack::maxPinnedParameters)
    {
        // Pinned after the touch's callback returns: the fourth pin ends learning, which deletes the watch.
        touchWatch = rack.watchTouches (plugin.id, [this] (const juce::String& parameterId)
        {
            juce::MessageManager::callAsync ([card = juce::Component::SafePointer<PluginDeviceCard> (this), parameterId]
            {
                if (card != nullptr && card->isLearningPins())
                    card->commands.invoke (cmd::pluginSetPinned, { card->plugin.id, parameterId, true });
            });
        });

        // The parameters are touched in the plug-in's own window.
        if (touchWatch != nullptr && ! windowOpen && onOpenEditor)
            onOpenEditor();
    }

    pinLearn->setToggleState (isLearningPins(), juce::dontSendNotification);
    repaint (pinnedHeader());
}

void PluginDeviceCard::rebuildPins()
{
    const auto parameters = rack.getParameters (plugin.id);
    std::vector<const PluginParameter*> pinned;

    for (auto& id : plugin.pinnedParameters)
        for (auto& p : parameters)
            if (p.id == id)
                pinned.push_back (&p);

    bool same = pinned.size() == pins.size();

    for (size_t i = 0; same && i < pinned.size(); ++i)
        same = pins[i]->parameterId == pinned[i]->id;

    if (! same)
    {
        pins.clear();

        for (auto* p : pinned)
        {
            auto row = std::make_unique<PinnedParameter> (themeManager, specFor (*p), p->name);
            row->parameterId = p->id;
            row->onChange = setterFor (p->id);
            addAndMakeVisible (*row);
            pins.push_back (std::move (row));
        }
    }

    for (size_t i = 0; i < pins.size(); ++i)
        pins[i]->setValue (pinned[i]->value);
}

void PluginDeviceCard::setWindowOpen (bool open)
{
    windowOpen = open;
    openWindow->setToggleState (open, juce::dontSendNotification);
    openWindow->setButtonText (open ? tr ("Window open %1 focus", middleDot) : TRANS ("Open plug-in window"));
    repaint();
}

juce::Rectangle<int> PluginDeviceCard::getTitleBar() const
{
    return getLocalBounds().removeFromTop (titleHeight);
}

juce::Rectangle<int> PluginDeviceCard::body() const
{
    return getLocalBounds().withTrimmedTop (titleHeight).withTrimmedBottom (statusHeight).reduced (bodyPadding);
}

juce::Rectangle<int> PluginDeviceCard::pinnedHeader() const
{
    return body().withTrimmedTop (buttonHeight + bodyGap).removeFromTop (pinnedHeaderHeight);
}

void PluginDeviceCard::resized()
{
    power.setBounds (getTitleBar().removeFromLeft (titlePadding + 14).withTrimmedLeft (titlePadding).withSizeKeepingCentre (14, 14));

    auto area = body();
    auto buttons = area.removeFromTop (buttonHeight);

    if (isMissing())
    {
        // The Missing badge sits left of Locate and Replace.
        buttons.removeFromLeft (missingBadgeWidth + bodyGap);
        const auto half = (buttons.getWidth() - bodyGap) / 2;
        locate->setBounds (buttons.removeFromLeft (half));
        replace->setBounds (buttons.removeFromRight (half));
    }
    else if (isCrashed())
    {
        // The Crashed badge sits left of Reload.
        reload->setBounds (buttons.withTrimmedLeft (missingBadgeWidth + bodyGap));
    }
    else
    {
        openWindow->setBounds (buttons);
    }

    area.removeFromTop (bodyGap);
    // The pin's 9 px glyph sits right-aligned in the header, in a larger target.
    pinLearn->setBounds (area.removeFromTop (pinnedHeaderHeight).removeFromRight (9).expanded (4));
    area.removeFromTop (bodyGap);

    // Four rows only fit if their gaps close up.
    const auto rows = (int) pins.size();
    const auto gap = rows > 1 ? juce::jlimit (0, rowGap, (area.getHeight() - rows * rowHeight) / (rows - 1)) : rowGap;

    for (auto& row : pins)
    {
        row->setBounds (area.removeFromTop (rowHeight));
        area.removeFromTop (gap);
    }
}

void PluginDeviceCard::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = theme.radiusXl;

    g.setColour (theme.bgTrack);
    g.fillRoundedRectangle (bounds, radius);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, radius);
        g.reduceClipRegion (clip);

        // A neutral title bar with a bottom border; the status bar with a top one.
        auto title = getTitleBar();
        g.setColour (theme.bgElevated);
        g.fillRect (title);
        g.setColour (theme.border);
        g.fillRect (title.removeFromBottom (1));

        auto status = getLocalBounds().removeFromBottom (statusHeight);
        g.setColour (theme.bgSlot);
        g.fillRect (status);
        g.setColour (theme.borderSoft);
        g.fillRect (status.removeFromTop (1));
    }

    // Title bar: power, plug, name over vendor, format badge.
    auto title = getTitleBar().withTrimmedLeft (power.getRight() + titleGap).withTrimmedRight (titlePadding);
    drawIcon (g, Icon::plug, title.removeFromLeft (11).toFloat().withSizeKeepingCentre (11.0f, 11.0f), theme.textSecondary);
    title.removeFromLeft (titleGap);

    const auto badgeText = plugin.formatBadge();
    const auto badgeWidth = juce::GlyphArrangement::getStringWidthInt (themeManager.font (badgeStyle), badgeText) + 2 * 4 + 2;
    const auto badge = title.removeFromRight (badgeWidth).withSizeKeepingCentre (badgeWidth, 11);
    g.setColour (theme.border);
    g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), 3.0f, 1.0f);
    drawNumber (g, themeManager, badgeText, badgeStyle, badge, juce::Justification::centred, theme.textSecondary);
    title.removeFromRight (titleGap);

    auto titles = title.withSizeKeepingCentre (title.getWidth(), 23);
    drawStyledText (g, themeManager, plugin.name, nameStyle, titles.removeFromTop (13), juce::Justification::centredLeft,
                    theme.textPrimary);
    drawStyledText (g, themeManager, plugin.manufacturer.isNotEmpty() ? plugin.manufacturer : TRANS ("Unknown vendor"),
                    vendorStyle, titles, juce::Justification::centredLeft, theme.textDim);

    // Body: the Missing or Crashed badge, or the Pinned Parameters header.
    if (isMissing() || isCrashed())
    {
        const auto badgeArea = body().removeFromTop (buttonHeight).removeFromLeft (missingBadgeWidth)
                                     .withSizeKeepingCentre (missingBadgeWidth, 14);
        g.setColour (theme.rec.withAlpha (0.2f));
        g.fillRoundedRectangle (badgeArea.toFloat(), 3.0f);
        drawStyledText (g, themeManager, isMissing() ? TRANS ("Missing") : TRANS ("Crashed"), theme.micro, badgeArea,
                        juce::Justification::centred, theme.rec);
    }
    else
    {
        const auto learning = isLearningPins();
        drawStyledText (g, themeManager, learning ? TRANS ("Touch a control to pin") : TRANS ("Pinned parameters"), pinnedHeaderStyle,
                        pinnedHeader().withTrimmedRight (9 + bodyGap), juce::Justification::centredLeft,
                        learning ? theme.accent : theme.textDim);
    }

    // Status: CPU, reported latency, sandbox; each a 9 px icon and a mono value.
    auto status = getLocalBounds().removeFromBottom (statusHeight).withTrimmedTop (1).reduced (titlePadding, 0);
    auto item = [&] (Icon icon, juce::Colour iconColour, const juce::String& text)
    {
        drawIcon (g, icon, status.removeFromLeft (9).toFloat().withSizeKeepingCentre (9.0f, 9.0f), iconColour);
        status.removeFromLeft (statusIconGap);
        const auto w = juce::GlyphArrangement::getStringWidthInt (themeManager.font (statusStyle), text) + 1;
        drawNumber (g, themeManager, text, statusStyle, status.removeFromLeft (w), juce::Justification::centredLeft, theme.textDim);
        status.removeFromLeft (statusGap);
    };

    item (Icon::cpu, theme.textDim, cpuText);
    item (Icon::timer, theme.textDim, tr ("%1 smp", plugin.latencySamples));
    item (Icon::shieldCheck, hostingState.kind == HostingState::Kind::sandboxed ? theme.meterLow : theme.textDim, [&]() -> juce::String
    {
        switch (hostingState.kind)
        {
            case HostingState::Kind::loading:     return TRANS ("loading");
            case HostingState::Kind::sandboxed:
            case HostingState::Kind::crashed:     return TRANS ("sandbox");
            case HostingState::Kind::inProcess:   return TRANS ("in-process");
            case HostingState::Kind::failed:      return TRANS ("failed");
            case HostingState::Kind::missing:     return TRANS ("missing");
        }

        return {};
    }());

    // Outline: red dashes when missing, red when crashed, 1.5 px accent-dim while the window is open.
    if (isMissing())
    {
        drawDashedOutline (g, bounds.reduced (1.0f), radius, theme.rec);
    }
    else if (isCrashed())
    {
        g.setColour (theme.rec);
        g.drawRoundedRectangle (bounds.reduced (0.75f), radius, 1.5f);
    }
    else if (windowOpen)
    {
        g.setColour (theme.accentDim);
        g.drawRoundedRectangle (bounds.reduced (0.75f), radius, 1.5f);
    }
    else
    {
        g.setColour (theme.border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }
}

void PluginDeviceCard::timerCallback()
{
    // The latency changes without an Edit change: polled, as the CPU is.
    if (auto info = rack.getPlugin (plugin.id); info.has_value() && info->latencySamples != plugin.latencySamples)
        setState (*info);

    auto text = juce::String (rack.getCpuLoad (plugin.id) * 100.0, 1) + "%";

    if (text != cpuText)
    {
        cpuText = text;
        repaint (getLocalBounds().removeFromBottom (statusHeight));
    }
}

void PluginDeviceCard::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Opens or focuses the window; a plug-in card never collapses.
    if (getTitleBar().contains (e.getPosition()) && ! power.getBounds().contains (e.getPosition()) && ! isMissing()
        && ! isCrashed() && onOpenEditor)
        onOpenEditor();
}

void PluginDeviceCard::addMenuItems (juce::PopupMenu& menu)
{
    menu.addItem (TRANS ("Open Plug-in Window"), ! isMissing() && ! isCrashed(), false, [this] { if (onOpenEditor) onOpenEditor(); });

    if (isCrashed())
        menu.addItem (TRANS ("Reload"), [this] { commands.invoke (cmd::pluginReload, { trackId, plugin.id }); });

    juce::PopupMenu pinMenu;
    const auto full = plugin.pinnedParameters.size() >= PluginRack::maxPinnedParameters;

    for (auto& p : rack.getParameters (plugin.id))
    {
        const auto pinned = plugin.pinnedParameters.contains (p.id);
        pinMenu.addItem (p.name, pinned || ! full, pinned,
                         [this, id = p.id, pinned] { commands.invoke (cmd::pluginSetPinned, { plugin.id, id, ! pinned }); });
    }

    menu.addSubMenu (TRANS ("Pin Parameter"), pinMenu, pinMenu.getNumItems() > 0);
    menu.addItem (TRANS ("Pin by Touching in Window"), ! isMissing() && ! isCrashed() && ! full, isLearningPins(),
                  [this] { setLearningPins (! isLearningPins()); });
    menu.addSeparator();
}

void PluginDeviceCard::showReplaceMenu()
{
    juce::PopupMenu menu;

    for (auto& candidate : rack.getCatalogue())
        if (candidate.instrument == plugin.instrument && ! candidate.midiEffect)
            menu.addItem (candidate.name + (candidate.external ? "  (" + candidate.manufacturer + ")" : juce::String()),
                          [this, path = candidate.path] { commands.invoke (cmd::pluginReplace, { trackId, plugin.id, path }); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (replace.get()));
}

} // namespace resamper
