#include "ArrangementView.h"
#include "Commands/AppCommands.h"
#include "UI/Browser/Library.h"
#include "UI/State/ShellState.h"
#include "UI/State/UIStateStore.h"
#include "UI/Theme/Interaction.h"

namespace resamper
{

namespace
{
    constexpr float wheelPixelsPerUnit = 300.0f;   // JUCE wheel deltas are fractions of a "notch"
    constexpr double zoomPerWheelUnit = 4.0;
}

ArrangementView::ArrangementView (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, UIStateStore& uiState,
                                  ShellState& s)
    : model (m), themeManager (tm), view (uiState.getState (componentId)),
      timeline (model, c, themeManager, view),
      trackList (c, themeManager, view),
      lanes (model, c, themeManager, view),
      commands (c), shell (s)
{
    setComponentID (componentId);

    // A click on a lane or the timeline would otherwise pass focus up to here,
    // which hands it to the first header's Arm.
    setMouseClickGrabsKeyboardFocus (false);

    for (auto* child : std::initializer_list<juce::Component*> { &timeline, &trackList, &lanes, &playhead })
        addAndMakeVisible (child);

    // Clicking a track header or an empty lane selects the track (engine
    // selection; never undoable, never through the UndoManager).
    trackList.onRowClicked = lanes.onRowClicked = [this] (int row, juce::ModifierKeys mods) { selectRow (row, mods); };
    lanes.onMidiClipOpened = [this] (const juce::String& id) { if (onMidiClipOpened) onMidiClipOpened (id); };
    lanes.onAudioClipOpened = [this] (const juce::String& id) { if (onAudioClipOpened) onAudioClipOpened (id); };

    model.addListener (this);
    themeManager.addListener (this);
    view.getState().addListener (this);
    refresh();
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    view.getState().removeListener (this);
    themeManager.removeListener (this);
    model.removeListener (this);
}

void ArrangementView::updateZoomLimits()
{
    // 8..400 px per bar at the Edit's tempo (PRD §8.3).
    const auto secondsPerBar = model.beatsToSeconds ((double) model.getBeatsPerBar (0.0));

    if (secondsPerBar > 0.0)
        view.setZoomLimits (8.0 / secondsPerBar, 400.0 / secondsPerBar);
}

void ArrangementView::zoomBy (double factor)
{
    view.zoomAround (factor, (float) lanes.getWidth() * 0.5f);
}

void ArrangementView::zoomToSelection()
{
    double start = 0, end = 0;

    for (auto& track : tracks)
        for (auto& clip : track.clips)
            if (clip.selected)
            {
                start = end > start ? std::min (start, clip.startSeconds) : clip.startSeconds;
                end = std::max (end, clip.startSeconds + clip.lengthSeconds);
            }

    if (end > start)
        view.zoomToFit (start, end, (float) lanes.getWidth());
}

void ArrangementView::zoomToSong()
{
    double end = 0;

    for (auto& track : tracks)
        for (auto& clip : track.clips)
            end = std::max (end, clip.startSeconds + clip.lengthSeconds);

    // An empty song still shows eight bars.
    end = std::max (end, model.beatsToSeconds (8.0 * model.getBeatsPerBar (0.0)));
    view.zoomToFit (0.0, end, (float) lanes.getWidth());
}

void ArrangementView::scrolledByHand()
{
    if (model.isPlaying())
        followPaused = true;
}

void ArrangementView::timerCallback()
{
    const auto playing = model.isPlaying();

    // Each Play resumes Follow.
    if (playing && ! wasPlaying)
        followPaused = false;

    wasPlaying = playing;

    if (playing && ! followPaused && shell.isFollowing() && isShowing())
        view.follow (model.getTransportPositionSeconds(), (float) lanes.getWidth());
}

void ArrangementView::refresh()
{
    updateZoomLimits();
    tracks = model.getTracks();
    trackList.setTracks (tracks, model.getAudioInputs(), model.getMidiInputs());
    lanes.setTracks (tracks);
    timeline.repaint();   // the loop
    clampVerticalScroll();
}

void ArrangementView::paint (juce::Graphics& g)
{
    // The corner above the headers, beside the ruler.
    auto& theme = themeManager.getTheme();
    auto corner = juce::Rectangle<int> (0, 0, trackList.getWidth(), timeline.getHeight());
    g.setColour (theme.bgPanel);
    g.fillRect (corner);
    g.setColour (theme.borderSoft);
    g.fillRect (corner.removeFromBottom (1));
    g.fillRect (corner.getRight() - 1, 0, 1, timeline.getHeight());
    drawStyledText (g, themeManager, TRANS ("Bars"), theme.caption, juce::Rectangle<int> (12, 0, trackList.getWidth() - 12, timeline.getHeight()),
                    juce::Justification::centredLeft, theme.textDim);
}

void ArrangementView::resized()
{
    auto& metrics = themeManager.getMetrics();
    auto r = getLocalBounds();
    auto left = r.removeFromLeft (metrics.trackHeaderWidth);
    left.removeFromTop (metrics.timelineHeight);

    trackList.setBounds (left);
    playhead.setBounds (r);
    timeline.setBounds (r.removeFromTop (metrics.timelineHeight));
    lanes.setBounds (r);

    clampVerticalScroll();
    lanes.layoutClips();
}

void ArrangementView::clampVerticalScroll()
{
    const auto contentHeight = (int) tracks.size() * laneHeight();
    const auto maxScroll = std::max (0, contentHeight - lanes.getHeight());

    if (view.getScrollY() > maxScroll)
        view.setScrollY (maxScroll);
}

void ArrangementView::selectRow (int row, juce::ModifierKeys mods)
{
    using Mode = ApplicationModel::SelectionMode;
    const auto mode = mods.isShiftDown() ? Mode::add : mods.isCommandDown() ? Mode::toggle : Mode::replace;
    commands.invoke (cmd::trackSelect, { juce::isPositiveAndBelow (row, (int) tracks.size()) ? tracks[(size_t) row].id : juce::String(), mode });
}

void ArrangementView::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    clampVerticalScroll();
    // Zoom or scroll changed: everything re-derives its position from the view state.
    lanes.layoutClips();
    lanes.repaint();
    trackList.layoutHeaders();
    timeline.repaint();
    playhead.update();
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto timelineX = (float) e.getEventRelativeTo (&timeline).x;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        view.zoomAround (std::pow (2.0, wheel.deltaY * zoomPerWheelUnit), timelineX);
        return;
    }

    if (e.mods.isAltDown())
    {
        const auto delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        view.setLaneHeight (view.getLaneHeight (themeManager.getMetrics().trackHeight) + juce::roundToInt (delta * 100.0f));
        return;
    }

    const auto dx = e.mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
    const auto dy = e.mods.isShiftDown() ? 0.0f : wheel.deltaY;

    if (dx != 0.0f)
        scrolledByHand();

    if (dx != 0.0f)
        view.setScrollSeconds (view.getScrollSeconds() - dx * wheelPixelsPerUnit / view.getPixelsPerSecond());

    if (dy != 0.0f)
    {
        view.setScrollY (view.getScrollY() - juce::roundToInt (dy * wheelPixelsPerUnit));
        clampVerticalScroll();
    }
}

