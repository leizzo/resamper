#pragma once

#include "ApplicationModel.h"
#include "PluginRack.h"

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace resamper
{

class ProjectManager;

/** One aux send on a track. gain uses the track fader range; ApplicationModel::minVolume is silence. */
struct SendInfo
{
    juce::String id;
    int bus = 0;
    Decibels gain;
    bool muted = false;
};

/** An audio track that carries an aux return. bus matches that return. */
struct ReturnInfo
{
    juce::String trackId;
    int bus = 0;
    juce::String name;
};

/** A submix folder. childTrackIds are its direct children. */
struct BusInfo
{
    juce::String trackId;
    juce::String name;
    std::vector<juce::String> childTrackIds;
};

/** A meter reading: the peak of each side since the last read, in dB. */
struct StereoLevel
{
    float left = -100.0f, right = -100.0f;
};

/** What a Strip stands for. The Master is not a Strip: see Mixer::getMaster(). */
enum class StripRole { track, bus, returnTrack };

/** One Strip in the Mixer: everything its column shows, read from the Edit. */
struct Strip
{
    juce::String id;                        ///< the track's id
    juce::String name;
    StripRole role = StripRole::track;
    TrackKind kind = TrackKind::audio;      ///< a Bus or Return reports audio
    int number = 0;                         ///< 1-based among track Strips; 0 on a Bus or Return
    juce::String returnLetter;              ///< A..D on a Return
    int colourIndex = 0;
    bool selected = false;
    Decibels volume;                        ///< ApplicationModel::minVolume is silence
    double pan = 0;                         ///< -1 (left) to 1 (right)
    bool muted = false, solo = false;
    juce::String input;                     ///< the input it records from, or empty
    bool armed = false;
    std::vector<SendInfo> sends;
    std::vector<PluginInfo> inserts;        ///< the Mixer Inserts
    std::vector<PluginInfo> deviceChain;    ///< read-only here, edited in the Detail View
    juce::String output = "Master";         ///< the Bus it sums into, or the Master
    int childCount = 0;                     ///< on a Bus: its direct children
};

/** The letter a Return and the Sends to it show for its bus: A for bus 0. */
juce::String returnLetterFor (int bus);

/** The Edit's master fader, not any track in ApplicationModel::getTracks(). */
struct MasterInfo
{
    Decibels volume;
};

/** Facade over the current Edit's returns, sends, submix buses and master fader,
    and the read model of the Mixer's Strips. Owns none of that state. Re-reads
    ProjectManager::getEdit() on every call. Nothing above this layer sees a Tracktion header.

    Undoable (Engine Undo): adding a return, a send or a bus, moving a track into
    a bus, send gain, and master volume. A continued fader drag
    (continuesGesture) joins the previous step when that step is still the same
    gesture. A call that changes nothing returns false and opens no undo step.

    Send mute follows AuxSendPlugin, which writes the send gain through the
    UndoManager, so mute is one undo step — unlike track mute, which the engine
    keeps out of undo.
*/
class Mixer
{
public:
    /** Reads tracks through the Application Model and chains through the
        PluginRack; both must outlive it. */
    Mixer (ProjectManager&, const ApplicationModel&, const PluginRack&);
    ~Mixer();

    /** An audio track with an aux return on the next free bus number. */
    juce::Result addReturn (const juce::String& name);

    std::vector<ReturnInfo> getReturns() const;

    /** An aux send on fromTrackId aimed at a return's bus. Fails, changing
        nothing, when the track or the bus's return is missing. */
    juce::Result addSend (const juce::String& fromTrackId, int bus);

    /** Clamped to ApplicationModel::minVolume .. maxVolume. continuesGesture
        as for ApplicationModel::setTrackVolume. */
    bool setSendGain (const juce::String& trackId, const juce::String& sendId, Decibels gain, bool continuesGesture = false);

    /** One undo step when it changes something. See the class note. */
    bool setSendMuted (const juce::String& trackId, const juce::String& sendId, bool muted);

    std::vector<SendInfo> getSends (const juce::String& trackId) const;

    /** A submix folder track. */
    juce::Result addBus (const juce::String& name);

    /** Nests an existing audio or MIDI track inside the bus folder. */
    bool moveTrackToBus (const juce::String& trackId, const juce::String& busTrackId);

    std::vector<BusInfo> getBuses() const;

    /** Every Strip but the Master's, in signal-flow order: tracks in Edit order,
        each Bus right after its last child (nested Buses alike), then the
        Returns A..D. Folder-only Folders have no Strip. */
    std::vector<Strip> getStrips() const;

    MasterInfo getMaster() const;

    /** Peaks of the track's level meter since the last read, left and right,
        in dB. Silence (ApplicationModel::minVolume) when the track has no meter or the meter has
        not seen audio. A mono signal reads the same on both sides. */
    StereoLevel getTrackLevel (const juce::String& trackId);

    /** The master track's meter. Same silence rule. */
    StereoLevel getMasterLevel();

    /** Whether the meters measure RMS (the mixer's RMS and LUFS modes) rather than peak. */
    void setMeasuringRms (bool);

    /** Master fader, not a track fader. continuesGesture as for setSendGain. */
    bool setMasterVolume (Decibels volume, bool continuesGesture = false);

private:
    struct MeterState;

    ProjectManager& projects;
    const ApplicationModel& model;
    const PluginRack& plugins;
    std::unique_ptr<MeterState> meters;
    bool measuringRms = false;

    /** meterPlugin is a tracktion::LevelMeterPlugin*. Kept as void* so this
        header stays free of Tracktion types. */
    StereoLevel levelOf (const juce::String& slotId, void* meterPlugin);

    JUCE_DECLARE_NON_COPYABLE (Mixer)
};

} // namespace resamper
