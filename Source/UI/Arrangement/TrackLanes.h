#pragma once

#include "ClipComponent.h"

#include <map>
#include <optional>

namespace resamper
{

class ArrangementViewState;
class CommandRegistry;

/** One lane per track, holding that track's ClipComponents at their time
    positions. Clip components are kept by clip ID across model updates so their
    waveforms aren't regenerated.

    Clip gestures (PRD §8.2): a click selects a clip (Shift adds, Mod toggles);
    dragging its body moves it (across lanes too); dragging an edge trims it;
    dragging its top-right corner loop-extends it. Moves and trims snap to the
    grid while Snap is on; Mod bypasses it. Alt drops a copy: the original
    stays put and a ghost follows the pointer. The drag is previewed here and
    committed on release as one Command. Right-click opens the clip menu with
    each item's shortcut; double-click opens the Piano Roll or the Editor.

    On empty lane space a click selects the track and clears the Time
    Selection; a drag makes a Time Selection across every track, on the grid
    (Mod bypasses it, Shift extends the one there is). Inside the Time
    Selection a drag moves its content to where it is dropped, Alt copies it
    under a ghost; a click there selects the clip under it or clears the
    selection. Right-click on empty lane space opens the Time Selection's menu while
    there is one (under the ruler), else the lane menu (createLaneMenu). The Time Selection draws as a tint
    down every lane, with the clip parts inside it outlined as selected.

    The grid draws a line on every step of the grid the zoom shows (bars in
    grid-bar, the beats between them in grid-beat).

    While recording, each recording is drawn in its lane with its waveform
    growing, polled from the model at ~30 Hz like the Playhead.
*/
class TrackLanes : public juce::Component,
                   private juce::Timer
{
public:
    TrackLanes (const ApplicationModel&, CommandRegistry&, ThemeManager&, ArrangementViewState&);

    void setTracks (const std::vector<TrackInfo>&);

    /** Scrolls when p (lane coordinates) is near an edge, during a drag. */
    void autoScrollAt (juce::Point<int> p);

    /** Drops a clip or Time Selection drag in progress (Esc). */
    void cancelDrag();

    /** Re-positions every clip from the view state (after zoom/scroll). */
    void layoutClips();

    /** The lane height the view state holds (Alt + wheel). */
    int laneHeight() const;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override   { layoutClips(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** The menu a right-click on empty lane space opens at row (may be out of
        range) and seconds: the design's Add Track items, which insert after the
        clicked lane's track, then Insert MIDI Clip, Select All and Loop Selection.
        Opening it leaves the selection alone. */
    juce::PopupMenu createLaneMenu (int row, double seconds);

    /** Called with the row index under a click on an empty lane (may be out of range). */
    std::function<void (int row, juce::ModifierKeys)> onRowClicked;

    /** Double-click on a MIDI clip (Piano Roll) or an audio clip (Editor). */
    std::function<void (const juce::String& clipId)> onMidiClipOpened, onAudioClipOpened;

private:
    enum class DragMode { move, resizeStart, resizeEnd, loopExtend };

    struct Drag
    {
        DragMode mode;
        ClipInfo original, preview;
        int row = 0;               ///< the preview's row
        double grabSeconds = 0;    ///< timeline position under the pointer at mouse-down
        bool copy = false;         ///< Alt-drag: a copy lands, the original stays
    };

    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState& view;

    std::vector<TrackInfo> tracks;
    std::map<juce::String, std::unique_ptr<ClipComponent>> clips;
    std::optional<Drag> drag;

    /** A drag that makes the Time Selection, or moves (copies) its content. */
    struct TimeDrag
    {
        bool moving = false;         ///< inside the Time Selection: its content moves
        double anchorSeconds = 0;    ///< making: the fixed end; moving: the time under the pointer at mouse-down
        TimeRangeSeconds original;   ///< moving: the selection at mouse-down
        double targetStart = 0;      ///< moving: where it would land
        bool copy = false;           ///< moving with Alt
        bool active = false;         ///< past the drag threshold
    };

    std::optional<TimeDrag> timeDrag;

    std::vector<RecordingInfo> recordings;
    std::map<juce::String, std::unique_ptr<ClipWaveform>> recordingWaveforms;   ///< by track ID

    juce::Colour trackColour (const TrackInfo&) const;
    ClipComponent* clipAt (juce::Point<int>) const;
    DragMode dragModeAt (const ClipComponent&, juce::Point<int>) const;
    int rowOf (const juce::String& clipId) const;
    int rowOfTrack (const juce::String& trackId) const;
    void showClipMenu (const ClipInfo&);
    void showTimeSelectionMenu (double seconds, int x);
    void timeDragged (const juce::MouseEvent&);
    void timeReleased (const TimeDrag&, const juce::MouseEvent&);
    void paintTimeSelection (juce::Graphics&, TimeRangeSeconds) const;
    void paintTimeGhost (juce::Graphics&, const TimeDrag&) const;
    juce::Rectangle<float> columnArea (TimeRangeSeconds) const;
    juce::Rectangle<int> clipBounds (const ClipInfo&, int row) const;
    void startRename (const ClipInfo&);
    double snap (double seconds, bool bypass) const;
    std::unique_ptr<juce::TextEditor> renameEditor;

    void timerCallback() override;
};

} // namespace resamper
