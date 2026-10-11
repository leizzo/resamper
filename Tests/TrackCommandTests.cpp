#include "TestFixture.h"

namespace resamper::test
{

struct TrackCommandTests : juce::UnitTest
{
    TrackCommandTests() : juce::UnitTest ("Track Commands", "Resamper") {}

    void runTest() override
    {
        beginTest ("A New Project holds an empty Edit with no undo history");
        {
            Fixture f;
            expectEquals (f.numTracks(), 0);
            expect (! f.model.canUndo());
        }

        beginTest ("track.add and track.remove mutate the Edit's track list");
        {
            Fixture f;
            expect (f.invoke (cmd::trackAdd));
            expect (f.invoke (cmd::trackAdd));
            expectEquals (f.numTracks(), 2);

            expect (f.invoke (cmd::trackRemove));
            expectEquals (f.numTracks(), 1);
        }

        beginTest ("track.remove removes the selected track");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            auto first = f.model.getTracks()[0].id;
            auto second = f.model.getTracks()[1].id;

            f.model.selectTrack (first);
            f.invoke (cmd::trackRemove);

            expectEquals (f.numTracks(), 1);
            expectEquals (f.model.getTracks()[0].id, second);
        }

        beginTest ("track.remove on an empty Edit changes nothing");
        {
            Fixture f;
            f.invoke (cmd::trackRemove);
            expectEquals (f.numTracks(), 0);
            expect (! f.model.canUndo());
        }

        beginTest ("edit.undo / edit.redo reverse and replay track.add, one step per Command");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);

            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 1);
            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 0);
            expect (! f.model.canUndo());

            f.invoke (cmd::editRedo);
            expectEquals (f.numTracks(), 1);
            f.invoke (cmd::editRedo);
            expectEquals (f.numTracks(), 2);
            expect (! f.model.canRedo());
        }

        beginTest ("edit.undo restores a removed track with its identity");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            auto before = f.model.getTracks()[0];

            f.invoke (cmd::trackRemove);
            f.invoke (cmd::editUndo);

            expectEquals (f.numTracks(), 1);
            expectEquals (f.model.getTracks()[0].id, before.id);
            expectEquals (f.model.getTracks()[0].name, before.name);
        }

        beginTest ("Selection never creates undo steps");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            auto id = f.model.getTracks()[0].id;

            f.model.selectTrack (id);
            expect (f.model.getTracks()[0].selected);

            // The only undo step is the track.add itself.
            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 0);
            expect (! f.model.canUndo());
        }

        beginTest ("New tracks take the next colour of the track palette; track.setColour changes it, one undo step");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAddMidi);
            f.invoke (cmd::trackAdd);
            auto tracks = f.model.getTracks();
            expectEquals (tracks[0].colourIndex, 0);
            expectEquals (tracks[1].colourIndex, 1);
            expectEquals (tracks[2].colourIndex, 2);

            f.invoke (cmd::trackSetColour, { tracks[1].id, 5 });
            expectEquals (f.model.getTracks()[1].colourIndex, 5);

            f.invoke (cmd::trackSetColour, { tracks[1].id, 99 });   // out of the palette: refused
            expectEquals (f.model.getTracks()[1].colourIndex, 5);

            f.invoke (cmd::editUndo);
            expectEquals (f.model.getTracks()[1].colourIndex, 1);
        }

        auto ids = [] (Fixture& f)
        {
            juce::StringArray result;

            for (auto& track : f.model.getTracks())
                result.add (track.id);

            return result;
        };

        beginTest ("A new track goes after the selected track and becomes the selection (#176)");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            const auto before = ids (f);

            f.model.selectTrack (before[0]);
            f.invoke (cmd::trackAddMidi);

            const auto after = ids (f);
            expectEquals (after.size(), 3);
            expectEquals (after[0], before[0]);
            expectEquals (after[2], before[1]);
            expectEquals (f.model.getSelectedTrackId(), after[1]);
            expect (f.model.getTracks()[1].kind == TrackKind::midi);
        }

        beginTest ("With nothing selected a new track goes at the end; with afterTrackId it goes after that track");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            f.model.deselectAll();
            f.invoke (cmd::trackAdd);
            const auto three = ids (f);
            expectEquals (f.model.getSelectedTrackId(), three[2]);

            f.model.selectTrack (three[2]);
            f.invoke (cmd::trackAdd, { three[0] });   // the clicked lane wins over the selection

            const auto four = ids (f);
            expectEquals (four[0], three[0]);
            expectEquals (four[2], three[1]);
            expectEquals (f.model.getSelectedTrackId(), four[1]);
        }

        beginTest ("A Return as the reference puts a new Audio or MIDI track before the first Return");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::mixerAddReturn);
            f.invoke (cmd::mixerAddReturn);
            const auto before = ids (f);

            f.invoke (cmd::trackAddMidi, { before[2] });
            auto tracks = f.model.getTracks();
            expectEquals ((int) tracks.size(), 4);
            expect (! tracks[1].isReturn && tracks[2].isReturn && tracks[3].isReturn);

            f.model.selectTrack (before[1]);
            f.invoke (cmd::trackAdd);
            tracks = f.model.getTracks();
            expect (! tracks[2].isReturn && tracks[3].isReturn && tracks[4].isReturn);
            expectEquals (f.model.getSelectedTrackId(), tracks[2].id);
        }

        beginTest ("Adding a track after another is one undo step");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            const auto before = ids (f);

            f.invoke (cmd::trackAddMidi, { before[0] });
            f.invoke (cmd::editUndo);
            expect (ids (f) == before);
        }
    }
};

static TrackCommandTests trackCommandTests;

} // namespace resamper::test
