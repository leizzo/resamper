#pragma once

#include "Engine/ApplicationModel.h"
#include "Engine/PluginRack.h"

#include <functional>
#include <optional>
#include <vector>

namespace resamper
{

/** The Browser's categories (PRD §6.2), in display order. */
enum class LibraryCategory { sounds, drums, instruments, audioEffects, midiEffects, plugins, clips, samples };

/** One row of the Browser's list. */
struct LibraryItem
{
    enum class Kind { folder, plugin, audioFile };

    Kind kind = Kind::plugin;
    juce::String name;
    juce::String pluginPath;    ///< a plug-in: what plugin.insert takes
    juce::File file;            ///< a folder or an audio file
    bool instrument = false;
    bool midiEffect = false;
    juce::String format {};     ///< a scanned plug-in's format (VST3, AudioUnit, ...); empty for a native device
    bool failedScan = false;    ///< a plug-in that failed to scan: listed, never inserted, only retried

    /** A scanned plug-in, not a native device. */
    bool isPlugin() const   { return kind == Kind::plugin && format.isNotEmpty(); }

    /** The format badge of a plug-in's row (VST3, AU, CLAP); empty for a native device. */
    juce::String formatBadge() const   { return format == "AudioUnit" ? juce::String ("AU") : format; }
};

/** What the Browser lists. Instruments, Audio Effects, MIDI Effects and
    Plugins come from the Plugin Catalogue (plug-ins that failed to scan only
    under Plugins); Sounds, Drums, Clips and Samples are
    folders of audio under the library root (created on first use), browsed
    folder by folder. */
class Library
{
public:
    Library (std::function<juce::Array<PluginInfo>()> catalogue, juce::File root);

    /** ~/Music/Resamper. */
    static juce::File defaultRoot();

    /** The category's English name: its folder under the root, and its key in the UI Language. */
    static juce::String nameOf (LibraryCategory);
    static bool isFileCategory (LibraryCategory);
    static bool isAudioFile (const juce::File&);

    /** The folder behind a file category (created if missing); none for a plug-in category. */
    juce::File folderFor (LibraryCategory) const;

    /** A category's items in folder (none: the category's own folder), filtered by
        search (case-insensitive, by name). Searching a file category looks
        through every subfolder and lists matching files only. Folders come first. */
    std::vector<LibraryItem> list (LibraryCategory, const juce::File& folder, const juce::String& search) const;

private:
    std::function<juce::Array<PluginInfo>()> catalogue;
    juce::File root;
};

/** A Browser item as a drag-and-drop description, and back (empty if the drag isn't one). */
juce::var dragDescription (const LibraryItem&);
std::optional<LibraryItem> itemFromDrag (const juce::var&);

/** A device goes on any track's device chain; a sample only onto an audio track.
    A plug-in that failed to scan goes nowhere. */
bool canDropOnTrack (const LibraryItem&, TrackKind);

class CommandRegistry;

/** Drops an item on a track: a device at the end of its device chain, a
    sample as a clip at startSeconds. */
void dropOnTrack (CommandRegistry&, const LibraryItem&, const juce::String& trackId, double startSeconds);

} // namespace resamper
