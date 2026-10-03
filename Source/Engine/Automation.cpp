#include "Automation.h"
#include "ApplicationModel.h"
#include "EditTracks.h"
#include "ProjectManager.h"

#include <tracktion_engine/tracktion_engine.h>

#include <cmath>
#include <optional>

namespace te = tracktion;

namespace resamper
{

namespace
{
    /** Inserts are everything ahead of the track fader, matching PluginRack.
        The level meter lives after the volume plug-in and is not a target. */
    int indexBeforeVolume (te::AudioTrack& track)
    {
        if (auto* volume = track.getVolumePlugin())
            if (const int index = track.pluginList.indexOf (volume); index >= 0)
                return index;

        return track.pluginList.size();
    }

    juce::String pluginKey (const te::Plugin& plugin, const te::AutomatableParameter& param)
    {
        return "plugin:" + plugin.itemID.toString() + ":" + param.paramID;
    }

    bool isSendKey (const juce::String& key)
    {
        return key.startsWith ("send:");
    }

    te::AutomatableParameter* findParameter (te::AudioTrack& track, const juce::String& key)
    {
        if (auto* volume = track.getVolumePlugin())
        {
            if (key == "volume")
                return volume->volParam.get();

            if (key == "pan")
                return volume->panParam.get();
        }

        if (isSendKey (key))
        {
            const auto sendId = key.fromFirstOccurrenceOf ("send:", false, false);

            for (auto* send : track.pluginList.getPluginsOfType<te::AuxSendPlugin>())
                if (send->itemID.toString() == sendId && send->gain != nullptr)
                    return send->gain.get();

            return nullptr;
        }

        if (! key.startsWith ("plugin:"))
            return nullptr;

        const auto body = key.substring (7);
        const auto sep = body.indexOfChar (':');

        if (sep <= 0)
            return nullptr;

        const auto pluginId = body.substring (0, sep);
        const auto paramId = body.substring (sep + 1);
        const int end = indexBeforeVolume (track);

        for (int i = 0; i < end; ++i)
        {
            auto* plugin = track.pluginList[i];

            if (plugin == nullptr || plugin == track.getVolumePlugin() || plugin->itemID.toString() != pluginId)
                continue;

            if (auto param = plugin->getAutomatableParameterByID (paramId))
                return param.get();
        }

        return nullptr;
    }

    // VolumeAndPanPlugin's volParam range is a fader position 0..1, not dB.
    // decibelsToVolumeFaderPosition / volumeFaderPositionToDB are the plugin's
    // own law: 0 dB sits near 0.74, +6 dB is 1, and -100 dB (silence) is 0.
    // Pan's range is already -1..1. Plug-in parameters are stored in their
    // native range; this facade speaks 0..1 via NormalisableRange.
    float publicToCurve (const te::AutomatableParameter& param, const juce::String& key, float value)
    {
        if (key == "volume" || isSendKey (key))
        {
            const auto db = juce::jlimit ((float) ApplicationModel::minVolume.value,
                                          (float) ApplicationModel::maxVolume.value, value);
            return te::decibelsToVolumeFaderPosition (db);
        }

        if (key == "pan")
            return juce::jlimit (-1.0f, 1.0f, value);

        return param.valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, value));
    }

    float curveToPublic (const te::AutomatableParameter& param, const juce::String& key, float curveValue)
    {
        if (key == "volume" || isSendKey (key))
            return te::volumeFaderPositionToDB (curveValue);

        if (key == "pan")
            return curveValue;

        return param.valueRange.convertTo0to1 (curveValue);
    }

    bool nearlyEqual (double a, double b)
    {
        return std::abs (a - b) < 1.0e-6;
    }
}

Automation::Automation (ProjectManager& pm) : projects (pm) {}

