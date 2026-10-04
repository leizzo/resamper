#pragma once

#include "Engine/ApplicationModel.h"
#include "Engine/Mixer.h"
#include "Engine/PluginRack.h"
#include "InsertSlot.h"
#include "StripParts.h"
#include "UI/Controls/Controls.h"

#include <memory>
#include <vector>

namespace resamper
{

class CommandRegistry;

/** What a strip shows (PRD §10.2): the Mixer's Strip and the inputs of its
    Track Kind it can record from. */
struct StripState
{
    Strip strip;
    juce::StringArray inputs;

    bool isReturn() const noexcept   { return strip.role == StripRole::returnTrack; }
    bool isBus() const noexcept      { return strip.role == StripRole::bus; }
};

/** One mixer channel strip, 145 wide (PRD §10.2), top to bottom in signal
    order: head (colour bar, number, name), I/O, the read-only Track chain row,
    a flow arrow, the mixer inserts, sends, channel pan, the fader section
    (gain and peak readouts, fader, stereo meter) and M / S / ●.

    Every change goes through a Command; a fader or knob drag is one undo step.
    Clicking the strip's background selects its track in every view. Whole
    sections can be hidden (the mixer's section chips); the fader takes the
    freed height.

    A Bus Strip (PRD §11.3) is narrower and tinted in the folder colour, with
    the git-merge icon for a number, its input chip (← N tracks) in place of
    the input select, a wider meter, and no Track chain row or arm. */
class ChannelStrip : public juce::Component,
                     public juce::SettableTooltipClient,
                     public juce::DragAndDropTarget
{
public:
    enum class Section { io, inserts, sends, fader };

    /** The role picks the fader geometry; a Strip's role never changes. The insert
        slots hear their plug-ins' Hosting States from Plug-in Hosting. */
    ChannelStrip (CommandRegistry&, const PluginHosting&, ThemeManager&, StripRole);
    ~ChannelStrip() override;

    void setState (const StripState&);
    const StripState& getState() const noexcept   { return state; }

    void setLevel (StereoLevel, double elapsedSeconds);
    void resetPeaks();
    void setMeterMode (MeterMode m)   { faderSection.setMeterMode (m); }

    void setSectionVisible (Section, bool);

    /** The Track chain row was clicked: show this track's device chain. */
    std::function<void()> onTrackChainClicked;

    /** A right-click on the strip: its track's mixer menu (sends, bus). */
    std::function<void()> onShowMenu;

    /** The pointer is over a section (the toolbar's signal-flow indicator); -1: none. */
    std::function<void (int stage)> onFlowStageHovered;

    /** An insert slot wants the effects picker: to fill it (replacing empty) or to replace that insert. */
    std::function<void (InsertSlot&, const juce::String& replacing)> onPickInsert;

    /** A filled insert slot was clicked: open its device's window (a
        plug-in's window, or a native device's floating Expanded editor). */
    std::function<void (const juce::String& pluginId)> onOpenPlugin;

    // Mixer inserts (PRD §10.6): drop a Browser effect on an empty slot,
    // drag an insert to reorder it, Alt+drag it onto another strip to copy.
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static constexpr int visibleInsertSlots = 4;

private:
    struct SendRow;

    CommandRegistry& commands;
    ThemeManager& themeManager;
    StripState state;
    juce::Colour colour;

    juce::ComboBox input;
    std::vector<std::unique_ptr<InsertSlot>> insertSlots;
    std::vector<std::unique_ptr<SendRow>> sendRows;
    Knob pan;
    FaderSection faderSection;
    TrackButton mute, solo, arm;

    std::array<bool, 4> sectionShown { true, true, true, true };
    juce::Rectangle<int> headArea, ioArea, chainArea, chainLink, flowArea, insertsArea, sendsArea, panArea,
                         faderArea, buttonsArea;

    bool shown (Section s) const   { return sectionShown[(size_t) s]; }
    void setUpInsertSlot (InsertSlot&);
    void showInsertMenu (InsertSlot&);
    InsertSlot* slotAt (juce::Point<int>) const;

    /** Whether a drop on that slot would do something, and if not, why not. */
    juce::String dropRefusal (const SourceDetails&, const InsertSlot&) const;
    void clearDropHighlights();
    void rebuildSends();
    void rebuildInsertSlots();
    juce::String chainSummary() const;
    int flowStageAt (juce::Point<int>) const;
    void paintSectionHeader (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, const juce::String& tag,
                             juce::Colour tagColour) const;
};

} // namespace resamper
