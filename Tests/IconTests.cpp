#include "TestFixture.h"

#include "UI/Controls/Icons.h"

namespace resamper::test
{

/** The icon set of PRD §15.8: lucide icons by lucide name, sizes and colours by role. */
struct IconTests : juce::UnitTest
{
    IconTests() : juce::UnitTest ("Icons", "Resamper") {}

    /** The design's Icon/<name> components (PRD §15.8), group by group. */
    static juce::StringArray designIcons()
    {
        return juce::StringArray::fromTokens (
            // Transport & audio
            "play skip-back repeat rotate-ccw timer audio-lines audio-waveform activity waves spline chart-spline piano music-2 "
            // Navigation & disclosure
            "chevron-down chevron-right arrow-down arrow-left arrow-right arrow-up-right arrow-left-right corner-down-right "
            "ellipsis x maximize maximize-2 zoom-in zoom-out "
            // Editing tools
            "mouse-pointer-2 text-cursor pencil eraser scissors scissors-line-dashed magnet fold-vertical copy trash-2 plus "
            "undo-2 flag search "
            // Routing, racks & structure
            "git-merge layers network folder folder-open folder-minus list sliders-horizontal sliders-vertical link-2-off "
            "unlink circle-dot "
            // Files & visibility
            "file-music file-code download eye eye-off "
            // Plug-ins & windows
            "plug app-window external-link pin power cpu shield-check save redo-2 move-diagonal-2 square-dashed sparkles package "
            // Sidechain
            "key-round headphones funnel cable check arrow-right-to-line",
            " ", {});
    }

    /** The total alpha drawn by the icon at size px under a UI scale. */
    static double inkAt (Icon icon, float size, float scale)
    {
        const auto pixels = (int) std::ceil (size * scale) + 2;
        juce::Image image (juce::Image::ARGB, pixels, pixels, true, juce::SoftwareImageType());

        {
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (scale));
            drawIcon (g, icon, { 1.0f / scale, 1.0f / scale, size, size }, juce::Colours::white);
        }

        double ink = 0;

        for (int y = 0; y < pixels; ++y)
            for (int x = 0; x < pixels; ++x)
                ink += image.getPixelAt (x, y).getFloatAlpha();

        return ink;
    }

