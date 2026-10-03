#pragma once

#include "DeviceBody.h"
#include "Engine/EqBand.h"

#include <array>

namespace resamper
{

class EqEightDevice;

/** EQ Eight's Display (design: `Device/EQ Eight v2` › Graph): the curve over
    the pre or post spectrum, a numbered node per band and the selected band's
    Q width shaded. It is the controller: drag a node for frequency and gain,
    the wheel on a node for Q, double-click a node to switch the band on or off,
    double-click empty space to add a bell. Each gesture is one undo step. */
class EqEightGraph : public juce::Component
{
public:
    static constexpr float rangeDb = 15.0f, lowestHz = 20.0f, highestHz = 20000.0f;

    explicit EqEightGraph (EqEightDevice&);

    /** Where a band's node sits now. */
    juce::Point<float> nodePosition (int band) const;

    /** The band whose node is under position, or -1. */
    int nodeAt (juce::Point<float>) const;

    float xForFrequency (float hz) const;
    float frequencyAtX (float x) const;
    float yForGain (float db) const;
    float gainAtY (float y) const;

    /** Pre (false) or post (true) spectrum: a view, not saved. */
    bool showsPostSpectrum() const noexcept   { return post; }
    void setShowsPostSpectrum (bool);

    /** Recomputes the curves from the device's values. */
    void refreshCurves();

    /** Reads the spectrum the audio thread sent. */
    void refreshSpectrum();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    EqEightDevice& device;
    std::vector<float> frequencies, curve, sideCurve, spectrum;
    bool post = true, showSideCurve = false;
    int dragBand = -1;
    bool dragContinues = false;
    int wheelBand = -1;
    juce::uint32 lastWheel = 0;
    float wheelAccumulator = 0;

    juce::Rectangle<float> spectrumToggle() const;
    juce::Path curvePath (const std::vector<float>&) const;
};

/** `Device/EQ Eight v2` (PRD §9.2.1a): Display (graph and band strip 1–8 with
    type glyphs and on/off), then the selected band's panel (type, audition,
    Freq / Gain / Q, Adaptive Q, St / L-R / M-S) and the Output zone (Scale,
    Out). The band strip, the panel and the graph mirror each other. */
class EqEightDevice : public DeviceBody
{
public:
    static constexpr int compactWidth = 470, numBands = NativeDevices::numEqBands;

    EqEightDevice (CommandRegistry&, const PluginRack&, ThemeManager&, const juce::String& pluginId);
    ~EqEightDevice() override;

    int getPreferredWidth (bool expanded, int dockedWidth) const override;
    void focusFirstControl() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** One band as the model has it. */
    struct Band
    {
        bool on = false;
        dsp::EqBandType type = dsp::EqBandType::bell;
        float frequency = 1000, gain = 0, q = 0.71f;
        int channel = 0;   ///< 0 both, 1 L or M, 2 R or S
    };

    Band band (int) const;
    int getMode() const;   ///< 0 St, 1 L/R, 2 M/S

    /** The band the panel edits: a view, not saved. */
    int getSelectedBand() const noexcept   { return selected; }
    void selectBand (int);

    // What the graph and strip do, each one undo step (a drag: one per gesture).
    void moveBand (int band, float hz, std::optional<float> gainDb, bool continuesGesture);
    void setBandQ (int band, float q, bool continuesGesture);
    void toggleBand (int band);
    void setBandType (int band, dsp::EqBandType);
    void setBandChannel (int band, int channel);

    /** Turns the first band that is off into a bell at hz and gainDb, and selects it. */
    void addBellAt (float hz, float gainDb);

    /** Plays the selected band alone while on (monitoring: never an undo step). */
    void setAuditioning (bool);
    bool isAuditioning() const noexcept   { return auditioning; }

    juce::Colour bandColour (int band) const;
    juce::Colour getColour() const noexcept   { return colour; }
    ThemeManager& getThemeManager() const noexcept   { return themeManager; }
    const NativeDevices& natives() const      { return rack.getNativeDevices(); }
    const juce::String& getPluginId() const   { return pluginId; }

    /** The span a modulator moves a band's parameter over, if one does. */
    std::optional<juce::Range<float>> modulationOf (int band, const juce::String& field) const;

private:
    struct BandButton;
    struct BandHeader;

    int selected = 0;
    bool auditioning = false, chosenSelection = false;
    EqEightGraph graph;
    std::array<std::unique_ptr<BandButton>, numBands> bandButtons;
    std::unique_ptr<BandHeader> header;
    IconButton audition;
    DeviceToggle channel;
    ParameterRow frequency, gain, q;
    DeviceToggle adaptive;
    Segmented mode;
    ParameterRow scale, output;
    int outputDividerY = -1;

    void parametersChanged() override;
    void readingsChanged() override;
    void showBandMenu (int band);
    ContinuousValue::Spec bandSpec (const juce::String& field);
};

} // namespace resamper
