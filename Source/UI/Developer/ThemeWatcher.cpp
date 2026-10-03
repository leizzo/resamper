#include "ThemeWatcher.h"

#include <gin/gin.h>

namespace resamper
{

struct ThemeWatcher::Impl : private gin::FileSystemWatcher::Listener
{
    Impl (ThemeWatcher& o, const juce::File& folder)
        : owner (o)
    {
        watcher.addListener (this);
        watcher.addFolder (folder);
    }

    ~Impl() override
    {
        watcher.removeListener (this);
    }

    void fileChanged (const juce::File& file, gin::FileSystemWatcher::FileSystemEvent event) override
    {
        if (event == gin::FileSystemWatcher::fileUpdated
            && file.hasFileExtension (".json")
            && owner.onJsonUpdated)
            owner.onJsonUpdated (file);
    }

    ThemeWatcher& owner;
    gin::FileSystemWatcher watcher;
};

ThemeWatcher::ThemeWatcher (const juce::File& folder)
    : impl (std::make_unique<Impl> (*this, folder))
{
}

ThemeWatcher::~ThemeWatcher() = default;

} // namespace resamper
