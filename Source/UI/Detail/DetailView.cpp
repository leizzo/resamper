#include "DetailView.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"
#include "Commands/TrackCommands.h"
#include "UI/Browser/Library.h"
#include "UI/Controls/ValueFormat.h"

#include <map>

namespace resamper
{

namespace
{
    constexpr int clipPanelWidth = 230, resizeEdge = 4, chainPadding = 14, cardGap = 10;

    constexpr int dropZoneWidth = 150;

    /** The end of the chain (§6.3), drawn to the design's Drop Zone: a bordered
        150 px column, a square-dashed icon, "Drop device" over "or plug-in here".
        Paints only; the chain takes the drop. */
    struct DropZone : juce::Component
    {
        explicit DropZone (ThemeManager& tm) : themeManager (tm)
        {
            setComponentID ("dropZone");
            setInterceptsMouseClicks (false, false);
        }

        void paint (juce::Graphics& g) override
        {
            auto& theme = themeManager.getTheme();
            g.setColour (highlighted ? theme.accentDim : theme.border);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), theme.radiusXl, 1.0f);

            // 18 px icon, 6, a 12 px line, 6, an 11 px line: centred as a group.
            constexpr int iconSize = 18, gap = 6, firstLine = 12, secondLine = 11;
            auto column = getLocalBounds().withSizeKeepingCentre (getWidth(), iconSize + gap + firstLine + gap + secondLine);
            drawIcon (g, Icon::squareDashed, column.removeFromTop (iconSize).toFloat().withSizeKeepingCentre ((float) iconSize, (float) iconSize),
                      highlighted ? theme.accent : theme.textDim);
            column.removeFromTop (gap);
            drawStyledText (g, themeManager, "Drop device", TypeStyle { 10.0f, false, 600 }, column.removeFromTop (firstLine),
                            juce::Justification::centred, theme.textSecondary);
            column.removeFromTop (gap);
            drawStyledText (g, themeManager, "or plug-in here", TypeStyle { 9.0f, false, 400 }, column.removeFromTop (secondLine),
                            juce::Justification::centred, theme.textDim);
        }

        ThemeManager& themeManager;
        bool highlighted = false;
    };

    juce::String barsText (const ApplicationModel& model, double startSeconds, double lengthSeconds)
    {
        const auto beats = model.secondsToBeats (startSeconds + lengthSeconds) - model.secondsToBeats (startSeconds);
        const auto perBar = model.getBeatsPerBar (startSeconds);
        const auto bars = beats / perBar;
        return juce::String (bars, std::abs (bars - std::round (bars)) < 0.01 ? 0 : 2) + (std::abs (bars - 1.0) < 0.01 ? " bar" : " bars");
    }

    juce::String positionText (const ApplicationModel& model, double seconds)
    {
        const auto p = model.toBarsBeats (seconds);
        return juce::String (p.bar) + "." + juce::String (p.beat) + "." + juce::String (p.sixteenth);
    }
}

//==============================================================================
/** The selected clip's (or, with none, the selected track's) name, colour and key properties. */
struct DetailView::ClipPanel : juce::Component
{
    explicit ClipPanel (ThemeManager& tm) : themeManager (tm) {}

    struct Row { juce::String key, value; bool accent = false; };

    juce::String title, subtitle;
    juce::Colour colour;
    std::vector<Row> rows;

    void paint (juce::Graphics& g) override
    {
        auto& theme = themeManager.getTheme();
        g.fillAll (theme.bgTrack);
        g.setColour (theme.borderSoft);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());

        auto r = getLocalBounds().reduced (16);

        if (title.isEmpty())
        {
            drawStyledText (g, themeManager, "Select a track or clip", theme.body, r.removeFromTop (20),
                            juce::Justification::centredLeft, theme.textDim);
            return;
        }

