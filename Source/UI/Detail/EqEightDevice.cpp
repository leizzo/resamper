#include "EqEightDevice.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    // Design: Device/EQ Eight v2 (470 wide; body padding 8, gap 8; Display: a
    // 96 px graph over an 18 px band strip, 4 apart; the band panel 150 wide,
    // rows 16 high and 3 apart, the toggles and Output rows 18).
    constexpr int bodyPadding = 8, bodyGap = 8, panelWidth = 150, graphHeight = 96, stripHeight = 18, displayGap = 4,
                  stripGap = 3, rowHeight = 16, rowGap = 3, toggleHeight = 18, auditionWidth = 18, outputPaddingTop = 2;
    constexpr float nodeSize = 14.0f, nodeHit = 9.0f, spectrumFloorDb = -90.0f;

    // Design: radii, strokes and the pieces inside the graph and the strip.
    constexpr float cellRadius = 3.0f, graphRadius = 5.0f, hairline = 1.0f, curveStroke = 1.5f, nodeStroke = 1.5f,
                    glyphStroke = 1.3f, glyphWidth = 14.0f, glyphHeight = 11.0f, glyphGap = 3.0f, pointSpacing = 2.0f;
    constexpr int bandDotSize = 7, bandDotGap = 5, gridLabelInset = 2, gridLabelWidth = 24, gridLabelHeight = 9,
                  gridLabelBottom = 10, overlayInset = 6, overlayTop = 5, overlayPaddingX = 4, overlayPaddingY = 1,
                  overlayGap = 4, preLabelWidth = 15, rangeLabelWidth = 40, numberPadding = 2;
    const juce::Rectangle<float> spectrumToggleBounds { 6.0f, 5.0f, 46.0f, 11.0f };

    const TypeStyle nodeStyle { 7.5f, true, 700 };
    const TypeStyle gridLabelStyle { 6.5f, true, 400 };
    const TypeStyle overlayStyle { 7.0f, true, 400 };
    const TypeStyle bandTitleStyle { 9.5f, false, 600 };

    const char* const typeNames[] = { NEEDS_TRANS ("Low Cut"), NEEDS_TRANS ("Low Shelf"), NEEDS_TRANS ("Bell"),
                                       NEEDS_TRANS ("Notch"), NEEDS_TRANS ("High Shelf"), NEEDS_TRANS ("High Cut") };

    /** The design's type glyphs (14 x 11): cut, shelf, bell, notch. */
    juce::Path typeGlyph (dsp::EqBandType type)
    {
        juce::Path p;

        switch (type)
        {
            case dsp::EqBandType::lowCut:    p = juce::Drawable::parseSVGPath ("M0 11c2-2 3-5 6-5l8 0"); break;
            case dsp::EqBandType::lowShelf:  p = juce::Drawable::parseSVGPath ("M0 3l4 0c3 0 3 6 6 6l4 0"); break;
            case dsp::EqBandType::bell:      p = juce::Drawable::parseSVGPath ("M0 9c4 0 5-7 7-7 2 0 3 7 7 7"); break;
            case dsp::EqBandType::notch:     p = juce::Drawable::parseSVGPath ("M0 2c4 0 5 7 7 7 2 0 3-7 7-7"); break;
            case dsp::EqBandType::highShelf: p = juce::Drawable::parseSVGPath ("M0 9l4 0c3 0 3-6 6-6l4 0"); break;
            case dsp::EqBandType::highCut:   p = juce::Drawable::parseSVGPath ("M0 6l8 0c3 0 4 3 6 5"); break;
        }

        return p;
    }

}

//==============================================================================
/** One cell of the band strip: the type glyph and the number. Click selects
    the band, double-click switches it on or off, right-click offers the rest. */
struct EqEightDevice::BandButton : public ThemedButton
{
    BandButton (EqEightDevice& d, int b) : ThemedButton (d.themeManager, tr ("Band %1", b + 1)), device (d), index (b)
    {
        setComponentID ("band" + juce::String (b + 1));
        setClickingTogglesState (false);
        onClick = [this] { device.selectBand (index); };
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            device.selectBand (index);
            device.showBandMenu (index);
            return;
        }

