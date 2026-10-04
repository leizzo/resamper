#include "InsertSlot.h"
#include "UI/Controls/Icons.h"

namespace resamper
{

namespace
{
    // Design: InsertSlot/Filled pads 0 7 with a 6 px gap; InsertSlot/Plugin pads 0 6 with 5, a 9 px plug, a 7 px mono format.
    constexpr int filledPadding = 7, filledGap = 6, pluginPadding = 6, pluginGap = 5, ledSize = 6, plugSize = 9, badgeWidth = 40;

    /** How far the power LED's click target reaches past the LED on each side. */
    constexpr int ledHitSlop = 3;
    const TypeStyle nameStyle { 10.0f, false, 400 }, formatStyle { 7.0f, true, 400 };
}

InsertSlot::InsertSlot (ThemeManager& tm, const PluginHosting& h, int i) : themeManager (tm), hosting (h), index (i)
{
    setRepaintsOnMouseActivity (true);
    setTitle ("Insert " + juce::String (i + 1));
    hosting.addListener (this);
}

InsertSlot::~InsertSlot()
{
    hosting.removeListener (this);
}

void InsertSlot::setPlugin (std::optional<PluginInfo> p)
{
    plugin = std::move (p);
    hostingState = plugin ? hosting.getState (plugin->id) : HostingState();
    updateTooltip();
    repaint();
}

void InsertSlot::hostingStateChanged (const juce::String& pluginId, const HostingState& state)
{
    if (! plugin || pluginId != plugin->id)
        return;

    hostingState = state;
    updateTooltip();
    repaint();
}

void InsertSlot::updateTooltip()
{
    if (! plugin)
        setTooltip ("Empty insert: click to add an effect, or drop one here");
    else if (plugin->external)
        setTooltip (plugin->name + " (" + plugin->formatBadge() + " plug-in" + (isMissing() ? ", missing)" : "): click to open its window"));
    else
        setTooltip (plugin->name + ": click to open its editor");

    setDescription (getTooltip());
}

InsertSlot::Look InsertSlot::getLook() const noexcept
{
    return ! plugin ? Look::empty : plugin->external ? Look::plugin : Look::filled;
}

void InsertSlot::setDropHighlight (std::optional<bool> valid)
{
    dropHighlight = valid;
    setMouseCursor (valid.has_value() && ! *valid ? notAllowedCursor() : juce::MouseCursor::NormalCursor);
    repaint();
}

juce::Rectangle<int> InsertSlot::powerBounds() const
{
    return getLocalBounds().removeFromLeft (filledPadding + ledSize + ledHitSlop).withTrimmedLeft (filledPadding - ledHitSlop);
}

void InsertSlot::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = theme.radiusMd;
    const auto hovered = isMouseOver (true);

    g.setColour (plugin ? (hovered ? theme.bgHover : theme.bgElevated) : (hovered ? theme.bgTrack : theme.bgSlot));
    g.fillRoundedRectangle (bounds, radius);

    if (plugin)
    {
        const auto on = plugin->enabled;
        const auto isPlugin = getLook() == Look::plugin;
        const auto gap = isPlugin ? pluginGap : filledGap;
        auto r = getLocalBounds().reduced (isPlugin ? pluginPadding : filledPadding, 0);
        g.setColour (isMissing() ? theme.rec : on ? theme.accent : theme.textDim);
        g.fillEllipse (r.removeFromLeft (ledSize).withSizeKeepingCentre (ledSize, ledSize).toFloat());
        r.removeFromLeft (gap);

        if (isMissing())
        {
            auto badge = r.removeFromRight (badgeWidth).withSizeKeepingCentre (badgeWidth, 12);
            g.setColour (theme.rec.withAlpha (0.2f));
            g.fillRoundedRectangle (badge.toFloat(), theme.radiusSm);
            drawStyledText (g, themeManager, "Missing", theme.micro, badge, juce::Justification::centred, theme.rec);
            r.removeFromRight (gap);
        }

        // A plug-in: the plug icon and its format, at the right of the name.
        if (isPlugin)
        {
            const auto format = plugin->formatBadge();
            const auto formatWidth = juce::GlyphArrangement::getStringWidthInt (themeManager.font (formatStyle), format) + 1;
            drawNumber (g, themeManager, format, formatStyle, r.removeFromRight (formatWidth), juce::Justification::centredRight,
                        theme.textDim);
            r.removeFromRight (gap);
            drawIcon (g, Icon::plug, r.removeFromRight (plugSize).toFloat().withSizeKeepingCentre ((float) plugSize, (float) plugSize),
                      theme.textDim);
            r.removeFromRight (gap);
        }

        drawStyledText (g, themeManager, plugin->name, nameStyle, r, juce::Justification::centredLeft,
                        on ? theme.textPrimary : theme.textDim);

        if (isMissing())
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle (bounds.reduced (0.75f), radius);
            const float dashes[] = { 3.0f, 2.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (theme.rec);
            g.fillPath (dashed);
        }
    }

    if (dropHighlight.has_value() && *dropHighlight)
    {
        g.setColour (theme.accentDim);
        g.drawRoundedRectangle (bounds.reduced (1.0f), radius, 2.0f);
    }
}

void InsertSlot::mouseDown (const juce::MouseEvent& e)
{
    dragStarted = false;

    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu (e);

        return;
    }

    if (plugin && powerBounds().contains (e.getPosition()))
    {
        if (onPowerClick)
            onPowerClick (e);

        return;
    }
}

void InsertSlot::mouseUp (const juce::MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown()
        || (plugin && powerBounds().contains (e.getMouseDownPosition())))
        return;

    if (onClick)
        onClick (e);
}

void InsertSlot::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragStarted && plugin && e.getDistanceFromDragStart() > 4 && ! powerBounds().contains (e.getMouseDownPosition()))
    {
        dragStarted = true;

        if (onDrag)
            onDrag (e);
    }
}

} // namespace resamper
