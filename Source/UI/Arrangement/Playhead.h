#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace resamper
{

class ApplicationModel;
class ArrangementViewState;
class ThemeManager;

/** A transparent overlay across the ruler and lanes drawing the transport
    position. Polls the transport at ~30 Hz (the engine's example pattern); it
    never touches the audio thread and only repaints when the line moves. */
class Playhead : public juce::Component,
                 private juce::Timer
{
public:
    Playhead (const ApplicationModel&, ThemeManager&, ArrangementViewState&);

    /** Re-reads the position now (after zoom/scroll). */
    void update();

    void paint (juce::Graphics&) override;

private:
    const ApplicationModel& model;
    ThemeManager& themeManager;
    ArrangementViewState& view;
    int lastX = -1;

    void timerCallback() override   { update(); }
};

} // namespace resamper
