#include "Controls.h"

namespace resamper
{

namespace
{
    // Design: a device's segmented switch (mono 8): a radius-4 well, radius-3 items, 1 px apart and from the edge.
    const TypeStyle deviceSegmentStyle { 8.0f, true, 400 };
    constexpr float deviceWellRadius = 4.0f, deviceItemRadius = 3.0f, deviceItemGap = 1.0f;

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }

    /** Button labels: body size at weight 600. */
    juce::Font buttonFont (const ThemeManager& tm)
    {
        return tm.font (TypeStyle { tm.getTheme().body.size, false, 600 });
    }

    void fillAndStroke (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill, juce::Colour stroke)
    {
        g.setColour (fill);
        g.fillRoundedRectangle (r, radius);

        if (! stroke.isTransparent())
        {
            g.setColour (stroke);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        }
    }
}

//==============================================================================
ThemedButton::ThemedButton (ThemeManager& tm, const juce::String& name)
    : juce::Button (name), themeManager (tm)
{
    setWantsKeyboardFocus (true);
    setTitle (name);
}

void ThemedButton::enablementChanged()
{
    applyEnablement (*this, themeManager.getTheme());
    juce::Button::enablementChanged();
}

void ThemedButton::paintFocus (juce::Graphics& g, float cornerRadius)
{
    if (hasKeyboardFocus (false))
        paintFocusRing (g, themeManager.getTheme(), getLocalBounds().toFloat(), cornerRadius);
}

//==============================================================================
Button::Button (ThemeManager& tm, const juce::String& text, Variant v, std::optional<Icon> i)
    : ThemedButton (tm, text), variant (v), icon (i)
{
    setButtonText (text);
}

juce::Font Button::labelFont() const
{
    return numeric ? themeManager.numberFont (TypeStyle { 13.0f, true, 400 }) : buttonFont (themeManager);
}

int Button::getIdealWidth() const
{
    auto& metrics = themeManager.getMetrics();
    const auto label = textWidth (labelFont(), getButtonText());
    return 2 * metrics.spaceLg + label + (icon ? 12 + metrics.spaceSm : 0);
}

void Button::paintButton (juce::Graphics& g, bool, bool down)
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    auto bounds = getLocalBounds().toFloat();
    const auto radius = theme.radiusLg;
    const auto s = state (down);

    StateColours colours;
    juce::Colour stroke;

    switch (variant)
    {
        case Variant::primary:
            colours = stateColours (theme, s, theme.accent, theme.accent, theme.textOnAccent, true);
            stroke = colours.fill;
            break;
        case Variant::secondary:
            colours = stateColours (theme, s, theme.bgElevated, theme.accent, theme.textPrimary);
            stroke = theme.border;
            break;
        case Variant::outline:
            colours = stateColours (theme, s, juce::Colours::transparentBlack, theme.accent, theme.textPrimary);
            stroke = theme.border;
            break;
        case Variant::ghost:
            colours = stateColours (theme, s, juce::Colours::transparentBlack, theme.accent, theme.textSecondary);
            break;
    }

    fillAndStroke (g, bounds, radius, colours.fill, stroke);

    const auto font = labelFont();
    auto content = getLocalBounds().reduced (metrics.spaceLg, 0);
    const auto labelWidth = textWidth (font, getButtonText());
    const auto contentWidth = labelWidth + (icon ? 12 + metrics.spaceSm : 0);
    content = content.withSizeKeepingCentre (juce::jmin (content.getWidth(), contentWidth), content.getHeight());

    if (icon)
    {
        drawIcon (g, *icon, content.removeFromLeft (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f), colours.text);
        content.removeFromLeft (metrics.spaceSm);
    }

    g.setColour (colours.text);
    g.setFont (font);
    g.drawText (getButtonText(), content, juce::Justification::centred, true);

    paintFocus (g, radius);
}

//==============================================================================
IconButton::IconButton (ThemeManager& tm, const juce::String& name, Icon i, Kind k)
    : ThemedButton (tm, name), icon (i), kind (k)
{
    setTooltip (name);
}

void IconButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto& theme = themeManager.getTheme();
    auto bounds = getLocalBounds().toFloat();
    const auto transport = kind == Kind::transport;
    const auto radius = transport ? theme.radiusXl : theme.radiusLg - 1.0f;
    const auto on = getToggleState();
    const auto active = activeColour.value_or (theme.accent);
    const auto s = state (down);

    juce::Colour fill = transport ? theme.bgElevated : juce::Colours::transparentBlack;
    juce::Colour stroke = theme.border;
    juce::Colour glyph = iconColour.value_or (theme.textSecondary);

    if (on && ! outlineWhenActive)
    {
        fill = s.hovered ? active.brighter (0.08f) : active;
        stroke = active;
        glyph = theme.textOnAccent;
    }
    else
    {
        if (s.hovered || s.pressed)
            fill = theme.bgHover;

        if (on)
        {
            stroke = active;
            glyph = active;
        }
    }

    fillAndStroke (g, bounds, radius, fill, stroke);
    drawIcon (g, icon, bounds.withSizeKeepingCentre (bounds.getWidth() * 0.45f, bounds.getHeight() * 0.45f), glyph);
    paintFocus (g, radius);
}

//==============================================================================
Toggle::Toggle (ThemeManager& tm, const juce::String& name) : ThemedButton (tm, name)
{
    setClickingTogglesState (true);
}

void Toggle::paintButton (juce::Graphics& g, bool, bool down)
{
    auto& theme = themeManager.getTheme();
    const auto track = getLocalBounds().toFloat().withSizeKeepingCentre (26.0f, 14.0f);
    const auto on = getToggleState();
    const auto s = state (down);
    auto fill = on ? (s.hovered ? theme.accentHover : theme.accent) : (s.hovered ? theme.bgHover : theme.bgElevated);

    g.setColour (fill);
    g.fillRoundedRectangle (track, track.getHeight() / 2);

    const auto knob = track.reduced (2.0f).withWidth (10.0f).translated (on ? track.getWidth() - 14.0f : 0.0f, 0.0f);
    g.setColour (on ? theme.textOnAccent : theme.textSecondary);
    g.fillEllipse (knob);

    if (hasKeyboardFocus (false))
        paintFocusRing (g, theme, track.expanded (2.0f), track.getHeight() / 2 + 2.0f);
}

//==============================================================================
Chip::Chip (ThemeManager& tm, const juce::String& text, std::optional<Icon> i) : ThemedButton (tm, text), icon (i)
{
    setButtonText (text);
    setClickingTogglesState (true);
}

int Chip::getIdealWidth() const
{
    const auto label = textWidth (themeManager.font (themeManager.getTheme().body), getButtonText());
    return 2 * 9 + label + (icon ? 12 + 5 : showsLed ? 5 + 5 : 0);
}

void Chip::paintButton (juce::Graphics& g, bool, bool down)
{
    auto& theme = themeManager.getTheme();
    const auto on = getToggleState();
    const auto s = state (down);
    const auto radius = 5.0f;
    auto fill = on ? theme.bgElevated : juce::Colours::transparentBlack;

    if (s.hovered)
        fill = theme.bgHover;

    fillAndStroke (g, getLocalBounds().toFloat(), radius, fill, on ? theme.border : theme.borderSoft);

    auto content = getLocalBounds().reduced (9, 0);

    if (icon)
    {
        drawIcon (g, *icon, content.removeFromLeft (12).toFloat().withSizeKeepingCentre (12.0f, 12.0f),
                  on ? theme.accent : theme.textDim);
        content.removeFromLeft (5);
    }
    else if (showsLed)
    {
        g.setColour (on ? theme.accent : theme.textDim);
        g.fillEllipse (content.removeFromLeft (5).toFloat().withSizeKeepingCentre (5.0f, 5.0f));
        content.removeFromLeft (5);
    }

    g.setColour (on ? theme.textPrimary : theme.textDim);
    g.setFont (themeManager.font (theme.body));
    g.drawText (getButtonText(), content, juce::Justification::centredLeft, true);
    paintFocus (g, radius);
}

