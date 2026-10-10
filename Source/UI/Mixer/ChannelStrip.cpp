#include "ChannelStrip.h"
#include "Commands/AppCommands.h"
#include "Commands/MixerCommands.h"
#include "Commands/PluginCommands.h"
#include "UI/Browser/Library.h"
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    using namespace StripMetrics;

    constexpr int labelHeight = 11, slotHeight = 19, selectHeight = 20, chainLinkHeight = 22, flowHeight = 10,
                  sendHeight = 18, panHeight = 22, buttonHeight = 20;

    /** The design's channel strip: fader track centred at 46 px, 7 px meter wells. */
    constexpr FaderSection::Geometry faderGeometry { 64, 7.0f };

    /** A Bus Strip: narrower fader, wider meter wells. */
    constexpr FaderSection::Geometry busFaderGeometry { 40, 9.0f };

    /** The Bus Strip's tint and outline, and the tint of a chip or badge, as alphas of their colour. */
    constexpr float busTintAlpha = 0.08f, busOutlineAlpha = 0.6f, chipTintAlpha = 0.15f;

    constexpr int headIconSize = 12, chipPadX = 7, badgePadX = 4, badgeGap = 4;

    /** Signal-flow stages, as the mixer toolbar names them. */
    enum Stage { trackChainStage, insertsStage, sendsStage, faderStage };



    juce::String twoDigits (int n)   { return n < 10 ? "0" + juce::String (n) : juce::String (n); }
}

//==============================================================================
/** A send: its return's letter (click: mute), a level bar, the level. */
struct ChannelStrip::SendRow : juce::Component
{
    SendRow (ThemeManager& tm, CommandRegistry& c) : themeManager (tm), commands (c), level (tm, sendSpec())
    {
        level.setTitle (TRANS ("Send level"));
        addAndMakeVisible (level);
    }

    static ContinuousValue::Spec sendSpec()
    {
        auto spec = gainReadoutSpec();
        spec.toProportion = [] (double db) { return 1.0 - FaderLaw::dbToTravel (db); };
        spec.fromProportion = [] (double p) { return FaderLaw::travelToDb (1.0 - p); };
        return spec;
    }

    void paint (juce::Graphics& g) override
    {
        auto& theme = themeManager.getTheme();
        auto badge = getLocalBounds().removeFromLeft (14).withSizeKeepingCentre (14, 14);
        g.setColour (send.muted || send.gain <= ApplicationModel::minVolume ? theme.textDim : theme.returnColours[0]);
        g.fillRoundedRectangle (badge.toFloat(), theme.radiusSm);
        drawStyledText (g, themeManager, letter, TypeStyle { 8.0f, false, 700 }, badge, juce::Justification::centred,
                        theme.textOnAccent);
        drawNumber (g, themeManager, send.muted ? TRANS ("off") : juce::String (send.gain.value, 1),
                    TypeStyle { 9.0f, true, 400 }, getLocalBounds().removeFromRight (30), juce::Justification::centredRight,
                    theme.textSecondary);
    }

    void resized() override
    {
        level.setBounds (getLocalBounds().withTrimmedLeft (18).withTrimmedRight (34));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < 16)
            commands.invoke (cmd::mixerSetSendMuted, { trackId, send.id, ! send.muted });
    }

    ThemeManager& themeManager;
    CommandRegistry& commands;
    Slider level;
    SendInfo send;
    juce::String trackId, letter;
};

