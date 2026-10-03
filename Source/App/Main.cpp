#include "App/ResamperApp.h"
#include "Commands/PluginCommands.h"
#include "Commands/ProductionCommands.h"
#include "Engine/EngineManager.h"
#include "Engine/PluginSandbox.h"
#include "Engine/PluginScanner.h"
#include "UI/Theme/UIFileSource.h"
#include "UI/MainWindow/MainWindow.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

namespace
{
    juce::File lastProjectFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Resamper")
                   .getChildFile ("last-project.txt");
    }

    void rememberProjectFolder (const juce::File& folder)
    {
        auto file = lastProjectFile();

        if (file.getParentDirectory().createDirectory().failed())
            return;

        file.replaceWithText (folder.getFullPathName());
    }

    juce::File rememberedProjectFolder()
    {
        auto file = lastProjectFile();

        if (! file.existsAsFile())
            return {};

        return file.loadFileAsString().trim();
    }
}

class ResamperApplication : public juce::JUCEApplication,
                            private juce::Timer
{
public:
    const juce::String getApplicationName() override       { return "Resamper"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return false; }

    void initialise (const juce::String&) override
    {
        if (auto r = theme.load(); r.failed())
        {
            // The embedded theme is part of the build; failing here is a packaging bug.
            reportError ("Cannot start: " + r.getErrorMessage());
            quit();
            return;
        }

        juce::LookAndFeel::setDefaultLookAndFeel (&theme.getLookAndFeel());

        engine = std::make_unique<EngineManager> (getApplicationName(), EngineManager::AudioDevice::initialise);
        app = std::make_unique<ResamperApp> (*engine, theme);
        app->preferences.setFile (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                      .getChildFile ("Resamper").getChildFile ("preferences.xml"));
        wireCommandHost();
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *app);

        offerRecovery();
        startTimer (Production::autosaveIntervalMs);

        // In the background, each plug-in in its own worker: startup never waits (PRD §19).
        app->commands.invoke (cmd::pluginScan);
    }

    void shutdown() override
    {
        stopTimer();
        mainWindow.reset();
        chooser.reset();

        if (app != nullptr)
            app->model.stop();

        app.reset();
        engine.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override   { quit(); }

    void timerCallback() override
    {
        if (app == nullptr)
            return;

        if (app->commands.invoke (cmd::projectAutosave))
            rememberProjectFolder (app->projects.getProjectFolder());
    }

    void offerRecovery()
    {
        const auto folder = rememberedProjectFolder();

        if (! Production::hasNewerRecovery (folder) || app == nullptr)
            return;

        const auto recovery = folder.getChildFile ("Recovery");
        const auto message = "A newer recovery copy of \"" + folder.getFileName()
                             + "\" was found. Open it?";

        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
                                            "Resamper", message, "Open Recovery", "Skip",
                                            mainWindow.get(),
                                            juce::ModalCallbackFunction::create ([this, recovery] (int result)
                                            {
                                                if (result != 1 || app == nullptr)
                                                    return;

                                                juce::var recovered;

                                                if (auto r = app->model.openProject (recovery, recovered); r.wasOk())
                                                {
                                                    app->uiState.restore (recovered);
                                                    rememberProjectFolder (recovery);
                                                }
                                                else
                                                {
                                                    reportError (r.getErrorMessage());
                                                }
                                            }));
    }

private:
    UIFileSource uiFiles;
    ThemeManager theme { uiFiles, "themes/dark.json" };

    std::unique_ptr<EngineManager> engine;
    std::unique_ptr<ResamperApp> app;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<MainWindow> mainWindow;

    void choose (const juce::String& title, const juce::String& patterns, int flags, AppCommandHost::FileCallback callback)
    {
        chooser = std::make_unique<juce::FileChooser> (title, juce::File::getSpecialLocation (juce::File::userMusicDirectory), patterns);
        chooser->launchAsync (flags, [cb = std::move (callback)] (const juce::FileChooser& fc)
        {
            if (auto result = fc.getResult(); result != juce::File())
                cb (result);
        });
    }

    void wireCommandHost()
    {
        using FB = juce::FileBrowserComponent;
        auto& commandHost = app->host;

        commandHost.chooseAudioFile = [this] (auto cb)
        {
            choose ("Add Audio Clip", "*.wav;*.aif;*.aiff;*.flac", FB::openMode | FB::canSelectFiles, std::move (cb));
        };

        commandHost.chooseProjectToOpen = [this] (auto cb)
        {
            choose ("Open Project Folder", {}, FB::openMode | FB::canSelectDirectories, std::move (cb));
        };

        commandHost.chooseProjectSaveLocation = [this] (auto cb)
        {
            choose ("Save Project As (creates a folder)", {}, FB::saveMode | FB::canSelectFiles, std::move (cb));
        };

        commandHost.choosePluginFile = [this] (auto cb)
        {
            choose ("Locate Plug-in", "*.vst3;*.component;*.clap",
                    FB::openMode | FB::canSelectFiles | FB::canSelectDirectories, std::move (cb));
        };

        // Errors are toasts, not modal dialogs (PRD §16.7); before the window exists, a dialog.
        commandHost.reportError = [this] (const juce::String& message)
        {
            if (mainWindow != nullptr)
                mainWindow->showToast (message, false, true);
            else
                reportError (message);
        };

        commandHost.notify = [this] (const juce::String& message, bool undoable)
        {
            if (mainWindow != nullptr)
                mainWindow->showToast (message, undoable);
        };
    }

    static void reportError (const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Resamper", message);
    }
};

} // namespace resamper

// START_JUCE_APPLICATION, except that a plug-in scan worker (this executable,
// run again by PluginScanner) scans and exits, and a sandbox host (run again
// by PluginSandbox) serves its one plug-in, without starting the app.
JUCE_CREATE_APPLICATION_DEFINE (resamper::ResamperApplication)

int main (int argc, char* argv[])
{
    if (resamper::PluginScanner::isWorker (argc, argv))
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        return resamper::PluginScanner::runWorker (argc, argv);
    }

    if (resamper::PluginSandbox::isHost (argc, argv))
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        return resamper::PluginSandbox::runHost (argc, argv);
    }

    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main (argc, (const char**) argv);
}
