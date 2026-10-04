#include "ClipComponent.h"
#include "UI/Theme/Interaction.h"

namespace resamper
{

ClipComponent::ClipComponent (const ApplicationModel& m, ThemeManager& tm, const ClipInfo& info)
    : model (m), themeManager (tm)
{
    setInterceptsMouseClicks (false, false);
    setClip (info);
}

void ClipComponent::setClip (const ClipInfo& info)
{
    // A new file (a replaced sample, another take) needs a new waveform; a trim
    // or a tempo change does not.
    const bool audioFileChanged = info.kind == TrackKind::audio
                               && (info.playbackFile != clip.playbackFile || waveform == nullptr);

    if (info.kind != TrackKind::audio)
        waveform.reset();

    clip = info;

    if (audioFileChanged)
        waveform = model.createWaveform (clip.id, *this);

    repaint();
}

void ClipComponent::setTrackLook (juce::Colour c, bool isMuted)
{
    if (c == colour && isMuted == muted)
        return;

    colour = c;
    muted = isMuted;
    setAlpha (muted ? 0.5f : 1.0f);
    repaint();
}

void ClipComponent::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    auto bounds = getLocalBounds();
    constexpr float radius = 5.0f;
    const auto ink = theme.textOnAccent;

    g.setColour (colour);
    g.fillRoundedRectangle (bounds.toFloat(), radius);

    // The part its parent shows: a clip can start off-screen or be far wider
    // than the screen. Not g.getClipBounds(): a partial repaint, such as the
    // Playhead's strip, would move whatever is placed by it (#88).
    auto onScreen = getLocalBounds();

    if (auto* parent = getParentComponent())
        onScreen = onScreen.getIntersection (getLocalArea (parent, parent->getLocalBounds()));

    auto header = bounds.removeFromTop (metrics.clipHeaderHeight);
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (getLocalBounds().toFloat(), radius);
        g.reduceClipRegion (shape);
        g.setColour (ink.withAlpha ((juce::uint8) 0x22));
        g.fillRect (header);
    }

    // Keep the name readable when the clip starts off-screen.
    auto nameArea = header.withLeft (onScreen.getX()).reduced (6, 0);
    const auto take = clip.numTakes == 0 ? juce::String()
                    : clip.currentTake < 0 ? "  (" + juce::String (clip.numTakes) + " takes)"
                                           : "  (Take " + juce::String (clip.currentTake + 1) + "/" + juce::String (clip.numTakes) + ")";
    drawStyledText (g, themeManager, clip.name + take, TypeStyle { 9.5f, false, 700 }, nameArea,
                    juce::Justification::centredLeft, ink);

    const auto content = ink.withAlpha ((juce::uint8) 0x88);
    const auto body = bounds.reduced (0, 4);

    if (clip.kind == TrackKind::midi)
    {
        if (getWidth() > 0 && clip.lengthSeconds > 0 && ! clip.notes.empty())
        {
            // Note dashes, placed by pitch across the clip's own range.
            int low = 127, high = 0;

            for (auto& note : clip.notes)
            {
                low = std::min (low, note.pitch);
                high = std::max (high, note.pitch);
            }

            const auto noteHeight = (float) metrics.midiNoteHeight;
            const auto span = (float) juce::jmax (12, high - low + 1);
            g.setColour (content);

            for (auto& note : clip.notes)
            {
                const auto x = (float) (note.startSeconds / clip.lengthSeconds) * (float) getWidth();
                const auto w = juce::jmax (2.0f, (float) (note.lengthSeconds / clip.lengthSeconds) * (float) getWidth() - 1.0f);
                const auto y = (float) body.getBottom() - noteHeight - ((float) (note.pitch - low) / span) * ((float) body.getHeight() - noteHeight);
                g.fillRoundedRectangle (x, y, w, noteHeight, 1.0f);
            }
        }
    }
    else if (waveform != nullptr && getWidth() > 0 && clip.lengthSeconds > 0)
    {
        // Only the slice being painted: a zoomed-in clip can be far wider than the screen.
        auto painted = body.getIntersection (g.getClipBounds());
        const auto textArea = body.getIntersection (onScreen);

        if (! painted.isEmpty())
        {
            const auto secondsPerPixel = clip.lengthSeconds / getWidth();

            g.setColour (content);
            waveform->draw (g, painted, painted.getX() * secondsPerPixel, painted.getRight() * secondsPerPixel,
                            clip.sourceOffsetSeconds);

            // Text only while there is nothing to draw; over a waveform still
            // being completed, just the progress, out of its way.
            if (waveform->isGenerating())
            {
                const auto percent = juce::String (juce::roundToInt (waveform->getProgress() * 100.0)) + "%";

                if (waveform->hasDrawableAudio())
                    drawStyledText (g, themeManager, percent, theme.bodySm, textArea.reduced (metrics.spaceSm, metrics.space2xs),
                                    juce::Justification::bottomRight, ink);
                else
                    drawStyledText (g, themeManager, "Preparing audio " + percent, theme.bodySm, textArea.reduced (metrics.spaceSm),
                                    juce::Justification::centredLeft, ink);
            }
        }
    }

    if (clip.selected)
    {
        g.setColour (theme.clipOutlineSelected);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), radius, 1.0f);
    }
}

} // namespace resamper
