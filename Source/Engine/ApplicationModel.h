#pragma once

#include "ClipWaveform.h"
#include "Decibels.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

namespace resamper
{

class ProjectManager;

/** Audio or MIDI. A track holds clips of its own kind (brief §5). */
enum class TrackKind { audio, midi };

/** One note of a MIDI clip. Times are within the clip, from its start. */
struct MidiNoteInfo
{
    int pitch = 0;              ///< 0..127
    double startSeconds = 0;    ///< from the clip's start
    double lengthSeconds = 0;
    juce::String id;
    int velocity = 0;           ///< 1..127
    bool selected = false;
};

/** Read-only snapshot of a clip, for views. */
struct ClipInfo
{
    juce::String id;
    juce::String name;
    double startSeconds = 0;
    double lengthSeconds = 0;
    double sourceOffsetSeconds = 0;   ///< where in the source the clip starts
    double sourceLengthSeconds = 0;   ///< how far the clip can extend (the source audio, or the Edit, for MIDI)
    juce::File file;
    bool selected = false;
    int numTakes = 0;       ///< passes of a loop recording; 0 for a clip without takes
    int currentTake = -1;   ///< the take playing, or -1
    TrackKind kind = TrackKind::audio;
    std::vector<MidiNoteInfo> notes;
    bool looping = false;             ///< its content repeats (loop-extend)
    double loopLengthSeconds = 0;     ///< the repeating part, when looping
    bool reversed = false;            ///< an audio clip playing backwards
    int colourIndex = -1;             ///< into the track palette; -1: the track's colour
    juce::File playbackFile;          ///< the file an audio clip plays
};

/** Read-only snapshot of a track, for views. */
struct TrackInfo
{
    juce::String id;
    juce::String name;
    TrackKind kind = TrackKind::audio;
    bool selected = false;
    int colourIndex = 0;   ///< into the track palette (clip-drums .. clip-fx)
    Decibels volume;       ///< ApplicationModel::minVolume is silence
    double pan = 0;        ///< -1 (left) to 1 (right)
    bool muted = false;
    bool solo = false;
    juce::String input;    ///< the audio input it records from, or empty
    bool armed = false;    ///< records its input when the transport records
    bool isReturn = false; ///< a return track (holds an aux return); it never records
    std::vector<ClipInfo> clips;
};

/** A recording in progress on one track. */
struct RecordingInfo
{
    juce::String trackId;
    double startSeconds = 0;
    double lengthSeconds = 0;
};

/** A time range on the timeline, in seconds. */
struct TimeRangeSeconds
{
    double start = 0, end = 0;
};

/** A timeline position as bars.beats.sixteenths, each counted from 1. */
struct BarsBeats
{
    int bar = 1, beat = 1, sixteenth = 1;
};

/** The Edit's time signature, e.g. 6 / 8. */
struct TimeSignature
{
    int numerator = 4, denominator = 4;
};

/** The facade over the current Edit.

    Exposes app-level operations and read-only snapshots; owns no track or clip
    state of its own. Nothing above this layer sees a Tracktion header.

    Undoable (Engine Undo): adding/removing tracks (audio and MIDI), track volume
    and pan, inserting, moving, resizing and splitting clips, switching a
    clip's take, each recording, and adding, deleting, moving, resizing,
    changing the velocity of, and quantizing MIDI notes — one undo step per
    call that changes something. A continued velocity gesture joins the previous
    step, the way a fader drag does. A clip only moves onto a track of its own
    kind. Not undoable: transport (including the loop), selection (clips and
    notes), mute, solo, track input and arming (the engine keeps mute, solo and
    inputs out of its UndoManager). The track, clip and note operations return
    false, recording no undo step, when they would change nothing (unknown clip
    or note, same value or position, empty range, unknown quantize grid).
*/
class ApplicationModel
{
public:
    struct Listener
    {
        virtual ~Listener() = default;

        /** Called asynchronously on the message thread after any change to the
            Edit (tracks, clips, Sends, Returns, Buses, the Master, Mixer
            Inserts, Device Chains, automation; not transport state), the
            selection or the current Project. It carries no payload: a listener
            re-reads what it shows. */
        virtual void modelChanged() = 0;
    };

    explicit ApplicationModel (ProjectManager&);
    ~ApplicationModel();

