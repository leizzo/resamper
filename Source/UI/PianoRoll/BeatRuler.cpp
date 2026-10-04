#include "BeatRuler.h"
#include "BeatGrid.h"
#include "Commands/AppCommands.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

BeatRuler::BeatRuler (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, ArrangementViewState& v)
    : model (m), commands (c), themeManager (tm), view (v)
{
}

void BeatRuler::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.panel);

    const auto step = gridStepBeats (pixelsPerBeat (model, view, (float) getWidth() * 0.5f), themeManager.getMetrics());
    const auto font = themeManager.getFont (0.75f);
    g.setFont (font);

    for (auto& line : beatLines (model, view.xToTime (0), view.xToTime ((float) getWidth()), step))
    {
        const auto x = view.timeToX (line.seconds);

        if (x < 0 || x > (float) getWidth())
            continue;

        g.setColour (line.bar ? theme.text : line.beat ? theme.ruler : theme.gridLine);
        g.drawVerticalLine (juce::roundToInt (x), (float) getHeight() * (line.bar ? 0.35f : 0.65f), (float) getHeight());

        if (line.bar)
        {
            const auto label = juce::String (line.barNumber);
            const auto textWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, label));
            g.setColour (theme.ruler);
            g.drawText (label, juce::roundToInt (x) + themeManager.getMetrics().textPadding / 2, 0,
                        textWidth, getHeight() / 2 + themeManager.getMetrics().textPadding, juce::Justification::bottomLeft, false);
        }
    }

    g.setColour (theme.background);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
}

void BeatRuler::mouseDown (const juce::MouseEvent& e)
{
    commands.invoke (cmd::transportSetPosition, std::max (0.0, view.xToTime ((float) e.x)));
}

} // namespace resamper
