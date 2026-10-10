#include "PluginWindow.h"

#include "Commands/CommandRegistry.h"
#include "Commands/EditCommands.h"
#include "Commands/PluginCommands.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include "UI/Localisation.h"

namespace resamper
{

namespace
{
    // Design: PluginWindow. Toolbar padding 0 10, gap 7; footer padding 0 8 0 12, gap 8 (the title
    // bar's are FloatingDeviceWindow's). The bar heights are Layout Metrics.
    constexpr int toolbarPadding = 10, footerPaddingLeft = 12, footerPaddingRight = 8, bypassWidth = 46, controlHeight = 22,
                  presetStepWidth = 18, presetNameWidth = 120, slotWidth = 22, slotHeight = 16, copyWidth = 66,
                  statsWidth = 92, sandboxWidth = 16, scaleWidth = 120, scaleHeight = 18, gripSize = 14,
                  minFrameWidth = 565, loadingHeight = 180, statusPollMs = 500, footerGap = 8,
                  slotWellPadding = 2, slotsGap = 6;

    // Parameters: an icon button as wide as the preset steps. The design's 540 px minimum frame
    // grows by it and a gap, so the stats keep their full width.
    constexpr int parametersWidth = presetStepWidth;

    // The format badge in the title: 12 px plug, the badge pads 0 5 inside a 1 px border and is 13 high.
    constexpr int plugGlyph = 12, badgePadding = 5, badgeHeight = 13;

    // The vendor area while loading or failed: a 60 px block lifted 10 px above centre; a 22 px row with
    // an 18 px spinner (2 px stroke, three quarters of a turn, one turn a second), 8 px, then 16 px text
    // lines. Retry and Run in-process sit 34 px below centre, 220 wide at most, 8 px apart.
    constexpr int stateBlockHeight = 60, stateBlockLift = 10, spinnerRow = 22, spinnerSize = 18, stateGap = 8,
                  stateLineHeight = 16, stateButtonsWidth = 220, stateButtonsOffset = 34, stateButtonsGap = 8,
                  spinnerPeriodMs = 1000;
    constexpr float spinnerStroke = 2.0f, spinnerSweep = 0.75f;

    const TypeStyle vendorStyle { 9.5f, false, 400 }, badgeStyle { 7.5f, true, 600 }, statsStyle { 9.0f, true, 400 },
                    footerStyle { 8.5f, true, 400 }, stateStyle { 11.0f, false, 400 }, stateDetailStyle { 9.5f, false, 400 };

    const juce::String middleDot (juce::CharPointer_UTF8 ("\xc2\xb7"));
    const juce::String rightArrow (juce::CharPointer_UTF8 ("\xe2\x86\x92"));

    juce::String vendorOf (const PluginInfo& info)
    {
        return info.manufacturer.isNotEmpty() ? info.manufacturer : TRANS ("Unknown vendor");
    }

    /** Where the plug-in runs, as the footer says it. */
    juce::String processText (const HostingState& state)
    {
        switch (state.kind)
        {
            case HostingState::Kind::loading:     return TRANS ("loading");
            case HostingState::Kind::sandboxed:   return TRANS ("out-of-process");
            case HostingState::Kind::inProcess:   return TRANS ("in-process");
            case HostingState::Kind::crashed:     return TRANS ("crashed");
            case HostingState::Kind::failed:      return TRANS ("not loaded");
            case HostingState::Kind::missing:     return TRANS ("missing");
        }

        return {};
    }

    /** The toolbar's sandbox status. */
    juce::String sandboxText (const HostingState& state)
    {
        switch (state.kind)
        {
            case HostingState::Kind::loading:     return TRANS ("Loading");
            case HostingState::Kind::sandboxed:   return TRANS ("Sandboxed: out-of-process");
            case HostingState::Kind::inProcess:   return TRANS ("Not sandboxed: in-process");
            case HostingState::Kind::crashed:     return TRANS ("Crashed: its sandbox died");
            case HostingState::Kind::failed:      return TRANS ("Not loaded");
            case HostingState::Kind::missing:     return TRANS ("Missing: not installed");
        }

        return {};
    }

