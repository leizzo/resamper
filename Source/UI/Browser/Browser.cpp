#include "Browser.h"
#include "Commands/CommandRegistry.h"
#include "Commands/PluginCommands.h"
#include "Engine/SamplePreview.h"

namespace resamper
{

namespace
{
    constexpr LibraryCategory categories[] = { LibraryCategory::sounds, LibraryCategory::drums, LibraryCategory::instruments,
                                               LibraryCategory::audioEffects, LibraryCategory::midiEffects,
                                               LibraryCategory::plugins, LibraryCategory::clips, LibraryCategory::samples };

    constexpr int categoryRowHeight = 30, itemRowHeight = 26, headerHeight = 26;

    const juce::String chevron (juce::CharPointer_UTF8 ("  \xe2\x80\xba  "));

    /** Hovering a sample this long starts its preview. */
    constexpr int hoverPreviewMs = 250;

    /** A list row: its text, icon and padding; a plug-in row's format badge (as
        on DeviceCard/Plugin) and a failed row's Retry. */
    const TypeStyle itemStyle { 12.0f, false, 400 }, badgeStyle { 7.5f, true, 600 }, noteStyle { 10.0f, false, 400 },
                    retryStyle { 10.5f, false, 600 };
    constexpr int rowPadding = 8, rowIconSize = 14, rowIconGap = 9, rowGap = 6;
    constexpr int badgePadding = 4, badgeBorder = 2, badgeHeight = 11, retryWidth = 40, retryHeight = 18;
    constexpr float rowRadius = 5.0f;

    /** Where a failed plug-in's Retry sits in its row. */
    juce::Rectangle<int> retryBounds (int width, int height)
    {
        return juce::Rectangle<int> (width, height).reduced (rowPadding, 0).removeFromRight (retryWidth)
                                                   .withSizeKeepingCentre (retryWidth, retryHeight);
    }

    /** The design's category icons, its phosphor glyphs as their lucide equivalents (PRD §15.8). */
    Icon iconFor (LibraryCategory c)
    {
        switch (c)
        {
            case LibraryCategory::sounds:        return Icon::volume2;
            case LibraryCategory::drums:         return Icon::drum;
            case LibraryCategory::instruments:   return Icon::piano;
            case LibraryCategory::audioEffects:  return Icon::slidersVertical;
            case LibraryCategory::midiEffects:   return Icon::audioWaveform;
            case LibraryCategory::plugins:       return Icon::puzzle;
            case LibraryCategory::clips:         return Icon::music;
            case LibraryCategory::samples:       return Icon::audioWaveform;
        }

        return Icon::file;
    }
}

Browser::Browser (CommandRegistry& c, const PluginRack& r, const ApplicationModel& m, ThemeManager& tm, SamplePreview& p, juce::File root)
    : commands (c), rack (r), model (m), themeManager (tm), preview (p),
      library ([&r] { return r.getCatalogue(); }, std::move (root)),
      scan (tm, "Scan", Button::Variant::ghost)
{
    setWantsKeyboardFocus (false);

    auto& theme = themeManager.getTheme();
    search.setTextToShowWhenEmpty ("Search Library", theme.textDim);
    search.setFont (themeManager.font (TypeStyle { 12.0f, false, 400 }));
    search.setIndents (28, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.onTextChange = [this] { folder = juce::File(); refresh(); };
    search.onEscapeKey = [this] { search.clear(); refresh(); };

    scan.setTooltip ("Scan for VST3 / AU plug-ins, in the background");
    scan.onClick = [this] { commands.invoke (cmd::pluginScan); };

    list.setRowHeight (itemRowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    list.addMouseListener (this, true);

    addAndMakeVisible (search);
    addAndMakeVisible (list);
    addChildComponent (scan);

    refresh();
    startTimerHz (4);   // picks up a finished plug-in scan
}

Browser::~Browser()
{
    preview.stop();
}

void Browser::refresh()
{
    items = library.list (category, folder, search.getText());
    catalogueSize = rack.getCatalogue().size();
    scan.setVisible (category == LibraryCategory::plugins);
    list.updateContent();
    list.deselectAllRows();
    repaint();
}

void Browser::selectCategory (LibraryCategory c)
{
    category = c;
    folder = juce::File();
    refresh();
}

void Browser::open (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return;

    auto& item = items[(size_t) row];

    if (item.kind == LibraryItem::Kind::folder)
    {
        folder = item.file;
        search.clear();
        refresh();
    }
    else if (item.kind == LibraryItem::Kind::plugin && ! item.failedScan)
    {
        const auto trackId = model.getSelectedTrackId();

        if (trackId.isEmpty())
            return;

        if (onInsertDevice != nullptr)
            onInsertDevice (trackId, item.pluginPath);
        else
            commands.invoke (cmd::pluginInsert, { trackId, item.pluginPath });
    }
    else
    {
        previewRow (row);
    }
}

void Browser::previewRow (int row)
{
    if (juce::isPositiveAndBelow (row, (int) items.size()) && items[(size_t) row].kind == LibraryItem::Kind::audioFile)
        preview.play (items[(size_t) row].file);
}

juce::String Browser::breadcrumb() const
{
    auto text = Library::nameOf (category);

    if (search.getText().trim().isNotEmpty())
        return text + chevron + "\"" + search.getText().trim() + "\"";

    if (folder.isDirectory())
    {
        const auto base = library.folderFor (category);
        juce::StringArray parts;

        for (auto f = folder; f != base && f.isAChildOf (base); f = f.getParentDirectory())
            parts.insert (0, f.getFileName());

        for (auto& part : parts)
            text << chevron << part;
    }

    return text;
}

juce::Rectangle<int> Browser::categoryBounds (int index) const
{
    return categoryArea.withHeight (categoryRowHeight).translated (0, index * categoryRowHeight);
}

int Browser::categoryAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) std::size (categories); ++i)
        if (categoryBounds (i).contains (p))
            return i;

