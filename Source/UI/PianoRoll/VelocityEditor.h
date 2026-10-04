#pragma once

#include "Engine/ApplicationModel.h"

#include <optional>

namespace resamper
{

class ArrangementViewState;
class CommandRegistry;
class ThemeManager;

/** Velocity bars for the open clip's notes. Dragging a bar sets the velocity
    of the selected notes (or just that note, if it wasn't selected) when the
    mouse is released: one note.setVelocity. */
class VelocityEditor : public juce::Component
{
public:
    VelocityEditor (const ApplicationModel&, CommandRegistry&, ThemeManager&, ArrangementViewState&);

    void setClip (const ClipInfo&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState& view;

    ClipInfo clip;
    std::optional<int> previewVelocity;

    const MidiNoteInfo* noteAt (int x) const;
    int velocityAt (int y) const;
    void reload();
};

} // namespace resamper
