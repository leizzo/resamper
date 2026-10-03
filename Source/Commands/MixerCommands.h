#pragma once

#include "CommandRegistry.h"
#include "Engine/Mixer.h"

namespace resamper
{

struct AppCommandHost;

/** A new Return; invoked without args it is named "Return". */
struct ReturnArgs
{
    juce::String name = "Return";
};

/** A new Bus; invoked without args it is named "Bus". */
struct BusArgs
{
    juce::String name = "Bus";
};

/** A send from a track to a return's bus number. */
struct SendArgs
{
    juce::String trackId;
    int bus = 0;
};

/** A send fader. continuesGesture joins a drag into one undo step. */
struct SendGainArgs
{
    juce::String trackId;
    juce::String sendId;
    Decibels gain;
    bool continuesGesture = false;
};

/** Mutes or unmutes one of a track's sends. */
struct SendMutedArgs
{
    juce::String trackId;
    juce::String sendId;
    bool muted = false;
};

/** Routes a track into a Bus; busTrackId names the Bus track. */
struct MoveToBusArgs
{
    juce::String trackId;
    juce::String busTrackId;
};

/** The master fader; continuesGesture as for SendGainArgs. */
struct MasterVolumeArgs
{
    Decibels volume;
    bool continuesGesture = false;
};

namespace cmd
{
    inline constexpr CommandRef<ReturnArgs> mixerAddReturn { "mixer.addReturn" };
    inline constexpr CommandRef<SendArgs> mixerAddSend { "mixer.addSend" };
    inline constexpr CommandRef<SendGainArgs> mixerSetSendGain { "mixer.setSendGain" };
    inline constexpr CommandRef<SendMutedArgs> mixerSetSendMuted { "mixer.setSendMuted" };
    inline constexpr CommandRef<BusArgs> mixerAddBus { "mixer.addBus" };
    inline constexpr CommandRef<MoveToBusArgs> mixerMoveToBus { "mixer.moveToBus" };
    inline constexpr CommandRef<MasterVolumeArgs> mixerSetMasterVolume { "mixer.setMasterVolume" };
}

/** Registers the mixer Commands above. They delegate to Mixer; undo stays in the engine. */
void registerMixerCommands (CommandRegistry&, Mixer&, AppCommandHost&);

} // namespace resamper
