#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace resamper
{

class LayoutSource;

/** One type token (PRD §15.2): size in px, family (Inter, or IBM Plex Mono
    when mono), weight, case and tracking in px. */
struct TypeStyle
{
    float size = 0;
    bool mono = false;
    int weight = 400;
    bool uppercase = false;
    float tracking = 0;

    /** The text as this style shows it (uppercase styles upper-case it). */
    juce::String apply (const juce::String& text) const   { return uppercase ? text.toUpperCase() : text; }
};

/** One drop shadow of an elevation level (PRD §15.3). */
struct Shadow
{
    juce::Colour colour;
    juce::Point<int> offset;
    int radius = 0;
};

/** Visual style only: colours, corner radii, fonts, shadows. Never geometry.

    The design-system tokens (PRD §15) come first. The older semantic entries
    after them (background, panel, clip, ...) usually name a token in the theme
    file ("$bg-deep"), so editing a token restyles both.
*/
struct Theme
{
    // Colour tokens (§15.1)
    juce::Colour bgDeep, bgPanel, bgTrack, bgElevated, bgSlot, bgHover,
                 border, borderSoft, gridBar, gridBeat,
                 textPrimary, textSecondary, textDim, textOnAccent,
                 accent, accentHover, accentDim, focusRing, rec,
                 stateWarning, stateMute, statePre, stateSolo, statePolarity, stateSidechain,
                 playhead, meterLow, meterMid, meterHigh, scrim;

    /** clip-drums, clip-bass, clip-chords, clip-pads, clip-arp, clip-vocal, clip-fx. */
    std::array<juce::Colour, 7> trackPalette;

    /** A track palette colour by index, clamped to the palette. */
    juce::Colour trackColour (int index) const   { return trackPalette[(size_t) juce::jlimit (0, (int) trackPalette.size() - 1, index)]; }

    /** return-a .. return-d. */
    std::array<juce::Colour, 4> returnColours;

    // Type tokens (§15.2)
    TypeStyle display, heading, title, label, body, bodySm, caption, micro;

    // Radius scale (§15.3): meter, badge/pad/M-S-R, slot/select, strip/segmented, device/card, popover
    float radiusXs = 0, radiusSm = 0, radiusMd = 0, radiusLg = 0, radiusXl = 0, radius2xl = 0;

    // Elevation (§15.3): L1 control, L2 popover, L3 floating window. L0 is flat.
    std::vector<Shadow> elevation1, elevation2, elevation3;

    float disabledOpacity = 1;

    // Semantic entries, most of them aliases of the tokens above
    juce::Colour background, panel, text, mutedText,
                 laneA, laneB, trackHeader, trackHeaderSelected, mute, solo, armed,
                 recording,   ///< a recording in progress in its lane
                 loop,        ///< the loop range on the ruler
                 clip, clipSelected, clipText, waveform,
                 clipOutlineSelected,   ///< the outline round a selected clip
                 midiClip, midiClipSelected, midiNote,
                 pianoWhite, pianoBlack, noteSelected, gridLine, velocity,
                 ruler, error;
    float cornerRadius = 0;
    float fontSize = 0;
};

/** UI geometry. Components read sizes from here, never hard-code them. */
struct LayoutMetrics
{
    // Spacing scale (§15.3): space-2xs .. space-3xl
    int space2xs = 0, spaceXs = 0, spaceSm = 0, spaceMd = 0, spaceLg = 0, spaceXl = 0, space2xl = 0, space3xl = 0;

    // Sizing scale (§15.3)
    int controlXs = 0, controlSm = 0, controlMd = 0, controlLg = 0;   ///< control heights 16 / 20 / 22 / 26
    int transportButton = 0;     ///< square transport buttons
    int iconChip = 0, iconControl = 0, iconSection = 0, iconToolbar = 0;   ///< icon sizes (§15.8) 10 / 12 / 14 / 16
    int toolbarHeight = 0;
    int topBarHeight = 0;
    int inspectorWidth = 0;
    int browserWidth = 0;        ///< the left Browser (§6.2)
    int stripWidth = 0, stripCompactWidth = 0, stripBusWidth = 0;

    int transportHeight = 0;
    int statusBarHeight = 0;
    int timelineHeight = 0;
    int trackHeight = 0;
    int trackHeaderWidth = 0;
    int clipHeaderHeight = 0;
    int inset = 0;          ///< gap between adjacent boxes (track rows, clips)
    int textPadding = 0;    ///< space between a box edge and its text
    int playheadWidth = 0;
    int clipResizeHandleWidth = 0;   ///< grab zone at each clip edge
    int trackControlHeight = 0;      ///< one row of track header controls
    int trackButtonWidth = 0;        ///< mute and solo buttons
    int midiNoteHeight = 0;          ///< compact note preview inside a MIDI clip
    int pianoKeyWidth = 0;           ///< piano roll keyboard
    int pianoKeyHeight = 0;          ///< one semitone row in the piano roll
    int pianoBlackKeyWidth = 0;      ///< black keys, shorter than a white key
    int pianoScrollMargin = 0;       ///< key rows kept above middle C on open
    int velocityLaneHeight = 0;      ///< piano roll velocity lane
    int gridEighthPixels = 0;        ///< pixels per beat before the grid shows 1/8
    int gridSixteenthPixels = 0;     ///< pixels per beat before the grid shows 1/16
    int pluginTitleBarHeight = 0;    ///< a plug-in window's title bar (§9.6)
    int pluginToolbarHeight = 0;     ///< a plug-in window's host toolbar
    int pluginFooterHeight = 0;      ///< a plug-in window's host footer
    int windowCascade = 0;           ///< how far each further floating window steps down-right
};

/** Loads Theme and Layout Metrics from one JSON file under separate keys
    ("colors", "type", "radius", "elevation" and "style" for the Theme,
    "metrics" for geometry) and applies the Theme to the app's
    LookAndFeel.

    A colour may name another colour of the same file with "$name". A file may
    say "extends": "<other theme file>"; the other file is read first and this
    one's entries replace its entries, section by section.

    Layout Metrics are read once at load(). reloadTheme() re-styles only: it
    never changes geometry, so nothing is re-laid-out or recreated.

    The fonts (Inter, IBM Plex Mono) are bundled under UI/fonts and loaded once.
*/
class ThemeManager
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void themeChanged() = 0;   ///< repaint
    };

    ThemeManager (const LayoutSource&, juce::String themeFile);
    ~ThemeManager();

    /** Reads Theme and Layout Metrics. Called once at startup. */
    juce::Result load();

    /** Re-reads the Theme only and notifies listeners. On failure, the current
        Theme is kept. Geometry is left as load() set it. */
    juce::Result reloadTheme();

    /** Stores themeFile and calls reloadTheme(). Colours change; Layout Metrics
        stay at the values from the last load(). */
    juce::Result useTheme (juce::String themeFile);

    const Theme& getTheme() const noexcept                  { return theme; }
    const LayoutMetrics& getMetrics() const noexcept        { return metrics; }
    juce::LookAndFeel& getLookAndFeel() noexcept            { return *lookAndFeel; }

    /** Where the Theme, and the layouts beside it, are read from. */
    const LayoutSource& getLayoutSource() const noexcept    { return source; }

    /** The legacy body font, scaled. Prefer font (TypeStyle). */
    juce::Font getFont (float scale = 1.0f) const;

    /** A type token's font: family, weight, size and tracking. */
    juce::Font font (const TypeStyle&) const;

    /** The mono font at a type token's size: numbers are always mono (§15.2). */
    juce::Font numberFont (const TypeStyle&) const;

    void addListener (Listener* l)      { listeners.add (l); }
    void removeListener (Listener* l)   { listeners.remove (l); }

    /** Parses a theme file (after any "extends" is merged). Every key is
        required: a missing one fails loudly rather than falling back to a
        hard-coded value. */
    static juce::Result parse (const juce::String& json, Theme&, LayoutMetrics&);

private:
    struct Fonts;

    const LayoutSource& source;
    juce::String themeFile;
    Theme theme;
    LayoutMetrics metrics;
    std::unique_ptr<Fonts> fonts;
    std::unique_ptr<juce::LookAndFeel_V4> lookAndFeel;

    juce::Result read (Theme&, LayoutMetrics&) const;
    juce::Result readMerged (const juce::String& file, juce::var& json, int depth) const;
    juce::ListenerList<Listener> listeners;
};

} // namespace resamper
