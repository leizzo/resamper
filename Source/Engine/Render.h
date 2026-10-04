#pragma once

#include "EngineInternal.h"
#ifdef RESAMPER_ENGINE_INTERNAL
#include "PluginHosting.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper
{

namespace te = tracktion;

/** Offline rendering shared by the engine facades (bounce, export, consolidate).
    Engine-internal: it takes Tracktion types. */
namespace render
{
    /** Bit for one track in Renderer::Parameters::tracksToDo.

        te::toBitSet sets every track whenever the array is non-empty, so a
        one-track bounce cannot use it. The renderer indexes getAllTracks. */
    juce::BigInteger bitForTrack (te::Track&);

    /** Renders the tracks to a 24-bit WAV, as they play (honouring mute and
        solo), with their plug-ins and faders. range: the whole Edit when empty.

        Waits first for any plug-in still Loading into its Sandbox, so none is
        left out; fails if one still is when its time is up. On the message thread. */
    juce::Result toWav (PluginHosting&, te::Edit&, const juce::File& destFile, const juce::BigInteger& tracksToDo,
                        te::TimeRange range = {});

    /** Renders the clips' own audio to a 24-bit WAV, without the tracks'
        plug-ins or faders (consolidate), so no plug-in load is waited for.
        range: the whole Edit when empty. */
    juce::Result clipsToWav (te::Edit&, const juce::File& destFile, const juce::BigInteger& tracksToDo,
                             te::TimeRange range = {});
}

} // namespace resamper
#endif
