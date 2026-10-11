#include "TestFixture.h"

#include "UI/Theme/UIFileSource.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper::test
{

/** The design-system tokens (PRD §15) as the Theme and Layout Metrics load them. */
struct ThemeTests : juce::UnitTest
{
    ThemeTests() : juce::UnitTest ("Theme", "Resamper") {}

    struct Setup
    {
        UIFileSource source;
        ThemeManager themes { source, "themes/dark.json" };
        juce::Result loaded { themes.load() };
    };

    void expectColour (juce::Colour actual, const char* hex, const juce::String& what)
    {
        const auto rgb = juce::String (hex).substring (1, 7).toUpperCase();
        const auto alpha = juce::String (hex).length() == 9 ? juce::String (hex).substring (7).toUpperCase() : juce::String ("FF");
        expectEquals (actual.toDisplayString (true), alpha + rgb, what);
    }

    void runTest() override
    {
        beginTest ("dark.json loads every PRD §15.1 colour token with its PRD value");
        {
            Setup s;
            expect (s.loaded.wasOk(), s.loaded.getErrorMessage());
            auto& t = s.themes.getTheme();

            const std::pair<juce::Colour, const char*> expected[] =
            {
                { t.bgDeep, "#141416" }, { t.bgPanel, "#1D1D20" }, { t.bgTrack, "#212125" }, { t.bgElevated, "#26262B" },
                { t.bgSlot, "#191A1D" }, { t.bgHover, "#2E2E34" }, { t.border, "#34343B" }, { t.borderSoft, "#2A2A2F" },
                { t.gridBar, "#34343B" }, { t.gridBeat, "#24252A" },
                { t.textPrimary, "#EAEAED" }, { t.textSecondary, "#9A9AA2" }, { t.textDim, "#66666E" }, { t.textOnAccent, "#141416" },
                { t.accent, "#C6F135" }, { t.accentHover, "#D4F75A" }, { t.accentDim, "#8FA83A" }, { t.focusRing, "#C6F13599" },
                { t.rec, "#F0503C" }, { t.stateWarning, "#E8A33D" }, { t.stateMute, "#E8A33D" }, { t.statePre, "#5B8DEF" },
                { t.stateSolo, "#5B8DEF" }, { t.statePolarity, "#E8C53D" }, { t.stateSidechain, "#3FC9B0" },
                { t.playhead, "#C6F135" },
                { t.meterLow, "#5FBF6B" }, { t.meterMid, "#E8C53D" }, { t.meterHigh, "#F0503C" },
                { t.trackPalette[0], "#E8A33D" }, { t.trackPalette[1], "#E0564F" }, { t.trackPalette[2], "#9B6DD6" },
                { t.trackPalette[3], "#5B8DEF" }, { t.trackPalette[4], "#4FC4D9" }, { t.trackPalette[5], "#D96BA0" },
                { t.trackPalette[6], "#5FBF6B" },
                { t.returnColours[0], "#8E97AD" }, { t.returnColours[1], "#AD9A8E" },
                { t.returnColours[2], "#9A9AA2" }, { t.returnColours[3], "#9A9AA2" },
            };

            for (auto& [colour, hex] : expected)
                expectColour (colour, hex, hex);
        }

        beginTest ("A legacy key that names a token with $ takes the token's value");
        {
            Setup s;
            auto& t = s.themes.getTheme();
            expect (t.background == t.bgDeep);
            expect (t.panel == t.bgPanel);
            expect (t.text == t.textPrimary);
        }

        beginTest ("An unknown $token fails loudly");
        {
            juce::String json;
            UIFileSource source;
            expect (source.read ("themes/dark.json", json).wasOk());
            json = json.replace ("\"$bg-deep\"", "\"$no-such-token\"");

            Theme theme;
            LayoutMetrics metrics;
            auto r = ThemeManager::parse (json, theme, metrics);
            expect (r.failed());
            expect (r.getErrorMessage().contains ("no-such-token"), r.getErrorMessage());
        }

        beginTest ("A colour with a non-hex digit fails loudly");
        {
            juce::String json;
            UIFileSource source;
            expect (source.read ("themes/dark.json", json).wasOk());
            json = json.replace ("\"#141416\"", "\"#14141g\"");

            Theme theme;
            LayoutMetrics metrics;
            auto r = ThemeManager::parse (json, theme, metrics);
            expect (r.failed());
            expect (r.getErrorMessage().contains ("#rrggbb"), r.getErrorMessage());
        }

        beginTest ("Type tokens carry size, family, weight, case and tracking (§15.2)");
        {
            Setup s;
            auto& t = s.themes.getTheme();
            expectEquals (t.display.size, 26.0f);
            expect (t.display.mono);
            expectEquals (t.display.weight, 500);
            expectEquals (t.heading.size, 14.0f);
            expectEquals (t.heading.weight, 700);
            expectEquals (t.body.size, 10.5f);
            expect (! t.body.mono);
            expectEquals (t.caption.size, 9.0f);
            expect (t.caption.uppercase);
            expectEquals (t.caption.tracking, 0.8f);
            expectEquals (t.micro.size, 7.5f);
            expectEquals (t.micro.weight, 700);
            expectEquals (t.micro.tracking, 0.5f);
            expectEquals (t.caption.apply ("Racks"), juce::String ("RACKS"));
            expectEquals (t.title.apply ("Racks"), juce::String ("Racks"));
        }

        beginTest ("Inter and IBM Plex Mono are bundled; numbers are mono by default");
        {
            Setup s;
            auto& t = s.themes.getTheme();
            expectEquals (s.themes.font (t.title).getTypefacePtr()->getName(), juce::String ("Inter"));
            expectEquals (s.themes.font (t.display).getTypefacePtr()->getName(), juce::String ("IBM Plex Mono"));
            expectEquals (s.themes.numberFont (t.title).getTypefacePtr()->getName(), juce::String ("IBM Plex Mono"));
            expectEquals (s.themes.numberFont (t.title).getHeightInPoints(), s.themes.font (t.title).getHeightInPoints());
        }

        beginTest ("Radius and elevation scales are Theme entries");
        {
            Setup s;
            auto& t = s.themes.getTheme();
            expectEquals (t.radiusXs, 2.0f);
            expectEquals (t.radiusSm, 3.0f);
            expectEquals (t.radiusMd, 4.0f);
            expectEquals (t.radiusLg, 6.0f);
            expectEquals (t.radiusXl, 8.0f);
            expectEquals (t.radius2xl, 10.0f);
            expectEquals (t.elevation1.front().radius, 8);
            expectEquals (t.elevation1.front().offset.y, 3);
            expectEquals ((int) t.elevation2.size(), 2);
            expectEquals (t.disabledOpacity, 0.4f);
        }

        beginTest ("Spacing and sizing scales are Layout Metrics");
        {
            Setup s;
            auto& m = s.themes.getMetrics();
            const std::pair<int, int> expected[] =
            {
                { m.space2xs, 2 }, { m.spaceXs, 4 }, { m.spaceSm, 6 }, { m.spaceMd, 8 }, { m.spaceLg, 10 },
                { m.spaceXl, 12 }, { m.space2xl, 16 }, { m.space3xl, 24 },
                { m.controlXs, 16 }, { m.controlSm, 20 }, { m.controlMd, 22 }, { m.controlLg, 26 },
                { m.transportButton, 34 }, { m.toolbarHeight, 40 }, { m.topBarHeight, 52 },
                { m.trackHeaderWidth, 200 }, { m.inspectorWidth, 248 },
                { m.stripWidth, 145 }, { m.stripCompactWidth, 86 }, { m.stripBusWidth, 104 },
            };

            for (auto& [actual, want] : expected)
                expectEquals (actual, want);
        }

        beginTest ("light.json extends dark.json: its own colours win, missing tokens fall back");
        {
            Setup s;
            const auto darkAccentHover = s.themes.getTheme().accentHover;
            expect (s.themes.useTheme ("themes/light.json").wasOk());
            expect (s.themes.getTheme().background != s.themes.getTheme().bgDeep);
            expect (s.themes.getTheme().accentHover == darkAccentHover);
        }

        beginTest ("Menu chrome is Theme entries and Layout Metrics (#179)");
        {
            Setup s;
            auto& t = s.themes.getTheme();
            expectColour (t.menuBackground, "#26262BF2", "menuBackground");
            expectColour (t.menuOutline, "#34343B", "menuOutline");
            expectColour (t.menuHighlight, "#34343B", "menuHighlight");
            expectEquals (s.themes.getMetrics().menuItemHeight, 26);
            expectEquals (s.themes.getMetrics().iconMenu, 13);

            expect (s.themes.useTheme ("themes/light.json").wasOk());
            auto& light = s.themes.getTheme();
            expectColour (light.menuBackground, "#FFFFFFF2", "light menuBackground");
            expectColour (light.menuOutline, "#00000022", "light menuOutline");
            expectColour (light.menuHighlight, "#0000000F", "light menuHighlight");
        }

        beginTest ("A menu has a 6 px border, 28-high rows and 9-high separators, and is as wide as its content");
        {
            Setup s;
            auto& lf = s.themes.getLookAndFeel();
            expectEquals (lf.getPopupMenuBorderSize(), 6);

            auto size = [&lf] (const juce::String& text, bool separator)
            {
                int w = 0, h = 0;
                lf.getIdealPopupMenuItemSize (text, separator, 0, w, h);
                return std::pair { w, h };
            };

            expectEquals (size ("Add MIDI Track", false).second, 28);
            expectEquals (size ({}, true).second, 9);

            // 8 padding, 13 icon, 8 gap and 8 padding round the label at least
            expectGreaterThan (size ("Add MIDI Track", false).first, 37);
            expectGreaterThan (size ("Add MIDI Track and More", false).first, size ("Add MIDI Track", false).first);
            expectGreaterThan (size ("Add MIDI Track   " + juce::String (juce::CharPointer_UTF8 ("\xe2\x87\xa7\xe2\x8c\x98T")), false).first,
                               size ("Add MIDI Track", false).first);
        }

        beginTest ("A menu paints a rounded menuBackground with a menuOutline, and the hovered row in menuHighlight");
        {
            Setup s;
            auto& t = s.themes.getTheme();
            auto& lf = s.themes.getLookAndFeel();
            expect (lf.findColour (juce::PopupMenu::backgroundColourId) == t.menuBackground);
            expect (lf.findColour (juce::PopupMenu::highlightedBackgroundColourId) == t.menuHighlight);
            expect (lf.findColour (juce::PopupMenu::highlightedTextColourId) == t.text);

            juce::Image window (juce::Image::ARGB, 120, 60, true);
            {
                juce::Graphics g (window);
                lf.drawPopupMenuBackground (g, 120, 60);
            }
            expect (window.getPixelAt (0, 0).getAlpha() == 0, "the corner is cut by the radius");
            expect (window.getPixelAt (60, 0) == t.menuOutline, "the top edge is the outline");
            // A translucent colour comes back through the image's premultiplied pixels.
            juce::Image stored (juce::Image::ARGB, 1, 1, true);
            stored.setPixelAt (0, 0, t.menuBackground);
            expect (window.getPixelAt (60, 30) == stored.getPixelAt (0, 0), "the inside is the background");

            juce::Image row (juce::Image::ARGB, 120, 28, true);
            {
                juce::Graphics g (row);
                lf.drawPopupMenuItem (g, row.getBounds(), false, true, true, false, false, "X", {}, nullptr, nullptr);
            }
            expect (row.getPixelAt (60, 0).getAlpha() == 0, "1 px above the highlight");
            expect (row.getPixelAt (110, 14) == t.menuHighlight, "the highlight");
            expect (row.getPixelAt (119, 14).getAlpha() == 0, "the menu's 6 px border beside the highlight");

            // A disabled row's image is drawn at disabledOpacity, whoever made it.
            juce::DrawableRectangle square;
            square.setRectangle (juce::Rectangle<float> (0, 0, 13, 13));
            square.setFill (juce::Colours::white);
            juce::Image disabled (juce::Image::ARGB, 120, 28, true);
            {
                juce::Graphics g (disabled);
                lf.drawPopupMenuItem (g, disabled.getBounds(), false, false, false, false, false, "X", {}, &square, nullptr);
            }
            expectEquals ((int) disabled.getPixelAt (20, 14).getAlpha(), juce::roundToInt (255 * t.disabledOpacity));
        }
    }
};

static ThemeTests themeTests;

} // namespace resamper::test
