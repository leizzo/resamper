#include "TrackHeader.h"
#include "Commands/AppCommands.h"

namespace resamper
{

namespace
{
    constexpr int paddingX = 12, paddingY = 10, buttonWidth = 20, buttonHeight = 17, buttonGap = 4;

    const char* const paletteNames[] = { NEEDS_TRANS ("Drums"), NEEDS_TRANS ("Bass"), NEEDS_TRANS ("Chords"), NEEDS_TRANS ("Pads"),
                                         NEEDS_TRANS ("Arp"), NEEDS_TRANS ("Vocal"), NEEDS_TRANS ("FX") };
}

TrackHeader::TrackHeader (CommandRegistry& c, ThemeManager& tm, const TrackInfo& info, const juce::StringArray& inputList)
    : commands (c), themeManager (tm), track (info),
      arm (tm, TrackButton::Kind::arm), solo (tm, TrackButton::Kind::solo),
      mute (tm, TrackButton::Kind::mute), automation (tm, TrackButton::Kind::automation)
{
    arm.setTooltip (TRANS ("Arm for recording"));
    solo.setTooltip (TRANS ("Solo (S)"));
    mute.setTooltip (TRANS ("Mute"));
    automation.setTooltip (TRANS ("Show automation"));

    arm.onClick = [this] { commands.invoke (cmd::trackToggleArm, { track.id }); };
    solo.onClick = [this] { commands.invoke (cmd::trackToggleSolo, { track.id }); };
    mute.onClick = [this] { commands.invoke (cmd::trackToggleMute, { track.id }); };
    automation.onClick = [this] { if (onToggleAutomation) onToggleAutomation(); };

    for (auto* b : { &arm, &solo, &mute, &automation })
        addAndMakeVisible (b);

    // A click selects the track. Taking focus would hand it to the first button (Arm).
    setMouseClickGrabsKeyboardFocus (false);

    setTrack (info, inputList, false);
}

void TrackHeader::setTrack (const TrackInfo& info, const juce::StringArray& inputList, bool shown)
{
    track = info;
    inputs = inputList;
    automationShown = shown;

    arm.setToggleState (track.armed, juce::dontSendNotification);
    solo.setToggleState (track.solo, juce::dontSendNotification);
    mute.setToggleState (track.muted, juce::dontSendNotification);
    automation.setToggleState (automationShown, juce::dontSendNotification);

    // Audio and MIDI tracks record; a return never does.
    arm.setVisible (! track.isReturn);
    setTitle (track.name);
    resized();
    repaint();
}

juce::Rectangle<int> TrackHeader::chevronBounds() const
{
    return { paddingX, paddingY, 11, 16 };
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto bounds = getLocalBounds();

    g.setColour (track.selected ? theme.bgElevated : theme.bgTrack);
    g.fillRect (bounds);
    g.setColour (theme.borderSoft);
    g.fillRect (bounds.removeFromBottom (1));
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    if (track.selected)
    {
        g.setColour (theme.accent);
        g.fillRect (0, 0, 2, getHeight());
    }

    auto title = getLocalBounds().reduced (paddingX, paddingY).removeFromTop (16);
    drawIcon (g, automationShown ? Icon::chevronDown : Icon::chevronRight,
              title.removeFromLeft (11).toFloat().withSizeKeepingCentre (11.0f, 11.0f), theme.textDim);
    title.removeFromLeft (8);

    const auto colour = theme.trackColour (track.colourIndex);
    g.setColour (colour);
    g.fillRoundedRectangle (title.removeFromLeft (9).withSizeKeepingCentre (9, 9).toFloat(), 3.0f);
    title.removeFromLeft (8);

    drawStyledText (g, themeManager, track.name, TypeStyle { 12.5f, false, 600 }, title, juce::Justification::centredLeft,
                    theme.textPrimary);
}

void TrackHeader::resized()
{
    auto row = getLocalBounds().reduced (paddingX, paddingY).withTrimmedTop (16 + 7).removeFromTop (buttonHeight);

    for (auto* b : { &arm, &solo, &mute, &automation })
    {
        if (! b->isVisible())
            continue;

        b->setBounds (row.removeFromLeft (b == &automation ? buttonWidth + 8 : buttonWidth));
        row.removeFromLeft (buttonGap);
    }
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    if (chevronBounds().expanded (4).contains (e.getPosition()) && onToggleAutomation)
        onToggleAutomation();
}

void TrackHeader::showMenu()
{
    // The menu outlives a click, and the header may be rebuilt before an item is
    // picked: the items hold the registry (the app's) and the track id, not this.
    auto& theme = themeManager.getTheme();
    auto& registry = commands;
    const auto id = track.id;
    juce::PopupMenu colours;

    for (int i = 0; i < ApplicationModel::trackPaletteSize; ++i)
        colours.addItem (juce::PopupMenu::Item (TRANS (paletteNames[i]))
                             .setColour (theme.trackPalette[(size_t) i])
                             .setTicked (i == track.colourIndex)
                             .setAction ([&registry, id, i] { registry.invoke (cmd::trackSetColour, { id, i }); }));

    juce::PopupMenu menu;
    menu.addSubMenu (TRANS ("Colour"), colours);

    if (! track.isReturn)
    {
        juce::PopupMenu inputMenu;
        inputMenu.addItem (TRANS ("No Input"), true, track.input.isEmpty(),
                           [&registry, id] { registry.invoke (cmd::trackSetInput, { id, {} }); });

        for (auto& input : inputs)
            inputMenu.addItem (input, true, input == track.input,
                               [&registry, id, input] { registry.invoke (cmd::trackSetInput, { id, input }); });

        menu.addSubMenu (TRANS ("Input"), inputMenu);
    }

    menu.addSeparator();
    menu.addItem (TRANS ("Show Automation"), true, automationShown,
                  [safeThis = juce::Component::SafePointer<TrackHeader> (this)]
                  {
                      if (safeThis != nullptr && safeThis->onToggleAutomation)
                          safeThis->onToggleAutomation();
                  });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

} // namespace resamper
