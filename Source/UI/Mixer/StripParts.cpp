#include "StripParts.h"

namespace resamper
{

namespace
{
    constexpr int capWidth = 26, capHeight = 38, scaleWidth = 28, readoutHeight = 20, readoutGap = 8, meterGap = 3;

    /** Peaks above this are drawn red: near clipping. */
    constexpr double clipWarningDb = -1.5;
    constexpr double scaleMarks[] = { 6.0, 0.0, -6.0, -12.0, -24.0, -36.0, FaderLaw::floorDb };

    ContinuousValue::Spec faderSpec()
    {
        ContinuousValue::Spec spec;
        spec.minimum = FaderLaw::floorDb;
        spec.maximum = 6.0;
        spec.defaultValue = 0.0;
        spec.format = ValueFormat::decibels (FaderLaw::floorDb);
        spec.wheelStep = 0.5;
        spec.toProportion = [] (double db) { return 1.0 - FaderLaw::dbToTravel (db); };
        spec.fromProportion = [] (double p) { return FaderLaw::travelToDb (1.0 - p); };
        return spec;
    }
}

ContinuousValue::Spec panKnobSpec()
{
    ContinuousValue::Spec spec;
    spec.minimum = -1.0;
    spec.maximum = 1.0;
    spec.format = ValueFormat::pan();
    return spec;
}

ContinuousValue::Spec gainReadoutSpec()
{
    ContinuousValue::Spec spec;
    spec.minimum = FaderLaw::floorDb;
    spec.maximum = 6.0;
    spec.format = ValueFormat::decibels (FaderLaw::floorDb);
    spec.wheelStep = 0.5;
    return spec;
}

float yForDb (juce::Rectangle<float> travel, double db)
{
    return travel.getY() + (float) FaderLaw::dbToTravel (db) * travel.getHeight();
}

//==============================================================================
Fader::Fader (ThemeManager& tm) : ContinuousControl (tm, faderSpec(), Axis::vertical)
{
    setTitle ("Volume");
}

juce::Rectangle<int> Fader::getTravelBounds() const
{
    // Half a cap of room at each end, so the cap never leaves the component.
    return getLocalBounds().withTrimmedLeft (scaleWidth).reduced (0, capHeight / 2);
}

juce::Rectangle<float> Fader::capBounds() const
{
    const auto travel = getTravelBounds().toFloat();
    const auto y = yForDb (travel, getValue());
    return juce::Rectangle<float> ((float) capWidth, (float) capHeight).withCentre ({ travel.getCentreX(), y });
}

std::optional<double> Fader::getProportionAt (juce::Point<float> position) const
{
    if (capBounds().contains (position))
        return {};

    // The model's proportion runs up the travel; the law's travel runs down it.
    const auto travel = getTravelBounds().toFloat();
    return 1.0 - juce::jlimit (0.0, 1.0, (double) ((position.y - travel.getY()) / travel.getHeight()));
}

void Fader::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto travel = getTravelBounds().toFloat();

    // dB scale.
    const auto scaleFont = themeManager.numberFont (TypeStyle { 8.5f, true, 400 });
    g.setFont (scaleFont);

    for (auto db : scaleMarks)
    {
        const auto y = yForDb (travel, db);
        const auto label = db <= FaderLaw::floorDb ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"))
                                                   : (db > 0 ? "+" : "") + juce::String ((int) db);
        g.setColour (db == 0.0 ? theme.textSecondary : theme.textDim);
        g.drawText (label, juce::Rectangle<float> (0.0f, y - 6.0f, (float) scaleWidth - 6.0f, 12.0f), juce::Justification::centredRight, false);
        g.setColour (db == 0.0 ? theme.textDim : theme.border);
        g.fillRect (juce::Rectangle<float> ((float) scaleWidth - 5.0f, y, 5.0f, 1.0f));
    }

    // Track, and its fill below the cap.
    const auto track = juce::Rectangle<float> (4.0f, travel.getHeight()).withCentre (travel.getCentre());
    g.setColour (theme.bgSlot);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (theme.borderSoft);
    g.drawRoundedRectangle (track, 2.0f, 1.0f);

    const auto cap = capBounds();
    g.setColour (colour.withAlpha (0.35f));
    g.fillRoundedRectangle (track.withTop (cap.getCentreY()), 2.0f);

    // Cap: elevation level 1, bg-elevated, a centre line in the track colour.
    paintElevation (g, theme.elevation1, cap, theme.radiusMd);
    g.setColour (isMouseOverOrDragging() ? theme.bgHover : theme.bgElevated);
    g.fillRoundedRectangle (cap, theme.radiusMd);
    g.setColour (theme.border);
    g.drawRoundedRectangle (cap.reduced (0.5f), theme.radiusMd, 1.0f);
    g.setColour (colour);
    g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 8.0f, 2.0f));

    for (auto dy : { -6.0f, 6.0f })
    {
        g.setColour (theme.border);
        g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 12.0f, 1.0f).translated (0.0f, dy));
    }
}

//==============================================================================
StereoMeter::StereoMeter (ThemeManager& tm) : themeManager (tm)
{
    setTitle ("Meter");
    setTooltip ("Click to reset the peaks");
}

void StereoMeter::setLevel (StereoLevel level, double elapsedSeconds)
{
    const std::array<double, 2> raw { level.left, level.right };
    auto changed = false;

    for (size_t i = 0; i < 2; ++i)
    {
        // The bar follows the mode's ballistics; the peak hold always sees the true peak.
        const auto shown = ballistics[i].update (raw[i], elapsedSeconds);
        const auto before = holds[i].get();
        holds[i].update (raw[i], elapsedSeconds);
        changed = changed || std::abs (shown - levels[i]) > 0.1 || std::abs (holds[i].get() - before) > 0.05;
        levels[i] = shown;
    }

    if (changed)
        repaint();
}

