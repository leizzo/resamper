#include "TestFixture.h"

#include "Commands/ProductionCommands.h"

#include <tracktion_engine/tracktion_engine.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace resamper::test
{

/** Peak of a WAV, or 0 if the file is missing or empty. Same read as RenderTests. */
static float wavPeak (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples == 0)
        return 0.0f;

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer.getMagnitude (0, buffer.getNumSamples());
}

static juce::var sampleUIState()
{
    auto arrangement = std::make_unique<juce::DynamicObject>();
    arrangement->setProperty ("pixelsPerSecond", 80.0);
    auto state = std::make_unique<juce::DynamicObject>();
    state->setProperty ("arrangement", arrangement.release());
    return state.release();
}

struct ProductionTests : juce::UnitTest
{
    ProductionTests() : juce::UnitTest ("Production", "Resamper") {}

    void runTest() override
    {
        beginTest ("exportMix of an Edit with a sine clip writes a WAV peaking above 0.1");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipAdd);

            auto dest = f.scratchDir().getChildFile ("mix.wav");
            expect (f.invoke (cmd::fileExportMix, { dest.getFullPathName() }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expect (dest.existsAsFile());
            expectGreaterThan (wavPeak (dest), 0.1f);
        }

        beginTest ("bounceTrack of the sine track peaks above 0.1 and an empty track peaks near 0");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipAdd);

            const auto tone = f.model.getTracks()[0].id;
            const auto empty = f.model.getTracks()[1].id;
            expectEquals ((int) f.model.getTracks()[0].clips.size(), 1);
            expect (f.model.getTracks()[1].clips.empty());

            auto toneFile = f.scratchDir().getChildFile ("bounce-tone.wav");
            expect (f.invoke (cmd::trackBounce, { tone, toneFile.getFullPathName() }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expectGreaterThan (wavPeak (toneFile), 0.1f);

            auto emptyFile = f.scratchDir().getChildFile ("bounce-empty.wav");
            expect (f.invoke (cmd::trackBounce, { empty, emptyFile.getFullPathName() }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expectLessThan (wavPeak (emptyFile), 1.0e-3f);
        }

        beginTest ("saveTemplate + newFromTemplate round-trips the track and its clip");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            auto tone = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.audioFileToChoose = tone;
            f.invoke (cmd::clipAdd);
            f.app.uiState.restore (sampleUIState());
            f.projectSaveLocation = f.scratchDir().getChildFile ("Saved");
            f.invoke (cmd::projectSaveAs);
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            auto templ = f.scratchDir().getChildFile ("Template");
            expect (f.invoke (cmd::projectSaveTemplate, { templ.getFullPathName() }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expectEquals (f.model.getProjectName(), juce::String ("Saved"));
            expect (templ.getChildFile ("project.json").existsAsFile());

            f.invoke (cmd::trackAdd);
            expectEquals (f.numTracks(), 2);

            auto dest = f.scratchDir().getChildFile ("FromTemplate");
            expect (f.invoke (cmd::projectNewFromTemplate, { templ.getFullPathName(), dest.getFullPathName() }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            auto tracks = f.model.getTracks();
            expectEquals ((int) tracks.size(), 1);
            expectEquals ((int) tracks[0].clips.size(), 1);
            expect (tracks[0].clips[0].file == tone);
            expectEquals ((double) f.app.uiState.toVar()["arrangement"]["pixelsPerSecond"], 80.0);
        }

        beginTest ("autosave then track.add then recover restores the autosaved track count");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.app.uiState.restore (sampleUIState());
            const auto saved = f.numTracks();

            expect (f.invoke (cmd::projectAutosave));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expect (f.production.hasRecovery());

            f.invoke (cmd::trackAdd);
            f.app.uiState.restore ({});
            expectEquals (f.numTracks(), saved + 1);

            expect (f.invoke (cmd::projectRecover));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expectEquals (f.numTracks(), saved);
            expectEquals ((double) f.app.uiState.toVar()["arrangement"]["pixelsPerSecond"], 80.0);
        }

        beginTest ("hasNewerRecovery follows which Edit was written last");
        {
            Fixture f;
            f.app.uiState.restore (sampleUIState());
            const auto project = f.projects.getProjectFolder();
            expect (! Production::hasNewerRecovery (project));

            expect (f.invoke (cmd::projectAutosave));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            auto recoveryEdits = project.getChildFile ("Recovery").findChildFiles (juce::File::findFiles, false, "*.tracktionedit");
            auto projectEdits = project.findChildFiles (juce::File::findFiles, false, "*.tracktionedit");
            expectEquals (recoveryEdits.size(), 1);
            expect (! projectEdits.isEmpty());

            recoveryEdits[0].setLastModificationTime (projectEdits[0].getLastModificationTime() + juce::RelativeTime::seconds (2));
            expect (Production::hasNewerRecovery (project));

            projectEdits[0].setLastModificationTime (recoveryEdits[0].getLastModificationTime() + juce::RelativeTime::seconds (2));
            expect (! Production::hasNewerRecovery (project));
        }

        beginTest ("theme.use light then dark changes colour and keeps trackHeight");
        {
            Fixture f;
            const auto themeLoad = f.theme.load();
            expect (themeLoad.wasOk(), themeLoad.getErrorMessage());

            const auto background = f.theme.getTheme().background;
            const auto accent = f.theme.getTheme().accent;
            const auto trackHeight = f.theme.getMetrics().trackHeight;
            expectGreaterThan (trackHeight, 0);

            expect (f.invoke (cmd::themeUse, { "themes/light.json" }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expect (f.theme.getTheme().background != background);
            expect (f.theme.getTheme().accent != accent);
            expectEquals (f.theme.getMetrics().trackHeight, trackHeight);

            expect (f.invoke (cmd::themeUse, { "themes/dark.json" }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expect (f.theme.getTheme().background == background);
            expect (f.theme.getTheme().accent == accent);
            expectEquals (f.theme.getMetrics().trackHeight, trackHeight);
        }

        beginTest ("freeze either freezes the track or fails without a new undo step");
        {
            // Undo follows the engine: frozenIndividually uses a null UndoManager, so the
            // flag is not its own undo step. A successful freeze may still join the open
            // UndoManager transaction via the freeze-point plug-in insert. A failed freeze
            // must not append a transaction.
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipAdd);

            const auto id = f.model.getTracks()[0].id;
            auto& undo = f.projects.getEdit().getUndoManager();
            const auto undoCount = undo.getUndoDescriptions().size();
            auto result = f.production.freezeTrack (id);

            if (result.wasOk())
            {
                expect (f.production.isFrozen (id));
                expect (f.production.unfreezeTrack (id).wasOk(), "unfreeze failed");
                expect (! f.production.isFrozen (id));
            }
            else
            {
                expect (! f.production.isFrozen (id));
                expectEquals (undo.getUndoDescriptions().size(), undoCount);
            }
        }
    }
};

static ProductionTests productionTests;

} // namespace resamper::test
