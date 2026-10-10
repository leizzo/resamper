#pragma once

#include "Commands/CommandRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace resamper
{

/** A context-menu item that invokes a Command by ID with args (empty, or the
    Command's args type) and shows the shortcut bound to it (PRD §16.5).
    Disabled when the Command is. label defaults to its name in the UI Language. */
juce::PopupMenu::Item commandItem (CommandRegistry&, const juce::String& commandId, std::any args,
                                   const juce::String& label);

/** A context-menu item for a Command that takes no args (see above). */
inline juce::PopupMenu::Item commandItem (CommandRegistry& commands, CommandRef<> command, const juce::String& label = {})
{
    return commandItem (commands, command.id, {}, label);
}

/** A context-menu item for a Command with these args (see above). */
template <typename Args>
juce::PopupMenu::Item commandItem (CommandRegistry& commands, CommandRef<Args> command, std::type_identity_t<Args> args,
                                   const juce::String& label = {})
{
    return commandItem (commands, command.id, std::any (std::move (args)), label);
}

/** "Name (shortcut)" for a tooltip, when the Command has a shortcut (PRD §16.6). */
juce::String tooltipFor (const juce::String& name, const juce::String& commandId);

} // namespace resamper
