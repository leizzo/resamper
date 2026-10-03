#include "Shaper.h"
#include "EditTracks.h"
#include "ProjectManager.h"

#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

namespace resamper
{

namespace
{
    const juce::Identifier shaperFlag ("resamperShaper");
    const juce::Identifier parameterKeyProperty ("resamperParameterKey");
    const juce::Identifier lengthBeatsProperty ("resamperLengthBeats");
    const juce::Identifier depthProperty ("resamperDepth");
    const juce::Identifier attackProperty ("resamperAttackSeconds");
    const juce::Identifier holdProperty ("resamperHoldSeconds");
    const juce::Identifier releaseProperty ("resamperReleaseSeconds");
    const juce::Identifier thresholdProperty ("resamperThresholdDb");
    const juce::Identifier shapeType ("resamperShape");

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

    te::AutomatableParameter* findParameter (te::AudioTrack& track, const juce::String& key)
    {
        if (auto* volume = track.getVolumePlugin())
        {
            if (key == "volume")
                return volume->volParam.get();

            if (key == "pan")
                return volume->panParam.get();
        }

        if (key.startsWith ("send:"))
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

    bool isResamperShaper (const te::Modifier& mod)
    {
        if (! (bool) mod.state[shaperFlag])
            return false;

        return dynamic_cast<const te::BreakpointOscillatorModifier*> (&mod) != nullptr
            || dynamic_cast<const te::EnvelopeFollowerModifier*> (&mod) != nullptr;
    }

    void setParam (te::AutomatableParameter* param, float value)
    {
        if (param != nullptr)
            param->setParameter (value, juce::sendNotification);
    }

    float clampedDepth (float depth)
    {
        return juce::jlimit (0.0f, 1.0f, depth);
    }

    std::vector<ShaperShapePoint> clampShape (const std::vector<ShaperShapePoint>& shape)
    {
        std::vector<ShaperShapePoint> points;
        points.reserve (4);

        for (auto& point : shape)
        {
            if (points.size() == 4)
                break;

            points.push_back ({ juce::jlimit (0.0f, 1.0f, point.time),
                                juce::jlimit (0.0f, 1.0f, point.value) });
        }

        return points;
    }

    void writeShape (juce::ValueTree& state, const std::vector<ShaperShapePoint>& points, juce::UndoManager* um)
    {
        if (auto existing = state.getChildWithName (shapeType); existing.isValid())
            state.removeChild (existing, um);

        juce::ValueTree node (shapeType);

        for (auto& point : points)
        {
            juce::ValueTree child ("resamperShapePoint");
            child.setProperty ("t", (double) point.time, nullptr);
            child.setProperty ("v", (double) point.value, nullptr);
            node.addChild (child, -1, nullptr);
        }

        state.addChild (node, -1, um);
    }

    std::vector<ShaperShapePoint> readShape (const juce::ValueTree& state)
    {
        std::vector<ShaperShapePoint> points;
        auto node = state.getChildWithName (shapeType);

        for (int i = 0; i < node.getNumChildren(); ++i)
        {
            auto point = node.getChild (i);
            points.push_back ({ (float) (double) point["t"], (float) (double) point["v"] });
        }

        return points;
    }

    float propertyOr (const juce::ValueTree& state, const juce::Identifier& id, float fallback)
    {
        return state.hasProperty (id) ? (float) (double) state[id] : fallback;
    }

    /** Loop depth is the assignment amount. The oscillator's own depth stays at 1
        so the shape is not scaled twice. Audio-trigger depth is the follower's
        depth; its assignment stays at 1 for the same reason. */
    void ensureAssignment (te::AutomatableParameter& param, te::Modifier& mod, float amount)
    {
        for (auto assignment : param.getAssignments())
        {
            if (assignment->isForModifierSource (mod))
            {
                assignment->value = amount;
                return;
            }
        }

        param.addModifier (mod, amount);
    }

    float assignmentValue (te::AutomatableParameter& param, te::Modifier& mod, float fallback)
    {
        for (auto assignment : param.getAssignments())
            if (assignment->isForModifierSource (mod))
                return assignment->value.get();

        return fallback;
    }

    juce::String keyFromAssignments (te::AudioTrack& track, te::Modifier& mod)
    {
        auto assigned = [&] (te::AutomatableParameter* param) {
            return param != nullptr && assignmentValue (*param, mod, -1.0f) >= 0.0f;
        };

        if (auto* volume = track.getVolumePlugin())
        {
            if (assigned (volume->volParam.get()))
                return "volume";

            if (assigned (volume->panParam.get()))
                return "pan";
        }

        for (auto* send : track.pluginList.getPluginsOfType<te::AuxSendPlugin>())
            if (send->gain != nullptr && assigned (send->gain.get()))
                return "send:" + send->itemID.toString();

        const int end = indexBeforeVolume (track);

        for (int i = 0; i < end; ++i)
        {
            auto* plugin = track.pluginList[i];

            if (plugin == nullptr || plugin == track.getVolumePlugin()
                || dynamic_cast<te::AuxSendPlugin*> (plugin) != nullptr
                || dynamic_cast<te::AuxReturnPlugin*> (plugin) != nullptr
                || dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr)
                continue;

            for (auto* param : plugin->getAutomatableParameters())
                if (assigned (param))
                    return pluginKey (*plugin, *param);
        }

        return {};
    }

    std::vector<ShaperShapePoint> shapeFromStages (const te::BreakpointOscillatorModifier& osc)
    {
        const int count = juce::jlimit (0, 4, juce::roundToInt (osc.numActivePoints.get()));
        std::vector<ShaperShapePoint> points;

        auto add = [&] (const juce::CachedValue<float>& time, const juce::CachedValue<float>& value) {
            points.push_back ({ time.get(), value.get() });
        };

        if (count >= 1) add (osc.stageOneTime, osc.stageOneValue);
        if (count >= 2) add (osc.stageTwoTime, osc.stageTwoValue);
        if (count >= 3) add (osc.stageThreeTime, osc.stageThreeValue);
        if (count >= 4) add (osc.stageFourTime, osc.stageFourValue);
        return points;
    }

    // One transport-synced cycle lasts
    //   ModifierCommon::getBarFraction(rateType) * timeSignature.numerator / rate
    // beats (BreakpointOscillatorModifierTimer, syncType == transport, rateType
    // other than hertz).
    //
    // rateType is quarter, a quarter of a bar. In 4/4 that is one beat when
    // rate is 1, so rate = (getBarFraction(quarter) * numerator) / lengthBeats
    // makes one cycle equal lengthBeats at the current time signature, for any
    // length the rate parameter can hold. Lengths outside that span cannot be
    // represented by the rate enum, so resamperLengthBeats on the modifier
    // ValueTree is what getShapers() returns. syncType stays transport either
    // way, so the cycle still locks to playback.
    void configureLoopRate (te::Edit& edit, te::BreakpointOscillatorModifier& osc, double lengthBeats)
    {
        const auto numerator = (double) juce::jmax (1, edit.tempoSequence.getTimeSigAt (te::TimePosition()).numerator.get());
        const auto barFraction = te::ModifierCommon::getBarFraction (te::ModifierCommon::quarter);
        const auto safeLength = juce::jmax (1.0e-4, lengthBeats);

        setParam (osc.syncTypeParam.get(), (float) te::ModifierCommon::transport);
        setParam (osc.rateTypeParam.get(), (float) te::ModifierCommon::quarter);

        if (osc.rateParam != nullptr)
            setParam (osc.rateParam.get(), osc.rateParam->getValueRange().clipValue ((float) (barFraction * numerator / safeLength)));
        osc.state.setProperty (lengthBeatsProperty, lengthBeats, &edit.getUndoManager());
    }

    void applyLoop (te::Edit& edit, te::BreakpointOscillatorModifier& osc, te::AutomatableParameter& param,
                    double lengthBeats, const std::vector<ShaperShapePoint>& shape, float depth)
    {
        const auto points = clampShape (shape);
        const auto amount = clampedDepth (depth);
        auto& undo = edit.getUndoManager();

        configureLoopRate (edit, osc, lengthBeats);
        setParam (osc.depthParam.get(), 1.0f);

        te::AutomatableParameter* values[4] = { osc.stageOneValueParam.get(), osc.stageTwoValueParam.get(),
                                                osc.stageThreeValueParam.get(), osc.stageFourValueParam.get() };
        te::AutomatableParameter* times[4] = { osc.stageOneTimeParam.get(), osc.stageTwoTimeParam.get(),
                                               osc.stageThreeTimeParam.get(), osc.stageFourTimeParam.get() };

        for (size_t i = 0; i < points.size(); ++i)
        {
            setParam (values[i], points[i].value);
            setParam (times[i], points[i].time);
        }

        // stageZero is the value held from the start of the cycle until stage one.
        if (! points.empty())
            setParam (osc.stageZeroValueParam.get(), points.front().value);

        setParam (osc.numActivePointsParam.get(), (float) juce::jmax (1, (int) points.size()));
        writeShape (osc.state, points, &undo);
        osc.state.setProperty (depthProperty, amount, &undo);
        ensureAssignment (param, osc, amount);
    }

    // EnvelopeFollowerModifier::gainDb is input gain ahead of the detector
    // (-20..20 dB). A louder threshold should need a louder signal, so the
    // gain is the negation of the threshold, clipped to that range:
    //   gainDb = clamp(-thresholdDb, -20, 20)
    // threshold -20 dB -> gain +20 dB (sensitive); threshold 0 dB -> gain 0 dB.
    // The requested threshold is stored as resamperThresholdDb so getShapers()
    // round-trips values the gain parameter cannot represent.
    // Attack, hold, and release on the follower are milliseconds; the facade
    // speaks seconds and stores those seconds as app properties too.
    void applyAudio (te::Edit& edit, te::EnvelopeFollowerModifier& env, te::AutomatableParameter& param,
                     float attackSeconds, float holdSeconds, float releaseSeconds, float thresholdDb, float depth)
    {
        const auto amount = clampedDepth (depth);
        auto& undo = edit.getUndoManager();
        const auto gain = juce::jlimit (-20.0f, 20.0f, -thresholdDb);

        auto setMilliseconds = [] (te::AutomatableParameter* parameter, float seconds) {
            if (parameter != nullptr)
                parameter->setParameter (parameter->getValueRange().clipValue (seconds * 1000.0f), juce::sendNotification);
        };

        setMilliseconds (env.attackParam.get(), attackSeconds);
        setMilliseconds (env.holdParam.get(), holdSeconds);
        setMilliseconds (env.releaseParam.get(), releaseSeconds);
        setParam (env.gainDbParam.get(), gain);
        setParam (env.depthParam.get(), amount);

        env.state.setProperty (attackProperty, (double) attackSeconds, &undo);
        env.state.setProperty (holdProperty, (double) holdSeconds, &undo);
        env.state.setProperty (releaseProperty, (double) releaseSeconds, &undo);
        env.state.setProperty (thresholdProperty, (double) thresholdDb, &undo);
        env.state.setProperty (depthProperty, amount, &undo);
        ensureAssignment (param, env, 1.0f);
    }

    struct Located
    {
        te::Edit* edit = nullptr;
        te::AudioTrack* track = nullptr;
        te::Modifier* modifier = nullptr;
    };

    /** The returned modifier is owned by the track's ModifierList. Do not
        keep a ReferenceCountedObjectPtr across remove(): that would leave
        the old object in the edit's item cache. */
    Located locate (te::Edit& edit, const juce::String& shaperId)
    {
        Located found;
        found.edit = &edit;

        for (auto* track : te::getAudioTracks (edit))
        {
            auto* list = track->getModifierList();

            if (list == nullptr)
                continue;

            for (auto mod : list->getModifiers())
            {
                if (mod->itemID.toString() == shaperId && isResamperShaper (*mod))
                {
                    found.track = track;
                    found.modifier = mod;
                    return found;
                }
            }
        }

        return found;
    }

    te::Modifier* replaceModifier (te::AudioTrack& track, te::Modifier& oldModifier, const juce::Identifier& type,
                                   const juce::String& key)
    {
        auto* list = track.getModifierList();

        if (list == nullptr)
            return nullptr;

        oldModifier.remove();

        juce::ValueTree state (type);
        state.setProperty (shaperFlag, true, nullptr);
        state.setProperty (parameterKeyProperty, key, nullptr);

        auto created = list->insertModifier (state, list->getModifiers().size(), nullptr);
        return created.get();
    }
}

Shaper::Shaper (ProjectManager& pm) : projects (pm) {}

juce::Result Shaper::add (const juce::String& trackId, const juce::String& parameterKey, ShaperMode mode)
{
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);

