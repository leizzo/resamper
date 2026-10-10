#include "PianoRollView.h"
#include "Commands/AppCommands.h"
#include "UI/State/UIStateStore.h"

#include <cmath>
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    constexpr float wheelPixelsPerUnit = 300.0f;
    constexpr double zoomPerWheelUnit = 4.0;
    const juce::Identifier clipIdProperty ("clipId");
}

PianoRollView::PianoRollView (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, UIStateStore& uiState)
    : model (m), commands (c), themeManager (tm), view (uiState.getState (componentId)),
      keyboard (c, tm, view), ruler (m, c, tm, view), grid (m, c, tm, view), velocity (m, c, tm, view),
      playhead (m, tm, view)
{
    setComponentID (componentId);

    arrangementButton.onClick = [this] { close(); };
    quarterButton.onClick = [this] { quantize ("1/4"); };
    eighthButton.onClick = [this] { quantize ("1/8"); };
    sixteenthButton.onClick = [this] { quantize ("1/16"); };
    deleteButton.onClick = [this] { commands.invoke (cmd::noteDelete); };

    arrangementButton.setTooltip (TRANS ("Back to the Arrangement"));
    quarterButton.setTooltip (tr ("Quantize to %1", "1/4"));
    eighthButton.setTooltip (tr ("Quantize to %1", "1/8"));
    sixteenthButton.setTooltip (tr ("Quantize to %1", "1/16"));
    deleteButton.setTooltip (TRANS ("Delete selected notes"));

    for (auto* child : std::initializer_list<juce::Component*> { &arrangementButton, &quarterButton, &eighthButton,
                                                                 &sixteenthButton, &deleteButton, &keyboard, &ruler,
                                                                 &grid, &velocity, &playhead })
        addAndMakeVisible (*child);

    model.addListener (this);
    themeManager.addListener (this);
    view.getState().addListener (this);
    refresh();
}

PianoRollView::~PianoRollView()
{
    view.getState().removeListener (this);
    themeManager.removeListener (this);
    model.removeListener (this);
}

bool PianoRollView::isOpen() const
{
    return currentClip() != nullptr;
}

void PianoRollView::openClip (const juce::String& clipId)
{
    const auto clip = model.getClip (clipId);

    if (! clip || clip->kind != TrackKind::midi)
        return;

    view.setScrollSeconds (std::max (0.0, clip->startSeconds));
    const auto rowOfMiddleC = 127 - 60;
    const auto& metrics = themeManager.getMetrics();
    view.setScrollY (rowOfMiddleC * metrics.pianoKeyHeight - metrics.pianoKeyHeight * metrics.pianoScrollMargin);
    view.getState().setProperty (clipIdProperty, clipId, nullptr);
}

juce::String PianoRollView::openClipId() const
{
    return view.getState()[clipIdProperty].toString();
}

const ClipInfo* PianoRollView::currentClip() const
{
    const auto id = openClipId();

    if (id.isEmpty())
        return nullptr;

    for (auto& track : tracks)
        for (auto& clip : track.clips)
            if (clip.id == id && clip.kind == TrackKind::midi)
                return &clip;

    return nullptr;
}

void PianoRollView::refresh()
{
    tracks = model.getTracks();

    if (auto* clip = currentClip())
    {
        grid.setClip (*clip);
        velocity.setClip (*clip);
    }
    else
    {
        grid.setClip ({});
        velocity.setClip ({});
    }

    deleteButton.setEnabled (model.hasSelectedNotes());
    ruler.repaint();
    repaint();
}

void PianoRollView::close()
{
    view.getState().setProperty (clipIdProperty, juce::String(), nullptr);
}

void PianoRollView::quantize (const char* gridName)
{
    if (auto* clip = currentClip())
        commands.invoke (cmd::noteQuantize, { clip->id, gridName });
}

