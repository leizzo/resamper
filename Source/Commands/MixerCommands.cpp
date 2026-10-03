#include "MixerCommands.h"
#include "AppCommandHost.h"

namespace resamper
{

void registerMixerCommands (CommandRegistry& registry, Mixer& mixer, AppCommandHost& host)
{
    registry.add (cmd::mixerAddReturn, { "Add Return" }, [&mixer, &host] (const ReturnArgs& a)
    {
        host.report (mixer.addReturn (a.name));
    });

    registry.add (cmd::mixerAddSend, { "Add Send" }, [&mixer, &host] (const SendArgs& a)
    {
        host.report (mixer.addSend (a.trackId, a.bus));
    });

    // A send fader: one undo step per drag.
    registry.add (cmd::mixerSetSendGain, { "Set Send Gain" }, [&mixer] (const SendGainArgs& a)
    {
        mixer.setSendGain (a.trackId, a.sendId, a.gain, a.continuesGesture);
    });

    registry.add (cmd::mixerSetSendMuted, { "Mute Send" }, [&mixer] (const SendMutedArgs& a)
    {
        mixer.setSendMuted (a.trackId, a.sendId, a.muted);
    });

    registry.add (cmd::mixerAddBus, { "Add Bus" }, [&mixer, &host] (const BusArgs& a)
    {
        host.report (mixer.addBus (a.name));
    });

    registry.add (cmd::mixerMoveToBus, { "Move to Bus" }, [&mixer] (const MoveToBusArgs& a)
    {
        mixer.moveTrackToBus (a.trackId, a.busTrackId);
    });

    // The master fader: one undo step per drag.
    registry.add (cmd::mixerSetMasterVolume, { "Set Master Volume" }, [&mixer] (const MasterVolumeArgs& a)
    {
        mixer.setMasterVolume (a.volume, a.continuesGesture);
    });
}

} // namespace resamper
