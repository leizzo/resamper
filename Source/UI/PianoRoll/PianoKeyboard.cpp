#include "PianoKeyboard.h"
#include "BeatGrid.h"
#include "Commands/AppCommands.h"
#include "UI/State/ArrangementViewState.h"

namespace resamper
{

PianoKeyboard::PianoKeyboard (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, ArrangementViewState& v)
    : model (m), commands (c), themeManager (tm), view (v)
{
}

void PianoKeyboard::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    const auto keyHeight = metrics.pianoKeyHeight;

    g.fillAll (theme.pianoWhite);

    const auto firstRow = view.yToRow (0, keyHeight);
    const auto lastRow = view.yToRow (getHeight(), keyHeight);

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto pitch = 127 - row;

        if (! juce::isPositiveAndBelow (pitch, 128))
            continue;

        auto key = juce::Rectangle<int> (0, view.rowToY (row, keyHeight), getWidth(), keyHeight);
        const auto black = isBlackKey (pitch);

        if (black)
            key = key.withWidth (metrics.pianoBlackKeyWidth);

        g.setColour (black ? theme.pianoBlack : theme.pianoWhite);
        g.fillRect (key);
        g.setColour (theme.background);
        g.drawRect (key);

        if (! black && pitch % 12 == 0)
        {
            g.setColour (theme.background);
            g.setFont (themeManager.getFont (0.7f));
            g.drawText ("C" + juce::String (pitch / 12 - 1),
                        key.reduced (metrics.textPadding, 0), juce::Justification::centredLeft, false);
        }
    }
}

void PianoKeyboard::mouseDown (const juce::MouseEvent&)
{
    commands.invoke (cmd::noteSelect, {});
}

} // namespace resamper
