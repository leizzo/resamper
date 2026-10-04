#include "TopBar.h"
#include "Commands/AppCommands.h"
#include "Commands/ApplicationCommandTable.h"
#include "UI/Controls/Menus.h"

namespace resamper
{

namespace
{
    using View = ShellState::View;

    constexpr View tabViews[] = { View::session, View::arrange, View::mixer, View::pianoRoll, View::editor };

    /** CPU at or above this is an overload: the readout turns meter-high. */
    constexpr float overloadCpu = 0.9f;

    ContinuousValue::Spec tempoSpec()
    {
        ContinuousValue::Spec spec;
        spec.minimum = ApplicationModel::minTempo;
        spec.maximum = ApplicationModel::maxTempo;
        spec.defaultValue = 120.0;
        spec.format = ValueFormat::bpm();
        spec.wheelStep = 1.0;
        spec.unitsPerPixel = 1.0;        // PRD §6.1: 1 BPM per pixel, Shift 0.01
        spec.fineUnitsPerPixel = 0.01;
        return spec;
    }

    /** m:ss.s; a count-in before the start reads -m:ss.s. */
    juce::String clockText (double seconds)
    {
        const auto sign = seconds < -0.05 ? juce::String ("-") : juce::String();
        seconds = std::abs (seconds);
        const auto minutes = (int) (seconds / 60.0);
        const auto rest = seconds - minutes * 60.0;
        return sign + juce::String (minutes) + ":" + (rest < 10.0 ? "0" : "") + juce::String (rest, 1);
    }

    juce::String barsBeatsText (const BarsBeats& p)
    {
        return juce::String (p.bar) + ". " + juce::String (p.beat) + ". " + juce::String (p.sixteenth);
    }

    int stringWidth (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidthInt (font, text);
    }

    void paintBox (juce::Graphics& g, const Theme& theme, juce::Rectangle<int> r, float radius)
    {
        g.setColour (theme.bgElevated);
        g.fillRoundedRectangle (r.toFloat(), radius);
        g.setColour (theme.border);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), radius, 1.0f);
    }
}

TopBar::TopBar (const ApplicationModel& m, CommandRegistry& c, ThemeManager& tm, ShellState& s)
    : model (m), commands (c), themeManager (tm), shell (s),
      tempo (tm, tempoSpec()),
      signature (tm, "4 / 4"),
      prev (tm, "Return to Start", Icon::skipBack),
      record (tm, "Record (Shift: no count-in)", Icon::circleDot),
      automationArm (tm, "Automation Arm (arrives with automation recording)", Icon::spline),
      play (tm, "Play", Icon::play),
      stop (tm, "Stop (twice: return to start)", Icon::square),
      metronome (tm, "Metronome (C)", Icon::timer),
      follow (tm, "Follow", Icon::crosshair),
      views (tm, { "Session", "Arrange", "Mixer", "Piano Roll", "Editor" }, Segmented::Style::tabs)
{
    tempo.setTitle ("Tempo");
    tempo.setTooltip ("Tempo: drag, or double-click to type (T taps)");
    tempo.setValueStyle (TypeStyle { 15.0f, true, 500 });
    tempo.setSuffix ("BPM");
    tempo.setRaised (true);
    tempo.onChange = [this] (double bpm, bool continues) { commands.invoke (cmd::transportSetTempo, { bpm, continues }); };

    signature.setNumeric (true);
    signature.setTooltip ("Time signature");
    signature.onClick = [this] { showSignatureMenu(); };

    prev.setTooltip (tooltipFor ("Return to Start", "transport.returnToStart"));
    record.setTooltip (tooltipFor ("Record (Shift: no count-in)", "transport.record"));
    play.setTooltip (tooltipFor ("Play", "transport.togglePlay"));
    stop.setTooltip (tooltipFor ("Stop (twice: return to start)", "transport.togglePlay"));
    metronome.setTooltip (tooltipFor ("Metronome", "transport.toggleMetronome"));
    follow.setTooltip (tooltipFor ("Follow", "view.toggleFollow"));

    prev.onClick = [this] { commands.invoke (cmd::transportReturnToStart); };
    // Shift-click records at once, skipping the count-in.
    record.onClick = [this]
    {
        commands.invoke (cmd::transportRecord, { ! juce::ModifierKeys::getCurrentModifiers().isShiftDown() });
    };
    record.setIconColour (themeManager.getTheme().rec);
    record.setActiveColour (themeManager.getTheme().rec);
    automationArm.setOutlineWhenActive (true);
    automationArm.setEnabled (false);
    play.onClick = [this] { commands.invoke (cmd::transportPlay); };
    stop.onClick = [this] { commands.invoke (cmd::transportStop); };
    metronome.onClick = [this] { commands.invoke (cmd::transportToggleMetronome); };
    follow.onClick = [this] { commands.invoke (cmd::viewToggleFollow); };

    views.setTitle ("View");
    views.onChange = [this] (int index)
    {
        static constexpr CommandRef<> viewCommands[] = { cmd::viewSession, cmd::viewArrange, cmd::viewMixer,
                                                         cmd::viewPianoRoll, cmd::viewEditor };
        commands.invoke (viewCommands[index]);
    };

    for (auto* child : std::initializer_list<juce::Component*> { &tempo, &signature, &prev, &record, &automationArm, &play,
                                                                 &stop, &metronome, &follow, &views })
        addAndMakeVisible (child);

    for (auto* name : getMenuNames())
        menus.push_back ({ name, {} });

    model.addListener (this);
    shell.getState().addListener (this);
    refresh();
    startTimerHz (30);
}

