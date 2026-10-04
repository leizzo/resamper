#pragma once

#include "CommandRegistry.h"
#include "Engine/ApplicationModel.h"

namespace resamper
{

/** Names one track. */
struct TrackArgs
{
    juce::String trackId;
};

/** A click on a track: replaces the selection, or extends or toggles it. */
struct TrackSelectArgs
{
    juce::String trackId;   ///< empty with replace: nothing selected
    ApplicationModel::SelectionMode mode = ApplicationModel::SelectionMode::replace;
};

/** A track fader. A continuous gesture (a fader drag) passes continuesGesture
    for every value after its first, making the whole gesture one undo step. */
struct TrackVolumeArgs
{
    juce::String trackId;
    Decibels volume;
    bool continuesGesture = false;
};

/** A track pan knob; continuesGesture as for TrackVolumeArgs. */
struct TrackPanArgs
{
    juce::String trackId;
    double pan = 0;                ///< -1 (left) to 1 (right)
    bool continuesGesture = false;
};

/** A track's input: one named by ApplicationModel::getAudioInputs(), or empty for none. */
struct TrackInputArgs
{
    juce::String trackId;
    juce::String input;
};

/** A track's colour: an index into the track palette. */
struct TrackColourArgs
{
    juce::String trackId;
    int colourIndex = 0;
};

namespace cmd
{
    inline constexpr CommandRef<> trackAdd { "track.add" };
    inline constexpr CommandRef<> trackAddMidi { "track.addMidi" };
    inline constexpr CommandRef<> trackRemove { "track.remove" };
    inline constexpr CommandRef<TrackVolumeArgs> trackSetVolume { "track.setVolume" };
    inline constexpr CommandRef<TrackPanArgs> trackSetPan { "track.setPan" };
    inline constexpr CommandRef<TrackArgs> trackToggleMute { "track.toggleMute" };
    inline constexpr CommandRef<TrackArgs> trackToggleSolo { "track.toggleSolo" };
    inline constexpr CommandRef<TrackArgs> trackToggleArm { "track.toggleArm" };
    inline constexpr CommandRef<TrackInputArgs> trackSetInput { "track.setInput" };
    inline constexpr CommandRef<TrackColourArgs> trackSetColour { "track.setColour" };
    inline constexpr CommandRef<TrackSelectArgs> trackSelect { "track.select" };    ///< in every view; never undoable
    inline constexpr CommandRef<int> trackToggleMuteAt { "track.toggleMuteAt" };    ///< F1-F8: the track's 0-based index
    inline constexpr CommandRef<> trackToggleSoloSelected { "track.toggleSoloSelected" };
}

/** Registers the track Commands above. */
void registerTrackCommands (CommandRegistry&, ApplicationModel&);

} // namespace resamper
