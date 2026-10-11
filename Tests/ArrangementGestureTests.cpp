#include "TestFixture.h"
#include "Commands/ApplicationCommandTable.h"
#include "UI/Arrangement/ClipComponent.h"
#include "UI/Arrangement/TimelineHeader.h"
#include "UI/Arrangement/TrackLanes.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/State/UIStateStore.h"

namespace resamper::test
{

namespace
{
    /** A mouse event on c at p with mods held, for a gesture that went down at downAt. */
    juce::MouseEvent mouseEventWith (juce::Component& c, juce::Point<int> p, juce::Point<int> downAt, bool dragged,
                                     juce::ModifierKeys mods)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p.toFloat(), mods,
                 juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                 juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                 juce::MouseInputSource::defaultTiltY, &c, &c, now, downAt.toFloat(), now, 1, dragged };
    }

    constexpr auto left = juce::ModifierKeys::leftButtonModifier;

    /** A press, a drag in four steps, and a release, with mods held throughout. */
    void dragAcross (juce::Component& c, juce::Point<int> from, juce::Point<int> to, int mods = 0)
    {
        c.mouseDown (mouseEventWith (c, from, from, false, juce::ModifierKeys (left | mods)));

        for (int step = 1; step <= 4; ++step)
            c.mouseDrag (mouseEventWith (c, from + (to - from) * step / 4, from, true, juce::ModifierKeys (left | mods)));

        c.mouseUp (mouseEventWith (c, to, from, true, juce::ModifierKeys (mods)));
    }

    /** One MIDI clip a bar long at startSeconds on each of numTracks MIDI tracks. */
    void addMidiClips (Fixture& f, int numTracks, double startSeconds)
    {
        for (int i = 0; i < numTracks; ++i)
            f.invoke (cmd::trackAddMidi);

        for (auto& track : f.model.getTracks())
        {
            f.model.selectTrack (track.id);
            f.invoke (cmd::transportSetPosition, startSeconds);
            f.invoke (cmd::clipAddMidi);
        }
    }

    ClipComponent* findClip (TrackLanes& lanes, const juce::String& id)
    {
        for (auto* child : lanes.getChildren())
            if (auto* clip = dynamic_cast<ClipComponent*> (child); clip != nullptr && clip->getClip().id == id)
                return clip;

        return nullptr;
    }

    /** How many pixels in [x1, x2) on row y c paints in a colour other than
        background: one per vertical line crossing that row. */
    int linesOnRow (juce::Component& c, int y, int x1, int x2, juce::Colour background)
    {
        auto image = c.createComponentSnapshot (c.getLocalBounds(), false, 1.0f);
        int n = 0;

        for (int x = x1; x < x2; ++x)
            if (image.getPixelAt (x, y) != background)
                ++n;

        return n;
    }

    bool onBeat (const ApplicationModel& model, double seconds)
    {
        const auto beats = model.secondsToBeats (seconds);
        return std::abs (beats - std::round (beats)) < 1.0e-6;
    }
}

/** Arrangement gestures and grid (#173). */
struct ArrangementGestureTests : juce::UnitTest
{
    ArrangementGestureTests() : juce::UnitTest ("Arrangement Gestures", "Resamper") {}

    void runTest() override
    {
        // 120 BPM in 4/4: a beat is 0.5 s, a bar 2 s. 200 px/s draws a bar 400 px
        // wide, the Arrangement's closest zoom.
        constexpr double zoomedIn = 200.0;

        beginTest ("Zoomed in, the lanes draw a line on every beat, not only on bars (#173 1)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            f.invoke (cmd::trackAddMidi);

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (zoomedIn);
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 200);
            lanes.setTracks (f.model.getTracks());

            // Inside the first track's row, clear of the bar line at x = 0.
            expectGreaterOrEqual (linesOnRow (lanes, 10, 2, 398, f.theme.getTheme().bgSlot), 3);
        }

        beginTest ("Zoomed in, the ruler draws a line on every beat (#173 1)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (zoomedIn);
            TimelineHeader ruler (f.model, f.commands, f.theme, view);
            ruler.setSize (1000, 30);

            // Near the bottom, clear of the bar number after the line at x = 0.
            expectGreaterOrEqual (linesOnRow (ruler, 26, 60, 398, f.theme.getTheme().bgPanel), 3);
        }