TopBar::~TopBar()
{
    shell.getState().removeListener (this);
    model.removeListener (this);
}

void TopBar::refresh()
{
    tempo.setValue (model.getTempo());

    const auto sig = model.getTimeSignature();
    signature.setButtonText (juce::String (sig.numerator) + " / " + juce::String (sig.denominator));
    metronome.setToggleState (model.isMetronomeOn(), juce::dontSendNotification);
    follow.setToggleState (shell.isFollowing(), juce::dontSendNotification);

    for (int i = 0; i < (int) std::size (tabViews); ++i)
        if (tabViews[i] == shell.getView())
            views.setSelectedIndex (i, juce::dontSendNotification);
}

void TopBar::timerCallback()
{
    play.setToggleState (model.isPlaying(), juce::dontSendNotification);
    record.setToggleState (model.isRecording(), juce::dontSendNotification);

    // Smoothed, so the readout doesn't flicker.
    cpu += (model.getCpuUsage() - cpu) * 0.2f;
    repaint (positionBounds);
    repaint (cpuBounds);
}

void TopBar::showSignatureMenu()
{
    juce::PopupMenu menu;
    const auto current = model.getTimeSignature();

    for (auto sig : { std::pair (2, 4), std::pair (3, 4), std::pair (4, 4), std::pair (5, 4), std::pair (6, 8),
                         std::pair (7, 8), std::pair (9, 8), std::pair (12, 8) })
        menu.addItem (juce::String (sig.first) + " / " + juce::String (sig.second), true,
                      current.numerator == sig.first && current.denominator == sig.second,
                      [this, sig] { commands.invoke (cmd::transportSetTimeSignature, { sig.first, sig.second }); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&signature));
}

