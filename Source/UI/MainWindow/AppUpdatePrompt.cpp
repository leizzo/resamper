#include "AppUpdatePrompt.h"

#include "UI/Controls/Icons.h"
#include "UI/Theme/Interaction.h"

namespace resamper
{

namespace
{
    constexpr int pillHeight = 30;
    constexpr int pillGapBelowBar = 11;
    constexpr int hintWidth = 216;
    constexpr int dialogWidth = 520;
    constexpr int dialogGap = 18;
    constexpr int badgeSize = 38;
    constexpr int headerGap = 14;
    constexpr int titleGap = 3;
    constexpr int closeSize = 28;
    constexpr int footerHeight = 28;
    constexpr int footerPadRestart = 14;
    constexpr int itemTextGap = 3;
    constexpr int maxHintLines = 4;
    constexpr int semibold = 600;
    constexpr int bold = 700;
    constexpr float dialogTitleSize = 18.0f;
    constexpr float badgeIconSize = 20.0f;
    constexpr float footerIconSize = 13.0f;
    constexpr double percentScale = 100.0;

    constexpr const char* updateLabel = "Update";
    constexpr const char* updatedLabel = "Updated";
    constexpr const char* downloadingLabel = "Downloading";
    constexpr const char* dialogTitle = "Update complete";
    constexpr const char* dialogSuffix = " is ready to use";
    constexpr const char* changelogLabel = "CHANGELOG";
    constexpr const char* releaseNotesLabel = "Release notes";
    constexpr const char* restartLabel = "Restart";
    constexpr const char* installingLabel = "Installing...";
    constexpr const char* hintSuffix = " is ready";
    constexpr const char* productName = "RESAMPER ";

    void focusIfShowing (juce::Component& component)
    {
        if (component.isShowing())
            component.grabKeyboardFocus();
    }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }

    juce::TextLayout textLayout (const juce::Font& font, juce::Colour colour, const juce::String& text, int width)
    {
        juce::AttributedString content;
        content.setJustification (juce::Justification::topLeft);
        content.append (text, font, colour);
        juce::TextLayout layout;
        layout.createLayout (content, (float) juce::jmax (1, width));
        return layout;
    }

    int wrappedHeight (const juce::Font& font, const juce::String& text, int width)
    {
        if (text.isEmpty() || width <= 0)
            return 0;

        return juce::jmax ((int) std::ceil (font.getHeight()),
                           (int) std::ceil (textLayout (font, juce::Colours::transparentBlack, text, width).getHeight()));
    }

    void drawLines (juce::Graphics& g, const juce::Font& font, juce::Colour colour, const juce::String& text,
                    juce::Rectangle<int> area)
    {
        if (text.isEmpty() || area.isEmpty())
            return;

        textLayout (font, colour, text, area.getWidth()).draw (g, area.toFloat());
    }

    void paintAction (juce::Graphics& g, ThemeManager& tm, juce::Rectangle<int> area, const juce::String& text,
                      Icon icon, bool primary, bool hovered, bool down)
    {
        auto& theme = tm.getTheme();
        auto& metrics = tm.getMetrics();
        const auto r = area.toFloat();
        ControlState state;
        state.hovered = hovered;
        state.pressed = down;
        const auto colours = primary ? stateColours (theme, state, theme.accent, theme.accent, theme.textOnAccent, true)
                                     : stateColours (theme, state, theme.bgElevated, theme.accent, theme.textPrimary);

        g.setColour (colours.fill);
        g.fillRoundedRectangle (r, theme.radiusLg);
        g.setColour (primary ? colours.fill : theme.border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme.radiusLg, 1.0f);

        auto style = theme.body;
        style.weight = primary ? bold : semibold;
        const auto font = tm.font (style);
        const auto group = (int) footerIconSize + metrics.spaceMd + textWidth (font, text);
        auto row = area.withSizeKeepingCentre (group, area.getHeight());
        const auto glyph = row.removeFromLeft ((int) footerIconSize).toFloat()
                               .withSizeKeepingCentre (footerIconSize, footerIconSize);
        drawIcon (g, icon, glyph, colours.text);
        row.removeFromLeft (metrics.spaceMd);
        g.setColour (colours.text);
        g.setFont (font);
        g.drawFittedText (text, row, juce::Justification::centredLeft, 1, 1.0f);
    }
}

