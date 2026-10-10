#include "TestFixture.h"

namespace resamper::test
{

struct TrackChannelTests : juce::UnitTest
{
    TrackChannelTests() : juce::UnitTest ("Track Channel Controls", "Resamper") {}

    /** Two fresh tracks; the undo history holds their two track.add steps. */
    struct ChannelFixture : Fixture
    {
        ChannelFixture()
        {
            invoke (cmd::trackAdd);
            invoke (cmd::trackAdd);
        }

        TrackInfo track (int index = 0) const        { return model.getTracks()[(size_t) index]; }
        juce::String trackId (int index = 0) const   { return track (index).id; }

        /** Undoes one step; returns false if there was none. */
        bool undoOnce()   { const bool could = model.canUndo(); invoke (cmd::editUndo); return could; }
    };

    void runTest() override
    {
        beginTest ("A new track sits at 0 dB, centred, unmuted and not soloed");
        {
            ChannelFixture f;
            expectWithinAbsoluteError (f.track().volume.value, 0.0, 1e-3);
            expectWithinAbsoluteError (f.track().pan, 0.0, 1e-6);
            expect (! f.track().muted);
            expect (! f.track().solo);
        }

        //==============================================================================
        beginTest ("track.setVolume sets one track's volume as one undo step");
        {
            ChannelFixture f;
            f.invoke (cmd::trackSetVolume, { f.trackId (1), Decibels (-12.0) });

            expectWithinAbsoluteError (f.track (1).volume.value, -12.0, 1e-3);
            expectWithinAbsoluteError (f.track (0).volume.value, 0.0, 1e-3);

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.track (1).volume.value, 0.0, 1e-3);
            expectEquals (f.numTracks(), 2);   // only the volume change was undone

            f.invoke (cmd::editRedo);
            expectWithinAbsoluteError (f.track (1).volume.value, -12.0, 1e-3);
        }

