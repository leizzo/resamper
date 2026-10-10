#include "ApplicationModel.h"
#include "ClipWaveformImpl.h"
#include "EditTracks.h"
#include "ProjectManager.h"
#include "Render.h"

#include <tracktion_engine/tracktion_engine.h>

#include <cmath>

namespace te = tracktion;

namespace resamper
{

struct ApplicationModel::Impl : private juce::ValueTree::Listener,
                                private juce::ChangeListener,
                                private juce::AsyncUpdater
{
    explicit Impl (ProjectManager& pm) : projectManager (pm)
    {
        selectionManager.addChangeListener (this);
        attach();
    }

    ~Impl() override
    {
        detach();
        selectionManager.removeChangeListener (this);
    }

    te::Edit& edit() const          { return projectManager.getEdit(); }
    juce::UndoManager& undoManager() { return edit().getUndoManager(); }
    EngineUndo& undo()              { return projectManager.getUndo(); }

    /** App-specific, on a MIDI note's ValueTree, so a note can be named across undo. */
    static const juce::Identifier noteIdProperty;

    /** A new track takes the palette colour after the last track's. */
    void giveNextColour (te::AudioTrack& track)
    {
        auto tracks = te::getAudioTracks (edit());
        const auto index = tracks.indexOf (&track);
        const auto colour = index > 0 ? (colourOf (*tracks[index - 1]) + 1) % trackPaletteSize : 0;
        track.state.setProperty (colourProperty, colour, &undoManager());
    }

    juce::StringArray selectedNoteIds;

    //==============================================================================
    /** Must wrap every replacement of the current Edit. */
    template <typename Fn>
    auto replacingEdit (Fn&& fn)
    {
        detach();
        auto result = fn();
        attach();
        insertMarkerSeconds = 0;
        notifyChanged();
        return result;
    }

    void attach()
    {
        selectionManager.edit = &edit();
        editState = edit().state;
        editState.addListener (this);
    }

    void detach()
    {
        editState.removeListener (this);
        editState = {};
        selectionManager.deselectAll();
        selectionManager.edit = nullptr;
        selectedNoteIds.clear();
    }

    //==============================================================================
    /** Changes a track's volume/pan through the engine's parameter, which records
        the change in the Edit's UndoManager (consecutive writes within one undo
        step coalesce). Returns whether the value changed. */
    template <typename Get, typename Set>
    bool changeVolumePlugin (const juce::String& trackId, const juce::String& stepName, bool continues, Get get, Set set)
    {
        auto* track = findStripTrack (edit(), trackId);
        auto* plugin = track != nullptr ? faderOf (*track) : nullptr;

        if (plugin == nullptr)
            return false;

        // Undoing the first change of a default value would remove the property,
        // leaving nothing for syncVolumeParametersFromState to compare against.
        for (auto* value : { &plugin->volume, &plugin->pan })
            if (value->isUsingDefault())
                plugin->state.setProperty (value->getPropertyID(), value->get(), nullptr);

        // An undo step that ends up empty records nothing.
        const auto before = get (*plugin);
        undo().beginGestureStep (stepName, stepName + ":" + trackId, continues);
        set (*plugin);
        return get (*plugin) != before;
    }

    /** Undo and redo restore plugin state, but a parameter never re-reads its
        state by itself (the engine expects changes through the parameter). Only
        stale parameters are touched: re-reading writes the state back, and a
        write after an undo would clear the redo history. */
    void syncAttached (te::AutomatableParameter* parameter, const juce::CachedValue<float>* value)
    {
        if (parameter != nullptr && value != nullptr && parameter->getCurrentValue() != value->get())
            parameter->updateFromAttachedValue();
    }

    void syncVolumeParametersFromState()
    {
        for (auto* t : te::getAllTracks (edit()))
        {
            if (auto* plugin = faderOf (*t))
            {
                syncAttached (plugin->volParam.get(), &plugin->volume);
                syncAttached (plugin->panParam.get(), &plugin->pan);
            }

            for (auto* send : t->pluginList.getPluginsOfType<te::AuxSendPlugin>())
                syncAttached (send->gain.get(), &send->gainLevel);
        }

        if (auto master = edit().getMasterVolumePlugin())
        {
            syncAttached (master->volParam.get(), &master->volume);
            syncAttached (master->panParam.get(), &master->pan);
        }
    }

    //==============================================================================
    te::TransportControl& transport() const   { return edit().getTransport(); }

    /** The Edit's inputs a track of this kind records from: audio inputs, or
        physical and virtual MIDI inputs. Allocates the playback context, which owns them. */
    juce::Array<te::InputDeviceInstance*> inputsFor (TrackKind kind) const
    {
        transport().ensureContextAllocated();
        juce::Array<te::InputDeviceInstance*> inputs;

        if (auto* context = edit().getCurrentPlaybackContext())
            for (auto* input : context->getAllInputs())
            {
                const auto type = input->getInputDevice().getDeviceType();
                const auto midi = type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice;

                if (kind == TrackKind::midi ? midi : type == te::InputDevice::waveDevice)
                    inputs.add (input);
            }

        return inputs;
    }

    /** Every input any track records from, audio then MIDI. */
    juce::Array<te::InputDeviceInstance*> recordingInputs() const
    {
        auto inputs = inputsFor (TrackKind::audio);
        inputs.addArray (inputsFor (TrackKind::midi));
        return inputs;
    }

    te::InputDeviceInstance* findInput (TrackKind kind, const juce::String& name) const
    {
        for (auto* input : inputsFor (kind))
            if (input->getInputDevice().getName() == name)
                return input;

        return nullptr;
    }

    /** The input a track records from (a track has at most one). */
    te::InputDeviceInstance* inputOf (const te::AudioTrack& track) const
    {
        return inputOf (track, recordingInputs());
    }

    te::InputDeviceInstance* inputOf (const te::AudioTrack& track, const juce::Array<te::InputDeviceInstance*>& inputs) const
    {
        for (auto* input : inputs)
            if (input->getTargets().contains (track.itemID))
                return input;

        return nullptr;
    }

    /** A take's audio file; takes store their source as a clip does. */
    juce::File takeFile (juce::ValueTree take) const
    {
        return te::SourceFileReference (edit(), take, te::IDs::source).getFile();
    }

    static juce::ValueTree takesOf (const te::Clip& clip)   { return clip.state.getChildWithName (te::IDs::TAKES); }

    int currentTakeOf (te::WaveAudioClip& clip) const
    {
        const auto takes = takesOf (clip);
        const auto playing = clip.getSourceFileReference().getFile();

        for (int i = 0; i < takes.getNumChildren(); ++i)
            if (takeFile (takes.getChild (i)) == playing)
                return i;

        return -1;
    }

    /** A loop recording lists its first pass twice among its takes (the engine
        adds the file both before and after cutting the later passes out of it). */
    void removeDuplicateTakes()
    {
        for (auto* t : te::getAudioTracks (edit()))
        {
            for (auto* c : t->getClips())
            {
                auto takes = takesOf (*c);
                juce::Array<juce::File> seen;

                for (int i = 0; i < takes.getNumChildren();)
                {
                    if (auto file = takeFile (takes.getChild (i)); seen.contains (file))
                    {
                        takes.removeChild (i, &undoManager());
                    }
                    else
                    {
                        seen.add (file);
                        ++i;
                    }
                }
            }
        }
    }

    //==============================================================================
    te::Clip* findClip (const juce::String& id) const
    {
        for (auto* t : te::getAudioTracks (edit()))
            for (auto* c : t->getClips())
                if (c->itemID.toString() == id)
                    return c;

        return nullptr;
    }

    te::Clip* selectedClip() const
    {
        auto selected = selectionManager.getItemsOfType<te::Clip>();
        return selected.isEmpty() ? nullptr : selected.getFirst();
    }

    te::AudioTrack* selectedTrack() const
    {
        auto selected = selectionManager.getItemsOfType<te::AudioTrack>();
        return selected.isEmpty() ? nullptr : selected.getFirst();
    }

    juce::String noteId (const te::MidiNote& note) const
    {
        return note.state[noteIdProperty].toString();
    }

    te::MidiNote* findNote (te::MidiClip& clip, const juce::String& id) const
    {
        if (id.isEmpty())
            return nullptr;

        for (auto* note : clip.getSequence().getNotes())
            if (noteId (*note) == id)
                return note;

        return nullptr;
    }

    /** A note's beat is measured from the clip's content, not the Edit start. */
    double noteBeatForLocalSeconds (const te::MidiClip& clip, double localSeconds) const
    {
        const auto editTime = clip.getPosition().getStart() + te::TimeDuration::fromSeconds (localSeconds);
        const auto editBeats = edit().tempoSequence.toBeats (editTime).inBeats();
        return editBeats - clip.getContentStartBeat().inBeats() + clip.getLoopStartBeats().inBeats();
    }

    double localSecondsForNoteBeat (const te::MidiClip& clip, double noteBeat) const
    {
        const auto editBeats = noteBeat - clip.getLoopStartBeats().inBeats() + clip.getContentStartBeat().inBeats();
        const auto editTime = edit().tempoSequence.toTime (te::BeatPosition::fromBeats (editBeats));
        return (editTime - clip.getPosition().getStart()).inSeconds();
    }

    juce::Array<te::MidiNote*> selectedNotes (te::MidiClip& clip) const
    {
        juce::Array<te::MidiNote*> found;

        for (auto* note : clip.getSequence().getNotes())
            if (auto id = noteId (*note); id.isNotEmpty() && selectedNoteIds.contains (id))
                found.add (note);

        return found;
    }

    /** An arrangement clip as the UI sees it; nothing for a clip kind the app does not show. */
    std::optional<ClipInfo> clipInfo (te::Clip& c) const
    {
        if (auto* wave = dynamic_cast<te::WaveAudioClip*> (&c))
        {
            const auto pos = wave->getPosition();
            ClipInfo clip { wave->itemID.toString(),
                            wave->getName(),
                            pos.getStart().inSeconds(),
                            pos.getLength().inSeconds(),
                            pos.getOffset().inSeconds(),
                            wave->getMaximumLength().inSeconds(),
                            wave->getOriginalFile(),
                            selectionManager.isSelected (wave),
                            takesOf (*wave).getNumChildren(),
                            currentTakeOf (*wave) };
            clip.playbackFile = wave->getPlaybackFile().getFile();
            clip.reversed = wave->getIsReversed();
            describeLoopAndColour (*wave, clip);
            return clip;
        }

        if (auto* midi = dynamic_cast<te::MidiClip*> (&c))
            return midiClipInfo (*midi);

        return {};
    }

    ClipInfo midiClipInfo (const te::MidiClip& clip) const
    {
        const auto pos = clip.getPosition();

        ClipInfo info;
        info.id = clip.itemID.toString();
        info.name = clip.getName();
        info.startSeconds = pos.getStart().inSeconds();
        info.lengthSeconds = pos.getLength().inSeconds();
        info.sourceOffsetSeconds = pos.getOffset().inSeconds();
        // MidiClip::getMaximumLength is the Edit's maximum end, and it isn't const.
        info.sourceLengthSeconds = te::Edit::getMaximumEditEnd().inSeconds();
        info.selected = selectionManager.isSelected (&clip);
        info.kind = TrackKind::midi;
        describeLoopAndColour (clip, info);

        for (auto* note : clip.getSequence().getNotes())
        {
            const auto start = localSecondsForNoteBeat (clip, note->getStartBeat().inBeats());
            const auto end = localSecondsForNoteBeat (clip, note->getEndBeat().inBeats());

            if (end <= start)
                continue;

            MidiNoteInfo n;
            n.pitch = note->getNoteNumber();
            n.startSeconds = start;
            n.lengthSeconds = end - start;
            n.id = noteId (*note);
            n.velocity = note->getVelocity();
            n.selected = n.id.isNotEmpty() && selectedNoteIds.contains (n.id);
            info.notes.push_back (std::move (n));
        }

        return info;
    }

    void describeLoopAndColour (const te::Clip& clip, ClipInfo& info) const
    {
        info.looping = clip.isLooping();

        if (auto* midi = dynamic_cast<const te::MidiClip*> (&clip); midi != nullptr && info.looping)
        {
            const auto start = clip.getPosition().getStart();
            const auto startBeat = edit().tempoSequence.toBeats (start);
            info.loopLengthSeconds = (edit().tempoSequence.toTime (startBeat + midi->getLoopLengthBeats()) - start).inSeconds();
        }
        else if (auto* audio = dynamic_cast<const te::AudioClipBase*> (&clip); audio != nullptr && info.looping)
        {
            info.loopLengthSeconds = audio->getLoopLength().inSeconds();
        }

        if (auto colour = clip.state[colourProperty]; colour.isInt())
            info.colourIndex = juce::jlimit (0, trackPaletteSize - 1, (int) colour);
    }

    /** One bar after start, at the Edit's tempo, keeping the beat within the bar. */
    te::TimePosition oneBarAfter (te::TimePosition start) const
    {
        auto& tempo = edit().tempoSequence;
        auto bars = tempo.toBarsAndBeats (start);
        ++bars.bars;
        return tempo.toTime (bars);
    }

    te::AudioTrack* firstMidiTrack() const
    {
        for (auto* t : te::getAudioTracks (edit()))
            if (isMidi (*t))
                return t;

        return nullptr;
    }

    /** Where a new clip goes: the selected track, or the selected clip's track. */
    te::AudioTrack* insertionTrack() const
    {
        if (auto* track = selectedTrack())
            return track;

        auto* clip = selectedClip();
        return clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
    }

    /** Puts the audio file on the track as a clip named name, starting at start,
        inside the caller's undo step. */
    juce::Result placeAudioClip (te::AudioTrack& track, const juce::File& file, const juce::String& name,
                                 te::TimePosition start)
    {
        te::AudioFile audioFile (edit().engine, file);
        auto clip = track.insertWaveClip (name, file,
                                          { { start, te::TimeDuration::fromSeconds (audioFile.getLength()) }, {} },
                                          false);

        if (clip == nullptr)
            return juce::Result::fail ("The engine refused the clip: " + file.getFullPathName());

        projectManager.setClipSource (*clip, file);
        return juce::Result::ok();
    }

    /** Runs fn, then re-selects the clip that was selected before if fn replaced
        its object: re-parenting a clip, or undoing that, rebuilds it from its state. */
    template <typename Fn>
    auto keepingClipSelection (Fn&& fn)
    {
        auto* before = selectedClip();
        const auto id = before != nullptr ? before->itemID.toString() : juce::String();
        auto result = fn();

        if (id.isNotEmpty() && selectedClip() == nullptr)
            if (auto* clip = findClip (id))
                selectionManager.selectOnly (clip);

        return result;
    }

    //==============================================================================
    ProjectManager& projectManager;
    te::SelectionManager selectionManager { projectManager.getEdit().engine };
    juce::ValueTree editState;
    double insertMarkerSeconds = 0;
    juce::ListenerList<ApplicationModel::Listener> listeners;

    void notifyChanged()    { triggerAsyncUpdate(); }

private:
    static bool isTransportState (const juce::ValueTree& v)
    {
        for (auto t = v; t.isValid(); t = t.getParent())
            if (t.hasType (te::IDs::TRANSPORT))
                return true;

        return false;
    }

    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) override
    {
        // Transport position/state churn during playback is not a model change.
        if (! isTransportState (v))
            triggerAsyncUpdate();
    }

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override          { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override   { triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override           { triggerAsyncUpdate(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override                 { triggerAsyncUpdate(); }

    void handleAsyncUpdate() override
    {
        listeners.call ([] (ApplicationModel::Listener& l) { l.modelChanged(); });
    }
};

//==============================================================================
ApplicationModel::ApplicationModel (ProjectManager& pm)
    : impl (std::make_unique<Impl> (pm))
{
}

ApplicationModel::~ApplicationModel() = default;

//==============================================================================
void ApplicationModel::newProject()
{
    impl->replacingEdit ([this] { impl->projectManager.newProject(); return true; });
}

juce::Result ApplicationModel::openProject (const juce::File& folder, juce::var& uiState)
{
    return impl->replacingEdit ([&] { return impl->projectManager.open (folder, uiState); });
}

juce::Result ApplicationModel::saveProject (const juce::var& uiState)
{
    return impl->projectManager.save (uiState);
}

juce::Result ApplicationModel::saveProjectAs (const juce::File& folder, const juce::var& uiState)
{
    auto r = impl->projectManager.saveAs (folder, uiState);
    impl->notifyChanged();   // the Project name changed
    return r;
}

bool ApplicationModel::isProjectUntitled() const        { return impl->projectManager.isUntitled(); }
juce::String ApplicationModel::getProjectName() const   { return impl->projectManager.getProjectName(); }

//==============================================================================
const juce::Identifier ApplicationModel::Impl::noteIdProperty { "resamperNoteId" };

bool ApplicationModel::setTrackColour (const juce::String& trackId, int colourIndex)
{
    auto* track = findAudioTrack (impl->edit(), trackId);

    if (track == nullptr || ! juce::isPositiveAndBelow (colourIndex, trackPaletteSize) || colourOf (*track) == colourIndex)
        return false;

    impl->undo().beginStep ("Set Track Colour");
    track->state.setProperty (colourProperty, colourIndex, &impl->undoManager());
    return true;
}

void ApplicationModel::addAudioTrack()
{
    auto& edit = impl->edit();
    impl->undo().beginStep ("Add Track");

    if (auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr))
        impl->giveNextColour (*track);
}

void ApplicationModel::addMidiTrack()
{
    auto& edit = impl->edit();
    impl->undo().beginStep ("Add MIDI Track");
    auto track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr);

    if (track == nullptr)
        return;

    // Kind is a property, not "whichever synth is loaded": Phase 6 replaces the synth.
    markMidi (*track, &impl->undoManager());
    impl->giveNextColour (*track);

    // Ahead of the volume plugin, so the track's fader still applies.
    if (auto plugin = edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
        track->pluginList.insertPlugin (plugin, 0, nullptr);
}

bool ApplicationModel::removeTrack()
{
    auto* track = impl->selectedTrack();

    if (track == nullptr)
        track = te::getAudioTracks (impl->edit()).getLast();

    if (track == nullptr)
        return false;

    impl->undo().beginStep ("Remove Track");
    impl->edit().deleteTrack (track);
    return true;
}

bool ApplicationModel::setTrackVolume (const juce::String& trackId, Decibels volume, bool continuesGesture)
{
    // The engine stores a fader position, not dB.
    const auto position = te::decibelsToVolumeFaderPosition ((float) juce::jlimit (minVolume, maxVolume, volume).value);

    return impl->changeVolumePlugin (trackId, "Set Volume", continuesGesture,
                                     [] (te::VolumeAndPanPlugin& p) { return p.getSliderPos(); },
                                     [&] (te::VolumeAndPanPlugin& p) { p.setSliderPos (position); });
}

bool ApplicationModel::setTrackPan (const juce::String& trackId, double pan, bool continuesGesture)
{
    return impl->changeVolumePlugin (trackId, "Set Pan", continuesGesture,
                                     [] (te::VolumeAndPanPlugin& p) { return p.getPan(); },
                                     [&] (te::VolumeAndPanPlugin& p) { p.setPan ((float) juce::jlimit (-1.0, 1.0, pan)); });
}

bool ApplicationModel::setTrackMuted (const juce::String& trackId, bool muted)
{
    auto* track = findStripTrack (impl->edit(), trackId);

    if (track == nullptr || track->isMuted (false) == muted)
        return false;

    // Track::setMute and setSolo write without the UndoManager, so mute and
    // solo write the property through it.
    impl->undo().beginStep ("Mute Track");
    track->state.setProperty (te::IDs::mute, muted, &impl->undoManager());
    return true;
}

bool ApplicationModel::setTrackSolo (const juce::String& trackId, bool solo)
{
    auto* track = findStripTrack (impl->edit(), trackId);

    if (track == nullptr || track->isSolo (false) == solo)
        return false;

    impl->undo().beginStep ("Solo Track");
    track->state.setProperty (te::IDs::solo, solo, &impl->undoManager());
    return true;
}

bool ApplicationModel::isTrackMuted (const juce::String& trackId) const
{
    auto* track = findStripTrack (impl->edit(), trackId);
    return track != nullptr && track->isMuted (false);
}

bool ApplicationModel::isTrackSolo (const juce::String& trackId) const
{
    auto* track = findStripTrack (impl->edit(), trackId);
    return track != nullptr && track->isSolo (false);
}

//==============================================================================
juce::StringArray ApplicationModel::getAudioInputs() const
{
    juce::StringArray names;

    for (auto* input : impl->inputsFor (TrackKind::audio))
        names.add (input->getInputDevice().getName());

    return names;
}

juce::StringArray ApplicationModel::getMidiInputs() const
{
    juce::StringArray names;

    for (auto* input : impl->inputsFor (TrackKind::midi))
        names.add (input->getInputDevice().getName());

    return names;
}

bool ApplicationModel::setTrackInput (const juce::String& trackId, const juce::String& inputName)
{
    auto* track = findAudioTrack (impl->edit(), trackId);
    auto* current = track != nullptr ? impl->inputOf (*track) : nullptr;
    auto* input = inputName.isEmpty() || track == nullptr ? nullptr : impl->findInput (trackKindOf (*track), inputName);

    if (track == nullptr || input == current || (input == nullptr && inputName.isNotEmpty()))
        return false;

    // Inputs live outside the UndoManager, like arming.
    const bool armed = current != nullptr && current->isRecordingEnabled (track->itemID);

    if (current != nullptr)
        (void) current->removeTarget (track->itemID, nullptr);

    if (input != nullptr)
        if (auto destination = input->setTarget (track->itemID, false, nullptr); destination.has_value())
            (*destination)->recordEnabled = armed;

    return true;
}

bool ApplicationModel::setTrackArmed (const juce::String& trackId, bool armed)
{
    auto* track = findAudioTrack (impl->edit(), trackId);

    if (track == nullptr || (armed && isReturnTrack (*track)))
        return false;

    auto* input = impl->inputOf (*track);

    if (input == nullptr && armed)
        if (setTrackInput (trackId, getInputs (trackKindOf (*track))[0]))
            input = impl->inputOf (*track);

    if (input == nullptr || input->isRecordingEnabled (track->itemID) == armed)
        return false;

    input->setRecordingEnabled (track->itemID, armed);
    return true;
}

juce::Result ApplicationModel::insertAudioClip (const juce::File& file)
{
    auto& edit = impl->edit();

    if (! te::AudioFile (edit.engine, file).isValid())
        return juce::Result::fail ("Not a readable audio file: " + file.getFullPathName());

    auto* track = impl->insertionTrack();

    if (track != nullptr && isMidi (*track))
        return juce::Result::fail ("Audio clips go on audio tracks");

    juce::File playable;

    if (auto r = impl->projectManager.importAudio (file, playable); r.failed())
        return r;

    impl->undo().beginStep ("Insert Clip");

    if (track == nullptr)
        for (auto* t : te::getAudioTracks (edit))
            if (! isMidi (*t))
            {
                track = t;
                break;
            }

    // ensureNumberOfAudioTracks counts MIDI tracks, so it would not add one here.
    if (track == nullptr)
        track = edit.insertNewAudioTrack (te::TrackInsertPoint::getEndOfTracks (edit), nullptr).get();

    if (track == nullptr)
        return juce::Result::fail ("The engine refused the track");

    auto start = te::TimePosition();

    for (auto* c : track->getClips())
        start = std::max (start, c->getPosition().getEnd());

    return impl->placeAudioClip (*track, playable, file.getFileNameWithoutExtension(), start);
}

juce::Result ApplicationModel::insertAudioClipAt (const juce::File& file, const juce::String& trackId, double startSeconds)
{
    auto* track = findAudioTrack (impl->edit(), trackId);

    if (track == nullptr)
        return juce::Result::fail ("No track with that id");

    if (isMidi (*track))
        return juce::Result::fail ("Audio clips go on audio tracks");

    juce::File playable;

    if (auto r = impl->projectManager.importAudio (file, playable); r.failed())
        return r;

    impl->undo().beginStep ("Insert Clip");
    return impl->placeAudioClip (*track, playable, file.getFileNameWithoutExtension(),
                                 te::TimePosition::fromSeconds (std::max (0.0, startSeconds)));
}

juce::Result ApplicationModel::insertMidiClip()
{
    auto* track = impl->insertionTrack();

    if (track != nullptr && ! isMidi (*track))
        return juce::Result::fail ("Select a MIDI track");

    // Nothing selected: the first MIDI track, as clip.add uses the first audio track.
    if (track == nullptr)
        track = impl->firstMidiTrack();

    if (track == nullptr)
        return juce::Result::fail ("Select a MIDI track");

    impl->undo().beginStep ("Insert MIDI Clip");

    const auto start = te::TimePosition::fromSeconds (std::max (0.0, getTransportPositionSeconds()));
    auto clip = track->insertMIDIClip ("MIDI Clip", { start, impl->oneBarAfter (start) }, nullptr);

    if (clip == nullptr)
        return juce::Result::fail ("The engine refused the MIDI clip");

    return juce::Result::ok();
}

bool ApplicationModel::moveClip (const juce::String& clipId, double startSeconds, const juce::String& trackId)
{
    auto* clip = impl->findClip (clipId);
    auto* track = trackId.isEmpty() ? (clip != nullptr ? clip->getClipTrack() : nullptr)
                                    : findAudioTrack (impl->edit(), trackId);

    // A clip stays on tracks of its own kind.
    if (clip == nullptr || track == nullptr
         || isMidi (*track) != (dynamic_cast<te::MidiClip*> (clip) != nullptr))
        return false;

    const auto start = te::TimePosition::fromSeconds (std::max (0.0, startSeconds));
    const bool changesTrack = track != clip->getClipTrack();

    if (! changesTrack && start == clip->getPosition().getStart())
        return false;

    return impl->keepingClipSelection ([&]
    {
        impl->undo().beginStep ("Move Clip");

        if (changesTrack)
            clip->moveTo (*track);

        // Re-parenting may rebuild the clip object; look it up again by ID.
        if (auto* moved = impl->findClip (clipId))
            moved->setStart (start, false, true);

        return true;
    });
}

bool ApplicationModel::resizeClip (const juce::String& clipId, double startSeconds, double endSeconds)
{
    auto* clip = impl->findClip (clipId);

    if (clip == nullptr)
        return false;

    const auto pos = clip->getPosition();
    const auto sourceStart = pos.getStart() - pos.getOffset();
    const auto sourceEnd = sourceStart + clip->getMaximumLength();

    const auto start = std::max ({ te::TimePosition::fromSeconds (startSeconds), sourceStart, te::TimePosition() });
    const auto end = std::min (te::TimePosition::fromSeconds (endSeconds), sourceEnd);

    if (end <= start || (start == pos.getStart() && end == pos.getEnd()))
        return false;

    impl->undo().beginStep ("Resize Clip");
    clip->setPosition ({ { start, end }, pos.getOffset() + (start - pos.getStart()) });
    return true;
}

bool ApplicationModel::canSplitClip (const juce::String& clipId, double timeSeconds) const
{
    // The engine won't cut within a millisecond of either edge.
    auto* clip = impl->findClip (clipId);
    return clip != nullptr && clip->getPosition().time.reduced (te::TimeDuration::fromSeconds (0.001)).contains (te::TimePosition::fromSeconds (timeSeconds));
}

bool ApplicationModel::splitClip (const juce::String& clipId, double timeSeconds)
{
    if (! canSplitClip (clipId, timeSeconds))
        return false;

    return impl->keepingClipSelection ([&]
    {
        impl->undo().beginStep ("Split Clip");
        auto* clip = impl->findClip (clipId);
        return clip->getClipTrack()->splitClip (*clip, te::TimePosition::fromSeconds (timeSeconds)) != nullptr;
    });
}

//==============================================================================
namespace
{
    /** A copied MIDI clip's notes get ids of their own, or selecting one would select both. */
    void renameNoteIds (te::Clip& clip, const juce::Identifier& noteIdProperty, juce::UndoManager* um)
    {
        if (auto* midi = dynamic_cast<te::MidiClip*> (&clip))
            for (auto* note : midi->getSequence().getNotes())
                note->state.setProperty (noteIdProperty, juce::Uuid().toString(), um);
    }
}

juce::Result ApplicationModel::copyClip (const juce::String& clipId, double startSeconds, const juce::String& trackId)
{
    auto* clip = impl->findClip (clipId);
    auto* track = trackId.isEmpty() ? (clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getClipTrack()) : nullptr)
                                    : findAudioTrack (impl->edit(), trackId);

    if (clip == nullptr || track == nullptr)
        return juce::Result::fail ("No such clip or track");

    if (isMidi (*track) != (dynamic_cast<te::MidiClip*> (clip) != nullptr))
        return juce::Result::fail ("A clip only goes on a track of its own kind");

    impl->undo().beginStep ("Copy Clip");
    auto copy = te::duplicateClip (*clip);

    if (copy == nullptr)
        return juce::Result::fail ("The engine refused the copy");

    renameNoteIds (*copy, Impl::noteIdProperty, &impl->undoManager());
    const auto copyId = copy->itemID.toString();

    if (track != copy->getClipTrack())
        copy->moveTo (*track);

    // Re-parenting may rebuild the clip object; look it up again by ID.
    if (auto* moved = impl->findClip (copyId))
    {
        moved->setStart (te::TimePosition::fromSeconds (std::max (0.0, startSeconds)), false, true);
        impl->selectionManager.selectOnly (moved);
    }

    return juce::Result::ok();
}

bool ApplicationModel::duplicateSelectedClips()
{
    auto selected = impl->selectionManager.getItemsOfType<te::Clip>();

    if (selected.isEmpty())
        return false;

    impl->undo().beginStep ("Duplicate");
    juce::Array<te::Clip*> copies;

    for (auto* clip : selected)
    {
        if (auto copy = te::duplicateClip (*clip))
        {
            renameNoteIds (*copy, Impl::noteIdProperty, &impl->undoManager());
            copy->setStart (clip->getPosition().getEnd(), false, true);
            copies.add (copy.get());
        }
    }

    impl->selectionManager.deselectAll();

    for (auto* copy : copies)
        impl->selectionManager.addToSelection (copy);

    return ! copies.isEmpty();
}

bool ApplicationModel::loopExtendClip (const juce::String& clipId, double endSeconds)
{
    auto* clip = impl->findClip (clipId);

    if (clip == nullptr)
        return false;

    const auto pos = clip->getPosition();
    const auto end = te::TimePosition::fromSeconds (endSeconds);

    if (end <= pos.getStart() || end == pos.getEnd())
        return false;

    impl->undo().beginStep ("Loop Clip");

    // The clip's current content becomes the loop.
    if (! clip->isLooping())
    {
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
        {
            const auto startBeat = midi->getOffsetInBeats();
            midi->setLoopRangeBeats ({ te::BeatPosition() + startBeat, te::BeatPosition() + startBeat + midi->getLengthInBeats() });
        }
        else if (auto* audio = dynamic_cast<te::AudioClipBase*> (clip))
        {
            audio->setLoopRange ({ te::TimePosition() + pos.getOffset(), pos.getLength() });
        }

        if (! clip->isLooping())
            return false;
    }

    clip->setEnd (end, true);
    return true;
}

juce::Result ApplicationModel::consolidateSelectedClips()
{
    auto selected = impl->selectionManager.getItemsOfType<te::Clip>();

    if (selected.size() < 2)
        return juce::Result::fail ("Select two or more clips to consolidate");

    auto* track = dynamic_cast<te::AudioTrack*> (selected.getFirst()->getClipTrack());
    const auto midi = dynamic_cast<te::MidiClip*> (selected.getFirst()) != nullptr;
    te::TimeRange range = selected.getFirst()->getPosition().time;

    for (auto* clip : selected)
    {
        if (clip->getClipTrack() != track || (dynamic_cast<te::MidiClip*> (clip) != nullptr) != midi)
            return juce::Result::fail ("Consolidate works on clips of one kind on one track");

        range = range.getUnionWith (clip->getPosition().time);
    }

    if (track == nullptr)
        return juce::Result::fail ("Consolidate works on clips of one kind on one track");

    auto& edit = impl->edit();
    const auto name = selected.getFirst()->getName();

    if (midi)
    {
        struct Note { int pitch, velocity; double start, length; };
        std::vector<Note> notes;

        for (auto* clip : selected)
        {
            auto* source = dynamic_cast<te::MidiClip*> (clip);
            const auto clipStart = source->getPosition().getStart().inSeconds();
            const auto clipLength = source->getPosition().getLength().inSeconds();

            for (auto* note : source->getSequence().getNotes())
            {
                const auto start = impl->localSecondsForNoteBeat (*source, note->getStartBeat().inBeats());
                const auto end = impl->localSecondsForNoteBeat (*source, note->getEndBeat().inBeats());

                // Only what plays: notes inside the clip's visible part.
                if (end > start && start >= -1.0e-9 && start < clipLength)
                    notes.push_back ({ note->getNoteNumber(), note->getVelocity(), clipStart + start,
                                       std::min (end, clipLength) - start });
            }
        }

        impl->undo().beginStep ("Consolidate");

        for (auto* clip : selected)
            clip->removeFromParent();

        auto merged = track->insertMIDIClip (name, range, nullptr);

        if (merged == nullptr)
            return juce::Result::fail ("The engine refused the clip");

        for (auto& n : notes)
        {
            const auto startBeat = impl->noteBeatForLocalSeconds (*merged, n.start - range.getStart().inSeconds());
            const auto endBeat = impl->noteBeatForLocalSeconds (*merged, n.start + n.length - range.getStart().inSeconds());

            if (auto* note = merged->getSequence().addNote (n.pitch, te::BeatPosition::fromBeats (startBeat),
                                                            te::BeatDuration::fromBeats (endBeat - startBeat),
                                                            n.velocity, 0, &impl->undoManager()))
                note->state.setProperty (Impl::noteIdProperty, juce::Uuid().toString(), &impl->undoManager());
        }

        impl->selectionManager.selectOnly (merged.get());
        return juce::Result::ok();
    }

    // Audio: render the clips' own audio over their span, then swap them for the file.
    const auto folder = ProjectManager::getAudioFolder (impl->projectManager.getProjectFolder());
    const auto file = folder.getChildFile (name + " consolidated.wav").getNonexistentSibling (false);

    if (auto r = render::clipsToWav (edit, file, render::bitForTrack (*track), range); r.failed())
        return r;

    impl->undo().beginStep ("Consolidate");

    for (auto* clip : selected)
        clip->removeFromParent();

    if (auto r = impl->placeAudioClip (*track, file, file.getFileNameWithoutExtension(), range.getStart()); r.failed())
        return r;

    return juce::Result::ok();
}

bool ApplicationModel::deleteSelectedClips()
{
    auto selected = impl->selectionManager.getItemsOfType<te::Clip>();

    if (selected.isEmpty())
        return false;

    impl->undo().beginStep ("Delete Clips");
    impl->selectionManager.deselectAll();

    for (auto* clip : selected)
        clip->removeFromParent();

    return true;
}

bool ApplicationModel::renameClip (const juce::String& clipId, const juce::String& name)
{
    auto* clip = impl->findClip (clipId);
    const auto trimmed = name.trim();

    if (clip == nullptr || trimmed.isEmpty() || trimmed == clip->getName())
        return false;

    impl->undo().beginStep ("Rename Clip");
    clip->setName (trimmed);
    return true;
}

bool ApplicationModel::reverseClip (const juce::String& clipId)
{
    auto* audio = dynamic_cast<te::AudioClipBase*> (impl->findClip (clipId));

    if (audio == nullptr)
        return false;

    impl->undo().beginStep ("Reverse Clip");
    audio->setIsReversed (! audio->getIsReversed());
    return true;
}

bool ApplicationModel::setClipColour (const juce::String& clipId, int colourIndex)
{
    auto* clip = impl->findClip (clipId);

    if (clip == nullptr || colourIndex < -1 || colourIndex >= trackPaletteSize
        || (int) clip->state.getProperty (colourProperty, -1) == colourIndex)
        return false;

    impl->undo().beginStep ("Set Clip Colour");

    if (colourIndex < 0)
        clip->state.removeProperty (colourProperty, &impl->undoManager());
    else
        clip->state.setProperty (colourProperty, colourIndex, &impl->undoManager());

    return true;
}

bool ApplicationModel::setClipTake (const juce::String& clipId, int takeIndex)
{
    // Not WaveAudioClip::setCurrentTake: that only knows takes that are items of a
    // te::Project, and deletes the file takes a loop recording makes here.
    auto* clip = dynamic_cast<te::WaveAudioClip*> (impl->findClip (clipId));

    if (clip == nullptr || takeIndex == impl->currentTakeOf (*clip))
        return false;

    const auto take = Impl::takesOf (*clip).getChild (takeIndex);

    if (! take.isValid())
        return false;

    impl->undo().beginStep ("Switch Take");
    clip->state.setProperty (te::IDs::source, take[te::IDs::source], &impl->undoManager());
    return true;
}

//==============================================================================
namespace
{
    /** A quarter, eighth or sixteenth note, in quarter-note beats. 0 if the name isn't one of those. */
    double quantizeGridBeats (const juce::String& grid)
    {
        if (grid == "1/4")   return 1.0;
        if (grid == "1/8")   return 0.5;
        if (grid == "1/16")  return 0.25;
        return 0.0;
    }
}

bool ApplicationModel::addNote (const juce::String& clipId, double startSeconds, double lengthSeconds, int pitch, int velocity)
{
    auto* clip = dynamic_cast<te::MidiClip*> (impl->findClip (clipId));

    if (clip == nullptr || startSeconds < 0.0 || lengthSeconds <= 0.0
        || ! juce::isPositiveAndBelow (pitch, 128)
        || velocity < minNoteVelocity || velocity > maxNoteVelocity)
        return false;

    const auto startBeat = impl->noteBeatForLocalSeconds (*clip, startSeconds);
    const auto lengthBeats = impl->noteBeatForLocalSeconds (*clip, startSeconds + lengthSeconds) - startBeat;

    if (lengthBeats <= 1.0e-9)
        return false;

    impl->undo().beginStep ("Add Note");
    auto* note = clip->getSequence().addNote (pitch, te::BeatPosition::fromBeats (startBeat),
                                              te::BeatDuration::fromBeats (lengthBeats),
                                              velocity, 0, &impl->undoManager());

    if (note == nullptr)
        return false;

    const auto id = juce::Uuid().toString();
    note->state.setProperty (Impl::noteIdProperty, id, &impl->undoManager());
    impl->selectedNoteIds.clear();
    impl->selectedNoteIds.add (id);
    return true;
}

bool ApplicationModel::deleteSelectedNotes()
{
    struct Target
    {
        te::MidiClip* clip;
        juce::String id;
    };

    std::vector<Target> targets;

    for (auto* track : te::getAudioTracks (impl->edit()))
        for (auto* clip : track->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                for (auto* note : impl->selectedNotes (*midi))
                    targets.push_back ({ midi, impl->noteId (*note) });

    if (targets.empty())
        return false;

    impl->undo().beginStep ("Delete Notes");

    for (auto& target : targets)
        if (auto* note = impl->findNote (*target.clip, target.id))
            target.clip->getSequence().removeNote (*note, &impl->undoManager());

    return true;
}

bool ApplicationModel::moveNotes (const juce::String& clipId, const juce::StringArray& noteIds,
                                  double deltaSeconds, int deltaPitch)
{
    auto* clip = dynamic_cast<te::MidiClip*> (impl->findClip (clipId));

    if (clip == nullptr || noteIds.isEmpty())
        return false;

    struct Item
    {
        te::MidiNote* note;
        double startSeconds;
        int pitch;
        double lengthBeats;
    };

    std::vector<Item> items;

    for (auto& id : noteIds)
        if (auto* note = impl->findNote (*clip, id))
            items.push_back ({ note, impl->localSecondsForNoteBeat (*clip, note->getStartBeat().inBeats()),
                               note->getNoteNumber(), note->getLengthBeats().inBeats() });

    if (items.empty())
        return false;

    auto earliest = items.front().startSeconds;
    auto lowest = items.front().pitch;
    auto highest = items.front().pitch;

    for (auto& item : items)
    {
        earliest = std::min (earliest, item.startSeconds);
        lowest = std::min (lowest, item.pitch);
        highest = std::max (highest, item.pitch);
    }

    // One delta for the whole group, so a chord doesn't squash against the edges.
    deltaPitch = juce::jlimit (-lowest, 127 - highest, deltaPitch);

    if (earliest + deltaSeconds < 0.0)
        deltaSeconds = -earliest;

    if (deltaPitch == 0 && std::abs (deltaSeconds) < 1.0e-9)
        return false;

    impl->undo().beginStep ("Move Notes");

    for (auto& item : items)
    {
        if (deltaPitch != 0)
            item.note->setNoteNumber (item.pitch + deltaPitch, &impl->undoManager());

        const auto newStart = impl->noteBeatForLocalSeconds (*clip, item.startSeconds + deltaSeconds);
        item.note->setStartAndLength (te::BeatPosition::fromBeats (newStart),
                                      te::BeatDuration::fromBeats (item.lengthBeats),
                                      &impl->undoManager());
    }

    return true;
}

bool ApplicationModel::resizeNote (const juce::String& clipId, const juce::String& noteId,
                                   double startSeconds, double endSeconds)
{
    auto* clip = dynamic_cast<te::MidiClip*> (impl->findClip (clipId));
    auto* note = clip != nullptr ? impl->findNote (*clip, noteId) : nullptr;

    if (note == nullptr || endSeconds <= startSeconds)
        return false;

    startSeconds = std::max (0.0, startSeconds);

    if (endSeconds <= startSeconds)
        return false;

    const auto newStart = impl->noteBeatForLocalSeconds (*clip, startSeconds);
    const auto newLength = impl->noteBeatForLocalSeconds (*clip, endSeconds) - newStart;

    if (newLength <= 1.0e-9
        || (std::abs (newStart - note->getStartBeat().inBeats()) < 1.0e-9
            && std::abs (newLength - note->getLengthBeats().inBeats()) < 1.0e-9))
        return false;

    impl->undo().beginStep ("Resize Note");
    note->setStartAndLength (te::BeatPosition::fromBeats (newStart),
                             te::BeatDuration::fromBeats (newLength),
                             &impl->undoManager());
    return true;
}

bool ApplicationModel::setNoteVelocity (const juce::String& clipId, int velocity, bool continuesGesture)
{
    auto* clip = dynamic_cast<te::MidiClip*> (impl->findClip (clipId));

    if (clip == nullptr || velocity < minNoteVelocity || velocity > maxNoteVelocity)
        return false;

    auto notes = impl->selectedNotes (*clip);
    bool changes = false;

    for (auto* note : notes)
        if (note->getVelocity() != velocity)
            changes = true;

    if (! changes)
        return false;

    impl->undo().beginGestureStep ("Set Velocity", "Set Velocity:" + clipId, continuesGesture);

    for (auto* note : notes)
        note->setVelocity (velocity, &impl->undoManager());

    return true;
}

bool ApplicationModel::quantizeNotes (const juce::String& clipId, const juce::String& grid)
{
    auto* clip = dynamic_cast<te::MidiClip*> (impl->findClip (clipId));
    const auto step = quantizeGridBeats (grid);

    if (clip == nullptr || step <= 0.0)
        return false;

    // Ids of deleted notes stay so undo can restore the highlight. A selection
    // that names no living note in this clip is "none selected": quantize them all.
    auto notes = impl->selectedNotes (*clip);

    if (notes.isEmpty())
        for (auto* note : clip->getSequence().getNotes())
            notes.add (note);

    if (notes.isEmpty())
        return false;

    const auto contentStart = clip->getContentStartBeat().inBeats();
    const auto loopStart = clip->getLoopStartBeats().inBeats();

    struct Change
    {
        te::MidiNote* note;
        double beat;
    };

    std::vector<Change> changes;

    for (auto* note : notes)
    {
        const auto beat = note->getStartBeat().inBeats();
        // The piano roll's lines are Edit beats, not beats inside the clip.
        const auto editBeat = beat - loopStart + contentStart;
        auto snapped = std::round (editBeat / step) * step - contentStart + loopStart;

        if (snapped < 0.0)
            snapped = std::ceil ((contentStart - loopStart) / step - 1.0e-9) * step - contentStart + loopStart;

        snapped = std::max (0.0, snapped);

        if (std::abs (snapped - beat) > 1.0e-6)
            changes.push_back ({ note, snapped });
    }

    if (changes.empty())
        return false;

    impl->undo().beginStep ("Quantize Notes");

    for (auto& change : changes)
        change.note->setStartAndLength (te::BeatPosition::fromBeats (change.beat),
                                        change.note->getLengthBeats(),
                                        &impl->undoManager());

    return true;
}

//==============================================================================
bool ApplicationModel::undo()
{
    const auto undone = impl->keepingClipSelection ([this] { return impl->undoManager().undo(); });
    impl->syncVolumeParametersFromState();
    return undone;
}

bool ApplicationModel::redo()
{
    const auto redone = impl->keepingClipSelection ([this] { return impl->undoManager().redo(); });
    impl->syncVolumeParametersFromState();
    return redone;
}

bool ApplicationModel::canUndo() const   { return impl->undoManager().canUndo(); }
bool ApplicationModel::canRedo() const   { return impl->undoManager().canRedo(); }

//==============================================================================
void ApplicationModel::selectTrack (const juce::String& trackId, SelectionMode mode)
{
    auto* track = findAudioTrack (impl->edit(), trackId);
    auto& selection = impl->selectionManager;

    if (track == nullptr)
    {
        if (mode == SelectionMode::replace)
            selection.deselectAll();

        return;
    }

    if (mode == SelectionMode::replace)
    {
        selection.selectOnly (track);
        return;
    }

    // Tracks and clips aren't selected together.
    for (auto* clip : selection.getItemsOfType<te::Clip>())
        selection.deselect (clip);

    if (mode == SelectionMode::add || ! selection.isSelected (track))
        selection.addToSelection (track);
    else
        selection.deselect (track);
}

void ApplicationModel::deselectAll()
{
    impl->selectionManager.deselectAll();
    selectNotes ({});
}

void ApplicationModel::selectClip (const juce::String& clipId, SelectionMode mode)
{
    auto* clip = impl->findClip (clipId);
    auto& selection = impl->selectionManager;

    if (clip == nullptr)
    {
        if (mode == SelectionMode::replace)
            selection.deselectAll();

        return;
    }

    // Tracks and clips aren't selected together.
    if (mode != SelectionMode::replace)
        for (auto* track : selection.getItemsOfType<te::AudioTrack>())
            selection.deselect (track);

    if (mode == SelectionMode::replace)
        selection.selectOnly (clip);
    else if (mode == SelectionMode::add || ! selection.isSelected (clip))
        selection.addToSelection (clip);
    else
        selection.deselect (clip);
}

juce::StringArray ApplicationModel::getSelectedClipIds() const
{
    juce::StringArray ids;

    for (auto* clip : impl->selectionManager.getItemsOfType<te::Clip>())
        ids.add (clip->itemID.toString());

    return ids;
}

juce::String ApplicationModel::getSelectedTrackId() const
{
    if (auto* track = impl->selectedTrack())
        return track->itemID.toString();

    if (auto* clip = impl->selectedClip())
        if (auto* owner = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
            return owner->itemID.toString();

    return {};
}

juce::String ApplicationModel::getSelectedClipId() const
{
    auto* clip = impl->selectedClip();
    return clip != nullptr ? clip->itemID.toString() : juce::String();
}

void ApplicationModel::selectNotes (const juce::StringArray& noteIds)
{
    if (impl->selectedNoteIds == noteIds)
        return;

    impl->selectedNoteIds = noteIds;
    impl->notifyChanged();
}

bool ApplicationModel::hasSelectedNotes() const
{
    for (auto* track : te::getAudioTracks (impl->edit()))
        for (auto* clip : track->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip); midi != nullptr && ! impl->selectedNotes (*midi).isEmpty())
                return true;

    return false;
}

//==============================================================================
void ApplicationModel::play()
{
    auto& transport = impl->transport();
    const auto from = ! transport.isPlaying() && isLooping() ? getLoopRange().start : impl->insertMarkerSeconds;

    transport.setPosition (te::TimePosition::fromSeconds (from));

    if (! transport.isPlaying())
        transport.play (false);
}

void ApplicationModel::stop()
{
    if (! isRecording())
    {
        if (! isPlaying())
        {
            returnToStart();
            return;
        }

        impl->transport().stop (false, false);
        return;
    }

    // Stopping turns the recording into clips.
    impl->undo().beginStep ("Record");
    impl->transport().stop (false, false);
    impl->removeDuplicateTakes();
}

double ApplicationModel::getInsertMarkerSeconds() const
{
    return impl->insertMarkerSeconds;
}

bool ApplicationModel::isMetronomeOn() const
{
    return impl->edit().clickTrackEnabled.get();
}

void ApplicationModel::setMetronomeOn (bool on)
{
    impl->edit().clickTrackEnabled = on;
    impl->notifyChanged();
}

bool ApplicationModel::isCountInOn() const
{
    return impl->edit().getCountInMode() != te::Edit::CountIn::none;
}

void ApplicationModel::setCountInOn (bool on)
{
    impl->edit().setCountInMode (on ? te::Edit::CountIn::twoBar : te::Edit::CountIn::none);
    impl->notifyChanged();
}

float ApplicationModel::getCpuUsage() const
{
    return impl->edit().engine.getDeviceManager().getCpuUsage();
}

//==============================================================================
double ApplicationModel::getTempo() const
{
    return impl->edit().tempoSequence.getTempo (0)->getBpm();
}

bool ApplicationModel::setTempo (double bpm, bool continuesGesture)
{
    auto* tempo = impl->edit().tempoSequence.getTempo (0);
    const auto clamped = juce::jlimit (minTempo, maxTempo, bpm);

    if (tempo == nullptr || juce::exactlyEqual (tempo->getBpm(), clamped))
        return false;

    impl->undo().beginGestureStep ("Set Tempo", "tempo", continuesGesture);
    tempo->setBpm (clamped);
    return true;
}

TimeSignature ApplicationModel::getTimeSignature() const
{
    auto* sig = impl->edit().tempoSequence.getTimeSig (0);
    return sig != nullptr ? TimeSignature { sig->numerator.get(), sig->denominator.get() } : TimeSignature {};
}

bool ApplicationModel::setTimeSignature (int numerator, int denominator)
{
    auto* sig = impl->edit().tempoSequence.getTimeSig (0);
    const auto validDenominator = denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16;

    if (sig == nullptr || numerator < 1 || numerator > 32 || ! validDenominator
        || (sig->numerator.get() == numerator && sig->denominator.get() == denominator))
        return false;

    impl->undo().beginStep ("Set Time Signature");
    sig->numerator = numerator;
    sig->denominator = denominator;
    return true;
}

BarsBeats ApplicationModel::toBarsBeats (double seconds) const
{
    auto& tempo = impl->edit().tempoSequence;

    // Before the start there is no tempo map to read: count back in the first bar's metre.
    if (seconds < 0.0)
    {
        const auto beatsPerBar = (double) tempo.getTimeSig (0)->numerator;
        const auto beats = tempo.toBeats (te::TimePosition::fromSeconds (seconds)).inBeats();
        const auto bar = std::floor (beats / beatsPerBar);
        const auto inBar = beats - bar * beatsPerBar;
        const auto sixteenth = (int) std::floor ((inBar - std::floor (inBar)) * 4.0 + 1.0e-9);
        return { (int) bar + 1, (int) std::floor (inBar) + 1, juce::jlimit (0, 3, sixteenth) + 1 };
    }

    const auto bb = tempo.toBarsAndBeats (te::TimePosition::fromSeconds (seconds));
    const auto beat = bb.getWholeBeats();
    const auto sixteenth = (int) std::floor (bb.getFractionalBeats().inBeats() * 4.0 + 1.0e-9);
    return { bb.bars + 1, beat + 1, juce::jlimit (0, 3, sixteenth) + 1 };
}

juce::Result ApplicationModel::record (bool withCountIn)
{
    // The engine checks these too, but only tells its UIBehaviour.
    auto tracks = getTracks();

    if (std::none_of (tracks.begin(), tracks.end(), [] (const TrackInfo& t) { return t.armed; }))
        return juce::Result::fail ("Arm a track to record");

    const auto loop = getLoopRange();

    if (isLooping() && loop.end - loop.start < minLoopRecordingSeconds)
        return juce::Result::fail ("To record in a loop, make the loop at least "
                                   + juce::String (minLoopRecordingSeconds) + " seconds long");

    if (! isRecording())
    {
        // The engine reads the count-in as it starts; skipping it (Shift-click Rec) leaves the setting alone.
        const auto countIn = impl->edit().getCountInMode();

        if (! withCountIn)
            impl->edit().setCountInMode (te::Edit::CountIn::none);

        impl->transport().record (false);
        impl->edit().setCountInMode (countIn);
    }

    return juce::Result::ok();
}

void ApplicationModel::returnToStart()
{
    // A recording ends through stop(), which makes it one undo step.
    if (isRecording())
        stop();

    impl->edit().getTransport().setPosition (te::TimePosition());
    impl->insertMarkerSeconds = 0;
}

bool ApplicationModel::setTransportPosition (double seconds)
{
    if (isRecording())
        return false;

    impl->insertMarkerSeconds = std::max (0.0, seconds);
    impl->edit().getTransport().setPosition (te::TimePosition::fromSeconds (impl->insertMarkerSeconds));
    return true;
}

bool ApplicationModel::isPlaying() const
{
    return impl->edit().getTransport().isPlaying();
}

bool ApplicationModel::isRecording() const
{
    return impl->edit().getTransport().isRecording();
}

// The loop lives in transport state, which sends no modelChanged() by itself.
// Fixed while recording: the engine cuts a loop recording into takes by the
// loop it finds when the recording stops.
void ApplicationModel::setLooping (bool shouldLoop)
{
    if (isRecording())
        return;

    impl->transport().looping = shouldLoop;
    impl->notifyChanged();
}

bool ApplicationModel::isLooping() const
{
    return impl->transport().looping;
}

bool ApplicationModel::setLoopRange (double startSeconds, double endSeconds)
{
    const auto start = std::max (0.0, startSeconds);

    if (endSeconds <= start || isRecording())
        return false;

    impl->transport().setLoopRange ({ te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (endSeconds) });
    impl->notifyChanged();
    return true;
}

TimeRangeSeconds ApplicationModel::getLoopRange() const
{
    const auto range = impl->transport().getLoopRange();
    return { range.getStart().inSeconds(), range.getEnd().inSeconds() };
}

double ApplicationModel::getTransportPositionSeconds() const
{
    return impl->edit().getTransport().getPosition().inSeconds();
}

//==============================================================================
double ApplicationModel::secondsToBeats (double seconds) const
{
    return impl->edit().tempoSequence.toBeats (te::TimePosition::fromSeconds (std::max (0.0, seconds))).inBeats();
}

double ApplicationModel::beatsToSeconds (double beats) const
{
    return impl->edit().tempoSequence.toTime (te::BeatPosition::fromBeats (std::max (0.0, beats))).inSeconds();
}

int ApplicationModel::getBeatsPerBar (double seconds) const
{
    return std::max (1, (int) impl->edit().tempoSequence.getTimeSigAt (te::TimePosition::fromSeconds (std::max (0.0, seconds))).numerator);
}

std::optional<ClipInfo> ApplicationModel::getClip (const juce::String& clipId) const
{
    if (auto* clip = impl->findClip (clipId))
        return impl->clipInfo (*clip);

    return {};
}

std::vector<TrackInfo> ApplicationModel::getTracks() const
{
    std::vector<TrackInfo> tracks;
    const auto inputs = impl->recordingInputs();

    for (auto* t : te::getAudioTracks (impl->edit()))
    {
        TrackInfo info;
        info.id = t->itemID.toString();
        info.name = t->getName();
        info.kind = trackKindOf (*t);
        info.selected = impl->selectionManager.isSelected (t);
        info.colourIndex = colourOf (*t);
        info.muted = t->isMuted (false);
        info.solo = t->isSolo (false);
        info.isReturn = isReturnTrack (*t);

        if (auto* input = impl->inputOf (*t, inputs))
        {
            info.input = input->getInputDevice().getName();
            info.armed = input->isRecordingEnabled (t->itemID);
        }

        if (auto* volume = t->getVolumePlugin())
        {
            info.volume = juce::jmax (minVolume, Decibels (volume->getVolumeDb()));
            info.pan = volume->getPan();
        }

        for (auto* c : t->getClips())
            if (auto clip = impl->clipInfo (*c))
                info.clips.push_back (std::move (*clip));

        tracks.push_back (std::move (info));
    }

    return tracks;
}

std::unique_ptr<ClipWaveform> ApplicationModel::createWaveform (const juce::String& clipId,
                                                                juce::Component& repaintTarget) const
{
    if (auto* clip = dynamic_cast<te::WaveAudioClip*> (impl->findClip (clipId)))
        return std::make_unique<ClipWaveform> (std::make_unique<ClipWaveform::Impl> (*clip, repaintTarget));

    return nullptr;
}

std::vector<RecordingInfo> ApplicationModel::getRecordings() const
{
    std::vector<RecordingInfo> recordings;

    if (! isRecording())
        return recordings;

    // Unlooped: while loop recording, the recording runs on through every pass.
    auto* context = impl->edit().getCurrentPlaybackContext();

    if (context == nullptr)
        return recordings;

    const auto now = context->getUnloopedPosition().inSeconds();
    const auto loop = getLoopRange();

    for (auto* t : te::getAudioTracks (impl->edit()))
    {
        if (auto* input = impl->inputOf (*t); input != nullptr && input->isRecording (t->itemID))
        {
            const auto start = input->getPunchInTime (t->itemID).inSeconds();
            auto length = now - start;

            // A loop recording's later passes become takes over the same range.
            if (isLooping())
                length = std::min (length, loop.end - start);

            recordings.push_back ({ t->itemID.toString(), start, std::max (0.0, length) });
        }
    }

    return recordings;
}

std::unique_ptr<ClipWaveform> ApplicationModel::createRecordingWaveform (const juce::String& trackId) const
{
    auto* track = findAudioTrack (impl->edit(), trackId);
    auto* input = track != nullptr ? impl->inputOf (*track) : nullptr;

    // A MIDI recording has no waveform.
    if (input == nullptr || ! input->isRecording (track->itemID)
        || input->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
        return nullptr;

    auto thumbnail = impl->edit().engine.getRecordingThumbnailManager().getThumbnailFor (input->getRecordingFile (track->itemID));
    return std::make_unique<ClipWaveform> (std::make_unique<ClipWaveform::Impl> (std::move (thumbnail)));
}

void ApplicationModel::addListener (Listener* l) const      { impl->listeners.add (l); }
void ApplicationModel::removeListener (Listener* l) const   { impl->listeners.remove (l); }

} // namespace resamper
