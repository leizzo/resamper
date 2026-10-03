#include "Mixer.h"
#include "ApplicationModel.h"
#include "EditTracks.h"
#include "ProjectManager.h"

#include <tracktion_engine/tracktion_engine.h>

#include <algorithm>
#include <functional>
#include <map>

namespace te = tracktion;

namespace resamper
{

namespace
{
    // AuxSendPlugin::isMute() treats a send at or below this as muted.
    constexpr Decibels sendMuteThreshold { -90.0 };

    te::FolderTrack* findBus (te::Edit& edit, const juce::String& id)
    {
        auto* folder = dynamic_cast<te::FolderTrack*> (te::findTrackForID (edit, te::EditItemID::fromString (id)));
        return folder != nullptr && isBus (*folder) ? folder : nullptr;
    }

    te::AuxSendPlugin* findSend (te::Track& track, const juce::String& sendId)
    {
        for (auto* send : track.pluginList.getPluginsOfType<te::AuxSendPlugin>())
            if (send->itemID.toString() == sendId)
                return send;

        return nullptr;
    }

    /** The bus a Return track listens on, or -1 on any other track. */
    int returnBusOf (te::AudioTrack& track)
    {
        const auto returns = track.pluginList.getPluginsOfType<te::AuxReturnPlugin>();
        return returns.isEmpty() ? -1 : returns.getFirst()->busNumber.get();
    }

    /** A track's Output: the nearest Bus above it, else the Master. A Folder-only
        Folder passes its children through. */
    juce::String outputOf (const te::Track& track)
    {
        for (auto* folder = track.getParentFolderTrack(); folder != nullptr; folder = folder->getParentFolderTrack())
            if (isBus (*folder))
                return folder->getName();

        return "Master";
    }

    bool hasReturn (te::Edit& edit, int bus)
    {
        for (auto* track : te::getAudioTracks (edit))
            for (auto* ret : track->pluginList.getPluginsOfType<te::AuxReturnPlugin>())
                if (ret->busNumber.get() == bus)
                    return true;

        return false;
    }

    int nextFreeBus (te::Edit& edit)
    {
        juce::Array<int> used;

        for (auto* track : te::getAudioTracks (edit))
            for (auto* ret : track->pluginList.getPluginsOfType<te::AuxReturnPlugin>())
                used.addIfNotAlreadyThere (ret->busNumber.get());

        for (int bus = 0; ; ++bus)
            if (! used.contains (bus))
                return bus;
    }

    /** The master fader. MasterTrack::pluginList is the master insert chain;
        the fader lives on the Edit as getMasterVolumePlugin(). */
    te::VolumeAndPanPlugin* masterFader (te::Edit& edit)
    {
        return edit.getMasterVolumePlugin().get();
    }

    Decibels dbFromFader (float position)
    {
        return Decibels (te::volumeFaderPositionToDB (position));
    }

    float faderPosition (Decibels volume)
    {
        return te::decibelsToVolumeFaderPosition ((float) juce::jlimit (ApplicationModel::minVolume,
                                                                        ApplicationModel::maxVolume, volume).value);
    }


    /** Makes a still-default value explicit, without undo, so undoing the first
        real change restores it instead of removing the property. */
    void pinDefault (juce::CachedValue<float>& value)
    {
        if (value.isUsingDefault())
            value.getValueTree().setProperty (value.getPropertyID(), value.get(), nullptr);
    }

    void pinVolumeDefaults (te::VolumeAndPanPlugin& plugin)
    {
        pinDefault (plugin.volume);
        pinDefault (plugin.pan);
    }

    /** Undo restores plug-in state, not the live parameter. Push state back into
        the parameter when they differ. Skipped while the value is still the
        default: writing it would record an undo step and clear redo. */
    void syncParameter (te::AutomatableParameter& parameter, const juce::CachedValue<float>& value)
    {
        if (! value.isUsingDefault() && parameter.getCurrentValue() != value.get())
            parameter.updateFromAttachedValue();
    }
}

struct Mixer::MeterState
{
    const void* edit = nullptr;

    struct Slot
    {
        juce::String pluginId;
        te::LevelMeasurer::Client client;
        bool added = false;
        bool rms = false;
    };

    std::map<juce::String, Slot> slots;

    static void detach (te::Edit& edit, Slot& slot);
};

namespace
{
    te::LevelMeterPlugin* meterOnTrack (te::Track& track)
    {
        auto meters = track.pluginList.getPluginsOfType<te::LevelMeterPlugin>();
        return meters.isEmpty() ? nullptr : meters.getLast();
    }

