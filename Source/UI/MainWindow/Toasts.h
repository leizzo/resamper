#pragma once

#include "UI/Controls/Controls.h"

#include <deque>
#include <vector>

namespace resamper
{

/** Toasts (PRD §16.7): short notes at the bottom centre for outcomes that
    aren't obvious (and for errors, instead of modal dialogs), each gone after
    4 s, optionally with actions (Undo; a toggle such as "Auto-open window on
    insert"). The newest sits at the bottom; at most three show. While any are
    showing they float in their own window above a sandboxed plug-in's UI,
    still at the bottom centre of the window they belong to. */
class Toasts : public juce::Component,
               private juce::Timer
{
public:
    static constexpr int lifetimeMs = 4000, maxShown = 3;

    /** One action at a toast's right. A toggle shows its state and flips it
        when clicked (run gets the new state) without closing the toast;
        anything else runs and closes it. */
    struct Action
    {
        juce::String label;
        std::function<void (bool on)> run;
        std::optional<bool> toggle;   ///< set for a toggle: its state
    };

    explicit Toasts (ThemeManager&);

    /** undo: shows an Undo action that runs it and closes the toast. */
    void show (const juce::String& message, std::function<void()> undo = {}, bool isError = false);

    /** A toast with any actions, in order. */
    void show (const juce::String& message, std::vector<Action> actions, bool isError = false);

    /** Closes every toast showing message, before its time is up. */
    void dismiss (const juce::String& message);

    /** Lays toasts out at the bottom centre of their host. While any are showing,
        that is a window of their own above plug-in UI. */
    void followHost();

    /** The messages on screen, oldest first. */
    juce::StringArray getMessages() const;

    /** Clicks the action labelled label on the newest toast showing message.
        Returns false if there is none. */
    bool runAction (const juce::String& message, const juce::String& label);

    bool hitTest (int x, int y) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Toast
    {
        juce::String message;
        std::vector<Action> actions;
        bool error = false;
        juce::uint32 shownAt = 0;
        juce::Rectangle<int> bounds;
        std::vector<juce::Rectangle<int>> actionBounds;
    };

    ThemeManager& themeManager;
    std::deque<Toast> toasts;
    juce::Component::SafePointer<juce::Component> host;
    juce::Rectangle<int> anchored;
    bool placing = false;

    /** Bounds in the host's coordinates, from its bottom centre. */
    void layoutToasts (int hostWidth, int hostHeight);
    void returnToHost();
    int actionWidth (const Action&) const;

    /** Runs one of a toast's actions: a toggle flips and stays, anything else closes the toast first. */
    void trigger (std::deque<Toast>::iterator, size_t action);
    void timerCallback() override;
};

} // namespace resamper
