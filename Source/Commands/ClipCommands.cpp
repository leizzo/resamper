#include "ClipCommands.h"
#include "AppCommandHost.h"

namespace resamper
{

void registerClipCommands (CommandRegistry& registry, ApplicationModel& model, AppCommandHost& host)
{
    registry.add (cmd::clipAdd, { "Add Audio Clip..." }, [&model, &host]
    {
        host.chooseAudioFile ([&model, &host] (const juce::File& f) { host.report (model.insertAudioClip (f)); });
    });

    // A sample dropped from the Browser onto a lane.
    registry.add (cmd::clipInsertAt, { "Insert Audio Clip" }, [&model, &host] (const ClipInsertArgs& a)
    {
        host.report (model.insertAudioClipAt (a.file, a.trackId, a.startSeconds));
    });

    registry.add (cmd::clipAddMidi, { "Add MIDI Clip" }, [&model, &host] { host.report (model.insertMidiClip()); });

    registry.add (cmd::clipSelect, { "Select Clip" }, [&model] (const ClipSelectArgs& a) { model.selectClip (a.clipId, a.mode); });

    // A drag in the Arrangement.
    registry.add (cmd::clipMove, { "Move Clip" }, [&model] (const ClipMoveArgs& a)
    {
        model.moveClip (a.clipId, a.startSeconds, a.trackId);
    });

    // A drag on a clip's edge in the Arrangement.
    registry.add (cmd::clipResize, { "Resize Clip" }, [&model] (const ClipEdgesArgs& a)
    {
        model.resizeClip (a.clipId, a.startSeconds, a.endSeconds);
    });

    registry.add (cmd::clipSplit, { "Split Clip at Playhead",
                                    [&model] { return model.canSplitClip (model.getSelectedClipId(), model.getTransportPositionSeconds()); } },
                  [&model]
    {
        model.splitClip (model.getSelectedClipId(), model.getTransportPositionSeconds());
    });

    // A clip's take menu.
    registry.add (cmd::clipSetTake, { "Switch Take" }, [&model] (const ClipTakeArgs& a)
    {
        model.setClipTake (a.clipId, a.takeIndex);
    });

    // Alt-drag: a copy at the drop position.
    registry.add (cmd::clipCopy, { "Copy Clip" }, [&model, &host] (const ClipMoveArgs& a)
    {
        host.report (model.copyClip (a.clipId, a.startSeconds, a.trackId));
    });

    // The top-right corner drag.
    registry.add (cmd::clipLoopExtend, { "Loop Clip" }, [&model] (const ClipLoopArgs& a)
    {
        model.loopExtendClip (a.clipId, a.endSeconds);
    });

    registry.add (cmd::clipRename, { "Rename Clip" }, [&model] (const ClipRenameArgs& a) { model.renameClip (a.clipId, a.name); });

    registry.add (cmd::clipReverse, { "Reverse" }, [&model] (const ClipArgs& a)
    {
        model.reverseClip (a.clipId.isNotEmpty() ? a.clipId : model.getSelectedClipId());
    });

    registry.add (cmd::clipSetColour, { "Clip Colour" }, [&model] (const ClipColourArgs& a)
    {
        model.setClipColour (a.clipId, a.colourIndex);
    });

    // These act on the selected clips, so they are enabled only while some are.
    auto clipsSelected = [&model] { return ! model.getSelectedClipIds().isEmpty(); };

    registry.add (cmd::clipDuplicate, { "Duplicate", clipsSelected }, [&model] { model.duplicateSelectedClips(); });

    registry.add (cmd::clipConsolidate, { "Consolidate", clipsSelected }, [&model, &host]
    {
        const auto count = model.getSelectedClipIds().size();

        if (auto r = model.consolidateSelectedClips(); r.failed())
            host.report (r);
        else if (host.notify)
            host.notify ("Consolidated " + juce::String (count) + " clips into one", true);
    });

    registry.add (cmd::clipDelete, { "Delete", clipsSelected }, [&model] { model.deleteSelectedClips(); });
}

} // namespace resamper