    /** By id in the Edit's plug-in cache, so a meter whose track was deleted, and may
        come back on undo, is still found. */
    te::LevelMeterPlugin* findMeterById (te::Edit& edit, const juce::String& pluginId)
    {
        auto plugin = edit.getPluginCache().getPluginFor (te::EditItemID::fromString (pluginId));
        return dynamic_cast<te::LevelMeterPlugin*> (plugin.get());
    }

    StereoLevel readPeaks (te::LevelMeasurer::Client& client)
    {
        const auto floor = (float) ApplicationModel::minVolume.value;
        const int channels = juce::jlimit (1, 8, juce::jmax (1, client.getNumChannelsUsed()));
        StereoLevel level { floor, floor };
        level.left = juce::jmax (floor, client.getAndClearAudioLevel (0).dB);
        level.right = channels > 1 ? juce::jmax (floor, client.getAndClearAudioLevel (1).dB) : level.left;
        return level;
    }
}

void Mixer::MeterState::detach (te::Edit& edit, Slot& slot)
{
    if (slot.added)
        if (auto* meter = findMeterById (edit, slot.pluginId))
            meter->measurer.removeClient (slot.client);

    slot.client.reset();
    slot.added = false;
    slot.pluginId.clear();
}

juce::String returnLetterFor (int bus)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + juce::jlimit (0, 25, bus)));
}

Mixer::Mixer (ProjectManager& pm, const ApplicationModel& m, const PluginRack& p)
    : projects (pm), model (m), plugins (p), meters (std::make_unique<MeterState>())
{
}

Mixer::~Mixer()
{
    auto& edit = projects.getEdit();

    for (auto& [id, slot] : meters->slots)
        MeterState::detach (edit, slot);
}

StereoLevel Mixer::levelOf (const juce::String& slotId, void* meterPlugin)
{
    auto* meter = static_cast<te::LevelMeterPlugin*> (meterPlugin);
    auto& edit = projects.getEdit();

    if (meters->edit != &edit)
    {
        // The previous Edit, and its meters, are already gone.
        meters->slots.clear();
        meters->edit = &edit;
    }

    auto& slot = meters->slots[slotId];
    const auto pluginId = meter != nullptr ? meter->itemID.toString() : juce::String();

    if (slot.pluginId != pluginId || slot.added != (meter != nullptr))
    {
        MeterState::detach (edit, slot);
        slot.pluginId = pluginId;
        slot.rms = false;   // a new meter starts in peak mode

        if (meter != nullptr)
        {
            meter->measurer.addClient (slot.client);
            slot.added = true;
        }
    }

    if (meter == nullptr)
        return {};

    if (slot.rms != measuringRms)
    {
        meter->measurer.setMode (measuringRms ? te::LevelMeasurer::RMSMode : te::LevelMeasurer::peakMode);
        slot.rms = measuringRms;
    }

    return readPeaks (slot.client);
}

StereoLevel Mixer::getTrackLevel (const juce::String& trackId)
{
    auto* track = findStripTrack (projects.getEdit(), trackId);
    auto* audio = dynamic_cast<te::AudioTrack*> (track);
    return levelOf (trackId, audio != nullptr ? audio->getLevelMeterPlugin()
                             : track != nullptr ? meterOnTrack (*track) : nullptr);
}

void Mixer::setMeasuringRms (bool rms)
{
    measuringRms = rms;
}

StereoLevel Mixer::getMasterLevel()
{
    auto* master = projects.getEdit().getMasterTrack();
    return levelOf ("master", master != nullptr ? meterOnTrack (*master) : nullptr);
}

juce::Result Mixer::addReturn (const juce::String& name)
{
    auto& edit = projects.getEdit();
    const int bus = nextFreeBus (edit);
    auto plugin = edit.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
    auto* ret = dynamic_cast<te::AuxReturnPlugin*> (plugin.get());

    if (ret == nullptr)
        return juce::Result::fail ("Couldn't add an aux return");

    projects.getUndo().beginStep ("Add Return");
    auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

    if (track == nullptr)
        return juce::Result::fail ("Couldn't add a return track");

    // Ahead of the volume plug-in, so the return is the front of the chain.
    track->pluginList.insertPlugin (plugin, 0, nullptr);
    ret->busNumber = bus;
    track->setName (name.isNotEmpty() ? name : juce::String ("Return"));
    return juce::Result::ok();
}

std::vector<ReturnInfo> Mixer::getReturns() const
{
    std::vector<ReturnInfo> returns;

    for (auto* track : te::getAudioTracks (projects.getEdit()))
        for (auto* ret : track->pluginList.getPluginsOfType<te::AuxReturnPlugin>())
            returns.push_back ({ track->itemID.toString(), ret->busNumber.get(), track->getName() });

    return returns;
}

