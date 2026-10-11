#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace resamper
{

/** Zoom and scroll for the Arrangement, and the single owner of the
    time <-> x conversion. The values live in a UI State subtree,
    not in this object, so every view built over the same subtree agrees and
    rebuilt components pick up where the old ones left off.

    x is measured from the left edge of the timeline area.
*/
class ArrangementViewState
{
public:
    static constexpr double minPixelsPerSecond = 0.5;
    static constexpr double maxPixelsPerSecond = 2000.0;
    static constexpr double defaultPixelsPerSecond = 17.0;   ///< 34 px per bar at 120 BPM in 4/4 (PRD §8.1)

    explicit ArrangementViewState (juce::ValueTree uiState);

    double getPixelsPerSecond() const;
    double getScrollSeconds() const;
    int getScrollY() const;

    void setPixelsPerSecond (double);
    void setScrollSeconds (double);
    void setScrollY (int);

    /** Zooms by factor while keeping the time under anchorX fixed on screen. */
    void zoomAround (double factor, float anchorX);

    /** Zoom bounds in pixels per second. The view sets them from the bar
        length, for 8..400 px per bar (PRD §8.3). */
    void setZoomLimits (double minPixelsPerSecond, double maxPixelsPerSecond);

    /** Zooms and scrolls so [startSeconds, endSeconds] fills width, with a 5 % margin. */
    void zoomToFit (double startSeconds, double endSeconds, float width);

    /** Follow: when seconds is off the visible width, scrolls so it sits near
        the left edge. Returns whether it scrolled. */
    bool follow (double seconds, float width);

    static constexpr int minLaneHeight = 32, maxLaneHeight = 240;

    /** Track lane height (Alt + wheel); defaultHeight until the user changes it. */
    int getLaneHeight (int defaultHeight) const;
    void setLaneHeight (int);

    float timeToX (double seconds) const;
    double xToTime (float x) const;

    /** Top of a track row, and the row under a y, in lane coordinates
        (vertical scroll applied). yToRow may return a row that doesn't exist. */
    int rowToY (int row, int rowHeight) const;
    int yToRow (int y, int rowHeight) const;

    /** Whether clip moves and trims snap to the grid (Mod bypasses it while
        held). On until the user turns it off. The loop brace always snaps. */
    bool isSnapOn() const;
    void setSnapOn (bool);

    /** The grid size in beats, or adaptiveGrid for the grid the zoom shows. */
    static constexpr double adaptiveGrid = 0.0;
    double getGridBeats() const;
    void setGridBeats (double);

    /** Whether a track's automation shows (its header's Auto button). */
    bool isAutomationShown (const juce::String& trackId) const;
    void setAutomationShown (const juce::String& trackId, bool);

    /** Listen here for zoom/scroll changes. */
    juce::ValueTree& getState() noexcept             { return state; }
    const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
    double zoomMin = minPixelsPerSecond, zoomMax = maxPixelsPerSecond;
};

} // namespace resamper
