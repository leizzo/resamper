#include "TestFixture.h"
#include "Commands/ApplicationCommandTable.h"
#include "Engine/Automation.h"
#include "Engine/ProjectManager.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper::test
{

namespace te = tracktion;

/** The Time Selection (#175) at the Command seam. At 120 BPM in 4/4 a bar is 2 s. */
struct TimeSelectionTests : juce::UnitTest
{
    TimeSelectionTests() : juce::UnitTest ("Time Selection", "Resamper") {}

    /** Two MIDI tracks. The first holds clip A on 0..2 s, with a note at 1.25 s,
        and clip B on 2..4 s; its volume has points at 1.5 s and 3.5 s. */
    struct Content : Fixture
    {
        Content()
        {
            invoke (cmd::trackAddMidi);
            invoke (cmd::trackAddMidi);
            invoke (cmd::clipAddMidiAt, { trackId(), 0.0 });
            invoke (cmd::clipAddMidiAt, { trackId(), 2.0 });
            invoke (cmd::noteAdd, { clip (0).id, 1.25, 0.25, 60 });
            automation.addPoint (trackId(), "volume", 1.5, -6.0f);
            automation.addPoint (trackId(), "volume", 3.5, -12.0f);
            model.deselectAll();
        }

        juce::String trackId (int i = 0) const   { return model.getTracks()[(size_t) i].id; }

        /** The first track's clips, earliest first. */
        std::vector<ClipInfo> clips() const
        {
            auto result = model.getTracks()[0].clips;
            std::sort (result.begin(), result.end(), [] (auto& a, auto& b) { return a.startSeconds < b.startSeconds; });
            return result;
        }

        ClipInfo clip (int i) const   { return clips()[(size_t) i]; }

        std::vector<double> pointTimes() const
        {
            std::vector<double> times;

            for (auto& p : automation.getPoints (trackId(), "volume"))
                times.push_back (p.timeSeconds);

            return times;
        }

        /** The notes that sound inside the clip, by their start from the clip's start. */
        static std::vector<double> noteStarts (const ClipInfo& c)
        {
            std::vector<double> starts;

            for (auto& n : c.notes)
                if (n.startSeconds >= -1.0e-9 && n.startSeconds < c.lengthSeconds)
                    starts.push_back (n.startSeconds);

            return starts;
        }
    };

    /** Each clip's start and end on the first track, flattened. */
    void expectClips (const Content& f, std::vector<double> edges, const juce::String& what)
    {
        const auto clips = f.clips();
        std::vector<double> actual;

        for (auto& c : clips)
        {
            actual.push_back (c.startSeconds);
            actual.push_back (c.startSeconds + c.lengthSeconds);
        }

        juce::StringArray a, e;

        for (auto v : actual) a.add (juce::String (v, 3));
        for (auto v : edges) e.add (juce::String (v, 3));

        expectEquals (a.joinIntoString (" "), e.joinIntoString (" "), what);
    }

    void expectTimes (std::vector<double> actual, std::vector<double> expected, const juce::String& what)
    {
        juce::StringArray a, e;

        for (auto v : actual) a.add (juce::String (v, 3));
        for (auto v : expected) e.add (juce::String (v, 3));

        expectEquals (a.joinIntoString (" "), e.joinIntoString (" "), what);
    }

    void expectSelection (const Content& f, double start, double end)
    {
        const auto selection = f.model.getTimeSelection();
        expect (selection.has_value(), "no Time Selection");

        if (selection)
        {
            expectWithinAbsoluteError (selection->start, start, 1.0e-6);
            expectWithinAbsoluteError (selection->end, end, 1.0e-6);
        }
    }

    void runTest() override
    {
        beginTest ("A Time Selection and a Clip Selection never coexist; Esc clears it; none of it is undoable");
        {
            Content f;
            f.model.selectClip (f.clip (0).id);
            const auto undoable = f.model.canUndo();

            expect (f.invoke (cmd::timeSelect, { 1.0, 3.0 }));
            expectSelection (f, 1.0, 3.0);
            expect (f.model.getSelectedClipIds().isEmpty(), "making a Time Selection keeps the clip selected");
            expect (f.model.canUndo() == undoable, "making a Time Selection added an undo step");

            f.invoke (cmd::clipSelect, { f.clip (1).id });
            expect (! f.model.getTimeSelection().has_value(), "selecting a clip keeps the Time Selection");

            f.invoke (cmd::timeSelect, { 1.0, 3.0 });
            f.invoke (cmd::editDeselectAll);
            expect (! f.model.getTimeSelection().has_value(), "Esc keeps the Time Selection");

            f.invoke (cmd::timeSelect, { 1.0, 3.0 });
            f.invoke (cmd::timeSelect, {});
            expect (! f.model.getTimeSelection().has_value(), "an empty range keeps the Time Selection");

            // A ruler click moves the playhead and keeps it.
            f.invoke (cmd::timeSelect, { 1.0, 3.0 });
            f.invoke (cmd::transportSetPosition, 5.0);
            expectSelection (f, 1.0, 3.0);
        }

        beginTest ("Duplicate copies the range right after itself, overwriting, as one undo step; the selection follows");
        {
            Content f;
            f.invoke (cmd::timeSelect, { 1.0, 2.0 });
            expect (f.invoke (cmd::timeDuplicate));

            expectClips (f, { 0, 2, 2, 3, 3, 4 }, "A, the copy of A's second half, then what is left of B");
            expectTimes (Content::noteStarts (f.clip (1)), { 0.25 }, "the copied note");
            expectTimes (f.pointTimes(), { 1.5, 2.5, 3.5 }, "the copied automation point");
            expectSelection (f, 2.0, 3.0);

            expect (f.invoke (cmd::editUndo));
            expectClips (f, { 0, 2, 2, 4 }, "after undo");
            expectTimes (f.pointTimes(), { 1.5, 3.5 }, "points after undo");
            expectSelection (f, 2.0, 3.0);
        }

        beginTest ("Insert Duplicate Time pushes later material back by the range's length");
        {
            Content f;
            f.invoke (cmd::timeSelect, { 1.0, 2.0 });
            const auto loop = f.model.getLoopRange();
            expect (f.invoke (cmd::timeInsertDuplicate));

            expectClips (f, { 0, 2, 2, 3, 3, 5 }, "B moved back a second");
            expectTimes (f.pointTimes(), { 1.5, 2.5, 4.5 }, "later points moved back");
            expectSelection (f, 2.0, 3.0);
            expectEquals (f.model.getLoopRange().end, loop.end);
            expectWithinAbsoluteError (f.model.getTempo(), 120.0, 1.0e-9);
        }

        beginTest ("Move Selection Here moves the content, leaving a gap and overwriting the target");
        {
            Content f;
            f.invoke (cmd::timeSelect, { 1.0, 2.0 });
            expect (f.invoke (cmd::timeMoveTo, { 3.0 }));

            expectClips (f, { 0, 1, 2, 3, 3, 4 }, "A's second half replaced B's");
            expectTimes (f.pointTimes(), { 3.5 }, "the point moved; the one it landed on was replaced");
            expectSelection (f, 3.0, 4.0);

            expect (f.invoke (cmd::editUndo));
            expectClips (f, { 0, 2, 2, 4 }, "after undo");
        }

        beginTest ("Alt-drag copies the content, leaving the original");
        {
            Content f;
            f.invoke (cmd::timeSelect, { 1.0, 2.0 });
            expect (f.invoke (cmd::timeMoveTo, { 6.0, true }));
            expectClips (f, { 0, 2, 2, 4, 6, 7 }, "a copy at 6 s");
            expectSelection (f, 6.0, 7.0);
        }

        beginTest ("Split Clips at Selection cuts every clip crossing either edge");
        {
            Content f;
            f.invoke (cmd::timeSelect, { 0.5, 3.0 });
            expect (f.invoke (cmd::timeSplit));
            expectClips (f, { 0, 0.5, 0.5, 2, 2, 3, 3, 4 }, "both clips split");
            expectSelection (f, 0.5, 3.0);
        }

        beginTest ("Clear Time Selection leaves a gap; Delete Time closes it; the Loop and playhead stay");
        {
            Content f;
            f.invoke (cmd::timeSelect, { 1.0, 3.0 });
            expect (f.invoke (cmd::timeClear));
            expectClips (f, { 0, 1, 3, 4 }, "a gap");
            expectTimes (f.pointTimes(), { 3.5 }, "the point inside went");

            f.invoke (cmd::editUndo);
            f.invoke (cmd::transportSetPosition, 3.5);
            const auto loop = f.model.getLoopRange();
            expect (f.invoke (cmd::timeDelete));
            expectClips (f, { 0, 1, 1, 2 }, "B pulled earlier");
            expectTimes (f.pointTimes(), { 1.5 }, "the later point pulled earlier");
            expectEquals (f.model.getTransportPositionSeconds(), 3.5);
            expectEquals (f.model.getLoopRange().start, loop.start);
            expectEquals (f.model.getLoopRange().end, loop.end);
        }

        beginTest ("A frozen track is left untouched");
        {
            Content f;
            f.invoke (cmd::clipAddMidiAt, { f.trackId (1), 0.0 });
            auto* frozen = te::findAudioTrackForID (f.projects.getEdit(), te::EditItemID::fromVar (f.trackId (1)));
            expect (frozen != nullptr);

            if (frozen != nullptr)
            {
                frozen->state.setProperty (te::IDs::frozen, true, nullptr);
                expect (frozen->isFrozen (te::Track::anyFreeze));
            }

            f.invoke (cmd::timeSelect, { 1.0, 3.0 });
            expect (f.invoke (cmd::timeSplit));
            expectEquals ((int) f.model.getTracks()[1].clips.size(), 1);
            expectEquals ((int) f.clips().size(), 4);
        }

        beginTest ("A Bus's automation moves with the range too");
        {
            Content f;
            expect (f.mixer.addBus ("Bus").wasOk());
            te::FolderTrack* bus = nullptr;

            for (auto* track : te::getAllTracks (f.projects.getEdit()))
                if (auto* folder = dynamic_cast<te::FolderTrack*> (track); folder != nullptr && folder->getVolumePlugin() != nullptr)
                    bus = folder;

            expect (bus != nullptr, "no Bus");

            if (bus != nullptr)
            {
                auto& curve = bus->getVolumePlugin()->volParam->getCurve();
                curve.addPoint (te::TimePosition::fromSeconds (3.5), 0.5f, 0.0f, nullptr);

                f.invoke (cmd::timeSelect, { 1.0, 3.0 });
                expect (f.invoke (cmd::timeDelete));
                expectEquals (curve.getNumPoints(), 1);
                expectWithinAbsoluteError (curve.getPointTime (0).inSeconds(), 1.5, 1.0e-6);
            }
        }

        beginTest ("With a Time Selection, Mod+D, Mod+E, Delete, Mod+L and Shift+Space act on it");
        {
            Content f;
            f.invoke (cmd::timeSelect, { 1.0, 2.0 });
            expect (f.invoke (cmd::clipDuplicate));
            expectClips (f, { 0, 2, 2, 3, 3, 4 }, "Mod+D duplicated the time");

            f.invoke (cmd::timeSelect, { 0.5, 1.5 });
            expect (f.invoke (cmd::clipSplit));
            expectEquals ((int) f.clips().size(), 5);

            expect (f.invoke (cmd::editDelete));
            expectClips (f, { 0, 0.5, 1.5, 2, 2, 3, 3, 4 }, "Delete cleared the time");

            f.invoke (cmd::transportLoopSelection);
            expect (f.model.isLooping());
            expectWithinAbsoluteError (f.model.getLoopRange().start, 0.5, 1.0e-6);
            expectWithinAbsoluteError (f.model.getLoopRange().end, 1.5, 1.0e-6);

            f.invoke (cmd::transportPlayFromSelection);
            f.invoke (cmd::transportStop);
            expectWithinAbsoluteError (f.model.getInsertMarkerSeconds(), 0.5, 1.0e-6);
        }

        beginTest ("Without a Time Selection the operations do nothing and are disabled");
        {
            Content f;

            for (auto* id : { cmd::timeDuplicate.id, cmd::timeInsertDuplicate.id, cmd::timeSplit.id,
                              cmd::timeClear.id, cmd::timeDelete.id, cmd::timeMoveTo.id })
                expect (! f.commands.find (id)->isEnabled(), juce::String (id) + " is enabled");

            expect (! f.model.duplicateTimeSelection (false));
            expectClips (f, { 0, 2, 2, 4 }, "unchanged");
        }

        beginTest ("The shortcuts: Mod+Shift+D inserts, Mod+Shift+Backspace deletes time; the menu shows the shared keys");
        {
            using MK = juce::ModifierKeys;
            const auto cmdShift = MK (MK::commandModifier | MK::shiftModifier);

            auto* insert = findBinding (juce::KeyPress ('D', cmdShift, 0), arrangeView);
            expect (insert != nullptr && juce::String (insert->commandId) == cmd::timeInsertDuplicate.id);

            auto* deleteTime = findBinding (juce::KeyPress (juce::KeyPress::backspaceKey, cmdShift, 0), arrangeView);
            expect (deleteTime != nullptr && juce::String (deleteTime->commandId) == cmd::timeDelete.id);

            expect (findShortcut (cmd::timeDuplicate.id) == findShortcut (cmd::clipDuplicate.id));
            expect (findShortcut (cmd::timeSplit.id) == findShortcut (cmd::clipSplit.id));
            expect (findShortcut (cmd::timeClear.id) == findShortcut (cmd::editDelete.id));
        }
    }
};

static TimeSelectionTests timeSelectionTests;

} // namespace resamper::test
