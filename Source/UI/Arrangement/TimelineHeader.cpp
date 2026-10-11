#include "TimelineHeader.h"
#include "Commands/AppCommands.h"
#include "UI/PianoRoll/BeatGrid.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Controls/Menus.h"
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    /** Movement below this stays a click (move the playhead), not a loop drag. */
    constexpr int loopDragThresholdPixels = 4;
}

TimelineHeader::TimelineHeader (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, ArrangementViewState& v)
    : model (m), commands (c), themeManager (tm), view (v)
{
}

void TimelineHeader::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    g.fillAll (theme.bgPanel);

    const auto startSeconds = view.xToTime (0.0f), endSeconds = view.xToTime ((float) getWidth());

    // A tick on every grid line the lanes draw.
    g.setColour (theme.gridBar);
    const auto tickHeight = getHeight() / 3;

    for (auto& line : beatLines (model, startSeconds, endSeconds, drawnGridBeats (model, view, metrics)))
        g.fillRect (juce::roundToInt (view.timeToX (line.seconds)), getHeight() - tickHeight, 1, tickHeight);

    // Bar numbers, kept far enough apart to read.
    const auto numberFont = themeManager.numberFont (TypeStyle { 10.0f, true, 400 });

    for (auto& line : barLines (model, startSeconds, endSeconds, barsPerGridLine (model, view)))
    {
        const auto x = juce::roundToInt (view.timeToX (line.seconds));
        g.setColour (theme.border);
        g.fillRect (x, 0, 1, getHeight());
        g.setColour (theme.textDim);
        g.setFont (numberFont);
        g.drawText (juce::String (line.barNumber), x + 5, 0, 40, getHeight(), juce::Justification::centredLeft, false);
    }

    // The loop brace, always there: lime while looping, faint when off.
    const auto loop = draggedLoop.value_or (model.getLoopRange());
    const auto brace = braceArea (loop);
    const bool active = model.isLooping() || (draggedLoop && dragMode == DragMode::newLoop);
    g.setColour (theme.accent.withAlpha (active ? 0.9f : 0.3f));
    g.fillRoundedRectangle (brace.withHeight (6.0f), 2.0f);
    g.fillRect (brace.withWidth (2.0f));
    g.fillRect (brace.withTrimmedLeft (brace.getWidth() - 2.0f));

    g.setColour (theme.borderSoft);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

juce::Rectangle<float> TimelineHeader::braceArea (TimeRangeSeconds loop) const
{
    const auto x1 = view.timeToX (loop.start), x2 = view.timeToX (loop.end);
    return { x1, 2.0f, std::max (2.0f, x2 - x1), 12.0f };
}

TimelineHeader::DragMode TimelineHeader::dragModeAt (juce::Point<int> p) const
{
    const auto handle = (float) themeManager.getMetrics().clipResizeHandleWidth;
    const auto brace = braceArea (model.getLoopRange());
    const auto x = (float) p.x;

    if ((float) p.y > brace.getBottom() + 2.0f || x < brace.getX() - handle || x > brace.getRight() + handle)
        return DragMode::newLoop;

    if (std::abs (x - brace.getX()) <= handle)
        return DragMode::loopStart;

    if (std::abs (x - brace.getRight()) <= handle)
        return DragMode::loopEnd;

    return DragMode::moveLoop;
}

double TimelineHeader::snapped (double seconds) const
{
    return std::max (0.0, snapToGrid (model, view, themeManager.getMetrics(), std::max (0.0, seconds)));
}

void TimelineHeader::mouseMove (const juce::MouseEvent& e)
{
    const auto mode = dragModeAt (e.getPosition());
    setMouseCursor (mode == DragMode::loopStart || mode == DragMode::loopEnd ? juce::MouseCursor::LeftRightResizeCursor
                  : mode == DragMode::moveLoop                              ? juce::MouseCursor::DraggingHandCursor
                                                                            : juce::MouseCursor::NormalCursor);
}

void TimelineHeader::mouseDown (const juce::MouseEvent& e)
{
    draggedLoop.reset();

    if (e.mods.isPopupMenu())
    {
        dragMode = DragMode::newLoop;
        showMenu();
        return;
    }

    dragMode = dragModeAt (e.getPosition());
    dragStartSeconds = std::max (0.0, view.xToTime ((float) e.x));
    loopAtDragStart = model.getLoopRange();
}

void TimelineHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || e.getDistanceFromDragStart() < loopDragThresholdPixels)
        return;

    const auto now = std::max (0.0, view.xToTime ((float) e.x));
    auto loop = loopAtDragStart;

    switch (dragMode)
    {
        case DragMode::newLoop:
            loop = { snapped (std::min (dragStartSeconds, now)), snapped (std::max (dragStartSeconds, now)) };
            break;

        case DragMode::moveLoop:
        {
            const auto length = loop.end - loop.start;
            loop.start = snapped (loop.start + now - dragStartSeconds);
            loop.end = loop.start + length;
            break;
        }

        case DragMode::loopStart:   loop.start = snapped (now); break;
        case DragMode::loopEnd:     loop.end = snapped (now); break;
    }

    // An end dragged past the other, or a loop narrower than the grid, keeps the last preview.
    if (loop.end > loop.start)
        draggedLoop = loop;

    repaint();
}

void TimelineHeader::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (auto loop = std::exchange (draggedLoop, std::nullopt))
        commands.invoke (dragMode == DragMode::newLoop ? cmd::transportSetLoopRange : cmd::transportMoveLoop,
                         { loop->start, loop->end });
    else
        commands.invoke (cmd::transportSetPosition, dragStartSeconds);

    repaint();
}

void TimelineHeader::showMenu()
{
    juce::PopupMenu grid;
    const auto gridBeats = view.getGridBeats();

    auto chooseGrid = [this] (double beats)
    {
        return [safe = juce::Component::SafePointer<TimelineHeader> (this), beats]
        {
            if (safe != nullptr)
                safe->view.setGridBeats (beats);
        };
    };

    grid.addItem (juce::PopupMenu::Item (TRANS ("Adaptive")).setTicked (gridBeats <= ArrangementViewState::adaptiveGrid)
                      .setAction (chooseGrid (ArrangementViewState::adaptiveGrid)));

    // 1/2 .. 1/16 notes, in quarter-note beats.
    for (const int noteValue : { 2, 4, 8, 16 })
    {
        const auto beats = 4.0 / (double) noteValue;
        grid.addItem (juce::PopupMenu::Item ("1/" + juce::String (noteValue)).setTicked (std::abs (gridBeats - beats) < 1.0e-9)
                          .setAction (chooseGrid (beats)));
    }

    juce::PopupMenu menu;
    menu.addItem (commandItem (commands, cmd::transportToggleLoop).setTicked (model.isLooping()));
    menu.addItem (commandItem (commands, cmd::transportLoopSelection));
    menu.addSeparator();
    menu.addItem (juce::PopupMenu::Item (TRANS ("Snap to Grid")).setTicked (view.isSnapOn())
                      .setAction ([safe = juce::Component::SafePointer<TimelineHeader> (this)]
                                  {
                                      if (safe != nullptr)
                                          safe->view.setSnapOn (! safe->view.isSnapOn());
                                  }));
    menu.addSubMenu (TRANS ("Grid"), grid);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

} // namespace resamper