void TopBar::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    g.fillAll (theme.bgPanel);
    g.setColour (theme.borderSoft);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // Brand: the mark and the name.
    auto brand = brandBounds;
    auto mark = brand.removeFromLeft (22).withSizeKeepingCentre (22, 22).toFloat();
    g.setColour (theme.accent);
    g.fillRoundedRectangle (mark, 6.0f);
    drawIcon (g, Icon::audioWaveform, mark.reduced (4.0f), theme.textOnAccent);
    brand.removeFromLeft (8);
    g.setColour (theme.textPrimary);
    g.setFont (themeManager.font (TypeStyle { 15.0f, false, 700 }));
    g.drawText ("RESAMPER", brand, juce::Justification::centredLeft, false);

    // Menu titles.
    g.setFont (themeManager.font (TypeStyle { 12.5f, false, 400 }));
    g.setColour (theme.textSecondary);

    for (auto& menu : menus)
        if (! menu.bounds.isEmpty())
            g.drawText (compactMenu ? juce::String ("Menu") : menu.name, menu.bounds, juce::Justification::centredLeft, false);

    // Position: bars.beats.sixteenths, then the clock.
    if (! positionBounds.isEmpty())
    {
        paintBox (g, theme, positionBounds, theme.radiusLg);
        const auto seconds = model.getTransportPositionSeconds();
        const auto font = themeManager.numberFont (TypeStyle { 14.0f, true, 400 });
        auto r = positionBounds.reduced (12, 0);
        const auto bars = barsBeatsText (model.toBarsBeats (seconds));

        g.setFont (font);
        g.setColour (theme.accent);
        g.drawText (bars, r.removeFromLeft (stringWidth (font, bars)), juce::Justification::centredLeft, false);

        if (showTime)
        {
            r.removeFromLeft (8);
            g.setColour (theme.border);
            g.fillRect (r.removeFromLeft (1).withSizeKeepingCentre (1, 14));
            r.removeFromLeft (8);
            g.setColour (theme.textSecondary);
            g.drawText (clockText (seconds), r, juce::Justification::centredLeft, false);
        }
    }

    // CPU.
    if (! cpuBounds.isEmpty())
    {
        paintBox (g, theme, cpuBounds, theme.radiusLg);
        auto r = cpuBounds.reduced (12, 0);
        const auto text = juce::String (juce::roundToInt (cpu * 100.0f)) + "%";
        const auto font = themeManager.numberFont (TypeStyle { 13.0f, true, 400 });
        g.setFont (font);
        g.setColour (cpu >= overloadCpu ? theme.meterHigh : theme.textSecondary);
        g.drawText (text, r.removeFromLeft (stringWidth (font, text)), juce::Justification::centredLeft, false);
        drawStyledText (g, themeManager, "CPU", TypeStyle { theme.caption.size, false, 400 }, r.withTrimmedLeft (8),
                        juce::Justification::centredLeft, theme.textDim);
    }

    paintBox (g, theme, viewsBox, 7.0f);
}

