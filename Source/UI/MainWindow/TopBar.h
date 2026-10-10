#pragma once

#include "Engine/ApplicationModel.h"
#include "UI/Controls/ContinuousControl.h"
#include "UI/Controls/Controls.h"
#include "UI/State/ShellState.h"

namespace resamper
{

class CommandRegistry;

/** The 52 px top bar (PRD §6.1): brand and menu; tempo, time signature and
    transport; position, Metronome, Follow, CPU and the view switcher.
    Every change goes through a Command. It polls the transport at 30 Hz. */
class TopBar : public juce::Component,
               private juce::Timer,
               private ApplicationModel::Listener,
               private juce::ValueTree::Listener
{
public:
    TopBar (const ApplicationModel&, CommandRegistry&, ThemeManager&, ShellState&);
    ~TopBar() override;

    /** A menu title was clicked; show that menu under area (screen coordinates). */
    std::function<void (const juce::String& menuName, juce::Rectangle<int> screenArea)> onMenu;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    const ApplicationModel& model;
    CommandRegistry& commands;
    ThemeManager& themeManager;
    ShellState& shell;

    ValueField tempo;
    Button signature;
    IconButton prev, record, automationArm, play, stop, metronome, follow;
    Segmented views;

    struct MenuTitle
    {
        juce::String name;      ///< the menu's English name, which picks its Commands
        juce::String title;     ///< the name shown, in the UI Language
        juce::Rectangle<int> bounds;
    };

    std::vector<MenuTitle> menus;
    juce::Rectangle<int> brandBounds, positionBounds, cpuBounds, viewsBox;
    bool compactMenu = false, showTime = true;
    float cpu = 0;

    void refresh();
    void showSignatureMenu();

    void timerCallback() override;
    void modelChanged() override   { refresh(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override   { refresh(); }
};

} // namespace resamper
