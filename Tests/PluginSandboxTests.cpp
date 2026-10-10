#include "ComponentSearch.h"
#include "FakePlugin.h"
#include "HostedAudio.h"
#include "TestFixture.h"
#include "TestPluginFormat.h"
#include "Commands/ClipCommands.h"
#include "Commands/EditCommands.h"
#include "Commands/PluginCommands.h"
#include "Commands/ProductionCommands.h"
#include "Commands/ProjectCommands.h"
#include "Commands/TrackCommands.h"
#include "Engine/PluginHosting.h"
#include "Engine/PluginSandbox.h"
#include "Engine/SandboxDock.h"
#include "UI/MainWindow/MainComponent.h"

#include <tracktion_engine/tracktion_engine.h>

#if JUCE_MAC
 #include <objc/message.h>
#endif

namespace te = tracktion;

namespace resamper::test
{

/** PRD §19 / §9.6 (#69): plug-ins run out of process, in their sandbox; a
    crash bypasses that plug-in only, and Reload brings it back. */
struct PluginSandboxTests : juce::UnitTest
{
    PluginSandboxTests() : juce::UnitTest ("Plug-in Sandbox", "Resamper") {}

    /** A test plug-in file the engine has scanned, while alive (see TestPluginFormat). */
    struct TestPlugin
    {
        TestPlugin (Fixture& f, const juce::String& fileName, const juce::String& text)
            : known (f.projects.getEdit().engine.getPluginManager().knownPluginList)
        {
            TestPluginFormat::registerWith (f.projects.getEdit().engine.getPluginManager().pluginFormatManager);
            f.app.engine.getPluginHosting().addHostedFormat (TestPluginFormat::formatName);

            file = folder().getChildFile (fileName + TestPluginFormat::fileExtension);
            file.replaceWithText (text);

            TestPluginFormat format;
            juce::OwnedArray<juce::PluginDescription> found;
            format.findAllTypesForFile (found, file.getFullPathName());

            if (! found.isEmpty())
            {
                desc = *found.getFirst();
                known.addType (desc);
            }
        }

        ~TestPlugin()
        {
            known.removeType (desc);
            file.deleteFile();
        }

        juce::String path() const   { return desc.fileOrIdentifier; }

        /** Not the folder the test format scans: a scan never finds these. */
        static juce::File folder()
        {
            static const auto dir = []
            {
                auto d = juce::File::createTempFile ("resamper-sandbox-plugins");
                d.createDirectory();
                return d;
            }();

            return dir;
        }

        juce::KnownPluginList& known;
        juce::File file;
        juce::PluginDescription desc;
    };

    static juce::String nameOf (HostingState::Kind kind)
    {
        switch (kind)
        {
            case HostingState::Kind::loading:     return "loading";
            case HostingState::Kind::sandboxed:   return "sandboxed";
            case HostingState::Kind::inProcess:   return "in-process";
            case HostingState::Kind::crashed:     return "crashed";
            case HostingState::Kind::failed:      return "failed";
            case HostingState::Kind::missing:     return "missing";
        }

        return {};
    }

    /** Hears Plug-in Hosting: every plug-in's Hosting States, in the order they were pushed. */
    struct Transitions : PluginHosting::Listener
    {
        explicit Transitions (Fixture& f) : hosting (f.app.engine.getPluginHosting())   { hosting.addListener (this); }
        ~Transitions() override                                                         { hosting.removeListener (this); }

        void hostingStateChanged (const juce::String& pluginId, const HostingState& state) override
        {
            heard.push_back ({ pluginId, nameOf (state.kind) });
        }

        /** The plug-in's states heard since last asked, comma-separated. */
        juce::String take (const juce::String& pluginId)
        {
            juce::StringArray kinds;

            for (auto it = heard.begin(); it != heard.end();)
            {
                if (it->first == pluginId)
                {
                    kinds.add (it->second);
                    it = heard.erase (it);
                }
                else
                {
                    ++it;
                }
            }

            return kinds.joinIntoString (", ");
        }

        PluginHosting& hosting;
        std::vector<std::pair<juce::String, juce::String>> heard;
    };

    /** An AUv3 stand-in: a format the engine creates asynchronously (it needs the message
        thread free), in-process. A ".asynctest" file holds FakePlugin, created after
        createMs; one whose name has "fail" in it can't be created. */
    struct AsyncFormat : juce::AudioPluginFormat
    {
        static constexpr const char* extension = ".asynctest";
        static constexpr int createMs = 100;

        juce::String getName() const override   { return "AsyncTest"; }

        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& path) override
        {
            if (! fileMightContainThisPluginType (path))
                return;

            auto d = FakePlugin::description();
            d.pluginFormatName = getName();
            d.fileOrIdentifier = path;
            d.name = juce::File (path).getFileNameWithoutExtension();
            results.add (new juce::PluginDescription (d));
        }

        bool fileMightContainThisPluginType (const juce::String& path) override   { return path.endsWith (extension); }
        juce::String getNameOfPluginFromIdentifier (const juce::String& path) override { return juce::File (path).getFileNameWithoutExtension(); }
        bool pluginNeedsRescanning (const juce::PluginDescription&) override      { return false; }
        bool doesPluginStillExist (const juce::PluginDescription&) override       { return true; }
        bool canScanForPlugins() const override                                   { return false; }
        bool isTrivialToScan() const override                                     { return true; }
        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
        juce::FileSearchPath getDefaultLocationsToSearch() override               { return {}; }
        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return true; }

