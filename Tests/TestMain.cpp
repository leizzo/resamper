#include "TestFixture.h"
#include "TestPluginFormat.h"
#include "Engine/PluginSandbox.h"
#include "Engine/PluginScanner.h"

#include <iostream>

namespace resamper::test
{

namespace
{
    EngineManager* engineManager = nullptr;

    /** Logs to stderr everywhere: JUCE's default goes to the debugger on
        Windows, where a CI run would show nothing. */
    struct ConsoleTestRunner : juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::cerr << message << std::endl;
        }
    };
}

EngineManager& getEngineManager()
{
    jassert (engineManager != nullptr);
    return *engineManager;
}

} // namespace resamper::test

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Run again by a plug-in scan (PluginScanner): scan one file, test plug-ins included.
    if (resamper::PluginScanner::isWorker (argc, argv))
    {
        std::vector<std::unique_ptr<juce::AudioPluginFormat>> formats;
        formats.push_back (std::make_unique<resamper::test::TestPluginFormat>());
        return resamper::PluginScanner::runWorker (argc, argv, std::move (formats));
    }

    // Run again by a sandboxed plug-in (PluginSandbox): host it, test plug-ins included.
    if (resamper::PluginSandbox::isHost (argc, argv))
    {
        resamper::test::TestPluginFormat::inSandboxHost = true;
        std::vector<std::unique_ptr<juce::AudioPluginFormat>> formats;
        formats.push_back (std::make_unique<resamper::test::TestPluginFormat>());
        return resamper::PluginSandbox::runHost (argc, argv, std::move (formats));
    }

    // The whole run shares one headless engine, as Tracktion's own TestRunner does.
    resamper::EngineManager engine ("ResamperTests", resamper::EngineManager::AudioDevice::none);
    resamper::test::engineManager = &engine;

    resamper::test::ConsoleTestRunner runner;
    runner.setAssertOnFailure (false);

    // Optional argument: run only the suites whose name contains it.
    juce::Array<juce::UnitTest*> selected;

    // "--snapshot [name]" renders the UI to PNGs instead (Tests/Snapshots.cpp).
    const auto snapshot = argc >= 2 && juce::String (argv[1]) == "--snapshot";
    const auto filter = snapshot ? (argc >= 3 ? juce::String (argv[2]) : juce::String()) : (argc >= 2 ? juce::String (argv[1]) : juce::String());

    for (auto* t : juce::UnitTest::getTestsInCategory (snapshot ? "Snapshot" : "Resamper"))
        if (filter.isEmpty() || t->getName().containsIgnoreCase (filter))
            selected.add (t);

    runner.runTests (selected);

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    resamper::test::engineManager = nullptr;
    std::cout << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