    /** The plug-in's own editor inside the engine's wrapper, if it is a JUCE AudioProcessorEditor. */
    juce::AudioProcessorEditor* processorEditorIn (juce::Component* c)
    {
        if (c == nullptr)
            return nullptr;

        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (c))
            return editor;

        for (auto* child : c->getChildren())
            if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (child))
                return editor;

        return nullptr;
    }

    /** Shown when the plug-in has no editor of its own. */
    struct NoEditor : juce::Component
    {
        explicit NoEditor (ThemeManager& tm) : themeManager (tm)
        {
            setSize (minFrameWidth, loadingHeight);
        }

        void paint (juce::Graphics& g) override
        {
            auto& theme = themeManager.getTheme();
            drawStyledText (g, themeManager, TRANS ("This plug-in has no editor of its own"), stateStyle, getLocalBounds(),
                            juce::Justification::centred, theme.textSecondary);
        }

        ThemeManager& themeManager;
    };
}

//==============================================================================
/** Host text a screen reader reads: the stats, the sandbox status, the footer. Paints itself. */
class PluginWindow::Readout : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    enum class Kind { stats, sandbox, footer };

    Readout (ThemeManager& tm, Kind k) : themeManager (tm), kind (k)
    {
        setInterceptsMouseClicks (false, false);
    }

    void setText (const juce::String& t, bool good = false)
    {
        if (t == text && good == positive)
            return;

        text = t;
        positive = good;
        setTitle (t);
        setTooltip (t);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& theme = themeManager.getTheme();

        if (kind == Kind::sandbox)
            drawIcon (g, Icon::shieldCheck, getLocalBounds().toFloat().withSizeKeepingCentre (11.0f, 11.0f),
                      positive ? theme.meterLow : theme.textDim);
        else
            drawNumber (g, themeManager, text, kind == Kind::stats ? statsStyle : footerStyle, getLocalBounds(),
                        kind == Kind::stats ? juce::Justification::centredRight : juce::Justification::centredLeft, theme.textDim);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText);
    }

private:
    ThemeManager& themeManager;
    Kind kind;
    juce::String text;
    bool positive = false;
};

//==============================================================================
/** The footer's resize grip: dragging it resizes the plug-in's editor (within its own limits). */
class PluginWindow::Grip : public juce::Component
{
public:
    explicit Grip (PluginWindow& w) : window (w)
    {
        setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor);
        setTitle (TRANS ("Resize"));
    }

    void paint (juce::Graphics& g) override
    {
        auto& theme = window.themeManager.getTheme();
        g.setColour (theme.textDim);
        const auto r = getLocalBounds().toFloat().reduced (2.0f);

        for (float d = 0; d < r.getWidth(); d += 4.0f)
            g.drawLine (r.getRight() - d, r.getBottom(), r.getRight(), r.getBottom() - d, 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (auto* editor = processorEditorIn (window.vendor.get()))
            startSize = { editor->getWidth(), editor->getHeight() };
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* editor = processorEditorIn (window.vendor.get());

        if (editor == nullptr)
            return;

        const auto factor = (float) window.uiScale / 100.0f;
        auto bounds = editor->getBounds().withSize (startSize.x + juce::roundToInt ((float) e.getDistanceFromDragStartX() / factor),
                                                   startSize.y + juce::roundToInt ((float) e.getDistanceFromDragStartY() / factor));

        if (auto* constrainer = editor->getConstrainer())
            constrainer->checkBounds (bounds, editor->getBounds(), {}, false, false, true, true);

        editor->setSize (juce::jmax (8, bounds.getWidth()), juce::jmax (8, bounds.getHeight()));
    }

private:
    PluginWindow& window;
    juce::Point<int> startSize;
};

