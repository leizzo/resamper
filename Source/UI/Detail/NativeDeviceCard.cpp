#include "NativeDeviceCard.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"

namespace resamper
{

namespace
{
    // Design: DeviceCard/Native body (padding 14 / 16, gap 14) and `Knob` (a 30 px dial, 44 wide).
    constexpr int knobWidth = 44, knobGap = 14, bodyPaddingX = 16, bodyPaddingY = 14, dialSize = 30,
                  knobHeight = dialSize + 4 + 2 * 13;

    // Design: DeviceHeader (padding 0 6 0 8, gap 6, title 11 / 700, parts 18 high).
    constexpr int headerLeft = 8, headerRight = 6, headerGap = 6, powerSize = 14, partHeight = 18, minNameWidth = 48,
                  minPresetWidth = 56;
    const TypeStyle titleStyle { 11.0f, false, 700 };

    // Design: Device/Folded (padding 6 0, gap 8: a 3 px stripe, power, a 100 px name, the Mods indicator).
    constexpr int foldedPadding = 6, foldedGap = 8, stripeHeight = 3, foldedNameHeight = 100, foldedModsSize = 10;

    // Design: the v2 devices' colours, as track palette entries (EQ Eight clip-arp, Compressor clip-pads).
    constexpr int eqEightPaletteIndex = 4, compressorPaletteIndex = 3;
    const TypeStyle foldedNameStyle { 10.0f, false, 700 };

    int zoneWidth (int columns)
    {
        return columns > 0 ? columns * knobWidth + (columns - 1) * knobGap : 0;
    }

