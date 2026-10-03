#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace resamper
{

class ProjectManager;

/** Loop repeats a drawn shape with the transport. Audio trigger opens an
    envelope from the track's own audio. */
enum class ShaperMode { loop, audioTrigger };

/** One vertex of a loop shape. time is 0..1 across the cycle, value is 0..1. */
struct ShaperShapePoint
{
    float time = 0;
    float value = 0;
};

/** One shaper: a modifier on a track, assigned to one parameter. */
struct ShaperInfo
{
    juce::String id;
    juce::String trackId;
    juce::String parameterKey;
    ShaperMode mode = ShaperMode::loop;
    double lengthBeats = 1.0;
    float depth = 1.0f;          ///< assignment depth 0..1
    float attackSeconds = 0.01f; ///< audio trigger
    float holdSeconds = 0.0f;
    float releaseSeconds = 0.1f;
    float thresholdDb = -20.0f;
    std::vector<ShaperShapePoint> shape; ///< loop mode, at most 4 points
};

/** Facade over Tracktion modifiers. Not a second automation curve.

    Loop mode is a transport-synced breakpoint oscillator. Audio-trigger mode
    is an envelope follower on the track. Every call re-reads
    ProjectManager::getEdit(). Switching mode replaces the modifier in one
    undo step. Unknown tracks, keys, and ids change nothing.
*/
class Shaper
{
public:
    explicit Shaper (ProjectManager&);

    /** Loop inserts a breakpoint oscillator; audioTrigger inserts an envelope follower.
        Assigns it to the target parameter. */
    juce::Result add (const juce::String& trackId, const juce::String& parameterKey, ShaperMode mode);

    bool remove (const juce::String& shaperId);
    bool setLoop (const juce::String& shaperId, double lengthBeats, const std::vector<ShaperShapePoint>& shape, float depth);
    bool setAudioTrigger (const juce::String& shaperId, float attackSeconds, float holdSeconds, float releaseSeconds,
                          float thresholdDb, float depth);

    std::vector<ShaperInfo> getShapers (const juce::String& trackId) const;

private:
    ProjectManager& projects;

    JUCE_DECLARE_NON_COPYABLE (Shaper)
};

} // namespace resamper