//==============================================================================
PluginWindow::PluginWindow (const PluginRack& r, CommandRegistry& c, ThemeManager& tm, const PluginInfo& info,
                            const HostingState& state, const juce::String& track)
    : FloatingDeviceWindow (tm, info, track, "PluginWindow"), rack (r), commands (c)
{
    using Kind = ChromeButton::Kind;
    bypass = std::make_unique<ChromeButton> (tm, TRANS ("Bypass"), Kind::bypass);
    previousPreset = std::make_unique<ChromeButton> (tm, TRANS ("Previous preset"), Kind::icon, Icon::chevronLeft);
    presetName = std::make_unique<ChromeButton> (tm, TRANS ("Presets"), Kind::text, Icon::chevronDown);
    nextPreset = std::make_unique<ChromeButton> (tm, TRANS ("Next preset"), Kind::icon, Icon::chevronRight);
    savePreset = std::make_unique<ChromeButton> (tm, TRANS ("Save preset"), Kind::icon, Icon::save);
    // The A / B slots read as on hardware in every UI Language.
    slotA = std::make_unique<ChromeButton> (tm, "A", Kind::slot);
    slotB = std::make_unique<ChromeButton> (tm, "B", Kind::slot);
    copyAToB = std::make_unique<ChromeButton> (tm, tr ("Copy A%1B", rightArrow), Kind::text);
    undo = std::make_unique<ChromeButton> (tm, TRANS ("Undo"), Kind::icon, Icon::undo2);
    redo = std::make_unique<ChromeButton> (tm, TRANS ("Redo"), Kind::icon, Icon::redo2);
    parameters = std::make_unique<ChromeButton> (tm, TRANS ("Parameters"), Kind::icon, Icon::slidersHorizontal);
    retry = std::make_unique<ChromeButton> (tm, TRANS ("Retry"), Kind::text);
    runInProcess = std::make_unique<ChromeButton> (tm, TRANS ("Run in-process"), Kind::text);
    stats = std::make_unique<Readout> (tm, Readout::Kind::stats);
    sandbox = std::make_unique<Readout> (tm, Readout::Kind::sandbox);
    footerInfo = std::make_unique<Readout> (tm, Readout::Kind::footer);
    scale = std::make_unique<Segmented> (tm, juce::StringArray { "100%", "150%", "200%" });
    grip = std::make_unique<Grip> (*this);

    slotA->setTitle (TRANS ("A/B compare: A"));
    slotB->setTitle (TRANS ("A/B compare: B"));
    scale->setTitle (TRANS ("UI scale"));

    bypass->setComponentID ("bypass");
    presetName->setComponentID ("preset");
    slotA->setComponentID ("slotA");
    slotB->setComponentID ("slotB");
    copyAToB->setComponentID ("copyAToB");
    parameters->setComponentID ("parameters");
    parameters->accentWhenOn = true;
    retry->setComponentID ("retry");
    runInProcess->setComponentID ("runInProcess");
    scale->setComponentID ("uiScale");
    grip->setComponentID ("resizeGrip");

    bypass->onClick = [this] { commands.invoke (cmd::pluginSetBypassed, { plugin.trackId, plugin.id, plugin.enabled }); };
    previousPreset->onClick = [this] { stepPreset (-1); };
    nextPreset->onClick = [this] { stepPreset (1); };
    presetName->onClick = [this] { showPresetMenu(); };
    savePreset->onClick = [this] { askPresetName(); };
    slotA->onClick = [this] { commands.invoke (cmd::pluginSelectAB, { plugin.id, 0 }); };
    slotB->onClick = [this] { commands.invoke (cmd::pluginSelectAB, { plugin.id, 1 }); };
    copyAToB->onClick = [this] { commands.invoke (cmd::pluginCopyAToB, { plugin.trackId, plugin.id }); };
    undo->onClick = [this] { commands.invoke (cmd::editUndo); };
    redo->onClick = [this] { commands.invoke (cmd::editRedo); };
    parameters->onClick = [this] { showParameters (! isShowingParameters()); };
    retry->onClick = [this] { commands.invoke (cmd::pluginReload, { plugin.trackId, plugin.id }); };
    runInProcess->onClick = [this] { if (onRunInProcess) onRunInProcess(); };
    scale->onChange = [this] (int index)
    {
        setUiScale (index == 2 ? 200 : index == 1 ? 150 : 100);

        if (onUiScaleChanged)
            onUiScaleChanged (uiScale);
    };

    for (juce::Component* child : { (juce::Component*) bypass.get(),
                                    (juce::Component*) previousPreset.get(), (juce::Component*) presetName.get(),
                                    (juce::Component*) nextPreset.get(), (juce::Component*) savePreset.get(),
                                    (juce::Component*) slotA.get(), (juce::Component*) slotB.get(), (juce::Component*) copyAToB.get(),
                                    (juce::Component*) undo.get(), (juce::Component*) redo.get(), (juce::Component*) parameters.get(),
                                    (juce::Component*) stats.get(),
                                    (juce::Component*) sandbox.get(), (juce::Component*) footerInfo.get(), (juce::Component*) scale.get() })
        addAndMakeVisible (child);

    addChildComponent (*retry);
    addChildComponent (*runInProcess);
    addChildComponent (*grip);

    themeManager.addListener (this);
    setState (info, track);

    // The chrome and the loading state show first; the vendor UI follows when the plug-in runs (§19).
    showLoading();
    setHostingState (state);

    // Only its CPU and latency are polled: they aren't its Hosting State.
    startTimer (statusPollMs);
}

