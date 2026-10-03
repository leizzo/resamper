#include "TrackCommands.h"

namespace resamper
{

void registerTrackCommands (CommandRegistry& registry, ApplicationModel& model)
{
    registry.add (cmd::trackAdd, { "Add Audio Track" }, [&model] { model.addAudioTrack(); });
    registry.add (cmd::trackAddMidi, { "Add MIDI Track" }, [&model] { model.addMidiTrack(); });
    registry.add (cmd::trackRemove, { "Remove Track" }, [&model] { model.removeTrack(); });

    registry.add (cmd::trackSetVolume, { "Set Track Volume" }, [&model] (const TrackVolumeArgs& a)
    {
        model.setTrackVolume (a.trackId, a.volume, a.continuesGesture);
    });

    registry.add (cmd::trackSetPan, { "Set Track Pan" }, [&model] (const TrackPanArgs& a)
    {
        model.setTrackPan (a.trackId, a.pan, a.continuesGesture);
    });

    // Mute and solo also flip on a Bus.
    registry.add (cmd::trackToggleMute, { "Mute Track" }, [&model] (const TrackArgs& a)
    {
        model.setTrackMuted (a.trackId, ! model.isTrackMuted (a.trackId));
    });

    registry.add (cmd::trackToggleSolo, { "Solo Track" }, [&model] (const TrackArgs& a)
    {
        model.setTrackSolo (a.trackId, ! model.isTrackSolo (a.trackId));
    });

    registry.add (cmd::trackToggleArm, { "Arm Track for Recording" }, [&model] (const TrackArgs& a)
    {
        for (auto& track : model.getTracks())
            if (track.id == a.trackId)
                model.setTrackArmed (a.trackId, ! track.armed);
    });

    // A track header's input menu.
    registry.add (cmd::trackSetInput, { "Set Track Input" }, [&model] (const TrackInputArgs& a)
    {
        model.setTrackInput (a.trackId, a.input);
    });

    registry.add (cmd::trackSetColour, { "Set Track Colour" }, [&model] (const TrackColourArgs& a)
    {
        model.setTrackColour (a.trackId, a.colourIndex);
    });

    registry.add (cmd::trackSelect, { "Select Track" }, [&model] (const TrackArgs& a) { model.selectTrack (a.trackId); });

    registry.add (cmd::trackToggleMuteAt, { "Mute Track" }, [&model] (const int& index)
    {
        const auto tracks = model.getTracks();

        if (juce::isPositiveAndBelow (index, (int) tracks.size()))
            model.setTrackMuted (tracks[(size_t) index].id, ! tracks[(size_t) index].muted);
    });

    // S: solo the selected track.
    registry.add (cmd::trackToggleSoloSelected, { "Solo", [&model] { return model.getSelectedTrackId().isNotEmpty(); } },
                  [&model]
    {
        for (auto& track : model.getTracks())
            if (track.id == model.getSelectedTrackId())
                model.setTrackSolo (track.id, ! track.solo);
    });
}

} // namespace resamper
