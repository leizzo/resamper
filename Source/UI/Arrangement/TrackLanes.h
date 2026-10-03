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
    dragging its body moves it (across lanes too, snapping to beats; Mod
    bypasses the snap, Alt drops a copy); dragging an edge trims it; dragging
    its top-right corner loop-extends it. The drag is previewed here and
    committed on release as one Command. Right-click opens the clip menu with
    each item's shortcut; double-click opens the Piano Roll or the Editor.

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

    /** Drops a clip drag in progress, leaving the clip where it was (Esc). */
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

    std::vector<RecordingInfo> recordings;
    std::map<juce::String, std::unique_ptr<ClipWaveform>> recordingWaveforms;   ///< by track ID

    juce::Colour trackColour (const TrackInfo&) const;
    ClipComponent* clipAt (juce::Point<int>) const;
    DragMode dragModeAt (const ClipComponent&, juce::Point<int>) const;
    int rowOf (const juce::String& clipId) const;
    int rowOfTrack (const juce::String& trackId) const;
    void showClipMenu (const ClipInfo&);
    void startRename (const ClipInfo&);
    double snap (double seconds, bool bypass) const;
    std::unique_ptr<juce::TextEditor> renameEditor;

    void timerCallback() override;
};

} // namespace resamper
