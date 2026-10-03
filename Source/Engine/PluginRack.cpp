#include "PluginRack.h"
#include "EditTracks.h"
#include "EngineManager.h"
#include "PluginHostingImpl.h"
#include "PluginSandbox.h"
#include "PluginScanner.h"
#include "NativeDevicePlugins.h"
#include "ProjectManager.h"

#include <tracktion_engine/tracktion_engine.h>

#include <map>

namespace te = tracktion;

namespace resamper
{

namespace
{
    /** On a mixer insert's state: "mixer". Absent: the device chain (so older projects' inserts land there). */
    const juce::Identifier chainProperty { "resamperChain" };
    const juce::String mixerChainValue { "mixer" };

    /** On an external plug-in's state: its pinned parameter ids, one per line. */
    const juce::Identifier pinsProperty { "resamperPins" };

    juce::StringArray pinsOf (const te::Plugin& plugin)
    {
        return juce::StringArray::fromLines (plugin.state[pinsProperty].toString());
    }

    /** On a native device's state: "folded" or "expanded"; absent is compact. */
    const juce::Identifier sizeProperty { "resamperSize" };

    DeviceSize sizeOf (const te::Plugin& plugin)
    {
        const auto value = plugin.state[sizeProperty].toString();
        return value == "folded" ? DeviceSize::folded : value == "expanded" ? DeviceSize::expanded : DeviceSize::compact;
    }

    /** On a plug-in's state: the preset last chosen or saved in its window. */
    const juce::Identifier presetProperty { "resamperPreset" };

    /** On an external plug-in's state: the A/B slot in use (1 = B; absent is A),
        and the other slot's plug-in state, base64. */
    const juce::Identifier abSlotProperty { "resamperABSlot" };
    const juce::Identifier abOtherProperty { "resamperABOther" };

    /** On a plug-in's state: its window (§20 Plugin.window). Absent x / y: not placed yet. */
    const juce::Identifier windowOpenProperty { "resamperWindowOpen" };
    const juce::Identifier windowPinnedProperty { "resamperWindowPinned" };
    const juce::Identifier windowXProperty { "resamperWindowX" };
    const juce::Identifier windowYProperty { "resamperWindowY" };
    const juce::Identifier windowScaleProperty { "resamperWindowScale" };

    /** The file extension of a preset savePreset writes. */
    const juce::String presetExtension { ".resamperpreset" };

    /** A built-in's Mix and Out parameters, by type: the last zone of its card (§9.2.1a). */
    bool isOutputParameter (const juce::String& pluginType, const juce::String& parameterId)
    {
        static const std::map<juce::String, juce::StringArray> outputs {
            { te::ReverbPlugin::xmlTypeName,     { "wet level", "dry level" } },
            { te::CompressorPlugin::xmlTypeName, { "output gain" } },
            { te::DelayPlugin::xmlTypeName,      { "mix proportion" } },
            { te::FourOscPlugin::xmlTypeName,    { "masterLevel" } },
            { EqEightPlugin::xmlTypeName,        { "scale", "output" } },
            { CompressorV2Plugin::xmlTypeName,   { "makeupAuto", "makeup", "mix", "output" } },
        };

        auto found = outputs.find (pluginType);
        return found != outputs.end() && found->second.contains (parameterId);
    }

    PluginChain chainOf (const te::Plugin& plugin)
    {
        return plugin.state[chainProperty].toString() == mixerChainValue ? PluginChain::mixer : PluginChain::device;
    }

    bool isMidiEffectType (const juce::String& type)
    {
        return type == te::MidiModifierPlugin::xmlTypeName || type == te::MidiPatchBayPlugin::xmlTypeName;
    }

    bool isMidiEffect (te::Plugin& plugin)
    {
        if (isMidiEffectType (plugin.getPluginType()))
            return true;

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
            if (auto* instance = external->getAudioPluginInstance())
                return instance->isMidiEffect();

        return false;
    }

    //==============================================================================
    /** Built-ins registered in PluginManager::initialise. VCA has no getPluginName(),
        and audio tracks reject it, so it stays out of the catalogue. ReWire is off. */
    template <typename PluginClass>
    PluginInfo builtIn (bool synth)
    {
        const auto desc = te::PluginManager::createBuiltInPluginDescription<PluginClass> (synth);

        PluginInfo info;
        info.name = desc.name;
        info.manufacturer = desc.manufacturerName;
        info.format = desc.pluginFormatName;
        info.path = desc.fileOrIdentifier;
        info.category = desc.category;
        info.instrument = synth;
        info.midiEffect = isMidiEffectType (info.path);
        return info;
    }

    /** A v2 native device of Resamper's own (PRD §9.2.1a). */
    template <typename PluginClass>
    PluginInfo nativeV2()
    {
        auto info = builtIn<PluginClass> (false);
        info.manufacturer = "Resamper";
        return info;
    }

    /** The built-ins a user can put on a track. Engine plumbing (the fader,
        meters, aux sends and returns, freeze points, patch bays, text) is added
        by the app where it belongs, never from the catalogue. EQ Eight and
        Compressor v2 replace the engine's equaliser and compressor (v1). */
    const juce::Array<PluginInfo>& builtInCatalogue()
    {
        static const auto catalogue = []
        {
            juce::Array<PluginInfo> list;
            list.add (nativeV2<EqEightPlugin>());
            list.add (builtIn<te::ReverbPlugin> (false));
            list.add (nativeV2<CompressorV2Plugin>());
            list.add (builtIn<te::ChorusPlugin> (false));
            list.add (builtIn<te::DelayPlugin> (false));
            list.add (builtIn<te::PhaserPlugin> (false));
            list.add (builtIn<te::PitchShiftPlugin> (false));
            list.add (builtIn<te::LowPassPlugin> (false));
            list.add (builtIn<te::SamplerPlugin> (true));
            list.add (builtIn<te::FourOscPlugin> (true));
            list.add (builtIn<te::MidiModifierPlugin> (false));
            return list;
        }();

        return catalogue;
    }

