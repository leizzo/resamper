#pragma once

#include "Engine/ApplicationModel.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

class ArrangementViewState;
class CommandRegistry;

/** The piano roll's keyboard: one row per MIDI pitch, black keys inset,
    scrolling with the note grid. A click clears the note selection, so Quantize
    can apply to every note. */
class PianoKeyboard : public juce::Component
{
public:
    PianoKeyboard (CommandRegistry&, ThemeManager&, ArrangementViewState&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState& view;
};

} // namespace resamper
