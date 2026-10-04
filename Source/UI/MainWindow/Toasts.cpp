#include "Toasts.h"

namespace resamper
{

namespace
{
    constexpr int toastHeight = 36, gap = 8, bottomMargin = 24, paddingX = 14, switchWidth = 26, switchHeight = 14, switchGap = 6;
}

Toasts::Toasts (ThemeManager& tm) : themeManager (tm)
{
    setInterceptsMouseClicks (true, false);
    setOpaque (false);
    setAlwaysOnTop (true);
}

void Toasts::show (const juce::String& message, std::function<void()> undo, bool isError)
{
    std::vector<Action> actions;

    if (undo)
        actions.push_back ({ "Undo", [run = std::move (undo)] (bool) { run(); }, std::nullopt });

    show (message, std::move (actions), isError);
}

void Toasts::show (const juce::String& message, std::vector<Action> actions, bool isError)
{
    toasts.push_back ({ message, std::move (actions), isError, juce::Time::getMillisecondCounter(), {}, {} });

    while ((int) toasts.size() > maxShown)
        toasts.pop_front();

    followHost();
    toFront (false);
    startTimer (100);
}

void Toasts::dismiss (const juce::String& message)
{
    if (std::erase_if (toasts, [&message] (const Toast& t) { return t.message == message; }) == 0)
        return;

    followHost();
    repaint();
}

juce::StringArray Toasts::getMessages() const
{
    juce::StringArray messages;

    for (auto& t : toasts)
        messages.add (t.message);

    return messages;
}

bool Toasts::runAction (const juce::String& message, const juce::String& label)
{
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        if (it->message != message)
            continue;

        for (size_t i = 0; i < it->actions.size(); ++i)
        {
            if (it->actions[i].label == label)
            {
                trigger (std::next (it).base(), i);
                return true;
            }
        }
    }

    return false;
}

void Toasts::trigger (std::deque<Toast>::iterator toast, size_t index)
{
    auto& action = toast->actions[index];

    if (action.toggle.has_value())
    {
        action.toggle = ! *action.toggle;
        toast->shownAt = juce::Time::getMillisecondCounter();   // a flipped toggle keeps the toast up a while longer
        const auto on = *action.toggle;
        const auto run = action.run;
        repaint();

        if (run)
            run (on);

        return;
    }

    // Closed before it runs: what it runs may show a toast of its own.
    const auto run = action.run;
    toasts.erase (toast);
    followHost();

    if (run)
        run (true);
}

int Toasts::actionWidth (const Action& action) const
{
    const auto font = themeManager.font (TypeStyle { themeManager.getTheme().body.size, false, 600 });
    const auto text = juce::GlyphArrangement::getStringWidthInt (font, action.label);
    return text + 2 * paddingX + (action.toggle.has_value() ? switchWidth + switchGap : 0);
}

void Toasts::layoutToasts (int hostWidth, int hostHeight)
{
    auto& theme = themeManager.getTheme();
    const auto font = themeManager.font (theme.body);
    auto y = hostHeight - bottomMargin;

    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        const auto textWidth = juce::GlyphArrangement::getStringWidthInt (font, it->message);
        int actionsWidth = 0;

        for (auto& action : it->actions)
            actionsWidth += actionWidth (action);

        const auto width = juce::jmin (hostWidth - 40, textWidth + 2 * paddingX + actionsWidth);
        y -= toastHeight;
        it->bounds = juce::Rectangle<int> ((hostWidth - width) / 2, y, width, toastHeight);
        it->actionBounds.clear();

        auto right = it->bounds;

        for (auto action = it->actions.rbegin(); action != it->actions.rend(); ++action)
            it->actionBounds.insert (it->actionBounds.begin(), right.removeFromRight (actionWidth (*action)));

        y -= gap;
    }

    repaint();
}

