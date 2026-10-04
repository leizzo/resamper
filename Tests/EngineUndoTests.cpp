#include "TestFixture.h"
#include "Commands/EditCommands.h"
#include "Commands/MixerCommands.h"
#include "Commands/PluginCommands.h"
#include "Commands/TrackCommands.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace resamper::test
{

/** Engine Undo across facades: a gesture (a drag) is one undo step, and it
    never joins a step another facade started in the middle of it. */
struct EngineUndoTests : juce::UnitTest
{
    EngineUndoTests() : juce::UnitTest ("Engine Undo", "Resamper") {}

    /** One track with a reverb on its device chain. */
    struct UndoFixture : Fixture
    {
        juce::String trackId, pluginId;

        UndoFixture()
        {
            invoke (cmd::trackAdd);
            trackId = model.getTracks()[0].id;
            invoke (cmd::pluginInsert, { trackId, te::ReverbPlugin::xmlTypeName });
            pluginId = plugins.getChain (trackId, PluginChain::device)[0].id;
        }

        /** The reverb's first parameter, a fraction of the way through its range. */
        float parameterAt (float fraction) const
        {
            const auto param = plugins.getParameters (pluginId)[0];
            return param.minimum + (param.maximum - param.minimum) * fraction;
        }

        bool bypassed() const   { return ! plugins.getChain (trackId, PluginChain::device)[0].enabled; }
    };

    void runTest() override
    {
        beginTest ("An undo straight after a new track keeps Redo: the engine's track sort is no step (#133)");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            const auto trackId = f.model.getTracks()[0].id;
            f.invoke (cmd::pluginInsert, { trackId, te::ReverbPlugin::xmlTypeName });

            auto& undo = f.projects.getEdit().getUndoManager();
            const auto history = undo.getUndoDescriptions();

            // The engine sorts the Edit's tracks after a new one, asynchronously: here, after the undo.
            f.invoke (cmd::editUndo);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            expect (undo.canRedo(), "a step after the undo cleared Redo: " + undo.getUndoDescriptions().joinIntoString (" | "));

            f.invoke (cmd::editRedo);
            expect (undo.getUndoDescriptions() == history, undo.getUndoDescriptions().joinIntoString (" | "));
        }

        beginTest ("A track volume drag never joins a plug-in step made during it");
        {
            UndoFixture f;
            f.invoke (cmd::trackSetVolume, { f.trackId, Decibels (-3.0) });
            f.invoke (cmd::pluginSetBypassed, { f.trackId, f.pluginId, true });
            f.invoke (cmd::trackSetVolume, { f.trackId, Decibels (-6.0), true });

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.model.getTracks()[0].volume.value, -3.0, 1e-3);
            expect (f.bypassed(), "the bypass step survives");
        }

        beginTest ("A plug-in parameter drag never joins a track step made during it");
        {
            UndoFixture f;
            const auto param = f.plugins.getParameters (f.pluginId)[0];
            const auto low = f.parameterAt (0.6f), high = f.parameterAt (0.9f);

            f.invoke (cmd::pluginSetParameter, { f.pluginId, param.id, low });
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::pluginSetParameter, { f.pluginId, param.id, high, true });

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.plugins.getParameters (f.pluginId)[0].value, low, 1.0e-4f);
            expectEquals (f.numTracks(), 2, "the track step survives");
        }

        beginTest ("A master volume drag never joins a plug-in step made during it");
        {
            UndoFixture f;
            f.invoke (cmd::mixerSetMasterVolume, { Decibels (-3.0) });
            f.invoke (cmd::pluginSetBypassed, { f.trackId, f.pluginId, true });
            f.invoke (cmd::mixerSetMasterVolume, { Decibels (-6.0), true });

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.mixer.getMaster().volume.value, -3.0, 1e-3);
            expect (f.bypassed(), "the bypass step survives");
        }

        beginTest ("A drag that continues after an undo is its own step");
        {
            UndoFixture f;
            const auto param = f.plugins.getParameters (f.pluginId)[0];
            const auto low = f.parameterAt (0.6f), high = f.parameterAt (0.9f);

            f.invoke (cmd::pluginSetBypassed, { f.trackId, f.pluginId, true });
            f.invoke (cmd::pluginSetParameter, { f.pluginId, param.id, low });
            f.invoke (cmd::editUndo);
            f.invoke (cmd::pluginSetParameter, { f.pluginId, param.id, high, true });

            f.invoke (cmd::editUndo);
            expectWithinAbsoluteError (f.plugins.getParameters (f.pluginId)[0].value, param.value, 1.0e-4f);
            expect (f.bypassed(), "the bypass step survives");
        }
    }
};

static EngineUndoTests engineUndoTests;

} // namespace resamper::test