    //==============================================================================
    // Project
    void newProject();
    juce::Result openProject (const juce::File& folder, juce::var& uiState);
    juce::Result saveProject (const juce::var& uiState);
    juce::Result saveProjectAs (const juce::File& folder, const juce::var& uiState);
    bool isProjectUntitled() const;
    juce::String getProjectName() const;

    //==============================================================================
    // Model mutations (each one is a single Engine Undo step)
    void addAudioTrack();

    /** Adds a MIDI track: an audio track marked MIDI, with the built-in instrument. */
    void addMidiTrack();

    /** Removes the selected track, or the last one if none is selected.
        Returns false if there is no track to remove. */
    bool removeTrack();

    /** Sets a track's volume, clamped to [minVolume, maxVolume]. The fader,
        pan, mute and solo setters take a Bus's id as well as an audio track's.

        With continuesGesture, the change joins the undo step of the previous call
        if that was a volume change on the same track with nothing undoable in
        between — so a whole fader drag is one undo step. Otherwise it starts one. */
    bool setTrackVolume (const juce::String& trackId, Decibels volume, bool continuesGesture = false);

    /** Sets a track's pan, clamped to [-1, 1]; continuesGesture as for setTrackVolume. */
    bool setTrackPan (const juce::String& trackId, double pan, bool continuesGesture = false);

    /** The track palette's size (PRD §15.1: clip-drums .. clip-fx). */
    static constexpr int trackPaletteSize = 7;

    /** Sets the track's palette colour, 0 .. trackPaletteSize - 1. One undo step. */
    bool setTrackColour (const juce::String& trackId, int colourIndex);

    /** Never undoable. */
    bool setTrackMuted (const juce::String& trackId, bool muted);
    bool setTrackSolo (const juce::String& trackId, bool solo);

    /** False for an unknown id. */
    bool isTrackMuted (const juce::String& trackId) const;
    bool isTrackSolo (const juce::String& trackId) const;

    //==============================================================================
    // Inputs (never undoable). An audio track records from an audio input, a
    // MIDI track from a MIDI input.

    /** The engine's enabled audio inputs, by name. */
    juce::StringArray getAudioInputs() const;

    /** The engine's enabled MIDI inputs, by name ("All MIDI Ins" first). */
    juce::StringArray getMidiInputs() const;

    /** The inputs a track of this kind records from. */
    juce::StringArray getInputs (TrackKind kind) const   { return kind == TrackKind::midi ? getMidiInputs() : getAudioInputs(); }

    /** Makes the named input the track's only one; an empty name removes the
        track's input, which also disarms it. Returns false for an unknown track,
        an input of the other kind, or if nothing changed. */
    bool setTrackInput (const juce::String& trackId, const juce::String& inputName);

    /** Arms or disarms a track. Arming a track without an input first gives it
        the first input of its kind; it fails if there is none. A return track
        never arms. A MIDI track records notes into a MIDI clip and, while armed,
        plays what comes in through its instrument. */
    bool setTrackArmed (const juce::String& trackId, bool armed);

    /** The engine's fader range; minVolume is silence. */
    static constexpr Decibels minVolume { -100.0 }, maxVolume { 6.0 };

    /** MIDI velocity written by note.add when a gesture doesn't choose one. */
    static constexpr int defaultNoteVelocity = 100;
    static constexpr int minNoteVelocity = 1, maxNoteVelocity = 127;

    /** Inserts the file as a clip at the end of the selected audio track, or the
        selected clip's track, or the first audio track — creating one if the Edit
        has none. Fails, changing nothing, when that track is a MIDI track. */
    juce::Result insertAudioClip (const juce::File&);

    /** Inserts the file as a clip on that audio track, starting at startSeconds
        (a drop from the Browser). Fails, changing nothing, on a MIDI track or a
        file that isn't audio. */
    juce::Result insertAudioClipAt (const juce::File&, const juce::String& trackId, double startSeconds);

    /** Inserts an empty MIDI clip at the playhead, one bar long, on the selected
        MIDI track, or the selected MIDI clip's track, or the first MIDI track
        when nothing is selected. Fails, changing nothing, when the selected
        track is not a MIDI track or the Edit has none. */
    juce::Result insertMidiClip();

