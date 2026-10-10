#pragma once

#include <juce_core/juce_core.h>
#include <any>
#include <functional>
#include <map>
#include <memory>
#include <type_traits>
#include <typeindex>

namespace resamper
{

/** A typed handle on a Command: its string ID and the args it takes, or void
    for none. Each area's header declares its handles in namespace cmd, so code
    names a Command once and the compiler checks the args it passes. */
template <typename Args = void>
struct CommandRef
{
    const char* id;
};

/** A named, user-triggerable operation keyed by a string ID (e.g. "transport.play").

    The only path by which UI mutates the model. Commands never
    implement undo: model mutations are recorded by Engine Undo.
*/
class Command
{
public:
    /** A Command is built by CommandRegistry::add from its handle, CommandInfo and body. */
    Command (juce::String commandId, juce::String displayName, std::type_index argsType,
             std::function<void (const std::any&)> body,
             std::function<bool()> isEnabled, std::function<bool()> isTicked);

    const juce::String& getId() const noexcept     { return id; }
    const juce::String& getName() const noexcept   { return name; }

    /** The type of args the Command takes: typeid (void) for none. */
    std::type_index getArgsType() const noexcept   { return argsType; }

    /** Runs the Command. args holds the Command's args type, or nothing: a
        Command invoked from a menu, shortcut or JSON button without args runs
        with default-constructed args. */
    void execute (const std::any& args) const      { body (args); }

    /** Whether invoking now would do anything (menus grey out disabled Commands). */
    bool isEnabled() const                         { return enabled == nullptr || enabled(); }

    /** A toggle's state: menus tick the Command while it is on. */
    bool isTicked() const                          { return ticked != nullptr && ticked(); }

private:
    const juce::String id, name;
    const std::type_index argsType;
    std::function<void (const std::any&)> body;
    std::function<bool()> enabled, ticked;

    JUCE_DECLARE_NON_COPYABLE (Command)
};

/** How a Command shows: its name, and optionally whether it is enabled and
    whether it is ticked (see Command). Unset, it is always enabled and never ticked. */
struct CommandInfo
{
    juce::String name;
    std::function<bool()> isEnabled = {};
    std::function<bool()> isTicked = {};
};

/** The single registry behind JSON buttons, menus and keyboard shortcuts.
    If an action isn't registered here, no UI surface can reach it. */
class CommandRegistry
{
public:
    /** Registers a Command that takes args. IDs must be unique. */
    template <typename Args>
    void add (CommandRef<Args> ref, CommandInfo info, std::type_identity_t<std::function<void (const Args&)>> body)
    {
        static_assert (std::is_default_constructible_v<Args>, "a Command invoked without args runs with Args{}");
        jassert (body != nullptr);

        add (ref.id, std::move (info), typeid (Args), [fn = std::move (body)] (const std::any& args)
        {
            if (args.has_value())
                fn (std::any_cast<const Args&> (args));
            else
                fn (Args {});
        });
    }

    /** Registers a Command that takes no args. IDs must be unique. */
    void add (CommandRef<> ref, CommandInfo info, std::function<void()> body);

    /** Executes a Command with its args. Returns false (and asserts) if it isn't registered. */
    template <typename Args>
    bool invoke (CommandRef<Args> ref, std::type_identity_t<Args> args)
    {
        return invokeById (ref.id, std::any (std::move (args)));
    }

    /** Executes a Command that takes no args, or one that takes args with default ones. */
    template <typename Args>
    bool invoke (CommandRef<Args> ref)                   { return invokeById (ref.id); }

    /** Executes the Command a menu, key binding or JSON button names by ID: args
        is empty, or holds the Command's args type. Returns false (and asserts)
        for an unknown ID or args of another type. */
    bool invokeById (const juce::String& commandId, const std::any& args = {});

    const Command* find (const juce::String& commandId) const;
    bool contains (const juce::String& commandId) const    { return find (commandId) != nullptr; }

    /** Every registered Command's ID, in ID order. */
    juce::StringArray getIds() const;

private:
    void add (const char* id, CommandInfo, std::type_index argsType, std::function<void (const std::any&)> body);

    std::map<juce::String, std::unique_ptr<Command>> commands;
};

} // namespace resamper