//==============================================================================
ChannelStrip::ChannelStrip (CommandRegistry& c, const PluginHosting& hosting, ThemeManager& tm, StripRole role)
    : commands (c), themeManager (tm),
      pan (tm, panKnobSpec(), TRANS ("Pan"), true), faderSection (tm, role == StripRole::bus ? busFaderGeometry : faderGeometry),
      mute (tm, TrackButton::Kind::mute), solo (tm, TrackButton::Kind::solo), arm (tm, TrackButton::Kind::arm)
{
    input.setTitle (TRANS ("Input"));
    input.onChange = [this]
    {
        const auto index = input.getSelectedItemIndex();
        const auto chosen = index > 0 ? state.inputs[index - 1] : juce::String();

        if (chosen != state.strip.input)
            commands.invoke (cmd::trackSetInput, { state.strip.id, chosen });
    };

    pan.setDialSize (panHeight);
    pan.setReadoutBeside (true);
    pan.onChange = [this] (double v, bool continues) { commands.invoke (cmd::trackSetPan, { state.strip.id, v, continues }); };

    faderSection.onVolumeChange = [this] (Decibels volume, bool continues)
    {
        commands.invoke (cmd::trackSetVolume, { state.strip.id, volume, continues });
    };

    mute.onClick = [this] { commands.invoke (cmd::trackToggleMute, { state.strip.id }); };
    solo.onClick = [this] { commands.invoke (cmd::trackToggleSolo, { state.strip.id }); };
    arm.onClick = [this] { commands.invoke (cmd::trackToggleArm, { state.strip.id }); };

    for (auto* child : std::initializer_list<juce::Component*> { &input, &pan, &faderSection, &mute, &solo, &arm })
        addAndMakeVisible (child);

    for (int i = 0; i < PluginRack::maxMixerInserts; ++i)
    {
        insertSlots.push_back (std::make_unique<InsertSlot> (themeManager, hosting, i));
        setUpInsertSlot (*insertSlots.back());
        addChildComponent (*insertSlots.back());
    }
}

ChannelStrip::~ChannelStrip() = default;

void ChannelStrip::setUpInsertSlot (InsertSlot& slot)
{
    slot.onClick = [this, &slot] (const juce::MouseEvent&)
    {
        if (slot.getPlugin())
        {
            if (onOpenPlugin)
                onOpenPlugin (slot.getPlugin()->id);
        }
        else if (onPickInsert)
        {
            onPickInsert (slot, {});
        }
    };

    slot.onPowerClick = [this, &slot] (const juce::MouseEvent&)
    {
        if (auto& plugin = slot.getPlugin())
            commands.invoke (cmd::pluginSetBypassed, { state.strip.id, plugin->id, plugin->enabled });
    };

    slot.onMenu = [this, &slot] (const juce::MouseEvent&) { showInsertMenu (slot); };

    slot.onDrag = [this, &slot] (const juce::MouseEvent&)
    {
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);

        if (container == nullptr || container->isDragAndDropActive() || ! slot.getPlugin())
            return;

        auto d = new juce::DynamicObject();
        d->setProperty ("mixerInsert", slot.getPlugin()->id);
        d->setProperty ("fromTrack", state.strip.id);
        container->startDragging (juce::var (d), &slot, juce::ScaledImage (slot.createComponentSnapshot (slot.getLocalBounds())));
    };
}

void ChannelStrip::showInsertMenu (InsertSlot& slot)
{
    juce::PopupMenu menu;
    const auto& plugin = slot.getPlugin();

    if (! plugin)
    {
        menu.addItem (TRANS ("Add Effect..."), [this, &slot] { if (onPickInsert) onPickInsert (slot, {}); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slot));
        return;
    }

    const auto trackId = state.strip.id;
    const auto id = plugin->id;
    const auto name = plugin->name;

    menu.addItem (TRANS ("Replace..."), [this, &slot, id] { if (onPickInsert) onPickInsert (slot, id); });
    menu.addItem (plugin->enabled ? TRANS ("Bypass") : TRANS ("Enable"),
                  [this, trackId, id, on = plugin->enabled] { commands.invoke (cmd::pluginSetBypassed, { trackId, id, on }); });
    menu.addItem (TRANS ("Remove"), [this, trackId, id] { commands.invoke (cmd::pluginRemove, { trackId, id }); });
    menu.addItem (TRANS ("Save Preset..."), false, false, nullptr);   // presets arrive with the racks work
    menu.addSeparator();
    menu.addItem (TRANS ("Move to Track Chain"), [this, trackId, id, name]
    {
        // It changes where the sound is made, so it asks first (PRD §10.6).
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, TRANS ("Move to Track Chain"),
                                            tr ("Move \"%1\" from the mixer inserts to the end of the track's device chain?", name),
                                            TRANS ("Move"), TRANS ("Cancel"), this,
                                            juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ChannelStrip> (this), trackId, id] (int result)
                                            {
                                                if (result == 1 && safe != nullptr)
                                                    safe->commands.invoke (cmd::pluginMoveToDeviceChain, { trackId, id });
                                            }));
    });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slot));
}