    return -1;
}

void Browser::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.bgPanel);
    g.setColour (theme.borderSoft);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    // Search well.
    const auto field = search.getBounds().toFloat();
    g.setColour (theme.bgSlot);
    g.fillRoundedRectangle (field, 7.0f);
    g.setColour (search.hasKeyboardFocus (true) ? theme.focusRing : theme.border);
    g.drawRoundedRectangle (field.reduced (0.5f), 7.0f, 1.0f);
    drawIcon (g, Icon::search, field.withWidth (28.0f).withSizeKeepingCentre (14.0f, 14.0f).translated (3.0f, 0.0f),
              theme.textDim);

    // Categories.
    for (int i = 0; i < (int) std::size (categories); ++i)
    {
        auto row = categoryBounds (i);
        const auto selected = categories[i] == category;

        if (selected || i == hoveredCategory)
        {
            g.setColour (selected ? theme.bgElevated : theme.bgHover);
            g.fillRoundedRectangle (row.toFloat(), theme.radiusLg);
        }

        auto content = row.reduced (10, 0);
        drawIcon (g, iconFor (categories[i]), content.removeFromLeft (15).toFloat().withSizeKeepingCentre (15.0f, 15.0f),
                  selected ? theme.accent : theme.textSecondary);
        content.removeFromLeft (10);
        g.setColour (selected ? theme.textPrimary : theme.textSecondary);
        g.setFont (themeManager.font (TypeStyle { 12.5f, false, selected ? 600 : 400 }));
        g.drawText (Library::nameOf (categories[i]), content, juce::Justification::centredLeft, true);
    }

    // Divider and the list header (a breadcrumb; click to go up a folder).
    g.setColour (theme.borderSoft);
    g.fillRect (0, headerArea.getY() - 1, getWidth(), 1);
    drawStyledText (g, themeManager, breadcrumb(), TypeStyle { 10.0f, false, 600, true, 0.0f },
                    headerArea.reduced (16, 0).withTrimmedRight (scan.isVisible() ? scan.getWidth() : 0),
                    juce::Justification::centredLeft, theme.textDim);

    if (items.empty())
        drawStyledText (g, themeManager,
                        Library::isFileCategory (category) ? "Drop audio files into " + library.folderFor (category).getFullPathName()
                                                           : juce::String ("Nothing here"),
                        theme.bodySm, list.getBounds().reduced (16, 8).withHeight (40), juce::Justification::topLeft,
                        theme.textDim);
}

void Browser::resized()
{
    auto r = getLocalBounds().withTrimmedRight (1);
    auto top = r.removeFromTop (12 + 32 + 12).reduced (14, 12);
    search.setBounds (top);

    categoryArea = r.removeFromTop (4 + (int) std::size (categories) * categoryRowHeight + 4).reduced (8, 4);
    r.removeFromTop (1);
    r.reduce (0, 8);
    headerArea = r.removeFromTop (headerHeight);
    scan.setBounds (headerArea.removeFromRight (scan.getIdealWidth() + 8).reduced (0, 2).translated (-8, 0));
    list.setBounds (r.reduced (8, 0));
}

void Browser::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != this)
        return;

    if (auto index = categoryAt (e.getPosition()); index >= 0)
    {
        selectCategory (categories[index]);
        return;
    }

    // The header goes up one folder.
    if (headerArea.contains (e.getPosition()) && folder.isDirectory())
    {
        const auto parent = folder.getParentDirectory();
        folder = parent == library.folderFor (category) ? juce::File() : parent;
        refresh();
    }
}

void Browser::mouseMove (const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
    {
        if (auto index = categoryAt (e.getPosition()); index != hoveredCategory)
        {
            hoveredCategory = index;
            repaint (categoryArea);
        }

        return;
    }

    const auto row = list.getRowContainingPosition (e.getEventRelativeTo (&list).x, e.getEventRelativeTo (&list).y);

    if (row != hoveredRow)
    {
        hoveredRow = row;
        list.repaint();
        preview.stop();
        pendingPreview = juce::isPositiveAndBelow (row, (int) items.size()) && items[(size_t) row].kind == LibraryItem::Kind::audioFile
                           ? items[(size_t) row].file : juce::File();
        startTimer (hoverPreviewMs);
    }
}