        beginTest ("track.setVolume clamps to the fader range; -inf is silence");
        {
            ChannelFixture f;
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (40.0) });
            expectWithinAbsoluteError (f.track().volume.value, ApplicationModel::maxVolume.value, 1e-3);

            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (-500.0) });
            expectWithinAbsoluteError (f.track().volume.value, ApplicationModel::minVolume.value, 1e-3);
        }

        beginTest ("A volume change that changes nothing records no undo step");
        {
            ChannelFixture f;
            f.undoOnce();   // back to one track, so the only step left is its track.add
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (0.0) });
            f.invoke (cmd::trackSetPan, { f.trackId(), 0.003 });   // the engine snaps it to centre
            f.invoke (cmd::trackSetVolume, { "no-such-track", Decibels (-6.0) });
            f.invoke (cmd::trackSetVolume);   // no args at all

            expect (f.undoOnce());
            expectEquals (f.numTracks(), 0);
        }

        beginTest ("A fader gesture is one undo step, however many values it sends");
        {
            ChannelFixture f;
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (-1.0) });
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (-3.0), true });
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (-9.0), true });
            expectWithinAbsoluteError (f.track().volume.value, -9.0, 1e-3);

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.track().volume.value, 0.0, 1e-3);
            expectEquals (f.numTracks(), 2);
        }

        beginTest ("Separate gestures are separate undo steps");
        {
            ChannelFixture f;
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (-3.0) });
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (-6.0) });

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.track().volume.value, -3.0, 1e-3);
        }

        beginTest ("A continuing gesture never merges into an unrelated undo step");
        {
            ChannelFixture f;

            // Continuing after another track's gesture: its own step.
            f.invoke (cmd::trackSetVolume, { f.trackId (1), Decibels (-3.0) });
            f.invoke (cmd::trackSetVolume, { f.trackId (0), Decibels (-6.0), true });
            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.track (0).volume.value, 0.0, 1e-3);
            expectWithinAbsoluteError (f.track (1).volume.value, -3.0, 1e-3);

            // Continuing after a different edit: its own step.
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackSetVolume, { f.trackId (1), Decibels (-6.0), true });
            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 3);

            // Continuing after an undo: its own step.
            f.invoke (cmd::trackSetVolume, { f.trackId (1), Decibels (-6.0) });
            f.invoke (cmd::editUndo);
            f.invoke (cmd::trackSetVolume, { f.trackId (1), Decibels (-9.0), true });
            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.track (1).volume.value, -3.0, 1e-3);
        }

        //==============================================================================
        beginTest ("track.setPan pans one track as one undo step, clamped to [-1, 1]");
        {
            ChannelFixture f;
            f.invoke (cmd::trackSetPan, { f.trackId (1), -0.5 });
            expectWithinAbsoluteError (f.track (1).pan, -0.5, 1e-6);
            expectWithinAbsoluteError (f.track (0).pan, 0.0, 1e-6);

            f.invoke (cmd::trackSetPan, { f.trackId (1), 3.0 });
            expectWithinAbsoluteError (f.track (1).pan, 1.0, 1e-6);

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.track (1).pan, -0.5, 1e-6);
        }

        beginTest ("A pan gesture is one undo step, separate from a volume gesture");
        {
            ChannelFixture f;
            f.invoke (cmd::trackSetVolume, { f.trackId(), Decibels (-6.0) });
            f.invoke (cmd::trackSetPan, { f.trackId(), 0.2, true });
            f.invoke (cmd::trackSetPan, { f.trackId(), 0.4, true });

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.track().pan, 0.0, 1e-6);
            expectWithinAbsoluteError (f.track().volume.value, -6.0, 1e-3);
        }

        //==============================================================================
        beginTest ("track.toggleMute and track.toggleSolo flip one track, each as one undo step");
        {
            ChannelFixture f;
            f.invoke (cmd::trackToggleMute, { f.trackId (1) });
            f.invoke (cmd::trackToggleSolo, { f.trackId (0) });

            expect (f.track (1).muted);
            expect (! f.track (0).muted);
            expect (f.track (0).solo);
            expect (! f.track (1).solo);

            f.invoke (cmd::editUndo);   // the solo
            expect (! f.track (0).solo);
            expect (f.track (1).muted);

            f.invoke (cmd::editUndo);   // the mute
            expect (! f.track (1).muted);
            expectEquals (f.numTracks(), 2);

            f.invoke (cmd::editRedo);
            f.invoke (cmd::editRedo);
            expect (f.track (1).muted);
            expect (f.track (0).solo);

            f.invoke (cmd::trackToggleSolo, { f.trackId (0) });   // toggling back is a step of its own
            expect (! f.track (0).solo);
            f.invoke (cmd::editUndo);
            expect (f.track (0).solo);
        }

        beginTest ("Mute and solo that change nothing record no undo step");
        {
            ChannelFixture f;
            f.undoOnce();   // one track left; its track.add is the only step
            expect (! f.model.setTrackMuted (f.trackId(), false));
            expect (! f.model.setTrackSolo (f.trackId(), false));
            expect (! f.model.setTrackMuted ("unknown", true));

            expect (f.undoOnce());
            expectEquals (f.numTracks(), 0);
            expect (! f.model.canUndo());
        }

        beginTest ("Removing and restoring a track keeps its channel settings");
        {
            ChannelFixture f;
            f.invoke (cmd::trackSetVolume, { f.trackId (1), Decibels (-6.0) });
            f.invoke (cmd::trackSetPan, { f.trackId (1), 0.5 });
            f.invoke (cmd::trackToggleMute, { f.trackId (1) });

            f.invoke (cmd::trackRemove);
            f.invoke (cmd::editUndo);

            expectWithinAbsoluteError (f.track (1).volume.value, -6.0, 1e-3);
            expectWithinAbsoluteError (f.track (1).pan, 0.5, 1e-6);
            expect (f.track (1).muted);
        }

        beginTest ("Undo never clears the redo history, whatever the channel settings");
        {
            ChannelFixture f;
            f.invoke (cmd::trackRemove);   // a track with default settings
            f.invoke (cmd::editUndo);
            expect (f.model.canRedo());

            f.invoke (cmd::trackSetPan, { f.trackId(), 0.5 });   // the first change from a default
            f.invoke (cmd::editUndo);
            expect (f.model.canRedo());
            f.invoke (cmd::editRedo);
            expectWithinAbsoluteError (f.track().pan, 0.5, 1e-6);
        }
    }
};

static TrackChannelTests trackChannelTests;

} // namespace resamper::test
