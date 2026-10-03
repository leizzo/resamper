#include "CompressorDevice.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"

namespace resamper
{

namespace
{
    // Design: Device/Compressor v2 (520 wide; body padding 8, gap 8): Input 44,
    // Display 176 (an 18 px switch over the graph, 4 apart), Controls 172
    // (knobs, then the LA and DT switches, 6 apart), Output 72 behind a
    // divider with 8 px of padding.
    constexpr int bodyPadding = 8, bodyGap = 8, inputWidth = 44, controlsWidth = 172, outputWidth = 72,
                  switchHeight = 18, displayGap = 4, controlsGap = 6, outputPadding = 8, outputGap = 4, rowHeight = 18,
                  dialSize = 26, knobHeight = dialSize + 4 + 2 * 13, switchCaptionWidth = 14, makeupHeaderHeight = 10;

    // Design: meters 7 wide, 4 apart, radius 2; the caption mono 7, the reduction mono 7.5.
    constexpr int meterWidth = 7, meterGap = 4, meterCaptionHeight = 10;
    constexpr float meterFloorDb = -60.0f, reductionRangeDb = 24.0f, meterFallDbPerSecond = 30.0f,
                    meterHighDb = -3.0f, meterMidDb = -12.0f;
    constexpr int meterInset = 4, makeupRowGap = 2, gridDivisions = 4;

    // Design: the graph's radius and strokes, the threshold line and its tag, the live dot.
    constexpr float meterRadius = 2.0f, graphRadius = 5.0f, hairline = 1.0f, curveStroke = 1.6f, reductionStroke = 1.2f,
                    thresholdWidth = 2.0f, thresholdHit = 6.0f, dotSize = 7.0f, dotStroke = 1.5f, tagRadius = 3.0f,
                    tagPaddingX = 8.0f, tagHeight = 11.0f, tagInset = 4.0f, tagMargin = 2.0f;

    const TypeStyle smallMono { 7.0f, true, 400 };
    const TypeStyle reductionStyle { 7.5f, true, 400 };
    const TypeStyle switchCaptionStyle { 7.5f, true, 400 };
    const TypeStyle handleStyle { 7.5f, true, 700 };
    const TypeStyle captionStyle { 8.0f, false, 400 };

    /** Seconds of Activity history: one column per UI tick across the graph. */
    constexpr int historyLength = 180;

    juce::String minusNumber (float db, int decimals)
    {
        return designMinus (juce::String (db, decimals));
    }
}

//==============================================================================
DynamicsMeters::DynamicsMeters (ThemeManager& tm) : themeManager (tm)
{
    setComponentID ("DynamicsMeters");
    setTitle ("Input and gain reduction meters");
}

void DynamicsMeters::update (const DynamicsReading& reading, double elapsedSeconds)
{
    const auto fall = (float) (meterFallDbPerSecond * elapsedSeconds);
    const float levels[] = { reading.inputLeftDb, reading.inputRightDb };

    for (size_t ch = 0; ch < 2; ++ch)
        input[ch] = juce::jmax (levels[ch], input[ch] - fall, -120.0f);

    reduction = juce::jmax (reading.reductionDb, reduction - fall, 0.0f);
    repaint();
}

void DynamicsMeters::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto r = getLocalBounds();

    drawNumber (g, themeManager, "IN  GR", smallMono, r.removeFromTop (meterCaptionHeight), juce::Justification::centred, theme.textDim);
    auto value = r.removeFromBottom (meterCaptionHeight);
    r.removeFromTop (meterInset);
    r.removeFromBottom (meterInset);

    auto bars = r.withSizeKeepingCentre (3 * meterWidth + 2 * meterGap, r.getHeight());
    auto drawBar = [&] (juce::Rectangle<int> bar, float proportion, bool fromTop, juce::Colour fill)
    {
        g.setColour (theme.bgSlot);
        g.fillRoundedRectangle (bar.toFloat(), meterRadius);
        const auto h = juce::roundToInt ((float) bar.getHeight() * juce::jlimit (0.0f, 1.0f, proportion));

        if (h > 0)
        {
            g.setColour (fill);
            g.fillRoundedRectangle ((fromTop ? bar.removeFromTop (h) : bar.removeFromBottom (h)).toFloat(), meterRadius);
        }
    };

