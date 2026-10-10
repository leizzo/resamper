#include "ContinuousControl.h"
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    /** The dial's travel, clockwise from 12 o'clock: from 7 o'clock round to 5 (the design's 220° start, 260° sweep). */
    constexpr float dialStart = -juce::MathConstants<float>::pi * 130.0f / 180.0f;
    constexpr float dialEnd = juce::MathConstants<float>::pi * 130.0f / 180.0f;

    /** A trackpad scroll this far (in JUCE wheel units) counts as one notch. */
    constexpr float smoothWheelPerNotch = 0.05f;

    /** Space between a ValueTag and the shape it sits beside. */
    constexpr int valueTagGap = 6;
}

//==============================================================================
ValueTag::ValueTag (ThemeManager& tm) : themeManager (tm)
{
    setInterceptsMouseClicks (false, false);
    setAlwaysOnTop (true);
}

void ValueTag::show (juce::Component& owner, juce::Rectangle<float> anchor,
                     const juce::String& newValue, const juce::String& newPosition)
{
    auto* top = owner.getTopLevelComponent();

    if (top == nullptr)
        return;

    value = newValue;
    position = newPosition;

    auto& theme = themeManager.getTheme();
    const auto font = themeManager.numberFont (theme.caption);
    const auto textWidth = [&] (const juce::String& t) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, t)); };
    const auto width = 12 + textWidth (value) + (position.isEmpty() ? 0 : textWidth (position) + 6);
    const auto height = (int) std::ceil (font.getHeight()) + 4;

    if (getParentComponent() != top)
        top->addChildComponent (this);

    const auto area = top->getLocalArea (&owner, anchor);
    setBounds (juce::Rectangle<int> (juce::roundToInt (area.getRight()) + valueTagGap, juce::roundToInt (area.getCentreY()) - height / 2,
                                     width, height)
                   .constrainedWithin (top->getLocalBounds()));
    setVisible (true);
    toFront (false);
    repaint();
}

void ValueTag::hide()
{
    setVisible (false);

    if (auto* parent = getParentComponent())
        parent->removeChildComponent (this);
}

void ValueTag::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.setColour (theme.accent);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), theme.radiusMd);

    auto r = getLocalBounds().reduced (6, 2);
    const auto font = themeManager.numberFont (theme.caption);
    g.setColour (theme.textOnAccent);

    if (position.isNotEmpty())
    {
        g.setFont (font);
        const auto w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, position));
        g.drawText (position, r.removeFromLeft (w), juce::Justification::centredLeft, false);
        r.removeFromLeft (6);
    }

    g.setFont (font.boldened());
    g.drawText (value, r, juce::Justification::centredLeft, false);
}

//==============================================================================
ContinuousControl::ContinuousControl (ThemeManager& tm, ContinuousValue::Spec spec, Axis a)
    : themeManager (tm), model (std::move (spec)), axis (a), tag (tm)
{
    setWantsKeyboardFocus (true);
    setRepaintsOnMouseActivity (true);

    model.onChange = [this] (double v, bool continues)
    {
        repaint();

        if (onChange)
            onChange (v, continues);
    };
}

ContinuousControl::~ContinuousControl() = default;

void ContinuousControl::setValue (double v)
{
    if (model.isDragging() || editor != nullptr || juce::exactlyEqual (v, model.getValue()))
        return;

    model.setValue (v);
    repaint();
}

void ContinuousControl::enablementChanged()
{
    applyEnablement (*this, themeManager.getTheme());
}

void ContinuousControl::showTag()
{
    tag.show (*this, getFocusBounds(), model.getText());
}

void ContinuousControl::mouseDown (const juce::MouseEvent& e)
{
    if (editor != nullptr || ! isEnabled() || e.mods.isPopupMenu())
        return;

    grabKeyboardFocus();
    resetOnPress = e.mods.isAltDown();

    if (resetOnPress)
    {
        model.reset();
        return;
    }

    pressedOnReadout = ! doubleClickEdits && getReadoutBounds().contains (e.getPosition());
    lastDragPosition = e.position;

    if (const auto proportion = getProportionAt (e.position))
        model.beginDragAt (*proportion);
    else
        model.beginDrag();
}

