#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace resamper::test
{

/** A plug-in format for scan and sandbox tests: a "plug-in" is a file whose
    text says how its scan goes. "ok <name>" finds one effect called name;
    "crash" kills the scanning process, as a crashing plug-in does; "hang"
    never returns. Files end in fileExtension and are found in the folder
    given to the constructor. What "ok" finds can't be created.

    "plugin <name>" finds an effect that can be created: a gain (its "Gain"
    parameter, 0.5 at first, is its state) reporting pluginLatency samples of
    latency, whose "Crash" parameter, once on, kills the process at its next
    audio block, and whose editor is editorWidth x editorHeight; Up typed in the
    editor drags Gain up by dragSteps x dragStep, as a mouse drag would. "sandboxcrash <name>" is the same, except that it kills a
    sandbox host while loading (inSandboxHost) and loads in-process. */
class TestPluginFormat : public juce::AudioPluginFormat
{
public:
    explicit TestPluginFormat (juce::File folder = {});

    static constexpr const char* formatName = "ResamperTest";
    static constexpr const char* fileExtension = ".resampertest";
    static constexpr int pluginLatency = 64, editorWidth = 300, editorHeight = 160, sluggishLoadMs = 1500;
    static constexpr int dragSteps = 30, dragStepMs = 20;
    static constexpr float dragStep = 0.01f;   ///< Gain's own step: smaller ones snap back

    /** The folder the format registerWith adds scans: one per run. */
    static juce::File scanFolder();

    /** Adds the format, scanning scanFolder, to a format manager that has none of that name. */
    static void registerWith (juce::AudioPluginFormatManager&);

    /** Set in a sandbox host process (TestMain). */
    static inline bool inSandboxHost = false;

    juce::String getName() const override   { return formatName; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String& fileOrIdentifier) override;
    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override   { return false; }
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override   { return true; }
    bool isTrivialToScan() const override   { return false; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive, bool allowAsync) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override   { return juce::FileSearchPath (folder.getFullPathName()); }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override   { return false; }

private:
    juce::File folder;

    void createPluginInstance (const juce::PluginDescription&, double, int, PluginCreationCallback) override;
};

} // namespace resamper::test