InsertSlot* ChannelStrip::slotAt (juce::Point<int> p) const
{
    for (auto& slot : insertSlots)
        if (slot->isVisible() && slot->getBounds().expanded (0, 2).contains (p))
            return slot.get();

    return nullptr;
}

juce::String ChannelStrip::dropRefusal (const SourceDetails& details, const InsertSlot& slot) const
{
    if (auto moved = details.description["mixerInsert"].toString(); moved.isNotEmpty())
    {
        const auto sameStrip = details.description["fromTrack"].toString() == state.strip.id;

        if (sameStrip)
            return {};

        if (! juce::ModifierKeys::currentModifiers.isAltDown())
            return TRANS ("Alt+drag copies an insert to another strip");

        return (int) state.strip.inserts.size() >= PluginRack::maxMixerInserts ? TRANS ("This strip's inserts are full") : juce::String();
    }

    if (auto item = itemFromDrag (details.description))
    {
        if (item->kind != LibraryItem::Kind::plugin)
            return TRANS ("Only effects go in mixer inserts");

        if (item->instrument || item->midiEffect)
            return TRANS ("Mixer inserts take effects only");

        if (slot.getPlugin())
            return TRANS ("Drop on an empty slot");
    }

    return {};
}

void ChannelStrip::clearDropHighlights()
{
    for (auto& slot : insertSlots)
        slot->setDropHighlight (std::nullopt);
}

bool ChannelStrip::isInterestedInDragSource (const SourceDetails& details)
{
    if (! shown (Section::inserts))
        return false;

    if (details.description.hasProperty ("mixerInsert"))
        return true;

    auto item = itemFromDrag (details.description);
    return item && item->kind == LibraryItem::Kind::plugin;
}

void ChannelStrip::itemDragMove (const SourceDetails& details)
{
    clearDropHighlights();

    if (auto* slot = slotAt (details.localPosition))
        slot->setDropHighlight (dropRefusal (details, *slot).isEmpty());
}

void ChannelStrip::itemDragExit (const SourceDetails&)
{
    clearDropHighlights();
}

void ChannelStrip::itemDropped (const SourceDetails& details)
{
    clearDropHighlights();
    auto* slot = slotAt (details.localPosition);

    if (slot == nullptr)
        return;

    if (auto why = dropRefusal (details, *slot); why.isNotEmpty())
    {
        rejectWithShake (*slot, why);
        return;
    }

    const auto index = juce::jmin (slot->getIndex(), (int) state.strip.inserts.size());

    if (auto moved = details.description["mixerInsert"].toString(); moved.isNotEmpty())
    {
        const auto from = details.description["fromTrack"].toString();

        if (from == state.strip.id)
            commands.invoke (cmd::pluginMove, { state.strip.id, moved, juce::jmin (index, (int) state.strip.inserts.size() - 1) });
        else
            commands.invoke (cmd::pluginCopyInsert, { from, moved, state.strip.id, index });

        return;
    }

    if (auto item = itemFromDrag (details.description))
        commands.invoke (cmd::pluginInsert, { state.strip.id, item->pluginPath, PluginChain::mixer });
}

void ChannelStrip::setState (const StripState& next)
{
    state = next;
    colour = state.isReturn() ? themeManager.getTheme().returnColours[0]
                              : themeManager.getTheme().trackColour (state.strip.colourIndex);

    setTitle (state.strip.name);
    setTooltip (state.strip.name);

    // Input choices: "No Input" then each input of the track's kind.
    input.clear (juce::dontSendNotification);
    input.addItem (TRANS ("No Input"), 1);

    for (int i = 0; i < state.inputs.size(); ++i)
        input.addItem (state.inputs[i], i + 2);

    input.setSelectedItemIndex (state.strip.input.isEmpty() ? 0 : state.inputs.indexOf (state.strip.input) + 1,
                                juce::dontSendNotification);
    input.setEnabled (! state.isReturn() && ! state.isBus());

    pan.setValue (state.strip.pan);
    faderSection.setVolume (state.strip.volume, colour);

    mute.setToggleState (state.strip.muted, juce::dontSendNotification);
    solo.setToggleState (state.strip.solo, juce::dontSendNotification);
    arm.setToggleState (state.strip.armed, juce::dontSendNotification);
    arm.setVisible (! state.isReturn() && ! state.isBus());

    rebuildSends();
    rebuildInsertSlots();
    resized();
    repaint();
}

