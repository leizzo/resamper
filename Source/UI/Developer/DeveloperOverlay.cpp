#include "DeveloperOverlay.h"

namespace resamper
{

DeveloperOverlay::DeveloperOverlay (ThemeManager& tm)
    : themes (tm)
{
    setInterceptsMouseClicks (false, true);

    addAndMakeVisible (status);
    status.setComponentID ("developer.status");
    status.setJustificationType (juce::Justification::centredLeft);

    themes.addListener (this);
    applyTheme();
}

DeveloperOverlay::~DeveloperOverlay()
{
    themes.removeListener (this);
}

void DeveloperOverlay::setStatusText (const juce::String& text)
{
    status.setText (text, juce::dontSendNotification);
}

void DeveloperOverlay::paint (juce::Graphics& g)
{
    g.fillAll (themes.getTheme().background);
}

void DeveloperOverlay::resized()
{
    status.setBounds (getLocalBounds());
}

void DeveloperOverlay::themeChanged()
{
    applyTheme();
    repaint();
}

void DeveloperOverlay::applyTheme()
{
    status.setFont (themes.getFont());
    status.setColour (juce::Label::textColourId, themes.getTheme().text);
    status.setColour (juce::Label::backgroundColourId, themes.getTheme().panel);
}

} // namespace resamper