    void runTest() override
    {
        beginTest ("Every design icon is there by its lucide name, as a path inside the 24 x 24 box");
        {
            const auto names = designIcons();
            expectEquals (names.size(), 77);   // 90 components, 13 of them the phosphor legacy set

            for (auto& name : names)
            {
                const auto icon = iconNamed (name);
                expect (icon.has_value(), "missing: " + name);

                if (! icon)
                    continue;

                expectEquals (lucideName (*icon), name);
                const auto bounds = iconPath (*icon).getBounds();
                expect (! iconPath (*icon).isEmpty(), "empty: " + name);
                expect (juce::Rectangle<float> (-0.01f, -0.01f, 24.02f, 24.02f).contains (bounds),
                        name + " out of its box: " + bounds.toString());
            }

            expect (! iconNamed ("not-an-icon").has_value());
        }

        beginTest ("Every Icon has its lucide entry");
        {
            for (int i = 0; i <= (int) Icon::arrowRightToLine; ++i)
            {
                const auto name = lucideName ((Icon) i);
                expect (name.isNotEmpty(), "no entry for Icon " + juce::String (i));
                expect (iconNamed (name) == (Icon) i, name + " names another Icon");
                expect (! iconPath ((Icon) i).isEmpty(), "empty: " + name);
            }
        }

        beginTest ("The phosphor transport and browser-category glyphs have lucide equivalents");
        {
            for (auto* name : { "play", "pause", "square", "circle-dot", "skip-back", "crosshair", "music", "piano",
                                "volume-2", "sliders-vertical", "box", "puzzle", "drum" })
                expect (iconNamed (name).has_value(), juce::String ("missing: ") + name);

            expectEquals (lucideName (Icon::stop), juce::String ("square"));
            expectEquals (lucideName (Icon::record), juce::String ("circle-dot"));
            expectEquals (lucideName (Icon::follow), juce::String ("crosshair"));
        }

        beginTest ("Icons are lucide geometry, not the old hand-drawn shapes");
        {
            // lucide's play is a rounded triangle whose upright edge is x = 5 (the old one was x = 7);
            // x is two strokes corner to corner.
            expectWithinAbsoluteError (iconPath (Icon::play).getBounds().getX(), 5.0f, 0.01f);
            expectEquals (iconPath (Icon::x).getBounds().toString(), juce::Rectangle<float> (6.0f, 6.0f, 12.0f, 12.0f).toString());
            // cpu is a rounded square with two pins a side.
            expectEquals (iconPath (Icon::cpu).getBounds().toString(), juce::Rectangle<float> (2.0f, 2.0f, 20.0f, 20.0f).toString());
        }

        beginTest ("Sizes by role come from Layout Metrics: 10 chips, 12 controls, 14 sections, 16 transport / toolbar");
        {
            UIFileSource source;
            ThemeManager themes { source, "themes/dark.json" };
            expect (themes.load().wasOk());
            auto& m = themes.getMetrics();
            expectEquals (m.iconChip, 10);
            expectEquals (m.iconControl, 12);
            expectEquals (m.iconSection, 14);
            expectEquals (m.iconToolbar, 16);
        }

        beginTest ("Colours by role are Theme tokens");
        {
            UIFileSource source;
            ThemeManager themes { source, "themes/dark.json" };
            expect (themes.load().wasOk());
            auto& t = themes.getTheme();
            expect (iconColour (t, IconRole::normal) == t.textSecondary);
            expect (iconColour (t, IconRole::inactive) == t.textDim);
            expect (iconColour (t, IconRole::active) == t.accent);
            expect (iconColour (t, IconRole::onFilled) == t.textOnAccent);
            expect (iconColour (t, IconRole::sidechain) == t.stateSidechain);
            expectEquals (t.stateSidechain.toDisplayString (false), juce::String ("3FC9B0"));
        }

        beginTest ("Strokes are lucide's 2 / 24 of the size, never under one physical pixel");
        {
            expectEquals (iconStrokeWidth (12.0f, 1.0f), 1.0f);
            expectEquals (iconStrokeWidth (24.0f, 1.0f), 2.0f);
            expectEquals (iconStrokeWidth (24.0f, 2.0f), 2.0f);
            expectWithinAbsoluteError (iconStrokeWidth (9.0f, 2.0f), 0.75f, 1.0e-6f);
            expectWithinAbsoluteError (iconStrokeWidth (9.0f, 1.0f), 1.0f, 1.0e-6f);
            expectWithinAbsoluteError (iconStrokeWidth (10.0f, 0.8f), 1.25f, 1.0e-6f);
        }

        beginTest ("Icons are drawn as vectors at every UI scale, 80 to 200 %");
        {
            // The path is stroked at the physical resolution: twice the scale covers four times the pixels.
            const auto at100 = inkAt (Icon::plus, 16.0f, 1.0f);
            const auto at200 = inkAt (Icon::plus, 16.0f, 2.0f);
            expect (at100 > 0.0);
            expectWithinAbsoluteError (at200 / at100, 4.0, 0.4);

            // At 80 % a 10 px icon still gets a full pixel of stroke.
            const auto at80 = inkAt (Icon::plus, 10.0f, 0.8f);
            const auto armLength = 14.0 / 24.0 * 10.0 * 0.8;   // lucide's plus runs 5 to 19
            expect (at80 >= 2.0 * armLength - 1.0, "too faint at 80 %: " + juce::String (at80));
        }

        beginTest ("drawIcon with a centre and size draws the same as with its square");
        {
            juce::Image a (juce::Image::ARGB, 20, 20, true, juce::SoftwareImageType());
            juce::Image b (juce::Image::ARGB, 20, 20, true, juce::SoftwareImageType());

            {
                juce::Graphics g (a);
                drawIcon (g, Icon::keyRound, { 2.0f, 2.0f, 16.0f, 16.0f }, juce::Colours::white);
            }
            {
                juce::Graphics g (b);
                drawIcon (g, Icon::keyRound, juce::Point<float> (10.0f, 10.0f), 16.0f, juce::Colours::white);
            }

            bool same = true;

            for (int y = 0; y < 20; ++y)
                for (int x = 0; x < 20; ++x)
                    same = same && a.getPixelAt (x, y) == b.getPixelAt (x, y);

            expect (same);
        }
    }
};

static IconTests iconTests;

} // namespace resamper::test
