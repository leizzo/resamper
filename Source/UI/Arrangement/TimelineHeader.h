#pragma once

#include "Engine/ApplicationModel.h"

#include <optional>

namespace resamper
{

class ArrangementViewState;
class CommandRegistry;
class ThemeManager;

/** The time ruler above the lanes: bar numbers, a tick on every grid line,
    and the loop brace, bright while looping and dim when off.

    A click moves the playhead (transport.setPosition); play starts from there.
    Dragging along it sets a new loop (transport.setLoopRange, on release).
    Dragging the brace moves the loop, and dragging either end resizes it,
    leaving looping on or off (transport.moveLoop). The loop always lands on
    the grid. Right-click: Loop, Loop Selection, Snap and the grid size. */
class TimelineHeader : public juce::Component
{
public:
    TimelineHeader (const ApplicationModel&, CommandRegistry&, ThemeManager&, ArrangementViewState&);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class DragMode { newLoop, moveLoop, loopStart, loopEnd };

    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState& view;

    DragMode dragMode = DragMode::newLoop;
    double dragStartSeconds = 0;
    TimeRangeSeconds loopAtDragStart;
    std::optional<TimeRangeSeconds> draggedLoop;   ///< previewed until release

    juce::Rectangle<float> braceArea (TimeRangeSeconds) const;
    DragMode dragModeAt (juce::Point<int>) const;
    double snapped (double seconds) const;
    void showMenu();
};

} // namespace resamper