void ContinuousControl::mouseDrag (const juce::MouseEvent& e)
{
    if (resetOnPress || editor != nullptr || ! model.isDragging())
        return;

    const auto delta = axis == Axis::vertical ? lastDragPosition.y - e.position.y
                                              : e.position.x - lastDragPosition.x;
    lastDragPosition = e.position;

    if (delta != 0.0f)
        model.dragBy (delta, e.mods.isShiftDown());

    showTag();
}

void ContinuousControl::mouseUp (const juce::MouseEvent& e)
{
    model.endDrag();
    tag.hide();

    if (pressedOnReadout && ! e.mouseWasDraggedSinceMouseDown() && ! resetOnPress)
        showTextEntry();

    pressedOnReadout = resetOnPress = false;
}

void ContinuousControl::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! isEnabled() || e.mods.isAltDown())
        return;

    if (doubleClickEdits)
        showTextEntry();
    else
        model.reset();
}

void ContinuousControl::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! isEnabled() || editor != nullptr)
        return;

    const auto delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;

    if (! wheel.isSmooth)
    {
        if (delta != 0.0f)
            model.step (delta > 0 ? 1 : -1, e.mods.isShiftDown());

        return;
    }

    wheelAccumulator += delta;
    const auto notches = (int) (wheelAccumulator / smoothWheelPerNotch);

    if (notches != 0)
    {
        wheelAccumulator -= (float) notches * smoothWheelPerNotch;
        model.step (notches, e.mods.isShiftDown());
    }
}

bool ContinuousControl::keyPressed (const juce::KeyPress& key)
{
    const auto fine = key.getModifiers().isShiftDown();
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
    {
        model.step (1, fine);
        return true;
    }

    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
    {
        model.step (-1, fine);
        return true;
    }

    if (code == juce::KeyPress::returnKey)
    {
        showTextEntry();
        return true;
    }

    return false;
}

void ContinuousControl::showTextEntry()
{
    if (editor != nullptr || ! isEnabled())
        return;

    auto& theme = themeManager.getTheme();
    auto area = getReadoutBounds();

    if (area.isEmpty())
        area = getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (getHeight(), themeManager.getMetrics().controlMd));

    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (themeManager.numberFont (theme.body));
    editor->setJustification (juce::Justification::centred);
    editor->setIndents (2, 0);
    editor->setBorder ({});
    editor->setSelectAllWhenFocused (true);
    editor->setText (model.getText(), false);
    editor->setBounds (area);
    editor->onReturnKey = [this] { closeTextEntry (true); };
    editor->onEscapeKey = [this] { closeTextEntry (false); };
    editor->onFocusLost = [this] { closeTextEntry (false); };
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

void ContinuousControl::closeTextEntry (bool commit)
{
    if (editor == nullptr || ! editor->isVisible())
        return;

    const auto text = editor->getText();
    editor->setVisible (false);

    // The editor is still inside its own callback: delete it afterwards.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ContinuousControl> (this)]
    {
        if (safe != nullptr)
            safe->editor.reset();
    });

    if (commit)
        model.commitText (text);

    grabKeyboardFocus();
    repaint();
}

juce::String ContinuousControl::getTooltip()
{
    auto name = juce::SettableTooltipClient::getTooltip();

    if (name.isEmpty())
        name = getTitle();

    return name.isEmpty() ? model.getText() : tr ("%1: %2", name, model.getText());
}

void ContinuousControl::paintOverChildren (juce::Graphics& g)
{
    if (hasKeyboardFocus (false))
        paintFocusRing (g, themeManager.getTheme(), getFocusBounds(), getFocusRadius());
}

