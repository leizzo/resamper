#include "Playhead.h"
#include "Engine/ApplicationModel.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

namespace
{
    constexpr int tipSize = 10;
}

Playhead::Playhead (const ApplicationModel& m, ThemeManager& tm, ArrangementViewState& v)
    : model (m), themeManager (tm), view (v)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void Playhead::update()
{
    const auto x = juce::roundToInt (view.timeToX (model.getTransportPositionSeconds()));

    if (x != lastX)
    {
        repaint (lastX - tipSize, 0, tipSize * 2, getHeight());
        repaint (x - tipSize, 0, tipSize * 2, getHeight());
        lastX = x;
    }
}

void Playhead::paint (juce::Graphics& g)
{
    if (lastX < 0 || lastX > getWidth())
        return;

    const auto lineWidth = themeManager.getMetrics().playheadWidth;
    g.setColour (themeManager.getTheme().playhead);
    g.fillRect (lastX - lineWidth / 2, 0, lineWidth, getHeight());

    // The triangular tip in the ruler.
    juce::Path tip;
    const auto x = (float) lastX;
    tip.addTriangle (x - tipSize / 2.0f, 0.0f, x + tipSize / 2.0f, 0.0f, x, (float) tipSize);
    g.fillPath (tip);
}

} // namespace resamper