    const char* modsTooltip = NEEDS_TRANS ("Modulators: the Mods Drawer is coming");
}

NativeDeviceCard::NativeDeviceCard (CommandRegistry& c, const PluginRack& r, ThemeManager& tm, const juce::String& track,
                                    const PluginInfo& info)
    : DeviceCard (c, r, tm, track, info),
      power (tm, DevicePowerButton::Style::native),
      preset (tm, TRANS ("Preset: presets are coming with the preset browser"), DeviceHeaderButton::Kind::preset),
      ab (tm, TRANS ("A/B compare is coming"), DeviceHeaderButton::Kind::abCompare),
      mods (tm, TRANS (modsTooltip), DeviceHeaderButton::Kind::mods),
      fold (tm, TRANS ("Fold"), DeviceHeaderButton::Kind::icon, Icon::foldVertical),
      expand (tm, TRANS ("Expand"), DeviceHeaderButton::Kind::icon, Icon::maximize2),
      options (tm, TRANS ("Options"), DeviceHeaderButton::Kind::icon, Icon::ellipsis)
{
    setComponentID ("DeviceCard/Native");
    setDescription (TRANS ("Native device"));

    preset.setComponentID ("preset");
    ab.setComponentID ("ab");
    mods.setComponentID ("mods");
    fold.setComponentID ("fold");
    expand.setComponentID ("expand");
    options.setComponentID ("options");

    preset.setButtonText (TRANS ("Default"));
    mods.setButtonText ("0");

    // Until they land, these show their state but can't be used.
    preset.setEnabled (false);
    ab.setEnabled (false);
    mods.setEnabled (false);

    power.onClick = [this] { toggleBypass(); };
    fold.onClick = [this] { if (onSizeChange) onSizeChange (toggledSize (size, DeviceSize::folded)); };
    expand.onClick = [this] { if (onSizeChange) onSizeChange (toggledSize (size, DeviceSize::expanded)); };
    options.onClick = [this] { showMenu(); };

    for (auto* b : std::initializer_list<juce::Component*> { &power, &preset, &ab, &mods, &fold, &expand, &options })
        addAndMakeVisible (b);

    if ((body = DeviceBody::create (c, r, tm, info)))
        addAndMakeVisible (*body);

    rebuild (rack.getParameters (plugin.id));
}

void NativeDeviceCard::rebuild (const std::vector<PluginParameter>& list)
{
    if (body != nullptr)
    {
        body->setParameters (list, plugin.enabled, colour);
        resized();
        return;
    }

    std::vector<juce::String> ids;

    for (auto& p : list)
        ids.push_back (p.id);

    if (ids != parameterIds)
    {
        parameters.clear();
        parameterIds = ids;

        for (auto& p : list)
        {
            // A device without a body of its own shows its parameters' own names, untranslated.
            auto knob = std::make_unique<Knob> (themeManager, specFor (p), p.name);
            knob->setComponentID (p.id);
            knob->setDialSize (dialSize);
            knob->setTooltip (p.name);
            knob->onChange = setterFor (p.id);
            addChildComponent (*knob);
            parameters.push_back ({ p.id, p.output, std::move (knob) });
        }

        // Controls first, then the Output zone, each in the device's own order.
        std::stable_partition (parameters.begin(), parameters.end(), [] (const Parameter& p) { return ! p.output; });
    }

    for (auto& p : list)
        for (auto& shown : parameters)
            if (shown.id == p.id)
            {
                shown.knob->setValue (p.value);
                shown.knob->setAutomated (p.automated);
                shown.knob->setDimmed (! plugin.enabled);
                shown.knob->setArcColour (colour);
            }

    resized();
}

void NativeDeviceCard::setFloating (bool b)
{
    floating = b;
    setState (plugin);
}

void NativeDeviceCard::setState (const PluginInfo& info)
{
    auto& theme = themeManager.getTheme();
    plugin = info;
    size = floating ? DeviceSize::expanded : plugin.size;
    // A stable pick from the track palette by device type; the v2 devices take the design's.
    if (plugin.path == NativeDevices::eqEightType)
        colour = theme.trackColour (eqEightPaletteIndex);
    else if (plugin.path == NativeDevices::compressorType)
        colour = theme.trackColour (compressorPaletteIndex);
    else
        colour = theme.trackColour ((int) ((juce::uint32) (plugin.manufacturer + "/" + plugin.name).hashCode()
                                           % (juce::uint32) theme.trackPalette.size()));
    setTitle (plugin.name);
    setAlpha (plugin.enabled ? 1.0f : 0.5f);

    const auto folded = size == DeviceSize::folded;
    power.setToggleState (plugin.enabled, juce::dontSendNotification);
    power.setDeviceColour (colour);
    power.setStyle (folded ? DevicePowerButton::Style::folded : DevicePowerButton::Style::native);
    mods.setKind (folded ? DeviceHeaderButton::Kind::foldedMods : DeviceHeaderButton::Kind::mods);
    expand.setIcon (size == DeviceSize::expanded ? Icon::minimize2 : Icon::maximize2);
    expand.setTooltip (size == DeviceSize::expanded ? TRANS ("Compact") : TRANS ("Expand"));

    rebuild (rack.getParameters (plugin.id));
    repaint();
}

int NativeDeviceCard::columns (int knobs) const
{
    // Expanded stacks two rows; compact is one.
    return size == DeviceSize::expanded ? (knobs + 1) / 2 : knobs;
}

std::vector<NativeDeviceCard::Parameter*> NativeDeviceCard::shownParameters (bool output)
{
    std::vector<Parameter*> shown;

    for (auto& p : parameters)
        if (p.output == output && (output || size == DeviceSize::expanded || (int) shown.size() < maxCompactControls))
            shown.push_back (&p);

    return shown;
}

int NativeDeviceCard::minHeaderWidth() const
{
    const auto parts = ab.getIdealWidth() + mods.getIdealWidth() + 3 * fold.getIdealWidth();
    return headerLeft + powerSize + minNameWidth + minPresetWidth + parts + 7 * headerGap + headerRight;
}

int NativeDeviceCard::getPreferredWidth (int dockedWidth) const
{
    if (size == DeviceSize::folded)
        return foldedWidth;

    if (body != nullptr)
        return juce::jmax (minHeaderWidth(), body->getPreferredWidth (size == DeviceSize::expanded, dockedWidth));

    int controls = 0, outputs = 0;

    for (auto& p : parameters)
        ++(p.output ? outputs : controls);

    if (size == DeviceSize::compact)
        controls = juce::jmin (controls, maxCompactControls);

    const auto divider = controls > 0 && outputs > 0 ? 2 * knobGap + 1 : 0;
    const auto zones = 2 * bodyPaddingX + zoneWidth (columns (controls)) + divider + zoneWidth (columns (outputs));
    return juce::jmax (minHeaderWidth(), zones, size == DeviceSize::expanded ? dockedWidth : 0);
}

void NativeDeviceCard::focusFirstControl()
{
    if (body != nullptr && body->isShowing())
    {
        body->focusFirstControl();
        return;
    }

    // Only a card on screen can take focus.
    for (auto& p : parameters)
        if (p.knob->isShowing())
        {
            p.knob->grabKeyboardFocus();
            return;
        }
}

juce::Rectangle<int> NativeDeviceCard::getTitleBar() const
{
    return size == DeviceSize::folded ? getLocalBounds() : getLocalBounds().removeFromTop (headerHeight);
}

void NativeDeviceCard::addMenuItems (juce::PopupMenu& menu)
{
    auto resize = [this] (DeviceSize s) { return [this, s] { if (onSizeChange) onSizeChange (s); }; };
    if (floating)
        return;

    menu.addItem (TRANS ("Fold"), true, size == DeviceSize::folded, resize (toggledSize (size, DeviceSize::folded)));
    menu.addItem (TRANS ("Expand"), true, size == DeviceSize::expanded, resize (toggledSize (size, DeviceSize::expanded)));
    menu.addItem (TRANS ("Open in Window"), [this] { if (onFloat) onFloat(); });
    menu.addSeparator();
}

juce::Rectangle<int> NativeDeviceCard::nameArea() const
{
    if (size == DeviceSize::folded)
        return { 0, power.getBottom() + foldedGap, getWidth(), foldedNameHeight };

    const auto width = juce::GlyphArrangement::getStringWidthInt (themeManager.font (titleStyle), plugin.name);
    const auto left = power.getRight() + headerGap;
    return { left, 0, juce::jmax (0, juce::jmin (width, preset.getX() - headerGap - left)), headerHeight };
}

void NativeDeviceCard::resized()
{
    const auto folded = size == DeviceSize::folded;

    for (auto* b : std::initializer_list<juce::Component*> { &preset, &ab, &options })
        b->setVisible (! folded);

    // A floating device is always expanded: nothing to fold or expand.
    fold.setVisible (! folded && ! floating);
    expand.setVisible (! folded && ! floating);

    for (auto& p : parameters)
        p.knob->setVisible (false);

    dividerX = -1;

    if (body != nullptr)
    {
        body->setVisible (! folded);
        body->setBounds (getLocalBounds().withTrimmedTop (headerHeight));
    }

    if (folded)
    {
        // Device/Folded: stripe, power, name, Mods, top to bottom from the 6 px padding.
        auto column = getLocalBounds().reduced (0, foldedPadding);
        column.removeFromTop (stripeHeight + foldedGap);
        power.setBounds (column.removeFromTop (powerSize).withSizeKeepingCentre (powerSize + 4, powerSize));
        column.removeFromTop (foldedGap + foldedNameHeight + foldedGap);
        mods.setBounds (column.removeFromTop (foldedModsSize + 4).withSizeKeepingCentre (foldedModsSize + 4, foldedModsSize + 4));
        return;
    }

    // DeviceHeader, left to right: power, name, preset (takes what's left), A/B, Mods, fold, expand, options.
    auto header = getLocalBounds().removeFromTop (headerHeight).withTrimmedLeft (headerLeft).withTrimmedRight (headerRight);
    auto place = [&] (juce::Component& c, int width, bool fromRight)
    {
        auto slot = fromRight ? header.removeFromRight (width) : header.removeFromLeft (width);
        c.setBounds (slot.withSizeKeepingCentre (width, partHeight));
        fromRight ? header.removeFromRight (headerGap) : header.removeFromLeft (headerGap);
    };

    // An icon's 11 px glyph sits in a 15 px button: the gap between buttons shrinks to keep the design's 6 px between glyphs.
    const auto iconSlack = fold.getIdealWidth() - 11;

    for (auto* b : { &options, &expand, &fold })
    {
        if (! b->isVisible())
            continue;

        b->setBounds (header.removeFromRight (b->getIdealWidth()).withSizeKeepingCentre (b->getIdealWidth(), partHeight));
        header.removeFromRight (headerGap - iconSlack);
    }

    header.removeFromRight (iconSlack);
    place (mods, mods.getIdealWidth(), true);
    place (ab, ab.getIdealWidth(), true);
    place (power, powerSize, false);

    const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (themeManager.font (titleStyle), plugin.name);
    header.removeFromLeft (juce::jmin (nameWidth, juce::jmax (minNameWidth, header.getWidth() - minPresetWidth - headerGap)));
    header.removeFromLeft (headerGap);
    preset.setBounds (header.withSizeKeepingCentre (header.getWidth(), partHeight));

    // Zones: Controls, then the divider and Output.
    auto zoneArea = getLocalBounds().withTrimmedTop (headerHeight).reduced (bodyPaddingX, 0);
    const auto rows = size == DeviceSize::expanded ? 2 : 1;
    const auto top = headerHeight + (getHeight() - headerHeight - rows * knobHeight) / 2;

    auto placeZone = [&] (const std::vector<Parameter*>& zone)
    {
        const auto cols = columns ((int) zone.size());

        for (size_t i = 0; i < zone.size(); ++i)
        {
            const auto col = (int) i % juce::jmax (1, cols), row = (int) i / juce::jmax (1, cols);
            zone[i]->knob->setBounds (zoneArea.getX() + col * (knobWidth + knobGap), top + row * knobHeight, knobWidth, knobHeight);
            zone[i]->knob->setVisible (true);
        }

        zoneArea.removeFromLeft (zoneWidth (cols));
    };

    const auto controls = shownParameters (false), outputs = shownParameters (true);
    placeZone (controls);

    if (! outputs.empty())
    {
        // Output hugs the right edge; the card is always wide enough for the divider's gaps.
        zoneArea.removeFromLeft (juce::jmax (0, zoneArea.getWidth() - zoneWidth (columns ((int) outputs.size()))));

        if (! controls.empty())
            dividerX = zoneArea.getX() - knobGap - 1;

        placeZone (outputs);
    }
}

void NativeDeviceCard::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = theme.radiusXl;
    const auto folded = size == DeviceSize::folded;