        beginTest ("A loop dragged in the ruler starts and ends on the grid (#173 3)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);   // a beat every 20 px
            TimelineHeader ruler (f.model, f.commands, f.theme, view);
            ruler.setSize (1000, 30);

            dragAcross (ruler, { 37, 15 }, { 133, 15 });

            const auto loop = f.model.getLoopRange();
            expectGreaterThan (loop.end, loop.start);
            expect (onBeat (f.model, loop.start), "loop start " + juce::String (loop.start) + " is off the grid");
            expect (onBeat (f.model, loop.end), "loop end " + juce::String (loop.end) + " is off the grid");
        }

        beginTest ("The loop brace is on the ruler before any loop is set, dim, and turning Loop on plays it (#173 3)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);
            TimelineHeader ruler (f.model, f.commands, f.theme, view);
            ruler.setSize (1000, 30);

            const auto shown = f.model.getLoopRange();
            expectEquals (shown.start, 0.0);
            expectWithinAbsoluteError (shown.end, 8.0, 1.0e-6);   // four bars
            expectGreaterThan (linesOnRow (ruler, 4, 100, 300, f.theme.getTheme().bgPanel), 150);

            expect (f.invoke (cmd::transportToggleLoop));
            expect (f.model.isLooping());
            expectWithinAbsoluteError (f.model.getLoopRange().end, 8.0, 1.0e-6);
        }

        beginTest ("Dragging the brace moves the loop and dragging an end resizes it, on the grid, looping left off (#173 3)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            expect (f.model.setLoopRange (2.0, 4.0));

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);   // a beat every 20 px
            TimelineHeader ruler (f.model, f.commands, f.theme, view);
            ruler.setSize (1000, 30);

            // Its body (2..4 s is 80..160 px) by 1.1 s.
            dragAcross (ruler, { 120, 5 }, { 164, 5 });
            expectWithinAbsoluteError (f.model.getLoopRange().start, 3.0, 1.0e-6);
            expectWithinAbsoluteError (f.model.getLoopRange().end, 5.0, 1.0e-6);