AppUpdatePrompt::Notes::Notes (ThemeManager& tm)
    : themeManager (tm)
{
    setOpaque (false);
}

void AppUpdatePrompt::Notes::setItems (std::vector<ReleaseNote> next)
{
    items = std::move (next);
    repaint();
}

int AppUpdatePrompt::Notes::preferredHeight (int width) const
{
    if (width <= 0 || items.empty())
        return 0;

    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    const auto titleFont = themeManager.font (theme.label);
    const auto bodyFont = themeManager.font (theme.bodySm);
    const auto textW = juce::jmax (1, width - metrics.spaceSm - metrics.spaceLg);
    auto height = 0;

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            height += metrics.spaceLg;

        auto row = wrappedHeight (titleFont, items[i].title, textW);

        if (items[i].description.isNotEmpty())
            row += itemTextGap + wrappedHeight (bodyFont, items[i].description, textW);

        height += juce::jmax (metrics.spaceSm, row);
    }

    return height;
}

void AppUpdatePrompt::Notes::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    const auto titleFont = themeManager.font (theme.label);
    const auto bodyFont = themeManager.font (theme.bodySm);
    const auto textW = juce::jmax (1, getWidth() - metrics.spaceSm - metrics.spaceLg);
    auto y = 0;

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i > 0)
            y += metrics.spaceLg;

        const auto titleH = wrappedHeight (titleFont, items[i].title, textW);
        const auto descH = items[i].description.isEmpty()
                               ? 0
                               : itemTextGap + wrappedHeight (bodyFont, items[i].description, textW);
        const auto rowH = juce::jmax (metrics.spaceSm, titleH + descH);
        const auto dot = (float) metrics.spaceSm;
        const auto dotY = (float) y + (titleFont.getHeight() - dot) / 2.0f;

        g.setColour (theme.accent);
        g.fillEllipse (0.0f, dotY, dot, dot);

        drawLines (g, titleFont, theme.textPrimary, items[i].title,
                   { metrics.spaceSm + metrics.spaceLg, y, textW, titleH });

        if (descH > 0)
            drawLines (g, bodyFont, theme.textSecondary, items[i].description,
                       { metrics.spaceSm + metrics.spaceLg, y + titleH + itemTextGap, textW, descH - itemTextGap });

        y += rowH;
    }
}

AppUpdatePrompt::AppUpdatePrompt (ThemeManager& tm)
    : themeManager (tm),
      noteList (tm)
{
    setWantsKeyboardFocus (true);
    setOpaque (false);
    addChildComponent (noteViewport);
    noteViewport.setViewedComponent (&noteList, false);
    noteViewport.setScrollBarsShown (true, false);
    noteViewport.setOpaque (false);
    setVisible (false);
}

AppUpdatePrompt::~AppUpdatePrompt()
{
    if (check != nullptr)
        check->setListener (nullptr);
}

void AppUpdatePrompt::bind (UpdateCheck& next)
{
    if (check != nullptr)
        check->setListener (nullptr);

    check = &next;
    next.setListener (this);
}

void AppUpdatePrompt::showOffer (const AppRelease& release, const juce::String& text, bool canReplace)
{
    phase = Phase::available;
    version = release.version;
    summary = text;
    page = release.tag.isNotEmpty() ? release.tag : release.version;
    replaceable = canReplace;
    modal = false;
    installing = false;
    welcomeClose = nullptr;
    welcomeRestart = nullptr;
    setVisible (true);
    resized();
    repaint();
}

void AppUpdatePrompt::showWelcome (const juce::String& next, std::vector<ReleaseNote> notes,
                                   std::function<void()> onClose, std::function<void()> onRestart)
{
    phase = Phase::welcomed;
    version = next;
    page = next;
    items = std::move (notes);
    noteList.setItems (items);
    welcomeClose = std::move (onClose);
    welcomeRestart = std::move (onRestart);
    modal = true;
    installing = false;
    setVisible (true);
    resized();
    repaint();
    focusIfShowing (*this);
}