    bool isBuiltInType (const juce::String& type)
    {
        for (const auto& info : builtInCatalogue())
            if (info.path == type)
                return true;

        // The v1 equaliser and compressor are out of the catalogue, but a
        // project or a Command that names them still gets them.
        return type == te::EqualiserPlugin::xmlTypeName || type == te::CompressorPlugin::xmlTypeName;
    }

    PluginInfo infoFromDescription (const juce::PluginDescription& desc)
    {
        PluginInfo info;
        info.name = desc.name;
        info.manufacturer = desc.manufacturerName;
        info.format = desc.pluginFormatName;
        info.path = desc.fileOrIdentifier.isNotEmpty() ? desc.fileOrIdentifier
                                                       : desc.createIdentifierString();
        info.category = desc.category;
        info.instrument = desc.isInstrument;
        info.external = true;
        return info;
    }

    PluginInfo infoFromPlugin (te::Plugin& plugin)
    {
        PluginInfo info;
        info.id = plugin.itemID.toString();
        info.name = plugin.getName();
        info.manufacturer = plugin.getVendor();
        info.instrument = plugin.isSynth();
        info.path = plugin.getPluginType();

        if (auto* external = dynamic_cast<te::ExternalPlugin*> (&plugin))
        {
            info.format = external->desc.pluginFormatName;
            info.path = external->desc.fileOrIdentifier.isNotEmpty() ? external->desc.fileOrIdentifier
                                                                     : external->desc.createIdentifierString();
            info.category = external->desc.category;
            info.instrument = external->desc.isInstrument;
            info.version = external->desc.version;
            info.external = true;
            info.pinnedParameters = pinsOf (plugin);
            info.pinnedParameters.removeEmptyStrings();
        }
        else
        {
            info.format = te::PluginManager::builtInPluginFormatName;
            info.category = info.instrument ? "Synth" : "Effect";
            info.size = sizeOf (plugin);
        }

        info.midiEffect = isMidiEffect (plugin);
        info.chain = chainOf (plugin);
        info.enabled = plugin.isEnabled();
        info.presetName = plugin.state[presetProperty].toString();
        info.abSlot = (int) plugin.state[abSlotProperty] == 1 ? 1 : 0;

        if (auto* track = plugin.getOwnerTrack())
            info.trackId = track->itemID.toString();

        info.latencySamples = juce::roundToInt (plugin.getLatencySeconds() * plugin.engine.getDeviceManager().getSampleRate());
        return info;
    }

    /** Routing, not a chain member: aux sends and returns, and meters. */
    bool isRouting (te::Plugin& plugin)
    {
        return dynamic_cast<te::AuxSendPlugin*> (&plugin) != nullptr
            || dynamic_cast<te::AuxReturnPlugin*> (&plugin) != nullptr
            || dynamic_cast<te::LevelMeterPlugin*> (&plugin) != nullptr;
    }

    /** Index of the track fader: the volume plug-in. The chains are before it. */
    int faderIndex (te::Track& track)
    {
        if (auto* volume = faderOf (track))
            if (const int index = track.pluginList.indexOf (volume); index >= 0)
                return index;

        return track.pluginList.size();
    }

    /** A track's two chains, in signal order. */
    struct Chains
    {
        te::Track* track = nullptr;   ///< an audio track or a Bus
        std::vector<te::Plugin::Ptr> device, mixer;

        std::vector<te::Plugin::Ptr>& operator[] (PluginChain c)   { return c == PluginChain::mixer ? mixer : device; }

        te::Plugin::Ptr find (const juce::String& pluginId) const
        {
            for (auto* list : { &device, &mixer })
                for (auto& plugin : *list)
                    if (plugin->itemID.toString() == pluginId)
                        return plugin;

            return {};
        }

        /** Where a plug-in appended to a chain goes in the track's plug-in list:
            after that chain's last member; for an empty chain, after the device
            chain (a mixer insert) and before the next send or the fader. */
        int endIndex (PluginChain chain) const
        {
            auto& list = track->pluginList;
            auto& members = chain == PluginChain::mixer ? mixer : device;

            if (! members.empty())
                return list.indexOf (members.back().get()) + 1;

            int start = 0;

            if (chain == PluginChain::mixer && ! device.empty())
                start = list.indexOf (device.back().get()) + 1;

            const int fader = faderIndex (*track);

            for (int i = start; i < fader; ++i)
            {
                auto* plugin = list[i];

                if (dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr
                    || (chain == PluginChain::device && chainOf (*plugin) == PluginChain::mixer))
                    return i;
            }

            return fader;
        }

        /** Where a plug-in at position index of a chain goes (past the end: endIndex). */
        int indexFor (PluginChain chain, int index) const
        {
            auto& members = chain == PluginChain::mixer ? mixer : device;

            if (juce::isPositiveAndBelow (index, (int) members.size()))
                return track->pluginList.indexOf (members[(size_t) index].get());

            return endIndex (chain);
        }
    };

    Chains chainsFor (te::Edit& edit, const juce::String& trackId)
    {
        Chains chains;
        chains.track = findStripTrack (edit, trackId);

        if (chains.track == nullptr)
            return chains;

        const int end = faderIndex (*chains.track);

        for (int i = 0; i < end; ++i)
            if (auto* plugin = chains.track->pluginList[i]; plugin != nullptr && ! isRouting (*plugin))
                chains[chainOf (*plugin)].push_back (plugin);

        return chains;
    }

    juce::String mixerRefusal (bool instrument, bool midiEffect)
    {
        if (instrument || midiEffect)
            return "Mixer inserts take effects only";

        return {};
    }