        ThemedButton::mouseDown (e);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override   { device.toggleBand (index); }

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        auto& theme = themeManager.getTheme();
        const auto b = device.band (index);
        const auto selectedBand = device.getSelectedBand() == index;
        const auto bandColour = device.bandColour (index);
        const auto bounds = getLocalBounds().toFloat();

        // Design: on is bg-elevated, off is bg-slot at half opacity, selected a 20 % tint with a band-coloured outline.
        juce::Graphics::ScopedSaveState save (g);

        if (! b.on && ! selectedBand)
            g.setOpacity (0.5f);

        g.setColour (selectedBand ? bandColour.withAlpha (0.2f) : (b.on ? (highlighted ? theme.bgHover : theme.bgElevated) : theme.bgSlot));
        g.fillRoundedRectangle (bounds, cellRadius);

        if (selectedBand)
        {
            g.setColour (bandColour);
            g.drawRoundedRectangle (bounds.reduced (0.5f), cellRadius, hairline);
        }

        const auto glyphColour = b.on ? bandColour : theme.textDim;
        auto glyph = typeGlyph (b.type);
        const auto number = juce::String (index + 1);
        const auto numberWidth = (float) juce::GlyphArrangement::getStringWidthInt (themeManager.numberFont (nodeStyle), number);
        const auto total = glyphWidth + glyphGap + numberWidth;
        const auto left = bounds.getCentreX() - total / 2;
        glyph.applyTransform (glyph.getTransformToScaleToFit ({ left, bounds.getCentreY() - glyphHeight / 2, glyphWidth, glyphHeight }, false));
        g.setColour (glyphColour);
        g.strokePath (glyph, juce::PathStrokeType (glyphStroke));
        drawNumber (g, themeManager, number, nodeStyle,
                    juce::Rectangle<float> (left + glyphWidth + glyphGap, bounds.getY(), numberWidth + numberPadding, bounds.getHeight()).toNearestInt(),
                    juce::Justification::centredLeft, b.on ? theme.textPrimary : theme.textDim);

        if (hasKeyboardFocus (false))
            paintFocus (g, cellRadius);
    }

    EqEightDevice& device;
    const int index;
};

/** The band panel's title: its colour dot and "Band 3 · Bell". Clicking it picks the type. */
struct EqEightDevice::BandHeader : public juce::Component
{
    explicit BandHeader (EqEightDevice& d) : device (d)
    {
        setComponentID ("bandType");
        setTitle (TRANS ("Band type"));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto& theme = device.getThemeManager().getTheme();
        const auto b = device.getSelectedBand();
        auto r = getLocalBounds();
        g.setColour (device.bandColour (b));
        g.fillEllipse (r.removeFromLeft (bandDotSize).withSizeKeepingCentre (bandDotSize, bandDotSize).toFloat());
        r.removeFromLeft (bandDotGap);
        drawStyledText (g, device.getThemeManager(),
                        tr ("Band %1", b + 1) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + TRANS (typeNames[(int) device.band (b).type]),
                        bandTitleStyle, r, juce::Justification::centredLeft, theme.textPrimary);
    }

    void mouseDown (const juce::MouseEvent&) override   { device.showBandMenu (device.getSelectedBand()); }

    EqEightDevice& device;
};

//==============================================================================
EqEightGraph::EqEightGraph (EqEightDevice& d) : device (d)
{
    setComponentID ("EqGraph");
    setTitle (TRANS ("EQ curve"));
    setDescription (TRANS ("Drag a node: frequency and gain. Wheel: Q. Double-click a node: band on or off; empty space: add a bell."));
}

float EqEightGraph::xForFrequency (float hz) const
{
    return (float) getWidth() * std::log (juce::jlimit (lowestHz, highestHz, hz) / lowestHz) / std::log (highestHz / lowestHz);
}