std::vector<ParameterInfo> Automation::getTargets (const juce::String& trackId) const
{
    std::vector<ParameterInfo> targets;
    auto* track = findAudioTrack (projects.getEdit(), trackId);

    if (track == nullptr)
        return targets;

    if (track->getVolumePlugin() != nullptr)
    {
        targets.push_back ({ "volume", "Volume" });
        targets.push_back ({ "pan", "Pan" });
    }

    for (auto* send : track->pluginList.getPluginsOfType<te::AuxSendPlugin>())
        if (send->gain != nullptr)
            targets.push_back ({ "send:" + send->itemID.toString(),
                                 "Send " + juce::String (send->getBusNumber()) });

    const int end = indexBeforeVolume (*track);

    for (int i = 0; i < end; ++i)
    {
        auto* plugin = track->pluginList[i];

        if (plugin == nullptr || plugin == track->getVolumePlugin()
            || dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr
            || dynamic_cast<te::AuxReturnPlugin*> (plugin) != nullptr
            || dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr)
            continue;

        for (auto* param : plugin->getAutomatableParameters())
            if (param != nullptr && param->paramID.isNotEmpty())
                targets.push_back ({ pluginKey (*plugin, *param),
                                     plugin->getName() + " / " + param->getParameterName() });
    }

    return targets;
}

std::vector<AutomationPointInfo> Automation::getPoints (const juce::String& trackId,
                                                        const juce::String& parameterKey) const
{
    std::vector<AutomationPointInfo> points;
    auto* track = findAudioTrack (projects.getEdit(), trackId);
    auto* param = track != nullptr ? findParameter (*track, parameterKey) : nullptr;

    if (param == nullptr)
        return points;

    auto& curve = param->getCurve();

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        AutomationPointInfo info;
        info.index = i;
        info.timeSeconds = curve.getPointTime (i).inSeconds();
        info.value = curveToPublic (*param, parameterKey, curve.getPointValue (i));
        points.push_back (info);
    }

    return points;
}

bool Automation::addPoint (const juce::String& trackId, const juce::String& parameterKey,
                           double timeSeconds, float value)
{
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);
    auto* param = track != nullptr ? findParameter (*track, parameterKey) : nullptr;

    if (param == nullptr)
        return false;

    const auto stored = publicToCurve (*param, parameterKey, value);
    const auto time = te::TimePosition::fromSeconds (juce::jmax (0.0, timeSeconds));

    projects.getUndo().beginStep ("Add Automation Point");
    param->getCurve().addPoint (time, stored, 0.0f, &edit.getUndoManager());
    return true;
}

bool Automation::movePoint (const juce::String& trackId, const juce::String& parameterKey,
                            int index, double timeSeconds, float value)
{
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);
    auto* param = track != nullptr ? findParameter (*track, parameterKey) : nullptr;

    if (param == nullptr)
        return false;

    auto& curve = param->getCurve();

    if (! juce::isPositiveAndBelow (index, curve.getNumPoints()))
        return false;

    const auto stored = publicToCurve (*param, parameterKey, value);
    const auto time = juce::jmax (0.0, timeSeconds);

    if (nearlyEqual (time, curve.getPointTime (index).inSeconds())
        && nearlyEqual (stored, curve.getPointValue (index)))
        return false;

    projects.getUndo().beginStep ("Move Automation Point");
    curve.movePoint (index, te::TimePosition::fromSeconds (time), stored,
                     std::optional<juce::Range<float>> (param->getValueRange()),
                     false, &edit.getUndoManager());
    return true;
}

bool Automation::removePoint (const juce::String& trackId, const juce::String& parameterKey, int index)
{
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);
    auto* param = track != nullptr ? findParameter (*track, parameterKey) : nullptr;

    if (param == nullptr || ! juce::isPositiveAndBelow (index, param->getCurve().getNumPoints()))
        return false;

    projects.getUndo().beginStep ("Remove Automation Point");
    param->getCurve().removePoint (index, &edit.getUndoManager());
    return true;
}

bool Automation::clear (const juce::String& trackId, const juce::String& parameterKey)
{
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);
    auto* param = track != nullptr ? findParameter (*track, parameterKey) : nullptr;

    if (param == nullptr || param->getCurve().getNumPoints() == 0)
        return false;

    projects.getUndo().beginStep ("Clear Automation");
    param->getCurve().clear (&edit.getUndoManager());
    return true;
}

} // namespace resamper
