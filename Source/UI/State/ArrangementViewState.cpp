#include "ArrangementViewState.h"

namespace resamper
{

namespace
{
    const juce::Identifier pixelsPerSecondId ("pixelsPerSecond");
    const juce::Identifier scrollSecondsId ("scrollSeconds");
    const juce::Identifier scrollYId ("scrollY");
    const juce::Identifier laneHeightId ("laneHeight");
    const juce::Identifier snapId ("snap");
    const juce::Identifier gridBeatsId ("gridBeats");
}

ArrangementViewState::ArrangementViewState (juce::ValueTree uiState)
    : state (std::move (uiState))
{
}

double ArrangementViewState::getPixelsPerSecond() const
{
    return juce::jlimit (zoomMin, zoomMax,
                         (double) state.getProperty (pixelsPerSecondId, defaultPixelsPerSecond));
}

double ArrangementViewState::getScrollSeconds() const   { return std::max (0.0, (double) state.getProperty (scrollSecondsId, 0.0)); }
int ArrangementViewState::getScrollY() const            { return std::max (0, (int) state.getProperty (scrollYId, 0)); }

void ArrangementViewState::setPixelsPerSecond (double pps)
{
    state.setProperty (pixelsPerSecondId, juce::jlimit (zoomMin, zoomMax, pps), nullptr);
}

void ArrangementViewState::setZoomLimits (double minimum, double maximum)
{
    zoomMin = juce::jlimit (minPixelsPerSecond, maxPixelsPerSecond, minimum);
    zoomMax = juce::jlimit (zoomMin, maxPixelsPerSecond, maximum);

    if (const auto pps = getPixelsPerSecond(); pps < zoomMin || pps > zoomMax)
        setPixelsPerSecond (pps);
}

void ArrangementViewState::zoomToFit (double startSeconds, double endSeconds, float width)
{
    if (endSeconds <= startSeconds || width <= 0.0f)
        return;

    const auto margin = (endSeconds - startSeconds) * 0.05;
    setPixelsPerSecond ((double) width / (endSeconds - startSeconds + 2.0 * margin));
    setScrollSeconds (startSeconds - margin);
}

bool ArrangementViewState::follow (double seconds, float width)
{
    const auto x = timeToX (seconds);

    if (x >= 0.0f && x < width)
        return false;

    setScrollSeconds (seconds - 0.1 * (double) width / getPixelsPerSecond());
    return true;
}

bool ArrangementViewState::isSnapOn() const        { return state.getProperty (snapId, true); }
void ArrangementViewState::setSnapOn (bool on)      { state.setProperty (snapId, on, nullptr); }
double ArrangementViewState::getGridBeats() const   { return std::max (adaptiveGrid, (double) state.getProperty (gridBeatsId, adaptiveGrid)); }
void ArrangementViewState::setGridBeats (double beats)   { state.setProperty (gridBeatsId, std::max (adaptiveGrid, beats), nullptr); }

int ArrangementViewState::getLaneHeight (int defaultHeight) const
{
    return juce::jlimit (minLaneHeight, maxLaneHeight, (int) state.getProperty (laneHeightId, defaultHeight));
}

void ArrangementViewState::setLaneHeight (int height)
{
    state.setProperty (laneHeightId, juce::jlimit (minLaneHeight, maxLaneHeight, height), nullptr);
}

void ArrangementViewState::setScrollSeconds (double seconds)   { state.setProperty (scrollSecondsId, std::max (0.0, seconds), nullptr); }
void ArrangementViewState::setScrollY (int y)                  { state.setProperty (scrollYId, std::max (0, y), nullptr); }

bool ArrangementViewState::isAutomationShown (const juce::String& trackId) const
{
    return state.getProperty ("automation_" + trackId, false);
}

void ArrangementViewState::setAutomationShown (const juce::String& trackId, bool shown)
{
    state.setProperty ("automation_" + trackId, shown, nullptr);
}

void ArrangementViewState::zoomAround (double factor, float anchorX)
{
    const auto anchorTime = xToTime (anchorX);
    setPixelsPerSecond (getPixelsPerSecond() * factor);
    setScrollSeconds (anchorTime - anchorX / getPixelsPerSecond());
}

float ArrangementViewState::timeToX (double seconds) const
{
    return (float) ((seconds - getScrollSeconds()) * getPixelsPerSecond());
}

double ArrangementViewState::xToTime (float x) const
{
    return getScrollSeconds() + x / getPixelsPerSecond();
}

int ArrangementViewState::rowToY (int row, int rowHeight) const
{
    return row * rowHeight - getScrollY();
}

int ArrangementViewState::yToRow (int y, int rowHeight) const
{
    return (int) std::floor ((double) (y + getScrollY()) / rowHeight);
}

} // namespace resamper
