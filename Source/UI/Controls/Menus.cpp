#include "Menus.h"
#include "Commands/ApplicationCommandTable.h"
#include "UI/Localisation.h"

namespace resamper
{

juce::PopupMenu::Item commandItem (CommandRegistry& commands, const juce::String& commandId, std::any args,
                                   const juce::String& label)
{
    auto* command = commands.find (commandId);
    juce::PopupMenu::Item item (label.isNotEmpty() ? label : command != nullptr ? TRANS (command->getName()) : commandId);
    item.setEnabled (command != nullptr && command->isEnabled());
    item.setAction ([&commands, commandId, args] { commands.invokeById (commandId, args); });

    if (auto key = findShortcut (commandId); key.isValid())
        item.shortcutKeyDescription = key.getTextDescriptionWithIcons();

    return item;
}

juce::String tooltipFor (const juce::String& name, const juce::String& commandId)
{
    if (auto key = findShortcut (commandId); key.isValid())
        return tr ("%1 (%2)", name, key.getTextDescriptionWithIcons());

    return name;
}

} // namespace resamper
