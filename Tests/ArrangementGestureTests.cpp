#include "TestFixture.h"
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

        beginTest ("Dragging on empty lane space selects every clip the rectangle touches, across tracks (#173 5)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            addMidiClips (f, 2, 2.0);
            f.invoke (cmd::editDeselectAll);

            UIStateStore store;
            ArrangementViewState view (store.getState ("arrangement"));
            view.setPixelsPerSecond (40.0);
            TrackLanes lanes (f.model, f.commands, f.theme, view);
            lanes.setSize (1000, 400);
            lanes.setTracks (f.model.getTracks());

            // From empty space left of the first clip to the middle of the second track's clip.
            const auto secondRowMiddle = view.rowToY (1, lanes.laneHeight()) + lanes.laneHeight() / 2;
            dragAcross (lanes, { 20, 10 }, { (int) view.timeToX (2.5), secondRowMiddle });

            expectEquals (f.model.getSelectedClipIds().size(), 2);

            // Shift adds the clips the rectangle touches; Mod toggles them.
            const auto firstId = f.model.getTracks()[0].clips[0].id;
            const auto firstMiddle = view.rowToY (0, lanes.laneHeight()) + lanes.laneHeight() / 2;
            f.model.selectClip (firstId);
            dragAcross (lanes, { 20, secondRowMiddle }, { (int) view.timeToX (2.5), secondRowMiddle + 2 },
                        juce::ModifierKeys::shiftModifier);
            expectEquals (f.model.getSelectedClipIds().size(), 2);

            dragAcross (lanes, { 20, firstMiddle }, { (int) view.timeToX (2.5), firstMiddle + 2 },
                        juce::ModifierKeys::commandModifier);
            expect (f.model.getSelectedClipIds() == juce::StringArray (f.model.getTracks()[1].clips[0].id));

            // A click on empty space still reports its row (the view selects that track).
            int clickedRow = -1;
            lanes.onRowClicked = [&] (int row, juce::ModifierKeys) { clickedRow = row; };
            dragAcross (lanes, { 20, firstMiddle }, { 20, firstMiddle });
            expectEquals (clickedRow, 0);
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
    }
};

static ArrangementGestureTests arrangementGestureTests;

} // namespace resamper::test
