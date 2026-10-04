#include "MainWindow.h"
#include "App/ResamperApp.h"
#include "Commands/ApplicationCommandTable.h"

namespace resamper
{

MainWindow::MainWindow (const juce::String& title, ResamperApp& app)
    : juce::DocumentWindow (title,
                            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                            juce::DocumentWindow::allButtons)
{
    auto content = std::make_unique<MainComponent> (app, commandManager);

    commandManager.registerAllCommandsForTarget (content.get());
    commandManager.setFirstCommandTarget (content.get());
    addKeyListener (&content->getShortcutListener());

    for (auto* name : getMenuNames())
        menuNames.add (name);

    setApplicationCommandManagerToWatch (&commandManager);

   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #else
    setMenuBar (this);
   #endif

    setUsingNativeTitleBar (true);
    setContentOwned (content.release(), false);
    setResizable (true, true);

    // Designed at 1600 x 1000; responsive from 1280 x 800 (PRD §5.3).
    setResizeLimits (1280, 800, 16384, 16384);
    const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                        ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt()
                        : juce::Rectangle<int> (1600, 1000);
    centreWithSize (juce::jmin (1600, area.getWidth()), juce::jmin (1000, area.getHeight()));
    setVisible (true);
}

MainWindow::~MainWindow()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #else
    setMenuBar (nullptr);
   #endif

    commandManager.setFirstCommandTarget (nullptr);
    clearContentComponent();
}

void MainWindow::showToast (const juce::String& message, bool undoable, bool isError)
{
    if (auto* main = dynamic_cast<MainComponent*> (getContentComponent()))
        main->showToast (message, undoable, isError);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

juce::PopupMenu MainWindow::getMenuForIndex (int, const juce::String& name)
{
    return createCommandMenu (commandManager, name);
}

} // namespace resamper