juce::Result Mixer::addSend (const juce::String& fromTrackId, int bus)
{
    auto& edit = projects.getEdit();
    auto* track = findStripTrack (edit, fromTrackId);

    if (track == nullptr)
        return juce::Result::fail ("No such track");

    if (! hasReturn (edit, bus))
        return juce::Result::fail ("No return on bus " + juce::String (bus));

    auto plugin = edit.getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
    auto* send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());

    if (send == nullptr)
        return juce::Result::fail ("Couldn't add a send");

    projects.getUndo().beginStep ("Add Send");
    auto index = 0;

    if (auto* volume = faderOf (*track))
        if (auto volumeIndex = track->pluginList.indexOf (volume); volumeIndex >= 0)
            index = volumeIndex;

    track->pluginList.insertPlugin (plugin, index, nullptr);
    send->busNumber = bus;
    return juce::Result::ok();
}

bool Mixer::setSendGain (const juce::String& trackId, const juce::String& sendId, Decibels gain, bool continuesGesture)
{
    auto* track = findStripTrack (projects.getEdit(), trackId);
    auto* send = track != nullptr ? findSend (*track, sendId) : nullptr;

    if (send == nullptr || send->gain == nullptr)
        return false;

    const auto position = faderPosition (gain);
    pinDefault (send->gainLevel);
    syncParameter (*send->gain, send->gainLevel);

    const auto before = send->gainLevel.get();

    if (before == position)
        return false;

    projects.getUndo().beginGestureStep ("Set Send Gain", "Set Send Gain:" + trackId + ":" + sendId, continuesGesture);
    // Set the fader position itself, as track volume does, so a repeated dB
    // lands on the same value and a no-op stays out of the undo history.
    send->gain->setParameter (position, juce::sendNotification);
    return send->gainLevel.get() != before;
}

bool Mixer::setSendMuted (const juce::String& trackId, const juce::String& sendId, bool muted)
{
    auto* track = findStripTrack (projects.getEdit(), trackId);
    auto* send = track != nullptr ? findSend (*track, sendId) : nullptr;

    if (send == nullptr)
        return false;

    // AuxSendPlugin::setMute is not a separate flag. It stores the previous
    // gain and then drives the send to silence (or back) through the gain
    // parameter, and that write goes through the UndoManager. Track mute does
    // not. So a send mute that changes something is one undo step; we do not
    // try to keep it out of undo. A no-op opens no transaction.
    pinDefault (send->gainLevel);

    if (send->gain != nullptr)
        syncParameter (*send->gain, send->gainLevel);

    const bool already = dbFromFader (send->gainLevel.get()) <= sendMuteThreshold;

    if (already == muted)
        return false;

    projects.getUndo().beginStep ("Mute Send");
    send->setMute (muted);
    return true;
}

std::vector<SendInfo> Mixer::getSends (const juce::String& trackId) const
{
    std::vector<SendInfo> sends;
    auto* track = findStripTrack (projects.getEdit(), trackId);

    if (track == nullptr)
        return sends;

    for (auto* send : track->pluginList.getPluginsOfType<te::AuxSendPlugin>())
    {
        if (send->gain != nullptr)
            syncParameter (*send->gain, send->gainLevel);

        const auto gain = dbFromFader (send->gainLevel.get());
        sends.push_back ({ send->itemID.toString(), send->getBusNumber(), gain,
                           gain <= sendMuteThreshold });
    }

    return sends;
}

juce::Result Mixer::addBus (const juce::String& name)
{
    auto& edit = projects.getEdit();
    projects.getUndo().beginStep ("Add Bus");
    auto folder = edit.insertNewFolderTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr, true);

    if (folder == nullptr)
        return juce::Result::fail ("Couldn't add a bus");

    folder->setName (name.isNotEmpty() ? name : juce::String ("Bus"));
    return juce::Result::ok();
}

bool Mixer::moveTrackToBus (const juce::String& trackId, const juce::String& busTrackId)
{
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);
    auto* bus = findBus (edit, busTrackId);

    if (track == nullptr || bus == nullptr || track->getParentFolderTrack() == bus || bus->isAChildOf (*track))
        return false;

    auto children = bus->getAllSubTracks (false);
    te::Track* preceding = children.isEmpty() ? nullptr : children.getLast();
    projects.getUndo().beginStep ("Move to Bus");
    edit.moveTrack (track, te::TrackInsertPoint (bus, preceding));
    return track->getParentFolderTrack() == bus;
}

