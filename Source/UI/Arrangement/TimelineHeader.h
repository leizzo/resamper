#pragma once

#include "Engine/ApplicationModel.h"

#include <optional>

namespace resamper
{

class ArrangementViewState;
class CommandRegistry;
class ThemeManager;

/** The time ruler above the lanes. A click moves the playhead
    (transport.setPosition); play starts from there. Dragging along it sets a
    new loop (transport.setLoopRange, on release). The loop is drawn in the
    ruler, bright while looping. */
class TimelineHeader : public juce::Component
{
public:
    TimelineHeader (const ApplicationModel&, CommandRegistry&, ThemeManager&, ArrangementViewState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState& view;

    double dragStartSeconds = 0;
    std::optional<TimeRangeSeconds> draggedLoop;   ///< previewed until release
};

} // namespace resamper
