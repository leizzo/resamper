#pragma once

#include "EngineInternal.h"
#ifdef RESAMPER_ENGINE_INTERNAL
#include "ApplicationModel.h"

#include <tracktion_engine/tracktion_engine.h>

// Track rules every facade shares: looking up an audio track or a Bus by id,
// its fader, its Track Kind (a saved property) and whether it is a Return or a
// Bus. Engine-internal: it names Tracktion types.

namespace resamper
{

/** The audio track with this id (an EditItemID string), or nullptr. */
tracktion::AudioTrack* findAudioTrack (const tracktion::Edit&, const juce::String& trackId);

/** Whether the track has a Strip of its own: an audio track or a Bus. */
bool isStripTrack (const tracktion::Track&);

/** The track a Strip stands for: an audio track or a Bus, else nullptr. */
tracktion::Track* findStripTrack (const tracktion::Edit&, const juce::String& trackId);

/** Whether the track is a Bus: a Folder in Folder + Bus mode. */
bool isBus (const tracktion::Track&);

/** The fader of an audio track or a Bus, or nullptr. */
tracktion::VolumeAndPanPlugin* faderOf (tracktion::Track&);

/** A track's Track Kind. It is a saved property, not "whichever instrument is
    loaded"; absent (engine-made tracks, older projects) means audio. */
TrackKind trackKindOf (const tracktion::Track&);

/** Whether the track's Track Kind is MIDI. */
bool isMidi (const tracktion::Track&);

/** Gives a new track the MIDI Track Kind, the only way a track gets one. */
void markMidi (tracktion::Track&, juce::UndoManager*);

/** On a track's or a clip's ValueTree: its index in the track palette. Saved in
    project files: never rename. */
extern const juce::Identifier colourProperty;

/** A track's palette colour: the saved one; else an audio track's by its order
    among audio tracks, a Bus's from its first child, else 0. */
int colourOf (const tracktion::Track&);

/** Whether the track is a Return: it holds an aux return. */
bool isReturnTrack (const tracktion::AudioTrack&);

} // namespace resamper
#endif
