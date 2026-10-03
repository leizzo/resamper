#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <memory>

namespace resamper
{

/** Watches a folder for .json updates via gin::FileSystemWatcher.

    MainComponent watches the dev-mode themes folder and invokes
    dev.reloadTheme. Callbacks arrive on the message thread.
*/
class ThemeWatcher
{
public:
    explicit ThemeWatcher (const juce::File& folder);
    ~ThemeWatcher();

    /** Called when a watched .json file is updated. */
    std::function<void (const juce::File&)> onJsonUpdated;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThemeWatcher)
};

} // namespace resamper