std::unique_ptr<juce::AccessibilityHandler> ContinuousControl::createAccessibilityHandler()
{
    struct Value : juce::AccessibilityValueInterface
    {
        explicit Value (ContinuousControl& c) : owner (c) {}

        bool isReadOnly() const override            { return ! owner.isEnabled(); }
        double getCurrentValue() const override      { return owner.model.getValue(); }
        void setValue (double v) override            { owner.model.setByUser (v); }
        juce::String getCurrentValueAsString() const override { return owner.model.getText(); }
        void setValueAsString (const juce::String& text) override { owner.model.commitText (text); }

        AccessibleValueRange getRange() const override
        {
            auto& spec = owner.model.getSpec();
            return AccessibleValueRange ({ spec.minimum, spec.maximum },
                                         spec.wheelStep > 0 ? spec.wheelStep : (spec.maximum - spec.minimum) / 100.0);
        }

        ContinuousControl& owner;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider,
                                                         juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
Knob::Knob (ThemeManager& tm, ContinuousValue::Spec spec, juce::String labelText, bool isBipolar)
    : ContinuousControl (tm, std::move (spec), Axis::vertical), label (std::move (labelText)), bipolar (isBipolar)
{
    setTitle (label);
}

void Knob::setDimmed (bool b)
{
    dimmed = b;
    repaint();
}

void Knob::setArcColour (std::optional<juce::Colour> c)
{
    arcColour = c;
    repaint();
}

void Knob::setDialSize (int size)
{
    dialSize = size;
    repaint();
}

void Knob::setReadoutBeside (bool b)
{
    readoutBeside = b;
    repaint();
}

void Knob::setAutomated (bool b)
{
    if (std::exchange (automated, b) != b)
        repaint();
}

juce::Rectangle<float> Knob::dialBounds() const
{
    const auto size = (float) juce::jmin (dialSize, getWidth(), getHeight());

    if (readoutBeside)
        return { 0.0f, ((float) getHeight() - size) / 2.0f, size, size };

    return { ((float) getWidth() - size) / 2.0f, 0.0f, size, size };
}

juce::Rectangle<int> Knob::getReadoutBounds() const
{
    if (readoutBeside)
        return getLocalBounds().withLeft ((int) dialBounds().getRight() + 8);

    return getLocalBounds().withTop ((int) dialBounds().getBottom());
}

juce::Rectangle<float> Knob::getFocusBounds() const
{
    // On the dial's rim, not around it: the dial can touch the component's edge.
    return dialBounds();
}

float Knob::getFocusRadius() const
{
    return dialBounds().getWidth() / 2.0f;
}

void Knob::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto dial = dialBounds();
    const auto proportion = (float) getModel().getProportion();
    const auto angle = dialStart + proportion * (dialEnd - dialStart);
    const auto from = bipolar ? 0.0f : dialStart;

    juce::Path track, arc;
    track.addPieSegment (dial, dialStart, dialEnd, 0.72f);
    g.setColour (theme.bgSlot);
    g.fillPath (track);

    if (std::abs (angle - from) > 0.001f)
    {
        arc.addPieSegment (dial, juce::jmin (from, angle), juce::jmax (from, angle), 0.72f);
        const auto hover = arcColour ? arcColour->brighter (0.2f) : theme.accentHover;
        g.setColour (dimmed ? theme.textDim : (isMouseOverOrDragging() ? hover : arcColour.value_or (theme.accent)));
        g.fillPath (arc);
    }

    const auto cap = dial.withSizeKeepingCentre (dial.getWidth() * 0.5f, dial.getHeight() * 0.5f);
    g.setColour (theme.bgElevated);
    g.fillEllipse (cap);
    g.setColour (theme.border);
    g.drawEllipse (cap, 1.0f);

    if (automated)
    {
        // `Knob/Automated`: a 6 px rec dot ringed in the panel colour, in the dial's top-right corner (24, 0 of 30).
        const auto size = dial.getWidth() * 0.2f;
        const auto dot = juce::Rectangle<float> (dial.getRight() - size, dial.getY(), size, size);
        g.setColour (theme.rec);
        g.fillEllipse (dot);
        g.setColour (theme.bgPanel);
        g.drawEllipse (dot, 1.0f);
    }

    auto text = getReadoutBounds();

    if (isEditingText() || text.isEmpty())
        return;

    const auto lineHeight = juce::roundToInt (theme.caption.size) + 3;

    if (readoutBeside)
    {
        // Label over a brighter value, centred on the dial as a pair.
        const auto pairHeight = 2 * lineHeight;
        text = text.withSizeKeepingCentre (text.getWidth(), pairHeight);
        drawStyledText (g, themeManager, label, TypeStyle { theme.caption.size, false, 400 },
                        text.removeFromTop (lineHeight), juce::Justification::centredLeft, theme.textDim);
        drawNumber (g, themeManager, getModel().getText(), TypeStyle { theme.caption.size + 1.0f, true, 400 },
                    text.removeFromTop (lineHeight), juce::Justification::centredLeft, theme.textPrimary);
        return;
    }

    text.removeFromTop (4);

    if (label.isNotEmpty())
        drawStyledText (g, themeManager, label, TypeStyle { theme.caption.size, false, 400 },
                        text.removeFromTop (lineHeight), juce::Justification::centred, theme.textDim);

    drawNumber (g, themeManager, getModel().getText(), theme.caption, text.removeFromTop (lineHeight),
                juce::Justification::centred, theme.textSecondary);
}

//==============================================================================
Slider::Slider (ThemeManager& tm, ContinuousValue::Spec spec, bool isBipolar)
    : ContinuousControl (tm, std::move (spec), Axis::horizontal), bipolar (isBipolar)
{
}

void Slider::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    constexpr float thumbSize = 12.0f, trackHeight = 4.0f;
    const auto area = getLocalBounds().toFloat().reduced (thumbSize / 2.0f, 0.0f);
    const auto track = area.withSizeKeepingCentre (area.getWidth(), trackHeight);
    const auto x = track.getX() + (float) getModel().getProportion() * track.getWidth();
    const auto fromX = bipolar ? track.getCentreX() : track.getX();

    g.setColour (theme.bgSlot);
    g.fillRoundedRectangle (track, trackHeight / 2);
    g.setColour (theme.accentDim);
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (fromX, x), track.getY(),
                                                                         juce::jmax (fromX, x), track.getBottom()),
                            trackHeight / 2);

    if (bipolar)
    {
        g.setColour (theme.textDim);
        g.fillRect (juce::Rectangle<float> (track.getCentreX() - 0.5f, track.getY() - 2.0f, 1.0f, trackHeight + 4.0f));
    }

    const auto thumb = juce::Rectangle<float> (thumbSize, thumbSize).withCentre ({ x, track.getCentreY() });
    g.setColour (isMouseOverOrDragging() ? theme.accentHover : theme.textPrimary);
    g.fillEllipse (thumb);
    g.setColour (theme.textOnAccent);
    g.drawEllipse (thumb, 1.0f);
}

