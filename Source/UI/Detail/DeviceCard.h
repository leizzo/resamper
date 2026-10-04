#pragma once

#include "Engine/PluginHosting.h"
#include "Engine/PluginRack.h"
#include "UI/Controls/ContinuousValue.h"
#include "UI/Controls/Controls.h"

#include <functional>
#include <memory>
#include <optional>

namespace resamper
{

class CommandRegistry;

/** target, or back to compact when already there: what a fold or expand toggle does. */
inline DeviceSize toggledSize (DeviceSize current, DeviceSize target)
{
    return current == target ? DeviceSize::compact : target;
}

/** One device of a track's device chain (PRD §9.2). A chain mixes two
    contracts, told apart at a glance: a NativeDeviceCard for a built-in, a
    PluginDeviceCard for a scanned (VST3 / AU / CLAP) plug-in. Both are 164
    high; dragging the title bar reorders the chain and right-clicking it opens
    the card's menu. A bypassed device's card drops to 50 %. */
class DeviceCard : public juce::Component
{
public:
    static constexpr int height = 164;

    /** The card for info's contract. A plug-in's card follows its Hosting State. */
    static std::unique_ptr<DeviceCard> create (CommandRegistry&, const PluginRack&, const PluginHosting&, ThemeManager&,
                                               const juce::String& trackId, const PluginInfo&);

    const PluginInfo& getPlugin() const noexcept   { return plugin; }

    /** New state from the model. */
    virtual void setState (const PluginInfo&) = 0;

    /** Width in the chain. dockedWidth is what an expanded device fills at least. */
    virtual int getPreferredWidth (int dockedWidth) const = 0;

    /** Whether the plug-in's window is open. */
    virtual void setWindowOpen (bool) {}

    /** Gives the first control keyboard focus (a native device just dropped in). */
    virtual void focusFirstControl() {}

    std::function<void (DeviceSize)> onSizeChange;
    std::function<void()> onOpenEditor;
    std::function<void()> onFloat;   ///< a native device: open it, expanded, in its own window

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

protected:
    DeviceCard (CommandRegistry&, const PluginRack&, ThemeManager&, const juce::String& trackId, const PluginInfo&);

    CommandRegistry& commands;
    const PluginRack& rack;
    ThemeManager& themeManager;
    juce::String trackId;
    PluginInfo plugin;

    /** Where a drag reorders and a right-click opens the menu. */
    virtual juce::Rectangle<int> getTitleBar() const = 0;

    /** The card's own menu items; Bypass and Delete follow them. */
    virtual void addMenuItems (juce::PopupMenu&) = 0;

    void toggleBypass();
    void showMenu();

    /** A control's range and text for one of the plug-in's parameters, shown as the plug-in shows it. */
    ContinuousValue::Spec specFor (const PluginParameter&);

    /** Sets a parameter through plugin.setParameter: a control's onChange. */
    std::function<void (double, bool)> setterFor (const juce::String& parameterId);
};

/** A device's power button, on while the device is enabled (PRD §9.2.3):
    a 14 px disc in text-on-accent with a 5 px dot in the device colour (a
    native header), the same inverted (a folded device), or an accent ring
    with a 4 px dot (a plug-in). */
class DevicePowerButton : public ThemedButton
{
public:
    enum class Style { native, folded, plugin };

    DevicePowerButton (ThemeManager&, Style);

    void setStyle (Style);

    /** The device colour: a native dot, a folded disc. */
    void setDeviceColour (juce::Colour);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Style style;
    juce::Colour deviceColour;
};

/** A part of a native device's `DeviceHeader` (PRD §9.2.1a), drawn in
    text-on-accent over the device colour: an 11 px icon, the preset pill
    (name and chevron), the A/B segment, the Mods pill (spline and count), or,
    on a folded device, the Mods indicator alone. */
class DeviceHeaderButton : public ThemedButton
{
public:
    enum class Kind { icon, preset, abCompare, mods, foldedMods };

    DeviceHeaderButton (ThemeManager&, const juce::String& name, Kind, std::optional<Icon> = {});

    void setKind (Kind k)   { kind = k; repaint(); }
    void setIcon (Icon i)   { icon = i; repaint(); }

    /** The width the design gives it at this text. */
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Kind kind;
    std::optional<Icon> icon;
};

} // namespace resamper
