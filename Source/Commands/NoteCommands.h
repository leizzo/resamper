#pragma once

#include "ClipCommands.h"
#include "CommandRegistry.h"
#include "Engine/ApplicationModel.h"

namespace resamper
{

/** The notes to select in the open MIDI clip; empty selects none. */
struct NoteSelectArgs
{
    juce::StringArray noteIds;
};

/** A new note. Times are seconds from the clip's start. */
struct NoteAddArgs
{
    juce::String clipId;
    double startSeconds = 0, lengthSeconds = 0;
    int pitch = 0;
    int velocity = ApplicationModel::defaultNoteVelocity;
};

/** Every named note shifts by the same amount. */
struct NoteMoveArgs
{
    juce::String clipId;
    juce::StringArray noteIds;
    double deltaSeconds = 0;
    int deltaPitch = 0;
};

/** One note's new edges, seconds from the clip's start. */
struct NoteResizeArgs
{
    juce::String clipId;
    juce::String noteId;
    double startSeconds = 0, endSeconds = 0;
};

/** The selected notes' velocity. continuesGesture joins a drag into one undo step. */
struct NoteVelocityArgs
{
    juce::String clipId;
    int velocity = 0;
    bool continuesGesture = false;
};

/** grid is "1/4", "1/8" or "1/16". */
struct NoteQuantizeArgs
{
    juce::String clipId;
    juce::String grid;
};

/** Shifts a clip's selected notes by semitones. */
struct NoteTransposeArgs
{
    juce::String clipId;
    int semitones = 0;
};

namespace cmd
{
    inline constexpr CommandRef<NoteAddArgs> noteAdd { "note.add" };
    inline constexpr CommandRef<> noteDelete { "note.delete" };                        ///< the selected notes
    inline constexpr CommandRef<NoteMoveArgs> noteMove { "note.move" };
    inline constexpr CommandRef<NoteResizeArgs> noteResize { "note.resize" };
    inline constexpr CommandRef<NoteVelocityArgs> noteSetVelocity { "note.setVelocity" };
    inline constexpr CommandRef<NoteQuantizeArgs> noteQuantize { "note.quantize" };
    inline constexpr CommandRef<NoteTransposeArgs> noteTransposeSelected { "note.transposeSelected" };
    inline constexpr CommandRef<ClipArgs> noteSelectAll { "note.selectAll" };          ///< never undoable
    inline constexpr CommandRef<NoteSelectArgs> noteSelect { "note.select" };          ///< never undoable
}

/** Registers the note Commands above. */
void registerNoteCommands (CommandRegistry&, ApplicationModel&);

} // namespace resamper
