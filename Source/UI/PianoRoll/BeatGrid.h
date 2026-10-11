#pragma once

#include "Engine/ApplicationModel.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"

#include <cmath>
#include <vector>

namespace resamper
{

/** One vertical line of the Piano Roll's beat grid, in Edit time. */
struct BeatLine
{
    double seconds = 0;
    bool bar = false;
    bool beat = false;     ///< a quarter note, including the downbeat
    int barNumber = 1;     ///< 1-based, counting from the Edit start
};

inline bool isBlackKey (int pitch)
{
    switch (pitch % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default:                                return false;
    }
}

/** MIDI pitch under a y in a lane that scrolls with the view. 127 is the top row. */
inline int pitchAtY (const ArrangementViewState& view, int y, int keyHeight)
{
    return juce::jlimit (0, 127, 127 - view.yToRow (y, keyHeight));
}

/** Finer than a quarter note only while the lines stay far enough apart to read. */
inline double gridStepBeats (double pixelsPerBeat, const LayoutMetrics& metrics)
{
    if (pixelsPerBeat >= (double) metrics.gridSixteenthPixels) return 0.25;
    if (pixelsPerBeat >= (double) metrics.gridEighthPixels) return 0.5;
    return 1.0;
}

inline double pixelsPerBeat (const ApplicationModel& model, const ArrangementViewState& view, float x)
{
    const auto time = std::max (0.0, view.xToTime (x));
    const auto beat = model.secondsToBeats (time);
    return (double) (view.timeToX (model.beatsToSeconds (beat + 1.0)) - view.timeToX (time));
}

inline std::vector<BeatLine> beatLines (const ApplicationModel& model, double startSeconds, double endSeconds, double stepBeats)
{
    std::vector<BeatLine> lines;

    if (stepBeats <= 0.0 || endSeconds <= startSeconds)
        return lines;

    const auto startBeat = model.secondsToBeats (std::max (0.0, startSeconds));
    const auto endBeat = model.secondsToBeats (std::max (0.0, endSeconds));
    const auto beatsPerBar = std::max (1, model.getBeatsPerBar (std::max (0.0, startSeconds)));
    const auto first = (long long) std::ceil (startBeat / stepBeats - 1.0e-9);
    const auto last = (long long) std::floor (endBeat / stepBeats + 1.0e-9);

    for (auto i = first; i <= last; ++i)
    {
        const auto beat = (double) i * stepBeats;
        const auto intoBar = std::fmod (beat, (double) beatsPerBar);
        const auto intoBeat = std::fmod (beat, 1.0);

        BeatLine line;
        line.seconds = model.beatsToSeconds (beat);
        line.bar = intoBar < 1.0e-4 || (double) beatsPerBar - intoBar < 1.0e-4;
        line.beat = intoBeat < 1.0e-4 || 1.0 - intoBeat < 1.0e-4;
        line.barNumber = (int) std::floor (beat / (double) beatsPerBar) + 1;
        lines.push_back (line);
    }

    return lines;
}

/** How many bars apart the Arrangement's grid lines and bar numbers are: the
    smallest power of two that keeps them minPixels apart (every 4 bars at the
    default 34 px per bar, PRD §8.1). */
inline int barsPerGridLine (const ApplicationModel& model, const ArrangementViewState& view, float minPixels = 100.0f)
{
    const auto pixelsPerBar = pixelsPerBeat (model, view, 0.0f) * std::max (1, model.getBeatsPerBar (0.0));

    for (int bars = 1; bars < 1024; bars *= 2)
        if (pixelsPerBar * bars >= (double) minPixels)
            return bars;

    return 1024;
}

/** The Arrangement's grid lines between two times, one every barsPerLine bars. */
inline std::vector<BeatLine> barLines (const ApplicationModel& model, double startSeconds, double endSeconds, int barsPerLine)
{
    return beatLines (model, startSeconds, endSeconds, (double) (barsPerLine * std::max (1, model.getBeatsPerBar (0.0))));
}

/** The grid the Arrangement's zoom shows, in beats: zoomed in, a beat or its
    1/8 or 1/16 subdivision as the Piano Roll's; zoomed out, 1, 2, 4 ... bars.
    The finest whose lines stay gridMinPixels apart. */
inline double adaptiveGridBeats (const ApplicationModel& model, const ArrangementViewState& view, const LayoutMetrics& metrics)
{
    const auto perBeat = pixelsPerBeat (model, view, 0.0f);
    const auto minPixels = (double) std::max (1, metrics.gridMinPixels);

    if (perBeat >= minPixels)
        return gridStepBeats (perBeat, metrics);

    const auto beatsPerBar = (double) std::max (1, model.getBeatsPerBar (0.0));

    for (int bars = 1; bars < 1024; bars *= 2)
        if (perBeat * beatsPerBar * (double) bars >= minPixels)
            return beatsPerBar * (double) bars;

    return beatsPerBar * 1024.0;
}

/** The grid clip moves and trims snap to, in beats: the size chosen in the
    ruler's menu, else the grid the zoom shows. */
inline double snapGridBeats (const ApplicationModel& model, const ArrangementViewState& view, const LayoutMetrics& metrics)
{
    const auto chosen = view.getGridBeats();
    return chosen > ArrangementViewState::adaptiveGrid ? chosen : adaptiveGridBeats (model, view, metrics);
}

/** The grid lines the lanes and ruler draw, in beats: the snap grid, but never
    closer than the zoom can show. */
inline double drawnGridBeats (const ApplicationModel& model, const ArrangementViewState& view, const LayoutMetrics& metrics)
{
    return std::max (snapGridBeats (model, view, metrics), adaptiveGridBeats (model, view, metrics));
}

/** seconds moved to the nearest line of the snap grid. */
inline double snapToGrid (const ApplicationModel& model, const ArrangementViewState& view, const LayoutMetrics& metrics,
                          double seconds)
{
    const auto step = snapGridBeats (model, view, metrics);
    return model.beatsToSeconds (std::round (model.secondsToBeats (seconds) / step) * step);
}

} // namespace resamper
