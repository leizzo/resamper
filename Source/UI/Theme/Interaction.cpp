#include "Interaction.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace resamper
{

ControlState ControlState::of (const juce::Component& c, bool on, bool pressed)
{
    ControlState s;
    s.enabled = c.isEnabled();
    s.hovered = s.enabled && c.isMouseOverOrDragging (true);
    s.pressed = s.enabled && pressed;
    s.on = on;
    s.focused = c.hasKeyboardFocus (false);
    return s;
}

StateColours stateColours (const Theme& theme, const ControlState& state, juce::Colour base, juce::Colour onColour,
                           juce::Colour text, bool primary)
{
    if (state.on || (state.pressed && ! primary))
        return { state.hovered ? onColour.brighter (0.08f) : onColour, theme.textOnAccent };

    if (primary)
        return { state.hovered || state.pressed ? theme.accentHover : theme.accent, theme.textOnAccent };

    if (state.hovered)
        return { theme.bgHover, text };

    return { base, text };
}

void paintFocusRing (juce::Graphics& g, const Theme& theme, juce::Rectangle<float> bounds, float cornerRadius)
{
    g.setColour (theme.focusRing);
    g.drawRoundedRectangle (bounds.reduced (1.0f), juce::jmax (0.0f, cornerRadius - 1.0f), 2.0f);
}

juce::MouseCursor notAllowedCursor()
{
    static const juce::MouseCursor cursor = []
    {
        constexpr int size = 20;
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        const auto ring = juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size).reduced (3.0f);

        for (auto [colour, width] : { std::pair (juce::Colours::white, 4.5f), std::pair (juce::Colour (0xffe0443a), 2.0f) })
        {
            g.setColour (colour);
            g.drawEllipse (ring, width);
            g.drawLine ({ ring.getTopLeft().translated (2.5f, 2.5f), ring.getBottomRight().translated (-2.5f, -2.5f) }, width);
        }

        return juce::MouseCursor (image, size / 2, size / 2);
    }();

    return cursor;
}

namespace
{
    /** Moves a component side to side for a moment, then puts it back. Deletes itself. */
    struct Shake : juce::Timer
    {
        explicit Shake (juce::Component& c) : target (&c), home (c.getPosition())   { startTimer (16); }

        void timerCallback() override
        {
            static constexpr int offsets[] = { 5, -5, 4, -4, 2, -2, 0 };

            if (target == nullptr || step >= (int) std::size (offsets))
            {
                if (target != nullptr)
                    target->setTopLeftPosition (home);

                delete this;   // nosemgrep: no-explicit-delete -- a fire-and-forget timer that owns itself
                return;
            }

            target->setTopLeftPosition (home.translated (offsets[step++], 0));
        }

        juce::Component::SafePointer<juce::Component> target;
        juce::Point<int> home;
        int step = 0;
    };
}

void rejectWithShake (juce::Component& c, const juce::String& why)
{
    new Shake (c);

    if (why.isEmpty())
        return;

    if (auto* top = c.getTopLevelComponent())
    {
        auto* bubble = new juce::BubbleMessageComponent (300);
        top->addChildComponent (bubble);
        juce::AttributedString text;
        text.append (why, juce::FontOptions (12.0f), juce::Colours::white);
        bubble->showAt (top->getLocalArea (&c, c.getLocalBounds()), text, 2500, true, true);
    }
}

void applyEnablement (juce::Component& c, const Theme& theme)
{
    const auto enabled = c.isEnabled();
    c.setAlpha (enabled ? 1.0f : theme.disabledOpacity);
    c.setMouseCursor (enabled ? juce::MouseCursor::NormalCursor : notAllowedCursor());
}

void paintElevation (juce::Graphics& g, const std::vector<Shadow>& level, juce::Rectangle<float> bounds, float cornerRadius)
{
    juce::Path shape;
    shape.addRoundedRectangle (bounds, cornerRadius);

    for (auto& shadow : level)
        juce::DropShadow (shadow.colour, shadow.radius, shadow.offset).drawForPath (g, shape);
}

void drawNumber (juce::Graphics& g, const ThemeManager& tm, const juce::String& text, const TypeStyle& style,
                 juce::Rectangle<int> area, juce::Justification justification, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (tm.numberFont (style));
    g.drawText (text, area, justification, false);
}

void drawStyledText (juce::Graphics& g, const ThemeManager& tm, const juce::String& text, const TypeStyle& style,
                     juce::Rectangle<int> area, juce::Justification justification, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (tm.font (style));
    g.drawText (style.apply (text), area, justification, true);
}

} // namespace resamper