    for (size_t ch = 0; ch < 2; ++ch)
    {
        const auto db = input[ch];
        drawBar (bars.removeFromLeft (meterWidth), (db - meterFloorDb) / -meterFloorDb, false,
                 db > meterHighDb ? theme.meterHigh : (db > meterMidDb ? theme.meterMid : theme.meterLow));
        bars.removeFromLeft (meterGap);
    }

    drawBar (bars.removeFromLeft (meterWidth), reduction / reductionRangeDb, true, theme.stateWarning);
    drawNumber (g, themeManager, reduction > 0.05f ? minusNumber (-reduction, 1) : "0.0", reductionStyle, value,
                juce::Justification::centred, theme.stateWarning);
}

//==============================================================================
CompressorGraph::CompressorGraph (CompressorDevice& d)
    : device (d), levelHistory ((size_t) historyLength, -120.0f), reductionHistory ((size_t) historyLength, 0.0f)
{
    setComponentID ("CompressorGraph");
    setTitle ("Compressor curve");
    setDescription ("Drag the threshold line; double-click it to reset.");
}

float CompressorGraph::xForDb (float db) const
{
    return (float) getWidth() * (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb;
}

float CompressorGraph::dbAtX (float x) const
{
    return juce::jlimit (floorDb, 0.0f, floorDb + x / (float) juce::jmax (1, getWidth()) * -floorDb);
}

float CompressorGraph::yForDb (float db) const
{
    return (float) getHeight() * (1.0f - (juce::jlimit (floorDb, 0.0f, db) - floorDb) / -floorDb);
}

float CompressorGraph::dbAtY (float y) const
{
    return juce::jlimit (floorDb, 0.0f, floorDb + (1.0f - y / (float) juce::jmax (1, getHeight())) * -floorDb);
}

float CompressorGraph::thresholdPosition() const
{
    return activity ? yForDb (device.getThreshold()) : xForDb (device.getThreshold());
}

bool CompressorGraph::onThreshold (juce::Point<float> p) const
{
    return std::abs ((activity ? p.y : p.x) - thresholdPosition()) <= thresholdHit;
}

void CompressorGraph::setActivity (bool b)
{
    activity = b;
    repaint();
}

void CompressorGraph::refreshCurve()
{
    curve.resize ((size_t) juce::jmax (0, getWidth() + 1));

    for (size_t x = 0; x < curve.size(); ++x)
        curve[x] = device.transferAt (dbAtX ((float) x));

    repaint();
}

void CompressorGraph::addReading (const DynamicsReading& reading)
{
    const auto level = juce::jmax (reading.inputLeftDb, reading.inputRightDb);
    levelHistory[(size_t) historyWrite] = level;
    reductionHistory[(size_t) historyWrite] = reading.reductionDb;
    historyWrite = (historyWrite + 1) % historyLength;

    // The dot sits at the detector's level and what the compressor makes of it now.
    if (reading.detectorDb > floorDb)
        live = juce::Point<float> (reading.detectorDb, reading.detectorDb - reading.reductionDb);
    else
        live.reset();

    repaint();
}

void CompressorGraph::paint (juce::Graphics& g)
{
    auto& tm = device.getThemeManager();
    auto& theme = tm.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto w = bounds.getWidth(), h = bounds.getHeight();

    g.setColour (theme.bgSlot);
    g.fillRoundedRectangle (bounds, graphRadius);

    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (bounds, graphRadius);
    g.reduceClipRegion (clip);

    // A 4 x 4 grid.
    g.setColour (theme.gridBeat);

    for (int i = 1; i < gridDivisions; ++i)
    {
        g.fillRect (std::round (w * (float) i / gridDivisions), 0.0f, hairline, h);
        g.fillRect (0.0f, std::round (h * (float) i / gridDivisions), w, hairline);
    }

    const auto colour = device.getColour();
    const auto threshold = thresholdPosition();

    if (! activity)
    {
        // Unity, the curve and the area under it.
        g.setColour (theme.gridBar);
        g.drawLine (0, h, w, 0, hairline);

        juce::Path line;

        for (size_t x = 0; x < curve.size(); ++x)
        {
            const juce::Point<float> p ((float) x, yForDb (curve[x]));

            if (x == 0)
                line.startNewSubPath (p);
            else
                line.lineTo (p);
        }

        auto area = line;
        area.lineTo (w, h);
        area.lineTo (0, h);
        area.closeSubPath();
        g.setColour (colour.withAlpha (0.1f));
        g.fillPath (area);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (curveStroke));

        if (auto range = device.thresholdModulation())
        {
            g.setColour (theme.statePre.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xForDb (range->getStart()), 0, xForDb (range->getEnd()), h));
        }

        g.setColour (theme.accent);
        g.fillRect (threshold - thresholdWidth / 2, 0.0f, thresholdWidth, h);

        if (live)
        {
            const auto dot = juce::Rectangle<float> (dotSize, dotSize).withCentre ({ xForDb (live->x), yForDb (live->y) });
            g.setColour (theme.textPrimary);
            g.fillEllipse (dot);
            g.setColour (theme.bgDeep);
            g.drawEllipse (dot, dotStroke);
        }
    }
    else
    {
        // The level filled from the floor, the gain reduction hanging from the top, oldest on the left.
        juce::Path level, reduction;
        level.startNewSubPath (0, h);
        reduction.startNewSubPath (0, 0);

        for (int i = 0; i < historyLength; ++i)
        {
            const auto index = (size_t) ((historyWrite + i) % historyLength);
            const auto x = w * (float) i / (float) (historyLength - 1);
            level.lineTo (x, yForDb (levelHistory[index]));
            reduction.lineTo (x, h * juce::jlimit (0.0f, 1.0f, reductionHistory[index] / reductionRangeDb));
        }

        level.lineTo (w, h);
        level.closeSubPath();
        g.setColour (colour.withAlpha (0.25f));
        g.fillPath (level);
        g.setColour (theme.stateWarning);
        g.strokePath (reduction, juce::PathStrokeType (reductionStroke));

        g.setColour (theme.accent);
        g.fillRect (0.0f, threshold - thresholdWidth / 2, w, thresholdWidth);
    }

    // The threshold's handle: its value on an accent tag.
    const auto text = minusNumber ((float) juce::roundToInt (device.getThreshold()), 0);
    const auto textWidth = (float) juce::GlyphArrangement::getStringWidthInt (tm.numberFont (handleStyle), text) + tagPaddingX;
    const auto handle = activity ? juce::Rectangle<float> (tagInset, threshold - tagHeight / 2, textWidth, tagHeight)
                                 : juce::Rectangle<float> (threshold - textWidth / 2, tagInset, textWidth, tagHeight);
    const auto placed = handle.constrainedWithin (bounds.reduced (tagMargin));
    g.setColour (theme.accent);
    g.fillRoundedRectangle (placed, tagRadius);
    drawNumber (g, tm, text, handleStyle, placed.toNearestInt(), juce::Justification::centred, theme.textOnAccent);
}

