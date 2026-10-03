#include "ThemeManager.h"
#include "UI/Theme/UIFileSource.h"

namespace resamper
{

namespace
{
    class ResamperLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        explicit ResamperLookAndFeel (const ThemeManager& tm) : themeManager (tm) {}

        void apply (const Theme& t)
        {
            setColour (juce::ResizableWindow::backgroundColourId, t.background);
            setColour (juce::ScrollBar::thumbColourId, t.border);
            setColour (juce::TextEditor::backgroundColourId, t.bgSlot);
            setColour (juce::TextEditor::textColourId, t.textPrimary);
            setColour (juce::TextEditor::highlightColourId, t.accentDim);
            setColour (juce::TextEditor::highlightedTextColourId, t.textOnAccent);
            setColour (juce::TextEditor::outlineColourId, t.border);
            setColour (juce::TextEditor::focusedOutlineColourId, t.focusRing);
            setColour (juce::CaretComponent::caretColourId, t.accent);
            setColour (juce::ComboBox::backgroundColourId, t.bgElevated);
            setColour (juce::ComboBox::textColourId, t.textPrimary);
            setColour (juce::ComboBox::arrowColourId, t.textSecondary);
            setColour (juce::ListBox::backgroundColourId, t.bgPanel);
            setColour (juce::PopupMenu::highlightedTextColourId, t.textOnAccent);
            setColour (juce::DocumentWindow::textColourId, t.text);
            setColour (juce::TextButton::buttonColourId, t.panel);
            setColour (juce::TextButton::buttonOnColourId, t.accent);
            setColour (juce::TextButton::textColourOffId, t.text);
            setColour (juce::TextButton::textColourOnId, t.text);
            setColour (juce::ComboBox::outlineColourId, t.mutedText.withAlpha (0.4f));
            setColour (juce::Slider::backgroundColourId, t.background);
            setColour (juce::Slider::trackColourId, t.accent);
            setColour (juce::Slider::thumbColourId, t.text);
            setColour (juce::Slider::rotarySliderFillColourId, t.accent);
            setColour (juce::Slider::rotarySliderOutlineColourId, t.background);
            setColour (juce::BubbleComponent::backgroundColourId, t.panel);
            setColour (juce::BubbleComponent::outlineColourId, t.mutedText.withAlpha (0.4f));
            setColour (juce::TooltipWindow::backgroundColourId, t.panel);
            setColour (juce::TooltipWindow::textColourId, t.text);
            setColour (juce::PopupMenu::backgroundColourId, t.panel);
            setColour (juce::PopupMenu::textColourId, t.text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent);
            setColour (juce::AlertWindow::backgroundColourId, t.panel);
            setColour (juce::AlertWindow::textColourId, t.text);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int) override   { return themeManager.font (themeManager.getTheme().label); }
        juce::Font getPopupMenuFont() override                           { return themeManager.font (themeManager.getTheme().body).withPointHeight (12.0f); }
        juce::Font getComboBoxFont (juce::ComboBox&) override             { return themeManager.font (themeManager.getTheme().body); }
        juce::Font getLabelFont (juce::Label&) override                   { return themeManager.font (themeManager.getTheme().body); }