float EqEightGraph::frequencyAtX (float x) const
{
    return lowestHz * std::pow (highestHz / lowestHz, juce::jlimit (0.0f, 1.0f, x / (float) juce::jmax (1, getWidth())));
}

float EqEightGraph::yForGain (float db) const
{
    const auto half = (float) getHeight() / 2;
    return half - db / rangeDb * (half - nodeSize / 2);
}

float EqEightGraph::gainAtY (float y) const
{
    const auto half = (float) getHeight() / 2;
    return juce::jlimit (-rangeDb, rangeDb, (half - y) / (half - nodeSize / 2) * rangeDb);
}

juce::Point<float> EqEightGraph::nodePosition (int band) const
{
    const auto b = device.band (band);
    return { xForFrequency (b.frequency), yForGain (dsp::hasGain (b.type) ? b.gain : 0.0f) };
}

int EqEightGraph::nodeAt (juce::Point<float> p) const
{
    int best = -1;
    auto bestDistance = nodeHit;

    // The selected band's node is drawn on top, so it wins a tie.
    for (int b = 0; b < EqEightDevice::numBands; ++b)
    {
        const auto distance = nodePosition (b).getDistanceFrom (p);

        if (distance < bestDistance || (distance <= nodeHit && b == device.getSelectedBand()))
        {
            best = b;
            bestDistance = distance;
        }
    }

    return best;
}

void EqEightGraph::setShowsPostSpectrum (bool b)
{
    post = b;
    refreshSpectrum();
}

void EqEightGraph::resized()
{
    // One point every two pixels across the range.
    frequencies.clear();

    for (int x = 0; x <= getWidth(); x += (int) pointSpacing)
        frequencies.push_back (frequencyAtX ((float) x));

    refreshCurves();
    refreshSpectrum();
}

void EqEightGraph::refreshCurves()
{
    auto& natives = device.natives();
    natives.getEqResponse (device.getPluginId(), frequencies, curve, -1, 0);

    // L/R and M/S: a band on one side only bends the other side's curve, drawn fainter.
    showSideCurve = false;

    if (device.getMode() != 0)
        for (int b = 0; b < EqEightDevice::numBands; ++b)
            showSideCurve = showSideCurve || (device.band (b).on && device.band (b).channel != 0);

    if (showSideCurve)
        natives.getEqResponse (device.getPluginId(), frequencies, sideCurve, -1, 1);

    repaint();
}

void EqEightGraph::refreshSpectrum()
{
    if (device.natives().getSpectrum (device.getPluginId(), post, frequencies, spectrum))
        repaint();
}

juce::Rectangle<float> EqEightGraph::spectrumToggle() const
{
    // Design: 6, 5 from the corner; "PRE POST" in mono 7, 1 / 4 padding, 4 apart.
    return spectrumToggleBounds;
}

juce::Path EqEightGraph::curvePath (const std::vector<float>& db) const
{
    juce::Path p;

    for (size_t i = 0; i < db.size() && i < frequencies.size(); ++i)
    {
        const juce::Point<float> point ((float) i * pointSpacing, juce::jlimit (0.0f, (float) getHeight(), yForGain (db[i])));

        if (i == 0)
            p.startNewSubPath (point);
        else
            p.lineTo (point);
    }

    return p;
}