bool AppUpdatePrompt::dismissModal()
{
    if (! modal)
        return false;

    closeModal();
    return true;
}

void AppUpdatePrompt::updateAvailable (const AppRelease& release, const juce::String& text, bool canReplace)
{
    showOffer (release, text, canReplace);
}

void AppUpdatePrompt::updateProgress (double next)
{
    if (phase == Phase::idle || phase == Phase::ready || phase == Phase::welcomed)
        return;

    const auto before = pillText();
    phase = Phase::downloading;
    fraction = next;

    if (pillText() != before)
        resized();

    repaint();
}

void AppUpdatePrompt::updateReady (const AppRelease& release, const std::vector<ReleaseNote>& notes)
{
    phase = Phase::ready;
    version = release.version;
    page = release.tag.isNotEmpty() ? release.tag : release.version;
    summary = {};
    items = notes;
    noteList.setItems (items);
    modal = true;
    installing = false;
    setVisible (true);
    resized();
    repaint();
    focusIfShowing (*this);
}

void AppUpdatePrompt::updateFailed (const juce::String& message)
{
    installing = false;

    if (phase == Phase::downloading)
        phase = Phase::available;

    if (onFailed != nullptr)
        onFailed (message);

    resized();
    repaint();
}

juce::String AppUpdatePrompt::pillText() const
{
    if (phase == Phase::downloading)
    {
        if (fraction > 0.0)
            return juce::String (downloadingLabel) + " "
                   + juce::String (juce::roundToInt (fraction * percentScale)) + "%";

        return downloadingLabel;
    }

    if (phase == Phase::ready)
        return updatedLabel;

    return updateLabel;
}

void AppUpdatePrompt::rebuildGeometry()
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    pill = {};
    hint = {};
    dialog = {};
    divider = {};
    closeButton = {};
    notesButton = {};
    restartButton = {};
    changelogHeading = {};
    changelog = {};

    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    const auto showPill = phase == Phase::available || phase == Phase::downloading || phase == Phase::ready;

    if (showPill)
    {
        const auto font = themeManager.font (theme.label);
        const auto width = metrics.spaceXl + metrics.iconSection + metrics.spaceMd
                           + textWidth (font, pillText()) + metrics.spaceXl;
        pill = { juce::jmax (metrics.space2xl, getWidth() - metrics.space2xl - width),
                 metrics.topBarHeight + pillGapBelowBar, width, pillHeight };
    }

    if ((phase == Phase::available || phase == Phase::downloading) && ! pill.isEmpty())
    {
        const auto titleFont = themeManager.font (TypeStyle { theme.body.size, false, semibold });
        const auto bodyFont = themeManager.font (theme.bodySm);
        const auto textW = hintWidth - 2 * metrics.spaceXl;
        const auto title = (version.startsWithChar ('v') ? version : "v" + version) + hintSuffix;
        auto height = metrics.spaceLg + juce::jmax ((int) std::ceil (titleFont.getHeight()),
                                                    wrappedHeight (titleFont, title, textW));

        if (summary.isNotEmpty())
        {
            const auto natural = wrappedHeight (bodyFont, summary, textW);
            const auto cap = (int) std::ceil (bodyFont.getHeight()) * maxHintLines;
            height += metrics.spaceXs + juce::jmin (natural, cap);
        }

        height += metrics.spaceLg;
        hint = { pill.getRight() - hintWidth, pill.getBottom() + metrics.spaceMd, hintWidth, height };

        if (hint.getX() < metrics.space2xl)
            hint.setX (metrics.space2xl);
    }

    if (! modal)
    {
        noteViewport.setVisible (false);
        return;
    }

    const auto pad = metrics.space3xl;
    const auto width = juce::jmin (dialogWidth, juce::jmax (0, getWidth() - 2 * metrics.space2xl));
    const auto inner = juce::jmax (1, width - 2 * pad);
    const auto labelFont = themeManager.font (TypeStyle { theme.bodySm.size, false, bold });
    const auto notesFont = themeManager.font (TypeStyle { theme.body.size, false, semibold });
    const auto restartFont = themeManager.font (TypeStyle { theme.body.size, false, bold });
    const auto labelH = (int) std::ceil (labelFont.getHeight());
    const auto notesNatural = noteList.preferredHeight (inner);
    const auto notesGap = notesNatural > 0 ? metrics.spaceLg : 0;
    const auto chrome = pad + badgeSize + dialogGap + 1 + dialogGap + labelH + notesGap
                        + dialogGap + footerHeight + pad;
    const auto maxH = juce::jmax (chrome, getHeight() - 2 * metrics.space2xl);
    const auto notesH = juce::jmin (notesNatural, juce::jmax (0, maxH - chrome));
    const auto height = chrome + notesH;

    dialog = { (getWidth() - width) / 2, juce::jmax (metrics.space2xl, (getHeight() - height) / 2), width, height };

    auto content = dialog.reduced (pad);
    content.removeFromTop (badgeSize);
    closeButton = { dialog.getRight() - pad - closeSize,
                    dialog.getY() + pad + (badgeSize - closeSize) / 2, closeSize, closeSize };
    content.removeFromTop (dialogGap);
    divider = content.removeFromTop (1);
    content.removeFromTop (dialogGap);
    changelogHeading = content.removeFromTop (labelH);

    if (notesGap > 0)
        content.removeFromTop (notesGap);

    changelog = content.removeFromTop (notesH);
    content.removeFromTop (dialogGap);
    auto footer = content.removeFromTop (footerHeight);
    const auto restartText = installing ? juce::String (installingLabel) : juce::String (restartLabel);
    const auto restartW = footerPadRestart + (int) footerIconSize + metrics.spaceMd
                          + textWidth (restartFont, restartText) + footerPadRestart;
    restartButton = footer.removeFromRight (restartW);
    footer.removeFromRight (metrics.spaceLg);
    const auto notesW = metrics.spaceXl + (int) footerIconSize + metrics.spaceMd
                        + textWidth (notesFont, releaseNotesLabel) + metrics.spaceXl;
    notesButton = footer.removeFromRight (notesW);

    noteViewport.setVisible (notesH > 0);
    noteViewport.setBounds (changelog);
    const auto viewW = notesH > 0 && notesNatural > notesH ? noteViewport.getMaximumVisibleWidth() : changelog.getWidth();
    noteList.setBounds (0, 0, juce::jmax (1, viewW), juce::jmax (notesNatural, notesH));
}

