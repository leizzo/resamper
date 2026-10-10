#include "HostedAudio.h"
#include "TestFixture.h"
#include "Engine/Mixer.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace resamper::test
{

struct RecordingTests : juce::UnitTest
{
    RecordingTests() : juce::UnitTest ("Recording", "Resamper") {}

    /** A Project with one track, running on the hosted device (created first,
        so the Edit sees its inputs). */
    struct RecordingFixture : HostedAudio, Fixture
    {
        RecordingFixture()   { invoke (cmd::trackAdd); }

        TrackInfo track (int index = 0) const        { return model.getTracks()[(size_t) index]; }
        juce::String trackId (int index = 0) const   { return track (index).id; }

        /** Lets the engine rebuild its playback graph after input changes. */
        void settle()   { projects.getEdit().dispatchPendingUpdatesSynchronously(); }

        /** Arming changes what the graph monitors; the engine rebuilds it on the message loop. */
        void rebuildGraph()
        {
            settle();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            settle();
        }

        /** Records for this long onto the armed tracks, then stops. */
        void recordFor (double seconds, bool playNotes = false)
        {
            settle();
            invoke (cmd::transportRecord);
            process (seconds, playNotes);
            invoke (cmd::transportStop);
        }

        int undoDepth()
        {
            int steps = 0;

            while (model.canUndo())
            {
                model.undo();
                ++steps;
            }

            while (model.canRedo())
                model.redo();

            return steps;
        }
    };

    void runTest() override
    {
        beginTest ("The engine's audio inputs are listed; a new track has none and is not armed");
        {
            RecordingFixture f;
            expect (! f.model.getAudioInputs().isEmpty());
            expect (f.track().input.isEmpty());
            expect (! f.track().armed);
        }

        beginTest ("track.toggleArm arms a track, giving it the first input if it has none");
        {
            RecordingFixture f;
            const auto steps = f.undoDepth();

            f.invoke (cmd::trackToggleArm, { f.trackId() });
            expect (f.track().armed);
            expectEquals (f.track().input, f.model.getAudioInputs()[0]);

            f.invoke (cmd::trackToggleArm, { f.trackId() });
            expect (! f.track().armed);
            expectEquals (f.track().input, f.model.getAudioInputs()[0]);   // disarming keeps the input

            expectEquals (f.undoDepth(), steps);   // never undoable
        }

        beginTest ("track.setInput chooses a track's input; an empty name removes it");
        {
            RecordingFixture f;
            const auto inputs = f.model.getAudioInputs();
            const auto last = inputs[inputs.size() - 1];

            f.invoke (cmd::trackSetInput, { f.trackId(), last });
            expectEquals (f.track().input, last);

            f.invoke (cmd::trackSetInput, { f.trackId(), "no-such-input" });
            expectEquals (f.track().input, last);

            f.invoke (cmd::trackToggleArm, { f.trackId() });
            expect (f.track().armed);

            f.invoke (cmd::trackSetInput, { f.trackId(), {} });
            expect (f.track().input.isEmpty());
            expect (! f.track().armed);
        }

        beginTest ("Two tracks may record from one input");
        {
            RecordingFixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackToggleArm, { f.trackId (0) });
            f.invoke (cmd::trackToggleArm, { f.trackId (1) });
            expect (f.track (0).armed && f.track (1).armed);

            f.recordFor (0.5);
            expectEquals ((int) f.track (0).clips.size(), 1);
            expectEquals ((int) f.track (1).clips.size(), 1);
        }

        //==============================================================================
        beginTest ("transport.record records the armed track's input into a clip in the Project's Audio folder");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.recordFor (1.0);

            expect (! f.model.isRecording());
            expect (! f.model.isPlaying());

            const auto clips = f.track().clips;
            expectEquals ((int) clips.size(), 1);

            if (clips.size() == 1)
            {
                expectWithinAbsoluteError (clips[0].startSeconds, 0.0, 1e-6);
                expectWithinAbsoluteError (clips[0].lengthSeconds, 1.0, 0.05);
                expect (clips[0].file.existsAsFile());
                expect (clips[0].file.isAChildOf (f.projects.getProjectFolder().getChildFile ("Audio")),
                        clips[0].file.getFullPathName());
                expectEquals (clips[0].numTakes, 0);
            }
        }

        beginTest ("transport.record reports why it can't record: nothing armed, or a loop under 2 seconds");
        {
            RecordingFixture f;
            f.invoke (cmd::transportRecord);
            expect (! f.model.isRecording());
            expectEquals (f.errors.size(), 1);

            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.invoke (cmd::transportSetLoopRange, { 0.0, ApplicationModel::minLoopRecordingSeconds - 0.5 });
            f.invoke (cmd::transportRecord);
            expect (! f.model.isRecording());
            expectEquals (f.errors.size(), 2);

            f.invoke (cmd::transportToggleLoop);   // off: the loop no longer matters
            f.invoke (cmd::transportRecord);
            expect (f.model.isRecording());
            expectEquals (f.errors.size(), 2);
            f.invoke (cmd::transportStop);
        }

        beginTest ("Unarmed tracks record nothing");
        {
            RecordingFixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackToggleArm, { f.trackId (1) });
            f.recordFor (0.5);

            expect (f.track (0).clips.empty());
            expectEquals ((int) f.track (1).clips.size(), 1);
        }

        beginTest ("A recording is one undo step");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            const auto steps = f.undoDepth();
            f.recordFor (0.5);

            expectEquals (f.undoDepth(), steps + 1);
            f.invoke (cmd::editUndo);
            expect (f.track().clips.empty());
            expectEquals (f.numTracks(), 1);

            f.invoke (cmd::editRedo);
            expectEquals ((int) f.track().clips.size(), 1);
        }

        beginTest ("MIDI inputs are listed; arming a MIDI track gives it the first MIDI input");
        {
            RecordingFixture f;
            f.invoke (cmd::trackAddMidi);
            const auto midi = f.trackId (1);
            expect (! f.model.getMidiInputs().isEmpty());

            f.invoke (cmd::trackToggleArm, { midi });
            expect (f.track (1).armed);
            expectEquals (f.track (1).input, f.model.getMidiInputs()[0]);
        }

        beginTest ("A track only takes inputs of its own kind");
        {
            RecordingFixture f;
            f.invoke (cmd::trackAddMidi);

            f.invoke (cmd::trackSetInput, { f.trackId (1), f.model.getAudioInputs()[0] });
            expect (f.track (1).input.isEmpty());

            f.invoke (cmd::trackSetInput, { f.trackId (0), f.model.getMidiInputs()[0] });
            expect (f.track (0).input.isEmpty());

            const auto midiInputs = f.model.getMidiInputs();
            const auto last = midiInputs[midiInputs.size() - 1];
            f.invoke (cmd::trackSetInput, { f.trackId (1), last });
            expectEquals (f.track (1).input, last);
        }

        beginTest ("Recording an armed MIDI track writes its notes into a MIDI clip, as one undo step");
        {
            RecordingFixture f;
            f.invoke (cmd::trackAddMidi);
            f.invoke (cmd::trackSetInput, { f.trackId (1), "MIDI Input" });   // the hosted device's input
            f.invoke (cmd::trackToggleArm, { f.trackId (1) });
            const auto steps = f.undoDepth();

            f.recordFor (1.0, true);

            const auto clips = f.track (1).clips;
            expectEquals ((int) clips.size(), 1);

            if (clips.size() == 1)
            {
                expect (clips[0].kind == TrackKind::midi);
                expect (! clips[0].notes.empty());
                expectEquals (clips[0].notes.front().pitch, 60);
            }

            expectEquals (f.undoDepth(), steps + 1);
            f.invoke (cmd::editUndo);
            expect (f.track (1).clips.empty());
        }

        beginTest ("An armed MIDI track plays what comes in through its instrument; a disarmed one doesn't");
        {
            RecordingFixture f;
            f.invoke (cmd::trackAddMidi);
            const auto midi = f.trackId (1);
            f.invoke (cmd::trackSetInput, { midi, "MIDI Input" });
            f.rebuildGraph();
            expect (f.process (0.5, true) < 1.0e-4f);

            f.invoke (cmd::trackToggleArm, { midi });
            f.rebuildGraph();
            expect (f.process (0.5, true) > 1.0e-3f);
        }

        beginTest ("Count-in is off by default; transport.toggleCountIn turns a 2-bar count-in on and off");
        {
            RecordingFixture f;
            f.deviceManager.engine.getPropertyStorage().removeProperty (te::SettingID::countInMode);
            expect (! f.model.isCountInOn());

            f.invoke (cmd::transportToggleCountIn);
            expect (f.model.isCountInOn());
            expect (f.projects.getEdit().getCountInMode() == te::Edit::CountIn::twoBar);

            f.invoke (cmd::transportToggleCountIn);
            expect (! f.model.isCountInOn());
        }

        beginTest ("With the count-in on, Rec counts in 2 bars before recording from the playhead");
        {
            RecordingFixture f;
            f.model.setCountInOn (true);
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.settle();

            // The engine starts half a beat before the 2 bars, so the first click isn't clipped.
            const auto countIn = f.model.beatsToSeconds (2.0 * f.model.getBeatsPerBar (0.0) + 0.5);
            f.invoke (cmd::transportRecord);
            f.process (0.5);
            expect (f.model.getTransportPositionSeconds() < 0.0, "the playhead counts in before the start");

            f.process (countIn - 0.5 + 1.0);
            f.invoke (cmd::transportStop);
            f.model.setCountInOn (false);

            const auto clips = f.track().clips;
            expectEquals ((int) clips.size(), 1);

            if (clips.size() == 1)
            {
                expectWithinAbsoluteError (clips[0].startSeconds, 0.0, 1e-6);
                expectWithinAbsoluteError (clips[0].lengthSeconds, 1.0, 0.1);
            }
        }

        beginTest ("Shift-click Rec (countIn: false) records at once, and leaves the count-in on");
        {
            RecordingFixture f;
            f.model.setCountInOn (true);
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.settle();

            f.invoke (cmd::transportRecord, { false });
            f.process (1.0);
            f.invoke (cmd::transportStop);
            expect (f.model.isCountInOn());
            f.model.setCountInOn (false);

            const auto clips = f.track().clips;
            expectEquals ((int) clips.size(), 1);

            if (clips.size() == 1)
                expectWithinAbsoluteError (clips[0].lengthSeconds, 1.0, 0.05);
        }

        beginTest ("Stopping during the count-in records nothing");
        {
            RecordingFixture f;
            f.model.setCountInOn (true);
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            const auto steps = f.undoDepth();
            f.settle();

            f.invoke (cmd::transportRecord);
            f.process (1.0);
            f.invoke (cmd::transportStop);
            f.model.setCountInOn (false);

            expect (f.track().clips.empty());
            expect (! f.model.isRecording());
            expectEquals (f.undoDepth(), steps);
        }

        beginTest ("A return track never arms");
        {
            RecordingFixture f;
            auto& mixer = f.mixer;
            expect (mixer.addReturn ("Return").wasOk());
            const auto returnTrack = f.trackId (1);
            expect (f.track (1).isReturn);

            f.invoke (cmd::trackToggleArm, { returnTrack });
            expect (! f.track (1).armed);
        }

        beginTest ("Returning to the start while recording ends the recording as its own undo step");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            const auto steps = f.undoDepth();
            f.settle();
            f.invoke (cmd::transportRecord);
            f.process (0.5);
            f.invoke (cmd::transportReturnToStart);

            expect (! f.model.isRecording());
            expectEquals ((int) f.track().clips.size(), 1);
            expectEquals (f.undoDepth(), steps + 1);
        }

        beginTest ("The loop can't change while recording");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.invoke (cmd::transportSetLoopRange, { 0.0, 2.0 });
            f.settle();
            f.invoke (cmd::transportRecord);
            f.process (0.5);

            f.invoke (cmd::transportToggleLoop);
            f.invoke (cmd::transportSetLoopRange, { 0.0, 3.0 });
            expect (f.model.isLooping());
            expectWithinAbsoluteError (f.model.getLoopRange().end, 2.0, 1e-6);
            f.invoke (cmd::transportStop);
        }

        beginTest ("While recording, the model reports each armed track's recording so far");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.settle();
            expect (f.model.getRecordings().empty());

            f.invoke (cmd::transportRecord);
            f.process (0.5);
            expect (f.model.isRecording());

            const auto recordings = f.model.getRecordings();
            expectEquals ((int) recordings.size(), 1);

            if (recordings.size() == 1)
            {
                expectEquals (recordings[0].trackId, f.trackId());
                expectWithinAbsoluteError (recordings[0].startSeconds, 0.0, 1e-6);
                expectWithinAbsoluteError (recordings[0].lengthSeconds, 0.5, 0.05);

                expect (f.model.createRecordingWaveform (f.trackId()) != nullptr);
            }

            f.invoke (cmd::transportStop);
            expect (f.model.getRecordings().empty());
        }

        beginTest ("A recording in an untitled Project moves with it on Save As");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.recordFor (0.5);

            f.projectSaveLocation = f.scratchDir().getChildFile ("Recorded");
            f.invoke (cmd::projectSaveAs);
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            f.invoke (cmd::projectNew);
            f.projectToOpen = f.projectSaveLocation;
            f.invoke (cmd::projectOpen);

            const auto clips = f.track().clips;
            expectEquals ((int) clips.size(), 1);

            if (clips.size() == 1)
            {
                expect (clips[0].file.existsAsFile());
                expect (clips[0].file.isAChildOf (f.projectSaveLocation), clips[0].file.getFullPathName());
            }
        }

        //==============================================================================
        beginTest ("transport.setLoopRange sets the loop and turns looping on; transport.toggleLoop flips it");
        {
            RecordingFixture f;
            const auto steps = f.undoDepth();
            expect (! f.model.isLooping());

            f.invoke (cmd::transportSetLoopRange, { 1.0, 3.0 });
            expect (f.model.isLooping());
            expectWithinAbsoluteError (f.model.getLoopRange().start, 1.0, 1e-6);
            expectWithinAbsoluteError (f.model.getLoopRange().end, 3.0, 1e-6);

            f.invoke (cmd::transportSetLoopRange, { 3.0, 3.0 });   // empty: ignored
            expectWithinAbsoluteError (f.model.getLoopRange().end, 3.0, 1e-6);

            f.invoke (cmd::transportToggleLoop);
            expect (! f.model.isLooping());
            f.invoke (cmd::transportToggleLoop);
            expect (f.model.isLooping());

            expectEquals (f.undoDepth(), steps);   // transport state is never undoable
        }

        beginTest ("Loop recording makes one clip holding a take per pass");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.invoke (cmd::transportSetLoopRange, { 0.0, 2.0 });
            f.recordFor (5.0);

            const auto clips = f.track().clips;
            expectEquals ((int) clips.size(), 1);

            if (clips.size() == 1)
            {
                expectWithinAbsoluteError (clips[0].lengthSeconds, 2.0, 0.05);
                expectEquals (clips[0].numTakes, 3);
                expect (juce::isPositiveAndBelow (clips[0].currentTake, 3));
            }
        }

        beginTest ("clip.setTake switches a clip's take as one undo step");
        {
            RecordingFixture f;
            f.invoke (cmd::trackToggleArm, { f.trackId() });
            f.invoke (cmd::transportSetLoopRange, { 0.0, 2.0 });
            f.recordFor (5.0);

            if (f.track().clips.empty())
            {
                expect (false, "the loop recording made no clip");
                return;
            }

            auto clip = f.track().clips[0];
            const auto original = clip.currentTake;
            const auto other = original == 0 ? 1 : 0;
            const auto steps = f.undoDepth();

            f.invoke (cmd::clipSetTake, { clip.id, other });
            expectEquals (f.track().clips[0].currentTake, other);
            expect (f.track().clips[0].file != clip.file);
            expect (f.track().clips[0].file.existsAsFile());

            f.invoke (cmd::clipSetTake, { clip.id, other });   // already current
            f.invoke (cmd::clipSetTake, { clip.id, 7 });       // no such take
            expectEquals (f.undoDepth(), steps + 1);

            f.invoke (cmd::editUndo);
            expectEquals (f.track().clips[0].currentTake, original);
            expect (f.track().clips[0].file == clip.file);
        }
    }
};

static RecordingTests recordingTests;

} // namespace resamper::test
