#pragma once

#include "EngineInternal.h"
#ifdef RESAMPER_ENGINE_INTERNAL
#include "NativeDeviceDsp.h"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <vector>

/*  Engine-private: the v2 native devices' Tracktion plug-ins (PRD §9.2.1a).
    Only Source/Engine includes this; everything above talks to NativeDevices. */

namespace resamper
{

/** Registers EQ Eight and Compressor v2 as built-in plug-in types. Once per engine. */
void registerNativeDevices (tracktion::PluginManager&);

//==============================================================================
/** Carries one point of a device's signal from the audio thread to the UI for
    a spectrum (PRD §9.2.1a). The audio thread writes the newest samples into a
    fixed ring of atomics and publishes how far it got; it never allocates,
    locks or waits, and it overwrites what the UI hasn't read (a spectrum only
    wants the latest window). The message thread copies the last fftSize
    samples, windows them and runs the FFT. */
class SpectrumTap
{
public:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, numBins = fftSize / 2;

    SpectrumTap();

    /** Audio thread: the mono sum of channels 0 and 1 (or 0 alone). */
    void push (const juce::AudioBuffer<float>&, int start, int numSamples) noexcept;

    /** Message thread: refreshes the smoothed spectrum from the newest
        samples. Returns false if nothing new arrived (it then falls away). */
    bool update();

    /** Message thread: the smoothed level, in dB, around frequency hz. */
    float levelAt (double hz, double sampleRate) const;

private:
    /** A power of two, so the running count indexes it across wrap-around. */
    static constexpr juce::uint32 ringSize = (juce::uint32) fftSize * 4;

    std::unique_ptr<std::atomic<float>[]> ring;
    std::atomic<juce::uint32> written { 0 };   ///< samples pushed so far (wraps)

    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<juce::uint32>::is_always_lock_free);

    // Message thread only.
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> scratch, smoothed;
    juce::uint32 lastRead = 0;
};

//==============================================================================
/** `Device/EQ Eight v2`: eight bands, each a cut, shelf, bell or notch, with
    Adaptive Q, St / L-R / M-S, Scale and Out, and an audition band-pass.
    A new built-in, not a wrapper of te::EqualiserPlugin (four fixed bands, no
    M/S): see CONTEXT.md, Native Device. */
class EqEightPlugin : public tracktion::Plugin
{
public:
    static constexpr int numBands = 8;

    explicit EqEightPlugin (tracktion::PluginCreationInfo);
    ~EqEightPlugin() override;

    static const char* getPluginName()      { return "EQ Eight"; }
    static const char* xmlTypeName;

    juce::String getName() const override                               { return getPluginName(); }
    juce::String getVendor() override                                   { return "Resamper"; }
    juce::String getPluginType() override                               { return xmlTypeName; }
    juce::String getShortName (int) override                            { return "EQ8"; }
    juce::String getSelectableDescription() override                    { return getPluginName(); }
    int getNumOutputChannelsGivenInputs (int numInputChannels) override { return juce::jmin (numInputChannels, 2); }
    BusLayout getBusses() const override                                { return BusLayout::singleStereoInOut(); }

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    struct Band
    {
        tracktion::ParameterWithStateValue on, type, frequency, gain, q, channel;
    };

    std::array<Band, numBands> bands;
    tracktion::ParameterWithStateValue adaptive, mode, scale, output;

    /** The rate it runs at (or the engine default before it first runs). */
    double getSampleRate() const noexcept   { return sampleRate; }

    /** One band's section at its settings (Scale and Adaptive Q applied): what the curve shows. */
    dsp::Biquad sectionFor (int band) const;

    /** Whether band applies to channel (0: L or M, 1: R or S) in the current mode. */
    bool bandAppliesTo (int band, int channel) const;

    /** The band auditioned on its own (a band-pass), or -1. Not saved, not undoable. */
    std::atomic<int> auditionBand { -1 };

    SpectrumTap pre, post;

private:
    /** Where a band's frequency (in octaves), gain and Q are on their way to its settings. */
    struct Glide
    {
        double log2Hz = 10, gainDb = 0, q = 0.71;
    };

    static constexpr int glideBlock = 32;
    static constexpr double glideSeconds = 0.02;

    /** Per band, per channel. */
    std::array<std::array<dsp::BiquadState, 2>, numBands> states;
    std::array<dsp::BiquadState, 2> auditionStates;
    std::array<Glide, numBands> glides;

    /** What a band's section is designed from: its type, and its frequency, gain and
        Q with Scale and Adaptive Q applied. */
    struct Design
    {
        int type = -1;
        double hz = 0, gainDb = 0, q = 0;

        bool operator== (const Design&) const = default;
    };

    /** Per band, the section the audio thread runs and what it was designed from:
        a band is redesigned only when that changes. */
    std::array<std::pair<Design, dsp::Biquad>, numBands> designed;
    bool glideFromSettings = true;   ///< the first block starts at the settings, not a glide toward them
    float outputGain = 1.0f;

    /** The section for band at these values, with the band's type, Scale and Adaptive Q. */
    Design designFor (int band, double hz, double gainDb, double q) const;

    /** Moves every band's glide numSamples further, or straight to its settings. */
    void glideBands (int numSamples, bool jump) noexcept;

    std::vector<tracktion::ParameterWithStateValue*> allParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqEightPlugin)
};

//==============================================================================
/** `Device/Compressor v2`: threshold, ratio, attack, release, soft knee,
    lookahead 0 / 1 / 10 ms, Peak / RMS / Expand detection, makeup (auto or
    set), Mix and Out. Its meters reach the UI through lock-free atomics. */
class CompressorV2Plugin : public tracktion::Plugin
{
public:
    explicit CompressorV2Plugin (tracktion::PluginCreationInfo);
    ~CompressorV2Plugin() override;

    static const char* getPluginName()      { return "Compressor"; }
    static const char* xmlTypeName;

    juce::String getName() const override                               { return getPluginName(); }
    juce::String getVendor() override                                   { return "Resamper"; }
    juce::String getPluginType() override                               { return xmlTypeName; }
    juce::String getShortName (int) override                            { return "Comp"; }
    juce::String getSelectableDescription() override                    { return getPluginName(); }
    int getNumOutputChannelsGivenInputs (int numInputChannels) override { return juce::jmin (numInputChannels, 2); }
    BusLayout getBusses() const override                                { return BusLayout::singleStereoInOut(); }
    double getLatencySeconds() override;

    void initialise (const tracktion::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const tracktion::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    tracktion::ParameterWithStateValue threshold, ratio, attack, release, knee, lookahead, detect,
                                       makeupAuto, makeup, mix, output;

    /** The makeup gain in effect: the auto value or the set one. */
    double makeupDb() const;

    /** The meters (PRD §9.2.1a), written by the audio thread: since the UI
        last took them, the input peaks, the most gain reduction (dB, positive)
        and the loudest level the detector read (peak or RMS, as a gain). */
    dsp::PeakSince inputLeft, inputRight, reduction, detector;

private:
    std::vector<float> delayLine;   ///< two channels interleaved; sized in initialise
    int delayWrite = 0, delaySize = 0;
    double heldDb = 0, envelopeDb = 0, meanSquare = 0;   ///< the detector's two stages, in dB of gain
    double makeupGlideDb = 0, wetGlide = 1, outGlideDb = 0;   ///< Makeup, Mix and Out on their way to their settings
    bool glideFromSettings = true;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorV2Plugin)
};

} // namespace resamper
#endif