        /** `Select`: a bg-slot well with a dim chevron (PRD §15.4). */
        void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
        {
            auto& t = themeManager.getTheme();
            const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

            g.setColour (t.bgSlot);
            g.fillRoundedRectangle (bounds, t.radiusMd);
            g.setColour (box.isMouseOver (true) ? t.border : t.borderSoft);
            g.drawRoundedRectangle (bounds.reduced (0.5f), t.radiusMd, 1.0f);

            const auto chevron = juce::Rectangle<float> ((float) width - 8.0f - 11.0f, 0.0f, 11.0f, (float) height)
                                     .withSizeKeepingCentre (8.0f, 4.0f);
            juce::Path p;
            p.startNewSubPath (chevron.getTopLeft());
            p.lineTo (chevron.getCentreX(), chevron.getBottom());
            p.lineTo (chevron.getTopRight());
            g.setColour (t.textDim);
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (box.hasKeyboardFocus (true))
            {
                g.setColour (t.focusRing);
                g.drawRoundedRectangle (bounds.reduced (1.0f), t.radiusMd, 2.0f);
            }
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (juce::Rectangle<int> (8, 0, juce::jmax (0, box.getWidth() - 8 - 11 - 8), box.getHeight()));
            label.setBorderSize ({});
            label.setFont (getComboBoxFont (box));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                   bool highlighted, bool down) override
        {
            auto colour = background.withMultipliedBrightness (down ? 1.4f : highlighted ? 1.2f : 1.0f);
            auto bounds = b.getLocalBounds().toFloat().reduced (0.5f);
            const auto radius = themeManager.getTheme().cornerRadius;

            g.setColour (colour);
            g.fillRoundedRectangle (bounds, radius);
            g.setColour (b.findColour (juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle (bounds, radius, 1.0f);
        }

        /** Fits any size (V4 insets by a fixed 10 px); a range around zero, like
            pan, fills from zero rather than from the start. */
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
            const auto lineWidth = juce::jmax (1.5f, bounds.getWidth() * 0.12f);
            const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - lineWidth;
            const auto centre = bounds.getCentre();

            const auto zero = slider.getMinimum() < 0 && slider.getMaximum() > 0
                                ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
            const auto angleAt = [&] (float proportion) { return startAngle + proportion * (endAngle - startAngle); };
            const auto stroke = juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            juce::Path track, fill;
            track.addCentredArc (centre.x, centre.y, radius, radius, 0, startAngle, endAngle, true);
            fill.addCentredArc (centre.x, centre.y, radius, radius, 0, angleAt (zero), angleAt (position), true);

            g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
            g.strokePath (track, stroke);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (fill, stroke);

            const auto thumb = centre.getPointOnCircumference (radius, angleAt (position));
            g.setColour (slider.findColour (juce::Slider::thumbColourId));
            g.drawLine ({ centre, thumb }, lineWidth * 0.75f);
        }

    private:
        const ThemeManager& themeManager;
    };

    juce::Result parseHex (const juce::String& value, const juce::String& key, juce::Colour& out)
    {
        if (! value.startsWith ("#") || (value.length() != 7 && value.length() != 9)
            || ! value.substring (1).containsOnly ("0123456789abcdefABCDEF"))
            return juce::Result::fail ("colors." + key + " must be \"#rrggbb\", \"#rrggbbaa\" or \"$token\"");

        auto hex = value.substring (1);
        out = hex.length() == 6 ? juce::Colour ((juce::uint32) (0xff000000u | (juce::uint32) hex.getHexValue32()))
                                : juce::Colour::fromRGBA ((juce::uint8) hex.substring (0, 2).getHexValue32(),
                                                          (juce::uint8) hex.substring (2, 4).getHexValue32(),
                                                          (juce::uint8) hex.substring (4, 6).getHexValue32(),
                                                          (juce::uint8) hex.substring (6, 8).getHexValue32());
        return juce::Result::ok();
    }

    /** A colour is "#rrggbb[aa]" or "$name" of another colour in the same section. */
    juce::Result readColour (const juce::var& colours, const juce::String& key, juce::Colour& out, int depth = 0)
    {
        auto value = colours[juce::Identifier (key)].toString();

        if (value.startsWith ("$"))
        {
            const auto target = value.substring (1);

            if (depth > 8 || ! colours.hasProperty (juce::Identifier (target)))
                return juce::Result::fail ("colors." + key + " names an unknown colour: " + value);

            return readColour (colours, target, out, depth + 1);
        }

        return parseHex (value, key, out);
    }

    template <typename T>
    juce::Result readNumber (const juce::var& section, const juce::String& path, const juce::String& key, T& out)
    {
        auto value = section[juce::Identifier (key)];

        if (! (value.isInt() || value.isInt64() || value.isDouble()))
            return juce::Result::fail (path + "." + key + " must be a number");

        out = (T) value;
        return juce::Result::ok();
    }