void EqEightGraph::paint (juce::Graphics& g)
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

    // Grid: 0 dB brighter, half the range either side; the decades the design labels.
    for (auto db : { -rangeDb / 2, 0.0f, rangeDb / 2 })
    {
        g.setColour (juce::exactlyEqual (db, 0.0f) ? theme.gridBar : theme.gridBeat);
        g.fillRect (0.0f, std::round (yForGain (db)), w, hairline);
    }

    for (auto [hz, label] : { std::pair { 50.0f, "50" }, { 200.0f, "200" }, { 1000.0f, "1k" }, { 5000.0f, "5k" }, { 15000.0f, "15k" } })
    {
        const auto x = std::round (xForFrequency (hz));
        g.setColour (theme.gridBeat);
        g.fillRect (x, 0.0f, hairline, h);
        drawNumber (g, tm, label, gridLabelStyle, juce::Rectangle<int> ((int) x + gridLabelInset, (int) h - gridLabelBottom, gridLabelWidth, gridLabelHeight),
                    juce::Justification::centredLeft, theme.textDim);
    }

    // The spectrum, filled from the floor.
    if (! spectrum.empty())
    {
        juce::Path fill;
        fill.startNewSubPath (0, h);

        for (size_t i = 0; i < spectrum.size(); ++i)
            fill.lineTo ((float) i * pointSpacing, h * (1.0f - juce::jlimit (0.0f, 1.0f, (spectrum[i] - spectrumFloorDb) / -spectrumFloorDb)));

        fill.lineTo ((float) (spectrum.size() - 1) * pointSpacing, h);
        fill.closeSubPath();
        g.setColour (theme.textPrimary.withAlpha (0.06f));
        g.fillPath (fill);
    }

    // The selected band: its Q width shaded, a guide at its frequency.
    const auto selected = device.getSelectedBand();
    const auto sel = device.band (selected);
    const auto selColour = device.bandColour (selected);

    if (sel.on)
    {
        const auto ratio = (float) dsp::qEdgeRatio (sel.q);
        const auto left = xForFrequency (sel.frequency / ratio), right = xForFrequency (sel.frequency * ratio);
        g.setColour (selColour.withAlpha (0.08f));
        g.fillRect (left, 0.0f, right - left, h);
        g.setColour (selColour.withAlpha (0.4f));
        g.fillRect (std::round (xForFrequency (sel.frequency)), 0.0f, hairline, h);
    }

    // The curve, with the area between it and 0 dB.
    const auto colour = device.getColour();

    if (! curve.empty())
    {
        auto line = curvePath (curve);
        auto area = line;
        area.lineTo ((float) (curve.size() - 1) * pointSpacing, yForGain (0));
        area.lineTo (0, yForGain (0));
        area.closeSubPath();
        g.setColour (colour.withAlpha (0.12f));
        g.fillPath (area);

        if (showSideCurve)
        {
            g.setColour (colour.withAlpha (0.45f));
            g.strokePath (curvePath (sideCurve), juce::PathStrokeType (hairline));
        }

        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (curveStroke));
    }

    // Modulation ranges (the Mods Drawer): a bar along each modulated axis of a node.
    for (int b = 0; b < EqEightDevice::numBands; ++b)
    {
        const auto node = nodePosition (b);
        g.setColour (theme.statePre.withAlpha (0.7f));

        if (auto range = device.modulationOf (b, "Freq"))
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xForFrequency (range->getStart()), node.y - 1,
                                                                    xForFrequency (range->getEnd()), node.y + 1));

        if (auto range = device.modulationOf (b, "Gain"))
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (node.x - 1, yForGain (range->getEnd()),
                                                                    node.x + 1, yForGain (range->getStart())));
    }

    // Nodes, the selected one last (on top).
    auto drawNode = [&] (int b)
    {
        const auto band = device.band (b);
        const auto isSelected = b == selected;
        const auto bandColour = device.bandColour (b);
        const auto r = juce::Rectangle<float> (nodeSize, nodeSize).withCentre (nodePosition (b));

        juce::Graphics::ScopedSaveState nodeState (g);

        if (! band.on)
            g.setOpacity (0.5f);

        g.setColour (isSelected ? bandColour : theme.bgDeep);
        g.fillEllipse (r);
        g.setColour (isSelected ? theme.textPrimary : (band.on ? bandColour : theme.textDim));
        g.drawEllipse (r.reduced (nodeStroke / 2), nodeStroke);
        drawNumber (g, tm, juce::String (b + 1), nodeStyle, r.toNearestInt(), juce::Justification::centred,
                    isSelected ? theme.textOnAccent : (band.on ? bandColour : theme.textDim));
    };

    for (int b = 0; b < EqEightDevice::numBands; ++b)
        if (b != selected)
            drawNode (b);

    drawNode (selected);

    // Overlays: the spectrum's PRE / POST switch and the range.
    const auto toggle = spectrumToggle();
    g.setColour (theme.bgDeep.withAlpha (0.6f));
    g.fillRoundedRectangle (toggle, cellRadius);
    auto t = toggle.toNearestInt().reduced (overlayPaddingX, overlayPaddingY);
    // PRE / POST and the range read as on hardware in every UI Language.
    drawNumber (g, tm, "PRE", TypeStyle { overlayStyle.size, true, post ? 400 : 700 }, t.removeFromLeft (preLabelWidth),
                juce::Justification::centredLeft, post ? theme.textDim : theme.textPrimary);
    t.removeFromLeft (overlayGap);
    drawNumber (g, tm, "POST", TypeStyle { overlayStyle.size, true, post ? 700 : 400 }, t, juce::Justification::centredLeft,
                post ? theme.textPrimary : theme.textDim);
    drawNumber (g, tm, juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (juce::roundToInt (rangeDb)) + " dB",
                overlayStyle, juce::Rectangle<int> ((int) w - overlayInset - rangeLabelWidth, overlayTop, rangeLabelWidth, gridLabelHeight), juce::Justification::centredRight,
                theme.textDim);
}