    /** Moves a clip to start at startSeconds (clamped to the Edit start), onto
        the track trackId if given. */
    bool moveClip (const juce::String& clipId, double startSeconds, const juce::String& trackId = {});

    /** Sets a clip's edges, clamped to its source audio. The audio stays where it
        is on the timeline: trimming the start advances the source offset. */
    bool resizeClip (const juce::String& clipId, double startSeconds, double endSeconds);

    /** Cuts a clip in two at timeSeconds; see canSplitClip. The left part keeps
        the clip's ID (and its selection). */
    bool splitClip (const juce::String& clipId, double timeSeconds);

    /** Whether timeSeconds lies far enough inside the clip for a cut. */
    bool canSplitClip (const juce::String& clipId, double timeSeconds) const;

    /** Makes one of a clip's takes (0-based) the one it plays. */
    bool setClipTake (const juce::String& clipId, int takeIndex);

    /** Copies the clip to start at startSeconds on trackId (its own track when
        empty); a clip only lands on a track of its kind. The copy is selected. */
    juce::Result copyClip (const juce::String& clipId, double startSeconds, const juce::String& trackId = {});

    /** Duplicates each selected clip right after itself; the copies become the selection. */
    bool duplicateSelectedClips();

    /** Makes the clip repeat its content (its current length is the loop) up
        to endSeconds (PRD §8.2, the top-right corner drag). */
    bool loopExtendClip (const juce::String& clipId, double endSeconds);

    /** Joins the selected clips of one track into one clip spanning them:
        MIDI clips merge their notes; audio clips render (without the track's
        plug-ins) to a new file in the Project's Audio folder. Fails with fewer
        than two clips, clips on several tracks, or mixed kinds. */
    juce::Result consolidateSelectedClips();

    /** Removes every selected clip. */
    bool deleteSelectedClips();

    bool renameClip (const juce::String& clipId, const juce::String& name);

    /** Plays an audio clip backwards, or forwards again. False for a MIDI clip. */
    bool reverseClip (const juce::String& clipId);

    /** The clip's own palette colour, or -1 for its track's. */
    bool setClipColour (const juce::String& clipId, int colourIndex);

    //==============================================================================
    // MIDI notes. Times are seconds from the clip's start. The new note is selected.
    bool addNote (const juce::String& clipId, double startSeconds, double lengthSeconds, int pitch, int velocity);

    /** Removes every selected note, whichever clip it is on. */
    bool deleteSelectedNotes();

    /** Moves the named notes by the same amount. Pitch stays inside 0..127 and
        no note starts before the clip, and the whole group keeps its shape. */
    bool moveNotes (const juce::String& clipId, const juce::StringArray& noteIds, double deltaSeconds, int deltaPitch);

    /** Sets one note's edges, in seconds from the clip's start. */
    bool resizeNote (const juce::String& clipId, const juce::String& noteId, double startSeconds, double endSeconds);

    /** Sets the velocity of the clip's selected notes. continuesGesture joins
        this write to the previous velocity change on the same clip. */
    bool setNoteVelocity (const juce::String& clipId, int velocity, bool continuesGesture = false);

    /** Snaps note starts onto a grid of "1/4", "1/8" or "1/16", keeping each
        note's length. Selected notes in the clip, or every note when none are selected. */
    bool quantizeNotes (const juce::String& clipId, const juce::String& grid);

    //==============================================================================
    // Engine Undo (a selected clip stays selected if it survives)
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    //==============================================================================
    // Selection (engine SelectionManager; never undoable)
    /** A click replaces the selection; Shift adds; Mod toggles (PRD §16.1). */
    enum class SelectionMode { replace, add, toggle };

    void selectTrack (const juce::String& trackId, SelectionMode = SelectionMode::replace);

    /** Clears every selection: tracks, clips and notes (Esc). */
    void deselectAll();
    void selectClip (const juce::String& clipId, SelectionMode = SelectionMode::replace);
    juce::String getSelectedTrackId() const;

    /** The first selected clip, or empty. */
    juce::String getSelectedClipId() const;
    juce::StringArray getSelectedClipIds() const;

    /** Replaces the note selection. Never an undo step. Ids that don't match a
        note simply aren't shown selected. */
    void selectNotes (const juce::StringArray& noteIds);
    bool hasSelectedNotes() const;