//==============================================================================
TrackButton::TrackButton (ThemeManager& tm, Kind k)
    : ThemedButton (tm, k == Kind::mute ? TRANS ("Mute") : k == Kind::solo ? TRANS ("Solo")
                      : k == Kind::arm ? TRANS ("Arm") : TRANS ("Automation")),
      kind (k)
{
    setTooltip (getName());
}

void TrackButton::paintButton (juce::Graphics& g, bool, bool down)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto on = getToggleState();
    const auto s = state (down);
    const auto radius = theme.radiusSm;

    const auto onColour = kind == Kind::mute ? theme.stateMute
                        : kind == Kind::solo ? theme.stateSolo
                        : kind == Kind::arm ? theme.rec
                                            : theme.accent;

    // Auto shows a lime tint and an accent-dim outline rather than a fill (§8.4).
    if (kind == Kind::automation && on)
        fillAndStroke (g, bounds, radius, theme.accent.withAlpha (0.16f), theme.accentDim);
    else
        fillAndStroke (g, bounds, radius, on ? onColour : (s.hovered ? theme.bgHover : theme.bgElevated), {});

    const auto glyphColour = kind == Kind::automation ? (on ? theme.accent : theme.textSecondary)
                                                      : (on ? theme.textOnAccent : theme.textSecondary);

    if (kind == Kind::arm)
    {
        g.setColour (glyphColour);   // red is the armed fill, never the idle dot
        g.fillEllipse (bounds.withSizeKeepingCentre (7.0f, 7.0f));
    }
    else if (kind == Kind::automation)
    {
        drawIcon (g, Icon::spline, bounds.reduced (2.0f), glyphColour);
    }
    else
    {
        g.setColour (glyphColour);
        g.setFont (themeManager.font (TypeStyle { theme.caption.size + 0.5f, false, 700 }));
        // The glyphs stay M and S in every UI Language, as on a console.
        g.drawText (kind == Kind::mute ? "M" : "S", getLocalBounds(), juce::Justification::centred, false);
    }

    paintFocus (g, radius);
}

//==============================================================================
Segmented::Segmented (ThemeManager& tm, juce::StringArray itemList, Style s)
    : themeManager (tm), items (std::move (itemList)), style (s)
{
    setWantsKeyboardFocus (true);
}

const TypeStyle& Segmented::textStyle() const
{
    if (style == Style::device)
        return deviceSegmentStyle;

    return style == Style::tabs ? themeManager.getTheme().label : themeManager.getTheme().bodySm;
}

void Segmented::setSelectedIndex (int index, juce::NotificationType notification)
{
    if (! juce::isPositiveAndBelow (index, items.size()) || index == selected)
        return;

    selected = index;
    repaint();

    if (notification != juce::dontSendNotification && onChange)
        onChange (selected);
}

int Segmented::getIdealWidth() const
{
    auto font = themeManager.font (textStyle());
    const auto padding = style == Style::tabs ? 11 : themeManager.getMetrics().spaceLg;
    auto width = style == Style::tabs ? 0 : 4;

    for (auto& item : items)
        width += textWidth (font, item) + 2 * padding;

    return width;
}

juce::Rectangle<float> Segmented::itemBounds (int index) const
{
    auto area = getLocalBounds().toFloat();

    if (style == Style::device)
    {
        // Equal items in a 1 px padded well, 1 px apart.
        area = area.reduced (deviceItemGap);
        const auto w = (area.getWidth() - deviceItemGap * (float) (items.size() - 1)) / (float) juce::jmax (1, items.size());
        return juce::isPositiveAndBelow (index, items.size()) ? juce::Rectangle<float> (area.getX() + (float) index * (w + deviceItemGap), area.getY(), w, area.getHeight())
                                                              : juce::Rectangle<float>();
    }

    if (style != Style::tabs)
        area = area.reduced (2.0f);

    auto font = themeManager.font (textStyle());
    const auto padding = style == Style::tabs ? 11.0f : (float) themeManager.getMetrics().spaceLg;
    float total = 0;

    for (auto& item : items)
        total += (float) textWidth (font, item) + 2 * padding;

    // Items share any spare width in proportion to their text.
    const auto scale = total > 0 ? area.getWidth() / total : 1.0f;
    auto x = area.getX();

    for (int i = 0; i < items.size(); ++i)
    {
        const auto w = ((float) textWidth (font, items[i]) + 2 * padding) * scale;

        if (i == index)
            return { x, area.getY(), w, area.getHeight() };

        x += w;
    }

    return {};
}

