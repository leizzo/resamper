#pragma once

#include "TestFixture.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper::test
{

/** Feeds audio into the engine through its hosted device, in place of a
    sound card: the engine's inputs are the hosted device's input channels. */
struct HostedAudio
{
    HostedAudio()
    {
        // Blocks are pushed far faster than real time; don't let the CPU guard mute them.
        deviceManager.setCpuLimitBeforeMuting (1000.0);

        tracktion::HostedAudioDeviceInterface::Parameters params;
        params.sampleRate = sampleRate;
        params.blockSize = blockSize;
        params.inputChannels = 2;
        params.outputChannels = 2;
        io.initialise (params);
        io.prepareToPlay (sampleRate, blockSize);
        deviceManager.dispatchPendingUpdates();
    }

    // The hosted interface stays for the whole run: the engine keeps the first
    // hosted MIDI input it made (a rescan reuses devices by name), and only the
    // interface that made it feeds it.
    ~HostedAudio()
    {
        deviceManager.deviceManager.closeAudioDevice();
    }

    /** Runs the engine for this long with a sine wave on every input and,
        with playNotes, a short middle C at the start of every other block on the
        MIDI input. Returns the output's peak. */
    float process (double seconds, bool playNotes = false)
    {
        float outputPeak = 0.0f;

        const auto total = (int) (seconds * sampleRate);

        for (int done = 0, index = 0; done < total; done += blockSize, ++index)
        {
            juce::MidiBuffer midi;

            if (playNotes)
                midi.addEvent (index % 2 == 0 ? juce::MidiMessage::noteOn (1, 60, 0.8f) : juce::MidiMessage::noteOff (1, 60), 0);

            juce::AudioBuffer<float> block (2, std::min (blockSize, total - done));

            for (int ch = 0; ch < block.getNumChannels(); ++ch)
                for (int i = 0; i < block.getNumSamples(); ++i)
                    block.setSample (ch, i, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (done + i) / sampleRate));

            io.processBlock (block, midi);
            outputPeak = std::max (outputPeak, block.getMagnitude (0, block.getNumSamples()));
        }

        return outputPeak;
    }

    static constexpr double sampleRate = 44100.0;
    static constexpr int blockSize = 512;

    tracktion::DeviceManager& deviceManager { getEngineManager().getEngine().getDeviceManager() };
    tracktion::HostedAudioDeviceInterface& io { deviceManager.getHostedAudioDeviceInterface() };
};

} // namespace resamper::test