    bool findExternal (te::Engine& engine, const juce::String& typeOrIdentifier, juce::PluginDescription& out)
    {
        auto& list = engine.getPluginManager().knownPluginList;

        if (auto match = list.getTypeForIdentifierString (typeOrIdentifier))
        {
            out = *match;
            return true;
        }

        if (auto match = list.getTypeForFile (typeOrIdentifier))
        {
            out = *match;
            return true;
        }

        for (const auto& desc : list.getTypes())
            if (desc.fileOrIdentifier == typeOrIdentifier || desc.createIdentifierString() == typeOrIdentifier)
            {
                out = desc;
                return true;
            }

        return false;
    }

    /** A plug-in that failed to scan, as the catalogue lists it: named after its file. */
    PluginInfo failedInfo (juce::AudioPluginFormat& format, const juce::String& fileOrIdentifier)
    {
        PluginInfo info;
        info.name = format.getNameOfPluginFromIdentifier (fileOrIdentifier);

        if (info.name.isEmpty() || info.name == fileOrIdentifier)
            info.name = juce::File::isAbsolutePath (fileOrIdentifier) ? juce::File (fileOrIdentifier).getFileNameWithoutExtension()
                                                                     : fileOrIdentifier.fromLastOccurrenceOf ("/", false, false);

        info.format = format.getName();
        info.path = fileOrIdentifier;
        info.external = true;
        info.failedScan = true;
        return info;
    }
}

//==============================================================================
struct PluginRack::ScanThread : juce::Thread
{
    explicit ScanThread (PluginRack& owner) : juce::Thread ("Plugin Scan"), rack (owner) {}

    void run() override { rack.runScan(); }

    PluginRack& rack;
};

PluginRack::PluginRack (ProjectManager& pm)
    : projectManager (pm)
{
    installScanner (PluginScanner::defaultTimeoutMs);
    publishExternalSnapshot();
}

PluginRack::~PluginRack()
{
    stopScan();
}

void PluginRack::stopScan()
{
    if (scanThread == nullptr)
        return;

    scanThread->signalThreadShouldExit();

    // An AU is created on the message thread while the scan waits for it: blocking
    // that thread here would stall the scan until the timeout killed it mid-call.
    auto* messages = juce::MessageManager::getInstanceWithoutCreating();
    const auto deadline = juce::Time::getMillisecondCounter() + scanStopTimeoutMs;

    if (messages != nullptr && messages->isThisTheMessageThread())
        while (scanThread->isThreadRunning() && juce::Time::getMillisecondCounter() < deadline)
            messages->runDispatchLoopUntil (10);

    scanThread->stopThread (scanStopTimeoutMs);
    scanThread.reset();
}

juce::Array<PluginInfo> PluginRack::getCatalogue() const
{
    auto catalogue = builtInCatalogue();

    const juce::ScopedLock sl (snapshotLock);
    catalogue.addArray (externalSnapshot);
    return catalogue;
}

void PluginRack::installScanner (int timeoutMs)
{
    auto& manager = projectManager.getEdit().engine.getPluginManager();

    // Tracktion's own scanner goes, and with it what its abort hook points at.
    // A scan here is stopped by stopping its thread.
    manager.abortCurrentPluginScan = [] {};
    manager.knownPluginList.setCustomScanner (PluginScanner::createScanner (timeoutMs));
}

void PluginRack::publishExternalSnapshot()
{
    juce::Array<PluginInfo> scanned;
    auto& manager = projectManager.getEdit().engine.getPluginManager();

    for (const auto& desc : manager.knownPluginList.getTypes())
    {
        if (te::PluginManager::isBuiltInPlugin (desc))
            continue;

        scanned.add (infoFromDescription (desc));
    }

    // The list keeps the files whose scan failed in its blacklist.
    for (const auto& path : manager.knownPluginList.getBlacklistedFiles())
        for (auto* format : manager.pluginFormatManager.getFormats())
            if (scanFormats.contains (format->getName()) && format->fileMightContainThisPluginType (path))
            {
                scanned.add (failedInfo (*format, path));
                break;
            }

    const juce::ScopedLock sl (snapshotLock);
    externalSnapshot = std::move (scanned);
}

void PluginRack::startScan()
{
    if (! scanning.load())
        startScanThread ({});
}

juce::Result PluginRack::retryScan (const juce::String& path)
{
    if (! failedToScan (path))
        return juce::Result::fail ("That plug-in didn't fail to scan");

    if (scanning.load())
        return juce::Result::fail ("A plug-in scan is in progress");

    startScanThread (path);
    return juce::Result::ok();
}

bool PluginRack::failedToScan (const juce::String& path) const
{
    return path.isNotEmpty()
        && projectManager.getEdit().engine.getPluginManager().knownPluginList.getBlacklistedFiles().contains (path);
}

void PluginRack::startScanThread (const juce::String& onlyPath)
{
    stopScan();

    retryPath = onlyPath;
    scanning.store (true);
    scanBodyRanOffCaller.store (false);
    scanCallerId = juce::Thread::getCurrentThreadId();
    scanThread = std::make_unique<ScanThread> (*this);

    if (! scanThread->startThread())
    {
        scanning.store (false);
        scanThread.reset();
    }
}

bool PluginRack::isScanning() const
{
    return scanning.load();
}

void PluginRack::runScan()
{
    struct ClearWhenDone
    {
        std::atomic<bool>& flag;
        ~ClearWhenDone() { flag.store (false); }
    } clearWhenDone { scanning };

    // The body itself never runs on startScan's thread. This is set before any disk walk.
    if (juce::Thread::getCurrentThreadId() != scanCallerId)
        scanBodyRanOffCaller.store (true);

    auto shouldStop = [this]
    {
        return scanThread != nullptr && scanThread->threadShouldExit();
    };

    auto& manager = projectManager.getEdit().engine.getPluginManager();
    auto& formats = manager.pluginFormatManager;

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        if (shouldStop())
            break;

        auto* format = formats.getFormat (i);

        if (format == nullptr || ! scanFormats.contains (format->getName()))
            continue;

        // A retry scans its one file, and only with a format that can hold it.
        if (retryPath.isNotEmpty() && ! format->fileMightContainThisPluginType (retryPath))
            continue;

        if (retryPath.isNotEmpty())
            manager.knownPluginList.removeFromBlacklist (retryPath);

        const auto files = retryPath.isNotEmpty() ? juce::StringArray (retryPath)
                                                  : format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, false);

