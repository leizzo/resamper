#pragma once

#include "Engine/Mixer.h"
#include "Metering.h"
#include "UI/Controls/ContinuousControl.h"

namespace resamper
{

/** The measures every mixer strip shares (PRD §10.1), so strips line up. */
namespace StripMetrics
{
    constexpr int padX = 10, sectionPadY = 8, rowGap = 4, colourBarHeight = 3, headTextHeight = 14;

    /** The head: colour bar, then a padded row of text. */
    constexpr int headHeight = colourBarHeight + 2 * sectionPadY + headTextHeight;
}

/** `Fader`: a vertical fader on the normative dB law (PRD §10.3), with its dB
    scale on the left, a track in bg-slot filled in the track colour up to the
    cap, and a 26 x 38 cap with a centre line in the track colour. The
    continuous-control rules apply: 200 px of drag is the whole travel, Shift
    is fine, double-click or Alt+click resets to 0 dB, the wheel steps 0.5 dB.
    A press away from the cap (the scale or the track) jumps the cap there first. */
class Fader : public ContinuousControl
{
public:
    explicit Fader (ThemeManager&);

    void setColour (juce::Colour c)   { colour = c; repaint(); }

    /** Where the fader's travel sits inside its bounds (the meter lines up with it). */
    juce::Rectangle<int> getTravelBounds() const;

    void paint (juce::Graphics&) override;

protected:
    juce::Rectangle<float> getFocusBounds() const override   { return capBounds(); }
    std::optional<double> getProportionAt (juce::Point<float>) const override;

private:
    juce::Colour colour;
    juce::Rectangle<float> capBounds() const;
};

/** `Meter/Stereo`: two 7 px wells. The level fill is one full-height gradient
    (meter-low -> meter-mid at 80 % -> meter-high) clipped to the level, never
    scaled, on the fader's dB law. A 2 px peak-hold line per side holds 1.5 s
    then falls 20 dB/s. Clicking resets the peaks. */
class StereoMeter : public juce::Component,
                    public juce::SettableTooltipClient
{
public:
    explicit StereoMeter (ThemeManager&);

    /** A new reading, elapsedSeconds after the last. */
    void setLevel (StereoLevel, double elapsedSeconds);

    /** The higher peak hold, for the strip's peak readout. */
    double getPeakDb() const   { return std::max (holds[0].get(), holds[1].get()); }

    void resetPeaks();

    /** Peak, RMS or LUFS ballistics (the mixer toolbar's meter mode). */
    void setMode (MeterMode);

    /** Width of each of the two wells; the rest of the width is the gap between them. */
    void setWellWidth (float w)   { wellWidth = w; repaint(); }

    /** Called when a click resets the peaks. */
    std::function<void()> onPeaksReset;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    ThemeManager& themeManager;
    float wellWidth = 7.0f;
    std::array<double, 2> levels { FaderLaw::floorDb, FaderLaw::floorDb };
    std::array<PeakHold, 2> holds;
    std::array<MeterBallistics, 2> ballistics;
};

/** A strip's fader section (PRD §10.3): the gain and peak readouts above the
    fader and its stereo meter, laid out as the design draws each strip. The peak
    readout shows the meter's peak hold, red above -1.5 dBFS; clicking it, or the
    meter, resets the peaks. Gaps pass mouse events through to the strip. */
class FaderSection : public juce::Component
{
public:
    /** Per strip in the design: the fader's width (which centres its track) and the meter's wells. */
    struct Geometry
    {
        int faderWidth;
        float meterWellWidth;
    };

    FaderSection (ThemeManager&, Geometry);

    /** A fader drag or a typed gain; continues joins the drag's undo step. */
    std::function<void (Decibels, bool continues)> onVolumeChange;

    void setVolume (Decibels, juce::Colour);
    void setLevel (StereoLevel, double elapsedSeconds);
    void resetPeaks();
    void setMeterMode (MeterMode m)   { meter.setMode (m); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    ThemeManager& themeManager;
    Geometry geometry;
    ValueField gain;
    Fader fader;
    StereoMeter meter;
    juce::Rectangle<int> peakReadout;
};

/** Fills a strip's colour bar: its top edge, clipped to the strip's rounded corners. */
void paintColourBar (juce::Graphics&, juce::Rectangle<float> strip, float radius, juce::Colour);

/** A pan knob: -1..1, shown as L30 / C / R20. */
ContinuousValue::Spec panKnobSpec();

/** A gain readout to type into: -inf..+6 dB. */
ContinuousValue::Spec gainReadoutSpec();

/** Where the fader law puts a dB value in an area, as a y. */
float yForDb (juce::Rectangle<float> travel, double db);

} // namespace resamper