std::vector<BusInfo> Mixer::getBuses() const
{
    std::vector<BusInfo> buses;

    for (auto* folder : te::getTracksOfType<te::FolderTrack> (projects.getEdit(), true))
    {
        if (! isBus (*folder))
            continue;

        BusInfo info;
        info.trackId = folder->itemID.toString();
        info.name = folder->getName();

        for (auto* child : folder->getAllSubTracks (false))
            info.childTrackIds.push_back (child->itemID.toString());

        buses.push_back (std::move (info));
    }

    return buses;
}

std::vector<Strip> Mixer::getStrips() const
{
    auto& edit = projects.getEdit();
    std::map<juce::String, TrackInfo> tracks;

    for (auto& track : model.getTracks())
        tracks[track.id] = std::move (track);

    std::vector<Strip> strips;
    std::vector<std::pair<int, Strip>> returns;   // by bus, to sort A..D
    int trackNumber = 0;

    auto trackStrip = [&] (te::AudioTrack& audio, const TrackInfo& info)
    {
        Strip strip;
        strip.id = info.id;
        strip.name = info.name;
        strip.kind = info.kind;
        strip.colourIndex = info.colourIndex;
        strip.selected = info.selected;
        strip.volume = info.volume;
        strip.pan = info.pan;
        strip.muted = info.muted;
        strip.solo = info.solo;
        strip.input = info.input;
        strip.armed = info.armed;
        strip.sends = getSends (info.id);
        strip.inserts = plugins.getChain (info.id, PluginChain::mixer);
        strip.deviceChain = plugins.getChain (info.id, PluginChain::device);
        strip.output = outputOf (audio);
        return strip;
    };

    auto busStrip = [&] (te::FolderTrack& folder)
    {
        Strip strip;
        strip.id = folder.itemID.toString();
        strip.name = folder.getName();
        strip.role = StripRole::bus;
        strip.colourIndex = colourOf (folder);
        strip.muted = folder.isMuted (false);
        strip.solo = folder.isSolo (false);
        strip.childCount = folder.getAllSubTracks (false).size();
        strip.sends = getSends (strip.id);
        strip.inserts = plugins.getChain (strip.id, PluginChain::mixer);
        strip.deviceChain = plugins.getChain (strip.id, PluginChain::device);
        strip.output = outputOf (folder);

        if (auto* volume = folder.getVolumePlugin())
        {
            strip.volume = juce::jmax (ApplicationModel::minVolume, Decibels (volume->getVolumeDb()));
            strip.pan = volume->getPan();
        }

        return strip;
    };

    std::function<void (te::Track&)> visit = [&] (te::Track& track)
    {
        if (auto* folder = dynamic_cast<te::FolderTrack*> (&track))
        {
            for (auto* child : folder->getAllSubTracks (false))
                visit (*child);

            if (isBus (*folder))
                strips.push_back (busStrip (*folder));
        }
        else if (auto* audio = dynamic_cast<te::AudioTrack*> (&track))
        {
            auto info = tracks.find (audio->itemID.toString());

            if (info == tracks.end())
                return;

            auto strip = trackStrip (*audio, info->second);

            if (const auto bus = returnBusOf (*audio); bus >= 0)
            {
                strip.role = StripRole::returnTrack;
                strip.returnLetter = returnLetterFor (bus);
                returns.emplace_back (bus, std::move (strip));
            }
            else
            {
                strip.number = ++trackNumber;
                strips.push_back (std::move (strip));
            }
        }
    };

    for (auto* track : te::getTopLevelTracks (edit))
        visit (*track);

    std::stable_sort (returns.begin(), returns.end(), [] (auto& a, auto& b) { return a.first < b.first; });

    for (auto& ret : returns)
        strips.push_back (std::move (ret.second));

    return strips;
}

MasterInfo Mixer::getMaster() const
{
    auto* plugin = masterFader (projects.getEdit());

    if (plugin == nullptr)
        return {};

    // Undo re-syncs this fader in ApplicationModel::syncVolumeParametersFromState
    // Read it from state, and catch the live parameter up so a
    // later playback follows even if nothing has undone yet.
    if (plugin->volParam != nullptr)
        syncParameter (*plugin->volParam, plugin->volume);

    return { dbFromFader (plugin->volume.get()) };
}

bool Mixer::setMasterVolume (Decibels volume, bool continuesGesture)
{
    auto* plugin = masterFader (projects.getEdit());

    if (plugin == nullptr || plugin->volParam == nullptr)
        return false;

    const auto position = faderPosition (volume);
    pinVolumeDefaults (*plugin);
    syncParameter (*plugin->volParam, plugin->volume);

    const auto before = plugin->volume.get();

    if (before == position)
        return false;

    projects.getUndo().beginGestureStep ("Set Master Volume", "Set Master Volume", continuesGesture);
    plugin->setSliderPos (position);
    return plugin->volume.get() != before;
}



} // namespace resamper
