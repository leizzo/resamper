#pragma once

#include "Engine/PluginRack.h"
#include "UI/Controls/ContinuousControl.h"
#include "UI/Controls/Controls.h"

#include <map>
#include <memory>

namespace resamper
{

class CommandRegistry;

/** A control's range and text for one of a plug-in's parameters, as the
    plug-in shows it: travel follows the parameter's range (a frequency's is
    logarithmic) and a choice steps by one. */
ContinuousValue::Spec parameterSpec (const PluginRack&, const juce::String& pluginId, const PluginParameter&);

/** A value's text in the design's typography: a leading "-" becomes a minus sign (−4.0 dB). */
juce::String designMinus (const juce::String&);

/** The body of a v2 native device with its own display (PRD §9.2.1a): the
    zones Input → Display → Controls → Output under the card's DeviceHeader.
    Graph = controller: every graph is directly editable, and the controls
    mirror it. Every change goes through a Command (plugin.setParameter, or
    plugin.setParameters for a gesture that moves several), so each gesture is
    one Engine Undo step.

    Meters and spectra are read from NativeDevices at UI rate (30 Hz) while the
    body is on screen; the audio thread hands them over without locks. */
class DeviceBody : public juce::Component,
                   private juce::Timer
{
public:
    /** The body for a v2 device (EQ Eight, Compressor v2); empty for any other. */
    static std::unique_ptr<DeviceBody> create (CommandRegistry&, const PluginRack&, ThemeManager&, const PluginInfo&);

    ~DeviceBody() override;

    /** New values from the model, whether the device is on, and its colour. */
    void setParameters (const std::vector<PluginParameter>&, bool enabled, juce::Colour);

    /** Width of the card it fills: the design's compact width, or at least dockedWidth expanded. */
    virtual int getPreferredWidth (bool expanded, int dockedWidth) const = 0;

    virtual void focusFirstControl() {}

    /** Reads the meters and spectra now, as the 30 Hz timer does. */
    void refreshReadings()   { readingsChanged(); }

protected:
    DeviceBody (CommandRegistry&, const PluginRack&, ThemeManager&, const juce::String& pluginId);

    CommandRegistry& commands;
    const PluginRack& rack;
    ThemeManager& themeManager;
    const juce::String pluginId;
    juce::Colour colour;
    bool enabled = true;

    /** The parameter's current value; 0 when the device has no such parameter. */
    float valueOf (const juce::String& parameterId) const;
    const PluginParameter* parameter (const juce::String& parameterId) const;

    /** Sets one parameter (plugin.setParameter). */
    void set (const juce::String& parameterId, float value, bool continuesGesture = false);

    /** Sets several as one change (plugin.setParameters). */
    void setTogether (const std::vector<ParameterValue>&, bool continuesGesture);

    /** The parameter's text, as the device shows it. */
    juce::String textOf (const juce::String& parameterId, float value) const;
    juce::String textOf (const juce::String& parameterId) const   { return textOf (parameterId, valueOf (parameterId)); }

    /** A control for one of the device's parameters, its text as the design writes it. */
    ContinuousValue::Spec specFor (const juce::String& parameterId) const;

    /** Model values arrived: mirror them in the controls and graphs. */
    virtual void parametersChanged() = 0;

    /** Meters and spectra to read again (30 Hz while on screen). */
    virtual void readingsChanged() {}

private:
    std::map<juce::String, PluginParameter> parameters;

    /** Re-reads the values after this body changed them. */
    void reload();
    void timerCallback() override;
};

/** A value row of a device panel (design: EQ Eight's Freq / Gain / Q, the
    Output zone's Mix / Out): a dim caption on the left and the mono value on
    the right, in a bg-slot well. Drag vertically; click the value to type;
    double-click resets. */
class ParameterRow : public ContinuousControl
{
public:
    ParameterRow (ThemeManager&, ContinuousValue::Spec, juce::String caption);

    void setCaption (juce::String c)   { caption = std::move (c); repaint(); }

    /** Shows this text instead of the value (Makeup while Auto sets it). */
    void setDisplayText (juce::String t)   { displayText = std::move (t); repaint(); }

    void paint (juce::Graphics&) override;

protected:
    juce::Rectangle<int> getReadoutBounds() const override;

private:
    juce::String caption, displayText;
};

/** A small mono on/off pill in a device's colour (design: EQ Eight's ADPT Q,
    the Compressor's Makeup Auto "A"): tinted when on, dim when off. */
class DeviceToggle : public ThemedButton
{
public:
    DeviceToggle (ThemeManager&, const juce::String& text, const juce::String& tooltip);

    /** Filled in this colour when on (Makeup Auto), rather than tinted. */
    void setSolid (bool b)               { solid = b; repaint(); }
    void setColour (juce::Colour c)      { colour = c; repaint(); }
    int getIdealWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour colour;
    bool solid = false;
};

} // namespace resamper
