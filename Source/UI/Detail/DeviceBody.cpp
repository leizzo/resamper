#include "DeviceBody.h"
#include "CompressorDevice.h"
#include "EqEightDevice.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"
#include "UI/Controls/ValueFormat.h"

namespace resamper
{

namespace
{
    /** Meters and spectra repaint at UI rate. */
    constexpr int readingsHz = 30;

    // Design: a device row's caption (Inter 8.5, dim) and value (mono 9).
    const TypeStyle rowCaptionStyle { 8.5f, false, 400 };
    const TypeStyle rowValueStyle { 9.0f, true, 400 };
    const TypeStyle toggleStyle { 7.5f, true, 700 };
    constexpr int rowPaddingX = 6, togglePaddingX = 5;
    constexpr float rowRadius = 3.0f, toggleRadius = 4.0f, solidToggleRadius = 2.0f, hairline = 1.0f;
}

ContinuousValue::Spec parameterSpec (const PluginRack& rack, const juce::String& pluginId, const PluginParameter& p)
{
    ContinuousValue::Spec spec;
    spec.minimum = p.minimum;
    spec.maximum = p.maximum;
    spec.defaultValue = p.defaultValue;
    spec.format.format = [&rack, pluginId, id = p.id] (double v) { return rack.getParameterText (pluginId, id, (float) v); };
    spec.format.parse = ValueFormat::number (3).parse;

    // Travel along the parameter's own range; a choice lands on its steps.
    spec.toProportion = [range = p.range] (double v) { return (double) range.convertTo0to1 ((float) v); };
    spec.fromProportion = [range = p.range] (double proportion)
    {
        return (double) range.snapToLegalValue (range.convertFrom0to1 ((float) juce::jlimit (0.0, 1.0, proportion)));
    };

    if (p.range.interval > 0)
        spec.wheelStep = p.range.interval;

    return spec;
}

juce::String designMinus (const juce::String& text)
{
    return text.startsWith ("-") ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + text.substring (1) : text;
}

//==============================================================================
std::unique_ptr<DeviceBody> DeviceBody::create (CommandRegistry& c, const PluginRack& r, ThemeManager& tm, const PluginInfo& info)
{
    if (info.external)
        return {};

    if (info.path == NativeDevices::eqEightType)
        return std::make_unique<EqEightDevice> (c, r, tm, info.id);

    if (info.path == NativeDevices::compressorType)
        return std::make_unique<CompressorDevice> (c, r, tm, info.id);

    return {};
}

DeviceBody::DeviceBody (CommandRegistry& c, const PluginRack& r, ThemeManager& tm, const juce::String& id)
    : commands (c), rack (r), themeManager (tm), pluginId (id)
{
    startTimerHz (readingsHz);
}

DeviceBody::~DeviceBody()
{
    stopTimer();
}

void DeviceBody::setParameters (const std::vector<PluginParameter>& list, bool on, juce::Colour c)
{
    parameters.clear();

    for (auto& p : list)
        parameters[p.id] = p;

    enabled = on;
    colour = c;
    parametersChanged();
    repaint();
}

const PluginParameter* DeviceBody::parameter (const juce::String& id) const
{
    auto found = parameters.find (id);
    return found != parameters.end() ? &found->second : nullptr;
}

float DeviceBody::valueOf (const juce::String& id) const
{
    if (auto* p = parameter (id))
        return p->value;

    return 0.0f;
}

void DeviceBody::set (const juce::String& id, float value, bool continuesGesture)
{
    commands.invoke (cmd::pluginSetParameter, { pluginId, id, value, continuesGesture });
    reload();
}

void DeviceBody::setTogether (const std::vector<ParameterValue>& values, bool continuesGesture)
{
    commands.invoke (cmd::pluginSetParameters, { pluginId, values, continuesGesture });
    reload();
}

void DeviceBody::reload()
{
    // The model tells the card later (asynchronously); a gesture that builds on
    // the last value (a wheel on a node, the next drag step) needs it now.
    setParameters (rack.getParameters (pluginId), enabled, colour);
}

juce::String DeviceBody::textOf (const juce::String& id, float value) const
{
    return rack.getParameterText (pluginId, id, value);
}

ContinuousValue::Spec DeviceBody::specFor (const juce::String& id) const
{
    // Read from the rack, not the last values: controls are made before the first ones arrive.
    for (auto& p : rack.getParameters (pluginId))
        if (p.id == id)
        {
            auto spec = parameterSpec (rack, pluginId, p);
            spec.format.format = [format = spec.format.format] (double v) { return designMinus (format (v)); };
            return spec;
        }

    return {};
}

void DeviceBody::timerCallback()
{
    // Only a body on screen reads: a hidden card leaves the audio thread's data where it is.
    if (isShowing())
        readingsChanged();
}

//==============================================================================
ParameterRow::ParameterRow (ThemeManager& tm, ContinuousValue::Spec spec, juce::String c)
    : ContinuousControl (tm, std::move (spec), Axis::vertical), caption (std::move (c))
{
    setTitle (caption);
}

juce::Rectangle<int> ParameterRow::getReadoutBounds() const
{
    return getLocalBounds().withTrimmedLeft (getWidth() / 2);
}

void ParameterRow::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (isMouseOverOrDragging() && isEnabled() ? theme.bgHover : theme.bgSlot);
    g.fillRoundedRectangle (bounds, rowRadius);

    if (isEditingText())
        return;

    auto r = getLocalBounds().reduced (rowPaddingX, 0);
    drawStyledText (g, themeManager, caption, rowCaptionStyle, r, juce::Justification::centredLeft, theme.textDim);
    drawNumber (g, themeManager, displayText.isNotEmpty() ? displayText : getModel().getText(), rowValueStyle, r,
                juce::Justification::centredRight, theme.textPrimary);
}

//==============================================================================
DeviceToggle::DeviceToggle (ThemeManager& tm, const juce::String& text, const juce::String& tooltip)
    : ThemedButton (tm, tooltip)
{
    setButtonText (text);
    setTooltip (tooltip);
    setClickingTogglesState (false);
}

int DeviceToggle::getIdealWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt (themeManager.numberFont (toggleStyle), getButtonText()) + 2 * togglePaddingX;
}

void DeviceToggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto on = getToggleState();
    const auto radius = solid ? solidToggleRadius : toggleRadius;

    if (solid)
    {
        g.setColour (on ? colour : theme.bgElevated);
        g.fillRoundedRectangle (bounds, radius);
        drawNumber (g, themeManager, getButtonText(), toggleStyle, getLocalBounds(), juce::Justification::centred,
                    on ? theme.textOnAccent : theme.textDim);
    }
    else
    {
        // Design: ADPT Q, a tint of the device colour (15 %) with a 40 % outline when on.
        g.setColour (on ? colour.withAlpha (0.15f) : (highlighted ? theme.bgHover : theme.bgSlot));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (on ? colour.withAlpha (0.4f) : theme.borderSoft);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, hairline);
        drawNumber (g, themeManager, getButtonText(), toggleStyle, getLocalBounds(), juce::Justification::centred,
                    on ? colour : theme.textDim);
    }

    if (hasKeyboardFocus (false))
        paintFocus (g, radius);
}

} // namespace resamper