        for (const auto& file : files)
        {
            if (shouldStop())
                break;

            juce::OwnedArray<juce::PluginDescription> found;
            manager.knownPluginList.scanAndAddFile (file, true, found, *format);
        }
    }

    // Plug-in Hosting hears the catalogue change, and starts the missing plug-ins the scan found.
    manager.knownPluginList.scanFinished();
    publishExternalSnapshot();
}

juce::StringArray PluginRack::getHostedFormats() const
{
    juce::StringArray names;
    auto& formats = projectManager.getEdit().engine.getPluginManager().pluginFormatManager;

    for (int i = 0; i < formats.getNumFormats(); ++i)
        if (auto* format = formats.getFormat (i))
            names.addIfNotAlreadyThere (format->getName());

    return names;
}

juce::Result PluginRack::insert (const juce::String& trackId, const juce::String& typeOrIdentifier, PluginChain chain,
                                 juce::String* addedId)
{
    auto& edit = projectManager.getEdit();
    auto* track = findStripTrack (edit, trackId);

    if (track == nullptr)
        return juce::Result::fail ("No track with that id");

    if (typeOrIdentifier.isEmpty())
        return juce::Result::fail ("No plug-in was specified");

    const bool builtIn = isBuiltInType (typeOrIdentifier);
    juce::PluginDescription external;
    // Each file is scanned in a worker process, so a running scan doesn't stop an insert.
    const bool haveExternal = ! builtIn && findExternal (edit.engine, typeOrIdentifier, external);

    if (! builtIn && ! haveExternal && failedToScan (typeOrIdentifier))
        return juce::Result::fail ("That plug-in failed to scan: retry it in the Browser");

    if (! builtIn && ! haveExternal)
        return juce::Result::fail ("Unknown plug-in");

    if (chain == PluginChain::mixer)
    {
        if ((int) chainsFor (edit, trackId).mixer.size() >= maxMixerInserts)
            return juce::Result::fail ("A track holds at most " + juce::String (maxMixerInserts) + " mixer inserts");

        // What the catalogue already knows is refused before anything is created.
        for (const auto& info : builtInCatalogue())
            if (info.path == typeOrIdentifier)
                if (auto refusal = mixerRefusal (info.instrument, info.midiEffect); refusal.isNotEmpty())
                    return juce::Result::fail (refusal);

        if (haveExternal && external.isInstrument)
            return juce::Result::fail (mixerRefusal (true, false));
    }

    // Creation writes default parameter state through the Edit undo manager,
    // so it has to land in the same transaction as the insert (and, on a MIDI
    // track, the removal of the built-in synth).
    projectManager.getUndo().beginStep ("Insert Plug-in");

    // A refusal after creation takes back what creation wrote: no stray step.
    auto fail = [this] (const juce::String& why)
    {
        projectManager.getUndo().abandonStep();
        return juce::Result::fail (why);
    };

    te::Plugin::Ptr plugin = builtIn ? edit.getPluginCache().createNewPlugin (typeOrIdentifier, {})
                                     : edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, external);

    if (plugin == nullptr)
        return fail ("Couldn't create the plug-in");

    if (! track->canContainPlugin (plugin.get()))
        return fail ("This track can't hold that plug-in");

    if (chain == PluginChain::mixer)
    {
        if (auto refusal = mixerRefusal (plugin->isSynth(), isMidiEffect (*plugin)); refusal.isNotEmpty())
            return fail (refusal);

        plugin->state.setProperty (chainProperty, mixerChainValue, &edit.getUndoManager());
    }
    else if (isMidi (*track) && plugin->isSynth())
    {
        std::vector<te::Plugin::Ptr> replaced;

        for (auto* existing : track->pluginList)
            if (existing != nullptr && existing->isSynth())
                replaced.push_back (existing);

        for (auto& existing : replaced)
            existing->deleteFromParent();
    }

    track->pluginList.insertPlugin (plugin, chainsFor (edit, trackId).endIndex (chain), nullptr);

    if (track->pluginList.indexOf (plugin.get()) < 0)
        return fail ("Couldn't insert the plug-in");

    lastInsertedId = plugin->itemID.toString();
    lastInsertDepth = edit.getUndoManager().getUndoDescriptions().size();

    if (addedId != nullptr)
        *addedId = lastInsertedId;

    return juce::Result::ok();
}

bool PluginRack::isNewestStepInsertOf (const juce::String& pluginId) const
{
    auto& undo = projectManager.getEdit().getUndoManager();
    return pluginId.isNotEmpty() && pluginId == lastInsertedId && contains (pluginId)
        && undo.getUndoDescriptions().size() == lastInsertDepth
        && undo.getUndoDescription() == "Insert Plug-in";
}

juce::Result PluginRack::replace (const juce::String& trackId, const juce::String& pluginId, const juce::String& typeOrIdentifier,
                                  juce::String* addedId)
{
    auto& edit = projectManager.getEdit();
    auto chains = chainsFor (edit, trackId);
    auto old = chains.find (pluginId);

    if (old == nullptr)
        return juce::Result::fail ("No such plug-in");

    const auto chain = chainOf (*old);
    auto& members = chains[chain];
    const auto index = (int) std::distance (members.begin(), std::find (members.begin(), members.end(), old));

    // The new one goes on the end, then takes the old one's place; both inside
    // insert()'s transaction, so Replace is one undo step. A full mixer chain
    // still has room: the old insert is about to go.
    if (chain == PluginChain::mixer)
        old->state.setProperty (chainProperty, juce::var(), nullptr);

    auto result = insert (trackId, typeOrIdentifier, chain);

    if (chain == PluginChain::mixer)
        old->state.setProperty (chainProperty, mixerChainValue, nullptr);

    if (result.failed())
        return result;

    auto after = chainsFor (edit, trackId);
    auto added = after[chain].back();
    added->removeFromParent();
    after.track->pluginList.insertPlugin (added, chainsFor (edit, trackId).indexFor (chain, index), nullptr);
    old->deleteFromParent();

    if (addedId != nullptr)
        *addedId = added->itemID.toString();

    return juce::Result::ok();
}

