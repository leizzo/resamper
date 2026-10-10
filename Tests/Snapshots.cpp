#include "App/AppUpdate.h"
#include "App/UILanguage.h"
#include "ComponentSearch.h"
#include "TestFixture.h"
#include "Commands/ApplicationCommandTable.h"
#include "Engine/NativeDevicePlugins.h"
#include "UI/MainWindow/AppUpdatePrompt.h"
#include "UI/MainWindow/MainComponent.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper::test
{

namespace
{
    /** SNAPSHOT_LANG=tr renders in that UI Language; unset, English, as the tests run. */
    void installSnapshotLanguage (const UIFileSource& files)
    {
        installUILanguage (files, resolveUILanguage (juce::SystemStats::getEnvironmentVariable ("SNAPSHOT_LANG", "en"),
                                                     juce::SystemStats::getUserLanguage()));
    }
}

/** Not a test: renders the whole window offscreen to PNGs in /tmp/resamper-snapshots,
    one per view, for eyeballing against the design. Run with
    `ResamperTests --snapshot`. */
struct Snapshots : juce::UnitTest
{
    Snapshots() : juce::UnitTest ("Window snapshots", "Snapshot") {}

    void runTest() override
    {
        beginTest ("Render every view");

        Fixture f;
        installSnapshotLanguage (f.uiFiles);
        expect (f.theme.load().wasOk());
        juce::LookAndFeel::setDefaultLookAndFeel (&f.theme.getLookAndFeel());

        // Some content to look at.
        f.invoke (cmd::trackAdd);
        f.invoke (cmd::trackAddMidi);
        f.audioFileToChoose = writeSineWav (f.scratchDir().getChildFile ("tone.wav"), 4.0);
        f.model.selectTrack (f.model.getTracks()[0].id);
        f.invoke (cmd::clipAdd);
        f.model.selectTrack (f.model.getTracks()[1].id);
        f.invoke (cmd::clipAddMidi);
        f.invoke (cmd::mixerAddReturn);
        f.plugins.insert (f.model.getTracks()[0].id, tracktion::ReverbPlugin::xmlTypeName);
        f.plugins.insert (f.model.getTracks()[0].id, tracktion::CompressorPlugin::xmlTypeName, PluginChain::mixer);
        f.plugins.insert (f.model.getTracks()[0].id, tracktion::DelayPlugin::xmlTypeName);
        f.plugins.insert (f.model.getTracks()[0].id, EqEightPlugin::xmlTypeName);
        f.plugins.insert (f.model.getTracks()[0].id, CompressorV2Plugin::xmlTypeName);
        f.invoke (cmd::noteAdd, { f.model.getTracks()[1].clips[0].id, 0.0, 0.25, 60 });
        f.invoke (cmd::noteAdd, { f.model.getTracks()[1].clips[0].id, 0.5, 0.25, 64 });
        f.invoke (cmd::noteAdd, { f.model.getTracks()[1].clips[0].id, 1.0, 0.5, 67 });
        f.invoke (cmd::trackToggleSolo, { f.model.getTracks()[1].id });
        f.invoke (cmd::transportSetLoopRange, { 0.0, 8.0 });
        f.model.selectClip (f.model.getTracks()[0].clips[0].id);

        // Last: nesting the first track in a Bus reorders getTracks().
        f.invoke (cmd::mixerAddBus, { "Drum Bus" });
        f.invoke (cmd::mixerMoveToBus, { f.model.getTracks()[0].id, f.mixer.getBuses()[0].trackId });
        f.plugins.insert (f.mixer.getBuses()[0].trackId, tracktion::CompressorPlugin::xmlTypeName, PluginChain::mixer);

        juce::ApplicationCommandManager commandManager;
        const auto size = juce::Point<int> (juce::SystemStats::getEnvironmentVariable ("SNAPSHOT_W", "1600").getIntValue(),
                                            juce::SystemStats::getEnvironmentVariable ("SNAPSHOT_H", "1000").getIntValue());
        {
            MainComponent main (f.app, commandManager);
            main.setSize (size.x, size.y);

            auto dir = juce::File ("/tmp/resamper-snapshots");
            dir.createDirectory();

            for (auto [view, command] : { std::pair { "session", cmd::viewSession }, std::pair { "arrange", cmd::viewArrange },
                                          std::pair { "mixer", cmd::viewMixer }, std::pair { "pianoRoll", cmd::viewPianoRoll },
                                          std::pair { "editor", cmd::viewEditor } })
            {
                f.invoke (command);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                main.resized();
                auto image = main.createComponentSnapshot (main.getLocalBounds(), true, 2.0f);
                auto file = dir.getChildFile (juce::String (view) + ".png");
                file.deleteFile();
                juce::FileOutputStream out (file);
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        installUILanguage (f.uiFiles, "en");
    }
};

static Snapshots snapshots;

/** The update button and the completion dialog, offscreen, for a version step 0.2.1 → 0.2.2. */
struct WhatsNewSnapshot : juce::UnitTest
{
    WhatsNewSnapshot() : juce::UnitTest ("What's new", "Snapshot") {}

    void runTest() override
    {
        beginTest ("Render the update button and the completion dialog");

        struct Host : juce::Component
        {
            explicit Host (ThemeManager& tm) : themeManager (tm), prompt (tm)
            {
                addAndMakeVisible (prompt);
            }

            void paint (juce::Graphics& g) override
            {
                auto& theme = themeManager.getTheme();
                g.fillAll (theme.bgDeep);
                g.setColour (theme.bgPanel);
                g.fillRect (0, 0, getWidth(), themeManager.getMetrics().topBarHeight);
            }

            void resized() override   { prompt.setBounds (getLocalBounds()); }

            ThemeManager& themeManager;
            AppUpdatePrompt prompt;
        };

        UIFileSource source;
        installSnapshotLanguage (source);
        ThemeManager themes { source, "themes/dark.json" };
        expect (themes.load().wasOk());
        juce::LookAndFeel::setDefaultLookAndFeel (&themes.getLookAndFeel());

        const auto changelog = juce::File::getCurrentWorkingDirectory().getChildFile ("CHANGELOG.md").loadFileAsString();
        expect (changelog.isNotEmpty(), "run from the repo root so CHANGELOG.md is found");
        const auto items = releaseNoteItems (changelog, "0.2.1", "0.2.2");
        expect (! items.empty());

        const auto size = juce::Point<int> (juce::SystemStats::getEnvironmentVariable ("SNAPSHOT_W", "1600").getIntValue(),
                                            juce::SystemStats::getEnvironmentVariable ("SNAPSHOT_H", "1000").getIntValue());
        Host host (themes);
        host.setSize (size.x, size.y);

        AppRelease release;
        release.version = "0.2.2";
        release.tag = "v0.2.2";
        host.prompt.showOffer (release, releaseSummary (changelog, "0.2.2"), true);
        host.resized();

        auto dir = juce::File ("/tmp/resamper-snapshots");
        dir.createDirectory();

        auto write = [&] (const juce::String& name)
        {
            auto image = host.createComponentSnapshot (host.getLocalBounds(), true, 2.0f);
            auto file = dir.getChildFile (name);
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (juce::PNGImageFormat().writeImageToStream (image, out));
        };

        write ("update-available.png");

        host.prompt.showWelcome ("0.2.2", items, {}, {});
        host.resized();
        write ("whats-new.png");

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        installUILanguage (source, "en");
    }
};

static WhatsNewSnapshot whatsNewSnapshot;

/** The Options menu with its Language / Dil submenu, and the relaunch offer
    after choosing another language, in the SNAPSHOT_LANG UI Language. */
struct LanguageSnapshot : juce::UnitTest
{
    LanguageSnapshot() : juce::UnitTest ("Language menu", "Snapshot") {}

    void runTest() override
    {
        beginTest ("Render the Language / Dil submenu and the relaunch toast");

        Fixture f;
        installSnapshotLanguage (f.uiFiles);
        expect (f.theme.load().wasOk());
        juce::LookAndFeel::setDefaultLookAndFeel (&f.theme.getLookAndFeel());

        if (getInstalledUILanguage() != "en")
            f.app.preferences.setLanguage (getInstalledUILanguage());

        auto dir = juce::File ("/tmp/resamper-snapshots");
        dir.createDirectory();
        const auto suffix = getInstalledUILanguage() == "en" ? juce::String() : "-" + getInstalledUILanguage();

        auto write = [&] (juce::Component& c, const juce::String& name)
        {
            juce::Image image (juce::Image::ARGB, c.getWidth() * 2 + 48, c.getHeight() * 2 + 48, true);
            {
                juce::Graphics g (image);
                g.fillAll (f.theme.getTheme().bgDeep);
                g.drawImageAt (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), 24, 24);
            }

            auto file = dir.getChildFile (name + suffix + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            expect (juce::PNGImageFormat().writeImageToStream (image, out));
        };

        {
            juce::ApplicationCommandManager commandManager;
            MainComponent main (f.app, commandManager);
            commandManager.registerAllCommandsForTarget (&main);
            commandManager.setFirstCommandTarget (&main);
            main.setSize (1600, 1000);

            // Each popup in turn, captured as it opens: a menu closes once the run's app is not in front.
            auto capture = [&] (juce::PopupMenu menu, const juce::String& name)
            {
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ 200, 200, 1, 1 }));
                auto& desktop = juce::Desktop::getInstance();

                if (auto* window = desktop.getComponent (desktop.getNumComponents() - 1))
                    write (*window, name);

                juce::PopupMenu::dismissAllActiveMenus();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            };

            const auto options = createCommandMenu (commandManager, "Options");
            capture (options, "language-options-menu");

            for (juce::PopupMenu::MenuItemIterator it (options); it.next();)
                if (it.getItem().text == "Language / Dil" && it.getItem().subMenu != nullptr)
                    capture (*it.getItem().subMenu, "language-submenu");

            f.invoke (getInstalledUILanguage() == "tr" ? cmd::uiLanguageEnglish : cmd::uiLanguageTurkish);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);

            if (auto* toasts = findTypeOrOnDesktop<Toasts> (main))
                write (*toasts, "language-relaunch-toast");

            commandManager.setFirstCommandTarget (nullptr);
        }

        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        installUILanguage (f.uiFiles, "en");
    }
};

static LanguageSnapshot languageSnapshot;

} // namespace resamper::test