//==============================================================================
ValueField::ValueField (ThemeManager& tm, ContinuousValue::Spec spec, juce::String captionText)
    : ContinuousControl (tm, std::move (spec), Axis::vertical), caption (std::move (captionText))
{
    setDoubleClickEdits (true);
    setTitle (caption);
}

float ValueField::getFocusRadius() const
{
    return raised ? themeManager.getTheme().radiusLg : themeManager.getTheme().radiusMd;
}

void ValueField::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto stacked = caption.isNotEmpty();
    const auto radius = stacked ? 5.0f : getFocusRadius();
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (raised ? (isMouseOverOrDragging() ? theme.bgHover : theme.bgElevated) : theme.bgSlot);
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (raised || isMouseOverOrDragging() ? theme.border : theme.borderSoft);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

    if (isEditingText())
        return;

    if (stacked)
    {
        auto r = getLocalBounds().reduced (9, 6);
        drawStyledText (g, themeManager, caption, TypeStyle { theme.caption.size, false, 400 },
                        r.removeFromTop (juce::roundToInt (theme.caption.size) + 2), juce::Justification::centredLeft,
                        theme.textDim);
        r.removeFromTop (3);
        drawNumber (g, themeManager, getModel().getText(), valueStyle.value_or (TypeStyle { 13.0f, true, 500 }), r,
                    juce::Justification::centredLeft, theme.textPrimary);
        return;
    }

    const auto style = valueStyle.value_or (theme.body);
    const auto valueFont = themeManager.numberFont (style);
    const auto suffixStyle = TypeStyle { theme.caption.size, false, 400 };
    const auto valueWidth = juce::GlyphArrangement::getStringWidthInt (valueFont, getModel().getText());
    const auto suffixWidth = suffix.isEmpty() ? 0 : 6 + juce::GlyphArrangement::getStringWidthInt (themeManager.font (suffixStyle), suffix);
    auto r = getLocalBounds().withSizeKeepingCentre (valueWidth + suffixWidth, getHeight());

    g.setColour (theme.textPrimary);
    g.setFont (valueFont);
    g.drawText (getModel().getText(), r.removeFromLeft (valueWidth), juce::Justification::centredLeft, false);

    if (suffix.isNotEmpty())
        drawStyledText (g, themeManager, suffix, suffixStyle, r.withTrimmedLeft (6), juce::Justification::centredLeft,
                        theme.textDim);
}

} // namespace resamper
