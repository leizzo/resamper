#include "Production.h"
#include "EditTracks.h"
#include "ProjectManager.h"
#include "EngineManager.h"
#include "PluginHosting.h"
#include "Render.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace resamper
{

namespace
{
    const char* recoveryFolderName = "Recovery";

    juce::File recoveryFolder (const ProjectManager& projects)
    {
        return projects.getProjectFolder().getChildFile (recoveryFolderName);
    }

    juce::Array<juce::File> editFilesIn (const juce::File& folder)
    {
        return folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + te::editFileSuffix);
    }

    /** Edit file, project.json, and Audio/. Not Cache or Recovery. */
    juce::Result copyProjectSnapshot (const juce::File& source, const juce::File& dest)
    {
        if (! source.isDirectory())
            return juce::Result::fail ("Not a Project folder: " + source.getFullPathName());

        if (dest == source)
            return juce::Result::fail ("Cannot copy a Project folder onto itself");

        auto edits = editFilesIn (source);

        if (edits.size() != 1)
            return juce::Result::fail ("A Project folder must contain exactly one Edit file: " + source.getFullPathName());

        auto json = source.getChildFile (ProjectManager::projectFileName);

        if (! json.existsAsFile())
            return juce::Result::fail ("Missing " + juce::String (ProjectManager::projectFileName) + " in " + source.getFullPathName());

        if (dest.exists() && ! dest.deleteRecursively())
            return juce::Result::fail ("Could not replace " + dest.getFullPathName());

        if (auto r = dest.createDirectory(); r.failed())
            return r;

        if (! edits.getFirst().copyFileTo (dest.getChildFile (edits.getFirst().getFileName())))
            return juce::Result::fail ("Could not copy the Edit into " + dest.getFullPathName());

        if (! json.copyFileTo (dest.getChildFile (ProjectManager::projectFileName)))
            return juce::Result::fail ("Could not copy " + juce::String (ProjectManager::projectFileName));

        auto audio = ProjectManager::getAudioFolder (source);

        if (audio.isDirectory() && ! audio.copyDirectoryTo (ProjectManager::getAudioFolder (dest)))
            return juce::Result::fail ("Could not copy Audio into " + dest.getFullPathName());

        return juce::Result::ok();
    }

    juce::Result writeRecoveryProjectFile (const juce::File& folder, const juce::var& uiState)
    {
        if (auto r = folder.createDirectory(); r.failed())
            return r;

        auto json = std::make_unique<juce::DynamicObject>();
        json->setProperty ("version", ProjectManager::projectFormatVersion);
        json->setProperty ("ui", uiState);

        auto file = folder.getChildFile (ProjectManager::projectFileName);

        if (! file.replaceWithText (juce::JSON::toString (juce::var (json.release()))))
            return juce::Result::fail ("Could not write " + file.getFullPathName());

        return juce::Result::ok();
    }
}

Production::Production (ProjectManager& pm)
    : projects (pm)
{
}

juce::Result Production::freezeTrack (const juce::String& trackId)
{
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);

    if (track == nullptr)
        return juce::Result::fail ("No track with id " + trackId);

    if (track->isFrozen (te::Track::individualFreeze))
        return juce::Result::ok();

    // AudioTrack::freezeTrack() is private. setFrozen(true, individualFreeze) is the
    // synchronous public entry: the frozenIndividually property change calls it on
    // this thread and renders through Renderer::Parameters (not renderToFile(Edit&, File)).
    // freezeTrackAsync() only posts that flag change, so it cannot be waited on here.
    //
    // Undo follows the engine. frozenIndividually is a CachedValue with a null
    // UndoManager, so the flag itself is not an undo step. The render inserts a
    // freeze-point plug-in through PluginList, which records on the Edit UndoManager
    // and joins the open transaction (it does not call beginNewTransaction). A failed
    // freeze sets the flag back, which removes that plug-in, and any transaction the
    // engine opened on top of the ones already there is dropped below — a failure
    // adds no new undo step.
    auto& undo = edit.getUndoManager();
    const auto undoCount = undo.getUndoDescriptions().size();

    auto& dm = edit.engine.getDeviceManager();

    if (dm.getSampleRate() <= 7000.0 || dm.getBlockSize() <= 0)
        return juce::Result::fail ("The engine could not freeze this track headless: no sample rate");

    if (! projects.getEngineManager().getPluginHosting().waitForLoads())
        return juce::Result::fail ("A plug-in is still loading: try again once it has");

    track->setFrozen (true, te::Track::individualFreeze);

    if (! track->isFrozen (te::Track::individualFreeze))
    {
        while (undo.getUndoDescriptions().size() > undoCount && undo.canUndo())
            undo.undo();

        return juce::Result::fail ("The engine could not freeze this track headless");
    }

    return juce::Result::ok();
}