bool PluginRack::remove (const juce::String& trackId, const juce::String& pluginId)
{
    auto& edit = projectManager.getEdit();
    auto plugin = chainsFor (edit, trackId).find (pluginId);

    if (pluginId.isEmpty() || plugin == nullptr)
        return false;

    projectManager.getUndo().beginStep ("Remove Plug-in");
    plugin->deleteFromParent();
    return true;
}

bool PluginRack::move (const juce::String& trackId, const juce::String& pluginId, int newIndex)
{
    auto& edit = projectManager.getEdit();
    auto chains = chainsFor (edit, trackId);
    auto plugin = chains.find (pluginId);

    if (plugin == nullptr)
        return false;

    const auto chain = chainOf (*plugin);
    auto& members = chains[chain];
    const auto from = (int) std::distance (members.begin(), std::find (members.begin(), members.end(), plugin));

    if (newIndex < 0 || newIndex >= (int) members.size() || newIndex == from)
        return false;

    projectManager.getUndo().beginStep ("Move Plug-in");
    plugin->removeFromParent();

    auto remaining = chainsFor (edit, trackId);
    chains.track->pluginList.insertPlugin (plugin, remaining.indexFor (chain, newIndex), nullptr);
    return chains.track->pluginList.indexOf (plugin.get()) >= 0;
}

bool PluginRack::setBypassed (const juce::String& trackId, const juce::String& pluginId, bool bypassed)
{
    auto& edit = projectManager.getEdit();
    auto plugin = chainsFor (edit, trackId).find (pluginId);

    if (plugin == nullptr || plugin->isEnabled() == ! bypassed || ! plugin->canBeDisabled())
        return false;

    projectManager.getUndo().beginStep (bypassed ? "Bypass Plug-in" : "Enable Plug-in");
    plugin->setEnabled (! bypassed);
    return true;
}

juce::Result PluginRack::moveToDeviceChain (const juce::String& trackId, const juce::String& pluginId)
{
    auto& edit = projectManager.getEdit();
    auto chains = chainsFor (edit, trackId);
    auto plugin = chains.find (pluginId);

    if (plugin == nullptr || chainOf (*plugin) != PluginChain::mixer)
        return juce::Result::fail ("That plug-in isn't a mixer insert");

    auto& um = edit.getUndoManager();
    projectManager.getUndo().beginStep ("Move to Track Chain");
    plugin->removeFromParent();
    plugin->state.removeProperty (chainProperty, &um);
    chains.track->pluginList.insertPlugin (plugin, chainsFor (edit, trackId).endIndex (PluginChain::device), nullptr);
    return juce::Result::ok();
}

juce::Result PluginRack::copyInsert (const juce::String& fromTrackId, const juce::String& pluginId,
                                     const juce::String& toTrackId, int index)
{
    auto& edit = projectManager.getEdit();
    auto source = chainsFor (edit, fromTrackId).find (pluginId);
    auto target = chainsFor (edit, toTrackId);

    if (source == nullptr || target.track == nullptr)
        return juce::Result::fail ("No such plug-in or track");

    if ((int) target.mixer.size() >= maxMixerInserts)
        return juce::Result::fail ("A track holds at most " + juce::String (maxMixerInserts) + " mixer inserts");

    if (auto refusal = mixerRefusal (source->isSynth(), isMidiEffect (*source)); refusal.isNotEmpty())
        return juce::Result::fail (refusal);

    source->flushPluginStateToValueTree();
    auto state = source->state.createCopy();
    te::EditItemID::remapIDs (state, nullptr, edit);
    state.setProperty (chainProperty, mixerChainValue, nullptr);

    projectManager.getUndo().beginStep ("Copy Plug-in");
    auto copy = edit.getPluginCache().createNewPlugin (state);

    if (copy == nullptr)
        return juce::Result::fail ("Couldn't copy the plug-in");

    target.track->pluginList.insertPlugin (copy, target.indexFor (PluginChain::mixer, juce::jmax (0, index)), nullptr);
    return juce::Result::ok();
}

std::vector<PluginInfo> PluginRack::getChain (const juce::String& trackId, PluginChain chain) const
{
    std::vector<PluginInfo> result;
    auto chains = chainsFor (projectManager.getEdit(), trackId);

    for (auto& plugin : chains[chain])
        result.push_back (infoFromPlugin (*plugin));

    return result;
}

namespace
{
    te::Plugin::Ptr findPlugin (te::Edit& edit, const juce::String& pluginId)
    {
        if (pluginId.isEmpty())
            return {};

        for (auto* track : te::getAllTracks (edit))
            if (isStripTrack (*track))
                for (auto* plugin : track->pluginList)
                    if (plugin->itemID.toString() == pluginId)
                        return plugin;

        return {};
    }

    te::AutomatableParameter::Ptr findParameter (te::Plugin& plugin, const juce::String& parameterId)
    {
        for (auto* parameter : plugin.getAutomatableParameters())
            if (parameter->paramID == parameterId)
                return parameter;

        return {};
    }

    /** One parameter change in Engine Undo. The engine writes parameters into
        the Edit outside its UndoManager, so the change is recorded here, by id:
        undo still finds the plug-in if it was rebuilt from its state. */
    struct ParameterChange : juce::UndoableAction
    {
        ParameterChange (te::Edit& e, juce::String plugin, juce::String parameter, float from, float to)
            : edit (e), pluginId (std::move (plugin)), parameterId (std::move (parameter)), before (from), after (to) {}

        bool perform() override   { return apply (after); }
        bool undo() override      { return apply (before); }