        void createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback callback) override
        {
            juce::Timer::callAfterDelay (createMs, [fails = d.fileOrIdentifier.contains ("fail"), done = std::move (callback)]
            {
                if (fails)
                    done (nullptr, "It needs a licence");
                else
                    done (std::make_unique<FakePlugin>(), {});
            });
        }

        /** Registers the format with the engine once per run (a format can't be removed). */
        static void registerWith (te::PluginManager& manager)
        {
            static bool registered = false;

            if (! std::exchange (registered, true))
                manager.pluginFormatManager.addFormat (std::make_unique<AsyncFormat>());
        }
    };

    /** While alive, the engine knows an AsyncFormat plug-in called name. */
    struct AsyncPlugin
    {
        AsyncPlugin (Fixture& f, const juce::String& name)
            : known (f.projects.getEdit().engine.getPluginManager().knownPluginList)
        {
            AsyncFormat::registerWith (f.projects.getEdit().engine.getPluginManager());
            juce::OwnedArray<juce::PluginDescription> found;
            AsyncFormat().findAllTypesForFile (found, "/Library/Audio/Plug-Ins/Components/" + name + AsyncFormat::extension);
            if (auto* d = found.getFirst())
            {
                desc = *d;
                known.addType (desc);
            }
        }

        ~AsyncPlugin()
        {
            known.removeType (desc);

            // The engine saves the list to the test run's settings when it hears of the change:
            // now, or a later run would know a plug-in whose format it hasn't got.
            known.dispatchPendingMessages();
        }

        juce::KnownPluginList& known;
        juce::PluginDescription desc;
    };

    static HostingState state (Fixture& f, const juce::String& pluginId)
    {
        return f.app.engine.getPluginHosting().getState (pluginId);
    }

    static bool is (Fixture& f, const juce::String& pluginId, HostingState::Kind kind)
    {
        return state (f, pluginId).kind == kind;
    }

    static juce::String addTrack (Fixture& f)
    {
        f.invoke (cmd::trackAdd);
        return f.model.getTracks().back().id;
    }

    /** Until the plug-in is no longer Loading (into its sandbox, in the background, say). */
    static bool loaded (Fixture& f, const juce::String& pluginId)
    {
        return dispatchUntil ([&] { return ! is (f, pluginId, HostingState::Kind::loading); });
    }

    /** Inserts the plug-in, and waits until it has loaded. */
    static juce::String insert (Fixture& f, const juce::String& trackId, const juce::String& path)
    {
        f.invoke (cmd::pluginInsert, { trackId, path, PluginChain::device });
        const auto chain = f.plugins.getChain (trackId, PluginChain::device);
        const auto id = chain.empty() ? juce::String() : chain.back().id;
        loaded (f, id);
        return id;
    }

    static juce::AudioPluginInstance* instanceOf (Fixture& f, const juce::String& pluginId)
    {
        for (auto* plugin : te::getAllPlugins (f.projects.getEdit(), false))
            if (plugin->itemID.toString() == pluginId)
                if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin))
                    return external->getAudioPluginInstance();

        return nullptr;
    }

    static juce::String parameterId (Fixture& f, const juce::String& pluginId, const juce::String& name)
    {
        for (auto& p : f.plugins.getParameters (pluginId))
            if (p.name == name)
                return p.id;

        return {};
    }

    static float parameterValue (Fixture& f, const juce::String& pluginId, const juce::String& name)
    {
        for (auto& p : f.plugins.getParameters (pluginId))
            if (p.name == name)
                return p.value;

        return -1.0f;
    }

    static void setParameter (Fixture& f, const juce::String& pluginId, const juce::String& name, float value)
    {
        f.invoke (cmd::pluginSetParameter, { pluginId, parameterId (f, pluginId, name), value });
    }

    /** One block of ones through the instance, as the audio thread runs it; the last output sample. */
    static float processOnes (juce::AudioPluginInstance& instance, double* elapsedMs = nullptr)
    {
        constexpr int samples = 512;
        juce::AudioBuffer<float> buffer (2, samples);

        for (int c = 0; c < buffer.getNumChannels(); ++c)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (c), 1.0f, samples);

        juce::MidiBuffer midi;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        instance.processBlock (buffer, midi);

        if (elapsedMs != nullptr)
            *elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;

        return buffer.getSample (1, samples - 1);
    }

    static void prepare (juce::AudioPluginInstance& instance)
    {
        instance.setRateAndBufferSizeDetails (44100.0, 512);
        instance.prepareToPlay (44100.0, 512);
    }

    static std::optional<PluginInfo> info (Fixture& f, const juce::String& pluginId)
    {
        return f.plugins.getPlugin (pluginId);
    }

#if JUCE_MAC
    static juce::Button* findButton (juce::Component& root, const juce::String& name)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* button = dynamic_cast<juce::Button*> (child); button != nullptr && button->getName() == name)
                return button;

            if (auto* found = findButton (*child, name))
                return found;
        }

        return nullptr;
    }

    /** What AppKit does to a window clicked while key: brings it to the front of its
        level, over another process's panel, and tells JUCE nothing. */
    static void orderFrontNatively (juce::Component& c)
    {
        if (auto* peer = c.getTopLevelComponent()->getPeer())
        {
            auto* view = (id) peer->getNativeHandle();
            auto window = ((id (*) (id, SEL)) objc_msgSend) (view, sel_registerName ("window"));
            ((void (*) (id, SEL, id)) objc_msgSend) (window, sel_registerName ("orderFront:"), nil);
        }

        // The window server reorders a little later.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    }

    /** The open preset menu: a temporary modal window, not a tooltip or a toast. */
    static juce::Component* findPresetMenu()
    {
        auto& desktop = juce::Desktop::getInstance();

        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* c = desktop.getComponent (i);

            if (c == nullptr || ! c->isShowing() || c->getPeer() == nullptr)
                continue;

            if ((c->getPeer()->getStyleFlags() & juce::ComponentPeer::windowIsTemporary) == 0 || ! c->isCurrentlyModal())
                continue;

            if (dynamic_cast<juce::TooltipWindow*> (c) != nullptr || dynamic_cast<Toasts*> (c) != nullptr)
                continue;

            return c;
        }

        return nullptr;
    }