void EqEightGraph::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (nodeAt (e.position) >= 0 || spectrumToggle().contains (e.position) ? juce::MouseCursor::PointingHandCursor
                                                                                       : juce::MouseCursor::NormalCursor);
}

void EqEightGraph::mouseDown (const juce::MouseEvent& e)
{
    dragBand = -1;

    if (e.mods.isPopupMenu())
        return;

    if (spectrumToggle().contains (e.position))
    {
        setShowsPostSpectrum (! post);
        return;
    }

    if (const auto b = nodeAt (e.position); b >= 0)
    {
        device.selectBand (b);
        dragBand = b;
        dragContinues = false;
    }
}

void EqEightGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0 || ! e.mouseWasDraggedSinceMouseDown())
        return;

    const auto b = device.band (dragBand);
    const auto position = e.position.withX (juce::jlimit (0.0f, (float) getWidth(), e.position.x));
    device.moveBand (dragBand, frequencyAtX (position.x),
                     dsp::hasGain (b.type) ? std::optional<float> (gainAtY (position.y)) : std::nullopt, dragContinues);
    dragContinues = true;
}

void EqEightGraph::mouseUp (const juce::MouseEvent&)
{
    dragBand = -1;
}

void EqEightGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (spectrumToggle().contains (e.position))
        return;

    if (const auto b = nodeAt (e.position); b >= 0)
        device.toggleBand (b);
    else
        device.addBellAt (frequencyAtX (e.position.x), gainAtY (e.position.y));
}

void EqEightGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto b = nodeAt (e.position);

    // Off a node the wheel is the parent's: the chain scrolls.
    if (b < 0)
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }

    auto delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;

    if (wheel.isSmooth)
    {
        // A trackpad sends small deltas: a notch is 0.05 of them.
        wheelAccumulator += delta;
        delta = std::trunc (wheelAccumulator / 0.05f);
        wheelAccumulator -= delta * 0.05f;
    }
    else
    {
        delta = delta > 0 ? 1.0f : (delta < 0 ? -1.0f : 0.0f);
    }

    if (juce::exactlyEqual (delta, 0.0f))
        return;

    // Wheel notches on one node close together are one gesture: one undo step.
    const auto now = juce::Time::getMillisecondCounter();
    const auto continues = b == wheelBand && now - lastWheel < 500;
    wheelBand = b;
    lastWheel = now;

    device.selectBand (b);
    device.setBandQ (b, device.band (b).q * std::pow (1.12f, delta * (e.mods.isShiftDown() ? 0.1f : 1.0f)), continues);
}