    juce::Result readTypeStyle (const juce::var& type, const juce::String& key, TypeStyle& out)
    {
        auto node = type[juce::Identifier (key)];
        const auto path = "type." + key;

        if (! node.isObject())
            return juce::Result::fail (path + " must be an object");

        const auto family = node["family"].toString();

        if (family != "ui" && family != "mono")
            return juce::Result::fail (path + ".family must be \"ui\" or \"mono\"");

        TypeStyle style;
        style.mono = family == "mono";
        style.uppercase = (bool) node.getProperty ("uppercase", false);

        for (auto r : { readNumber (node, path, "size", style.size), readNumber (node, path, "weight", style.weight),
                        readNumber (node, path, "tracking", style.tracking) })
            if (r.failed())
                return r;

        out = style;
        return juce::Result::ok();
    }

    /** An elevation level: an array of { "x", "y", "blur", "color" } shadows. */
    juce::Result readElevation (const juce::var& elevation, const juce::var& colours, const juce::String& key,
                                std::vector<Shadow>& out)
    {
        auto list = elevation[juce::Identifier (key)];
        const auto path = "elevation." + key;

        if (! list.isArray())
            return juce::Result::fail (path + " must be an array of shadows");

        std::vector<Shadow> shadows;

        for (auto& node : *list.getArray())
        {
            Shadow shadow;
            const auto colour = node["color"].toString();
            const auto colourResult = colour.startsWith ("$") ? readColour (colours, colour.substring (1), shadow.colour)
                                                              : parseHex (colour, path + ".color", shadow.colour);

            for (auto r : { readNumber (node, path, "x", shadow.offset.x), readNumber (node, path, "y", shadow.offset.y),
                            readNumber (node, path, "blur", shadow.radius), colourResult })
                if (r.failed())
                    return r;

            shadows.push_back (shadow);
        }

        out = std::move (shadows);
        return juce::Result::ok();
    }

    /** base with each section of overlay laid over it, key by key. */
    juce::var mergeSections (const juce::var& base, const juce::var& overlay)
    {
        auto merged = juce::JSON::parse (juce::JSON::toString (base, true));

        if (auto* props = overlay.getDynamicObject())
        {
            for (auto& [name, value] : props->getProperties())
            {
                auto existing = merged[name];

                if (value.isObject() && existing.isObject())
                    for (auto& [key, entry] : value.getDynamicObject()->getProperties())
                        existing.getDynamicObject()->setProperty (key, entry);
                else
                    merged.getDynamicObject()->setProperty (name, value);
            }
        }

        return merged;
    }
}

//==============================================================================
/** The bundled typefaces, by family and weight. */
struct ThemeManager::Fonts
{
    struct Face
    {
        bool mono;
        int weight;
        const char* file;
        juce::Typeface::Ptr typeface;
    };

    std::vector<Face> faces
    {
        { false, 400, "fonts/Inter-Regular.ttf", {} },
        { false, 600, "fonts/Inter-SemiBold.ttf", {} },
        { false, 700, "fonts/Inter-Bold.ttf", {} },
        { true, 400, "fonts/IBMPlexMono-Regular.ttf", {} },
        { true, 500, "fonts/IBMPlexMono-Medium.ttf", {} },
        { true, 600, "fonts/IBMPlexMono-SemiBold.ttf", {} },
    };

    juce::Result load (const UIFileSource& files)
    {
        for (auto& face : faces)
        {
            if (face.typeface != nullptr)
                continue;

            juce::MemoryBlock data;

            if (auto r = files.readData (face.file, data); r.failed())
                return r;

            face.typeface = juce::Typeface::createSystemTypefaceFor (data.getData(), data.getSize());

            if (face.typeface == nullptr)
                return juce::Result::fail (juce::String ("Couldn't load the font ") + face.file);
        }

        return juce::Result::ok();
    }

