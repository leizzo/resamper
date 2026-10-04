#include "TestFixture.h"

namespace resamper::test
{

struct ProjectTests : juce::UnitTest
{
    ProjectTests() : juce::UnitTest ("Projects", "Resamper") {}

    static juce::var sampleUIState()
    {
        auto arrangement = std::make_unique<juce::DynamicObject>();
        arrangement->setProperty ("pixelsPerSecond", 120.0);
        auto state = std::make_unique<juce::DynamicObject>();
        state->setProperty ("arrangement", arrangement.release());
        return state.release();
    }

    void runTest() override
    {
        beginTest ("Save As then Open round-trips the Edit intact");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("media/tone.wav"), 1.25);
            f.model.selectTrack (f.model.getTracks()[1].id);
            f.invoke (cmd::clipAdd);
            f.invoke (cmd::trackSetVolume, { f.model.getTracks()[0].id, Decibels (-6.0) });
            f.invoke (cmd::trackSetPan, { f.model.getTracks()[0].id, -0.25 });
            f.invoke (cmd::trackToggleMute, { f.model.getTracks()[0].id });
            f.invoke (cmd::trackToggleSolo, { f.model.getTracks()[1].id });

            f.projectSaveLocation = f.scratchDir().getChildFile ("My Project");
            f.invoke (cmd::projectSaveAs);
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expect (! f.model.isProjectUntitled());
            expectEquals (f.model.getProjectName(), juce::String ("My Project"));

            auto saved = f.model.getTracks();

            Fixture reopened;
            reopened.projectToOpen = f.projectSaveLocation;
            reopened.invoke (cmd::projectOpen);
            expect (reopened.errors.isEmpty(), reopened.errors.joinIntoString ("; "));

            auto loaded = reopened.model.getTracks();
            expectEquals ((int) loaded.size(), 2);
            expectEquals (loaded[0].name, saved[0].name);
            expectEquals (loaded[1].name, saved[1].name);
            expect (loaded[0].clips.empty());
            expectEquals ((int) loaded[1].clips.size(), 1);
            expect (loaded[1].clips[0].file == f.audioFileToChoose);
            expectWithinAbsoluteError (loaded[1].clips[0].lengthSeconds, 1.25, 1e-3);
            expectWithinAbsoluteError (loaded[0].volume.value, -6.0, 1e-3);
            expectWithinAbsoluteError (loaded[0].pan, -0.25, 1e-6);
            expect (loaded[0].muted && ! loaded[0].solo);
            expect (loaded[1].solo && ! loaded[1].muted);
            expect (! reopened.model.canUndo());
        }

        beginTest ("A Project folder holds one Edit, project.json, Audio/ and Cache/");
        {
            Fixture f;
            f.projectSaveLocation = f.scratchDir().getChildFile ("Layout");
            f.invoke (cmd::projectSaveAs);

            auto folder = f.projectSaveLocation;
            expectEquals (folder.findChildFiles (juce::File::findFiles, false, "*.tracktionedit").size(), 1);
            expect (folder.getChildFile ("project.json").existsAsFile());
            expect (folder.getChildFile ("Audio").isDirectory());
            expect (folder.getChildFile ("Cache").isDirectory());
        }

        beginTest ("project.json holds a version and UI State, and no engine-owned keys");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.app.uiState.restore (sampleUIState());
            f.projectSaveLocation = f.scratchDir().getChildFile ("Json");
            f.invoke (cmd::projectSaveAs);

            auto json = juce::JSON::parse (f.projectSaveLocation.getChildFile ("project.json"));
            auto* obj = json.getDynamicObject();
            expect (obj != nullptr);

            juce::StringArray keys;
            for (auto& p : obj->getProperties())
                keys.add (p.name.toString());

            keys.sort (false);
            expectEquals (keys.joinIntoString (","), juce::String ("ui,version"));
            expectEquals ((int) json["version"], ProjectManager::projectFormatVersion);
            expectEquals ((double) json["ui"]["arrangement"]["pixelsPerSecond"], 120.0);
        }

        beginTest ("project.open restores the UI State stored in project.json");
        {
            Fixture f;
            f.app.uiState.restore (sampleUIState());
            f.projectSaveLocation = f.scratchDir().getChildFile ("UIStateRoundTrip");
            f.invoke (cmd::projectSaveAs);

            Fixture reopened;
            reopened.projectToOpen = f.projectSaveLocation;
            reopened.invoke (cmd::projectOpen);
            expectEquals ((double) reopened.app.uiState.toVar()["arrangement"]["pixelsPerSecond"], 120.0);
        }

        beginTest ("project.save on an untitled Project falls through to Save As");
        {
            Fixture f;
            f.projectSaveLocation = f.scratchDir().getChildFile ("FirstSave");
            f.invoke (cmd::projectSave);
            expect (! f.model.isProjectUntitled());
            expect (f.projectSaveLocation.getChildFile ("project.json").existsAsFile());
        }

        beginTest ("project.save writes in place after Save As");
        {
            Fixture f;
            f.projectSaveLocation = f.scratchDir().getChildFile ("InPlace");
            f.invoke (cmd::projectSaveAs);

            f.invoke (cmd::trackAdd);
            f.projectSaveLocation = juce::File();   // a second chooser would be a bug
            f.invoke (cmd::projectSave);
            expect (f.errors.isEmpty());

            Fixture reopened;
            reopened.projectToOpen = f.scratchDir().getChildFile ("InPlace");
            reopened.invoke (cmd::projectOpen);
            expectEquals (reopened.numTracks(), 1);
        }

        beginTest ("Opening a Project from a newer format version fails and keeps the current Project");
        {
            Fixture f;
            f.projectSaveLocation = f.scratchDir().getChildFile ("Future");
            f.invoke (cmd::projectSaveAs);
            f.projectSaveLocation.getChildFile ("project.json").replaceWithText (R"({ "version": 999 })");

            Fixture other;
            other.invoke (cmd::trackAdd);
            other.projectToOpen = f.projectSaveLocation;
            other.invoke (cmd::projectOpen);

            expectEquals (other.errors.size(), 1);
            expectEquals (other.numTracks(), 1);
            expect (other.model.isProjectUntitled());
        }

        beginTest ("project.new replaces the Project with an empty untitled Edit");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.app.uiState.restore (sampleUIState());
            f.invoke (cmd::projectNew);

            expectEquals (f.numTracks(), 0);
            expect (f.model.isProjectUntitled());
            expect (! f.model.canUndo());
            expect (! f.app.uiState.toVar()["arrangement"].hasProperty ("pixelsPerSecond"));
        }
    }
};

static ProjectTests projectTests;

} // namespace resamper::test