        bool apply (float value)
        {
            if (auto plugin = findPlugin (edit, pluginId))
                if (auto parameter = findParameter (*plugin, parameterId))
                    parameter->setParameter (value, juce::sendNotificationSync);

            return true;
        }

        te::Edit& edit;
        juce::String pluginId, parameterId;
        float before, after;
    };
}

bool PluginRack::contains (const juce::String& pluginId) const
{
    return findPlugin (projectManager.getEdit(), pluginId) != nullptr;
}

std::vector<PluginInfo> PluginRack::getAllPlugins() const
{
    std::vector<PluginInfo> result;
    auto& edit = projectManager.getEdit();

    for (auto* track : te::getAllTracks (edit))
    {
        if (! isStripTrack (*track))
            continue;

        auto chains = chainsFor (edit, track->itemID.toString());

        for (auto* list : { &chains.device, &chains.mixer })
            for (auto& plugin : *list)
                result.push_back (infoFromPlugin (*plugin));
    }

    return result;
}

std::optional<PluginInfo> PluginRack::getPlugin (const juce::String& pluginId) const
{
    auto& edit = projectManager.getEdit();

    // Only a chain member: never the fader, a meter or a send.
    if (auto plugin = findPlugin (edit, pluginId))
        if (auto* track = plugin->getOwnerTrack(); track != nullptr && chainsFor (edit, track->itemID.toString()).find (pluginId) != nullptr)
            return infoFromPlugin (*plugin);

    return std::nullopt;
}

std::vector<PluginParameter> PluginRack::getParameters (const juce::String& pluginId) const
{
    std::vector<PluginParameter> result;

    if (auto plugin = findPlugin (projectManager.getEdit(), pluginId))
    {
        for (auto* parameter : plugin->getAutomatableParameters())
        {
            const auto range = parameter->getValueRange();
            result.push_back ({ parameter->paramID, parameter->getParameterName(), range.getStart(), range.getEnd(),
                                parameter->getCurrentValue(), parameter->getDefaultValue().value_or (range.getStart()),
                                parameter->hasAutomationPoints(),
                                isOutputParameter (plugin->getPluginType(), parameter->paramID),
                                parameter->valueRange });
        }
    }

    return result;
}

juce::String PluginRack::getParameterText (const juce::String& pluginId, const juce::String& parameterId, float value) const
{
    if (auto plugin = findPlugin (projectManager.getEdit(), pluginId))
        if (auto parameter = findParameter (*plugin, parameterId))
            return parameter->valueToString (value);

    return {};
}

bool PluginRack::setParameter (const juce::String& pluginId, const juce::String& parameterId, float value, bool continuesGesture)
{
    auto& edit = projectManager.getEdit();
    auto plugin = findPlugin (edit, pluginId);
    auto parameter = plugin != nullptr ? findParameter (*plugin, parameterId) : nullptr;

    if (parameter == nullptr)
        return false;

    const auto clamped = parameter->valueRange.snapToLegalValue (parameter->getValueRange().clipValue (value));

    if (juce::exactlyEqual (clamped, parameter->getCurrentValue()))
        return false;

    projectManager.getUndo().beginGestureStep ("Change " + parameter->getParameterName(), pluginId + ":" + parameterId, continuesGesture);
    return edit.getUndoManager().perform (new ParameterChange (edit, pluginId, parameterId, parameter->getCurrentValue(), clamped));
}

bool PluginRack::setParameters (const juce::String& pluginId, const std::vector<ParameterValue>& values, bool continuesGesture)
{
    auto& edit = projectManager.getEdit();
    auto plugin = findPlugin (edit, pluginId);

    if (plugin == nullptr)
        return false;

    std::vector<std::pair<te::AutomatableParameter::Ptr, float>> changes;
    juce::StringArray ids;

    for (const auto& v : values)
    {
        auto parameter = findParameter (*plugin, v.parameterId);

        if (parameter == nullptr)
            return false;

        ids.add (v.parameterId);
        const auto clamped = parameter->valueRange.snapToLegalValue (parameter->getValueRange().clipValue (v.value));

        if (! juce::exactlyEqual (clamped, parameter->getCurrentValue()))
            changes.emplace_back (parameter, clamped);
    }

    if (changes.empty())
        return false;

    // The gesture's key names every parameter it sets, so a drag that only
    // happens to change one of them for a moment stays one step.
    projectManager.getUndo().beginGestureStep ("Change " + plugin->getName(), pluginId + ":" + ids.joinIntoString (","), continuesGesture);

    for (auto& [parameter, value] : changes)
        edit.getUndoManager().perform (new ParameterChange (edit, pluginId, parameter->paramID, parameter->getCurrentValue(), value));

    return true;
}

juce::Result PluginRack::setPinned (const juce::String& pluginId, const juce::String& parameterId, bool pinned)
{
    auto& edit = projectManager.getEdit();
    auto plugin = findPlugin (edit, pluginId);

    if (plugin == nullptr || findParameter (*plugin, parameterId) == nullptr)
        return juce::Result::fail ("No such plug-in parameter");

    if (dynamic_cast<te::ExternalPlugin*> (plugin.get()) == nullptr)
        return juce::Result::fail ("Only a plug-in pins parameters; a native device shows them all");

    auto pins = pinsOf (*plugin);
    pins.removeEmptyStrings();

    if (pins.contains (parameterId) == pinned)
        return juce::Result::ok();

    if (pinned && pins.size() >= maxPinnedParameters)
        return juce::Result::fail ("A plug-in card pins at most " + juce::String (maxPinnedParameters) + " parameters");

    if (pinned)
        pins.add (parameterId);
    else
        pins.removeString (parameterId);

    projectManager.getUndo().beginStep (pinned ? "Pin Parameter" : "Unpin Parameter");
    plugin->state.setProperty (pinsProperty, pins.joinIntoString ("\n"), &edit.getUndoManager());
    return juce::Result::ok();
}

