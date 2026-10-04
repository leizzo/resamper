#pragma once

#include "DeviceBody.h"

#include <array>
#include <vector>

namespace resamper
{

class CompressorDevice;

/** Compressor v2's Input zone (design: `Device/Compressor v2` › Input): IN
    left and right filling from the bottom, GR filling from the top, and the
    gain reduction as a number. Peaks jump up and fall back at a meter's rate. */
class DynamicsMeters : public juce::Component
{
public:
    explicit DynamicsMeters (ThemeManager&);

    /** A reading taken elapsedSeconds after the last. */
    void update (const DynamicsReading&, double elapsedSeconds);

    float getInputDb (int channel) const noexcept   { return input[(size_t) juce::jlimit (0, 1, channel)]; }
    float getReductionDb() const noexcept           { return reduction; }

    void paint (juce::Graphics&) override;

private:
    ThemeManager& themeManager;
    std::array<float, 2> input { -120.0f, -120.0f };
    float reduction = 0;
};

/** Compressor v2's Display: the Transfer curve (input against output level,
    with the threshold knee and a live level dot) or Activity (level and gain
    reduction over the last seconds). It is the controller: drag the threshold
    line, across in Transfer or up and down in Activity; double-click it to
    reset. A drag is one undo step. */
class CompressorGraph : public juce::Component
{
public:
    static constexpr float floorDb = -60.0f;

    explicit CompressorGraph (CompressorDevice&);

    /** Activity (true) or Transfer (false): a view, not saved. */
    void setActivity (bool);
    bool showsActivity() const noexcept   { return activity; }

    /** Where the threshold line is drawn: an x in Transfer, a y in Activity. */
    float thresholdPosition() const;

    /** The live level dot (input, output in dB), when there is signal. */
    std::optional<juce::Point<float>> getLiveLevel() const noexcept   { return live; }

    /** A meter reading for the dot and the Activity history. */
    void addReading (const DynamicsReading&);

    /** Recomputes the curve from the device's values. */
    void refreshCurve();

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    CompressorDevice& device;
    bool activity = false, dragging = false, dragContinues = false;
    std::vector<float> curve;               ///< output dB per pixel column of Transfer
    std::vector<float> levelHistory, reductionHistory;
    int historyWrite = 0;
    std::optional<juce::Point<float>> live;

    float xForDb (float db) const;
    float dbAtX (float x) const;
    float yForDb (float db) const;
    float dbAtY (float y) const;
    bool onThreshold (juce::Point<float>) const;
};

/** `Device/Compressor v2` (PRD §9.2.1a): Input (IN / GR meters) → Display
    (Transfer / Activity, the draggable threshold) → Controls (Ratio, Attack,
    Release, Knee; Lookahead 0 / 1 / 10 ms; Peak / RMS / Expand) → Output
    (Makeup with Auto, Mix, Out). */
class CompressorDevice : public DeviceBody
{
public:
    static constexpr int compactWidth = 520;

    CompressorDevice (CommandRegistry&, const PluginRack&, ThemeManager&, const juce::String& pluginId);

    int getPreferredWidth (bool expanded, int dockedWidth) const override;
    void focusFirstControl() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    float getThreshold() const   { return valueOf ("threshold"); }
    bool expands() const         { return juce::roundToInt (valueOf ("detect")) == 2; }

    /** Moves the threshold (a drag continues its gesture: one undo step). */
    void setThreshold (float db, bool continuesGesture);
    void resetThreshold();

    /** The static curve at inputDb. */
    float transferAt (float inputDb) const;

    /** The span a modulator moves the threshold over, if one does. */
    std::optional<juce::Range<float>> thresholdModulation() const;

    juce::Colour getColour() const noexcept   { return colour; }
    ThemeManager& getThemeManager() const noexcept   { return themeManager; }

private:
    DynamicsMeters meters;
    Segmented view;
    CompressorGraph graph;
    Knob ratio, attack, release, knee;
    Segmented lookahead, detect;
    DeviceToggle makeupAuto;
    ParameterRow makeup, mix, output;
    juce::uint32 lastReading = 0;
    juce::Rectangle<int> controlsArea, outputArea;

    void parametersChanged() override;
    void readingsChanged() override;
};

} // namespace resamper
