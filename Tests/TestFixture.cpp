#include "TestFixture.h"
#include "App/UILanguage.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <tracktion_engine/tracktion_engine.h>

namespace resamper::test
{

namespace
{
    constexpr double sineSampleRate = 44100.0;

    /** A 440 Hz sine, its first toneSeconds a tone and silence after (all of it if 0). */
    juce::AudioBuffer<float> makeSine (double seconds, int numChannels, double toneSeconds)
    {
        const auto numSamples = (int) (seconds * sineSampleRate);
        const auto numToneSamples = toneSeconds > 0 ? std::min (numSamples, (int) (toneSeconds * sineSampleRate)) : numSamples;

        juce::AudioBuffer<float> buffer (numChannels, numSamples);
        buffer.clear();

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numToneSamples; ++i)
                buffer.setSample (ch, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sineSampleRate));

        return buffer;
    }

    juce::File writeSine (juce::AudioFormat& format, const juce::File& file, const juce::AudioBuffer<float>& buffer,
                          const std::unordered_map<juce::String, juce::String>& metadata)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
        auto writer = format.createWriterFor (out,
                                              juce::AudioFormatWriterOptions().withSampleRate (sineSampleRate)
                                                                              .withNumChannels (buffer.getNumChannels())
                                                                              .withBitsPerSample (16)
                                                                              .withMetadataValues (metadata));
        jassert (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

juce::File writeSineWav (const juce::File& file, double seconds, int numChannels, double acidTempo, double toneSeconds)
{
    std::unordered_map<juce::String, juce::String> metadata;

    if (acidTempo > 0)
    {
        using W = juce::WavAudioFormat;
        metadata = { { W::acidOneShot, "0" }, { W::acidRootSet, "0" }, { W::acidStretch, "1" },
                     { W::acidDiskBased, "0" }, { W::acidizerFlag, "0" }, { W::acidRootNote, "60" },
                     { W::acidBeats, juce::String (juce::roundToInt (seconds * acidTempo / 60.0)) },
                     { W::acidNumerator, "4" }, { W::acidDenominator, "4" },
                     { W::acidTempo, juce::String (acidTempo) } };
    }

    juce::WavAudioFormat wav;
    return writeSine (wav, file, makeSine (seconds, numChannels, toneSeconds), metadata);
}

juce::File writeSineFlac (const juce::File& file, double seconds, int numChannels)
{
    juce::FlacAudioFormat flac;
    return writeSine (flac, file, makeSine (seconds, numChannels, 0), {});
}

Fixture::Fixture()
{
    // Tests run in English (ADR-0015); a snapshot installs its language after this.
    installUILanguage (uiFiles, "en");
    scratchDir().createDirectory();

    auto pick = [] (juce::File& chosen)
    {
        return [&chosen] (AppCommandHost::FileCallback cb)
        {
            if (chosen != juce::File())
                cb (chosen);
        };
    };

    host.chooseAudioFile = pick (audioFileToChoose);
    host.chooseProjectToOpen = pick (projectToOpen);
    host.chooseProjectSaveLocation = pick (projectSaveLocation);
    host.choosePluginFile = pick (pluginFileToChoose);
    host.reportError = [this] (const juce::String& e) { errors.add (e); };
    host.notify = [this] (const juce::String& n, bool) { notifications.add (n); };
}

Fixture::~Fixture()
{
    scratchDir().deleteRecursively();
}

float renderPeak (Fixture& f)
{
    auto rendered = f.scratchDir().getChildFile ("render.wav");
    rendered.deleteFile();

    auto& edit = f.projects.getEdit();

    // As render::toWav does: an Edit that has restarted playback (a plug-in
    // loaded in the background, say) has a playback context; this frees it and
    // keeps it from being rebuilt while rendering. false: headless, don't reattach.
    const tracktion::Edit::ScopedRenderStatus renderStatus (edit, false);

    // Not Renderer::renderToFile (Edit&, File): that un-mutes and solo-isolates every track.
    tracktion::Renderer::Parameters params (edit);
    params.destFile = rendered;
    params.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    params.bitDepth = 24;
    params.sampleRateForAudio = edit.engine.getDeviceManager().getSampleRate();
    params.blockSizeForAudio = edit.engine.getDeviceManager().getBlockSize();
    params.time = { tracktion::TimePosition(), edit.getLength() };
    params.tracksToDo = tracktion::toBitSet (tracktion::getAllTracks (edit));
    params.usePlugins = params.useMasterPlugins = true;

    if (! tracktion::Renderer::renderToFile ({}, params).existsAsFile())
        return 0.0f;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (rendered));

    if (reader == nullptr || reader->lengthInSamples == 0)
        return 0.0f;

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer.getMagnitude (0, buffer.getNumSamples());
}

} // namespace resamper::test
