#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace resamper
{

struct Theme;

/** The design's icons (PRD §15.8): lucide's own geometry, a 24 x 24 box drawn with 2 px strokes.
    Each is the lucide icon of the same name in camelCase (Icon::trash2 is "trash-2"); lucideName()
    and iconNamed() convert. The groups follow the design's Icons section; a new icon is added there
    first, then here. */
enum class Icon
{
    // Transport & audio
    play, pause, square, circleDot, skipBack, repeat, rotateCcw, refreshCw, timer, metronome, crosshair, audioLines,
    audioWaveform, activity, waves, spline, chartSpline, piano, music, music2, volume2, drum,
    // Navigation & disclosure
    chevronDown, chevronRight, chevronLeft, arrowDown, arrowLeft, arrowRight, arrowUpRight, arrowLeftRight, cornerDownRight,
    ellipsis, x, maximize, maximize2, minimize2, zoomIn, zoomOut,
    // Editing tools
    mousePointer2, textCursor, pencil, eraser, scissors, scissorsLineDashed, magnet, foldVertical, unfoldVertical,
    copy, trash2, plus, undo2, flag, search, gripVertical,
    // Routing, racks & structure
    gitMerge, layers, network, folder, folderOpen, folderMinus, list, slidersHorizontal, slidersVertical, link2Off, unlink,
    // Files & visibility
    file, fileMusic, fileCode, fileText, download, eye, eyeOff,
    // Plug-ins & windows
    plug, appWindow, externalLink, pin, power, cpu, shieldCheck, save, redo2, moveDiagonal2, squareDashed, sparkles,
    package, box, puzzle,
    // Sidechain
    keyRound, headphones, funnel, cable, check, arrowRightToLine,

    // Earlier names of three icons above, kept so their callers still read naturally
    stop = square, record = circleDot, follow = crosshair
};

/** What an icon's colour says (PRD §15.8). Track and folder icons take the track colour instead. */
enum class IconRole
{
    normal,     ///< text-secondary
    inactive,   ///< text-dim
    active,     ///< accent
    onFilled,   ///< text-on-accent, on a filled (accent) background
    sidechain   ///< state-sidechain
};

/** The Theme colour of an icon in role. */
juce::Colour iconColour (const Theme&, IconRole);

/** Draws the icon centred in area (its largest square), stroked in colour. The geometry is
    a vector path, so it is sharp at every UI scale; sizes by role are in Layout Metrics
    (icon-chip, icon-control, icon-section, icon-toolbar). */
void drawIcon (juce::Graphics&, Icon, juce::Rectangle<float> area, juce::Colour);

/** Draws the icon size px square, centred on centre: `<Icon name size color />`. */
void drawIcon (juce::Graphics&, Icon, juce::Point<float> centre, float size, juce::Colour);

/** The icon's lucide name, e.g. "trash-2". */
juce::String lucideName (Icon);

/** The icon with this lucide name, or nothing when the set doesn't have it. */
std::optional<Icon> iconNamed (const juce::String& lucideName);

/** The icon's centre line in its 24 x 24 box, before stroking. */
const juce::Path& iconPath (Icon);

/** The stroke width in px of an icon size px square: lucide's 2 / 24 of the size, but never
    thinner than one physical pixel at physicalScale (the display scale times the UI scale). */
float iconStrokeWidth (float size, float physicalScale);

} // namespace resamper