void ChannelStrip::rebuildSends()
{
    const auto showSends = ! state.isReturn();

    if (sendRows.size() != (showSends ? state.strip.sends.size() : 0))
    {
        sendRows.clear();

        for (size_t i = 0; showSends && i < state.strip.sends.size(); ++i)
        {
            auto row = std::make_unique<SendRow> (themeManager, commands);
            row->level.onChange = [this, i] (double db, bool continues)
            {
                if (i < state.strip.sends.size())
                    commands.invoke (cmd::mixerSetSendGain, { state.strip.id, state.strip.sends[i].id, Decibels (db), continues });
            };
            addAndMakeVisible (*row);
            sendRows.push_back (std::move (row));
        }
    }

    for (size_t i = 0; i < sendRows.size(); ++i)
    {
        auto& row = *sendRows[i];
        row.send = state.strip.sends[i];
        row.trackId = state.strip.id;
        row.letter = returnLetterFor (state.strip.sends[i].bus);
        row.level.setValue (state.strip.sends[i].gain.value);
        row.repaint();
    }
}

void ChannelStrip::rebuildInsertSlots()
{
    for (size_t i = 0; i < insertSlots.size(); ++i)
        insertSlots[i]->setPlugin (i < state.strip.inserts.size() ? std::optional<PluginInfo> (state.strip.inserts[i]) : std::nullopt);
}

juce::String ChannelStrip::chainSummary() const
{
    juce::StringArray names;

    for (auto& device : state.strip.deviceChain)
        names.add (device.name);

    return names.isEmpty() ? TRANS ("Empty") : names.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xe2\x80\xba ")));
}

void ChannelStrip::setLevel (StereoLevel level, double elapsedSeconds)
{
    faderSection.setLevel (level, elapsedSeconds);
}

void ChannelStrip::resetPeaks()
{
    faderSection.resetPeaks();
}

void ChannelStrip::setSectionVisible (Section section, bool visible)
{
    sectionShown[(size_t) section] = visible;
    resized();
    repaint();
}

//==============================================================================
void ChannelStrip::resized()
{
    auto r = getLocalBounds();
    headArea = r.removeFromTop (headHeight);

    auto section = [&] (bool visible, int contentHeight)
    {
        if (! visible)
            return juce::Rectangle<int>();

        return r.removeFromTop (2 * sectionPadY + labelHeight + rowGap + contentHeight);
    };

    auto content = [] (juce::Rectangle<int> area)
    {
        return area.reduced (padX, sectionPadY).withTrimmedTop (labelHeight + rowGap);
    };

    // I/O
    ioArea = section (shown (Section::io), 2 * selectHeight + rowGap);
    input.setVisible (shown (Section::io) && ! state.isBus());

    if (shown (Section::io))
        input.setBounds (content (ioArea).removeFromTop (selectHeight));

    // Track chain (read-only) and the flow arrow into the inserts; a Bus Strip has neither.
    const auto showChain = shown (Section::inserts) && ! state.isBus();
    chainArea = section (showChain, chainLinkHeight);
    chainLink = showChain ? content (chainArea) : juce::Rectangle<int>();
    flowArea = showChain ? r.removeFromTop (flowHeight) : juce::Rectangle<int>();

    // Mixer inserts: 4 slots show; a fuller chain grows the section up to 8.
    const auto slots = juce::jlimit (visibleInsertSlots, PluginRack::maxMixerInserts, (int) state.strip.inserts.size() + 1);
    insertsArea = section (shown (Section::inserts), slots * slotHeight + (slots - 1) * rowGap);

    for (int i = 0; i < (int) insertSlots.size(); ++i)
    {
        const auto visible = shown (Section::inserts) && i < slots;
        insertSlots[(size_t) i]->setVisible (visible);

        if (visible)
            insertSlots[(size_t) i]->setBounds (content (insertsArea).withTrimmedTop (i * (slotHeight + rowGap)).withHeight (slotHeight));
    }

    // Sends
    const auto sendCount = (int) sendRows.size();
    sendsArea = section (shown (Section::sends) && sendCount > 0, sendCount * sendHeight + juce::jmax (0, sendCount - 1) * rowGap);

    for (int i = 0; i < sendCount; ++i)
    {
        sendRows[(size_t) i]->setVisible (shown (Section::sends));
        sendRows[(size_t) i]->setBounds (content (sendsArea).withTrimmedTop (i * (sendHeight + rowGap)).withHeight (sendHeight));
    }

    // Buttons at the bottom; pan and the fader section take the rest.
    buttonsArea = r.removeFromBottom (buttonHeight + padX);
    auto buttons = buttonsArea.reduced (padX, 0).withTrimmedBottom (padX);
    const auto buttonCount = arm.isVisible() ? 3 : 2;
    const auto buttonWidth = (buttons.getWidth() - (buttonCount - 1) * rowGap) / buttonCount;

    for (auto* b : { static_cast<juce::Component*> (&mute), static_cast<juce::Component*> (&solo), static_cast<juce::Component*> (&arm) })
    {
        if (! b->isVisible())
            continue;

        b->setBounds (buttons.removeFromLeft (buttonWidth));
        buttons.removeFromLeft (rowGap);
    }

    panArea = r.removeFromTop (panHeight + 2 * sectionPadY);
    pan.setBounds (panArea.reduced (padX, sectionPadY));

    faderArea = r;
    faderSection.setVisible (shown (Section::fader));
    faderSection.setBounds (faderArea);
}