PluginWindow::~PluginWindow()
{
    cancelPendingUpdate();
    themeManager.removeListener (this);

    if (vendor != nullptr)
        vendor->removeComponentListener (this);
}

void PluginWindow::setState (const PluginInfo& info, const juce::String& track)
{
    plugin = info;
    trackName = track;
    setName (plugin.name);
    setTitle (tr ("%1 plug-in window", plugin.name));
    setDescription (trackName + ", " + vendorOf (plugin) + " " + middleDot + " " + plugin.formatBadge() + " " + plugin.version);
    bypass->setToggleState (plugin.enabled, juce::dontSendNotification);
    bypass->setTitle (plugin.enabled ? TRANS ("Bypass (plug-in on)") : TRANS ("Bypass (plug-in bypassed)"));
    slotA->setToggleState (plugin.abSlot == 0, juce::dontSendNotification);
    slotB->setToggleState (plugin.abSlot == 1, juce::dontSendNotification);
    updateTexts();
    repaint();
}

void PluginWindow::updateTexts()
{
    const auto presets = rack.getPresetNames (plugin.id);
    presetName->setButtonText (plugin.presetName.isNotEmpty() ? plugin.presetName
                                                              : presets.isEmpty() ? TRANS ("No presets") : TRANS ("Presets"));
    presetName->setTitle (tr ("Preset: %1", presetName->getButtonText()));

    for (auto* b : { previousPreset.get(), nextPreset.get() })
        b->setEnabled (! presets.isEmpty());

    stats->setText (tr ("%1 smp", plugin.latencySamples) + " " + middleDot + " " + cpuText);
    sandbox->setText (sandboxText (hostingState), hostingState.kind == HostingState::Kind::sandboxed);
    footerInfo->setText (tr ("Plug-in UI %1 rendered by %2", middleDot, vendorOf (plugin)) + " " + middleDot + " "
                         + (plugin.formatBadge() + " " + plugin.version).trim() + " " + middleDot + " " + processText (hostingState));
}

void PluginWindow::setUiScale (int percent)
{
    uiScale = percent >= 200 ? 200 : percent >= 150 ? 150 : 100;
    scale->setSelectedIndex (uiScale == 200 ? 2 : uiScale == 150 ? 1 : 0, juce::dontSendNotification);
    updateSize();
}

bool PluginWindow::hasResizeGrip() const
{
    auto* editor = processorEditorIn (vendor.get());
    return status == Status::ready && editor != nullptr && editor->isResizable();
}

void PluginWindow::showParameters (bool shouldShow)
{
    parameterPanel.reset();

    if (shouldShow && status == Status::ready)
        parameterPanel = rack.createParameterEditor (plugin.id);

    if (parameterPanel != nullptr)
        addAndMakeVisible (*parameterPanel);

    // The vendor UI hides behind the panel: a sandboxed one is another process's window, which nothing here can cover.
    if (vendor != nullptr)
        vendor->setVisible (parameterPanel == nullptr);

    parameters->setToggleState (parameterPanel != nullptr, juce::dontSendNotification);
    parameters->setTitle (parameterPanel != nullptr ? TRANS ("Parameters (shown)") : TRANS ("Parameters"));
    resized();
}

void PluginWindow::setHostingState (const HostingState& state)
{
    hostingState = state;
    updateTexts();

    if (state.kind == HostingState::Kind::loading)
        showLoading();
    else if (state.kind == HostingState::Kind::failed)
        showFailed();
    else if (state.isRunning() && status != Status::ready)
        triggerAsyncUpdate();

    repaint();
}

