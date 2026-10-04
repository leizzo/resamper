#include "NoteGrid.h"
#include "BeatGrid.h"
#include "Commands/AppCommands.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"

#include <algorithm>

namespace resamper
{

namespace
{
    void clampGroupMove (const std::vector<MidiNoteInfo>& original, double& deltaSeconds, int& deltaPitch)
    {
        if (original.empty())
            return;

        auto earliest = original.front().startSeconds;
        auto lowest = original.front().pitch;
        auto highest = original.front().pitch;

        for (auto& note : original)
        {
            earliest = std::min (earliest, note.startSeconds);
            lowest = std::min (lowest, note.pitch);
            highest = std::max (highest, note.pitch);
        }

        deltaPitch = juce::jlimit (-lowest, 127 - highest, deltaPitch);

        if (earliest + deltaSeconds < 0.0)
            deltaSeconds = -earliest;
    }
}

NoteGrid::NoteGrid (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, ArrangementViewState& v)
    : model (m), commands (c), themeManager (tm), view (v)
{
}

void NoteGrid::setClip (const ClipInfo& info)
{
    clip = info;
    std::map<juce::String, std::unique_ptr<NoteComponent>> kept;

    for (auto& note : clip.notes)
    {
        if (note.id.isEmpty())
            continue;

        if (auto existing = notes.find (note.id); existing != notes.end())
        {
            kept[note.id] = std::move (existing->second);
        }
        else
        {
            auto component = std::make_unique<NoteComponent> (themeManager, note);
            addAndMakeVisible (*component);
            kept[note.id] = std::move (component);
        }
    }

    notes = std::move (kept);

    if (drag.has_value())
    {
        const auto stillThere = [this] (const MidiNoteInfo& note) { return notes.contains (note.id); };

        if (! std::all_of (drag->original.begin(), drag->original.end(), stillThere))
            drag.reset();
    }

    layoutNotes();
    repaint();
}

const MidiNoteInfo* NoteGrid::shown (const juce::String& id) const
{
    if (drag.has_value())
        for (auto& note : drag->preview)
            if (note.id == id)
                return &note;

    for (auto& note : clip.notes)
        if (note.id == id)
            return &note;

    return nullptr;
}

void NoteGrid::layoutNotes()
{
    auto& metrics = themeManager.getMetrics();
    const auto keyHeight = metrics.pianoKeyHeight;
    const auto pixelsPerSecond = view.getPixelsPerSecond();

    for (auto& [id, component] : notes)
    {
        auto* note = shown (id);

        if (note == nullptr)
            continue;

        component->setNote (*note);

        const auto x = view.timeToX (clip.startSeconds + note->startSeconds);
        const auto y = view.rowToY (127 - note->pitch, keyHeight);
        const auto width = (float) (note->lengthSeconds * pixelsPerSecond);
        component->setBounds (juce::Rectangle<float> (x, (float) y, width, (float) keyHeight)
                                  .getSmallestIntegerContainer()
                                  .reduced (0, metrics.inset / 2));
    }
}

void NoteGrid::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    auto& metrics = themeManager.getMetrics();
    const auto keyHeight = metrics.pianoKeyHeight;

    g.fillAll (theme.background);

    const auto firstRow = view.yToRow (0, keyHeight);
    const auto lastRow = view.yToRow (getHeight(), keyHeight);

    for (int row = firstRow; row <= lastRow; ++row)
    {
        const auto pitch = 127 - row;

        if (! juce::isPositiveAndBelow (pitch, 128))
            continue;

        g.setColour (isBlackKey (pitch) ? theme.pianoBlack.withAlpha (0.45f) : theme.laneA);
        g.fillRect (0, view.rowToY (row, keyHeight), getWidth(), keyHeight);
    }

    const auto step = gridStepBeats (pixelsPerBeat (model, view, (float) getWidth() * 0.5f), metrics);

    for (auto& line : beatLines (model, view.xToTime (0), view.xToTime ((float) getWidth()), step))
    {
        const auto x = view.timeToX (line.seconds);

        if (x < 0 || x > (float) getWidth())
            continue;

        g.setColour (line.bar ? theme.ruler : line.beat ? theme.ruler.withAlpha (0.45f) : theme.gridLine);
        g.drawVerticalLine (juce::roundToInt (x), 0.0f, (float) getHeight());
    }

    if (clip.id.isNotEmpty() && clip.lengthSeconds > 0.0)
    {
        const auto x1 = view.timeToX (clip.startSeconds);
        const auto x2 = view.timeToX (clip.startSeconds + clip.lengthSeconds);
        g.setColour (theme.background.withAlpha (0.55f));

        if (x1 > 0.0f)
            g.fillRect (0, 0, juce::roundToInt (x1), getHeight());

        if (x2 < (float) getWidth())
            g.fillRect (juce::roundToInt (x2), 0, getWidth(), getHeight());
    }
}

NoteComponent* NoteGrid::noteAt (juce::Point<int> p) const
{
    for (int i = getNumChildComponents(); --i >= 0;)
        if (auto* note = dynamic_cast<NoteComponent*> (getChildComponent (i)); note != nullptr && note->getBounds().contains (p))
            return note;

    return nullptr;
}