void AppUpdatePrompt::resized()
{
    rebuildGeometry();
}

void AppUpdatePrompt::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    const auto showPill = phase == Phase::available || phase == Phase::downloading || phase == Phase::ready;

    if (showPill && ! pill.isEmpty())
    {
        const auto ready = phase == Phase::ready;
        const auto r = pill.toFloat();
        paintElevation (g, theme.elevation2, r, theme.radiusXl);

        ControlState state;
        state.hovered = hot == Hot::pill;
        state.pressed = pressed && state.hovered;
        const auto colours = ready ? stateColours (theme, state, theme.bgElevated, theme.accent, theme.textPrimary)
                                   : stateColours (theme, state, theme.accent, theme.accent, theme.textOnAccent, true);

        g.setColour (colours.fill);
        g.fillRoundedRectangle (r, theme.radiusXl);
        g.setColour (ready ? theme.border : theme.accentDim);
        g.drawRoundedRectangle (r.reduced (0.5f), theme.radiusXl, 1.0f);

        const auto font = themeManager.font (theme.label);
        const auto label = pillText();
        const auto group = metrics.iconSection + metrics.spaceMd + textWidth (font, label);
        auto row = pill.withSizeKeepingCentre (group, pill.getHeight());
        const auto glyph = row.removeFromLeft (metrics.iconSection).toFloat()
                               .withSizeKeepingCentre ((float) metrics.iconSection, (float) metrics.iconSection);
        drawIcon (g, ready ? Icon::check : Icon::download, glyph, colours.text);
        row.removeFromLeft (metrics.spaceMd);
        g.setColour (colours.text);
        g.setFont (font);
        g.drawFittedText (label, row, juce::Justification::centredLeft, 1, 1.0f);

        if (! modal && hasKeyboardFocus (true))
            paintFocusRing (g, theme, r, theme.radiusXl);
    }

    if (! hint.isEmpty() && (phase == Phase::available || phase == Phase::downloading))
    {
        const auto r = hint.toFloat();
        g.setColour (theme.bgElevated);
        g.fillRoundedRectangle (r, theme.radiusLg);
        g.setColour (theme.border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme.radiusLg, 1.0f);

        auto text = hint.reduced (metrics.spaceXl, metrics.spaceLg);
        const auto titleStyle = TypeStyle { theme.body.size, false, semibold };
        const auto titleFont = themeManager.font (titleStyle);
        const auto title = (version.startsWithChar ('v') ? version : "v" + version) + hintSuffix;
        const auto titleH = juce::jmax ((int) std::ceil (titleFont.getHeight()),
                                        wrappedHeight (titleFont, title, text.getWidth()));
        drawLines (g, titleFont, theme.textPrimary, title, text.removeFromTop (titleH));

        if (summary.isNotEmpty())
        {
            text.removeFromTop (metrics.spaceXs);
            drawLines (g, themeManager.font (theme.bodySm), theme.textSecondary, summary, text);
        }
    }

    if (! modal || dialog.isEmpty())
        return;

    const auto r = dialog.toFloat();
    paintElevation (g, theme.elevation3, r, theme.radius2xl);
    g.setColour (theme.bgPanel);
    g.fillRoundedRectangle (r, theme.radius2xl);
    g.setColour (theme.border);
    g.drawRoundedRectangle (r.reduced (0.5f), theme.radius2xl, 1.0f);

    auto badge = juce::Rectangle<float> ((float) (dialog.getX() + metrics.space3xl),
                                         (float) (dialog.getY() + metrics.space3xl),
                                         (float) badgeSize, (float) badgeSize);
    g.setColour (theme.accent);
    g.fillRoundedRectangle (badge, theme.radiusXl);
    drawIcon (g, Icon::check, badge.withSizeKeepingCentre (badgeIconSize, badgeIconSize), theme.textOnAccent);

    auto titleStyle = theme.heading;
    titleStyle.size = dialogTitleSize;
    titleStyle.weight = bold;
    const auto titleFont = themeManager.font (titleStyle);
    const auto subFont = themeManager.font (theme.body);
    const auto titleH = (int) std::ceil (titleFont.getHeight());
    const auto subH = (int) std::ceil (subFont.getHeight());
    auto titles = juce::Rectangle<int> (dialog.getX() + metrics.space3xl + badgeSize + headerGap,
                                        dialog.getY() + metrics.space3xl,
                                        juce::jmax (1, closeButton.getX() - metrics.spaceMd
                                                           - (dialog.getX() + metrics.space3xl + badgeSize + headerGap)),
                                        badgeSize);
    auto block = titles.withSizeKeepingCentre (titles.getWidth(), titleH + titleGap + subH);
    g.setColour (theme.textPrimary);
    g.setFont (titleFont);
    g.drawFittedText (dialogTitle, block.removeFromTop (titleH), juce::Justification::centredLeft, 1, 1.0f);
    block.removeFromTop (titleGap);
    g.setColour (theme.textSecondary);
    g.setFont (subFont);
    g.drawFittedText (juce::String (productName) + version + dialogSuffix, block.removeFromTop (subH),
                      juce::Justification::centredLeft, 1, 1.0f);

    {
        ControlState state;
        state.hovered = hot == Hot::close;
        state.pressed = pressed && state.hovered;
        const auto colours = stateColours (theme, state, theme.bgElevated, theme.accent, theme.textSecondary);
        const auto box = closeButton.toFloat();
        g.setColour (colours.fill);
        g.fillRoundedRectangle (box, theme.radiusLg);
        g.setColour (theme.border);
        g.drawRoundedRectangle (box.reduced (0.5f), theme.radiusLg, 1.0f);
        drawIcon (g, Icon::x, box.withSizeKeepingCentre ((float) metrics.iconSection, (float) metrics.iconSection),
                  colours.text);
    }

    if (! divider.isEmpty())
    {
        g.setColour (theme.borderSoft);
        g.fillRect (divider);
    }

    if (! changelogHeading.isEmpty())
    {
        g.setColour (theme.accent);
        g.setFont (themeManager.font (TypeStyle { theme.bodySm.size, false, bold }));
        g.drawFittedText (changelogLabel, changelogHeading, juce::Justification::centredLeft, 1, 1.0f);
    }

    paintAction (g, themeManager, notesButton, releaseNotesLabel, Icon::fileText, false,
                 hot == Hot::notes, pressed && hot == Hot::notes);
    paintAction (g, themeManager, restartButton, installing ? installingLabel : restartLabel, Icon::refreshCw, true,
                 hot == Hot::restart, pressed && hot == Hot::restart);

    if (hasKeyboardFocus (true))
        paintFocusRing (g, theme, restartButton.toFloat(), theme.radiusLg);
}

