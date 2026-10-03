#pragma once

#include "Engine/ApplicationModel.h"

namespace resamper
{

class ArrangementViewState;
class CommandRegistry;
class ThemeManager;

/** Bar numbers above the note grid. A click moves the playhead. */
class BeatRuler : public juce::Component
{
public:
    BeatRuler (const ApplicationModel&, CommandRegistry&, ThemeManager&, ArrangementViewState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState& view;
};

} // namespace resamper