void CompressorGraph::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (onThreshold (e.position) ? (activity ? juce::MouseCursor::UpDownResizeCursor
                                                         : juce::MouseCursor::LeftRightResizeCursor)
                                             : juce::MouseCursor::NormalCursor);
}

void CompressorGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = ! e.mods.isPopupMenu() && onThreshold (e.position);
    dragContinues = false;
}

void CompressorGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || ! e.mouseWasDraggedSinceMouseDown())
        return;

    device.setThreshold (activity ? dbAtY (e.position.y) : dbAtX (e.position.x), dragContinues);
    dragContinues = true;
}

void CompressorGraph::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
}

void CompressorGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (onThreshold (e.position))
        device.resetThreshold();
}

//==============================================================================
CompressorDevice::CompressorDevice (CommandRegistry& c, const PluginRack& r, ThemeManager& tm, const juce::String& id)
    : DeviceBody (c, r, tm, id),
      meters (tm),
      view (tm, { "TRANSFER", "ACTIVITY" }, Segmented::Style::device),
      graph (*this),
      ratio (tm, specFor ("ratio"), "Ratio"),
      attack (tm, specFor ("attack"), "Attack"),
      release (tm, specFor ("release"), "Release"),
      knee (tm, specFor ("knee"), "Knee"),
      lookahead (tm, { "0", "1", "10 ms" }, Segmented::Style::device),
      detect (tm, { "PEAK", "RMS", "EXP" }, Segmented::Style::device),
      makeupAuto (tm, "A", "Makeup Auto: sets the makeup from the threshold and ratio"),
      makeup (tm, specFor ("makeup"), {}),
      mix (tm, specFor ("mix"), "Mix"),
      output (tm, specFor ("output"), "Out")
{
    setComponentID ("Device/Compressor v2");

    view.setComponentID ("view");
    view.setTitle ("Display");
    view.onChange = [this] (int index) { graph.setActivity (index == 1); };

    for (auto [knob, parameterId] : { std::pair { &ratio, "ratio" }, { &attack, "attack" }, { &release, "release" }, { &knee, "knee" } })
    {
        knob->setComponentID (parameterId);
        knob->setDialSize (dialSize);
        knob->onChange = [this, key = juce::String (parameterId)] (double v, bool continues) { set (key, (float) v, continues); };
    }

    lookahead.setComponentID ("lookahead");
    lookahead.setTitle ("Lookahead");
    lookahead.onChange = [this] (int index) { set ("lookahead", (float) index); };
    detect.setComponentID ("detect");
    detect.setTitle ("Detection");
    detect.onChange = [this] (int index) { set ("detect", (float) index); };

    makeupAuto.setComponentID ("makeupAuto");
    makeupAuto.setSolid (true);
    makeupAuto.onClick = [this] { set ("makeupAuto", makeupAuto.getToggleState() ? 0.0f : 1.0f); };

    for (auto [row, parameterId] : { std::pair { &makeup, "makeup" }, { &mix, "mix" }, { &output, "output" } })
    {
        row->setComponentID (parameterId);
        row->onChange = [this, key = juce::String (parameterId)] (double v, bool continues) { set (key, (float) v, continues); };
    }

    makeup.setTitle ("Makeup");

    for (auto* child : std::initializer_list<juce::Component*> { &meters, &view, &graph, &ratio, &attack, &release, &knee,
                                                                &lookahead, &detect, &makeupAuto, &makeup, &mix, &output })
        addAndMakeVisible (child);

    setParameters (rack.getParameters (pluginId), true, colour);
}