        auto header = r.removeFromTop (18);
        g.setColour (colour);
        g.fillRoundedRectangle (header.removeFromLeft (4).withSizeKeepingCentre (4, 16).toFloat(), 2.0f);
        header.removeFromLeft (8);
        drawStyledText (g, themeManager, title, TypeStyle { 13.0f, false, 700 }, header, juce::Justification::centredLeft,
                        theme.textPrimary);

        r.removeFromTop (8);
        drawStyledText (g, themeManager, subtitle, TypeStyle { 10.0f, false, 500, true, 0.0f }, r.removeFromTop (14),
                        juce::Justification::centredLeft, theme.textDim);
        r.removeFromTop (12);

        for (auto& row : rows)
        {
            if (r.getHeight() < 28)
                break;

            auto box = r.removeFromTop (28);
            r.removeFromTop (7);
            g.setColour (theme.bgSlot);
            g.fillRoundedRectangle (box.toFloat(), theme.radiusLg);
            box.reduce (11, 0);
            drawStyledText (g, themeManager, row.key, TypeStyle { 11.0f, false, 400 }, box, juce::Justification::centredLeft,
                            theme.textSecondary);
            drawNumber (g, themeManager, row.value, TypeStyle { 11.0f, true, 400 }, box, juce::Justification::centredRight,
                        row.accent ? theme.accent : theme.textPrimary);
        }
    }

    ThemeManager& themeManager;
};

//==============================================================================
/** The row of DeviceCards; the drop target for Browser devices and card moves. */
struct DetailView::Chain : juce::Component,
                           juce::DragAndDropTarget
{
    Chain (DetailView& o) : dropZone (o.themeManager), owner (o)
    {
        addChildComponent (dropZone);
    }

    juce::String trackId;
    std::vector<std::unique_ptr<DeviceCard>> cards;
    DropZone dropZone;
    int dropIndex = -1;

    DeviceCard* findCard (const juce::String& pluginId) const
    {
        for (auto& card : cards)
            if (card->getPlugin().id == pluginId)
                return card.get();

        return nullptr;
    }

    void setChain (const juce::String& track, const std::vector<PluginInfo>& plugins)
    {
        auto same = track == trackId && plugins.size() == cards.size();

        for (size_t i = 0; same && i < plugins.size(); ++i)
            same = cards[i]->getPlugin().id == plugins[i].id;

        if (! same)
        {
            std::map<juce::String, std::unique_ptr<DeviceCard>> old;

            for (auto& card : cards)
                old[card->getPlugin().id] = std::move (card);

            cards.clear();
            trackId = track;

            for (auto& plugin : plugins)
            {
                if (auto kept = old.find (plugin.id); kept != old.end())
                {
                    cards.push_back (std::move (kept->second));
                    continue;
                }

                auto card = DeviceCard::create (owner.commands, owner.rack, owner.hosting, owner.themeManager, track, plugin);
                card->onSizeChange = [this, id = plugin.id] (DeviceSize size)
                {
                    owner.commands.invoke (cmd::pluginSetSize, { id, size });
                    owner.refresh();
                };
                card->onFloat = card->onOpenEditor = [this, id = plugin.id] { if (owner.onOpenEditor) owner.onOpenEditor (id); };
                addAndMakeVisible (*card);
                cards.push_back (std::move (card));
            }
        }

        for (size_t i = 0; i < plugins.size(); ++i)
        {
            cards[i]->setState (plugins[i]);
            cards[i]->setWindowOpen (owner.openWindowIds.contains (plugins[i].id));
        }

        dropZone.setVisible (trackId.isNotEmpty());

        layout();
    }

    void layout()
    {
        auto x = chainPadding;
        const auto height = juce::jmin (DeviceCard::height, juce::jmax (0, getParentHeight() - 2 * chainPadding));
        const auto docked = juce::jmax (0, getParentWidth() - 2 * chainPadding);

        for (auto& card : cards)
        {
            card->setBounds (x, chainPadding, card->getPreferredWidth (docked), height);
            x += card->getWidth() + cardGap;
        }

        dropZone.setBounds (x, chainPadding, dropZoneWidth, height);
        x += dropZoneWidth;

        setSize (juce::jmax (getParentWidth(), x + chainPadding), juce::jmax (0, getParentHeight()));
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (dropIndex < 0)
            return;

        const auto x = dropIndex < (int) cards.size() ? cards[(size_t) dropIndex]->getX() - cardGap / 2
                     : cards.empty()                  ? chainPadding
                                                      : cards.back()->getRight() + cardGap / 2;
        g.setColour (owner.themeManager.getTheme().accentDim);
        g.fillRect (x - 1, chainPadding, 2, getHeight() - 2 * chainPadding);
    }

    int indexAt (int x) const
    {
        for (size_t i = 0; i < cards.size(); ++i)
            if (x < cards[i]->getBounds().getCentreX())
                return (int) i;

        return (int) cards.size();
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return trackId.isNotEmpty()
            && (d.description.hasProperty ("deviceCard")
                || (itemFromDrag (d.description) && itemFromDrag (d.description)->kind == LibraryItem::Kind::plugin));
    }

    void itemDragMove (const SourceDetails& d) override
    {
        dropIndex = indexAt (d.localPosition.x);
        dropZone.highlighted = dropIndex == (int) cards.size();
        dropZone.repaint();
        const auto inView = owner.chainView.getLocalPoint (this, d.localPosition);
        owner.chainView.autoScroll (inView.x, inView.y, 30, 16);
        repaint();
    }
    void itemDragExit (const SourceDetails&) override
    {
        dropIndex = -1;
        dropZone.highlighted = false;
        repaint();
    }

    void itemDropped (const SourceDetails& d) override
    {
        const auto index = indexAt (d.localPosition.x);
        dropIndex = -1;
        dropZone.highlighted = false;
        repaint();

        if (auto moved = d.description["deviceCard"].toString(); moved.isNotEmpty())
        {
            int from = -1;

            for (size_t i = 0; i < cards.size(); ++i)
                if (cards[i]->getPlugin().id == moved)
                    from = (int) i;

            // Dropping after itself: the gap it leaves shifts the target left.
            const auto to = from >= 0 && index > from ? index - 1 : index;

            if (from >= 0 && to != from)
                owner.commands.invoke (cmd::pluginMove, { trackId, moved, juce::jmin (to, (int) cards.size() - 1) });

            return;
        }

        if (auto item = itemFromDrag (d.description))
            owner.insertDevice (trackId, item->pluginPath);
    }

    DetailView& owner;
};