    if (track == nullptr)
        return juce::Result::fail ("Unknown track");

    auto* param = findParameter (*track, parameterKey);

    if (param == nullptr)
        return juce::Result::fail ("Unknown parameter");

    auto* list = track->getModifierList();

    if (list == nullptr)
        return juce::Result::fail ("This track cannot hold a shaper");

    projects.getUndo().beginStep ("Add Shaper");

    const auto type = mode == ShaperMode::audioTrigger ? te::IDs::ENVELOPEFOLLOWER
                                                       : te::IDs::BREAKPOINTOSCILLATOR;
    juce::ValueTree state (type);
    state.setProperty (shaperFlag, true, nullptr);
    state.setProperty (parameterKeyProperty, parameterKey, nullptr);

    auto created = list->insertModifier (state, list->getModifiers().size(), nullptr);

    if (created == nullptr)
        return juce::Result::fail ("Could not add a shaper");

    if (auto* osc = dynamic_cast<te::BreakpointOscillatorModifier*> (created.get()))
        applyLoop (edit, *osc, *param, 1.0, {}, 1.0f);
    else if (auto* env = dynamic_cast<te::EnvelopeFollowerModifier*> (created.get()))
        applyAudio (edit, *env, *param, 0.01f, 0.0f, 0.1f, -20.0f, 1.0f);
    else
        return juce::Result::fail ("Could not add a shaper");