float CompressorDevice::transferAt (float inputDb) const
{
    return rack.getNativeDevices().getTransferDb (pluginId, inputDb);
}

std::optional<juce::Range<float>> CompressorDevice::thresholdModulation() const
{
    return rack.getNativeDevices().getModulationRange (pluginId, "threshold");
}

void CompressorDevice::setThreshold (float db, bool continuesGesture)
{
    set ("threshold", db, continuesGesture);
}

void CompressorDevice::resetThreshold()
{
    if (auto* p = parameter ("threshold"))
        set ("threshold", p->defaultValue);
}

void CompressorDevice::parametersChanged()
{
    ratio.setValue (valueOf ("ratio"));
    attack.setValue (valueOf ("attack"));
    release.setValue (valueOf ("release"));
    knee.setValue (valueOf ("knee"));

    for (auto* knob : { &ratio, &attack, &release, &knee })
    {
        knob->setArcColour (colour);
        knob->setDimmed (! enabled);

        if (auto* p = parameter (knob->getComponentID()))
            knob->setAutomated (p->automated);
    }

    lookahead.setSelectedIndex (juce::roundToInt (valueOf ("lookahead")), juce::dontSendNotification);
    detect.setSelectedIndex (juce::roundToInt (valueOf ("detect")), juce::dontSendNotification);

    // With Auto on, Makeup shows what Auto sets and can't be dragged.
    const auto automatic = valueOf ("makeupAuto") >= 0.5f;
    makeupAuto.setToggleState (automatic, juce::dontSendNotification);
    makeupAuto.setColour (themeManager.getTheme().accent);
    makeup.setValue (valueOf ("makeup"));
    makeup.setEnabled (! automatic);
    makeup.setDisplayText (automatic ? "+" + juce::String (rack.getNativeDevices().getMakeupDb (pluginId), 1) : juce::String());

    mix.setValue (valueOf ("mix"));
    output.setValue (valueOf ("output"));
    graph.refreshCurve();
}

