#include "TimelineHeader.h"
#include "Commands/AppCommands.h"
#include "UI/PianoRoll/BeatGrid.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

namespace
{
    /** Movement below this stays a click (move the playhead), not a loop drag. */
    constexpr int loopDragThresholdPixels = 4;
}

TimelineHeader::TimelineHeader (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, ArrangementViewState& v)
    : model (m), commands (c), themeManager (tm), view (v)
{
}

void TimelineHeader::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.bgPanel);

    // Bar numbers, one per grid line.
    const auto numberFont = themeManager.numberFont (TypeStyle { 10.0f, true, 400 });
    const auto every = barsPerGridLine (model, view);

    for (auto& line : barLines (model, view.xToTime (0.0f), view.xToTime ((float) getWidth()), every))
    {
        const auto x = juce::roundToInt (view.timeToX (line.seconds));
        g.setColour (theme.border);
        g.fillRect (x, 0, 1, getHeight());
        g.setColour (theme.textDim);
        g.setFont (numberFont);
        g.drawText (juce::String (line.barNumber), x + 5, 0, 40, getHeight(), juce::Justification::centredLeft, false);
    }

    // The loop brace: lime while looping, faint when off.
    const auto loop = draggedLoop.value_or (model.getLoopRange());

    if (loop.end > loop.start)
    {
        const auto x1 = view.timeToX (loop.start), x2 = view.timeToX (loop.end);
        const auto brace = juce::Rectangle<float> (x1, 2.0f, x2 - x1, 6.0f);
        g.setColour (theme.accent.withAlpha (draggedLoop || model.isLooping() ? 0.9f : 0.3f));
        g.fillRoundedRectangle (brace, 2.0f);
        g.fillRect (juce::Rectangle<float> (x1, 2.0f, 2.0f, 12.0f));
        g.fillRect (juce::Rectangle<float> (x2 - 2.0f, 2.0f, 2.0f, 12.0f));
    }

    g.setColour (theme.borderSoft);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

void TimelineHeader::mouseDown (const juce::MouseEvent& e)
{
    dragStartSeconds = std::max (0.0, view.xToTime ((float) e.x));
    draggedLoop.reset();
}

void TimelineHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < loopDragThresholdPixels)
        return;

    const auto now = std::max (0.0, view.xToTime ((float) e.x));
    draggedLoop = TimeRangeSeconds { std::min (dragStartSeconds, now), std::max (dragStartSeconds, now) };
    repaint();
}

void TimelineHeader::mouseUp (const juce::MouseEvent&)
{
    if (auto loop = std::exchange (draggedLoop, std::nullopt))
        commands.invoke (cmd::transportSetLoopRange, { loop->start, loop->end });
    else
        commands.invoke (cmd::transportSetPosition, dragStartSeconds);

    repaint();
}

} // namespace resamper
