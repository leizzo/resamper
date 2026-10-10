#include "Library.h"
#include "Commands/AppCommands.h"
#include "Commands/PluginCommands.h"

namespace resamper
{

namespace
{
    /** How many files a search lists at most. */
    constexpr int maxSearchResults = 500;

    const juce::String audioExtensions { "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3" };

    bool matches (const juce::String& name, const juce::String& search)
    {
        return search.isEmpty() || name.containsIgnoreCase (search.trim());
    }
}

Library::Library (std::function<juce::Array<PluginInfo>()> c, juce::File r)
    : catalogue (std::move (c)), root (std::move (r))
{
}

juce::File Library::defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Resamper");
}

juce::String Library::nameOf (LibraryCategory c)
{
    switch (c)
    {
        case LibraryCategory::sounds:        return NEEDS_TRANS ("Sounds");
        case LibraryCategory::drums:         return NEEDS_TRANS ("Drums");
        case LibraryCategory::instruments:   return NEEDS_TRANS ("Instruments");
        case LibraryCategory::audioEffects:  return NEEDS_TRANS ("Audio Effects");
        case LibraryCategory::midiEffects:   return NEEDS_TRANS ("MIDI Effects");
        case LibraryCategory::plugins:       return NEEDS_TRANS ("Plug-Ins");
        case LibraryCategory::clips:         return NEEDS_TRANS ("Clips");
        case LibraryCategory::samples:       return NEEDS_TRANS ("Samples");
    }

    return {};
}

bool Library::isFileCategory (LibraryCategory c)
{
    return c == LibraryCategory::sounds || c == LibraryCategory::drums || c == LibraryCategory::clips
        || c == LibraryCategory::samples;
}

bool Library::isAudioFile (const juce::File& f)
{
    return f.existsAsFile() && f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

juce::File Library::folderFor (LibraryCategory c) const
{
    if (! isFileCategory (c))
        return {};

    auto folder = root.getChildFile (nameOf (c));
    folder.createDirectory();
    return folder;
}

std::vector<LibraryItem> Library::list (LibraryCategory category, const juce::File& folder, const juce::String& search) const
{
    std::vector<LibraryItem> items;

    if (! isFileCategory (category))
    {
        for (auto& info : catalogue())
        {
            // What a plug-in that failed to scan is, nobody knows: it is listed under Plugins only.
            const auto wanted = category == LibraryCategory::plugins      ? info.external
                              : info.failedScan                           ? false
                              : category == LibraryCategory::instruments  ? info.instrument
                              : category == LibraryCategory::midiEffects  ? info.midiEffect
                                                                          : ! info.instrument && ! info.midiEffect;

            if (wanted && matches (info.name, search))
                items.push_back ({ LibraryItem::Kind::plugin, info.name, info.path, {}, info.instrument, info.midiEffect,
                                   info.external ? info.format : juce::String(), info.failedScan });
        }

        std::sort (items.begin(), items.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        return items;
    }

    const auto dir = folder.isDirectory() ? folder : folderFor (category);

    if (search.trim().isNotEmpty())
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, true, audioExtensions, juce::File::findFiles))
        {
            if (matches (entry.getFile().getFileNameWithoutExtension(), search))
                items.push_back ({ LibraryItem::Kind::audioFile, entry.getFile().getFileNameWithoutExtension(), {}, entry.getFile() });

            if ((int) items.size() >= maxSearchResults)
                break;
        }
    }
    else
    {
        for (auto& sub : dir.findChildFiles (juce::File::findDirectories, false))
            items.push_back ({ LibraryItem::Kind::folder, sub.getFileName(), {}, sub });

        for (auto& file : dir.findChildFiles (juce::File::findFiles, false, audioExtensions))
            items.push_back ({ LibraryItem::Kind::audioFile, file.getFileNameWithoutExtension(), {}, file });
    }

    std::stable_sort (items.begin(), items.end(), [] (auto& a, auto& b)
    {
        if ((a.kind == LibraryItem::Kind::folder) != (b.kind == LibraryItem::Kind::folder))
            return a.kind == LibraryItem::Kind::folder;

        return a.name.compareIgnoreCase (b.name) < 0;
    });

    return items;
}

juce::var dragDescription (const LibraryItem& item)
{
    auto d = new juce::DynamicObject();
    d->setProperty ("libraryItem", item.kind == LibraryItem::Kind::plugin ? "plugin" : "audioFile");
    d->setProperty ("name", item.name);
    d->setProperty ("pluginPath", item.pluginPath);
    d->setProperty ("file", item.file.getFullPathName());
    d->setProperty ("instrument", item.instrument);
    d->setProperty ("midiEffect", item.midiEffect);
    d->setProperty ("format", item.format);
    d->setProperty ("failedScan", item.failedScan);
    return d;
}

std::optional<LibraryItem> itemFromDrag (const juce::var& description)
{
    const auto kind = description["libraryItem"].toString();

    if (kind != "plugin" && kind != "audioFile")
        return {};

    LibraryItem item;
    item.kind = kind == "plugin" ? LibraryItem::Kind::plugin : LibraryItem::Kind::audioFile;
    item.name = description["name"].toString();
    item.pluginPath = description["pluginPath"].toString();
    item.file = juce::File (description["file"].toString());
    item.instrument = description["instrument"];
    item.midiEffect = description["midiEffect"];
    item.format = description["format"].toString();
    item.failedScan = description["failedScan"];
    return item;
}

bool canDropOnTrack (const LibraryItem& item, TrackKind kind)
{
    if (item.failedScan)
        return false;

    return item.kind == LibraryItem::Kind::plugin || (item.kind == LibraryItem::Kind::audioFile && kind == TrackKind::audio);
}

void dropOnTrack (CommandRegistry& commands, const LibraryItem& item, const juce::String& trackId, double startSeconds)
{
    if (item.failedScan)
        return;

    if (item.kind == LibraryItem::Kind::plugin)
        commands.invoke (cmd::pluginInsert, { trackId, item.pluginPath, PluginChain::device });
    else if (item.kind == LibraryItem::Kind::audioFile)
        commands.invoke (cmd::clipInsertAt, { item.file, trackId, startSeconds });
}

} // namespace resamper