bool AppUpdatePrompt::hitTest (int x, int y)
{
    if (modal)
        return true;

    const auto p = juce::Point<int> (x, y);
    return pill.contains (p) || hint.contains (p);
}

AppUpdatePrompt::Hot AppUpdatePrompt::hotAt (juce::Point<int> p) const
{
    if (modal)
    {
        if (closeButton.contains (p))
            return Hot::close;

        if (notesButton.contains (p))
            return Hot::notes;

        if (restartButton.contains (p) && ! installing)
            return Hot::restart;

        return Hot::none;
    }

    if (pill.contains (p) && (phase == Phase::available || phase == Phase::ready))
        return Hot::pill;

    return Hot::none;
}

void AppUpdatePrompt::mouseMove (const juce::MouseEvent& e)
{
    const auto next = hotAt (e.position.toInt());

    if (next == hot)
        return;

    hot = next;
    setMouseCursor (next == Hot::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    repaint();
}

void AppUpdatePrompt::mouseExit (const juce::MouseEvent&)
{
    hot = Hot::none;
    pressed = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void AppUpdatePrompt::mouseDown (const juce::MouseEvent& e)
{
    pressed = hotAt (e.position.toInt()) != Hot::none;
    repaint();
}

void AppUpdatePrompt::mouseUp (const juce::MouseEvent& e)
{
    const auto target = hotAt (e.getPosition());
    pressed = false;

    if (modal)
    {
        if (target == Hot::close || (target == Hot::none && ! dialog.contains (e.getPosition())))
            closeModal();
        else if (target == Hot::notes)
            openReleasePage (page);
        else if (target == Hot::restart)
            restart();
    }
    else if (target == Hot::pill)
    {
        activatePill();
    }

    repaint();
}

bool AppUpdatePrompt::keyPressed (const juce::KeyPress& key)
{
    if (modal && key == juce::KeyPress::escapeKey)
    {
        closeModal();
        return true;
    }

    return false;
}

void AppUpdatePrompt::closeModal()
{
    if (! modal)
        return;

    modal = false;

    if (phase == Phase::welcomed)
    {
        auto done = std::move (welcomeClose);
        welcomeClose = nullptr;
        welcomeRestart = nullptr;
        phase = Phase::idle;
        setVisible (false);

        if (done != nullptr)
            done();

        return;
    }

    resized();
    repaint();
}

void AppUpdatePrompt::activatePill()
{
    if (phase == Phase::available && check != nullptr)
    {
        if (replaceable)
        {
            phase = Phase::downloading;
            fraction = 0;
            resized();
        }

        check->accept();
        return;
    }

    if (phase == Phase::ready && ! modal)
    {
        modal = true;
        focusIfShowing (*this);
        resized();
    }
}

void AppUpdatePrompt::restart()
{
    if (installing)
        return;

    if (phase == Phase::ready && check != nullptr)
    {
        installing = true;
        rebuildGeometry();
        repaint();
        check->installAndQuit();
        return;
    }

    if (phase != Phase::welcomed)
        return;

    auto go = std::move (welcomeRestart);
    welcomeClose = nullptr;
    welcomeRestart = nullptr;
    phase = Phase::idle;
    modal = false;
    setVisible (false);

    if (go != nullptr)
        go();
}

} // namespace resamper
