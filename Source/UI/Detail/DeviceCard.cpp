#include "DeviceCard.h"
#include "DeviceBody.h"
#include "NativeDeviceCard.h"
#include "PluginDeviceCard.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"
#include "UI/Controls/ValueFormat.h"

namespace resamper
{

std::unique_ptr<DeviceCard> DeviceCard::create (CommandRegistry& c, const PluginRack& r, const PluginHosting& h, ThemeManager& tm,
                                                const juce::String& track, const PluginInfo& info)
{
    if (info.external)
        return std::make_unique<PluginDeviceCard> (c, r, h, tm, track, info);

    return std::make_unique<NativeDeviceCard> (c, r, tm, track, info);
}

DeviceCard::DeviceCard (CommandRegistry& c, const PluginRack& r, ThemeManager& tm, const juce::String& track, const PluginInfo& info)
    : commands (c), rack (r), themeManager (tm), trackId (track), plugin (info)
{
    setTitle (info.name);
}

void DeviceCard::toggleBypass()
{
    commands.invoke (cmd::pluginSetBypassed, { trackId, plugin.id, plugin.enabled });
}

ContinuousValue::Spec DeviceCard::specFor (const PluginParameter& p)
{
    return parameterSpec (rack, plugin.id, p);
}

std::function<void (double, bool)> DeviceCard::setterFor (const juce::String& parameterId)
{
    return [this, parameterId] (double v, bool continues)
    {
        commands.invoke (cmd::pluginSetParameter, { plugin.id, parameterId, (float) v, continues });
    };
}

void DeviceCard::showMenu()
{
    juce::PopupMenu menu;
    addMenuItems (menu);
    menu.addItem (plugin.enabled ? TRANS ("Bypass") : TRANS ("Enable"), [this] { toggleBypass(); });
    menu.addSeparator();
    menu.addItem (TRANS ("Delete"), [this] { commands.invoke (cmd::pluginRemove, { trackId, plugin.id }); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void DeviceCard::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && getTitleBar().contains (e.getPosition()))
        showMenu();
}

void DeviceCard::mouseDrag (const juce::MouseEvent& e)
{
    // Dragging the title bar reorders the chain (the DeviceChain row is the drop target).
    if (getTitleBar().contains (e.getMouseDownPosition()) && e.getDistanceFromDragStart() > 4)
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr
                                                                                            && ! container->isDragAndDropActive())
        {
            auto d = new juce::DynamicObject();
            d->setProperty ("deviceCard", plugin.id);
            container->startDragging (juce::var (d), this, juce::ScaledImage (createComponentSnapshot (getLocalBounds())));
        }
}

//==============================================================================
DevicePowerButton::DevicePowerButton (ThemeManager& tm, Style s) : ThemedButton (tm, TRANS ("Power")), style (s)
{
    setComponentID ("power");
    setTooltip (TRANS ("Power (bypass)"));
}

void DevicePowerButton::setStyle (Style s)
{
    style = s;
    repaint();
}

void DevicePowerButton::setDeviceColour (juce::Colour c)
{
    deviceColour = c;
    repaint();
}

void DevicePowerButton::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto& theme = themeManager.getTheme();
    const auto disc = getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
    const auto on = getToggleState();
    const auto hover = highlighted ? 0.85f : 1.0f;

    switch (style)
    {
        case Style::native:
        case Style::folded:
        {
            const auto native = style == Style::native;
            g.setColour ((native ? theme.textOnAccent : deviceColour).withMultipliedAlpha (hover));
            g.fillEllipse (disc);

            if (on)
            {
                g.setColour (native ? deviceColour : theme.textOnAccent);
                g.fillEllipse (disc.withSizeKeepingCentre (5.0f, 5.0f));
            }

            break;
        }

        case Style::plugin:
            g.setColour ((on ? theme.accent : theme.textDim).withMultipliedAlpha (hover));
            g.drawEllipse (disc.reduced (0.75f), 1.5f);

            if (on)
                g.fillEllipse (disc.withSizeKeepingCentre (4.0f, 4.0f));

            break;
    }

    paintFocus (g, disc.getWidth() / 2.0f);
}

