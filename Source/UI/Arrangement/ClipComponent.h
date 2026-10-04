#pragma once

#include "Engine/ApplicationModel.h"

namespace resamper
{

class ThemeManager;

/** One clip in a lane (PRD §8.1): a rounded box in its track's colour with a
    title bar, and either a waveform or MIDI note dashes. Selected: a white
    1 px outline. On a muted track it draws at 50 %. Its bounds are set by TrackLanes; a waveform
    draws the part of the source file that its visible area covers. */
class ClipComponent : public juce::Component
{
public:
    ClipComponent (const ApplicationModel&, ThemeManager&, const ClipInfo&);

    const ClipInfo& getClip() const noexcept   { return clip; }
    void setClip (const ClipInfo&);

    /** What its body draws; nullptr for a MIDI clip. Kept while the clip plays
        the same file, so a move doesn't read the audio again (#90). */
    const ClipWaveform* getWaveform() const noexcept   { return waveform.get(); }

    /** Its track's colour, and whether the track is muted (clips then draw at 50 %). */
    void setTrackLook (juce::Colour, bool muted);

    void paint (juce::Graphics&) override;

private:
    const ApplicationModel& model;
    ThemeManager& themeManager;
    ClipInfo clip;
    juce::Colour colour;
    bool muted = false;
    std::unique_ptr<ClipWaveform> waveform;
};

} // namespace resamper
