#include "EditCommands.h"
#include "ClipCommands.h"
#include "NoteCommands.h"

namespace resamper
{

void registerEditCommands (CommandRegistry& registry, ApplicationModel& model)
{
    registry.add (cmd::editUndo, { "Undo", [&model] { return model.canUndo(); } }, [&model] { model.undo(); });
    registry.add (cmd::editRedo, { "Redo", [&model] { return model.canRedo(); } }, [&model] { model.redo(); });

    // Delete / Backspace: the selected notes (Piano Roll), else the Time Selection's
    // content, else the selected clips.
    registry.add (cmd::editDelete, { "Delete",
                    [&model]
                    {
                        return model.hasSelectedNotes() || model.getTimeSelection().has_value()
                            || ! model.getSelectedClipIds().isEmpty();
                    } },
                  [&model, &registry]
    {
        registry.invoke (model.hasSelectedNotes()     ? cmd::noteDelete
                       : model.getTimeSelection()     ? cmd::timeClear
                                                      : cmd::clipDelete);
    });

    registry.add (cmd::editDeselectAll, { "Deselect All" }, [&model] { model.deselectAll(); });
}

} // namespace resamper