void PianoRollView::layoutToolbar (juce::Rectangle<int> toolbar)
{
    auto& metrics = themeManager.getMetrics();
    auto row = toolbar.reduced (metrics.textPadding, 0);
    const auto buttonHeight = std::min (metrics.trackControlHeight, row.getHeight());
    const auto y = row.getY() + (row.getHeight() - buttonHeight) / 2;
    const auto font = themeManager.getFont();

    auto place = [&] (juce::TextButton& button)
    {
        const auto width = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, button.getButtonText()))
                         + metrics.textPadding * 2;
        button.setBounds (row.getX(), y, std::max (width, metrics.trackButtonWidth), buttonHeight);
        row.removeFromLeft (button.getWidth() + metrics.inset);
    };

    place (arrangementButton);
    place (quarterButton);
    place (eighthButton);
    place (sixteenthButton);
    place (deleteButton);
    nameArea = row;
}

void PianoRollView::clampVerticalScroll()
{
    const auto contentHeight = 128 * themeManager.getMetrics().pianoKeyHeight;
    const auto maxScroll = std::max (0, contentHeight - grid.getHeight());

    if (view.getScrollY() > maxScroll)
        view.setScrollY (maxScroll);
}

void PianoRollView::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.background);
    g.setColour (theme.panel);
    g.fillRect (toolbarBounds);
    g.fillRect (velocityLabel);

    g.setColour (theme.mutedText);
    g.setFont (themeManager.getFont (0.75f));
    g.drawText (TRANS ("Velocity"), velocityLabel.reduced (themeManager.getMetrics().textPadding, 0),
                juce::Justification::centredLeft, true);

    if (auto* clip = currentClip())
    {
        g.setColour (theme.text);
        g.setFont (themeManager.getFont());
        g.drawText (clip->name, nameArea, juce::Justification::centredLeft, true);
    }
}

void PianoRollView::resized()
{
    auto& metrics = themeManager.getMetrics();
    auto area = getLocalBounds();
    toolbarBounds = area.removeFromTop (metrics.timelineHeight);
    layoutToolbar (toolbarBounds);

    auto velocityRow = area.removeFromBottom (metrics.velocityLaneHeight);
    auto keyboardArea = area.removeFromLeft (metrics.pianoKeyWidth);
    auto rulerArea = area.removeFromTop (metrics.timelineHeight);

    keyboard.setBounds (keyboardArea);
    ruler.setBounds (rulerArea);
    grid.setBounds (area);
    velocityLabel = velocityRow.removeFromLeft (metrics.pianoKeyWidth);
    velocity.setBounds (velocityRow);

    playhead.setBounds (rulerArea.getX(), rulerArea.getY(), rulerArea.getWidth(),
                        rulerArea.getHeight() + area.getHeight() + velocity.getHeight());

    clampVerticalScroll();
    grid.layoutNotes();
}

void PianoRollView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto anchorX = (float) e.getEventRelativeTo (&grid).x;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        view.zoomAround (std::pow (2.0, wheel.deltaY * zoomPerWheelUnit), anchorX);
        return;
    }

    const auto dx = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
    const auto dy = e.mods.isShiftDown() ? 0.0f : wheel.deltaY;

    if (dx != 0.0f)
        view.setScrollSeconds (view.getScrollSeconds() - dx * wheelPixelsPerUnit / view.getPixelsPerSecond());

    if (dy != 0.0f)
    {
        view.setScrollY (view.getScrollY() - juce::roundToInt (dy * wheelPixelsPerUnit));
        clampVerticalScroll();
    }
}

void PianoRollView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    view.zoomAround (scaleFactor, (float) e.getEventRelativeTo (&grid).x);
}

void PianoRollView::modelChanged()
{
    const auto id = openClipId();
    refresh();

    if (id.isNotEmpty() && currentClip() == nullptr)
        close();
}

void PianoRollView::themeChanged()
{
    repaint();
    keyboard.repaint();
    ruler.repaint();
    grid.repaint();
    velocity.repaint();
}

void PianoRollView::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == clipIdProperty)
    {
        refresh();

        if (onOpenStateChanged)
            onOpenStateChanged();

        return;
    }

    grid.layoutNotes();
    keyboard.repaint();
    ruler.repaint();
    velocity.repaint();
    playhead.update();
}

} // namespace resamper
