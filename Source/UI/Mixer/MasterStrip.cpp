#include "MasterStrip.h"
#include "Commands/MixerCommands.h"
#include "UI/Controls/Icons.h"

namespace resamper
{

namespace
{
    using namespace StripMetrics;

    /** The design's master: fader track centred at 54 px, 10 px meter wells. */
    constexpr FaderSection::Geometry faderGeometry { 80, 10.0f };
}

MasterStrip::MasterStrip (CommandRegistry& c, ThemeManager& tm)
    : commands (c), themeManager (tm), faderSection (tm, faderGeometry)
{
    setTitle ("Master");
    faderSection.onVolumeChange = [this] (Decibels volume, bool continues)
    {
        commands.invoke (cmd::mixerSetMasterVolume, { volume, continues });
    };
    addAndMakeVisible (faderSection);
}

void MasterStrip::setMaster (const MasterInfo& info)
{
    faderSection.setVolume (info.volume, themeManager.getTheme().accent);
}

void MasterStrip::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const auto bounds = getLocalBounds().toFloat();
    const auto radius = theme.radiusLg;
    g.setColour (theme.bgPanel);
    g.fillRoundedRectangle (bounds, radius);
    paintColourBar (g, bounds, radius, theme.accent);
    g.setColour (theme.border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);

    auto head = headArea.withTrimmedTop (colourBarHeight).reduced (padX, sectionPadY);
    drawIcon (g, Icon::audioLines, head.removeFromLeft (12).withSizeKeepingCentre (12, 12).toFloat(), theme.accent);
    head.removeFromLeft (7);
    drawNumber (g, themeManager, "1/2", TypeStyle { 9.5f, true, 400 }, head, juce::Justification::centredRight, theme.textDim);
    drawStyledText (g, themeManager, "Master", TypeStyle { 12.0f, false, 600 }, head, juce::Justification::centredLeft,
                    theme.textPrimary);

    g.setColour (theme.borderSoft);
    g.fillRect (faderSection.getX(), faderSection.getY(), faderSection.getWidth(), 1);
}

void MasterStrip::resized()
{
    auto r = getLocalBounds();
    headArea = r.removeFromTop (headHeight);
    faderSection.setBounds (r);
}

} // namespace resamper
