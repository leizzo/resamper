#pragma once

#include <juce_core/juce_core.h>
#include <memory>

namespace resamper
{

/** A Plug-in's Hosting State (CONTEXT.md): the one state it is in at a time. */
struct HostingState
{
    enum class Kind
    {
        loading,     ///< being started, or started again; an old instance may still play (or stay bypassed) till then
        sandboxed,   ///< runs in its Sandbox
        inProcess,   ///< runs in-process (Run in-process, a format the Sandbox can't host, or a built-in)
        crashed,     ///< its Sandbox died: its audio is bypassed until Reload
        failed,      ///< it couldn't be loaded: reason says why
        missing      ///< saved in the Project but not installed (the catalogue doesn't know it): audio passes through
    };

    Kind kind = Kind::inProcess;
    juce::String reason;   ///< Failed: why it couldn't be loaded; empty otherwise

    /** Sandboxed or In-process: it has an instance that plays. */
    bool isRunning() const noexcept   { return kind == Kind::sandboxed || kind == Kind::inProcess; }

    bool operator== (const HostingState&) const = default;
};

/** Plug-in Hosting (CONTEXT.md): where each Plug-in of an Edit runs, in its
    Sandbox or in-process, starting it there, and its Hosting State. The
    Sandbox (PluginSandbox) is the mechanism; this decides which Plug-ins use
    it, loads them into it in the background, and creates them again (Reload,
    Run in-process).

    Which plug-ins run sandboxed: those of a hosted format (the formats the
    sandbox host process knows, plus any added with addHostedFormat), unless
    the instance runs in-process (inProcessProperty on its state). A format
    that needs the message thread free while it creates a plug-in (AUv3) runs
    in-process, and so does anything the engine creates without loading it
    into an Edit (scans, ARA factories).

    Each Plug-in it has started has one Hosting State, pushed to Listeners on
    every transition. The Sandbox's load timeout is the one a sandboxed load
    has; an AUv3 the engine creates asynchronously gets as long (the engine
    doesn't say when such a creation fails).

    A Plug-in the engine didn't find in its catalogue when it made it is
    Missing, never started. Plug-in Hosting listens to the catalogue: once it
    knows a Missing Plug-in (a scan found it), that Plug-in starts, and
    Listeners hear it go Loading and on from there.

    The engine owner (EngineManager) holds the one Plug-in Hosting, and the
    engine reaches it through its Impl (PluginHostingImpl.h, engine module only).
*/
class PluginHosting
{
public:
    /** The engine side: Tracktion's hooks into Plug-in Hosting. Engine module only. */
    struct Impl;

    PluginHosting();
    ~PluginHosting();

    /** On a plug-in's state: true while the instance runs in-process (Run in-process). Saved with the project. */
    static constexpr const char* inProcessProperty = "resamperInProcess";

    /** Lets plug-ins of a format the sandbox host process knows besides the defaults
        (a format added through PluginSandbox::runHost's extraFormats) run sandboxed. */
    void addHostedFormat (const juce::String& formatName);

    /** Plug-ins of the format run in-process from now on (for tests whose double
        creates a format's plug-ins itself, through the engine's creation hook). */
    void removeHostedFormat (const juce::String& formatName);

    /** The Hosting State of the plug-in with this id in the newest Edit that has one.
        Missing for an external plug-in the catalogue doesn't know; In-process for
        anything else Plug-in Hosting never started (a built-in device). On the
        message thread. */
    HostingState getState (const juce::String& pluginId) const;

    /** Starts the plug-in again from the state last saved on it: Retry, and a
        crashed plug-in's Reload. It is Loading till then; if sandboxed, its old
        instance plays (or stays bypassed) until the new one is ready. Never an
        undo step. Fails for a plug-in Plug-in Hosting never started (a Missing one). */
    juce::Result reload (const juce::String& pluginId);

    /** Runs the plug-in in-process (Run in-process) or back in its Sandbox,
        and starts it again that way. Saved with the project, per instance;
        never an undo step. A plug-in whose format can't be sandboxed runs
        in-process either way. */
    juce::Result setRunInProcess (const juce::String& pluginId, bool runInProcess);

    /** Runs the message loop until no plug-in is loading into its Sandbox, or a
        load's time is up: an offline render mustn't leave a loading plug-in out.
        False if one still is. On the message thread. */
    bool waitForLoads();

    //==============================================================================
    /** Hears every Plug-in's Hosting State, whichever Edit it is in. */
    struct Listener
    {
        virtual ~Listener() = default;

        /** The plug-in's Hosting State changed to state. In order, on the message thread. */
        virtual void hostingStateChanged (const juce::String& pluginId, const HostingState& state) = 0;

        /** A sandboxed plug-in's own UI was clicked. On the message thread. */
        virtual void pluginUiClicked (const juce::String& /*pluginId*/) {}
    };

    /** Adds or removes a Listener. On the message thread. */
    void addListener (Listener*) const;
    void removeListener (Listener*) const;

    /** The engine side, for the engine module only (PluginHostingImpl.h). */
    Impl& getImpl() noexcept;

private:
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE (PluginHosting)
};

} // namespace resamper