#endif

    void runTest() override
    {
        beginTest ("A plug-in runs in its sandbox by default: its audio, parameters and latency go through it");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Gain", "plugin Sandbox Gain");
            Transitions heard (f);
            const auto track = addTrack (f);
            const auto id = insert (f, track, gain.path());

            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expectEquals (heard.take (id), juce::String ("loading, sandboxed"), "insert");
            auto plugin = info (f, id);

            auto* instance = instanceOf (f, id);
            expect (instance != nullptr && PluginSandbox::isSandboxed (instance),
                    "id " + id + ", reason: " + state (f, id).reason + ", instance " + juce::String (instance != nullptr ? 1 : 0));

            if (instance == nullptr)
                return;

            expectEquals (instance->getLatencySamples(), TestPluginFormat::pluginLatency);
            prepare (*instance);
            expectWithinAbsoluteError (processOnes (*instance), 0.5f, 1.0e-6f);

            setParameter (f, id, "Gain", 0.25f);
            expectWithinAbsoluteError (processOnes (*instance), 0.25f, 1.0e-6f);
            expectWithinAbsoluteError (parameterValue (f, id, "Gain"), 0.25f, 1.0e-6f);

            // The engine compensates the latency the sandbox reports, as for any plug-in.
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipInsertAt, { f.audioFileToChoose, track, 0.0 });
            expectGreaterThan (renderPeak (f), 0.05f);
            plugin = info (f, id);
            expect (plugin.has_value() && plugin->latencySamples > 0,
                    "latency " + juce::String (plugin.has_value() ? plugin->latencySamples : -1));
        }

        beginTest ("A plug-in added to a Device Chain plays: live, sandboxed or in-process, the track's audio goes through it (#168)");
        {
            HostedAudio device;
            Fixture f;
            TestPlugin gain (f, "Sandbox Live Gain", "plugin Live Gain");
            const auto track = addTrack (f);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 2.0);
            f.invoke (cmd::clipInsertAt, { f.audioFileToChoose, track, 0.0 });

            // The level the device plays once the transport has run a while.
            auto playedPeak = [&]
            {
                f.projects.getEdit().dispatchPendingUpdatesSynchronously();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
                f.invoke (cmd::transportPlay);
                device.process (0.3);
                const auto peak = device.process (0.2);
                f.invoke (cmd::transportStop);
                return peak;
            };

            const auto dry = playedPeak();
            expectGreaterThan (dry, 0.1f, "the track doesn't play");

            const auto id = insert (f, track, gain.path());
            setParameter (f, id, "Gain", 0.25f);
            expect (is (f, id, HostingState::Kind::sandboxed));
            expectWithinAbsoluteError (playedPeak(), dry * 0.25f, dry * 0.05f, "sandboxed");

            f.invoke (cmd::pluginSetBypassed, { track, id, true });
            expectWithinAbsoluteError (playedPeak(), dry, dry * 0.05f, "bypassed");
            f.invoke (cmd::pluginSetBypassed, { track, id, false });

            f.invoke (cmd::pluginSetRunInProcess, { id, true });
            expect (loaded (f, id) && is (f, id, HostingState::Kind::inProcess));
            expectWithinAbsoluteError (playedPeak(), dry * 0.25f, dry * 0.05f, "in-process");
        }

        beginTest ("A slow plug-in loads in the background: the insert returns, the window shows loading, then the plug-in, never Failed");
        {
            Fixture f;
            TestPlugin sluggish (f, "Sandbox Sluggish", "plugin Sluggish Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);

            const auto started = juce::Time::getMillisecondCounterHiRes();
            f.invoke (cmd::pluginInsert, { track, sluggish.path(), PluginChain::device });
            const auto took = juce::Time::getMillisecondCounterHiRes() - started;
            expectLessThan (took, TestPluginFormat::sluggishLoadMs / 2.0, "the insert waited for the plug-in to load");

            const auto chain = f.plugins.getChain (track, PluginChain::device);
            const auto id = chain.empty() ? juce::String() : chain.back().id;
            auto* window = main->getPluginWindows().getWindow (id);
            expect (window != nullptr && window->isVisible(), "the window isn't up at once");
            expect (is (f, id, HostingState::Kind::loading), "it isn't loading");
            expect (window != nullptr && window->getStatus() == PluginWindow::Status::loading);

            // The message thread stays free while it loads.
            int ticks = 0;
            juce::Timer::callAfterDelay (50, [&ticks] { ++ticks; });
            expect (dispatchUntil ([&] { return ticks > 0; }) && is (f, id, HostingState::Kind::loading),
                    "the message thread was held up");

            // The window has no load timeout of its own. Its old one (shortened to a second, say)
            // showed Failed here, and never picked up the load that finished after it.
            bool showedFailed = false;
            expect (window != nullptr && dispatchUntil ([&]
            {
                showedFailed = showedFailed || window->getStatus() == PluginWindow::Status::failed;
                return window->getStatus() == PluginWindow::Status::ready;
            }), "it never loaded");
            expect (! showedFailed, "the window showed Failed while the plug-in loaded");
            expectGreaterThan (juce::Time::getMillisecondCounterHiRes() - started, 1000.0, "the load wasn't slow");
            expect (is (f, id, HostingState::Kind::sandboxed));
            auto* instance = instanceOf (f, id);
            expect (instance != nullptr && PluginSandbox::isSandboxed (instance));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));

            if (instance != nullptr)
            {
                prepare (*instance);
                expectWithinAbsoluteError (processOnes (*instance), 0.5f, 1.0e-6f);
            }

            main.reset();
        }

        beginTest ("A plug-in the catalogue doesn't know is Missing; once it does, Plug-in Hosting loads it into its sandbox");
        {
            Fixture f;
            f.projectSaveLocation = f.scratchDir().getChildFile ("Missing Sandboxed");

            {
                TestPlugin gain (f, "Sandbox Found", "plugin Sandbox Found");
                insert (f, addTrack (f), gain.path());
                f.invoke (cmd::projectSaveAs);
            }

            // f's Edit stays open, its plug-in under the same id: the newer Edit's is the one asked about.
            Fixture reopened;
            reopened.projectToOpen = f.projectSaveLocation;
            reopened.invoke (cmd::projectOpen);
            expect (reopened.errors.isEmpty(), reopened.errors.joinIntoString ("; "));

            juce::String id;

            for (auto& plugin : reopened.plugins.getAllPlugins())
                if (plugin.external)
                    id = plugin.id;

            expect (id.isNotEmpty() && is (reopened, id, HostingState::Kind::missing), "it isn't Missing");
            expect (reopened.app.engine.getPluginHosting().reload (id).failed(), "a Missing plug-in reloaded");

            // A scan finds it: the catalogue changes, and nothing else does.
            Transitions heard (reopened);
            TestPlugin found (reopened, "Sandbox Found", "plugin Sandbox Found");
            expect (dispatchUntil ([&] { return is (reopened, id, HostingState::Kind::sandboxed); }),
                    "it never loaded: " + nameOf (state (reopened, id).kind) + " " + state (reopened, id).reason);
            expectEquals (heard.take (id), juce::String ("loading, sandboxed"));
            expect (PluginSandbox::isSandboxed (instanceOf (reopened, id)));
        }

        beginTest ("A plug-in undone while it loads comes back loaded on Redo");
        {
            Fixture f;
            TestPlugin sluggish (f, "Sandbox Sluggish Undo", "plugin Sluggish Undo Gain");
            const auto track = addTrack (f);
            f.invoke (cmd::pluginInsert, { track, sluggish.path(), PluginChain::device });
            const auto chain = f.plugins.getChain (track, PluginChain::device);
            const auto id = chain.empty() ? juce::String() : chain.back().id;
            expect (is (f, id, HostingState::Kind::loading));

            f.invoke (cmd::editUndo);
            expect (! f.plugins.contains (id));
            expect (f.app.engine.getPluginHosting().waitForLoads(), "the load never ended");

            f.invoke (cmd::editRedo);
            expect (f.plugins.contains (id) && loaded (f, id));
            expect (instanceOf (f, id) != nullptr && PluginSandbox::isSandboxed (instanceOf (f, id)),
                    "redo left it unloaded: " + state (f, id).reason);
            expect (is (f, id, HostingState::Kind::sandboxed));
        }

        beginTest ("A load still pending when the Edit is replaced is the old Edit's: the next Edit's plug-in of that id settles on its own");
        {
            // The old Edit's plug-in is undone while it loads, and another inserted: its load stays pending.
            Fixture f;
            TestPlugin sluggish (f, "Sandbox Sluggish Replaced", "plugin Sluggish Replaced Gain");
            const auto oldTrack = addTrack (f);
            f.invoke (cmd::pluginInsert, { oldTrack, sluggish.path(), PluginChain::device });
            const auto id = f.plugins.getChain (oldTrack, PluginChain::device).back().id;
            expect (is (f, id, HostingState::Kind::loading));
            f.invoke (cmd::editUndo);
            f.invoke (cmd::pluginInsert, { oldTrack, sluggish.path(), PluginChain::device });

            // Nothing of the new Edit loads: an offline render there doesn't wait for the old Edit's loads.
            f.invoke (cmd::projectNew);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            expect (f.app.engine.getPluginHosting().waitForLoads());
            expectLessThan (juce::Time::getMillisecondCounterHiRes() - started, TestPluginFormat::sluggishLoadMs / 2.0,
                            "it waited for the old Edit's loads");

            // A new Edit numbers its ids afresh: the same steps give its plug-in the same id.
            f.app.engine.getPluginHosting().removeHostedFormat (TestPluginFormat::formatName);
            const auto track = addTrack (f);
            f.invoke (cmd::pluginInsert, { track, sluggish.path(), PluginChain::device });
            const auto chain = f.plugins.getChain (track, PluginChain::device);
            expect (! chain.empty() && chain.back().id == id, "the ids don't collide");

            expect (loaded (f, id), "it stayed Loading");
            expect (is (f, id, HostingState::Kind::inProcess), "it is " + nameOf (state (f, id).kind) + ": " + state (f, id).reason);
            expect (instanceOf (f, id) != nullptr && ! PluginSandbox::isSandboxed (instanceOf (f, id)));
        }

        beginTest ("A load still pending when the Edit is replaced isn't adopted by the next Edit's sandboxed plug-in of that id");
        {
            Fixture f;
            TestPlugin sluggish (f, "Sandbox Sluggish Adopted", "plugin Sluggish Adopted Gain");
            f.invoke (cmd::pluginInsert, { addTrack (f), sluggish.path(), PluginChain::device });
            const auto id = f.plugins.getChain (f.model.getTracks().back().id, PluginChain::device).back().id;

            // Most of the old load's time goes by: adopted, it would finish well before a load of its own.
            bool waited = false;
            juce::Timer::callAfterDelay (TestPluginFormat::sluggishLoadMs * 2 / 3, [&waited] { waited = true; });
            dispatchUntil ([&] { return waited; });
            expect (is (f, id, HostingState::Kind::loading));

            f.invoke (cmd::projectNew);
            const auto track = addTrack (f);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            f.invoke (cmd::pluginInsert, { track, sluggish.path(), PluginChain::device });
            const auto chain = f.plugins.getChain (track, PluginChain::device);
            expect (! chain.empty() && chain.back().id == id, "the ids don't collide");

            expect (loaded (f, id) && is (f, id, HostingState::Kind::sandboxed), "it is " + nameOf (state (f, id).kind) + ": " + state (f, id).reason);
            expectGreaterOrEqual (juce::Time::getMillisecondCounterHiRes() - started, (double) TestPluginFormat::sluggishLoadMs,
                                  "it took over the old Edit's load");
        }

        beginTest ("An export straight after inserting a slow plug-in waits for it: the mix has it in");
        {
            Fixture f;
            TestPlugin sluggish (f, "Sandbox Sluggish Export", "plugin Sluggish Export Gain");
            const auto track = addTrack (f);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipInsertAt, { f.audioFileToChoose, track, 0.0 });
            const auto dry = renderPeak (f);

            f.invoke (cmd::pluginInsert, { track, sluggish.path(), PluginChain::device });
            const auto mix = f.scratchDir().getChildFile ("mix.wav");
            expect (f.invoke (cmd::fileExportMix, { mix.getFullPathName() }), f.errors.joinIntoString ("; "));

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (mix));
            expect (reader != nullptr && reader->lengthInSamples > 0, "no mix");

            if (reader != nullptr)
            {
                juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                // The test plug-in halves its input: left out, the mix would be the dry tone.
                expectWithinAbsoluteError (buffer.getMagnitude (0, buffer.getNumSamples()), dry * 0.5f, dry * 0.1f);
            }
        }

        beginTest ("A bounce straight after inserting a slow plug-in waits for it: the bounce has it in");
        {
            Fixture f;
            TestPlugin sluggish (f, "Sandbox Sluggish Bounce", "plugin Sluggish Bounce Gain");
            const auto track = addTrack (f);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipInsertAt, { f.audioFileToChoose, track, 0.0 });
            const auto dry = renderPeak (f);

            f.invoke (cmd::pluginInsert, { track, sluggish.path(), PluginChain::device });
            const auto bounce = f.scratchDir().getChildFile ("bounce.wav");
            expect (f.invoke (cmd::trackBounce, { track, bounce.getFullPathName() }), f.errors.joinIntoString ("; "));

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (bounce));
            expect (reader != nullptr && reader->lengthInSamples > 0, "no bounce");

            if (reader != nullptr)
            {
                juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                // The test plug-in halves its input: left out, the bounce would be the dry tone.
                expectWithinAbsoluteError (buffer.getMagnitude (0, buffer.getNumSamples()), dry * 0.5f, dry * 0.1f);
            }
        }

        beginTest ("A sandboxed plug-in's CPU is its host's time in the plug-in, not the round trip");
        {
            Fixture f;
            TestPlugin quick (f, "Quick Gain", "plugin Quick Gain");
            TestPlugin slow (f, "Slow Gain", "plugin Slow Gain");
            const auto track = addTrack (f);
            const auto quickId = insert (f, track, quick.path());
            const auto slowId = insert (f, track, slow.path());
            auto* quickInstance = instanceOf (f, quickId);
            auto* slowInstance = instanceOf (f, slowId);
            expect (quickInstance != nullptr && slowInstance != nullptr);

            if (quickInstance == nullptr || slowInstance == nullptr)
                return;

            for (auto* instance : { quickInstance, slowInstance })
            {
                prepare (*instance);

                for (int block = 0; block < 60; ++block)
                    processOnes (*instance);
            }

            // 2 ms of a 512-sample block at 44.1 kHz (11.6 ms) is about 17 %.
            expectGreaterThan (f.plugins.getCpuLoad (slowId), 0.1);
            expectLessThan (f.plugins.getCpuLoad (slowId), 0.6);
            expectLessThan (f.plugins.getCpuLoad (quickId), f.plugins.getCpuLoad (slowId) * 0.5);
        }

        beginTest ("A crash bypasses that plug-in only: playback and the other plug-ins go on; Reload restores its saved state");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Crash", "plugin Crashing Gain");
            Transitions heard (f);
            const auto first = addTrack (f);
            const auto second = addTrack (f);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipInsertAt, { f.audioFileToChoose, first, 0.0 });

            const auto id = insert (f, first, gain.path());
            const auto other = insert (f, second, gain.path());
            auto* instance = instanceOf (f, id);
            auto* otherInstance = instanceOf (f, other);
            expect (instance != nullptr && otherInstance != nullptr);

            if (instance == nullptr || otherInstance == nullptr)
                return;

            // The last saved state has Gain at 0.3; it changes after.
            setParameter (f, id, "Gain", 0.3f);
            f.projectSaveLocation = f.scratchDir().getChildFile ("Crash");
            f.invoke (cmd::projectSaveAs);
            setParameter (f, id, "Gain", 0.9f);

            prepare (*instance);
            prepare (*otherInstance);
            expectWithinAbsoluteError (processOnes (*instance), 0.9f, 1.0e-6f);

            f.model.play();
            expect (f.model.isPlaying());
            expectEquals (heard.take (id), juce::String ("loading, sandboxed"), "insert");
            heard.take (other);

            // The plug-in dies in the middle of an audio block.
            setParameter (f, id, "Crash", 1.0f);
            double elapsed = 0;
            expectWithinAbsoluteError (processOnes (*instance, &elapsed), 1.0f, 1.0e-6f);
            expectLessThan (elapsed, 50.0, "the audio thread waited on a dead sandbox");

            expect (dispatchUntil ([&] { return is (f, id, HostingState::Kind::crashed); }), "the crash went unnoticed");
            expectEquals (heard.take (id), juce::String ("crashed"));
            expect (heard.take (other).isEmpty(), "the other plug-in's state changed");
            expect (f.model.isPlaying(), "the crash stopped playback");

            // Bypassed: the input passes, at once.
            expectWithinAbsoluteError (processOnes (*instance, &elapsed), 1.0f, 1.0e-6f);
            expectLessThan (elapsed, 5.0);

            // The other track's plug-in, in its own sandbox, plays on.
            expectWithinAbsoluteError (processOnes (*otherInstance), 0.5f, 1.0e-6f);
            expect (is (f, other, HostingState::Kind::sandboxed));

            f.model.stop();
            expectGreaterThan (renderPeak (f), 0.1f, "the Edit stopped sounding");

            // Reload: a new sandbox, from the state last saved.
            f.invoke (cmd::pluginReload, { first, id });
            expect (is (f, id, HostingState::Kind::loading), "a plug-in being reloaded isn't Loading");
            expect (loaded (f, id), "Reload never finished loading");
            expectEquals (heard.take (id), juce::String ("loading, sandboxed"), "Reload");
            expectWithinAbsoluteError (parameterValue (f, id, "Gain"), 0.3f, 1.0e-6f);

            if (auto* fresh = instanceOf (f, id))
            {
                prepare (*fresh);
                expectWithinAbsoluteError (processOnes (*fresh), 0.3f, 1.0e-6f);
            }
            else
            {
                expect (false, "Reload left no instance");
            }
        }

        beginTest ("Run in-process, either way, and Reload keep the changes made since the last save (#171)");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Keep", "plugin Keep Gain");
            const auto track = addTrack (f);
            const auto id = insert (f, track, gain.path());

            // What the plug-in itself plays: its state, not Resamper's mirror of it.
            auto played = [&]
            {
                auto* instance = instanceOf (f, id);

                if (instance == nullptr)
                    return -1.0f;

                prepare (*instance);
                return processOnes (*instance);
            };

            auto check = [&] (float expected, const juce::String& after)
            {
                expect (loaded (f, id), after + ": it didn't load");
                expectWithinAbsoluteError (parameterValue (f, id, "Gain"), expected, 1.0e-3f, after + ": Resamper's Gain");
                expectWithinAbsoluteError (played(), expected, 1.0e-3f, after + ": the plug-in's Gain");
            };

            setParameter (f, id, "Gain", 0.25f);
            f.invoke (cmd::pluginSetRunInProcess, { id, true });
            check (0.25f, "Run in-process");
            expect (is (f, id, HostingState::Kind::inProcess));

            setParameter (f, id, "Gain", 0.75f);
            f.invoke (cmd::pluginSetRunInProcess, { id, false });
            check (0.75f, "back into the sandbox");
            expect (is (f, id, HostingState::Kind::sandboxed));

            setParameter (f, id, "Gain", 0.4f);
            f.invoke (cmd::pluginReload, { track, id });
            check (0.4f, "Reload");
        }

        beginTest ("Run in-process is per instance and saved with the project");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Choice", "plugin Choice Gain");
            const auto track = addTrack (f);
            Transitions heard (f);
            const auto inProcess = insert (f, track, gain.path());
            const auto sandboxed = insert (f, track, gain.path());
            heard.take (inProcess);

            if (const auto* command = f.commands.find (cmd::pluginSetRunInProcess.id))
                expectEquals (command->getName(), juce::String ("Run Plug-in In-process"));
            else
                expect (false, "plugin.setRunInProcess isn't registered");

            expect (f.invoke (cmd::pluginSetRunInProcess, { inProcess, true }));
            expect (f.errors.isEmpty(), f.errors.joinIntoString ("; "));
            expect (loaded (f, inProcess));
            expectEquals (heard.take (inProcess), juce::String ("loading, in-process"), "Run in-process");
            expect (is (f, sandboxed, HostingState::Kind::sandboxed));

            auto* instance = instanceOf (f, inProcess);
            expect (instance != nullptr && ! PluginSandbox::isSandboxed (instance), "it isn't running in-process");

            f.projectSaveLocation = f.scratchDir().getChildFile ("Choice");
            f.invoke (cmd::projectSaveAs);

            Fixture reopened;
            reopened.projectToOpen = f.projectSaveLocation;
            reopened.invoke (cmd::projectOpen);
            expect (loaded (reopened, inProcess) && loaded (reopened, sandboxed));
            expect (is (reopened, inProcess, HostingState::Kind::inProcess));
            expect (is (reopened, sandboxed, HostingState::Kind::sandboxed));

            // And back into the sandbox.
            Transitions reheard (reopened);
            expect (reopened.invoke (cmd::pluginSetRunInProcess, { inProcess, false }));
            expect (loaded (reopened, inProcess));
            expectEquals (reheard.take (inProcess), juce::String ("loading, sandboxed"), "back into the sandbox");
            expect (instanceOf (reopened, inProcess) != nullptr && PluginSandbox::isSandboxed (instanceOf (reopened, inProcess)));
        }

        beginTest ("A sandboxed plug-in's own UI shows in its window's vendor area; Parameters swaps in its parameters");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Own UI", "plugin OwnUi Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);

            const auto id = insert (f, track, gain.path());
            auto* window = main->getPluginWindows().getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));
            auto* instance = instanceOf (f, id);
            expect (instance != nullptr && PluginSandbox::isSandboxed (instance));

            if (window == nullptr || instance == nullptr || window->getVendorComponent() == nullptr)
                return;

            auto& vendor = *window->getVendorComponent();
            expectEquals (vendor.getWidth(), TestPluginFormat::editorWidth, "the vendor area isn't the UI's native size");
            expectEquals (vendor.getHeight(), TestPluginFormat::editorHeight);
            expect (findOne (*window, "showOwnEditor") == nullptr, "the UI still opens in a window of its own");
            expect (findType<juce::GenericAudioProcessorEditor> (*window) == nullptr, "parameters show without asking");

            auto ownUi = [&] { return PluginSandbox::getOwnEditorScreenBounds (instance); };
            expect (dispatchUntil ([&] { return ownUi() == vendor.getScreenBounds(); }),
                    "the sandbox doesn't lay the UI over the vendor area: " + ownUi().toString()
                        + " vs " + vendor.getScreenBounds().toString());

            window->setFramePosition (window->getFrameScreenBounds().getPosition() + juce::Point<int> (40, 30));
            expect (dispatchUntil ([&] { return ownUi() == vendor.getScreenBounds(); }), "the UI doesn't follow its window");

            auto* parametersButton = findOne (*window, "parameters");
            expect (parametersButton != nullptr && parametersButton->isEnabled());
            click (parametersButton);
            expect (dispatchUntil ([&] { return window->isShowingParameters(); }), "Parameters doesn't toggle");
            auto* parameters = findType<juce::GenericAudioProcessorEditor> (*window);
            expect (parameters != nullptr && parameters->isShowing(), "Parameters shows no parameters");
            expect (! vendor.isVisible());
            expect (dispatchUntil ([&] { return ownUi().isEmpty(); }), "the UI stays over the parameters");

            click (parametersButton);
            expect (dispatchUntil ([&] { return ! window->isShowingParameters(); }) && vendor.isVisible());
            expect (findType<juce::GenericAudioProcessorEditor> (*window) == nullptr);
            expect (dispatchUntil ([&] { return ownUi() == vendor.getScreenBounds(); }), "the UI doesn't come back");

            main->getPluginWindows().close (id);
            expect (dispatchUntil ([&] { return ownUi().isEmpty(); }), "the UI outlives its window");
            main.reset();
        }

        beginTest ("A drag in a sandboxed plug-in's own UI isn't pulled back to values it has left (#168)");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Drag", "plugin Drag Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);

            const auto id = insert (f, track, gain.path());
            auto* window = main->getPluginWindows().getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));
            auto* instance = instanceOf (f, id);

            if (window == nullptr || instance == nullptr)
                return;

            // The drag outlasts several of the host's reports: each comes back while it goes on.
            PluginSandbox::pressKeyInOwnEditor (instance, juce::KeyPress (juce::KeyPress::upKey));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (TestPluginFormat::dragSteps * TestPluginFormat::dragStepMs * 3);

            const auto dragged = 0.5f + TestPluginFormat::dragSteps * TestPluginFormat::dragStep;
            expect (dispatchUntil ([&] { return std::abs (parameterValue (f, id, "Gain") - dragged) < 1.0e-3f; }),
                    "Resamper doesn't end where the drag did: " + juce::String (parameterValue (f, id, "Gain")));

            prepare (*instance);
            expectWithinAbsoluteError (processOnes (*instance), dragged, 1.0e-3f, "the plug-in itself was pulled back");
            main.reset();
        }

        // The macOS window list can see the host's panel. Elsewhere that order is manual QA.