namespace
{
    struct ParameterTouchWatch : PluginRack::TouchWatch,
                                 private te::AutomatableParameter::Listener
    {
        ParameterTouchWatch (te::Plugin& plugin, std::function<void (const juce::String&)> callback)
            : onTouch (std::move (callback))
        {
            for (auto* parameter : plugin.getAutomatableParameters())
            {
                parameter->addListener (this);
                parameters.push_back (parameter);
            }
        }

        ~ParameterTouchWatch() override
        {
            for (auto& parameter : parameters)
                parameter->removeListener (this);
        }

        void curveHasChanged (te::AutomatableParameter&) override {}   // required; only a gesture is a touch

        void parameterChangeGestureBegin (te::AutomatableParameter& parameter) override
        {
            if (onTouch)
                onTouch (parameter.paramID);
        }

        std::function<void (const juce::String&)> onTouch;
        std::vector<te::AutomatableParameter::Ptr> parameters;
    };
}

std::unique_ptr<PluginRack::TouchWatch> PluginRack::watchTouches (const juce::String& pluginId,
                                                                  std::function<void (const juce::String&)> onTouch) const
{
    if (auto plugin = findPlugin (projectManager.getEdit(), pluginId))
        return std::make_unique<ParameterTouchWatch> (*plugin, std::move (onTouch));

    return {};
}

juce::Result PluginRack::setSize (const juce::String& pluginId, DeviceSize size)
{
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);

    if (plugin == nullptr)
        return juce::Result::fail ("No such plug-in");

    if (dynamic_cast<te::ExternalPlugin*> (plugin.get()) != nullptr)
        return juce::Result::fail ("A plug-in's card has one size");

    if (size == DeviceSize::compact)
        plugin->state.removeProperty (sizeProperty, nullptr);
    else
        plugin->state.setProperty (sizeProperty, size == DeviceSize::folded ? "folded" : "expanded", nullptr);

    return juce::Result::ok();
}

juce::Result PluginRack::locate (const juce::String& pluginId, const juce::File& file)
{
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    auto& hosting = projectManager.getEngineManager().getPluginHosting().getImpl();

    if (external == nullptr || ! hosting.isMissing (*external))
        return juce::Result::fail ("That plug-in isn't missing");

    if (scanning.load())
        return juce::Result::fail ("A plug-in scan is in progress");

    auto& manager = projectManager.getEdit().engine.getPluginManager();
    auto& formats = manager.pluginFormatManager;
    const auto path = file.getFullPathName();
    bool foundAny = false;

    // Pointing at the file is a retry, even if its scan failed before.
    manager.knownPluginList.removeFromBlacklist (path);

    for (auto* format : formats.getFormats())
    {
        if (format == nullptr || ! format->fileMightContainThisPluginType (path))
            continue;

        juce::OwnedArray<juce::PluginDescription> found;
        manager.knownPluginList.scanAndAddFile (path, true, found, *format);
        foundAny = foundAny || ! found.isEmpty();
    }

    if (! foundAny)
        return juce::Result::fail ("No plug-in in " + file.getFileName());

    publishExternalSnapshot();
    hosting.startMissing (*external);

    if (hosting.isMissing (*external))
        return juce::Result::fail (file.getFileName() + " doesn't hold " + external->desc.name);

    return juce::Result::ok();
}

double PluginRack::getCpuLoad (const juce::String& pluginId) const
{
    if (auto plugin = findPlugin (projectManager.getEdit(), pluginId))
    {
        // A sandboxed plug-in's own cost: the engine's figure includes the round trip to its host.
        if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
            if (auto* instance = external->getAudioPluginInstance(); PluginSandbox::isSandboxed (instance))
                return PluginSandbox::getHostCpuLoad (instance);

        return plugin->getCpuUsage();
    }

    return 0;
}

std::unique_ptr<juce::Component> PluginRack::createEditor (const juce::String& pluginId) const
{
    if (pluginId.isEmpty())
        return {};

    if (auto plugin = findPlugin (projectManager.getEdit(), pluginId))
        if (auto editor = plugin->createEditor())
            return std::unique_ptr<juce::Component> (editor.release());

    return {};
}

std::unique_ptr<juce::Component> PluginRack::createParameterEditor (const juce::String& pluginId) const
{
    if (pluginId.isEmpty())
        return {};

    const auto plugin = findPlugin (projectManager.getEdit(), pluginId);

    if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
        if (auto* instance = external->getAudioPluginInstance())
            return std::make_unique<juce::GenericAudioProcessorEditor> (*instance);

    return {};
}

//==============================================================================
PluginWindowState PluginRack::getWindowState (const juce::String& pluginId) const
{
    PluginWindowState window;

    if (auto plugin = findPlugin (projectManager.getEdit(), pluginId))
    {
        const auto& state = plugin->state;
        window.open = state[windowOpenProperty];
        window.pinned = state[windowPinnedProperty];
        window.placed = state.hasProperty (windowXProperty) && state.hasProperty (windowYProperty);
        window.position = { (int) state[windowXProperty], (int) state[windowYProperty] };
        window.uiScale = (int) state.getProperty (windowScaleProperty, 100);
    }

    return window;
}

juce::Result PluginRack::setWindowState (const juce::String& pluginId, const PluginWindowState& window)
{
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);

    if (plugin == nullptr)
        return juce::Result::fail ("No such plug-in");

    if (getWindowState (pluginId) == window)
        return juce::Result::ok();

    // A view of the plug-in: saved with it, never an undo step.
    auto& state = plugin->state;
    state.setProperty (windowOpenProperty, window.open, nullptr);
    state.setProperty (windowPinnedProperty, window.pinned, nullptr);
    state.setProperty (windowScaleProperty, window.uiScale, nullptr);

    if (window.placed)
    {
        state.setProperty (windowXProperty, window.position.x, nullptr);
        state.setProperty (windowYProperty, window.position.y, nullptr);
    }
    else
    {
        state.removeProperty (windowXProperty, nullptr);
        state.removeProperty (windowYProperty, nullptr);
    }

    return juce::Result::ok();
}