    //==============================================================================
    // Tempo and time signature (Engine Undo), at the start of the Edit
    double getTempo() const;

    /** Clamped to [minTempo, maxTempo]; continuesGesture as for setTrackVolume. */
    bool setTempo (double bpm, bool continuesGesture = false);

    static constexpr double minTempo = 20.0, maxTempo = 300.0;   ///< the engine's tempo range

    TimeSignature getTimeSignature() const;

    /** numerator 1..32; denominator 1, 2, 4, 8 or 16. Returns false otherwise, or if unchanged. */
    bool setTimeSignature (int numerator, int denominator);

    /** Where a timeline position falls in bars, beats and sixteenths. Before the
        start (a count-in) the bars run 0, -1, ... back from bar 1. */
    BarsBeats toBarsBeats (double seconds) const;

    //==============================================================================
    // Transport (never undoable)

    /** Stopped: plays from the loop start when looping, else from the insert
        marker. Playing: restarts from the insert marker (PRD §6.1). */
    void play();

    /** Stops, keeping the position; stopping while stopped returns to the start.
        A recording in progress becomes clips on its tracks, as one undo step. */
    void stop();

    /** Where the user last put the playhead (setTransportPosition); Play starts here. */
    double getInsertMarkerSeconds() const;

    /** The metronome (the engine's click track). Never undoable. */
    bool isMetronomeOn() const;
    void setMetronomeOn (bool);

    /** A 2-bar count-in before recording: the engine's own count-in, with the
        click sounding through it whatever the metronome. Off by default; an
        engine setting (kept across sessions), not part of the Project. */
    bool isCountInOn() const;
    void setCountInOn (bool);

    /** The audio engine's CPU load, 0..1. */
    float getCpuUsage() const;

    /** Plays and records every armed track's input. With the count-in on, and
        withCountIn, it first counts in 2 bars; recording starts at the playhead
        either way. While looping, each pass through the loop becomes a take of
        one clip. Fails, doing nothing, if no track is armed or the loop is
        shorter than minLoopRecordingSeconds. */
    juce::Result record (bool withCountIn = true);

    /** The engine won't loop-record a shorter loop. */
    static constexpr double minLoopRecordingSeconds = 2.0;

    /** Moves the playhead and the insert marker to 0. Ends a recording in
        progress first, as stop() does. */
    void returnToStart();

    /** Moves the playhead and the insert marker; play starts from here. Clamped
        at 0. Does nothing, and returns false, while recording. */
    bool setTransportPosition (double seconds);

    bool isPlaying() const;
    bool isRecording() const;
    double getTransportPositionSeconds() const;

    /** The loop is fixed while recording: these change nothing then. */
    void setLooping (bool);
    bool isLooping() const;

    /** Sets the loop; returns false for an empty range. */
    bool setLoopRange (double startSeconds, double endSeconds);
    TimeRangeSeconds getLoopRange() const;

    //==============================================================================
    // Queries
    std::vector<TrackInfo> getTracks() const;

    /** One arrangement clip, as in getTracks(); nothing for an unknown id. */
    std::optional<ClipInfo> getClip (const juce::String& clipId) const;

    /** Edit-timeline conversions, for the Piano Roll's beat grid. */
    double secondsToBeats (double seconds) const;
    double beatsToSeconds (double beats) const;

    /** Time-signature numerator at a timeline position: how many quarter-notes in a bar. */
    int getBeatsPerBar (double seconds) const;

    /** Creates a background-generated waveform for the clip; repaintTarget is
        repainted as data arrives. It stays valid while ClipInfo::playbackFile
        does. Returns nullptr for an unknown clip. */
    std::unique_ptr<ClipWaveform> createWaveform (const juce::String& clipId,
                                                  juce::Component& repaintTarget) const;

    /** The recordings in progress. Poll it: it changes with every audio block
        and sends no modelChanged(). */
    std::vector<RecordingInfo> getRecordings() const;

    /** The waveform of a track's recording in progress, filled in as audio
        arrives; repaint to see it.
        Returns nullptr if the track isn't recording. */
    std::unique_ptr<ClipWaveform> createRecordingWaveform (const juce::String& trackId) const;

    void addListener (Listener*) const;
    void removeListener (Listener*) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE (ApplicationModel)
};

} // namespace resamper