    g.setColour (theme.bgTrack);
    g.fillRoundedRectangle (bounds, radius);

    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (bounds, radius);
        g.reduceClipRegion (clip);
        g.setColour (colour);

        // Folded: a stripe of the device colour; otherwise the header filled with it.
        if (folded)
            g.fillRect (0, foldedPadding, getWidth(), stripeHeight);
        else
            g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    }

    const auto name = nameArea();

    if (folded)
    {
        // The name runs down the strip, top to bottom.
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, (float) name.getCentreX(),
                                                         (float) name.getCentreY()));
        drawStyledText (g, themeManager, plugin.name, foldedNameStyle, name.withSizeKeepingCentre (name.getHeight(), name.getWidth()),
                        juce::Justification::centredLeft, theme.textPrimary);
    }
    else
    {
        drawStyledText (g, themeManager, plugin.name, titleStyle, name, juce::Justification::centredLeft, theme.textOnAccent);
    }

    if (dividerX >= 0)
    {
        g.setColour (theme.borderSoft);
        g.fillRect (dividerX, headerHeight + bodyPaddingY, 1, getHeight() - headerHeight - 2 * bodyPaddingY);
    }

    g.setColour (theme.border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
}

void NativeDeviceCard::mouseUp (const juce::MouseEvent& e)
{
    // Clicking a folded strip unfolds it.
    if (size == DeviceSize::folded && ! e.mods.isPopupMenu()
        && ! e.mouseWasDraggedSinceMouseDown() && onSizeChange)
        onSizeChange (DeviceSize::compact);
}

} // namespace resamper