void StereoMeter::setMode (MeterMode mode)
{
    for (auto& b : ballistics)
        b.setMode (mode);

    resetPeaks();
}

void StereoMeter::resetPeaks()
{
    for (auto& hold : holds)
        hold.reset();

    repaint();
}

void StereoMeter::mouseDown (const juce::MouseEvent&)
{
    resetPeaks();

    if (onPeaksReset)
        onPeaksReset();
}

void StereoMeter::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto gap = juce::jmax (1.0f, bounds.getWidth() - 2.0f * wellWidth);

    // One gradient over the full height; the level only clips it.
    juce::ColourGradient ramp (theme.meterHigh, 0.0f, bounds.getY(), theme.meterLow, 0.0f, bounds.getBottom(), false);
    ramp.addColour (0.2, theme.meterMid);

    for (size_t i = 0; i < 2; ++i)
    {
        const auto well = juce::Rectangle<float> (bounds.getX() + (float) i * (wellWidth + gap), bounds.getY(), wellWidth, bounds.getHeight());
        g.setColour (theme.bgSlot);
        g.fillRoundedRectangle (well, theme.radiusXs);

        const auto top = yForDb (well, levels[i]);

        if (top < well.getBottom())
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (well.withTop (top).getSmallestIntegerContainer());
            g.setGradientFill (ramp);
            g.fillRoundedRectangle (well, theme.radiusXs);
        }

        const auto peak = holds[i].get();

        if (peak > FaderLaw::floorDb)
        {
            const auto y = yForDb (well, peak);
            g.setColour (peak > clipWarningDb ? theme.meterHigh : peak > -12.0 ? theme.meterMid : theme.meterLow);
            g.fillRect (well.withY (y - 1.0f).withHeight (2.0f));
        }
    }
}

//==============================================================================
FaderSection::FaderSection (ThemeManager& tm, Geometry g)
    : themeManager (tm), geometry (g), gain (tm, gainReadoutSpec()), fader (tm), meter (tm)
{
    setInterceptsMouseClicks (false, true);

    auto changeVolume = [this] (double db, bool continues) { if (onVolumeChange) onVolumeChange (Decibels (db), continues); };
    fader.onChange = changeVolume;
    gain.onChange = changeVolume;
    gain.setTitle ("Gain");
    gain.setTooltip ("Gain: click to type");
    gain.setDoubleClickEdits (false);

    meter.setWellWidth (geometry.meterWellWidth);
    meter.onPeaksReset = [this] { repaint (peakReadout); };

    for (auto* child : std::initializer_list<juce::Component*> { &gain, &fader, &meter })
        addAndMakeVisible (child);
}

void FaderSection::setVolume (Decibels volume, juce::Colour colour)
{
    fader.setValue (volume.value);
    fader.setColour (colour);
    gain.setValue (volume.value);
}

void FaderSection::setLevel (StereoLevel level, double elapsedSeconds)
{
    const auto peakBefore = meter.getPeakDb();
    meter.setLevel (level, elapsedSeconds);

    if (std::abs (meter.getPeakDb() - peakBefore) > 0.05)
        repaint (peakReadout);
}

void FaderSection::resetPeaks()
{
    meter.resetPeaks();
    repaint (peakReadout);
}

void FaderSection::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto peak = meter.getPeakDb();
    g.setColour (theme.bgSlot);
    g.fillRoundedRectangle (peakReadout.toFloat(), theme.radiusMd);
    drawNumber (g, themeManager, peak <= FaderLaw::floorDb ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")) : juce::String (peak, 1),
                TypeStyle { 10.0f, true, 400 }, peakReadout, juce::Justification::centred,
                peak > clipWarningDb ? theme.meterHigh : theme.textSecondary);
}

void FaderSection::resized()
{
    using namespace StripMetrics;
    auto r = getLocalBounds().reduced (padX, sectionPadY);

    auto readouts = r.removeFromTop (readoutHeight);
    gain.setBounds (readouts.removeFromLeft ((readouts.getWidth() - rowGap) / 2));
    readouts.removeFromLeft (rowGap);
    peakReadout = readouts;
    r.removeFromTop (readoutGap);

    // The meter at the right edge; the fader from the left, its track where the design puts it.
    const auto meterWidth = juce::roundToInt (2.0f * geometry.meterWellWidth) + meterGap;
    auto meterColumn = r.removeFromRight (meterWidth);
    fader.setBounds (r.removeFromLeft (juce::jmin (geometry.faderWidth, r.getWidth())));
    meter.setBounds (meterColumn.withY (fader.getY() + fader.getTravelBounds().getY())
                                .withHeight (fader.getTravelBounds().getHeight()));
}

void FaderSection::mouseDown (const juce::MouseEvent& e)
{
    if (peakReadout.contains (e.getPosition()))
        resetPeaks();
}

void paintColourBar (juce::Graphics& g, juce::Rectangle<float> strip, float radius, juce::Colour colour)
{
    juce::Graphics::ScopedSaveState save (g);
    juce::Path shape;
    shape.addRoundedRectangle (strip, radius);
    g.reduceClipRegion (shape);
    g.setColour (colour);
    g.fillRect (strip.withHeight ((float) StripMetrics::colourBarHeight));
}

} // namespace resamper