            // Its end (now at 200 px) out to 6.1 s.
            dragAcross (ruler, { 200, 5 }, { 244, 5 });
            expectWithinAbsoluteError (f.model.getLoopRange().start, 3.0, 1.0e-6);
            expectWithinAbsoluteError (f.model.getLoopRange().end, 6.0, 1.0e-6);
            expect (! f.model.isLooping());
        }

        beginTest ("With Snap off a clip moves where it is dropped; with it on, to the grid (#173 3)");
        {
            for (const bool snapOn : { true, false })
            {
                Fixture f;
                expect (f.theme.load().wasOk());
                addMidiClips (f, 1, 0.0);

                UIStateStore store;
                ArrangementViewState view (store.getState ("arrangement"));
                view.setPixelsPerSecond (40.0);
                view.setSnapOn (snapOn);
                TrackLanes lanes (f.model, f.commands, f.theme, view);
                lanes.setSize (1000, 400);
                lanes.setTracks (f.model.getTracks());

                const auto id = f.model.getTracks()[0].clips[0].id;
                const auto grab = findClip (lanes, id)->getBounds().getCentre();
                dragAcross (lanes, grab, grab + juce::Point<int> (27, 0));   // 0.675 s

                const auto start = f.model.getTracks()[0].clips[0].startSeconds;
                expectWithinAbsoluteError (start, snapOn ? 0.5 : 0.675, 1.0e-3);
            }
        }

        beginTest ("Alt-dragging a clip leaves the original drawn where it was (#173 4)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            addMidiClips (f, 1, 0.0);

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 400);
            lanes.setTracks (f.model.getTracks());

            const auto clip = f.model.getTracks()[0].clips[0];
            auto* component = findClip (lanes, clip.id);
            expect (component != nullptr);

            if (component != nullptr)
            {
                const auto before = component->getBounds();
                const auto grab = before.getCentre();
                const auto mods = juce::ModifierKeys (left | juce::ModifierKeys::altModifier);

                lanes.mouseDown (mouseEventWith (lanes, grab, grab, false, mods));
                lanes.mouseDrag (mouseEventWith (lanes, grab + juce::Point<int> (120, 0), grab, true, mods));

                expect (component->getBounds() == before, "the original moved during an Alt-drag");

                lanes.mouseUp (mouseEventWith (lanes, grab + juce::Point<int> (120, 0), grab, true, juce::ModifierKeys (juce::ModifierKeys::altModifier)));
                const auto clips = f.model.getTracks()[0].clips;
                expectEquals ((int) clips.size(), 2);
                expectEquals (clips[0].startSeconds, 0.0);
            }
        }

        beginTest ("A drag on empty lane space makes a Time Selection on the grid; Mod bypasses it, Shift extends, a click clears it (#175)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            addMidiClips (f, 2, 2.0);
            f.model.selectClip (f.model.getTracks()[0].clips[0].id);

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);   // a beat every 20 px
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 400);
            lanes.setTracks (f.model.getTracks());
            const auto rowMiddle = view.rowToY (0, lanes.laneHeight()) + lanes.laneHeight() / 2;

            // 0.925 s to 3.325 s, starting left of the clips and ending over one.
            dragAcross (lanes, { 37, rowMiddle }, { 133, rowMiddle });
            auto selection = f.model.getTimeSelection();
            expect (selection.has_value(), "no Time Selection");

            if (selection)
            {
                expect (onBeat (f.model, selection->start) && onBeat (f.model, selection->end), "off the grid");
                expectWithinAbsoluteError (selection->start, 0.925, 0.25);
                expectWithinAbsoluteError (selection->end, 3.325, 0.25);
            }

            expect (f.model.getSelectedClipIds().isEmpty(), "the Clip Selection stayed");

            dragAcross (lanes, { 37, rowMiddle }, { 133, rowMiddle }, juce::ModifierKeys::commandModifier);
            selection = f.model.getTimeSelection();
            expect (selection && std::abs (selection->start - 0.925) < 1.0e-3 && std::abs (selection->end - 3.325) < 1.0e-3,
                    "Mod didn't bypass the grid");

            f.invoke (cmd::timeSelect, { 1.0, 3.5 });
            dragAcross (lanes, { 200, rowMiddle }, { 240, rowMiddle }, juce::ModifierKeys::shiftModifier);
            selection = f.model.getTimeSelection();
            expect (selection && std::abs (selection->start - 1.0) < 1.0e-6 && std::abs (selection->end - 6.0) < 1.0e-6,
                    "Shift didn't extend it to 6 s");

            int clickedRow = -1;
            lanes.onRowClicked = [&] (int row, juce::ModifierKeys) { clickedRow = row; };
            dragAcross (lanes, { 300, rowMiddle }, { 300, rowMiddle });
            expect (! f.model.getTimeSelection().has_value(), "a click on empty lane space kept it");
            expectEquals (clickedRow, 0);
        }

        beginTest ("A drag inside the Time Selection moves its content; Alt copies it under a ghost (#175)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            addMidiClips (f, 2, 2.0);

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 400);
            lanes.setTracks (f.model.getTracks());
            const auto rowMiddle = view.rowToY (0, lanes.laneHeight()) + lanes.laneHeight() / 2;

            // Over the clip, inside the selection: its content moves 2 s later, on both tracks.
            f.invoke (cmd::timeSelect, { 2.0, 4.0 });
            dragAcross (lanes, { 120, rowMiddle }, { 200, rowMiddle });

            for (auto& track : f.model.getTracks())
            {
                expectEquals ((int) track.clips.size(), 1);
                expectWithinAbsoluteError (track.clips[0].startSeconds, 4.0, 1.0e-6);
            }

            expect (f.model.getTimeSelection() && std::abs (f.model.getTimeSelection()->start - 4.0) < 1.0e-6,
                    "the selection didn't follow");

            // Alt: a ghost while dragging, then a copy, the original staying.
            lanes.setTracks (f.model.getTracks());
            const auto background = lanes.createComponentSnapshot (lanes.getLocalBounds(), false, 1.0f);
            const auto alt = juce::ModifierKeys (left | juce::ModifierKeys::altModifier);
            const juce::Point<int> from { 200, rowMiddle }, to { 360, rowMiddle };
            lanes.mouseDown (mouseEventWith (lanes, from, from, false, alt));
            lanes.mouseDrag (mouseEventWith (lanes, to, from, true, alt));

            const auto ghostEdge = juce::roundToInt (view.timeToX (8.0));
            const auto during = lanes.createComponentSnapshot (lanes.getLocalBounds(), false, 1.0f);
            expect (during.getPixelAt (ghostEdge, rowMiddle) != background.getPixelAt (ghostEdge, rowMiddle), "no ghost");

            lanes.mouseUp (mouseEventWith (lanes, to, from, true, juce::ModifierKeys (juce::ModifierKeys::altModifier)));
            const auto clips = f.model.getTracks()[0].clips;
            expectEquals ((int) clips.size(), 2);

            // The Time Selection's menu, on a right-click while it exists.
            juce::PopupMenu::dismissAllActiveMenus();
            const auto rightClick = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);
            lanes.mouseDown (mouseEventWith (lanes, { 700, rowMiddle }, { 700, rowMiddle }, false, rightClick));
            expect (juce::PopupMenu::dismissAllActiveMenus(), "no Time Selection menu");
        }

        beginTest ("The Time Selection's handles in the ruler resize it over the loop brace; a ruler click keeps it (#175)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);
            TimelineHeader ruler (f.model, f.commands, f.theme, view);
            ruler.setSize (1000, 30);

            const auto plain = ruler.createComponentSnapshot (ruler.getLocalBounds(), false, 1.0f);
            f.invoke (cmd::timeSelect, { 2.0, 4.0 });
            const auto highlighted = ruler.createComponentSnapshot (ruler.getLocalBounds(), false, 1.0f);
            expect (highlighted.getPixelAt (100, 22) != plain.getPixelAt (100, 22), "no highlight");

            const auto loop = f.model.getLoopRange();

            // The start handle (x 80..84) out to 3 s, inside the loop brace.
            dragAcross (ruler, { 82, 8 }, { 122, 8 });
            auto selection = f.model.getTimeSelection();
            expect (selection && std::abs (selection->start - 3.0) < 1.0e-6 && std::abs (selection->end - 4.0) < 1.0e-6,
                    "the handle didn't resize it");
            expectEquals (f.model.getLoopRange().start, loop.start);
            expectEquals (f.model.getLoopRange().end, loop.end);

            // Onto the other end it keeps its last width rather than vanishing; past it, it turns round.
            ruler.mouseDown (mouseEventWith (ruler, { 122, 8 }, { 122, 8 }, false, juce::ModifierKeys (left)));
            ruler.mouseDrag (mouseEventWith (ruler, { 160, 8 }, { 122, 8 }, true, juce::ModifierKeys (left)));
            expect (f.model.getTimeSelection().has_value(), "dragged onto its end, it vanished");
            ruler.mouseDrag (mouseEventWith (ruler, { 200, 8 }, { 122, 8 }, true, juce::ModifierKeys (left)));
            ruler.mouseUp (mouseEventWith (ruler, { 200, 8 }, { 122, 8 }, true, juce::ModifierKeys()));
            selection = f.model.getTimeSelection();
            expect (selection && std::abs (selection->start - 4.0) < 1.0e-6 && std::abs (selection->end - 5.0) < 1.0e-6,
                    "dragged past its end, it didn't turn round");

            dragAcross (ruler, { 300, 20 }, { 300, 20 });
            expectWithinAbsoluteError (f.model.getTransportPositionSeconds(), 7.5, 1.0e-6);
            expect (f.model.getTimeSelection().has_value(), "a ruler click cleared it");
        }

        beginTest ("Select All and Insert MIDI Clip at a position act through Commands (#173 6)");
        {
            Fixture f;
            addMidiClips (f, 2, 2.0);
            expect (f.invoke (cmd::clipSelectAll));
            expectEquals (f.model.getSelectedClipIds().size(), 2);

            const auto trackId = f.model.getTracks()[0].id;
            expect (f.invoke (cmd::clipAddMidiAt, { trackId, 6.0 }));
            const auto clips = f.model.getTracks()[0].clips;
            expectEquals ((int) clips.size(), 2);
            expectEquals (clips.back().startSeconds, 6.0);
        }

        beginTest ("Right-clicking empty lane space or the ruler opens a menu (#173 6)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            f.invoke (cmd::trackAddMidi);

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 400);
            lanes.setTracks (f.model.getTracks());
            TimelineHeader ruler (f.model, f.commands, f.theme, view);
            ruler.setSize (1000, 30);

            const auto rightClick = juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier);

            for (juce::Component* c : { (juce::Component*) &lanes, (juce::Component*) &ruler })
            {
                juce::PopupMenu::dismissAllActiveMenus();
                c->mouseDown (mouseEventWith (*c, { 300, 10 }, { 300, 10 }, false, rightClick));
                expect (juce::PopupMenu::dismissAllActiveMenus(), c == &lanes ? "no lane menu" : "no ruler menu");
            }
        }

        beginTest ("The lane menu: the Add Track items with their shortcuts first, then the clip items (#176)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            f.invoke (cmd::trackAddMidi);
            f.invoke (cmd::trackAdd);
            f.model.deselectAll();

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 400);
            lanes.setTracks (f.model.getTracks());

            auto items = [] (const juce::PopupMenu& menu)
            {
                std::vector<juce::PopupMenu::Item> result;

                for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                    result.push_back (it.getItem());

                return result;
            };

            const auto first = f.model.getTracks()[0].id;
            auto menu = items (lanes.createLaneMenu (0, 1.0));
            expectEquals ((int) menu.size(), 9);

            const juce::StringArray labels { "Add MIDI Track", "Add Audio Track", "Add Folder Track", "Add Bus Track",
                                             "Add Return Track", "", "Insert MIDI Clip", "Select All", "Loop Selection" };

            for (size_t i = 0; i < menu.size(); ++i)
                expectEquals (menu[i].text, labels[(int) i]);

            expect (menu[5].isSeparator);
            expect (menu[0].isEnabled && menu[1].isEnabled && menu[4].isEnabled);
            expect (! menu[2].isEnabled && ! menu[3].isEnabled, "Folder and Bus wait on #37 / #38");
            expect (menu[6].isEnabled, "Insert MIDI Clip on a MIDI lane");
            expect (menu[0].image != nullptr && menu[4].image != nullptr);

            expectEquals (menu[0].shortcutKeyDescription, findShortcut (cmd::trackAddMidi.id).getTextDescriptionWithIcons());
            expectEquals (menu[1].shortcutKeyDescription, findShortcut (cmd::trackAdd.id).getTextDescriptionWithIcons());
            expectEquals (menu[4].shortcutKeyDescription, findShortcut (cmd::mixerAddReturn.id).getTextDescriptionWithIcons());

            // Add Audio Track on the first lane inserts after it and selects the new track.
            menu[1].action();
            auto tracks = f.model.getTracks();
            expectEquals ((int) tracks.size(), 3);
            expect (tracks[1].kind == TrackKind::audio && tracks[1].selected);
            expectEquals (tracks[0].id, first);

            // On the audio lane Insert MIDI Clip is off; below the last track nothing is clicked.
            expect (! items (lanes.createLaneMenu (1, 1.0))[6].isEnabled);
            expect (! items (lanes.createLaneMenu (7, 1.0))[6].isEnabled);
        }

        beginTest ("Right-clicking a lane leaves the clip selection and the selected track alone (#176)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            addMidiClips (f, 2, 0.0);
            const auto tracks = f.model.getTracks();
            f.model.selectClip (tracks[1].clips[0].id);
            const auto selectedTrack = f.model.getSelectedTrackId();

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 400);
            lanes.setTracks (f.model.getTracks());

            const auto y = view.rowToY (0, lanes.laneHeight()) + lanes.laneHeight() / 2;
            const auto x = juce::roundToInt (view.timeToX (20.0));
            juce::PopupMenu::dismissAllActiveMenus();
            lanes.mouseDown (mouseEventWith (lanes, { x, y }, { x, y }, false, juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)));
            expect (juce::PopupMenu::dismissAllActiveMenus(), "no lane menu");

            expect (f.model.getSelectedClipIds() == juce::StringArray { tracks[1].clips[0].id });
            expectEquals (f.model.getSelectedTrackId(), selectedTrack);
        }
    }
};

static ArrangementGestureTests arrangementGestureTests;

} // namespace resamper::test
