#pragma once

#include "CommandRegistry.h"
#include "Engine/ApplicationModel.h"

namespace resamper
{

struct AppCommandHost;

/** Names one clip. */
struct ClipArgs
{
    juce::String clipId;
};

/** A click on a clip: replaces the selection, or extends or toggles it. */
struct ClipSelectArgs
{
    juce::String clipId;
    ApplicationModel::SelectionMode mode = ApplicationModel::SelectionMode::replace;
};

/** An audio file dropped on a track at a position. */
struct ClipInsertArgs
{
    juce::File file;
    juce::String trackId;
    double startSeconds = 0;
};

/** A clip's new start, and optionally the track to move it to. */
struct ClipMoveArgs
{
    juce::String clipId;
    double startSeconds = 0;
    juce::String trackId = {};   ///< empty: the clip stays on its track
};

/** A clip's new edges. */
struct ClipEdgesArgs
{
    juce::String clipId;
    double startSeconds = 0, endSeconds = 0;
};

/** How far a clip repeats its content: up to endSeconds. */
struct ClipLoopArgs
{
    juce::String clipId;
    double endSeconds = 0;
};

/** A clip's take: a 0-based take index. */
struct ClipTakeArgs
{
    juce::String clipId;
    int takeIndex = 0;
};

/** A clip's new name. */
struct ClipRenameArgs
{
    juce::String clipId;
    juce::String name;
};

/** A clip's colour: a palette index, or -1 for the track's colour. */
struct ClipColourArgs
{
    juce::String clipId;
    int colourIndex = -1;
};

namespace cmd
{
    inline constexpr CommandRef<> clipAdd { "clip.add" };
    inline constexpr CommandRef<ClipInsertArgs> clipInsertAt { "clip.insertAt" };
    inline constexpr CommandRef<> clipAddMidi { "clip.addMidi" };
    inline constexpr CommandRef<ClipMoveArgs> clipMove { "clip.move" };
    inline constexpr CommandRef<ClipEdgesArgs> clipResize { "clip.resize" };
    inline constexpr CommandRef<> clipSplit { "clip.split" };                      ///< the selected clip, at the playhead
    inline constexpr CommandRef<ClipTakeArgs> clipSetTake { "clip.setTake" };
    inline constexpr CommandRef<ClipMoveArgs> clipCopy { "clip.copy" };            ///< a copy at the new start
    inline constexpr CommandRef<ClipLoopArgs> clipLoopExtend { "clip.loopExtend" };
    inline constexpr CommandRef<ClipRenameArgs> clipRename { "clip.rename" };
    inline constexpr CommandRef<ClipArgs> clipReverse { "clip.reverse" };          ///< no clipId: the selected clip
    inline constexpr CommandRef<ClipColourArgs> clipSetColour { "clip.setColour" };
    inline constexpr CommandRef<ClipSelectArgs> clipSelect { "clip.select" };     ///< never undoable

    // These act on the selected clips.
    inline constexpr CommandRef<> clipDuplicate { "clip.duplicate" };
    inline constexpr CommandRef<> clipConsolidate { "clip.consolidate" };
    inline constexpr CommandRef<> clipDelete { "clip.delete" };
}

/** Registers the clip Commands above. */
void registerClipCommands (CommandRegistry&, ApplicationModel&, AppCommandHost&);

} // namespace resamper
