#include "VelocityEditor.h"
#include "Commands/AppCommands.h"
#include "UI/State/ArrangementViewState.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

VelocityEditor::VelocityEditor (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, ArrangementViewState& v)
    : model (m), commands (c), themeManager (tm), view (v)
{
}

void VelocityEditor::setClip (const ClipInfo& info)
{
    clip = info;
    repaint();
}

const MidiNoteInfo* VelocityEditor::noteAt (int x) const
{
    const MidiNoteInfo* found = nullptr;

    for (auto& note : clip.notes)
    {
        if (note.id.isEmpty())
            continue;

        const auto x1 = view.timeToX (clip.startSeconds + note.startSeconds);
        const auto x2 = view.timeToX (clip.startSeconds + note.startSeconds + note.lengthSeconds);

        if ((float) x >= x1 && (float) x < x2)
            found = &note;
    }

    return found;
}

int VelocityEditor::velocityAt (int y) const
{
    if (getHeight() <= 0)
        return ApplicationModel::defaultNoteVelocity;

    const auto proportion = 1.0 - (double) y / (double) getHeight();
    return juce::jlimit (ApplicationModel::minNoteVelocity, ApplicationModel::maxNoteVelocity,
                         juce::roundToInt (proportion * ApplicationModel::maxNoteVelocity));
}

void VelocityEditor::reload()
{
    setClip (model.getClip (clip.id).value_or (ClipInfo {}));
}

void VelocityEditor::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.laneB);

    if (getHeight() <= 0)
        return;

    for (auto& note : clip.notes)
    {
        const auto x = view.timeToX (clip.startSeconds + note.startSeconds);
        const auto width = std::max (1.0f, (float) (note.lengthSeconds * view.getPixelsPerSecond()));
        const auto velocity = previewVelocity && note.selected ? *previewVelocity : note.velocity;
        const auto height = (float) velocity / (float) ApplicationModel::maxNoteVelocity * (float) getHeight();

        g.setColour (note.selected ? theme.noteSelected : theme.velocity);
        g.fillRect (x, (float) getHeight() - height, width, height);
    }
}

void VelocityEditor::mouseDown (const juce::MouseEvent& e)
{
    previewVelocity.reset();

    auto* note = noteAt (e.x);

    if (note == nullptr || note->id.isEmpty())
        return;

    if (! note->selected)
        commands.invoke (cmd::noteSelect, { { note->id } });

    previewVelocity = note->velocity;
    reload();
}

void VelocityEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (! previewVelocity)
        return;

    previewVelocity = velocityAt (e.y);
    repaint();
}

void VelocityEditor::mouseUp (const juce::MouseEvent& e)
{
    if (previewVelocity && e.mouseWasDraggedSinceMouseDown())
        commands.invoke (cmd::noteSetVelocity, { clip.id, *previewVelocity });

    previewVelocity.reset();
    reload();
}

} // namespace resamper
