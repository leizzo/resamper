#include "TestFixture.h"
#include "Commands/ApplicationCommandTable.h"
#include "UI/MainWindow/MainComponent.h"
#include "UI/Mixer/ChannelStrip.h"

namespace resamper::test
{

/** The composition root: one headless app the window, the tests and the
    snapshots all build, so every Command is reachable from a Fixture. */
struct AppTests : juce::UnitTest
{
    AppTests() : juce::UnitTest ("App", "Resamper") {}

    void runTest() override
    {
        beginTest ("The app registers every facade's Commands without a window");
        {
            Fixture f;

            for (auto* id : { "track.add", "file.exportMix", "plugin.insert", "mixer.addReturn",
                              "session.launchSlot", "automation.addPoint" })
                expect (f.commands.contains (id), id);
        }

        beginTest ("Built from the app, the window registers every Command a menu or key binding names");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            juce::ApplicationCommandManager commandManager;
            MainComponent main (f.app, commandManager);

            // The developer overlay exists only when the UI is read from the source tree.
            auto expected = [&f] (const juce::String& id)
            {
                return id != "dev.toggleOverlay" || f.uiFiles.isDevMode();
            };

            for (auto& entry : getApplicationCommandTable())
                if (expected (entry.commandId))
                    expect (f.commands.contains (entry.commandId), entry.commandId);

            for (auto& binding : getKeyBindings())
                if (expected (binding.commandId))
                    expect (f.commands.contains (binding.commandId), binding.commandId);
        }

        beginTest ("A function-backed Command reports its enabled and ticked state; unset, it is enabled and unticked");
        {
            Fixture f;
            auto* undo = f.commands.find ("edit.undo");
            auto* metronome = f.commands.find ("transport.toggleMetronome");
            auto* addTrack = f.commands.find ("track.add");

            expect (addTrack->isEnabled() && ! addTrack->isTicked());

            expect (! undo->isEnabled());
            f.invoke (cmd::trackAdd);
            expect (undo->isEnabled());

            const auto wasOn = metronome->isTicked();
            f.invoke (cmd::transportToggleMetronome);
            expect (metronome->isTicked() != wasOn);
        }

        beginTest ("A key binding's argument is its Command's args: an int, or none");
        {
            Fixture f;
            expect (f.theme.load().wasOk());
            juce::ApplicationCommandManager commandManager;
            MainComponent main (f.app, commandManager);

            for (auto& binding : getKeyBindings())
            {
                if (binding.argument == KeyBinding::noArgument)
                    continue;

                auto* command = f.commands.find (binding.commandId);
                expect (command != nullptr && command->getArgsType() == typeid (int), binding.commandId);
            }
        }

        beginTest ("Invoked by ID with args of another type, a Command does nothing and says so");
        {
            Fixture f;
            expect (! f.commands.invokeById ("track.add", 3));
            expect (! f.commands.invokeById ("track.setVolume", juce::String ("t1")));
            expectEquals (f.numTracks(), 0);
            expect (! f.model.canUndo());

            expect (f.commands.invokeById ("track.add"));
            expectEquals (f.numTracks(), 1);
        }

        beginTest ("A view Command drives the app: Esc clears the selection");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 1.0);
            f.invoke (cmd::clipAdd);
            f.model.selectClip (f.model.getTracks()[0].clips[0].id);
            expect (f.model.getSelectedClipId().isNotEmpty());

            expect (f.theme.load().wasOk());
            juce::ApplicationCommandManager commandManager;
            MainComponent main (f.app, commandManager);

            expect (f.invoke (cmd::uiEscape));
            expect (f.model.getSelectedClipId().isEmpty());
        }

        beginTest ("The Mixer draws a Bus Strip at the Bus width, right after its child");
        {
            Fixture f;
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::trackAdd);
            f.invoke (cmd::mixerAddBus, { "Drums" });
            const auto busId = f.mixer.getBuses()[0].trackId;
            f.invoke (cmd::mixerMoveToBus, { f.model.getTracks()[0].id, busId });

            expect (f.theme.load().wasOk());
            juce::ApplicationCommandManager commandManager;
            MainComponent main (f.app, commandManager);
            main.setSize (1600, 1000);
            f.invoke (cmd::viewMixer);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

            std::vector<ChannelStrip*> strips;
            std::function<void (juce::Component&)> collect = [&] (juce::Component& c)
            {
                if (auto* strip = dynamic_cast<ChannelStrip*> (&c))
                    strips.push_back (strip);

                for (auto* child : c.getChildren())
                    collect (*child);
            };
            collect (main);
            std::sort (strips.begin(), strips.end(), [] (auto* a, auto* b) { return a->getX() < b->getX(); });

            expectEquals ((int) strips.size(), 3);
            expect (strips.size() == 3 && strips[2]->getState().strip.id == busId);
            expect (strips.size() == 3 && strips[2]->getState().isBus());
            expect (strips.size() == 3 && strips[2]->getWidth() == f.theme.getMetrics().stripBusWidth);
            expect (strips.size() == 3 && strips[1]->getWidth() == f.theme.getMetrics().stripWidth);
        }
    }
};

static AppTests appTests;

} // namespace resamper::test