//==============================================================================
EqEightDevice::EqEightDevice (CommandRegistry& c, const PluginRack& r, ThemeManager& tm, const juce::String& id)
    : DeviceBody (c, r, tm, id),
      graph (*this),
      header (std::make_unique<BandHeader> (*this)),
      audition (tm, TRANS ("Audition the band"), Icon::headphones, IconButton::Kind::small),
      channel (tm, "L+R", TRANS ("Which side the band acts on")),
      frequency (tm, bandSpec ("Freq"), TRANS ("Freq")),
      gain (tm, bandSpec ("Gain"), TRANS ("Gain")),
      q (tm, bandSpec ("Q"), "Q"),
      adaptive (tm, "ADPT Q", TRANS ("Adaptive Q: Q grows with the boost or cut")),
      mode (tm, { "ST", "L/R", "M/S" }, Segmented::Style::device),
      scale (tm, specFor ("scale"), TRANS ("Scale")),
      output (tm, specFor ("output"), TRANS ("Out"))
{
    setComponentID ("Device/EQ Eight v2");

    for (int b = 0; b < numBands; ++b)
    {
        bandButtons[(size_t) b] = std::make_unique<BandButton> (*this, b);
        addAndMakeVisible (*bandButtons[(size_t) b]);
    }

    audition.setComponentID ("audition");
    audition.setClickingTogglesState (false);
    audition.onClick = [this] { setAuditioning (! auditioning); };
    channel.setComponentID ("channel");
    channel.onClick = [this] { setBandChannel (selected, (band (selected).channel + 1) % 3); };
    frequency.setComponentID ("freq");
    gain.setComponentID ("gain");
    q.setComponentID ("q");
    frequency.onChange = [this] (double v, bool continues) { set (NativeDevices::bandParameter (selected, "Freq"), (float) v, continues); };
    gain.onChange = [this] (double v, bool continues) { set (NativeDevices::bandParameter (selected, "Gain"), (float) v, continues); };
    q.onChange = [this] (double v, bool continues) { set (NativeDevices::bandParameter (selected, "Q"), (float) v, continues); };
    adaptive.setComponentID ("adaptiveQ");
    adaptive.onClick = [this] { set ("adaptiveQ", adaptive.getToggleState() ? 0.0f : 1.0f); };
    mode.setComponentID ("mode");
    mode.setTitle (TRANS ("Stereo mode"));
    mode.onChange = [this] (int index) { set ("mode", (float) index); };
    scale.setComponentID ("scale");
    output.setComponentID ("output");
    scale.onChange = [this] (double v, bool continues) { set ("scale", (float) v, continues); };
    output.onChange = [this] (double v, bool continues) { set ("output", (float) v, continues); };

    for (auto* child : std::initializer_list<juce::Component*> { &graph, header.get(), &audition, &channel, &frequency, &gain, &q,
                                                            &adaptive, &mode, &scale, &output })
        addAndMakeVisible (child);

    setParameters (rack.getParameters (pluginId), true, colour);
}

EqEightDevice::~EqEightDevice()
{
    // Auditioning is a monitoring gesture: it ends with the device's view.
    if (auditioning)
        commands.invoke (cmd::pluginAudition, { pluginId, -1 });
}

ContinuousValue::Spec EqEightDevice::bandSpec (const juce::String& field)
{
    // Every band has the same range; the text follows the band the panel shows.
    for (auto& p : rack.getParameters (pluginId))
        if (p.id == NativeDevices::bandParameter (0, field))
        {
            auto spec = parameterSpec (rack, pluginId, p);
            spec.format.format = [this, field] (double v)
            {
                return designMinus (rack.getParameterText (pluginId, NativeDevices::bandParameter (selected, field), (float) v));
            };
            return spec;
        }

    return {};
}

EqEightDevice::Band EqEightDevice::band (int b) const
{
    Band result;
    result.on = valueOf (NativeDevices::bandParameter (b, "On")) >= 0.5f;
    result.type = (dsp::EqBandType) juce::jlimit (0, dsp::numEqBandTypes - 1, juce::roundToInt (valueOf (NativeDevices::bandParameter (b, "Type"))));
    result.frequency = valueOf (NativeDevices::bandParameter (b, "Freq"));
    result.gain = valueOf (NativeDevices::bandParameter (b, "Gain"));
    result.q = valueOf (NativeDevices::bandParameter (b, "Q"));
    result.channel = juce::roundToInt (valueOf (NativeDevices::bandParameter (b, "Channel")));
    return result;
}

