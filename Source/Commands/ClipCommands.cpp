#include "ClipCommands.h"
#include "AppCommandHost.h"

#include <limits>

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

    registry.add (cmd::clipAddMidiAt, { "Insert MIDI Clip" }, [&model, &host] (const ClipMidiAtArgs& a)
    {
        host.report (model.insertMidiClip (a.trackId, a.startSeconds));
    });

    registry.add (cmd::clipSelect, { "Select Clip" }, [&model] (const ClipSelectArgs& a) { model.selectClip (a.clipId, a.mode); });

    registry.add (cmd::clipSelectAll, { "Select All" }, [&model]
    {
        model.selectClipsInRange ({ 0.0, std::numeric_limits<double>::max() }, {});
    });

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

    auto timeSelected = [&model] { return model.getTimeSelection().has_value(); };

    // Mod+E: the Time Selection's edges when there is one.
    registry.add (cmd::clipSplit, { "Split Clip at Playhead",
                                    [&model, timeSelected]
                                    {
                                        return timeSelected()
                                            || model.canSplitClip (model.getSelectedClipId(), model.getTransportPositionSeconds());
                                    } },
                  [&model]
    {
        if (model.getTimeSelection())
            model.splitClipsAtTimeSelection();
        else
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

    // Mod+D: the Time Selection when there is one.
    registry.add (cmd::clipDuplicate, { "Duplicate", [clipsSelected, timeSelected] { return clipsSelected() || timeSelected(); } },
                  [&model]
    {
        if (model.getTimeSelection())
            model.duplicateTimeSelection (false);
        else
            model.duplicateSelectedClips();
    });

    registry.add (cmd::clipConsolidate, { "Consolidate", clipsSelected }, [&model, &host]
    {
        const auto count = model.getSelectedClipIds().size();

        if (auto r = model.consolidateSelectedClips(); r.failed())
            host.report (r);
        else if (host.notify)
            host.notify ("Consolidated " + juce::String (count) + " clips into one", true);
    });

    registry.add (cmd::clipDelete, { "Delete", clipsSelected }, [&model] { model.deleteSelectedClips(); });

    // A drag on empty lane space, or a Time Selection handle in the ruler.
    registry.add (cmd::timeSelect, { "Select Time" }, [&model] (const TimeRangeSeconds& range) { model.selectTime (range); });

    registry.add (cmd::timeDuplicate, { "Duplicate Time Selection", timeSelected }, [&model] { model.duplicateTimeSelection (false); });
    registry.add (cmd::timeInsertDuplicate, { "Insert Duplicate Time", timeSelected }, [&model] { model.duplicateTimeSelection (true); });

    // The Time Selection's context menu, or a drag of it (Alt copies).
    registry.add (cmd::timeMoveTo, { "Move Selection Here", timeSelected }, [&model] (const TimeMoveArgs& a)
    {
        model.moveTimeSelection (a.startSeconds, a.copy);
    });

    registry.add (cmd::timeSplit, { "Split Clips at Selection", timeSelected }, [&model] { model.splitClipsAtTimeSelection(); });
    registry.add (cmd::timeClear, { "Clear Time Selection", timeSelected }, [&model] { model.eraseTimeSelection(); });
    registry.add (cmd::timeDelete, { "Delete Time", timeSelected }, [&model] { model.deleteTimeSelection(); });
}

} // namespace resamper
