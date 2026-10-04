#include "TestFixture.h"
#include "Commands/MixerCommands.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper::test
{

struct MixerTests : juce::UnitTest
{
    MixerTests() : juce::UnitTest ("Mixer", "Resamper") {}

    struct MixerFixture : Fixture
    {
        bool trackIsInABus (const juce::String& trackId) const
        {
            for (auto& bus : mixer.getBuses())
                for (auto& child : bus.childTrackIds)
                    if (child == trackId)
                        return true;

            return false;
        }

        /** The Strip for a track, or one with an empty id if it has none. */
        Strip strip (const juce::String& id) const
        {
            for (auto& s : mixer.getStrips())
                if (s.id == id)
                    return s;

            return {};
        }

        juce::String busId (const juce::String& name) const
        {
            for (auto& bus : mixer.getBuses())
                if (bus.name == name)
                    return bus.trackId;

            return {};
        }

        /** Nests any track in a folder through the engine: the Mixer only moves audio tracks. */
        void nest (const juce::String& trackId, const juce::String& folderId)
        {
            auto& edit = projects.getEdit();
            auto* track = tracktion::findTrackForID (edit, tracktion::EditItemID::fromString (trackId));
            auto* folder = tracktion::findTrackForID (edit, tracktion::EditItemID::fromString (folderId));
            auto children = folder->getAllSubTracks (false);
            edit.moveTrack (track, tracktion::TrackInsertPoint (folder, children.isEmpty() ? nullptr : children.getLast()));
        }
    };

    /** An audio track, a return on bus 0, and one send from the audio track. */
    struct SendFixture : MixerFixture
    {
        juce::String sourceId, sendId;

        SendFixture()
        {
            invoke (cmd::trackAdd);
            invoke (cmd::mixerAddReturn);
            sourceId = model.getTracks()[0].id;
            invoke (cmd::mixerAddSend, { sourceId, mixer.getReturns()[0].bus });
            sendId = mixer.getSends (sourceId)[0].id;
        }
    };

    void runTest() override
    {
        beginTest ("addReturn creates a track that getReturns lists on bus 0 and TrackInfo marks as a return; undo removes it");
        {
            MixerFixture f;
            f.invoke (cmd::mixerAddReturn);

            auto returns = f.mixer.getReturns();
            expectEquals ((int) returns.size(), 1);
            expectEquals (returns[0].bus, 0);
            expectEquals (returns[0].name, juce::String ("Return"));
            expectEquals (f.numTracks(), 1);
            expectEquals (f.model.getTracks()[0].id, returns[0].trackId);
            expect (f.model.getTracks()[0].isReturn);

            f.invoke (cmd::editUndo);
            expectEquals ((int) f.mixer.getReturns().size(), 0);
            expectEquals (f.numTracks(), 0);
        }

        beginTest ("addSend lists one send; setSendGain is one undo step and a drag is one step");
        {
            SendFixture f;
            expectEquals ((int) f.mixer.getSends (f.sourceId).size(), 1);
            expectEquals (f.mixer.getSends (f.sourceId)[0].bus, 0);

            f.invoke (cmd::mixerSetSendGain, { f.sourceId, f.sendId, Decibels (-12.0) });
            expectWithinAbsoluteError (f.mixer.getSends (f.sourceId)[0].gain.value, -12.0, 1.0e-2);

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.mixer.getSends (f.sourceId)[0].gain.value, 0.0, 1.0e-2);
            expectEquals ((int) f.mixer.getSends (f.sourceId).size(), 1);

            f.invoke (cmd::mixerSetSendGain, { f.sourceId, f.sendId, Decibels (-1.0) });
            f.invoke (cmd::mixerSetSendGain, { f.sourceId, f.sendId, Decibels (-3.0), true });
            f.invoke (cmd::mixerSetSendGain, { f.sourceId, f.sendId, Decibels (-9.0), true });
            expectWithinAbsoluteError (f.mixer.getSends (f.sourceId)[0].gain.value, -9.0, 1.0e-2);

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.mixer.getSends (f.sourceId)[0].gain.value, 0.0, 1.0e-2);
            expectEquals ((int) f.mixer.getSends (f.sourceId).size(), 1);
        }

        beginTest ("addSend to a bus with no return fails and adds no undo step");
        {
            MixerFixture f;
            f.invoke (cmd::trackAdd);
            const auto trackId = f.model.getTracks()[0].id;
            expect (! f.model.getTracks()[0].isReturn);

            f.invoke (cmd::mixerAddSend, { trackId, 0 });

            expect (! f.errors.isEmpty());
            expect (f.mixer.getSends (trackId).empty());
            expectEquals (f.numTracks(), 1);

            f.invoke (cmd::editUndo);
            expectEquals (f.numTracks(), 0);
            expect (! f.model.canUndo());
        }

        beginTest ("addBus and moveTrackToBus nest the track; undo restores it");
        {
            MixerFixture f;
            f.invoke (cmd::trackAdd);
            const auto trackId = f.model.getTracks()[0].id;

            f.invoke (cmd::mixerAddBus, { "Drums" });
            auto buses = f.mixer.getBuses();
            expectEquals ((int) buses.size(), 1);
            expectEquals (buses[0].name, juce::String ("Drums"));
            expect (buses[0].childTrackIds.empty());

            f.invoke (cmd::mixerMoveToBus, { trackId, buses[0].trackId });
            expect (f.trackIsInABus (trackId));
            expectEquals (f.mixer.getBuses()[0].childTrackIds[0], trackId);

            for (int i = 0; i < 4 && f.trackIsInABus (trackId); ++i)
                f.invoke (cmd::editUndo);

            expect (! f.trackIsInABus (trackId));

            bool stillThere = false;

            for (auto& track : f.model.getTracks())
                stillThere = stillThere || track.id == trackId;

            expect (stillThere);
        }

        beginTest ("getStrips: tracks in Edit order, each Bus after its last child, then Returns; no Strip for a Folder-only Folder");
        {
            MixerFixture f;
            f.invoke (cmd::mixerAddReturn);   // A, first in the Edit

            for (int i = 0; i < 4; ++i)
                f.invoke (cmd::trackAdd);

            f.invoke (cmd::mixerAddReturn);   // B

            const auto tracks = f.model.getTracks();
            const auto returnA = tracks[0].id, t1 = tracks[1].id, t2 = tracks[2].id, t3 = tracks[3].id, t4 = tracks[4].id;
            const auto returnB = tracks[5].id;

            f.invoke (cmd::mixerAddBus, { "Drums" });
            f.invoke (cmd::mixerAddBus, { "Kick" });
            const auto drums = f.busId ("Drums"), kick = f.busId ("Kick");

            // t3, B, Drums { t1, Kick { t2 } }, Folder { t4, A }: B comes before A in the Edit
            f.invoke (cmd::mixerMoveToBus, { t1, drums });
            f.nest (kick, drums);
            f.invoke (cmd::mixerMoveToBus, { t2, kick });

            auto& edit = f.projects.getEdit();
            auto folder = edit.insertNewFolderTrack (tracktion::TrackInsertPoint::getEndOfTracks (edit), nullptr, false);
            f.nest (t4, folder->itemID.toString());
            f.nest (returnA, folder->itemID.toString());

            juce::StringArray order, outputs;

            for (auto& s : f.mixer.getStrips())
            {
                order.add (s.id);
                outputs.add (s.output);
            }

            expectEquals (order.joinIntoString (","),
                          juce::StringArray { t3, t1, t2, kick, drums, t4, returnA, returnB }.joinIntoString (","));
            expectEquals (outputs.joinIntoString (","), juce::String ("Master,Drums,Kick,Drums,Master,Master,Master,Master"));

            expect (f.strip (drums).role == StripRole::bus);
            expect (f.strip (returnA).role == StripRole::returnTrack);
            expectEquals (f.strip (returnA).returnLetter, juce::String ("A"));
            expectEquals (f.strip (returnB).returnLetter, juce::String ("B"));
            expectEquals (f.strip (returnA).number, 0);
            expectEquals (f.strip (t3).number, 1);
            expectEquals (f.strip (t2).number, 3);
            expectEquals (f.strip (t4).number, 4);
            expect (f.strip (folder->itemID.toString()).id.isEmpty());
        }

        beginTest ("A Strip's Output follows moveTrackToBus, and undo puts it back");
        {
            MixerFixture f;
            f.invoke (cmd::trackAdd);
            const auto trackId = f.model.getTracks()[0].id;
            f.invoke (cmd::mixerAddBus, { "Drums" });

            expectEquals (f.strip (trackId).output, juce::String ("Master"));
            f.invoke (cmd::mixerMoveToBus, { trackId, f.busId ("Drums") });
            expectEquals (f.strip (trackId).output, juce::String ("Drums"));

            f.invoke (cmd::editUndo);
            expectEquals (f.strip (trackId).output, juce::String ("Master"));
        }

        beginTest ("A Strip carries its fader, sends and inserts, and follows undo");
        {
            SendFixture f;
            f.invoke (cmd::trackSetVolume, { f.sourceId, Decibels (-6.0), false });

            const auto strip = f.strip (f.sourceId);
            expectEquals (strip.id, f.sourceId);
            expectWithinAbsoluteError (strip.volume.value, -6.0, 1.0e-2);
            expectEquals ((int) strip.sends.size(), 1);
            expectEquals (strip.sends[0].id, f.sendId);
            expect (strip.inserts.empty());

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.strip (f.sourceId).volume.value, 0.0, 1.0e-2);
        }

        beginTest ("The track fader, pan, mute and solo Commands work on a Bus; the fader is one undo step and re-syncs");
        {
            MixerFixture f;
            f.invoke (cmd::mixerAddBus, { "Drums" });
            const auto bus = f.busId ("Drums");

            f.invoke (cmd::trackSetVolume, { bus, Decibels (-6.0), false });
            f.invoke (cmd::trackSetVolume, { bus, Decibels (-9.0), true });
            f.invoke (cmd::trackSetPan, { bus, 0.5, false });
            f.invoke (cmd::trackToggleMute, { bus });
            f.invoke (cmd::trackToggleSolo, { bus });

            auto strip = f.strip (bus);
            expectWithinAbsoluteError (strip.volume.value, -9.0, 1.0e-2);
            expectWithinAbsoluteError (strip.pan, 0.5, 1.0e-3);
            expect (strip.muted);
            expect (strip.solo);

            f.invoke (cmd::editUndo);   // the pan
            f.invoke (cmd::editUndo);   // the whole fader drag
            strip = f.strip (bus);
            expectWithinAbsoluteError (strip.volume.value, 0.0, 1.0e-2);
            expectWithinAbsoluteError (strip.pan, 0.0, 1.0e-3);
            expect (strip.muted);   // mute and solo are never undo steps

            auto* folder = dynamic_cast<tracktion::FolderTrack*> (tracktion::findTrackForID (f.projects.getEdit(), tracktion::EditItemID::fromString (bus)));
            auto* fader = folder->getVolumePlugin();
            expectWithinAbsoluteError (fader->volParam->getCurrentValue(), fader->volume.get(), 1.0e-6f);
        }

        beginTest ("A Bus takes Mixer Inserts and Sends, and its Strip lists them with its colour and child count");
        {
            MixerFixture f;
            f.invoke (cmd::mixerAddReturn);
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            const auto tracks = f.model.getTracks();
            f.invoke (cmd::trackSetColour, { tracks[1].id, 4 });

            f.invoke (cmd::mixerAddBus, { "Drums" });
            const auto bus = f.busId ("Drums");
            f.invoke (cmd::mixerMoveToBus, { tracks[1].id, bus });
            f.invoke (cmd::mixerMoveToBus, { tracks[2].id, bus });

            f.invoke (cmd::pluginInsert, { bus, tracktion::ReverbPlugin::xmlTypeName, PluginChain::mixer });
            f.invoke (cmd::mixerAddSend, { bus, f.mixer.getReturns()[0].bus });
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            const auto strip = f.strip (bus);
            expect (strip.role == StripRole::bus);
            expectEquals ((int) strip.inserts.size(), 1);
            expectEquals (strip.inserts[0].path, juce::String (tracktion::ReverbPlugin::xmlTypeName));
            expect (strip.deviceChain.empty());
            expectEquals ((int) strip.sends.size(), 1);
            expectEquals (strip.colourIndex, 4);   // its first child's
            expectEquals (strip.childCount, 2);
            expectWithinAbsoluteError (f.mixer.getTrackLevel (bus).left, (float) ApplicationModel::minVolume.value, 1.0e-3f);

            // The Send sits before the Bus fader, so the fader doesn't move it.
            auto* folder = tracktion::findTrackForID (f.projects.getEdit(), tracktion::EditItemID::fromString (bus));
            auto& list = folder->pluginList;
            const auto sends = list.getPluginsOfType<tracktion::AuxSendPlugin>();
            const auto faders = list.getPluginsOfType<tracktion::VolumeAndPanPlugin>();
            expect (! sends.isEmpty() && ! faders.isEmpty() && list.indexOf (sends.getFirst()) < list.indexOf (faders.getFirst()));

            // A saved colour wins; an empty Bus takes palette 0.
            folder->state.setProperty ("resamperColour", 2, nullptr);
            expectEquals (f.strip (bus).colourIndex, 2);
            f.invoke (cmd::mixerAddBus, { "Empty" });
            expectEquals (f.strip (f.busId ("Empty")).colourIndex, 0);
        }

        beginTest ("setMasterVolume changes the master only; undo restores it");
        {
            MixerFixture f;
            f.invoke (cmd::trackAdd);
            const auto trackId = f.model.getTracks()[0].id;
            expectWithinAbsoluteError (f.model.getTracks()[0].volume.value, 0.0, 1.0e-3);

            // The engine's Edit starts the master fader at -3 dB, not 0.
            const auto initialMaster = f.mixer.getMaster().volume.value;

            f.invoke (cmd::mixerSetMasterVolume, { Decibels (-6.0) });
            f.invoke (cmd::mixerSetMasterVolume, { Decibels (-9.0), true });
            f.invoke (cmd::mixerSetMasterVolume, { Decibels (-12.0), true });

            expectWithinAbsoluteError (f.mixer.getMaster().volume.value, -12.0, 1.0e-2);
            expectWithinAbsoluteError (f.model.getTracks()[0].volume.value, 0.0, 1.0e-3);

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.mixer.getMaster().volume.value, initialMaster, 1.0e-2);
            expectWithinAbsoluteError (f.model.getTracks()[0].volume.value, 0.0, 1.0e-3);
            expectEquals (f.model.getTracks()[0].id, trackId);
        }

        // Before the fix, the render hangs or crashes on a freed client under MallocScribble=1.
        beginTest ("A Mixer detaches from the meters it read when it is destroyed, a Bus's and a removed track's");
        {
            MixerFixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::mixerAddBus, { "Drums" });
            const auto bus = f.busId ("Drums");
            const auto first = f.model.getTracks()[0].id, last = f.model.getTracks()[1].id;
            const auto tone = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipInsertAt, { tone, first, 0.0 });
            f.invoke (cmd::clipInsertAt, { tone, last, 0.0 });
            f.invoke (cmd::mixerMoveToBus, { first, bus });
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            {
                Mixer other (f.projects, f.model, f.plugins);
                other.getTrackLevel (bus);
                other.getTrackLevel (last);

                // Its meter lives on in the plug-in cache, and undo brings it back.
                f.model.selectTrack (last);
                f.invoke (cmd::trackRemove);
                expect (f.model.getTracks().size() == 1 && f.model.getTracks()[0].id == first);
                other.getTrackLevel (last);
                f.invoke (cmd::editUndo);
                expect (f.model.getTracks().size() == 2);
            }

            // A client left on either meter would be written to after it was freed.
            expectGreaterThan (renderPeak (f), 0.1f);
        }

        beginTest ("getTrackLevel is silence when the track has not played");
        {
            MixerFixture f;
            f.invoke (cmd::trackAdd);
            const auto level = f.mixer.getTrackLevel (f.model.getTracks()[0].id);
            expectWithinAbsoluteError ((double) level.left, ApplicationModel::minVolume.value, 1.0e-3);
            expectWithinAbsoluteError ((double) level.right, ApplicationModel::minVolume.value, 1.0e-3);
            expectWithinAbsoluteError ((double) f.mixer.getMasterLevel().left, ApplicationModel::minVolume.value, 1.0e-3);
        }

        beginTest ("Send mute is one undo step, because the engine records the gain");
        {
            SendFixture f;
            f.invoke (cmd::mixerSetSendMuted, { f.sourceId, f.sendId, true });
            expect (f.mixer.getSends (f.sourceId)[0].muted);

            f.invoke (cmd::editUndo);
            expect (! f.mixer.getSends (f.sourceId)[0].muted);
            expectWithinAbsoluteError (f.mixer.getSends (f.sourceId)[0].gain.value, 0.0, 1.0e-2);
            expectEquals ((int) f.mixer.getSends (f.sourceId).size(), 1);
        }
    }
};

static MixerTests mixerTests;

} // namespace resamper::test