int EqEightDevice::getMode() const
{
    return juce::roundToInt (valueOf ("mode"));
}

juce::Colour EqEightDevice::bandColour (int b) const
{
    // Design: 1 bass red, 2 drums orange, 3 fx green, 4 pads blue; then chords, vocal, arp and the accent.
    static constexpr std::array<int, 7> paletteIndex { 1, 0, 6, 3, 2, 5, 4 };
    auto& theme = themeManager.getTheme();
    return juce::isPositiveAndBelow (b, (int) paletteIndex.size()) ? theme.trackColour (paletteIndex[(size_t) b]) : theme.accent;
}

std::optional<juce::Range<float>> EqEightDevice::modulationOf (int b, const juce::String& field) const
{
    return natives().getModulationRange (pluginId, NativeDevices::bandParameter (b, field));
}

void EqEightDevice::selectBand (int b)
{
    if (! juce::isPositiveAndBelow (b, numBands))
        return;

    chosenSelection = true;

    if (b == selected)
        return;

    selected = b;

    if (auditioning)
        commands.invoke (cmd::pluginAudition, { pluginId, selected });

    parametersChanged();
}

void EqEightDevice::moveBand (int b, float hz, std::optional<float> gainDb, bool continuesGesture)
{
    std::vector<ParameterValue> values { { NativeDevices::bandParameter (b, "Freq"), hz } };

    if (gainDb)
        values.push_back ({ NativeDevices::bandParameter (b, "Gain"), *gainDb });

    setTogether (values, continuesGesture);
}

void EqEightDevice::setBandQ (int b, float value, bool continuesGesture)
{
    set (NativeDevices::bandParameter (b, "Q"), value, continuesGesture);
}

void EqEightDevice::toggleBand (int b)
{
    set (NativeDevices::bandParameter (b, "On"), band (b).on ? 0.0f : 1.0f);
}

void EqEightDevice::setBandType (int b, dsp::EqBandType type)
{
    set (NativeDevices::bandParameter (b, "Type"), (float) type);
}

void EqEightDevice::setBandChannel (int b, int side)
{
    set (NativeDevices::bandParameter (b, "Channel"), (float) side);
}

void EqEightDevice::addBellAt (float hz, float gainDb)
{
    for (int b = 0; b < numBands; ++b)
        if (! band (b).on)
        {
            setTogether ({ { NativeDevices::bandParameter (b, "Type"), (float) dsp::EqBandType::bell },
                           { NativeDevices::bandParameter (b, "Freq"), hz },
                           { NativeDevices::bandParameter (b, "Gain"), gainDb },
                           { NativeDevices::bandParameter (b, "On"), 1.0f } }, false);
            selectBand (b);
            return;
        }
}

void EqEightDevice::setAuditioning (bool on)
{
    auditioning = on;
    commands.invoke (cmd::pluginAudition, { pluginId, on ? selected : -1 });
    audition.setToggleState (on, juce::dontSendNotification);
}

void EqEightDevice::showBandMenu (int b)
{
    // The menu outlives a device removed while it is open: its items check first.
    juce::PopupMenu menu;
    const auto current = band (b);
    juce::Component::SafePointer<EqEightDevice> safe (this);
    menu.addItem (current.on ? TRANS ("Turn Off") : TRANS ("Turn On"), [safe, b] { if (safe != nullptr) safe->toggleBand (b); });
    menu.addSeparator();

    for (int t = 0; t < dsp::numEqBandTypes; ++t)
        menu.addItem (TRANS (typeNames[t]), true, (int) current.type == t, [safe, b, t]
        {
            if (safe != nullptr)
                safe->setBandType (b, (dsp::EqBandType) t);
        });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bandButtons[(size_t) b].get()));
}

