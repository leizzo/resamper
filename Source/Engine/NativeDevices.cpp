#include "NativeDevices.h"
#include "NativeDevicePlugins.h"
#include "ProjectManager.h"

namespace te = tracktion;

namespace resamper
{

const char* const NativeDevices::eqEightType = EqEightPlugin::xmlTypeName;
const char* const NativeDevices::compressorType = CompressorV2Plugin::xmlTypeName;

namespace
{
    te::Plugin* findPlugin (te::Edit& edit, const juce::String& pluginId)
    {
        if (pluginId.isEmpty())
            return nullptr;

        for (auto* plugin : te::getAllPlugins (edit, false))
            if (plugin->itemID.toString() == pluginId)
                return plugin;

        return nullptr;
    }

    template <typename PluginType>
    PluginType* find (te::Edit& edit, const juce::String& pluginId)
    {
        return dynamic_cast<PluginType*> (findPlugin (edit, pluginId));
    }
}

NativeDevices::NativeDevices (ProjectManager& pm) : projects (pm)
{
}

juce::String NativeDevices::bandParameter (int band, const juce::String& field)
{
    return "b" + juce::String (band + 1) + field;
}

void NativeDevices::getEqResponse (const juce::String& pluginId, const std::vector<float>& hz, std::vector<float>& db,
                                   int band, int channel) const
{
    db.assign (hz.size(), 0.0f);
    auto* eq = find<EqEightPlugin> (projects.getEdit(), pluginId);

    if (eq == nullptr)
        return;

    for (int b = 0; b < EqEightPlugin::numBands; ++b)
    {
        if (band >= 0 && b != band)
            continue;

        if (band < 0 && (eq->bands[(size_t) b].on.getCurrentValue() < 0.5f || ! eq->bandAppliesTo (b, channel)))
            continue;

        const auto section = eq->sectionFor (b);

        for (size_t i = 0; i < hz.size(); ++i)
            db[i] += (float) section.magnitudeDb (hz[i], eq->getSampleRate());
    }
}

bool NativeDevices::getSpectrum (const juce::String& pluginId, bool post, const std::vector<float>& hz, std::vector<float>& db) const
{
    db.assign (hz.size(), -120.0f);
    auto* eq = find<EqEightPlugin> (projects.getEdit(), pluginId);

    if (eq == nullptr)
        return false;

    // Both taps drain every time, so the hidden one never falls behind.
    eq->pre.update();
    eq->post.update();
    auto& tap = post ? eq->post : eq->pre;

    for (size_t i = 0; i < hz.size(); ++i)
        db[i] = tap.levelAt (hz[i], eq->getSampleRate());

    return true;
}

void NativeDevices::setAudition (const juce::String& pluginId, int band)
{
    if (auto* eq = find<EqEightPlugin> (projects.getEdit(), pluginId))
        eq->auditionBand.store (juce::isPositiveAndBelow (band, numEqBands) ? band : -1);
}

int NativeDevices::getAudition (const juce::String& pluginId) const
{
    if (auto* eq = find<EqEightPlugin> (projects.getEdit(), pluginId))
        return eq->auditionBand.load();

    return -1;
}

float NativeDevices::getTransferDb (const juce::String& pluginId, float inputDb) const
{
    auto* c = find<CompressorV2Plugin> (projects.getEdit(), pluginId);

    if (c == nullptr)
        return inputDb;

    return (float) dsp::transferDb (inputDb, c->threshold.getCurrentValue(), c->ratio.getCurrentValue(),
                                    c->knee.getCurrentValue(),
                                    juce::roundToInt (c->detect.getCurrentValue()) == (int) dsp::DetectMode::expand);
}

float NativeDevices::getMakeupDb (const juce::String& pluginId) const
{
    if (auto* c = find<CompressorV2Plugin> (projects.getEdit(), pluginId))
        return (float) c->makeupDb();

    return 0.0f;
}

DynamicsReading NativeDevices::readDynamics (const juce::String& pluginId) const
{
    DynamicsReading reading;

    if (auto* c = find<CompressorV2Plugin> (projects.getEdit(), pluginId))
    {
        auto toDb = [] (float gain) { return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f; };
        reading.inputLeftDb = toDb (c->inputLeft.take());
        reading.inputRightDb = toDb (c->inputRight.take());
        reading.reductionDb = c->reduction.take();
        reading.detectorDb = toDb (c->detector.take());
    }

    return reading;
}

std::optional<juce::Range<float>> NativeDevices::getModulationRange (const juce::String& pluginId, const juce::String& parameterId) const
{
    auto* plugin = findPlugin (projects.getEdit(), pluginId);

    if (plugin == nullptr)
        return {};

    for (auto* parameter : plugin->getAutomatableParameters())
    {
        if (parameter->paramID != parameterId)
            continue;

        const auto assignments = parameter->getAssignments();

        if (assignments.isEmpty())
            return {};

        // Each assignment adds its depth (signed) and offset to the parameter's
        // own position along its range.
        const auto& range = parameter->valueRange;
        const auto base = range.convertTo0to1 (range.snapToLegalValue (parameter->getCurrentBaseValue()));
        auto low = base, high = base;

        for (auto* assignment : assignments)
        {
            const auto depth = assignment->value.get(), offset = assignment->offset.get();
            low += offset + juce::jmin (0.0f, depth);
            high += offset + juce::jmax (0.0f, depth);
        }

        return juce::Range<float> (range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, low)),
                                   range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, high)));
    }

    return {};
}

} // namespace resamper
