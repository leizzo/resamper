#pragma once

#include "EngineInternal.h"
#ifdef RESAMPER_ENGINE_INTERNAL
#include "ClipWaveform.h"

#include <tracktion_engine/tracktion_engine.h>

namespace resamper
{

/** Either a clip's file thumbnail or a recording's growing one. */
struct ClipWaveform::Impl
{
    /** The file the clip plays. */
    Impl (tracktion::WaveAudioClip&, juce::Component& repaintTarget);

    explicit Impl (tracktion::RecordingThumbnailManager::Thumbnail::Ptr recording)
        : recordingThumbnail (std::move (recording))
    {
    }

    /** The file the clip plays, its peaks read on a background thread. */
    std::unique_ptr<tracktion::SmartThumbnail> thumbnail;

    /** The clip, for its timing: a warped clip's file is drawn segment by
        segment, each where the clip plays it now (after any trim or tempo change). */
    tracktion::SafeSelectable<tracktion::WaveAudioClip> clip;

    tracktion::RecordingThumbnailManager::Thumbnail::Ptr recordingThumbnail;
};

} // namespace resamper
#endif