void EqEightDevice::parametersChanged()
{
    // Until the user picks one, the panel shows the first band that is on.
    if (! chosenSelection)
        for (int b = 0; b < numBands; ++b)
            if (band (b).on)
            {
                selected = b;
                break;
            }

    const auto b = band (selected);
    frequency.setValue (b.frequency);
    gain.setValue (b.gain);
    q.setValue (b.q);
    gain.setEnabled (dsp::hasGain (b.type));
    frequency.setTooltip (tr ("Band %1 frequency", selected + 1));

    const auto m = getMode();
    const juce::StringArray sides[] = { { "L+R", "L", "R" }, { "M+S", "M", "S" } };
    channel.setVisible (m != 0);
    channel.setButtonText (m == 2 ? sides[1][b.channel] : sides[0][b.channel]);
    channel.setColour (colour);
    adaptive.setColour (colour);
    adaptive.setToggleState (valueOf ("adaptiveQ") >= 0.5f, juce::dontSendNotification);
    mode.setSelectedIndex (m, juce::dontSendNotification);
    audition.setActiveColour (bandColour (selected));

    scale.setValue (valueOf ("scale"));
    output.setValue (valueOf ("output"));

    for (auto& button : bandButtons)
    {
        button->setToggleState (band (button->index).on, juce::dontSendNotification);
        button->repaint();
    }

    header->repaint();
    graph.refreshCurves();
    resized();
}

void EqEightDevice::readingsChanged()
{
    graph.refreshSpectrum();
}

int EqEightDevice::getPreferredWidth (bool expanded, int dockedWidth) const
{
    return expanded ? juce::jmax (compactWidth, dockedWidth) : compactWidth;
}

void EqEightDevice::focusFirstControl()
{
    bandButtons[(size_t) selected]->grabKeyboardFocus();
}

void EqEightDevice::resized()
{
    auto area = getLocalBounds().reduced (bodyPadding);
    auto panel = area.removeFromRight (panelWidth);
    area.removeFromRight (bodyGap);

    // Display: the graph over the band strip.
    graph.setBounds (area.removeFromTop (graphHeight));
    area.removeFromTop (displayGap);
    auto strip = area.removeFromTop (stripHeight);
    const auto cell = (strip.getWidth() - (numBands - 1) * stripGap) / numBands;

    for (int b = 0; b < numBands; ++b)
    {
        bandButtons[(size_t) b]->setBounds (strip.removeFromLeft (b == numBands - 1 ? strip.getWidth() : cell));
        strip.removeFromLeft (stripGap);
    }

    // The band panel: title (and audition), Freq / Gain / Q, the toggles, then Output.
    auto title = panel.removeFromTop (rowHeight);
    audition.setBounds (title.removeFromRight (auditionWidth));
    title.removeFromRight (5);

    if (channel.isVisible())
    {
        channel.setBounds (title.removeFromRight (channel.getIdealWidth() + 4));
        title.removeFromRight (5);
    }

    header->setBounds (title);

    for (auto* row : { &frequency, &gain, &q })
    {
        panel.removeFromTop (rowGap);
        row->setBounds (panel.removeFromTop (rowHeight));
    }

    panel.removeFromTop (rowGap);
    auto toggles = panel.removeFromTop (toggleHeight);
    adaptive.setBounds (toggles.removeFromLeft (adaptive.getIdealWidth()));
    toggles.removeFromLeft (rowGap);
    mode.setBounds (toggles);

    panel.removeFromTop (rowGap);
    outputDividerY = panel.getY();
    panel.removeFromTop (outputPaddingTop + 1);
    auto outputs = panel.removeFromTop (toggleHeight);
    scale.setBounds (outputs.removeFromLeft ((outputs.getWidth() - rowGap) / 2));
    outputs.removeFromLeft (rowGap);
    output.setBounds (outputs);
}

void EqEightDevice::paint (juce::Graphics& g)
{
    // The Output zone's top border (design: Output, border-soft, 1 px).
    if (outputDividerY >= 0)
    {
        g.setColour (themeManager.getTheme().borderSoft);
        g.fillRect (scale.getX(), outputDividerY, output.getRight() - scale.getX(), 1);
    }
}

} // namespace resamper
