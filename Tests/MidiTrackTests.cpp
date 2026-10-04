#include "TestFixture.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper::test
{

struct MidiTrackTests : juce::UnitTest
{
    MidiTrackTests() : juce::UnitTest ("MIDI Tracks", "Resamper") {}

    /** Note editing is Phase 5b, so a test that needs notes places one through the engine. */
    void placeNote (Fixture& f, int pitch, double startBeat, double lengthBeats)
    {
        auto* clip = dynamic_cast<tracktion::MidiClip*> (tracktion::getAudioTracks (f.projects.getEdit())[0]->getClips()[0]);
        expect (clip != nullptr);
        clip->getSequence().addNote (pitch, tracktion::BeatPosition::fromBeats (startBeat),
                                     tracktion::BeatDuration::fromBeats (lengthBeats), 100, 0, nullptr);
    }

    /** Renders the Edit offline and returns its peak level, as RenderTests does. */
    float renderPeak (Fixture& f)
    {
        auto rendered = f.scratchDir().getChildFile ("midi-render.wav");
        rendered.deleteFile();

        auto& edit = f.projects.getEdit();
        tracktion::Renderer::Parameters params (edit);
        params.destFile = rendered;
        params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
        params.bitDepth = 24;
        params.sampleRateForAudio = edit.engine.getDeviceManager().getSampleRate();
        params.blockSizeForAudio = edit.engine.getDeviceManager().getBlockSize();
        params.time = { tracktion::TimePosition(), edit.getLength() };
        params.tracksToDo = tracktion::toBitSet (tracktion::getAllTracks (edit));
        params.usePlugins = params.useMasterPlugins = true;
        expect (tracktion::Renderer::renderToFile ({}, params).existsAsFile(), "the render produced no file");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (rendered));

        if (reader == nullptr || reader->lengthInSamples == 0)
            return 0.0f;

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer.getMagnitude (0, buffer.getNumSamples());
    }

    void runTest() override
    {
        beginTest ("track.addMidi adds a MIDI track beside audio tracks and reports its kind");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            expect (f.invoke (cmd::trackAddMidi));

            auto tracks = f.model.getTracks();
            expectEquals ((int) tracks.size(), 2);
            expect (tracks[0].kind == TrackKind::audio);
            expect (tracks[1].kind == TrackKind::midi);

            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 1);
            expect (f.model.getTracks()[0].kind == TrackKind::audio);

            f.invoke (cmd::editRedo);
            expectEquals (f.numTracks(), 2);
            expect (f.model.getTracks()[1].kind == TrackKind::midi);
        }

        beginTest ("Volume, pan, mute, solo and remove work on a MIDI track as on an audio track");
        {
            Fixture f;
            f.invoke (cmd::trackAddMidi);
            const auto id = f.model.getTracks()[0].id;

            f.invoke (cmd::trackSetVolume, { id, Decibels (-6.0) });
            f.invoke (cmd::trackSetPan, { id, 0.5 });
            f.invoke (cmd::trackToggleMute, { id });
            f.invoke (cmd::trackToggleSolo, { id });

            auto track = f.model.getTracks()[0];
            expectWithinAbsoluteError (track.volume.value, -6.0, 1e-3);
            expectWithinAbsoluteError (track.pan, 0.5, 1e-6);
            expect (track.muted && track.solo);

            // Mute and solo are not undoable, same as an audio track.
            f.invoke (cmd::editUndo);
            track = f.model.getTracks()[0];
            expectWithinAbsoluteError (track.pan, 0.0, 1e-6);
            expect (track.muted && track.solo);

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.model.getTracks()[0].volume.value, 0.0, 1e-3);

            f.invoke (cmd::trackRemove);
            expectEquals (f.numTracks(), 0);
            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 1);
            expect (f.model.getTracks()[0].kind == TrackKind::midi);
        }

        beginTest ("clip.addMidi puts an empty one-bar MIDI clip on the selected MIDI track at the playhead");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack (f.model.getTracks()[1].id);
            f.model.setTransportPosition (3.0);

            expect (f.invoke (cmd::clipAddMidi));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            auto tracks = f.model.getTracks();
            expect (tracks[0].clips.empty());
            expectEquals ((int) tracks[1].clips.size(), 1);

            auto& clip = tracks[1].clips[0];
            expect (clip.kind == TrackKind::midi);
            expect (clip.notes.empty());
            expectWithinAbsoluteError (clip.startSeconds, 3.0, 1e-6);
            expectWithinAbsoluteError (clip.lengthSeconds, 2.0, 1e-6);   // one bar at the default 120 bpm, 4/4

            const auto found = f.model.getClip (clip.id);
            expect (found.has_value());
            expect (found->kind == TrackKind::midi);
            expectWithinAbsoluteError (found->startSeconds, 3.0, 1e-6);
            expect (! f.model.getClip ("no-such-clip").has_value());
            expect (! f.model.getClip ({}).has_value());

            f.invoke (cmd::editUndo);
            expect (f.model.getTracks()[1].clips.empty());
            f.invoke (cmd::editUndo);   // the position and the selection are not undo steps; next is track.addMidi
            expectEquals (f.numTracks(), 1);
        }

        beginTest ("clip.addMidi with nothing selected goes to the first MIDI track");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack ({});

            expect (f.invoke (cmd::clipAddMidi));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expect (f.model.getTracks()[0].clips.empty());
            expectEquals ((int) f.model.getTracks()[1].clips.size(), 1);
            expect (f.model.getTracks()[1].clips[0].kind == TrackKind::midi);
        }

        beginTest ("clip.addMidi on an audio track reports an error and changes nothing");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.model.selectTrack (f.model.getTracks()[0].id);

            f.invoke (cmd::clipAddMidi);
            expectEquals (f.errors.size(), 1);
            expect (f.model.getTracks()[0].clips.empty());

            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 0);   // the only step was track.add
        }

        beginTest ("Selecting a MIDI clip marks it and creates no undo step");
        {
            Fixture f;
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack (f.model.getTracks()[0].id);
            f.invoke (cmd::clipAddMidi);
            const auto id = f.model.getTracks()[0].clips[0].id;

            f.model.selectClip (id);
            expect (f.model.getTracks()[0].clips[0].selected);

            f.invoke (cmd::editUndo);
            expect (f.model.getTracks()[0].clips.empty());   // undoes clip.addMidi, not the selection
        }

        beginTest ("A MIDI clip moves onto another MIDI track, and not onto an audio track");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAddMidi);
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack (f.model.getTracks()[1].id);
            f.invoke (cmd::clipAddMidi);
            const auto id = f.model.getTracks()[1].clips[0].id;
            const auto audio = f.model.getTracks()[0].id;
            const auto other = f.model.getTracks()[2].id;

            f.invoke (cmd::clipMove, { id, 1.0, audio });
            expectWithinAbsoluteError (f.model.getTracks()[1].clips[0].startSeconds, 0.0, 1e-6);

            f.invoke (cmd::clipMove, { id, 1.0, other });
            auto tracks = f.model.getTracks();
            expect (tracks[1].clips.empty());
            expectEquals (tracks[2].clips[0].id, id);
            expect (tracks[2].clips[0].kind == TrackKind::midi);
            expectWithinAbsoluteError (tracks[2].clips[0].startSeconds, 1.0, 1e-6);

            f.invoke (cmd::editUndo);
            expectEquals ((int) f.model.getTracks()[1].clips.size(), 1);
            f.invoke (cmd::editUndo);
            expect (f.model.getTracks()[1].clips.empty());   // the refused move was not a step
        }

        beginTest ("An audio clip does not move onto a MIDI track");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipAdd);
            f.invoke (cmd::trackAddMidi);
            const auto id = f.model.getTracks()[0].clips[0].id;

            f.invoke (cmd::clipMove, { id, 0.5, f.model.getTracks()[1].id });

            expectEquals ((int) f.model.getTracks()[0].clips.size(), 1);
            expect (f.model.getTracks()[1].clips.empty());
            expectWithinAbsoluteError (f.model.getTracks()[0].clips[0].startSeconds, 0.0, 1e-6);

            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 1);   // the refused move was not a step; this undoes track.addMidi
            expectEquals ((int) f.model.getTracks()[0].clips.size(), 1);
        }

        beginTest ("clip.resize lengthens a MIDI clip past one bar, and clip.split cuts it");
        {
            Fixture f;
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack (f.model.getTracks()[0].id);
            f.invoke (cmd::clipAddMidi);
            const auto id = f.model.getTracks()[0].clips[0].id;

            f.invoke (cmd::clipResize, { id, 0.0, 4.0 });
            expectWithinAbsoluteError (f.model.getTracks()[0].clips[0].lengthSeconds, 4.0, 1e-6);

            f.model.selectClip (id);
            f.model.setTransportPosition (1.5);
            expect (f.invoke (cmd::clipSplit));

            auto clips = f.model.getTracks()[0].clips;
            expectEquals ((int) clips.size(), 2);
            expect (clips[0].selected);
            expectWithinAbsoluteError (clips[0].lengthSeconds, 1.5, 1e-3);
            expectWithinAbsoluteError (clips[1].startSeconds, 1.5, 1e-3);
            expectWithinAbsoluteError (clips[1].lengthSeconds, 2.5, 1e-3);

            f.invoke (cmd::editUndo);
            expectEquals ((int) f.model.getTracks()[0].clips.size(), 1);
            expect (f.model.getTracks()[0].clips[0].selected);
            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.model.getTracks()[0].clips[0].lengthSeconds, 2.0, 1e-3);
        }

        beginTest ("clip.add with only a MIDI track creates an audio track for the clip");
        {
            Fixture f;
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack ({});
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);

            expect (f.invoke (cmd::clipAdd));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            auto tracks = f.model.getTracks();
            expectEquals ((int) tracks.size(), 2);
            expect (tracks[0].kind == TrackKind::midi);
            expect (tracks[0].clips.empty());
            expect (tracks[1].kind == TrackKind::audio);
            expectEquals ((int) tracks[1].clips.size(), 1);

            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 1);
            expect (f.model.getTracks()[0].kind == TrackKind::midi);
        }

        beginTest ("clip.add on a MIDI track reports an error and changes nothing");
        {
            Fixture f;
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack (f.model.getTracks()[0].id);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);

            f.invoke (cmd::clipAdd);
            expectEquals (f.errors.size(), 1);
            expect (f.model.getTracks()[0].clips.empty());

            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 0);
        }

        beginTest ("A MIDI clip reports its notes, and they play through the built-in instrument");
        {
            Fixture f;
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack (f.model.getTracks()[0].id);
            f.invoke (cmd::clipAddMidi);
            placeNote (f, 60, 0.0, 1.0);

            auto notes = f.model.getTracks()[0].clips[0].notes;
            expectEquals ((int) notes.size(), 1);
            expectEquals (notes[0].pitch, 60);
            expectWithinAbsoluteError (notes[0].startSeconds, 0.0, 1e-6);
            expectWithinAbsoluteError (notes[0].lengthSeconds, 0.5, 1e-3);   // one beat at 120 bpm

            expectGreaterThan (renderPeak (f), 0.01f);
        }

        beginTest ("A MIDI track, its clip and its notes survive Save and Open, and still play");
        {
            Fixture f;
            f.invoke (cmd::trackAddMidi);
            f.model.selectTrack (f.model.getTracks()[0].id);
            f.model.setTransportPosition (1.0);
            f.invoke (cmd::clipAddMidi);
            placeNote (f, 60, 0.0, 1.0);

            f.projectSaveLocation = f.scratchDir().getChildFile ("Midi Project");
            f.invoke (cmd::projectSaveAs);
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            Fixture reopened;
            reopened.projectToOpen = f.projectSaveLocation;
            reopened.invoke (cmd::projectOpen);
            expect (reopened.errors.isEmpty(), reopened.errors.joinIntoString ("; "));

            auto tracks = reopened.model.getTracks();
            expectEquals ((int) tracks.size(), 1);
            expect (tracks[0].kind == TrackKind::midi);
            expectEquals ((int) tracks[0].clips.size(), 1);
            expect (tracks[0].clips[0].kind == TrackKind::midi);
            expectWithinAbsoluteError (tracks[0].clips[0].startSeconds, 1.0, 1e-6);
            expectWithinAbsoluteError (tracks[0].clips[0].lengthSeconds, 2.0, 1e-3);
            expectEquals ((int) tracks[0].clips[0].notes.size(), 1);
            expectEquals (tracks[0].clips[0].notes[0].pitch, 60);

            expectGreaterThan (renderPeak (reopened), 0.01f);
        }
    }
};

static MidiTrackTests midiTrackTests;

} // namespace resamper::test
