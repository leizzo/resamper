#pragma once

#include "Commands/CommandRegistry.h"

#include <functional>

namespace resamper
{

class ThemeManager;

namespace cmd
{
    inline constexpr CommandRef<> devReloadTheme { "dev.reloadTheme" };     ///< re-read the Theme and re-style in place
}

/** Registers the Developer Mode reload Command above.

    dev.toggleOverlay (show the status bar and developer overlay) belongs to
    MainComponent, which owns both.
*/
void registerDeveloperCommands (CommandRegistry&, ThemeManager&,
                                std::function<void (const juce::String&)> reportError);

} // namespace resamper