//==============================================================================
namespace
{
    // DeviceHeader parts (design: Resamper — Native Devices › DeviceHeader): pills are
    // text-on-accent at 15 % on the device colour, their text at 80 %, icons at 67 %.
    constexpr float pillAlpha = 0.15f, textAlpha = 0.8f, iconAlpha = 0.67f;
    constexpr int pillHeight = 18, iconSize = 11;
}

DeviceHeaderButton::DeviceHeaderButton (ThemeManager& tm, const juce::String& name, Kind k, std::optional<Icon> i)
    : ThemedButton (tm, name), kind (k), icon (i)
{
    setTooltip (name);
}

int DeviceHeaderButton::getIdealWidth() const
{
    switch (kind)
    {
        case Kind::icon:        return iconSize + 4;
        case Kind::abCompare:   return 1 + 16 + 1 + 16 + 1;
        case Kind::foldedMods:  return 14;
        case Kind::preset:
            return 6 + juce::GlyphArrangement::getStringWidthInt (themeManager.font (TypeStyle { 9.5f, false, 400 }), getButtonText())
                 + 4 + 9 + 6;
        case Kind::mods:
            return 5 + 10 + 3 + juce::GlyphArrangement::getStringWidthInt (themeManager.font (TypeStyle { 8.5f, true, 700 }),
                                                                          getButtonText()) + 5;
    }

    return iconSize;
}

void DeviceHeaderButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto& theme = themeManager.getTheme();
    const auto ink = theme.textOnAccent;
    const auto pressed = isEnabled() && (highlighted || down);
    auto pill = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) pillHeight);

    auto fillPill = [&]
    {
        g.setColour (ink.withAlpha (pillAlpha + (pressed ? 0.1f : 0.0f)));
        g.fillRoundedRectangle (pill, 4.0f);
    };

    switch (kind)
    {
        case Kind::icon:
            if (pressed)
            {
                g.setColour (ink.withAlpha (pillAlpha));
                g.fillRoundedRectangle (pill.withSizeKeepingCentre (pill.getWidth(), (float) iconSize + 4.0f), 3.0f);
            }

            if (icon)
                drawIcon (g, *icon, getLocalBounds().toFloat().withSizeKeepingCentre ((float) iconSize, (float) iconSize),
                          ink.withAlpha (iconAlpha));
            break;

        case Kind::preset:
        {
            fillPill();
            auto area = pill.reduced (6.0f, 0.0f);
            drawIcon (g, Icon::chevronDown, area.removeFromRight (9.0f).withSizeKeepingCentre (9.0f, 9.0f), ink.withAlpha (iconAlpha));
            area.removeFromRight (4.0f);
            drawStyledText (g, themeManager, getButtonText(), TypeStyle { 9.5f, false, 400 }, area.toNearestInt(),
                            juce::Justification::centredLeft, ink.withAlpha (textAlpha));
            break;
        }

        case Kind::abCompare:
        {
            fillPill();
            auto area = pill.reduced (1.0f);
            const auto a = area.removeFromLeft (16.0f);
            area.removeFromLeft (1.0f);
            const auto b = area.removeFromLeft (16.0f);
            const auto letter = TypeStyle { 8.5f, false, 700 };
            g.setColour (ink);
            g.fillRoundedRectangle (a, 3.0f);
            // A and B stay as they are in every UI Language, as on hardware.
            drawStyledText (g, themeManager, "A", letter, a.toNearestInt(), juce::Justification::centred, theme.textPrimary);
            drawStyledText (g, themeManager, "B", letter, b.toNearestInt(), juce::Justification::centred, ink.withAlpha (iconAlpha));
            break;
        }

        case Kind::mods:
        {
            fillPill();
            auto area = pill.reduced (5.0f, 0.0f);
            drawIcon (g, Icon::spline, area.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f), ink.withAlpha (textAlpha));
            area.removeFromLeft (3.0f);
            drawNumber (g, themeManager, getButtonText(), TypeStyle { 8.5f, true, 700 }, area.toNearestInt(),
                        juce::Justification::centredLeft, ink.withAlpha (textAlpha));
            break;
        }

        case Kind::foldedMods:
            // Blue (modulation) once the device has modulators.
            drawIcon (g, Icon::spline, getLocalBounds().toFloat().withSizeKeepingCentre (10.0f, 10.0f),
                      getButtonText().getIntValue() > 0 ? theme.statePre : theme.textDim);
            break;
    }

    paintFocus (g, 4.0f);
}

} // namespace resamper