#if JUCE_MAC
        beginTest ("A sandboxed plug-in's window brought over its own UI (a click in it, Bypass say) gets the UI back on top (#168)");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Click", "plugin Click Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);

            const auto id = insert (f, track, gain.path());
            auto* window = main->getPluginWindows().getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));
            auto* instance = instanceOf (f, id);

            if (window == nullptr || instance == nullptr || window->getVendorComponent() == nullptr)
                return;

            auto ownUi = [&] { return PluginSandbox::getOwnEditorScreenBounds (instance); };
            auto uiInFront = [&]
            {
                return ownUi() == window->getVendorComponent()->getScreenBounds() && ! sandboxdock::isInFrontOf (*window, ownUi());
            };

            // Back within a moment, not whenever something else happens to move the window.
            auto backSoon = [&]
            {
                for (int i = 0; i < 50 && ! uiInFront(); ++i)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

                return uiInFront();
            };

            expect (dispatchUntil (uiInFront), "the plug-in UI isn't up");

            orderFrontNatively (*window);
            expect (backSoon(), "the window stays over the plug-in's UI");

            f.invoke (cmd::pluginSetBypassed, { track, id, true });
            orderFrontNatively (*window);
            expect (backSoon(), "bypassed, the window stays over the plug-in's UI");

            main->getPluginWindows().close (id);
            main.reset();
        }

        beginTest ("Popups over a sandboxed plug-in's window show above its own UI");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Popups", "plugin Popups Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);

            const auto id = insert (f, track, gain.path());
            auto* window = main->getPluginWindows().getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));
            auto* instance = instanceOf (f, id);

            if (window == nullptr || instance == nullptr || window->getVendorComponent() == nullptr)
                return;

            auto ownUi = [&] { return PluginSandbox::getOwnEditorScreenBounds (instance); };
            expect (dispatchUntil ([&] { return ownUi() == window->getVendorComponent()->getScreenBounds(); }),
                    "the plug-in UI isn't up");

            auto above = [&] (juce::Component* popup)
            {
                return popup != nullptr && popup->isShowing() && sandboxdock::isInFrontOf (*popup, ownUi());
            };

            // Dismisses whatever this test opened, before the window goes.
            struct PopupsDown
            {
                ~PopupsDown()
                {
                    juce::PopupMenu::dismissAllActiveMenus();

                    if (auto* modal = juce::Component::getCurrentlyModalComponent())
                        modal->exitModalState (0);

                    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
                }
            } popupsDown;

            // The menu closes unless the app is in front or the pointer is over it.
            // Parking the pointer on it keeps it up when this process can't take focus.
            juce::Process::makeForegroundProcess();
            window->toFront (true);
            auto mouse = juce::Desktop::getInstance().getMainMouseSource();
            const auto savedMouse = mouse.getScreenPosition();

            if (auto* preset = findOne (*window, "preset"))
            {
                const auto b = preset->getScreenBounds();
                mouse.setScreenPosition ({ (float) b.getCentreX(), (float) b.getBottom() + 8.0f });
            }

            click (findOne (*window, "preset"));
            expect (dispatchUntil ([&] { return findPresetMenu() != nullptr; }), "the preset menu didn't open");
            expect (dispatchUntil ([&] { return above (findPresetMenu()); }),
                    "the preset menu is behind the plug-in UI " + ownUi().toString());
            juce::PopupMenu::dismissAllActiveMenus();
            mouse.setScreenPosition (savedMouse);

            click (findButton (*window, "Save preset"));
            auto* dialog = juce::Component::getCurrentlyModalComponent();
            expect (dialog != nullptr && dispatchUntil ([&] { return above (dialog); }),
                    "Save Preset is behind the plug-in UI");

            // The panel is reordered when its window moves. The dialog stays above it.
            window->setFramePosition (window->getFrameScreenBounds().getPosition() + juce::Point<int> (30, 20));
            expect (dispatchUntil ([&] { return above (juce::Component::getCurrentlyModalComponent()); }),
                    "moving the window put the plug-in UI over Save Preset");

            if (auto* modal = juce::Component::getCurrentlyModalComponent())
                modal->exitModalState (0);

            expect (findType<juce::TooltipWindow> (*main) == nullptr, "tooltips are drawn in the main window, under plug-in UI");
            juce::TooltipWindow tip;
            auto* preset = findOne (*window, "preset");
            tip.displayTip (preset != nullptr ? preset->getScreenBounds().getCentre() : ownUi().getCentre(), "Presets");
            expect (dispatchUntil ([&] { return tip.isShowing() && sandboxdock::isInFrontOf (tip, ownUi()); }),
                    "a tooltip over the chrome is behind the plug-in UI");

            main->showToast ("Saved the preset", false);
            auto* toasts = findTypeOrOnDesktop<Toasts> (*main);
            expect (toasts != nullptr && dispatchUntil ([&] { return above (toasts); }),
                    "a toast is behind the plug-in UI");

            main.reset();
        }
