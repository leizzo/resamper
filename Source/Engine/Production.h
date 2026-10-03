#pragma once

#include <juce_core/juce_core.h>

namespace resamper
{

class ProjectManager;

/** Production operations over the current Edit.

    No Tracktion types in this header. Freeze, bounce, and export render through
    the engine; templates and recovery are Project folders on disk.
*/
class Production
{
public:
    explicit Production (ProjectManager&);

    /** Renders the track to a freeze file the engine understands and marks it frozen.
        Fails cleanly (no crash, no new undo step) if the engine cannot freeze headless. */
    juce::Result freezeTrack (const juce::String& trackId);
    juce::Result unfreezeTrack (const juce::String& trackId);
    bool isFrozen (const juce::String& trackId) const;

    /** Offline render of one track to destFile (WAV). Other tracks are silent in the
        sense of tracksToDo = that track only. Same parameter rules as RenderTests. */
    juce::Result bounceTrack (const juce::String& trackId, const juce::File& destFile);

    /** Offline render of the whole mix to destFile. Same parameter rules as RenderTests. */
    juce::Result exportMix (const juce::File& destFile);

    /** Copies the current Project folder (edit, project.json, Audio/) to destFolder.
        Fails if untitled — caller must save first. destFolder becomes a template:
        it is not opened. */
    juce::Result saveTemplate (const juce::File& destFolder);

    /** Copies a template folder over destFolder. Does not open it: the caller opens
        through ApplicationModel::openProject, which detaches from the old Edit first. */
    juce::Result newFromTemplate (const juce::File& templateFolder, const juce::File& destFolder);

    /** Writes a recovery copy of the current Edit + project ui into <project>/Recovery/.
        Works for a saved project. For an untitled project, still writes into its
        existing folder (ProjectManager already created one). */
    juce::Result autosave (const juce::var& uiState);
    bool hasRecovery() const;

    /** The recovery Project folder. Open it with ApplicationModel::openProject. */
    juce::File getRecoveryFolder() const;

    /** How often the running app writes a recovery copy. */
    static constexpr int autosaveIntervalMs = 30 * 1000;

    /** True when projectFolder/Recovery holds an Edit at least as new as the
        Project's own Edit. Launch offers that copy. */
    static bool hasNewerRecovery (const juce::File& projectFolder);

private:
    ProjectManager& projects;

    JUCE_DECLARE_NON_COPYABLE (Production)
};

} // namespace resamper