void PluginWindow::showLoading()
{
    cancelPendingUpdate();

    // The vendor UI goes before the instance it belongs to does.
    showParameters (false);

    if (vendor != nullptr)
    {
        vendor->removeComponentListener (this);
        vendor.reset();
    }

    status = Status::loading;
    parameters->setEnabled (false);
    retry->setVisible (false);
    runInProcess->setVisible (false);

    if (spinnerTurn == nullptr)
        spinnerTurn = std::make_unique<juce::VBlankAttachment> (this, [this] { repaint (vendorArea()); });

    updateSize();
    repaint();
}

void PluginWindow::showFailed()
{
    showLoading();
    spinnerTurn.reset();
    status = Status::failed;
    retry->setVisible (true);
    runInProcess->setVisible (true);
    repaint();
}

void PluginWindow::handleAsyncUpdate()
{
    if (hostingState.isRunning() && status != Status::ready)
        loadVendor();
}

void PluginWindow::loadVendor()
{
    spinnerTurn.reset();
    retry->setVisible (false);
    runInProcess->setVisible (false);
    vendor = rack.createEditor (plugin.id);

    if (vendor == nullptr)
        vendor = std::make_unique<NoEditor> (themeManager);

    status = Status::ready;
    parameters->setEnabled (true);
    addAndMakeVisible (*vendor);
    vendor->addComponentListener (this);
    updateSize();
    repaint();
}

juce::Point<int> PluginWindow::vendorSize() const
{
    const auto s = (float) uiScale / 100.0f;

    if (status == Status::ready && vendor != nullptr)
        return { juce::roundToInt ((float) vendor->getWidth() * s), juce::roundToInt ((float) vendor->getHeight() * s) };

    return { minFrameWidth, loadingHeight };
}

void PluginWindow::updateSize()
{
    auto& metrics = themeManager.getMetrics();
    const auto content = vendorSize();
    const auto width = juce::jmax (minFrameWidth, content.x);
    setFrameSize (width, metrics.pluginTitleBarHeight + metrics.pluginToolbarHeight + content.y + metrics.pluginFooterHeight);
}

juce::Rectangle<int> PluginWindow::toolbar() const
{
    return frame().withTrimmedTop (titleBar().getHeight()).removeFromTop (themeManager.getMetrics().pluginToolbarHeight);
}

juce::Rectangle<int> PluginWindow::footer() const
{
    return frame().removeFromBottom (themeManager.getMetrics().pluginFooterHeight);
}

juce::Rectangle<int> PluginWindow::vendorArea() const
{
    return frame().withTrimmedTop (titleBar().getHeight() + toolbar().getHeight()).withTrimmedBottom (footer().getHeight());
}

void PluginWindow::resized()
{
    layoutTitleBar();

    layoutToolbar (toolbar().reduced (toolbarPadding, 0));
    layoutFooter (footer().withTrimmedLeft (footerPaddingLeft).withTrimmedRight (footerPaddingRight));

    const auto area = vendorArea();

    if (vendor != nullptr)
    {
        // Native size times the UI scale, centred in the vendor area; never restyled.
        const auto s = (float) uiScale / 100.0f;
        const auto size = vendorSize();
        const auto x = area.getX() + (area.getWidth() - size.x) / 2;
        vendor->setTransform (uiScale == 100 ? juce::AffineTransform() : juce::AffineTransform::scale (s));
        vendor->setTopLeftPosition (juce::roundToInt ((float) x / s), juce::roundToInt ((float) area.getY() / s));
    }

    if (parameterPanel != nullptr)
        parameterPanel->setBounds (area);

    auto buttons = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), stateButtonsWidth), controlHeight)
                       .translated (0, stateButtonsOffset);
    retry->setBounds (buttons.removeFromLeft ((buttons.getWidth() - stateButtonsGap) / 2));
    runInProcess->setBounds (buttons.withTrimmedLeft (stateButtonsGap));
}