void ChannelStrip::paintSectionHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title,
                                       const juce::String& tag, juce::Colour tagColour) const
{
    auto& theme = themeManager.getTheme();
    auto row = area.reduced (padX, sectionPadY).removeFromTop (labelHeight);
    const auto labelStyle = TypeStyle { 8.5f, false, 600, true, 0.5f };

    // The badge first, so a narrow strip's title never runs under it.
    if (tag.isNotEmpty())
    {
        const auto width = juce::GlyphArrangement::getStringWidthInt (themeManager.font (theme.micro), theme.micro.apply (tag)) + 2 * badgePadX;
        auto badge = row.removeFromRight (width);
        g.setColour (tagColour.withAlpha (chipTintAlpha));
        g.fillRoundedRectangle (badge.toFloat(), theme.radiusSm);
        drawStyledText (g, themeManager, tag, theme.micro, badge, juce::Justification::centred, tagColour);
        row.removeFromRight (badgeGap);
    }

    drawStyledText (g, themeManager, title, labelStyle, row, juce::Justification::centredLeft, theme.textDim);
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = theme.radiusLg;

    g.setColour (state.strip.selected ? theme.bgElevated : theme.bgTrack);
    g.fillRoundedRectangle (bounds, radius);

    if (state.isBus())
    {
        g.setColour (colour.withAlpha (busTintAlpha));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (colour.withAlpha (busOutlineAlpha));
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    // Head: colour bar, number in the track colour, name (ellipsis; the tooltip has it all).
    paintColourBar (g, bounds, radius, colour);

    auto head = headArea.withTrimmedTop (colourBarHeight).reduced (padX, sectionPadY);

    if (state.isBus())
    {
        drawIcon (g, Icon::gitMerge, head.removeFromLeft (headIconSize).toFloat().withSizeKeepingCentre ((float) headIconSize, (float) headIconSize), colour);
    }
    else
    {
        const auto number = state.isReturn() ? state.strip.returnLetter : twoDigits (state.strip.number);
        const auto numberFont = themeManager.numberFont (TypeStyle { 10.0f, true, 600 });
        g.setFont (numberFont);
        g.setColour (colour);
        g.drawText (number, head.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (numberFont, number)),
                    juce::Justification::centredLeft, false);
    }

    head.removeFromLeft (7);
    drawStyledText (g, themeManager, state.strip.name, TypeStyle { 12.0f, false, 600 }, head,
                    juce::Justification::centredLeft, theme.textPrimary);

    auto divider = [&] (juce::Rectangle<int> area)
    {
        if (! area.isEmpty())
        {
            g.setColour (theme.borderSoft);
            g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);
        }
    };

    // I/O: the input select draws itself (a Bus shows its input chip); the output is read-only for now.
    if (! ioArea.isEmpty())
    {
        divider (ioArea);
        paintSectionHeader (g, ioArea, TRANS ("I/O"), {}, {});
        auto rows = ioArea.reduced (padX, sectionPadY).withTrimmedTop (labelHeight + rowGap);

        if (state.isBus())
        {
            auto chip = rows.withHeight (selectHeight);
            g.setColour (colour.withAlpha (chipTintAlpha));
            g.fillRoundedRectangle (chip.toFloat(), theme.radiusMd);
            const auto count = state.strip.childCount;
            drawStyledText (g, themeManager, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 "))
                                                 + trPlural (count, "%1 track", "%1 tracks"),
                            theme.bodySm, chip.reduced (chipPadX, 0), juce::Justification::centredLeft, colour);
        }

        auto out = rows.withTrimmedTop (selectHeight + rowGap).withHeight (selectHeight);
        g.setColour (theme.bgSlot);
        g.fillRoundedRectangle (out.toFloat(), theme.radiusMd);
        drawStyledText (g, themeManager, juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92 ")) + state.strip.output, theme.bodySm,
                        out.reduced (7, 0), juce::Justification::centredLeft, theme.textPrimary);
    }

    // Track chain: a read-only summary of the device chain; click opens it.
    if (! chainArea.isEmpty())
    {
        divider (chainArea);
        paintSectionHeader (g, chainArea, TRANS ("Track chain"), TRANS ("Racks"), theme.textSecondary);
        const auto hovered = chainLink.contains (getMouseXYRelative()) && isMouseOver (true);
        g.setColour (hovered ? theme.bgHover : theme.bgSlot);
        g.fillRoundedRectangle (chainLink.toFloat(), theme.radiusMd);
        g.setColour (theme.borderSoft);
        g.drawRoundedRectangle (chainLink.toFloat().reduced (0.5f), theme.radiusMd, 1.0f);

        auto link = chainLink.reduced (6, 0);
        g.setColour (colour);
        g.fillRoundedRectangle (link.removeFromLeft (2).withSizeKeepingCentre (2, 12).toFloat(), 1.0f);
        link.removeFromLeft (5);
        drawIcon (g, Icon::layers, link.removeFromLeft (10).toFloat().withSizeKeepingCentre (10.0f, 10.0f), theme.textSecondary);
        drawIcon (g, Icon::arrowUpRight, link.removeFromRight (10).toFloat().withSizeKeepingCentre (10.0f, 10.0f), theme.textDim);
        link.reduce (5, 0);
        drawStyledText (g, themeManager, chainSummary(), TypeStyle { 9.5f, false, 400 }, link, juce::Justification::centredLeft,
                        state.strip.deviceChain.empty() ? theme.textDim : theme.textPrimary);
    }

    if (! flowArea.isEmpty())
        drawIcon (g, Icon::arrowDown, flowArea.toFloat().withSizeKeepingCentre (9.0f, 9.0f), theme.textDim);

    if (! insertsArea.isEmpty())
        paintSectionHeader (g, insertsArea, state.isBus() ? TRANS ("Inserts") : TRANS ("Mixer inserts"), TRANS ("Post"), theme.accent);

    if (! sendsArea.isEmpty())
    {
        divider (sendsArea);
        paintSectionHeader (g, sendsArea, TRANS ("Sends"), {}, {});
    }

    divider (panArea);

    if (shown (Section::fader))
        divider (faderArea);
}

int ChannelStrip::flowStageAt (juce::Point<int> p) const
{
    if (chainArea.contains (p) || flowArea.contains (p))  return trackChainStage;
    if (insertsArea.contains (p))                          return insertsStage;
    if (sendsArea.contains (p))                            return sendsStage;
    if (panArea.contains (p) || faderArea.contains (p))    return faderStage;
    return -1;
}

void ChannelStrip::mouseMove (const juce::MouseEvent& e)
{
    repaint (chainLink);

    if (onFlowStageHovered)
        onFlowStageHovered (flowStageAt (e.getEventRelativeTo (this).getPosition()));
}

void ChannelStrip::mouseExit (const juce::MouseEvent&)
{
    repaint (chainLink);

    if (onFlowStageHovered)
        onFlowStageHovered (-1);
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (chainLink.contains (e.getPosition()))
    {
        if (onTrackChainClicked)
            onTrackChainClicked();

        return;
    }

    // Selecting a strip selects its track in every view (PRD §16.1).
    commands.invoke (cmd::trackSelect, { state.strip.id });

    if (e.mods.isPopupMenu() && onShowMenu)
        onShowMenu();
}

} // namespace resamper