ArrangementView::DropTarget ArrangementView::dropTargetAt (const SourceDetails& details) const
{
    const auto item = itemFromDrag (details.description);
    const auto lanePoint = lanes.getLocalPoint (this, details.localPosition);
    const auto row = view.yToRow (lanePoint.y, laneHeight());

    if (! item || ! juce::isPositiveAndBelow (row, (int) tracks.size())
        || ! (lanes.getBounds().contains (details.localPosition) || trackList.getBounds().contains (details.localPosition)))
        return {};

    return { row, canDropOnTrack (*item, tracks[(size_t) row].kind) };
}

bool ArrangementView::isInterestedInDragSource (const SourceDetails& details)
{
    return itemFromDrag (details.description).has_value();
}

void ArrangementView::itemDragMove (const SourceDetails& details)
{
    if (lanes.getBounds().contains (details.localPosition))
        lanes.autoScrollAt (lanes.getLocalPoint (this, details.localPosition));

    const auto target = dropTargetAt (details);

    if (target.row != dropTarget.row || target.valid != dropTarget.valid)
    {
        dropTarget = target;
        setMouseCursor (target.row >= 0 && ! target.valid ? notAllowedCursor() : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ArrangementView::itemDragExit (const SourceDetails&)
{
    dropTarget = {};
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void ArrangementView::itemDropped (const SourceDetails& details)
{
    const auto target = dropTargetAt (details);
    itemDragExit (details);

    // An invalid lane (a sample on a MIDI track) refuses with a shake.
    if (target.row >= 0 && ! target.valid)
    {
        rejectWithShake (lanes, TRANS ("Samples go on audio tracks"));
        return;
    }

    if (auto item = itemFromDrag (details.description); item && target.valid)
    {
        // Onto a lane: where it was dropped. Onto a header: at the insert marker.
        const auto x = (float) lanes.getLocalPoint (this, details.localPosition).x;
        const auto seconds = lanes.getBounds().contains (details.localPosition) ? std::max (0.0, view.xToTime (x))
                                                                                : model.getInsertMarkerSeconds();
        const auto& trackId = tracks[(size_t) target.row].id;

        if (item->kind == LibraryItem::Kind::plugin && onDeviceDropped != nullptr)
            onDeviceDropped (trackId, item->pluginPath);
        else
            dropOnTrack (commands, *item, trackId, seconds);
    }
}

void ArrangementView::paintOverChildren (juce::Graphics& g)
{
    if (dropTarget.row < 0 || ! dropTarget.valid)
        return;

    // Valid targets get an accent-dim outline (§16.3).
    const auto rowHeight = laneHeight();
    const auto y = lanes.getY() + view.rowToY (dropTarget.row, rowHeight);
    g.setColour (themeManager.getTheme().accentDim);
    g.drawRoundedRectangle (juce::Rectangle<int> (trackList.getX(), y, lanes.getRight() - trackList.getX(), rowHeight)
                                .toFloat().reduced (1.0f),
                            themeManager.getTheme().radiusMd, 2.0f);
}

void ArrangementView::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    view.zoomAround (scaleFactor, (float) e.getEventRelativeTo (&timeline).x);
}

} // namespace resamper
