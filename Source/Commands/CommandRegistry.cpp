#include "CommandRegistry.h"

namespace resamper
{

Command::Command (juce::String commandId, juce::String displayName, std::type_index type,
                  std::function<void (const std::any&)> fn,
                  std::function<bool()> isEnabled, std::function<bool()> isTicked)
    : id (std::move (commandId)), name (std::move (displayName)), argsType (type),
      body (std::move (fn)), enabled (std::move (isEnabled)), ticked (std::move (isTicked))
{
}

void CommandRegistry::add (const char* id, CommandInfo info, std::type_index argsType,
                           std::function<void (const std::any&)> body)
{
    auto command = std::make_unique<Command> (id, std::move (info.name), argsType, std::move (body),
                                              std::move (info.isEnabled), std::move (info.isTicked));
    [[maybe_unused]] auto [it, inserted] = commands.emplace (id, std::move (command));
    jassert (inserted);   // two Commands registered under one ID
}

void CommandRegistry::add (CommandRef<> ref, CommandInfo info, std::function<void()> body)
{
    jassert (body != nullptr);
    add (ref.id, std::move (info), typeid (void), [fn = std::move (body)] (const std::any&) { fn(); });
}

bool CommandRegistry::invokeById (const juce::String& commandId, const std::any& args)
{
    auto it = commands.find (commandId);

    if (it == commands.end())
    {
        DBG ("Unknown Command: " << commandId);
        jassertfalse;
        return false;
    }

    auto& command = *it->second;

    if (args.has_value() && std::type_index (args.type()) != command.getArgsType())
    {
        DBG ("Command " << commandId << " takes other args");
        jassertfalse;
        return false;
    }

    command.execute (args);
    return true;
}

const Command* CommandRegistry::find (const juce::String& commandId) const
{
    auto it = commands.find (commandId);
    return it != commands.end() ? it->second.get() : nullptr;
}

juce::StringArray CommandRegistry::getIds() const
{
    juce::StringArray ids;

    for (auto& [id, command] : commands)
        ids.add (id);

    return ids;
}

} // namespace resamper
