#include "DeveloperCommands.h"
#include "Commands/CommandRegistry.h"
#include "UI/Theme/ThemeManager.h"

namespace resamper
{

void registerDeveloperCommands (CommandRegistry& registry, ThemeManager& themes,
                                std::function<void (const juce::String&)> reportError)
{
    registry.add (cmd::devReloadTheme, { "Reload Theme" }, [&themes, onError = std::move (reportError)]
    {
        if (auto r = themes.reloadTheme(); r.failed() && onError)
            onError (r.getErrorMessage());
    });
}

} // namespace resamper