#endif

        beginTest ("Space the sandboxed UI doesn't use plays; Esc hands focus back to the window");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Keys", "plugin Keys Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);
            commandManager.registerAllCommandsForTarget (main.get());
            commandManager.setFirstCommandTarget (main.get());

            const auto id = insert (f, track, gain.path());
            auto* window = main->getPluginWindows().getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));
            auto* instance = instanceOf (f, id);

            if (window == nullptr || instance == nullptr)
                return;

            expect (dispatchUntil ([&] { return ! PluginSandbox::getOwnEditorScreenBounds (instance).isEmpty(); }));
            expect (! f.model.isPlaying());
            PluginSandbox::pressKeyInOwnEditor (instance, juce::KeyPress (juce::KeyPress::spaceKey, {}, ' '));
            expect (dispatchUntil ([&] { return f.model.isPlaying(); }), "space in the sandboxed UI didn't play");

            PluginSandbox::pressKeyInOwnEditor (instance, juce::KeyPress (juce::KeyPress::escapeKey));
            expect (dispatchUntil ([&] { return window->hasFocusInside(); }), "Esc in the sandboxed UI didn't hand focus back");
            expect (main->getPluginWindows().isOpen (id), "Esc in the sandboxed UI closed its window");
            main.reset();
        }

        beginTest ("Keys the sandboxed UI doesn't use are Resamper's: a menu shortcut, Mod+Alt+P, Mod+W (#129)");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Shortcuts", "plugin Shortcuts Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);
            commandManager.registerAllCommandsForTarget (main.get());
            commandManager.setFirstCommandTarget (main.get());

            const auto id = insert (f, track, gain.path());
            auto& windows = main->getPluginWindows();
            auto* window = windows.getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));
            auto* instance = instanceOf (f, id);

            if (window == nullptr || instance == nullptr)
                return;

            expect (dispatchUntil ([&] { return ! PluginSandbox::getOwnEditorScreenBounds (instance).isEmpty(); }));
            const auto mod = juce::ModifierKeys::commandModifier;

            // A menu's key mapping (Mod+T adds a track).
            const auto tracks = f.model.getTracks().size();
            PluginSandbox::pressKeyInOwnEditor (instance, juce::KeyPress ('T', mod, 't'));
            expect (dispatchUntil ([&] { return f.model.getTracks().size() == tracks + 1; }), "Mod+T in the sandboxed UI added no track");

            // Mod+Alt+P hides every plug-in window, and shows them again.
            PluginSandbox::pressKeyInOwnEditor (instance, juce::KeyPress ('P', juce::ModifierKeys (mod | juce::ModifierKeys::altModifier), 'p'));
            expect (dispatchUntil ([&] { return ! windows.isShowing (id); }), "Mod+Alt+P in the sandboxed UI didn't hide the windows");
            windows.toggleAll();
            expect (dispatchUntil ([&] { return windows.isShowing (id) && ! PluginSandbox::getOwnEditorScreenBounds (instance).isEmpty(); }));

            // Mod+W closes its window.
            PluginSandbox::pressKeyInOwnEditor (instance, juce::KeyPress ('W', mod, 'w'));
            expect (dispatchUntil ([&] { return ! windows.isOpen (id); }), "Mod+W in the sandboxed UI didn't close its window");
            main.reset();
        }

        beginTest ("A plug-in that dies loading shows the error state; Run in-process loads it");
        {
            Fixture f;
            TestPlugin fragile (f, "Sandbox Fragile", "sandboxcrash Fragile Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);

            Transitions heard (f);
            const auto id = insert (f, track, fragile.path());
            expect (id.isNotEmpty());
            expectEquals (heard.take (id), juce::String ("loading, failed"), "a load that failed");
            expect (state (f, id).reason.contains ("crashed while loading"), "the reason: " + state (f, id).reason);

            auto* window = main->getPluginWindows().getWindow (id);
            expect (window != nullptr, "the window didn't open");

            if (window == nullptr)
                return;

            expect (dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::failed; }),
                    "no error state");

            auto* runInProcess = findOne (*window, "runInProcess");
            expect (runInProcess != nullptr && runInProcess->isVisible());
            click (runInProcess);

            expect (dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }),
                    "it didn't load in-process");
            expect (instanceOf (f, id) != nullptr && ! PluginSandbox::isSandboxed (instanceOf (f, id)));
            expectEquals (heard.take (id), juce::String ("loading, in-process"), "Run in-process");
            main.reset();
        }

        beginTest ("A crash closes the window, explains in a toast with Reload, and the card offers Reload");
        {
            Fixture f;
            TestPlugin gain (f, "Sandbox Window", "plugin Window Gain");
            f.theme.load();
            const auto track = addTrack (f);
            f.invoke (cmd::trackSelect, { track });
            juce::ApplicationCommandManager commandManager;
            auto main = std::make_unique<MainComponent> (f.app, commandManager);
            main->setSize (1400, 900);

            const auto id = insert (f, track, gain.path());
            auto& windows = main->getPluginWindows();
            expect (windows.isOpen (id));

            auto* window = windows.getWindow (id);
            expect (window != nullptr && dispatchUntil ([&] { return window->getStatus() == PluginWindow::Status::ready; }));

            auto* instance = instanceOf (f, id);
            expect (instance != nullptr);

            if (instance == nullptr)
                return;

            prepare (*instance);
            setParameter (f, id, "Crash", 1.0f);
            processOnes (*instance);

            expect (dispatchUntil ([&] { return ! windows.isOpen (id); }), "the window stayed open");

            auto* toasts = findTypeOrOnDesktop<Toasts> (*main);
            const auto message = "Window Gain crashed on " + f.model.getTracks().back().name
                               + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "its audio is bypassed; the rest plays on";
            expect (toasts != nullptr && toasts->getMessages().contains (message),
                    toasts != nullptr ? toasts->getMessages().joinIntoString (" | ") : juce::String());

            expect (dispatchUntil ([&] { auto* r = findOne (*main, "reload"); return r != nullptr && r->isVisible(); }),
                    "the card offers no Reload");

            expect (toasts != nullptr && toasts->runAction (message, "Reload"));
            expect (loaded (f, id));
            expect (is (f, id, HostingState::Kind::sandboxed));
            expect (dispatchUntil ([&] { auto* r = findOne (*main, "reload"); return r == nullptr || ! r->isVisible(); }),
                    "the card still offers Reload");
            main.reset();
        }

        beginTest ("An AUv3 the engine creates asynchronously reports its end of Loading by push: In-process, or Failed");
        {
            Fixture f;
            AsyncPlugin works (f, "Async Gain");
            AsyncPlugin fails (f, "Async fail Gain");
            Transitions heard (f);
            const auto track = addTrack (f);

            f.invoke (cmd::pluginInsert, { track, works.desc.fileOrIdentifier, PluginChain::device });
            const auto id = f.plugins.getChain (track, PluginChain::device).back().id;
            expect (is (f, id, HostingState::Kind::loading), "it isn't loading");
            expect (dispatchUntil ([&] { return is (f, id, HostingState::Kind::inProcess); }), "it never ran");
            expectEquals (heard.take (id), juce::String ("loading, in-process"));
            expect (instanceOf (f, id) != nullptr);

            // The engine never says such a creation failed: it has once it took the Sandbox's load time.
            f.invoke (cmd::pluginInsert, { track, fails.desc.fileOrIdentifier, PluginChain::device });
            const auto failing = f.plugins.getChain (track, PluginChain::device).back().id;
            expect (failing != id && is (f, failing, HostingState::Kind::loading));
            expect (dispatchUntil ([&] { return is (f, failing, HostingState::Kind::failed); }),
                    "it never failed");
            expectEquals (heard.take (failing), juce::String ("loading, failed"));
            expect (state (f, failing).reason.isNotEmpty());
        }
    }
};

static PluginSandboxTests pluginSandboxTests;


} // namespace resamper::test