//==============================================================================
DetailView::DetailView (ApplicationModel& m, PluginRack& r, PluginHosting& h, CommandRegistry& c, ThemeManager& tm, ShellState& s,
                        juce::ValueTree uiState)
    : model (m), rack (r), hosting (h), commands (c), themeManager (tm), shell (s), state (std::move (uiState)),
      clipPanel (std::make_unique<ClipPanel> (tm)), chain (std::make_unique<Chain> (*this))
{
    chainView.setViewedComponent (chain.get(), false);
    chainView.setScrollBarsShown (false, true);
    chainView.setScrollBarThickness (6);

    addAndMakeVisible (*clipPanel);
    addAndMakeVisible (chainView);

    model.addListener (this);
    refresh();
}

DetailView::~DetailView()
{
    model.removeListener (this);
}

void DetailView::setInspector (juce::Component* c)
{
    if (inspector != nullptr)
        removeChildComponent (inspector);

    inspector = c;

    if (inspector != nullptr)
        addAndMakeVisible (inspector);

    clipPanel->setVisible (inspector == nullptr);
    chainView.setVisible (inspector == nullptr);
    resized();
}

void DetailView::setOpenWindows (const juce::StringArray& pluginIds)
{
    openWindowIds = pluginIds;

    for (auto& card : chain->cards)
        card->setWindowOpen (openWindowIds.contains (card->getPlugin().id));
}