namespace
{
    /** One entry of a plug-in's preset menu: a program of its own, or a saved file. */
    struct PresetEntry
    {
        juce::String name;
        int program = -1;
        juce::File file;
    };

    juce::File presetFolderFor (const juce::File& root, const te::ExternalPlugin& plugin)
    {
        const auto base = root != juce::File() ? root
                                               : juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                                     .getChildFile ("Resamper").getChildFile ("Presets");
        return base.getChildFile (juce::File::createLegalFileName (plugin.desc.manufacturerName + " - " + plugin.desc.name));
    }

    std::vector<PresetEntry> presetsOf (te::ExternalPlugin& plugin, const juce::File& root)
    {
        std::vector<PresetEntry> entries;
        const auto programs = plugin.getNumPrograms();

        // A plug-in without programs still reports one, nameless.
        for (int i = 0; i < programs; ++i)
            if (auto name = plugin.getProgramName (i).trim(); name.isNotEmpty())
                entries.push_back ({ name, i, {} });

        auto files = presetFolderFor (root, plugin).findChildFiles (juce::File::findFiles, false, "*" + presetExtension);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
        });

        for (auto& file : files)
            entries.push_back ({ file.getFileNameWithoutExtension(), -1, file });

        return entries;
    }

    juce::MemoryBlock stateOf (te::ExternalPlugin& plugin)
    {
        juce::MemoryBlock block;

        if (auto* instance = plugin.getAudioPluginInstance())
            instance->getStateInformation (block);

        return block;
    }

    void restoreState (te::ExternalPlugin& plugin, const juce::MemoryBlock& block)
    {
        if (auto* instance = plugin.getAudioPluginInstance(); instance != nullptr && block.getSize() > 0)
            instance->setStateInformation (block.getData(), (int) block.getSize());
    }

    juce::MemoryBlock fromBase64 (const juce::String& text)
    {
        juce::MemoryBlock block;
        block.fromBase64Encoding (text);
        return block;
    }
}

juce::StringArray PluginRack::getPresetNames (const juce::String& pluginId) const
{
    juce::StringArray names;
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);

    if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
        for (auto& entry : presetsOf (*external, presetFolder))
            names.add (entry.name);

    return names;
}

juce::Result PluginRack::selectPreset (const juce::String& pluginId, int index)
{
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());

    if (external == nullptr || external->getAudioPluginInstance() == nullptr)
        return juce::Result::fail ("Only a loaded plug-in has presets");

    const auto entries = presetsOf (*external, presetFolder);

    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return juce::Result::fail ("No such preset");

    const auto& entry = entries[(size_t) index];

    if (entry.program >= 0)
        external->setCurrentProgram (entry.program, true);
    else
        restoreState (*external, fromBase64 (entry.file.loadFileAsString()));

    // Not an undo step: the plug-in keeps its own state outside the Edit, so undo couldn't take the preset back.
    plugin->state.setProperty (presetProperty, entry.name, nullptr);
    return juce::Result::ok();
}

juce::Result PluginRack::savePreset (const juce::String& pluginId, const juce::String& name)
{
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
    const auto legalName = juce::File::createLegalFileName (name.trim());

    if (external == nullptr || external->getAudioPluginInstance() == nullptr)
        return juce::Result::fail ("Only a loaded plug-in saves presets");

    if (legalName.isEmpty())
        return juce::Result::fail ("A preset needs a name");

    auto folder = presetFolderFor (presetFolder, *external);

    if (auto created = folder.createDirectory(); created.failed())
        return created;

    if (! folder.getChildFile (legalName + presetExtension).replaceWithText (stateOf (*external).toBase64Encoding()))
        return juce::Result::fail ("Couldn't write the preset " + legalName);

    // Not an undo step: the preset file stays written, and the name only says which preset is current.
    plugin->state.setProperty (presetProperty, legalName, nullptr);
    return juce::Result::ok();
}

juce::Result PluginRack::selectABSlot (const juce::String& pluginId, int slot)
{
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());

    if (external == nullptr || external->getAudioPluginInstance() == nullptr)
        return juce::Result::fail ("Only a loaded plug-in compares A and B");

    if (slot != 0 && slot != 1)
        return juce::Result::fail ("A/B compare has slots A and B only");

    auto& state = plugin->state;
    const auto current = (int) state[abSlotProperty] == 1 ? 1 : 0;

    if (slot == current)
        return juce::Result::ok();

    // The slot left behind is kept; the other comes back (B starts as a copy of A).
    // Not an undo step: a comparison, and the plug-in's own state lives outside the Edit.
    const auto leaving = stateOf (*external);
    const auto other = state[abOtherProperty].toString();

    if (other.isNotEmpty())
        restoreState (*external, fromBase64 (other));

    state.setProperty (abOtherProperty, leaving.toBase64Encoding(), nullptr);
    state.setProperty (abSlotProperty, slot, nullptr);
    return juce::Result::ok();
}

juce::Result PluginRack::copyAToB (const juce::String& pluginId)
{
    auto plugin = findPlugin (projectManager.getEdit(), pluginId);
    auto* external = dynamic_cast<te::ExternalPlugin*> (plugin.get());

    if (external == nullptr || external->getAudioPluginInstance() == nullptr)
        return juce::Result::fail ("Only a loaded plug-in compares A and B");

    auto& state = plugin->state;

    // On A, B (the kept slot) becomes A; on B, A (the kept slot) is loaded into B.
    // Not an undo step, like selectABSlot: the plug-in's own state lives outside the Edit.
    if ((int) state[abSlotProperty] == 1)
        restoreState (*external, fromBase64 (state[abOtherProperty].toString()));
    else
        state.setProperty (abOtherProperty, stateOf (*external).toBase64Encoding(), nullptr);

    return juce::Result::ok();
}

} // namespace resamper
