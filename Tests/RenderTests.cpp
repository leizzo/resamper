#include "TestFixture.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper::test
{

/** End-to-end audio proof without a device: Commands build the Edit, the
    engine renders it offline, and the result must be audible. */
struct RenderTests : juce::UnitTest
{
    RenderTests() : juce::UnitTest ("Offline Render", "Resamper") {}

    void runTest() override
    {
        beginTest ("An Edit with a WAV clip on an audio track renders non-silent audio");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipAdd);
            expectGreaterThan (renderPeak (f), 0.1f);
        }

        beginTest ("Channel controls reach the audio: volume, mute and solo");
        {
            // A tone on track 0; track 1 stays empty.
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.model.selectTrack (f.model.getTracks()[0].id);
            f.invoke (cmd::clipAdd);
            const auto tone = f.model.getTracks()[0].id, empty = f.model.getTracks()[1].id;

            const auto fullPeak = renderPeak (f);
            f.invoke (cmd::trackSetVolume, { tone, Decibels (-12.0) });
            expectWithinAbsoluteError (renderPeak (f), fullPeak * juce::Decibels::decibelsToGain (-12.0f), fullPeak * 0.05f);

            f.invoke (cmd::trackSetVolume, { tone, ApplicationModel::minVolume });
            expectLessThan (renderPeak (f), 1e-4f);
            f.invoke (cmd::editUndo);
            f.invoke (cmd::editUndo);

            f.invoke (cmd::trackToggleMute, { tone });
            expectLessThan (renderPeak (f), 1e-4f);
            f.invoke (cmd::trackToggleMute, { tone });

            f.invoke (cmd::trackToggleSolo, { empty });
            expectLessThan (renderPeak (f), 1e-4f);
            f.invoke (cmd::trackToggleSolo, { tone });
            expectGreaterThan (renderPeak (f), 0.1f);
        }

        beginTest ("A warped (ACID) loop renders non-silent from its own file, with no proxy (#111)");
        {
            // Warped in real time: nothing to wait for, before or after a trim.
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("loop.wav"), 2.0, 2, 172.0);
            f.invoke (cmd::clipAdd);
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            auto* clip = dynamic_cast<tracktion::WaveAudioClip*> (tracktion::getAudioTracks (f.projects.getEdit())[0]->getClips()[0]);
            expect (clip != nullptr && clip->getAutoTempo());
            expect (clip != nullptr && ! clip->canUseProxy());
            expect (f.model.getTracks()[0].clips[0].playbackFile == f.audioFileToChoose);
            expectGreaterThan (renderPeak (f), 0.1f);

            const auto added = f.model.getTracks()[0].clips[0];
            f.invoke (cmd::clipResize, { added.id, added.startSeconds + 0.25, added.startSeconds + added.lengthSeconds });
            expect (f.model.getTracks()[0].clips[0].playbackFile == f.audioFileToChoose);
            expectGreaterThan (renderPeak (f), 0.1f);
        }
    }
};

static RenderTests renderTests;

} // namespace resamper::test