void DetailView::insertDevice (const juce::String& trackId, const juce::String& path)
{
    const auto before = rack.getChain (trackId, PluginChain::device);

    if (! commands.invoke (cmd::pluginInsert, { trackId, path, PluginChain::device }))
        return;

    const auto after = rack.getChain (trackId, PluginChain::device);

    if (after.size() <= before.size())
        return;

    const auto& added = after.back();

    // A plug-in's window opens by the opening rule (pluginAdded), whichever view inserted it.
    if (added.external)
        return;

    // The chain shown is the selected track's: show this one, then focus the new card.
    if (model.getSelectedTrackId() != trackId)
        commands.invoke (cmd::trackSelect, { trackId });

    refresh();

    if (auto* card = chain->findCard (added.id))
        card->focusFirstControl();
}

void DetailView::revealDeviceChain()
{
    chainView.setViewPosition (0, 0);
}

void DetailView::refresh()
{
    const auto trackId = model.getSelectedTrackId();
    const auto clipId = model.getSelectedClipId();
    auto& theme = themeManager.getTheme();

    clipPanel->title = {};
    clipPanel->rows.clear();

    for (auto& track : model.getTracks())
    {
        if (track.id != trackId)
            continue;

        clipPanel->colour = theme.trackColour (track.colourIndex);
        const auto midi = track.kind == TrackKind::midi;

        for (auto& clip : track.clips)
        {
            if (clip.id != clipId)
                continue;

            clipPanel->title = clip.name;
            clipPanel->subtitle = (midi ? "MIDI clip" : "Audio clip") + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "))
                                + barsText (model, clip.startSeconds, clip.lengthSeconds)
                                + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + juce::String (model.getTempo(), 0) + " BPM";
            clipPanel->rows = { { "Start", positionText (model, clip.startSeconds) },
                                { "Length", barsText (model, clip.startSeconds, clip.lengthSeconds) },
                                midi ? ClipPanel::Row { "Notes", juce::String ((int) clip.notes.size()) }
                                     : ClipPanel::Row { "Takes", clip.numTakes == 0 ? juce::String ("1") : juce::String (clip.numTakes) },
                                { "Track", track.name } };
        }

        if (clipPanel->title.isEmpty())
        {
            clipPanel->title = track.name;
            clipPanel->subtitle = midi ? "MIDI track" : "Audio track";
            clipPanel->rows = { { "Clips", juce::String ((int) track.clips.size()) },
                                { "Volume", ValueFormat::decibels().format (track.volume.value) },
                                { "Pan", ValueFormat::pan().format (track.pan) },
                                { "Armed", track.armed ? "On" : "Off", track.armed } };
        }
    }

    clipPanel->repaint();
    chain->setChain (trackId, trackId.isNotEmpty() ? rack.getChain (trackId, PluginChain::device) : std::vector<PluginInfo>());
}

void DetailView::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.bgPanel);
    g.setColour (theme.borderSoft);
    g.fillRect (0, 0, getWidth(), 1);
}

void DetailView::resized()
{
    // The top edge stays free for resizing.
    auto r = getLocalBounds().withTrimmedTop (resizeEdge);

    if (inspector != nullptr)
    {
        inspector->setBounds (r);
        return;
    }

    clipPanel->setBounds (r.removeFromLeft (clipPanelWidth));
    chainView.setBounds (r);
    chain->setSize (r.getWidth(), r.getHeight());
    chain->layout();
}

bool DetailView::onResizeEdge (juce::Point<int> p) const
{
    return p.y < resizeEdge;
}

void DetailView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (onResizeEdge (e.getPosition()) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void DetailView::mouseDown (const juce::MouseEvent& e)
{
    heightAtDragStart = onResizeEdge (e.getPosition()) ? getHeight() : 0;
}

void DetailView::mouseDrag (const juce::MouseEvent& e)
{
    if (heightAtDragStart > 0)
        shell.setDetailHeight (heightAtDragStart - e.getDistanceFromDragStartY());
}

} // namespace resamper
