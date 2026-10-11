#include "TestFixture.h"
#include "UI/Arrangement/ClipComponent.h"
#include "UI/Arrangement/TrackLanes.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/State/UIStateStore.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace resamper::test
{

namespace
{
    /** A MIDI clip, which paints without a waveform to wait for. */
    ClipInfo midiClip (const juce::String& name)
    {
        ClipInfo info;
        info.id = "clip";
        info.name = name;
        info.kind = TrackKind::midi;
        info.lengthSeconds = 4;
        return info;
    }

    /** The clip as painted into a clip-sized image, limited to region when it is
        not empty, as a partial repaint limits it. */
    juce::Image paintClip (ClipComponent& clip, juce::Rectangle<int> region = {})
    {
        juce::Image image (juce::Image::ARGB, clip.getWidth(), clip.getHeight(), true);
        juce::Graphics g (image);

        if (! region.isEmpty())
            g.reduceClipRegion (region);

        clip.paintEntireComponent (g, false);
        return image;
    }

    /** How many pixels in region counts (x, y) is true for. */
    template <typename Predicate>
    int countPixels (juce::Rectangle<int> region, Predicate counts)
    {
        int n = 0;

        for (int y = region.getY(); y < region.getBottom(); ++y)
            for (int x = region.getX(); x < region.getRight(); ++x)
                if (counts (x, y))
                    ++n;

        return n;
    }

    /** How many pixels in region differ between two images. */
    int differingPixels (const juce::Image& a, const juce::Image& b, juce::Rectangle<int> region)
    {
        return countPixels (region, [&] (int x, int y) { return a.getPixelAt (x, y) != b.getPixelAt (x, y); });
    }

    constexpr int clipWidth = 400, clipHeight = 60, lanesWidth = 300;

    /** The lanes' component for a clip, or nullptr. */
    ClipComponent* findClip (TrackLanes& lanes, const juce::String& id)
    {
        for (auto* child : lanes.getChildren())
            if (auto* clip = dynamic_cast<ClipComponent*> (child); clip != nullptr && clip->getClip().id == id)
                return clip;

        return nullptr;
    }

    /** How many pixels of a clip's body a waveform inks: those unlike the fill,
        kept clear of the rounded corners and the selection outline. */
    int waveformInk (ClipComponent& clip, const ThemeManager& theme)
    {
        constexpr int cornerInset = 4;
        const auto image = paintClip (clip);
        const auto body = clip.getLocalBounds().withTrimmedTop (theme.getMetrics().clipHeaderHeight).reduced (cornerInset);
        const auto fill = image.getPixelAt (body.getX(), body.getY());

        return countPixels (body, [&] (int x, int y) { return image.getPixelAt (x, y) != fill; });
    }

    /** More waveformInk than "Preparing audio" alone (about 500): a waveform is drawn. */
    constexpr int minWaveformInk = 2000;
}

/** A clip in the Arrangement, as it paints (PRD §8.1). */
struct ClipViewTests : juce::UnitTest
{
    ClipViewTests() : juce::UnitTest ("Clip View", "Resamper") {}

    void runTest() override
    {
        beginTest ("A partial repaint, as the Playhead's, draws what a full paint draws there (#88)");
        {
            Fixture f;
            expect (f.theme.load().wasOk());

            ClipComponent clip (f.model, f.theme, midiClip ("Clip"));
            clip.setTrackLook (juce::Colours::darkorange, false);

            juce::Component lanes;
            lanes.setSize (lanesWidth, clipHeight);
            lanes.addAndMakeVisible (clip);

            // On screen from its start, then starting off-screen.
            for (const int offScreen : { 0, 200 })
            {
                clip.setBounds (-offScreen, 0, clipWidth, clipHeight);
                const juce::Rectangle<int> playheadStrip (offScreen + 100, 0, 20, clipHeight);
                expectEquals (differingPixels (paintClip (clip), paintClip (clip, playheadStrip), playheadStrip), 0);
            }
        }

        beginTest ("A clip that starts off-screen keeps its title at the visible edge");
        {
            Fixture f;
            expect (f.theme.load().wasOk());

            ClipComponent unnamed (f.model, f.theme, midiClip ({}));
            ClipComponent named (f.model, f.theme, midiClip ("Clip"));

            juce::Component lanes;
            lanes.setSize (lanesWidth, clipHeight);
            constexpr int offScreen = 200;

            for (auto* clip : { &unnamed, &named })
            {
                clip->setTrackLook (juce::Colours::darkorange, false);
                lanes.addAndMakeVisible (*clip);
                clip->setBounds (-offScreen, 0, clipWidth, clipHeight);
            }

            const juce::Rectangle<int> visibleStart (offScreen, 0, 40, f.theme.getMetrics().clipHeaderHeight);
            expectGreaterThan (differingPixels (paintClip (unnamed), paintClip (named), visibleStart), 0);
        }

        // Unwarped, and warped (an ACID loop): both halves of a split, and a moved
        // clip, play the clip's own file, so their waveforms come from the cache.
        for (const bool warped : { false, true })
        {
            for (const bool split : { true, false })
            {
                beginTest (juce::String (split ? "Splitting" : "Moving") + " an audio clip draws its waveform in the first paint (#89)"
                           + (warped ? ", warped" : ""));

                Fixture f;
                expect (f.theme.load().wasOk());
                f.invoke (cmd::trackAdd);
                f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 10.0, 2, warped ? 100.0 : 0.0);
                f.invoke (cmd::clipAdd);

                UIStateStore store;
                ArrangementViewState view (store.getState ("arrangement"));
                view.setPixelsPerSecond (40.0);
                TrackLanes lanes (f.model, f.commands, f.theme, view);
                lanes.setSize (1000, 200);
                lanes.setTracks (f.model.getTracks());

                // Its waveform drawn from the file it plays, every peak read. A
                // thumbnail yet to attach its reader reports fully loaded too.
                const auto clip = f.model.getTracks()[0].clips[0];
                auto& engine = f.projects.getEdit().engine;
                expect (dispatchUntil ([&] { return clip.playbackFile.existsAsFile()
                                                 && te::SmartThumbnail::areThumbnailsFullyLoaded (engine)
                                                 && waveformInk (*findClip (lanes, clip.id), f.theme) > minWaveformInk; }));

                if (split)
                {
                    f.model.selectClip (clip.id);
                    f.invoke (cmd::transportSetPosition, 5.0);
                    expect (f.invoke (cmd::clipSplit));
                }
                else
                {
                    expect (f.invoke (cmd::clipMove, { clip.id, 12.0 }));
                }

                lanes.setTracks (f.model.getTracks());

                // No messages dispatched: this is the paint that follows.
                const auto clips = f.model.getTracks()[0].clips;
                expectEquals ((int) clips.size(), split ? 2 : 1);

                // Playing the same file, every waveform has its peaks whole from the
                // cache rather than reading them again.
                expect (te::SmartThumbnail::areThumbnailsFullyLoaded (engine), "a waveform is reading its file again");

                for (auto& c : clips)
                    expectGreaterThan (waveformInk (*findClip (lanes, c.id), f.theme), minWaveformInk,
                                       c.id == clip.id ? "the kept clip" : "the new clip");
            }
        }

        // The drag previews the clip where it would land, then the Command moves it:
        // neither makes a new waveform, warped or not (#90).
        for (const bool warped : { false, true })
        {
            for (const bool toOtherTrack : { false, true })
            {
                beginTest (juce::String ("Dragging an audio clip") + (toOtherTrack ? " to another track" : "")
                           + " keeps its waveform (#90)" + (warped ? ", warped" : ""));

                Fixture f;
                expect (f.theme.load().wasOk());
                f.invoke (cmd::trackAdd);
                f.invoke (cmd::trackAdd);
                f.model.selectTrack (f.model.getTracks()[0].id);
                f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 10.0, 2, warped ? 100.0 : 0.0);
                f.invoke (cmd::clipAdd);

                UIStateStore store;
                ArrangementViewState view (store.getState ("arrangement"));
                view.setPixelsPerSecond (40.0);
                TrackLanes lanes (f.model, f.commands, f.theme, view);
                lanes.setSize (1000, 400);
                lanes.setTracks (f.model.getTracks());

                const auto clip = f.model.getTracks()[0].clips[0];
                auto& engine = f.projects.getEdit().engine;
                auto* component = findClip (lanes, clip.id);
                expect (component != nullptr);

                if (component == nullptr)
                    continue;

                expect (dispatchUntil ([&] { return clip.playbackFile.existsAsFile()
                                                 && te::SmartThumbnail::areThumbnailsFullyLoaded (engine)
                                                 && waveformInk (*component, f.theme) > minWaveformInk; }));

                const auto* waveform = component->getWaveform();
                const auto grab = component->getBounds().getCentre();
                const auto drop = grab + juce::Point<int> (120, toOtherTrack ? lanes.laneHeight() : 0);

                lanes.mouseDown (mouseEvent (lanes, grab, grab, false));

                for (int step = 1; step <= 4; ++step)
                {
                    lanes.mouseDrag (mouseEvent (lanes, grab + (drop - grab) * step / 4, grab, true));
                    expect (component->getWaveform() == waveform, "the drag preview made a new waveform");
                }

                lanes.mouseUp (mouseEvent (lanes, drop, grab, true));
                lanes.setTracks (f.model.getTracks());

                // No messages dispatched: this is the paint that follows.
                const auto moved = f.model.getTracks()[toOtherTrack ? 1 : 0].clips;
                expectEquals ((int) moved.size(), 1);
                expect (moved[0].id == clip.id);
                expectGreaterThan (moved[0].startSeconds, clip.startSeconds);
                expect (moved[0].playbackFile == clip.playbackFile, "the moved clip plays a new file");

                expect (findClip (lanes, clip.id) == component, "the moved clip has a new component");
                expect (component->getWaveform() == waveform, "the moved clip has a new waveform");

                // Nothing reads the clip's file again.
                expect (te::SmartThumbnail::areThumbnailsFullyLoaded (engine), "a waveform is reading its file again");
                expectGreaterThan (waveformInk (*component, f.theme), minWaveformInk);
            }
        }
    }
};

static ClipViewTests clipViewTests;

} // namespace resamper::test
