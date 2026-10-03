#pragma once

#include "Playhead.h"
#include "TimelineHeader.h"
#include "TrackLanes.h"
#include "TrackList.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

class CommandRegistry;
class ShellState;
class UIStateStore;

/** The hand-coded Arrangement: ruler, track headers, lanes with clips, and
    the playhead, all positioned through one ArrangementViewState.

    Mouse wheel scrolls (vertical, and horizontal with shift or a trackpad);
    Mod + wheel zooms around the pointer (8–400 px per bar); Alt + wheel sets
    the lane height (32–240). With Follow on, playback pages the view along;
    a manual horizontal scroll pauses that until the next Play (PRD §8.3).
    Zoom and scroll are UI State, never undoable. Clip gestures live in TrackLanes.
*/
class ArrangementView : public juce::Component,
                        public juce::DragAndDropTarget,
                        private juce::Timer,
                        private ApplicationModel::Listener,
                        private ThemeManager::Listener,
                        private juce::ValueTree::Listener
{
public:
    static constexpr const char* componentId = "arrangement";

    ArrangementView (const ApplicationModel&, CommandRegistry&, ThemeManager&, UIStateStore&, ShellState&);

    void cancelDrag()   { lanes.cancelDrag(); }

    void zoomIn()    { zoomBy (2.0); }
    void zoomOut()   { zoomBy (0.5); }

    /** Z: the selected clips fill the view. */
    void zoomToSelection();

    /** Shift+Z: the whole song fills the view. */
    void zoomToSong();
    ~ArrangementView() override;

    /** Double-click on a MIDI clip (Piano Roll) or an audio clip (Editor). */
    std::function<void (const juce::String& clipId)> onMidiClipOpened, onAudioClipOpened;

    /** A Browser device dropped on a track (path: what plugin.insert takes).
        Unset, it is inserted here and nothing more. */
    std::function<void (const juce::String& trackId, const juce::String& path)> onDeviceDropped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    void paintOverChildren (juce::Graphics&) override;

    // Browser items dropped on a track header or lane (PRD §6.2)
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    const ApplicationModel& model;
    ThemeManager& themeManager;
    ArrangementViewState view;

    TimelineHeader timeline;
    TrackList trackList;
    TrackLanes lanes;
    Playhead playhead { model, themeManager, view };
    std::vector<TrackInfo> tracks;
    CommandRegistry& commands;
    ShellState& shell;
    bool followPaused = false, wasPlaying = false;

    int laneHeight() const   { return lanes.laneHeight(); }
    void zoomBy (double factor);
    void updateZoomLimits();
    void scrolledByHand();
    void timerCallback() override;

    struct DropTarget
    {
        int row = -1;
        bool valid = false;
    };

    DropTarget dropTarget;
    DropTarget dropTargetAt (const SourceDetails&) const;

    void refresh();
    void clampVerticalScroll();
    void selectRow (int row, juce::ModifierKeys);

    void modelChanged() override        { refresh(); }
    void themeChanged() override        { trackList.applyTheme(); repaint(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
};

} // namespace resamper