void Browser::mouseExit (const juce::MouseEvent&)
{
    hoveredCategory = hoveredRow = -1;
    pendingPreview = juce::File();
    preview.stop();
    repaint();
}

bool Browser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::rightKey)
    {
        previewRow (list.getSelectedRow());
        return true;
    }

    return false;
}

void Browser::timerCallback()
{
    if (pendingPreview != juce::File())
    {
        preview.play (pendingPreview);
        pendingPreview = juce::File();
    }

    // A finished scan can change rows without changing their number (a retry that worked).
    const auto scanning = rack.isScanning();

    if (! scanning && (wasScanning || rack.getCatalogue().size() != catalogueSize) && ! Library::isFileCategory (category))
        refresh();

    wasScanning = scanning;

    startTimerHz (4);
}

void Browser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return;

    auto& theme = themeManager.getTheme();
    auto& item = items[(size_t) row];
    auto area = juce::Rectangle<int> (width, height);

    if (selected || row == hoveredRow)
    {
        g.setColour (selected ? theme.bgElevated : theme.bgHover);
        g.fillRoundedRectangle (area.toFloat(), rowRadius);
    }

    auto content = area.reduced (rowPadding, 0);

    // A plug-in has the plug icon and a format badge, so it reads apart from a native device (§6.2).
    const auto icon = item.kind == LibraryItem::Kind::folder ? Icon::folder
                    : item.isPlugin()                        ? Icon::plug
                    : item.kind == LibraryItem::Kind::plugin ? (item.instrument ? Icon::music : Icon::layers)
                                                             : Icon::audioLines;
    const auto iconColour = item.failedScan                         ? theme.textDim
                          : item.kind == LibraryItem::Kind::plugin ? theme.accentDim
                          : item.kind == LibraryItem::Kind::folder ? theme.textSecondary : theme.textDim;
    drawIcon (g, icon, content.removeFromLeft (rowIconSize).toFloat().withSizeKeepingCentre ((float) rowIconSize, (float) rowIconSize),
              iconColour);
    content.removeFromLeft (rowIconGap);

    if (item.failedScan)
    {
        // Listed dim, with Retry: it can't be inserted (§21).
        const auto retry = retryBounds (width, height);
        g.setColour (theme.border);
        g.drawRoundedRectangle (retry.toFloat().reduced (0.5f), theme.radiusSm, 1.0f);
        drawStyledText (g, themeManager, "Retry", retryStyle, retry, juce::Justification::centred,
                        row == hoveredRow ? theme.accent : theme.textSecondary);
        content.setRight (retry.getX() - rowGap);

        const auto note = juce::String ("Failed to scan");
        const auto noteWidth = juce::GlyphArrangement::getStringWidthInt (themeManager.font (noteStyle), note);
        drawStyledText (g, themeManager, note, noteStyle, content.removeFromRight (noteWidth), juce::Justification::centredRight,
                        theme.textDim);
        content.removeFromRight (rowGap);
    }
    else if (item.isPlugin())
    {
        const auto badgeText = item.formatBadge();
        const auto badgeWidth = juce::GlyphArrangement::getStringWidthInt (themeManager.font (badgeStyle), badgeText) + 2 * badgePadding + badgeBorder;
        const auto badge = content.removeFromRight (badgeWidth).withSizeKeepingCentre (badgeWidth, badgeHeight);
        g.setColour (theme.border);
        g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), theme.radiusSm, 1.0f);
        drawNumber (g, themeManager, badgeText, badgeStyle, badge, juce::Justification::centred, theme.textSecondary);
        content.removeFromRight (rowGap);
    }

    g.setColour (item.failedScan ? theme.textDim
                                 : item.kind == LibraryItem::Kind::audioFile ? theme.textSecondary : theme.textPrimary);
    g.setFont (themeManager.font (itemStyle));
    g.drawText (item.name, content, juce::Justification::centredLeft, true);
}

juce::String Browser::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return {};

    const auto& item = items[(size_t) row];

    if (item.failedScan)
        return item.formatBadge() + " plug-in that failed to scan: it crashed or timed out. Retry scans it again.";

    return item.isPlugin() ? item.formatBadge() + " plug-in" : juce::String();
}

void Browser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) items.size()))
        return;

    const auto& item = items[(size_t) row];

    if (item.kind == LibraryItem::Kind::folder)
        open (row);
    else if (item.failedScan && retryBounds (list.getVisibleRowWidth(), list.getRowHeight()).contains (e.getPosition()))
        commands.invoke (cmd::pluginRetryScan, { item.pluginPath });
}

void Browser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    open (row);
}

juce::var Browser::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], (int) items.size()))
        return {};

    auto& item = items[(size_t) rows[0]];
    return item.kind == LibraryItem::Kind::folder || item.failedScan ? juce::var() : dragDescription (item);
}

} // namespace resamper
