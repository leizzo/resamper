#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace resamper
{

class ProjectManager;

/** What the UI reads off a Compressor v2 at UI rate (PRD §9.2.1a). Levels in
    dB; reductionDb is positive. Each is the peak since the last read; the
    detector is the level the compressor acted on (peak or RMS). */
struct DynamicsReading
{
    float inputLeftDb = -120, inputRightDb = -120, reductionDb = 0, detectorDb = -120;
};

/** Facade over the v2 native devices' analysis (PRD §9.2.1a): EQ Eight's
    curve and spectrum, Compressor v2's curve and meters, and the range a
    modulator moves a parameter over.

    Every reading comes from the audio thread without a lock: it writes atomics
    and a fixed FIFO and never allocates or waits; these calls run on the
    message thread at UI rate and do the work (the FFT, the smoothing). Every
    call re-reads ProjectManager::getEdit(); an unknown plug-in id, or one of
    another type, reads nothing.

    Parameters are set through PluginRack (plugin.setParameter /
    plugin.setParameters), as for every device. */
class NativeDevices
{
public:
    explicit NativeDevices (ProjectManager&);

    /** The built-in type names (PluginInfo::path) of the v2 devices. */
    static const char* const eqEightType;
    static const char* const compressorType;

    static constexpr int numEqBands = 8;

    /** An EQ Eight band's parameter id, e.g. bandParameter (2, "Freq") is "b3Freq".
        band counts from 0; field is On, Type, Freq, Gain, Q or Channel. */
    static juce::String bandParameter (int band, const juce::String& field);

    //==============================================================================
    /** EQ Eight's response at each of hz, in dB, into db (resized): every band
        that is on (band < 0), or that one band alone, as channel (0: L or M,
        1: R or S) hears it. Scale and Adaptive Q are applied; Out is not. */
    void getEqResponse (const juce::String& pluginId, const std::vector<float>& hz, std::vector<float>& db,
                        int band = -1, int channel = 0) const;

    /** The EQ's spectrum before (post = false) or after its bands, in dB at
        each of hz, into db (resized). Drains what the audio thread sent since
        the last call. False when the id isn't an EQ Eight. */
    bool getSpectrum (const juce::String& pluginId, bool post, const std::vector<float>& hz, std::vector<float>& db) const;

    /** Plays only band (a band-pass at its frequency and Q) while auditioning;
        -1 stops. Monitoring only: never saved, never an undo step. */
    void setAudition (const juce::String& pluginId, int band);
    int getAudition (const juce::String& pluginId) const;

    //==============================================================================
    /** Compressor v2's static curve: the output level for inputDb. */
    float getTransferDb (const juce::String& pluginId, float inputDb) const;

    /** The makeup gain it applies now (the auto value when Auto is on). */
    float getMakeupDb (const juce::String& pluginId) const;

    /** Its meters since the last read. */
    DynamicsReading readDynamics (const juce::String& pluginId) const;

    //==============================================================================
    /** The span a modulator moves a device parameter over, in the parameter's
        units, around its own value; empty while nothing modulates it. The
        Mods Drawer (#83) adds the modulators; the graphs already draw this. */
    std::optional<juce::Range<float>> getModulationRange (const juce::String& pluginId, const juce::String& parameterId) const;

private:
    ProjectManager& projects;

    JUCE_DECLARE_NON_COPYABLE (NativeDevices)
};

} // namespace resamper