void Toasts::followHost()
{
    if (placing)
        return;

    if (toasts.empty())
    {
        if (isOnDesktop())
            returnToHost();
        else if (auto* parent = getParentComponent())
        {
            const juce::ScopedValueSetter<bool> guard (placing, true);
            setBounds (parent->getLocalBounds());
        }

        return;
    }

    if (auto* parent = getParentComponent())
        host = parent;

    if (host == nullptr)
        return;

    anchored = host->getScreenBounds();
    layoutToasts (host->getWidth(), host->getHeight());

    juce::Rectangle<int> stack;

    for (auto& toast : toasts)
        stack = stack.getUnion (toast.bounds);

    if (stack.isEmpty())
        return;

    const auto screen = host->localAreaToGlobal (stack);

    for (auto& toast : toasts)
    {
        toast.bounds -= stack.getPosition();

        for (auto& action : toast.actionBounds)
            action -= stack.getPosition();
    }

    if (! isOnDesktop())
    {
        const juce::ScopedValueSetter<bool> guard (placing, true);
        addToDesktop (juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
    }

    if (getBounds() != screen)
    {
        const juce::ScopedValueSetter<bool> guard (placing, true);
        setBounds (screen);
    }
}

void Toasts::returnToHost()
{
    if (! isOnDesktop() || placing)
        return;

    const juce::ScopedValueSetter<bool> guard (placing, true);
    removeFromDesktop();

    if (host == nullptr)
        return;

    host->addAndMakeVisible (this);
    setBounds (host->getLocalBounds());
}

void Toasts::resized()
{
    followHost();
}

bool Toasts::hitTest (int x, int y)
{
    for (auto& t : toasts)
        if (t.bounds.contains (x, y))
            return true;

    return false;
}

void Toasts::paint (juce::Graphics& g)
{
    auto& theme = themeManager.getTheme();
    const TypeStyle actionStyle { theme.body.size, false, 600 };

    for (auto& t : toasts)
    {
        const auto r = t.bounds.toFloat();
        paintElevation (g, theme.elevation2, r, theme.radius2xl);
        g.setColour (theme.bgElevated);
        g.fillRoundedRectangle (r, theme.radius2xl);
        g.setColour (t.error ? theme.rec : theme.border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme.radius2xl, 1.0f);

        auto text = t.bounds.reduced (paddingX, 0);

        for (size_t i = 0; i < t.actions.size() && i < t.actionBounds.size(); ++i)
        {
            auto& action = t.actions[i];
            auto area = t.actionBounds[i];

            if (action.toggle.has_value())
            {
                // Toggle/On · Off: a 26 x 14 switch before the label.
                auto knobArea = area.withTrimmedLeft (paddingX).removeFromLeft (switchWidth)
                                    .withSizeKeepingCentre (switchWidth, switchHeight).toFloat();
                const auto on = *action.toggle;
                g.setColour (on ? theme.accent : theme.bgSlot);
                g.fillRoundedRectangle (knobArea, switchHeight * 0.5f);
                const auto knob = juce::Rectangle<float> (switchHeight - 4.0f, switchHeight - 4.0f)
                                      .withCentre ({ on ? knobArea.getRight() - switchHeight * 0.5f : knobArea.getX() + switchHeight * 0.5f,
                                                     knobArea.getCentreY() });
                g.setColour (on ? theme.textOnAccent : theme.textSecondary);
                g.fillEllipse (knob);
                area.removeFromLeft (paddingX + switchWidth + switchGap);
                drawStyledText (g, themeManager, action.label, actionStyle, area, juce::Justification::centredLeft,
                                theme.textSecondary);
            }
            else
            {
                drawStyledText (g, themeManager, action.label, actionStyle, area, juce::Justification::centred, theme.accent);
            }

            text = text.withRight (juce::jmin (text.getRight(), t.actionBounds[i].getX()));
        }

        drawStyledText (g, themeManager, t.message, theme.body, text, juce::Justification::centredLeft,
                        t.error ? theme.rec : theme.textPrimary);
    }
}

void Toasts::mouseUp (const juce::MouseEvent& e)
{
    for (auto it = toasts.begin(); it != toasts.end(); ++it)
    {
        if (! it->bounds.contains (e.getPosition()))
            continue;

        for (size_t i = 0; i < it->actionBounds.size() && i < it->actions.size(); ++i)
        {
            if (it->actionBounds[i].contains (e.getPosition()))
            {
                trigger (it, i);
                return;
            }
        }

        toasts.erase (it);
        followHost();
        return;
    }
}

void Toasts::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const auto before = toasts.size();

    while (! toasts.empty() && now - toasts.front().shownAt > (juce::uint32) lifetimeMs)
        toasts.pop_front();

    const auto moved = host != nullptr && host->getScreenBounds() != anchored;

    if (toasts.size() != before || moved)
        followHost();

    if (toasts.empty())
        stopTimer();
}

} // namespace resamper