void PluginWindow::layoutToolbar (juce::Rectangle<int> bar)
{
    auto take = [&bar] (int width, int height = controlHeight)
    {
        auto r = bar.removeFromLeft (width).withSizeKeepingCentre (width, height);
        bar.removeFromLeft (gap);
        return r;
    };

    bypass->setBounds (take (bypassWidth));

    // The preset menu is one group: prev, name, next, save, 1 px apart.
    for (auto* part : { previousPreset.get(), presetName.get(), nextPreset.get() })
    {
        const auto width = part == presetName.get() ? presetNameWidth : presetStepWidth;
        part->setBounds (bar.removeFromLeft (width).withSizeKeepingCentre (width, controlHeight));
        bar.removeFromLeft (1);
    }

    savePreset->setBounds (take (presetStepWidth));

    auto slots = take (2 * slotWidth + slotsGap, slotHeight + 2 * slotWellPadding).reduced (slotWellPadding);
    slotA->setBounds (slots.removeFromLeft (slotWidth));
    slotB->setBounds (slots.removeFromRight (slotWidth));
    copyAToB->setBounds (take (copyWidth));
    undo->setBounds (take (presetStepWidth));
    redo->setBounds (take (presetStepWidth));
    parameters->setBounds (take (parametersWidth));

    sandbox->setBounds (bar.removeFromRight (sandboxWidth));
    bar.removeFromRight (gap);
    stats->setBounds (bar.removeFromRight (juce::jmin (statsWidth, bar.getWidth())));
}

void PluginWindow::layoutFooter (juce::Rectangle<int> bar)
{
    grip->setVisible (hasResizeGrip());

    if (grip->isVisible())
    {
        grip->setBounds (bar.removeFromRight (gripSize).withSizeKeepingCentre (gripSize, gripSize));
        bar.removeFromRight (footerGap);
    }

    scale->setBounds (bar.removeFromRight (scaleWidth).withSizeKeepingCentre (scaleWidth, scaleHeight));
    bar.removeFromRight (footerGap);
    footerInfo->setBounds (bar);
}

void PluginWindow::paintBody (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();

    // The toolbar on bg-panel with a bottom border; the footer with a top one.
    auto bar = toolbar();
    g.setColour (theme.bgPanel);
    g.fillRect (bar);
    g.setColour (theme.border);
    g.fillRect (bar.removeFromBottom (1));

    g.setColour (theme.bgSlot);
    g.fillRect (vendorArea());

    auto foot = footer();
    g.setColour (theme.bgPanel);
    g.fillRect (foot);
    g.setColour (theme.border);
    g.fillRect (foot.removeFromTop (1));

    // The A/B well.
    g.setColour (theme.bgSlot);
    g.fillRoundedRectangle (slotA->getBounds().getUnion (slotB->getBounds()).expanded (slotWellPadding).toFloat(), controlRadius);

    // The vendor area while there is no vendor UI: loading (spinner + name) or failed.
    if (status != Status::ready)
    {
        auto area = vendorArea().withSizeKeepingCentre (vendorArea().getWidth(), stateBlockHeight).translated (0, -stateBlockLift);

        if (status == Status::loading)
        {
            const auto spinner = area.removeFromTop (spinnerRow).toFloat().withSizeKeepingCentre ((float) spinnerSize, (float) spinnerSize);
            const auto radius = ((float) spinnerSize - spinnerStroke) / 2.0f;
            const auto turn = (float) (juce::Time::getMillisecondCounter() % spinnerPeriodMs) / (float) spinnerPeriodMs;
            const auto angle = turn * juce::MathConstants<float>::twoPi;
            juce::Path arc;
            arc.addCentredArc (spinner.getCentreX(), spinner.getCentreY(), radius, radius, angle, 0.0f,
                               juce::MathConstants<float>::twoPi * spinnerSweep, true);
            g.setColour (theme.accent);
            g.strokePath (arc, juce::PathStrokeType (spinnerStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            area.removeFromTop (stateGap);
            drawStyledText (g, themeManager, tr ("Loading %1", plugin.name) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")),
                            stateStyle, area.removeFromTop (stateLineHeight), juce::Justification::centred, theme.textSecondary);
        }
        else
        {
            drawStyledText (g, themeManager, tr ("%1 didn't load", plugin.name), stateStyle, area.removeFromTop (stateLineHeight),
                            juce::Justification::centred, theme.rec);
            drawStyledText (g, themeManager, hostingState.reason.isNotEmpty() ? hostingState.reason : TRANS ("It couldn't be loaded."),
                            stateDetailStyle, area.removeFromTop (stateLineHeight), juce::Justification::centred, theme.textDim);
        }
    }
}

void PluginWindow::paintTitle (juce::Graphics& g)
{
    // Plug (accent), track › name, vendor, format badge; what's left is the drag area.
    auto& theme = themeManager.getTheme();
    auto title = titleArea();
    drawIcon (g, Icon::plug, title.removeFromLeft (plugGlyph).toFloat().withSizeKeepingCentre ((float) plugGlyph, (float) plugGlyph),
              theme.accent);
    title.removeFromLeft (gap);
    drawTrackAndName (g, title);
    drawTitleText (g, title, vendorOf (plugin), vendorStyle, theme.textDim);

    const auto badgeText = plugin.formatBadge();
    const auto badgeWidth = juce::GlyphArrangement::getStringWidthInt (themeManager.font (badgeStyle), badgeText) + 2 * (badgePadding + 1);

    if (badgeWidth <= title.getWidth())
    {
        const auto badge = title.removeFromLeft (badgeWidth).withSizeKeepingCentre (badgeWidth, badgeHeight);
        g.setColour (theme.border);
        g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), theme.radiusSm, 1.0f);
        drawNumber (g, themeManager, badgeText, badgeStyle, badge, juce::Justification::centred, theme.textSecondary);
    }
}