    /** The loaded face of that family nearest the weight. */
    juce::Typeface::Ptr find (bool mono, int weight) const
    {
        const Face* best = nullptr;

        for (auto& face : faces)
            if (face.mono == mono && face.typeface != nullptr
                && (best == nullptr || std::abs (face.weight - weight) < std::abs (best->weight - weight)))
                best = &face;

        return best != nullptr ? best->typeface : nullptr;
    }
};

ThemeManager::ThemeManager (const UIFileSource& s, juce::String file)
    : source (s), themeFile (std::move (file)),
      fonts (std::make_unique<Fonts>()),
      lookAndFeel (std::make_unique<ResamperLookAndFeel> (*this))
{
}

ThemeManager::~ThemeManager() = default;

juce::Font ThemeManager::getFont (float scale) const
{
    return font (theme.body).withPointHeight (theme.fontSize * scale);
}

juce::Font ThemeManager::font (const TypeStyle& style) const
{
    auto typeface = fonts->find (style.mono, style.weight);
    return juce::Font ((typeface != nullptr ? juce::FontOptions (typeface) : juce::FontOptions())
                           .withPointHeight (style.size)
                           .withKerningFactor (style.size > 0 ? style.tracking / style.size : 0.0f));
}

juce::Font ThemeManager::numberFont (const TypeStyle& style) const
{
    auto mono = style;
    mono.mono = true;
    mono.tracking = 0;
    return font (mono);
}

juce::Result ThemeManager::parse (const juce::String& text, Theme& t, LayoutMetrics& m)
{
    juce::var json;

    if (auto r = juce::JSON::parse (text, json); r.failed())
        return juce::Result::fail ("Theme JSON: " + r.getErrorMessage());

    auto colours = json["colors"];
    auto type = json["type"];
    auto radius = json["radius"];
    auto elevation = json["elevation"];
    auto style = json["style"];
    auto geometry = json["metrics"];

    const std::pair<const char*, juce::Colour Theme::*> colourKeys[] =
    {
        { "bg-deep", &Theme::bgDeep }, { "bg-panel", &Theme::bgPanel }, { "bg-track", &Theme::bgTrack },
        { "bg-elevated", &Theme::bgElevated }, { "bg-slot", &Theme::bgSlot }, { "bg-hover", &Theme::bgHover },
        { "border", &Theme::border }, { "border-soft", &Theme::borderSoft },
        { "grid-bar", &Theme::gridBar }, { "grid-beat", &Theme::gridBeat },
        { "text-primary", &Theme::textPrimary }, { "text-secondary", &Theme::textSecondary },
        { "text-dim", &Theme::textDim }, { "text-on-accent", &Theme::textOnAccent },
        { "accent", &Theme::accent }, { "accent-hover", &Theme::accentHover }, { "accent-dim", &Theme::accentDim },
        { "focus-ring", &Theme::focusRing }, { "rec", &Theme::rec },
        { "state-warning", &Theme::stateWarning }, { "state-mute", &Theme::stateMute }, { "state-pre", &Theme::statePre },
        { "state-solo", &Theme::stateSolo }, { "state-polarity", &Theme::statePolarity },
        { "state-sidechain", &Theme::stateSidechain },
        { "playhead", &Theme::playhead }, { "meter-low", &Theme::meterLow }, { "meter-mid", &Theme::meterMid },
        { "meter-high", &Theme::meterHigh }, { "scrim", &Theme::scrim },

        { "background", &Theme::background }, { "panel", &Theme::panel }, { "text", &Theme::text },
        { "mutedText", &Theme::mutedText },
        { "laneA", &Theme::laneA }, { "laneB", &Theme::laneB },
        { "trackHeader", &Theme::trackHeader }, { "trackHeaderSelected", &Theme::trackHeaderSelected },
        { "mute", &Theme::mute }, { "solo", &Theme::solo }, { "armed", &Theme::armed },
        { "recording", &Theme::recording }, { "loop", &Theme::loop },
        { "clip", &Theme::clip }, { "clipSelected", &Theme::clipSelected }, { "clipText", &Theme::clipText }, { "waveform", &Theme::waveform },
        { "midiClip", &Theme::midiClip }, { "midiClipSelected", &Theme::midiClipSelected }, { "midiNote", &Theme::midiNote },
        { "pianoWhite", &Theme::pianoWhite }, { "pianoBlack", &Theme::pianoBlack },
        { "noteSelected", &Theme::noteSelected }, { "gridLine", &Theme::gridLine }, { "velocity", &Theme::velocity },
        { "ruler", &Theme::ruler }, { "error", &Theme::error },
    };

    const char* paletteKeys[] = { "clip-drums", "clip-bass", "clip-chords", "clip-pads", "clip-arp", "clip-vocal", "clip-fx" };
    const char* returnKeys[] = { "return-a", "return-b", "return-c", "return-d" };

    const std::pair<const char*, TypeStyle Theme::*> typeKeys[] =
    {
        { "fs-display", &Theme::display }, { "fs-heading", &Theme::heading }, { "fs-title", &Theme::title },
        { "fs-label", &Theme::label }, { "fs-body", &Theme::body }, { "fs-body-sm", &Theme::bodySm },
        { "fs-caption", &Theme::caption }, { "fs-micro", &Theme::micro },
    };

    const std::pair<const char*, float Theme::*> radiusKeys[] =
    {
        { "radius-xs", &Theme::radiusXs }, { "radius-sm", &Theme::radiusSm }, { "radius-md", &Theme::radiusMd },
        { "radius-lg", &Theme::radiusLg }, { "radius-xl", &Theme::radiusXl }, { "radius-2xl", &Theme::radius2xl },
    };

    const std::pair<const char*, int LayoutMetrics::*> metricKeys[] =
    {
        { "space-2xs", &LayoutMetrics::space2xs }, { "space-xs", &LayoutMetrics::spaceXs }, { "space-sm", &LayoutMetrics::spaceSm },
        { "space-md", &LayoutMetrics::spaceMd }, { "space-lg", &LayoutMetrics::spaceLg }, { "space-xl", &LayoutMetrics::spaceXl },
        { "space-2xl", &LayoutMetrics::space2xl }, { "space-3xl", &LayoutMetrics::space3xl },
        { "h-control-xs", &LayoutMetrics::controlXs }, { "h-control-sm", &LayoutMetrics::controlSm },
        { "h-control-md", &LayoutMetrics::controlMd }, { "h-control-lg", &LayoutMetrics::controlLg },
        { "h-transport", &LayoutMetrics::transportButton }, { "h-toolbar", &LayoutMetrics::toolbarHeight },
        { "icon-chip", &LayoutMetrics::iconChip }, { "icon-control", &LayoutMetrics::iconControl },
        { "icon-section", &LayoutMetrics::iconSection }, { "icon-toolbar", &LayoutMetrics::iconToolbar },
        { "h-topbar", &LayoutMetrics::topBarHeight }, { "w-track-header", &LayoutMetrics::trackHeaderWidth },
        { "w-inspector", &LayoutMetrics::inspectorWidth }, { "w-browser", &LayoutMetrics::browserWidth }, { "w-strip", &LayoutMetrics::stripWidth },
        { "w-strip-compact", &LayoutMetrics::stripCompactWidth }, { "w-strip-bus", &LayoutMetrics::stripBusWidth },

        { "transportHeight", &LayoutMetrics::transportHeight }, { "statusBarHeight", &LayoutMetrics::statusBarHeight },
        { "timelineHeight", &LayoutMetrics::timelineHeight }, { "trackHeight", &LayoutMetrics::trackHeight },
        { "clipHeaderHeight", &LayoutMetrics::clipHeaderHeight },
        { "inset", &LayoutMetrics::inset }, { "textPadding", &LayoutMetrics::textPadding },
        { "playheadWidth", &LayoutMetrics::playheadWidth }, { "clipResizeHandleWidth", &LayoutMetrics::clipResizeHandleWidth },
        { "trackControlHeight", &LayoutMetrics::trackControlHeight }, { "trackButtonWidth", &LayoutMetrics::trackButtonWidth },
        { "midiNoteHeight", &LayoutMetrics::midiNoteHeight },
        { "pianoKeyWidth", &LayoutMetrics::pianoKeyWidth }, { "pianoKeyHeight", &LayoutMetrics::pianoKeyHeight },
        { "pianoBlackKeyWidth", &LayoutMetrics::pianoBlackKeyWidth }, { "pianoScrollMargin", &LayoutMetrics::pianoScrollMargin },
        { "velocityLaneHeight", &LayoutMetrics::velocityLaneHeight },
        { "gridEighthPixels", &LayoutMetrics::gridEighthPixels }, { "gridSixteenthPixels", &LayoutMetrics::gridSixteenthPixels },
        { "h-plugin-titlebar", &LayoutMetrics::pluginTitleBarHeight }, { "h-plugin-toolbar", &LayoutMetrics::pluginToolbarHeight },
        { "h-plugin-footer", &LayoutMetrics::pluginFooterHeight }, { "window-cascade", &LayoutMetrics::windowCascade },
    };

    Theme newTheme;
    LayoutMetrics newMetrics;

    for (auto& [key, member] : colourKeys)
        if (auto r = readColour (colours, key, newTheme.*member); r.failed())
            return r;

    for (size_t i = 0; i < newTheme.trackPalette.size(); ++i)
        if (auto r = readColour (colours, paletteKeys[i], newTheme.trackPalette[i]); r.failed())
            return r;

    for (size_t i = 0; i < newTheme.returnColours.size(); ++i)
        if (auto r = readColour (colours, returnKeys[i], newTheme.returnColours[i]); r.failed())
            return r;

    for (auto& [key, member] : typeKeys)
        if (auto r = readTypeStyle (type, key, newTheme.*member); r.failed())
            return r;

    for (auto& [key, member] : radiusKeys)
        if (auto r = readNumber (radius, "radius", key, newTheme.*member); r.failed())
            return r;

    for (auto r : { readElevation (elevation, colours, "L1", newTheme.elevation1),
                    readElevation (elevation, colours, "L2", newTheme.elevation2),
                    readElevation (elevation, colours, "L3", newTheme.elevation3),
                    readNumber (style, "style", "cornerRadius", newTheme.cornerRadius),
                    readNumber (style, "style", "fontSize", newTheme.fontSize),
                    readNumber (style, "style", "opacity-disabled", newTheme.disabledOpacity) })
        if (r.failed())
            return r;

    for (auto& [key, member] : metricKeys)
        if (auto r = readNumber (geometry, "metrics", key, newMetrics.*member); r.failed())
            return r;

    t = newTheme;
    m = newMetrics;
    return juce::Result::ok();
}

juce::Result ThemeManager::readMerged (const juce::String& file, juce::var& json, int depth) const
{
    juce::String text;

    if (auto r = source.read (file, text); r.failed())
        return r;

    if (auto r = juce::JSON::parse (text, json); r.failed())
        return juce::Result::fail (file + ": Theme JSON: " + r.getErrorMessage());

    if (! json.isObject())
        return juce::Result::fail (file + ": Theme JSON must be an object");

    const auto base = json["extends"].toString();

    if (base.isEmpty())
        return juce::Result::ok();

    if (depth > 4)
        return juce::Result::fail (file + ": \"extends\" nests too deeply");

    juce::var baseJson;

    if (auto r = readMerged ("themes/" + base, baseJson, depth + 1); r.failed())
        return r;

    json = mergeSections (baseJson, json);
    return juce::Result::ok();
}

juce::Result ThemeManager::read (Theme& newTheme, LayoutMetrics& newMetrics) const
{
    juce::var json;

    if (auto r = readMerged (themeFile, json, 0); r.failed())
        return r;

    if (auto r = parse (juce::JSON::toString (json), newTheme, newMetrics); r.failed())
        return juce::Result::fail (themeFile + ": " + r.getErrorMessage());

    return juce::Result::ok();
}

juce::Result ThemeManager::load()
{
    if (auto r = fonts->load (source); r.failed())
        return r;

    lookAndFeel->setDefaultSansSerifTypeface (fonts->find (false, 400));

    Theme newTheme;
    LayoutMetrics newMetrics;

    if (auto r = read (newTheme, newMetrics); r.failed())
        return r;

    theme = newTheme;
    metrics = newMetrics;
    static_cast<ResamperLookAndFeel&> (*lookAndFeel).apply (theme);
    return juce::Result::ok();
}

juce::Result ThemeManager::reloadTheme()
{
    Theme newTheme;
    LayoutMetrics ignoredMetrics;

    if (auto r = read (newTheme, ignoredMetrics); r.failed())
        return r;

    theme = newTheme;
    static_cast<ResamperLookAndFeel&> (*lookAndFeel).apply (theme);
    listeners.call ([] (Listener& l) { l.themeChanged(); });
    return juce::Result::ok();
}

juce::Result ThemeManager::useTheme (juce::String file)
{
    auto previous = themeFile;
    themeFile = std::move (file);

    if (auto r = reloadTheme(); r.failed())
    {
        themeFile = std::move (previous);
        return r;
    }

    return juce::Result::ok();
}

} // namespace resamper
