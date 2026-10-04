#include "NoteCommands.h"

namespace resamper
{

void registerNoteCommands (CommandRegistry& registry, ApplicationModel& model)
{
    registry.add (cmd::noteAdd, { "Add Note" }, [&model] (const NoteAddArgs& a)
    {
        model.addNote (a.clipId, a.startSeconds, a.lengthSeconds, a.pitch, a.velocity);
    });

    registry.add (cmd::noteDelete, { "Delete Notes", [&model] { return model.hasSelectedNotes(); } },
                  [&model] { model.deleteSelectedNotes(); });

    // A drag of one or more notes in the Piano Roll.
    registry.add (cmd::noteMove, { "Move Notes" }, [&model] (const NoteMoveArgs& a)
    {
        model.moveNotes (a.clipId, a.noteIds, a.deltaSeconds, a.deltaPitch);
    });

    // A drag on a note's edge in the Piano Roll.
    registry.add (cmd::noteResize, { "Resize Note" }, [&model] (const NoteResizeArgs& a)
    {
        model.resizeNote (a.clipId, a.noteId, a.startSeconds, a.endSeconds);
    });

    // A drag in the velocity lane.
    registry.add (cmd::noteSetVelocity, { "Set Note Velocity" }, [&model] (const NoteVelocityArgs& a)
    {
        model.setNoteVelocity (a.clipId, a.velocity, a.continuesGesture);
    });

    registry.add (cmd::noteQuantize, { "Quantize Notes" }, [&model] (const NoteQuantizeArgs& a)
    {
        if (a.grid.isNotEmpty())
            model.quantizeNotes (a.clipId, a.grid);
    });

    // Up / Down (Shift: octave) in the Piano Roll.
    registry.add (cmd::noteTransposeSelected, { "Transpose" }, [&model] (const NoteTransposeArgs& a)
    {
        juce::StringArray selected;

        if (auto clip = model.getClip (a.clipId))
            for (auto& note : clip->notes)
                if (note.selected)
                    selected.add (note.id);

        if (! selected.isEmpty())
            model.moveNotes (a.clipId, selected, 0.0, a.semitones);
    });

    registry.add (cmd::noteSelect, { "Select Notes" }, [&model] (const NoteSelectArgs& a) { model.selectNotes (a.noteIds); });

    // Mod+A in the Piano Roll.
    registry.add (cmd::noteSelectAll, { "Select All Notes" }, [&model] (const ClipArgs& a)
    {
        juce::StringArray ids;

        if (auto clip = model.getClip (a.clipId))
            for (auto& note : clip->notes)
                ids.add (note.id);

        model.selectNotes (ids);
    });
}

} // namespace resamper