NoteGrid::DragMode NoteGrid::dragModeAt (const NoteComponent& note, juce::Point<int> p) const
{
    const auto handle = themeManager.getMetrics().clipResizeHandleWidth;
    const auto x = p.x - note.getX();

    if (note.getWidth() < 3 * handle)
        return DragMode::move;

    if (x < handle)
        return DragMode::resizeStart;

    if (x >= note.getWidth() - handle)
        return DragMode::resizeEnd;

    return DragMode::move;
}

void NoteGrid::reload()
{
    setClip (model.getClip (clip.id).value_or (ClipInfo {}));
}

void NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    auto* note = noteAt (e.getPosition());
    const bool onEdge = note != nullptr && dragModeAt (*note, e.getPosition()) != DragMode::move;
    setMouseCursor (onEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void NoteGrid::mouseDown (const juce::MouseEvent& e)
{
    drag.reset();
    clickOnEmpty = false;

    if (auto* component = noteAt (e.getPosition()))
    {
        auto info = component->getNote();
        const auto mode = dragModeAt (*component, e.getPosition());

        if (e.mods.isShiftDown())
        {
            // Toggle the note under the pointer; leave the rest of the selection alone.
            juce::StringArray ids;

            for (auto& note : clip.notes)
                if (note.selected != (note.id == info.id))
                    ids.add (note.id);

            commands.invoke (cmd::noteSelect, { ids });
            reload();
            return;
        }

        juce::StringArray moving;

        if (mode == DragMode::move && info.selected)
        {
            for (auto& note : clip.notes)
                if (note.selected)
                    moving.add (note.id);
        }
        else
        {
            commands.invoke (cmd::noteSelect, { { info.id } });
            info.selected = true;
            moving.add (info.id);
        }

        Drag started;
        started.mode = mode;
        started.grabEditSeconds = view.xToTime ((float) e.x);
        started.grabPitch = pitchAtY (view, e.y, themeManager.getMetrics().pianoKeyHeight);

        for (auto& id : moving)
            for (auto& note : clip.notes)
                if (note.id == id)
                {
                    auto copy = note;
                    copy.selected = true;
                    started.original.push_back (copy);
                }

        if (mode != DragMode::move)
        {
            started.original = { info };
        }

        started.preview = started.original;
        drag = std::move (started);
        return;
    }

    clickOnEmpty = true;
}

void NoteGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (! drag || drag->original.empty())
        return;

    auto& metrics = themeManager.getMetrics();
    const auto deltaSeconds = view.xToTime ((float) e.x) - drag->grabEditSeconds;
    const auto minLength = std::max (1.0e-3, 2.0 * metrics.clipResizeHandleWidth / view.getPixelsPerSecond());

    if (drag->mode == DragMode::move)
    {
        auto delta = deltaSeconds;
        auto deltaPitch = pitchAtY (view, e.y, metrics.pianoKeyHeight) - drag->grabPitch;
        clampGroupMove (drag->original, delta, deltaPitch);

        for (size_t i = 0; i < drag->original.size(); ++i)
        {
            drag->preview[i] = drag->original[i];
            drag->preview[i].startSeconds = drag->original[i].startSeconds + delta;
            drag->preview[i].pitch = drag->original[i].pitch + deltaPitch;
        }
    }
    else
    {
        const auto& from = drag->original.front();
        auto& to = drag->preview.front();
        const auto end = from.startSeconds + from.lengthSeconds;

        if (drag->mode == DragMode::resizeStart)
        {
            const auto latest = std::max (0.0, end - minLength);
            to.startSeconds = juce::jlimit (0.0, latest, from.startSeconds + deltaSeconds);
            to.lengthSeconds = end - to.startSeconds;
        }
        else
        {
            to.startSeconds = from.startSeconds;
            to.lengthSeconds = std::max (minLength, end + deltaSeconds - from.startSeconds);
        }
    }

    layoutNotes();
}

void NoteGrid::mouseUp (const juce::MouseEvent& e)
{
    if (drag && e.mouseWasDraggedSinceMouseDown() && ! drag->original.empty())
    {
        const auto released = *drag;
        drag.reset();

        if (released.mode == DragMode::move)
        {
            juce::StringArray ids;
            const auto deltaSeconds = released.preview.front().startSeconds - released.original.front().startSeconds;
            const auto deltaPitch = released.preview.front().pitch - released.original.front().pitch;

            for (auto& note : released.original)
                ids.add (note.id);

            commands.invoke (cmd::noteMove, { clip.id, ids, deltaSeconds, deltaPitch });
        }
        else
        {
            const auto& to = released.preview.front();
            commands.invoke (cmd::noteResize, { clip.id, to.id, to.startSeconds, to.startSeconds + to.lengthSeconds });
        }
    }
    else if (clickOnEmpty && ! e.mouseWasDraggedSinceMouseDown() && clip.id.isNotEmpty())
    {
        const auto editTime = view.xToTime ((float) e.x);
        const auto local = editTime - clip.startSeconds;

        if (local >= 0.0 && local < clip.lengthSeconds)
        {
            const auto beat = model.secondsToBeats (std::max (0.0, editTime));
            const auto length = model.beatsToSeconds (beat + 1.0) - editTime;

            if (length > 0.0)
                commands.invoke (cmd::noteAdd, { clip.id, local, length,
                                                          pitchAtY (view, e.y, themeManager.getMetrics().pianoKeyHeight) });
        }
    }

    drag.reset();
    clickOnEmpty = false;
    reload();
}

} // namespace resamper