void CompressorDevice::readingsChanged()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto elapsed = lastReading == 0 ? 0.0 : (now - lastReading) / 1000.0;
    lastReading = now;

    const auto reading = rack.getNativeDevices().readDynamics (pluginId);
    meters.update (reading, elapsed);
    graph.addReading (reading);
}

int CompressorDevice::getPreferredWidth (bool expanded, int dockedWidth) const
{
    return expanded ? juce::jmax (compactWidth, dockedWidth) : compactWidth;
}

void CompressorDevice::focusFirstControl()
{
    ratio.grabKeyboardFocus();
}

void CompressorDevice::resized()
{
    auto area = getLocalBounds().reduced (bodyPadding);

    meters.setBounds (area.removeFromLeft (inputWidth));
    area.removeFromLeft (bodyGap);

    // Output hugs the right edge, Controls before it; Display takes what is left (it grows when expanded).
    outputArea = area.removeFromRight (outputWidth);
    area.removeFromRight (bodyGap);
    controlsArea = area.removeFromRight (controlsWidth);
    area.removeFromRight (bodyGap);

    auto display = area;
    view.setBounds (display.removeFromTop (switchHeight));
    display.removeFromTop (displayGap);
    graph.setBounds (display);
    graph.refreshCurve();

    auto controls = controlsArea;
    auto knobs = controls.removeFromTop (knobHeight);
    const auto knobWidth = knobs.getWidth() / 4;

    for (auto* knob : { &ratio, &attack, &release, &knee })
        knob->setBounds (knobs.removeFromLeft (knobWidth));

    controls.removeFromTop (controlsGap);
    lookahead.setBounds (controls.removeFromTop (switchHeight).withTrimmedLeft (switchCaptionWidth + 4));
    controls.removeFromTop (controlsGap);
    detect.setBounds (controls.removeFromTop (switchHeight).withTrimmedLeft (switchCaptionWidth + 4));

    auto out = outputArea.withTrimmedLeft (outputPadding);
    out.removeFromTop (meterCaptionHeight + outputGap);
    auto makeupHeader = out.removeFromTop (makeupHeaderHeight);
    makeupAuto.setBounds (makeupHeader.removeFromRight (makeupAuto.getIdealWidth()));
    out.removeFromTop (makeupRowGap);
    makeup.setBounds (out.removeFromTop (rowHeight));
    out.removeFromTop (outputGap);
    mix.setBounds (out.removeFromTop (rowHeight));
    out.removeFromTop (outputGap);
    output.setBounds (out.removeFromTop (rowHeight));
}

void CompressorDevice::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();

    // Captions: LA and DT before their switches, OUTPUT and Makeup in the Output zone.
    drawNumber (g, themeManager, "LA", switchCaptionStyle, lookahead.getBounds().withX (controlsArea.getX()).withWidth (switchCaptionWidth),
                juce::Justification::centredLeft, theme.textDim);
    drawNumber (g, themeManager, "DT", switchCaptionStyle, detect.getBounds().withX (controlsArea.getX()).withWidth (switchCaptionWidth),
                juce::Justification::centredLeft, theme.textDim);

    // The Output zone's divider (border-soft, its left edge).
    g.setColour (theme.borderSoft);
    g.fillRect (outputArea.getX(), outputArea.getY(), 1, outputArea.getHeight());

    auto out = outputArea.withTrimmedLeft (outputPadding);
    drawNumber (g, themeManager, "OUTPUT", smallMono, out.removeFromTop (meterCaptionHeight), juce::Justification::centredLeft, theme.textDim);
    out.removeFromTop (outputGap);
    drawStyledText (g, themeManager, "Makeup", captionStyle, out.removeFromTop (makeupHeaderHeight), juce::Justification::centredLeft,
                    theme.textDim);
}

} // namespace resamper
