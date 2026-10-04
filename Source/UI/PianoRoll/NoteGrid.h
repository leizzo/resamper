#pragma once

#include "NoteComponent.h"

#include <map>
#include <optional>
#include <vector>

namespace resamper
{

class ArrangementViewState;
class CommandRegistry;

/** The pitch × time grid of one MIDI clip.

    Clicking empty space adds a quarter note (note.add). Clicking a note selects
    it; shift-click toggles it into the selection. Dragging a note moves it, and
    every selected note with it (note.move); dragging an edge resizes that note
    (note.resize). The drag is previewed here and committed on release as one
    Command. */
class NoteGrid : public juce::Component
{
public:
    NoteGrid (const ApplicationModel&, CommandRegistry&, ThemeManager&, ArrangementViewState&);

    void setClip (const ClipInfo&);
    void layoutNotes();

    void paint (juce::Graphics&) override;
    void resized() override   { layoutNotes(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class DragMode { move, resizeStart, resizeEnd };

    struct Drag
    {
        DragMode mode;
        std::vector<MidiNoteInfo> original, preview;
        double grabEditSeconds = 0;
        int grabPitch = 0;
    };

    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState& view;

    ClipInfo clip;
    std::map<juce::String, std::unique_ptr<NoteComponent>> notes;
    std::optional<Drag> drag;
    bool clickOnEmpty = false;

    NoteComponent* noteAt (juce::Point<int>) const;
    DragMode dragModeAt (const NoteComponent&, juce::Point<int>) const;
    const MidiNoteInfo* shown (const juce::String& id) const;
    void reload();
};

} // namespace resamper