int Segmented::itemAt (juce::Point<int> p) const
{
    for (int i = 0; i < items.size(); ++i)
        if (itemBounds (i).contains (p.toFloat()))
            return i;

    return -1;
}

void Segmented::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto tabs = style == Style::tabs;
    const auto radius = tabs ? 5.0f : theme.radiusSm;

    if (style == Style::device)
    {
        // Design: a bg-slot well (radius 4), the active item bg-elevated (radius 3) in text-primary 700, the rest text-dim.
        g.setColour (theme.bgSlot);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), deviceWellRadius);

        for (int i = 0; i < items.size(); ++i)
        {
            const auto r = itemBounds (i);
            const auto active = i == selected;

            if (active || i == hovered)
            {
                g.setColour (active ? theme.bgElevated : theme.bgHover);
                g.fillRoundedRectangle (r, deviceItemRadius);
            }

            auto itemStyle = textStyle();
            itemStyle.weight = active ? 700 : 400;
            g.setColour (active ? theme.textPrimary : theme.textDim);
            g.setFont (themeManager.font (itemStyle));
            g.drawText (items[i], r, juce::Justification::centred, true);
        }

        if (hasKeyboardFocus (false))
            paintFocusRing (g, theme, getLocalBounds().toFloat(), deviceWellRadius);

        return;
    }

    const auto sunken = style == Style::sunken;

    if (! tabs)
        fillAndStroke (g, getLocalBounds().toFloat(), theme.radiusLg, sunken ? theme.bgElevated : theme.bgSlot,
                       sunken ? theme.border : theme.borderSoft);

    for (int i = 0; i < items.size(); ++i)
    {
        const auto r = itemBounds (i);
        const auto active = i == selected;

        if (active || i == hovered)
        {
            g.setColour (active ? (sunken ? theme.bgSlot : theme.accent) : theme.bgHover);
            g.fillRoundedRectangle (r, radius);
        }

        auto textStyleForItem = textStyle();

        if (active && ! sunken)
            textStyleForItem.weight = 600;

        g.setColour (active ? (sunken ? theme.accent : theme.textOnAccent) : theme.textSecondary);
        g.setFont (themeManager.font (textStyleForItem));
        g.drawText (items[i], r, juce::Justification::centred, true);
    }

    if (hasKeyboardFocus (false))
        paintFocusRing (g, theme, getLocalBounds().toFloat(), tabs ? radius : theme.radiusLg);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (auto index = itemAt (e.getPosition()); index >= 0)
        setSelectedIndex (index);
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    if (auto index = itemAt (e.getPosition()); index != hovered)
    {
        hovered = index;
        repaint();
    }
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

bool Segmented::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)
    {
        setSelectedIndex (juce::jmax (0, selected - 1));
        return true;
    }

    if (key == juce::KeyPress::rightKey)
    {
        setSelectedIndex (juce::jmin (items.size() - 1, selected + 1));
        return true;
    }

    return false;
}

std::unique_ptr<juce::AccessibilityHandler> Segmented::createAccessibilityHandler()
{
    struct Value : juce::AccessibilityTextValueInterface
    {
        explicit Value (Segmented& s) : owner (s) {}
        bool isReadOnly() const override                      { return false; }
        juce::String getCurrentValueAsString() const override { return owner.items[owner.selected]; }
        void setValueAsString (const juce::String& v) override { owner.setSelectedIndex (owner.items.indexOf (v)); }
        Segmented& owner;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group,
                                                         juce::AccessibilityActions(),
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

} // namespace resamper