bool PluginWindow::releaseContentFocus()
{
    // Esc in the vendor UI hands focus back to the host (§18); on the host chrome it closes.
    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    if (vendor == nullptr || focused == nullptr || (focused != vendor.get() && ! vendor->isParentOf (focused)))
        return false;

    focusHost();
    return true;
}

void PluginWindow::stepPreset (int delta)
{
    const auto presets = rack.getPresetNames (plugin.id);

    if (presets.isEmpty())
        return;

    const auto current = presets.indexOf (plugin.presetName);
    const auto next = current < 0 ? (delta > 0 ? 0 : presets.size() - 1)
                                  : juce::negativeAwareModulo (current + delta, presets.size());
    commands.invoke (cmd::pluginSelectPreset, { plugin.id, next });
}

void PluginWindow::showPresetMenu()
{
    juce::PopupMenu menu;
    const auto presets = rack.getPresetNames (plugin.id);
    const juce::Component::SafePointer<PluginWindow> safe (this);

    for (int i = 0; i < presets.size(); ++i)
        menu.addItem (presets[i], true, presets[i] == plugin.presetName,
                      [safe, i]
                      {
                          if (safe != nullptr)
                              safe->commands.invoke (cmd::pluginSelectPreset, { safe->plugin.id, i });
                      });

    if (presets.isEmpty())
        menu.addItem (TRANS ("No presets yet: Save stores one"), false, false, nullptr);

    menu.addSeparator();
    menu.addItem (TRANS ("Save Preset") + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")), [safe]
    {
        if (safe != nullptr)
            safe->askPresetName();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName.get()));
}

void PluginWindow::askPresetName()
{
    auto* dialog = new juce::AlertWindow (TRANS ("Save Preset"), tr ("Save the current settings of %1 as:", plugin.name),
                                          juce::MessageBoxIconType::NoIcon, this);
    dialog->addTextEditor ("name", plugin.presetName.isNotEmpty() ? plugin.presetName : TRANS ("My Preset"));
    dialog->addButton (TRANS ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton (TRANS ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<PluginWindow> (this), dialog] (int result)
        {
            if (result == 1 && safe != nullptr)
                safe->commands.invoke (cmd::pluginSavePreset, { safe->plugin.id, dialog->getTextEditorContents ("name") });
        }), true);
}

void PluginWindow::timerCallback()
{
    // Its latency changes without an Edit change.
    if (auto info = rack.getPlugin (plugin.id); info.has_value() && info->latencySamples != plugin.latencySamples)
    {
        plugin.latencySamples = info->latencySamples;
        updateTexts();
    }

    auto text = juce::String (rack.getCpuLoad (plugin.id) * 100.0, 1) + "%";

    if (text != cpuText)
    {
        cpuText = text;
        updateTexts();
    }
}

void PluginWindow::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    // The plug-in resized its own UI: the window follows.
    if (&c == vendor.get() && wasResized)
        updateSize();
}

void PluginWindow::themeChanged()
{
    repaint();
}

} // namespace resamper