    return juce::Result::ok();
}

bool Shaper::remove (const juce::String& shaperId)
{
    auto& edit = projects.getEdit();
    auto located = locate (edit, shaperId);

    if (located.modifier == nullptr)
        return false;

    projects.getUndo().beginStep ("Remove Shaper");
    located.modifier->remove();
    return true;
}

bool Shaper::setLoop (const juce::String& shaperId, double lengthBeats, const std::vector<ShaperShapePoint>& shape, float depth)
{
    auto& edit = projects.getEdit();
    auto located = locate (edit, shaperId);

    if (located.modifier == nullptr || located.track == nullptr)
        return false;

    const auto key = located.modifier->state[parameterKeyProperty].toString().isNotEmpty()
                       ? located.modifier->state[parameterKeyProperty].toString()
                       : keyFromAssignments (*located.track, *located.modifier);
    auto* param = findParameter (*located.track, key);

    if (param == nullptr)
        return false;

    const bool replacing = dynamic_cast<te::BreakpointOscillatorModifier*> (located.modifier) == nullptr;

    if (replacing && located.track->getModifierList() == nullptr)
        return false;

    projects.getUndo().beginStep ("Set Shaper Loop");

    auto* osc = dynamic_cast<te::BreakpointOscillatorModifier*> (located.modifier);

    if (osc == nullptr)
        osc = dynamic_cast<te::BreakpointOscillatorModifier*> (replaceModifier (*located.track, *located.modifier,
                                                                               te::IDs::BREAKPOINTOSCILLATOR, key));

    if (osc == nullptr)
        return false;

    applyLoop (edit, *osc, *param, lengthBeats, shape, depth);
    return true;
}

bool Shaper::setAudioTrigger (const juce::String& shaperId, float attackSeconds, float holdSeconds, float releaseSeconds,
                              float thresholdDb, float depth)
{
    auto& edit = projects.getEdit();
    auto located = locate (edit, shaperId);

    if (located.modifier == nullptr || located.track == nullptr)
        return false;

    const auto key = located.modifier->state[parameterKeyProperty].toString().isNotEmpty()
                       ? located.modifier->state[parameterKeyProperty].toString()
                       : keyFromAssignments (*located.track, *located.modifier);
    auto* param = findParameter (*located.track, key);

    if (param == nullptr)
        return false;

    const bool replacing = dynamic_cast<te::EnvelopeFollowerModifier*> (located.modifier) == nullptr;

    if (replacing && located.track->getModifierList() == nullptr)
        return false;

    projects.getUndo().beginStep ("Set Shaper Audio Trigger");

    auto* env = dynamic_cast<te::EnvelopeFollowerModifier*> (located.modifier);

    if (env == nullptr)
        env = dynamic_cast<te::EnvelopeFollowerModifier*> (replaceModifier (*located.track, *located.modifier,
                                                                           te::IDs::ENVELOPEFOLLOWER, key));

    if (env == nullptr)
        return false;

    applyAudio (edit, *env, *param, attackSeconds, holdSeconds, releaseSeconds, thresholdDb, depth);
    return true;
}

std::vector<ShaperInfo> Shaper::getShapers (const juce::String& trackId) const
{
    std::vector<ShaperInfo> shapers;
    auto& edit = projects.getEdit();
    auto* track = findAudioTrack (edit, trackId);
    auto* list = track != nullptr ? track->getModifierList() : nullptr;

    if (list == nullptr)
        return shapers;

    for (auto mod : list->getModifiers())
    {
        if (! isResamperShaper (*mod))
            continue;

        ShaperInfo info;
        info.id = mod->itemID.toString();
        info.trackId = track->itemID.toString();
        info.parameterKey = mod->state[parameterKeyProperty].toString();

        if (info.parameterKey.isEmpty())
            info.parameterKey = keyFromAssignments (*track, *mod);

        if (auto* osc = dynamic_cast<te::BreakpointOscillatorModifier*> (mod))
        {
            info.mode = ShaperMode::loop;

            if (mod->state.hasProperty (lengthBeatsProperty))
                info.lengthBeats = (double) mod->state[lengthBeatsProperty];

            info.shape = mod->state.getChildWithName (shapeType).isValid() ? readShape (mod->state)
                                                                           : shapeFromStages (*osc);
        }
        else if (auto* env = dynamic_cast<te::EnvelopeFollowerModifier*> (mod))
        {
            info.mode = ShaperMode::audioTrigger;
            info.attackSeconds = propertyOr (mod->state, attackProperty, env->attack.get() / 1000.0f);
            info.holdSeconds = propertyOr (mod->state, holdProperty, env->hold.get() / 1000.0f);
            info.releaseSeconds = propertyOr (mod->state, releaseProperty, env->release.get() / 1000.0f);
            info.thresholdDb = propertyOr (mod->state, thresholdProperty, -env->gainDb.get());
        }
        else
        {
            continue;
        }

        if (mod->state.hasProperty (depthProperty))
            info.depth = propertyOr (mod->state, depthProperty, info.depth);
        else if (auto* param = findParameter (*track, info.parameterKey))
            info.depth = assignmentValue (*param, *mod, info.depth);

        shapers.push_back (std::move (info));
    }

    return shapers;
}

} // namespace resamper
