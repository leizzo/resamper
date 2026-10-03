#include "StatusBar.h"

namespace resamper
{

namespace
{
    constexpr int padding = 2;
    constexpr int gap = 12;
    constexpr int projectWidth = 220;
    constexpr int modeWidth = 180;
}

StatusBar::StatusBar (ThemeManager& tm)
    : themes (tm)
{
    setComponentID ("statusbar");
    setInterceptsMouseClicks (false, false);
    themes.addListener (this);
}

StatusBar::~StatusBar()
{
    themes.removeListener (this);
}

void StatusBar::setProject (const juce::String& text)   { setField (project, text); }
void StatusBar::setDevice (const juce::String& text)    { setField (device, text); }
void StatusBar::setMode (const juce::String& text)      { setField (mode, text); }

void StatusBar::setField (juce::String& field, const juce::String& text)
{
    if (field != text)
    {
        field = text;
        repaint();
    }
}

void StatusBar::paint (juce::Graphics& g)
{
    auto& t = themes.getTheme();
    const auto textPadding = themes.getMetrics().textPadding;
    g.setColour (t.mutedText);
    g.setFont (themes.getFont());

    auto area = getLocalBounds().reduced (padding);
    auto projectArea = area.removeFromLeft (projectWidth);
    area.removeFromLeft (gap);
    auto modeArea = area.removeFromRight (modeWidth);
    area.removeFromRight (gap);

    g.drawFittedText (project, projectArea.reduced (textPadding, 0), juce::Justification::centredLeft, 1);
    g.drawFittedText (device, area.reduced (textPadding, 0), juce::Justification::centredLeft, 1);
    g.drawFittedText (mode, modeArea.reduced (textPadding, 0), juce::Justification::centredRight, 1);
}

void StatusBar::themeChanged()
{
    repaint();
}

} // namespace resamper