juce::Result Production::unfreezeTrack (const juce::String& trackId)
{
    auto* track = findAudioTrack (projects.getEdit(), trackId);

    if (track == nullptr)
        return juce::Result::fail ("No track with id " + trackId);

    if (! track->isFrozen (te::Track::individualFreeze))
        return juce::Result::fail ("Track is not frozen");

    track->setFrozen (false, te::Track::individualFreeze);

    if (track->isFrozen (te::Track::individualFreeze))
        return juce::Result::fail ("The engine did not unfreeze the track");

    return juce::Result::ok();
}

bool Production::isFrozen (const juce::String& trackId) const
{
    for (auto* track : te::getAudioTracks (projects.getEdit()))
        if (track->itemID.toString() == trackId)
            return track->isFrozen (te::Track::individualFreeze);

    return false;
}

juce::Result Production::bounceTrack (const juce::String& trackId, const juce::File& destFile)
{
    auto* track = findAudioTrack (projects.getEdit(), trackId);

    if (track == nullptr)
        return juce::Result::fail ("No track with id " + trackId);

    return render::toWav (projects.getEngineManager().getPluginHosting(), projects.getEdit(), destFile,
                          render::bitForTrack (*track));
}

juce::Result Production::exportMix (const juce::File& destFile)
{
    auto& edit = projects.getEdit();
    return render::toWav (projects.getEngineManager().getPluginHosting(), edit, destFile,
                          te::toBitSet (te::getAllTracks (edit)));
}

juce::Result Production::saveTemplate (const juce::File& destFolder)
{
    if (projects.isUntitled())
        return juce::Result::fail ("An untitled Project must be saved before it can become a template");

    if (destFolder == juce::File() || destFolder.getFullPathName().isEmpty())
        return juce::Result::fail ("A template needs a destination folder");

    return copyProjectSnapshot (projects.getProjectFolder(), destFolder);
}

juce::Result Production::newFromTemplate (const juce::File& templateFolder, const juce::File& destFolder)
{
    if (! templateFolder.isDirectory())
        return juce::Result::fail ("Template folder does not exist: " + templateFolder.getFullPathName());

    if (destFolder == juce::File() || destFolder.getFullPathName().isEmpty())
        return juce::Result::fail ("New from template needs a destination folder");

    if (destFolder == templateFolder || destFolder.isAChildOf (templateFolder) || templateFolder.isAChildOf (destFolder))
        return juce::Result::fail ("Template and destination folders must be separate");

    // Copy only. Opening goes through ApplicationModel so it detaches its
    // listener before the old Edit is destroyed.
    return copyProjectSnapshot (templateFolder, destFolder);
}

juce::Result Production::autosave (const juce::var& uiState)
{
    // ProjectManager::save rejects an untitled Project. The folder already exists
    // (New writes it), so the recovery copy uses the public Edit file writer.
    auto& edit = projects.getEdit();
    auto folder = recoveryFolder (projects);
    auto editFile = folder.getChildFile ("Recovery" + juce::String (te::editFileSuffix));

    if (auto r = folder.createDirectory(); r.failed())
        return r;

    if (! te::EditFileOperations (edit).writeToFile (editFile, false))
        return juce::Result::fail ("Could not write the recovery Edit to " + editFile.getFullPathName());

    for (auto other : editFilesIn (folder))
        if (other != editFile)
            other.deleteFile();

    return writeRecoveryProjectFile (folder, uiState);
}

bool Production::hasRecovery() const
{
    auto folder = getRecoveryFolder();
    return folder.getChildFile (ProjectManager::projectFileName).existsAsFile()
        && editFilesIn (folder).size() == 1;
}

juce::File Production::getRecoveryFolder() const
{
    return recoveryFolder (projects);
}

bool Production::hasNewerRecovery (const juce::File& projectFolder)
{
    auto recovery = projectFolder.getChildFile (recoveryFolderName);
    auto recoveryEdits = editFilesIn (recovery);

    if (recoveryEdits.size() != 1 || ! recovery.getChildFile (ProjectManager::projectFileName).existsAsFile())
        return false;

    auto projectEdits = editFilesIn (projectFolder);

    if (projectEdits.isEmpty())
        return true;

    return recoveryEdits.getFirst().getLastModificationTime()
           >= projectEdits.getFirst().getLastModificationTime();
}

} // namespace resamper