void TopBar::resized()
{
    auto& metrics = themeManager.getMetrics();
    auto& theme = themeManager.getTheme();
    const auto button = metrics.transportButton;
    const auto gap = 14, statusGap = 12, groupGap = metrics.space2xl;
    const auto menuFont = themeManager.font (TypeStyle { 12.5f, false, 400 });
    const auto numberFont = themeManager.numberFont (TypeStyle { 14.0f, true, 400 });

    const auto brandWidth = 22 + 8 + stringWidth (themeManager.font (TypeStyle { 15.0f, false, 700 }), "RESAMPER");
    auto menuWidth = [&] (bool compact)
    {
        if (compact)
            return 20 + stringWidth (menuFont, "Menu");

        int w = 20;

        for (auto& m : menus)
            w += stringWidth (menuFont, m.name) + 18;

        return w - 18;
    };

    const auto tempoWidth = 24 + stringWidth (themeManager.numberFont (TypeStyle { 15.0f, true, 500 }), "000.00")
                          + 6 + stringWidth (themeManager.font (theme.caption), "BPM");
    const auto signatureWidth = signature.getIdealWidth();
    const auto transportWidth = tempoWidth + signatureWidth + 5 * button + 6 * gap;

    auto positionWidth = [&] (bool time)
    {
        return 24 + stringWidth (numberFont, "000. 0. 0") + (time ? 17 + stringWidth (numberFont, "00:00.0") : 0);
    };
    const auto cpuWidth = 24 + stringWidth (themeManager.numberFont (TypeStyle { 13.0f, true, 400 }), "100%")
                        + 8 + stringWidth (themeManager.font (theme.caption), "CPU");
    const auto viewsWidth = views.getIdealWidth() + 6;
    auto statusWidth = [&] (bool time) { return positionWidth (time) + 2 * 32 + cpuWidth + viewsWidth + 4 * statusGap; };

    // Responsive: past 1280 px everything fits; below, the menu folds, then the clock goes.
    const auto available = getWidth() - 2 * groupGap;
    compactMenu = brandWidth + menuWidth (false) + transportWidth + statusWidth (true) + 2 * groupGap > available;
    showTime = brandWidth + menuWidth (compactMenu) + transportWidth + statusWidth (true) + 2 * groupGap <= available;

    auto r = getLocalBounds().reduced (groupGap, 0);
    const auto centreY = [&] (juce::Rectangle<int> area, int h) { return area.withSizeKeepingCentre (area.getWidth(), h); };

    // Left: brand and menu.
    brandBounds = r.removeFromLeft (brandWidth);
    r.removeFromLeft (20);

    for (auto& m : menus)
        m.bounds = {};

    if (compactMenu)
    {
        menus.front().bounds = r.removeFromLeft (stringWidth (menuFont, "Menu"));
    }
    else
    {
        for (auto& m : menus)
        {
            m.bounds = r.removeFromLeft (stringWidth (menuFont, m.name));
            r.removeFromLeft (18);
        }
    }

    // Right: status, laid out from the right edge.
    auto right = getLocalBounds().reduced (groupGap, 0);
    viewsBox = centreY (right.removeFromRight (viewsWidth), button);
    views.setBounds (viewsBox.reduced (3));
    right.removeFromRight (statusGap);
    cpuBounds = centreY (right.removeFromRight (cpuWidth), button);
    right.removeFromRight (statusGap);
    follow.setBounds (centreY (right.removeFromRight (32), 32));
    right.removeFromRight (statusGap);
    metronome.setBounds (centreY (right.removeFromRight (32), 32));
    right.removeFromRight (statusGap);
    positionBounds = centreY (right.removeFromRight (positionWidth (showTime)), button);

    // Centre: tempo and transport, in the middle of the space left between.
    const auto leftEdge = menus.back().bounds.isEmpty() ? menus.front().bounds.getRight() : menus.back().bounds.getRight();
    const auto freeLeft = leftEdge + groupGap, freeRight = positionBounds.getX() - groupGap;
    auto centre = juce::Rectangle<int> (juce::jmax (freeLeft, (freeLeft + freeRight - transportWidth) / 2), 0,
                                        transportWidth, getHeight());

    tempo.setBounds (centreY (centre.removeFromLeft (tempoWidth), button));
    centre.removeFromLeft (gap);
    signature.setBounds (centreY (centre.removeFromLeft (signatureWidth), button));

    for (auto* b : { &prev, &record, &automationArm, &play, &stop })
    {
        centre.removeFromLeft (gap);
        b->setBounds (centreY (centre.removeFromLeft (button), button));
    }
}

void TopBar::mouseDown (const juce::MouseEvent& e)
{
    for (auto& m : menus)
    {
        if (m.bounds.expanded (6, getHeight()).contains (e.getPosition()) && ! m.bounds.isEmpty() && onMenu)
        {
            const auto area = localAreaToGlobal (m.bounds.withY (0).withHeight (getHeight()));

            if (compactMenu)
            {
                // One title for all menus: each becomes a submenu.
                onMenu ({}, area);
                return;
            }

            onMenu (m.name, area);
            return;
        }
    }
}

} // namespace resamper
