#pragma once

#include "BeatRuler.h"
#include "NoteGrid.h"
#include "PianoKeyboard.h"
#include "UI/Arrangement/Playhead.h"
#include "UI/State/ArrangementViewState.h"
#include "VelocityEditor.h"

#include <vector>

namespace resamper
{

class CommandRegistry;
class UIStateStore;

/** The MIDI clip editor: keyboard, beat ruler, note grid, and velocity lane.

    Opened by double-clicking a MIDI clip. Zoom and scroll live in UI State
    under "pianoRoll", separate from the Arrangement, and work the same way:
    the wheel scrolls, cmd/ctrl + wheel zooms around the pointer.

    Which clip is open is UI State too, so it survives a theme reload.
*/
class PianoRollView : public juce::Component,
                      private ApplicationModel::Listener,
                      private ThemeManager::Listener,
                      private juce::ValueTree::Listener
{
public:
    static constexpr const char* componentId = "pianoRoll";

    PianoRollView (const ApplicationModel&, CommandRegistry&, ThemeManager&, UIStateStore&);
    ~PianoRollView() override;

    /** The open clip is still in the Edit. */
    bool isOpen() const;

    /** The MIDI clip being edited, or empty. */
    juce::String openClipId() const;

    /** Shows this MIDI clip, scrolled to its start with middle C in view. */
    void openClip (const juce::String& clipId);

    /** The host shows or hides this view. */
    std::function<void()> onOpenStateChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

private:
    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ArrangementViewState view;

    juce::TextButton arrangementButton { TRANS ("Arrangement") };
    juce::TextButton quarterButton { "1/4" };
    juce::TextButton eighthButton { "1/8" };
    juce::TextButton sixteenthButton { "1/16" };
    juce::TextButton deleteButton { TRANS ("Delete") };

    PianoKeyboard keyboard;
    BeatRuler ruler;
    NoteGrid grid;
    VelocityEditor velocity;
    Playhead playhead;

    std::vector<TrackInfo> tracks;
    juce::Rectangle<int> toolbarBounds, nameArea, velocityLabel;

    const ClipInfo* currentClip() const;
    void refresh();
    void close();
    void quantize (const char* gridName);
    void layoutToolbar (juce::Rectangle<int>);
    void clampVerticalScroll();

    void modelChanged() override;
    void themeChanged() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
};

} // namespace resamper
