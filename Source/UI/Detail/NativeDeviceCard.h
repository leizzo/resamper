#pragma once

#include "DeviceBody.h"
#include "DeviceCard.h"
#include "UI/Controls/ContinuousControl.h"

#include <vector>

namespace resamper
{

/** `DeviceCard/Native`, the v2 contract (PRD §9.2.1a): a built-in device,
    edited inline with Resamper controls.

    One 28 px `DeviceHeader` in the device colour, in the same order on every
    device: power, name, preset, A/B, Mods (its count), fold, expand, options.
    Geometry and colours follow the design's `DeviceHeader`, `Device/Folded`
    and `Knob/Automated` exactly.
    The body runs its zones left to right, Controls then Output: Mix and Out
    are always last, behind a divider. A knob with automation shows a red dot.

    Folded (`Device/Folded`) is a 28 px strip: a stripe of the device colour,
    the power, the name running down it and the Mods indicator; clicking the
    strip unfolds it. Compact (the default) shows the first controls and the outputs
    and never scrolls. Expanded docks across the detail view and shows every
    parameter, docked across the detail view or floating in its own window
    (the menu's Open in Window). A device's colour comes from its type, so the same device looks
    the same on every track.

    A v2 device with a display of its own (EQ Eight, Compressor v2) fills the
    body with its DeviceBody instead: Input, Display, Controls, Output, at the
    design's width (wider when expanded).

    The preset menu, A/B compare and the Mods Drawer come with their own
    tickets; until then those buttons show their state and are disabled. */
class NativeDeviceCard : public DeviceCard
{
public:
    static constexpr int headerHeight = 28, foldedWidth = 28, maxCompactControls = 4;

    NativeDeviceCard (CommandRegistry&, const PluginRack&, ThemeManager&, const juce::String& trackId, const PluginInfo&);

    void setState (const PluginInfo&) override;

    /** Shown in its own window: always expanded, without fold, expand or Open in Window. */
    void setFloating (bool);
    int getPreferredWidth (int dockedWidth) const override;
    void focusFirstControl() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Parameter
    {
        juce::String id;
        bool output = false;   ///< in the Output zone: Mix, Wet / Dry, Out
        std::unique_ptr<Knob> knob;
    };

    juce::Colour colour;
    DeviceSize size = DeviceSize::compact;
    bool floating = false;
    DevicePowerButton power;
    DeviceHeaderButton preset, ab, mods, fold, expand, options;
    std::vector<Parameter> parameters;
    std::unique_ptr<DeviceBody> body;   ///< a v2 device's own zones, in place of the knobs
    std::vector<juce::String> parameterIds;   ///< in the device's order, to tell when it changes
    int dividerX = -1;

    juce::Rectangle<int> getTitleBar() const override;
    void addMenuItems (juce::PopupMenu&) override;

    void rebuild (const std::vector<PluginParameter>&);
    int minHeaderWidth() const;

    /** Where the name is drawn: in the header, or down a folded strip. */
    juce::Rectangle<int> nameArea() const;

    /** The knobs this size shows, Controls first, then Output. */
    std::vector<Parameter*> shownParameters (bool output);
    int columns (int knobs) const;
};

} // namespace resamper
