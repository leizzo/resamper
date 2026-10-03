#pragma once

#include "EngineInternal.h"
#ifdef RESAMPER_ENGINE_INTERNAL
#include "PluginHosting.h"
#include "PluginSandbox.h"

#include <tracktion_engine/tracktion_engine.h>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace resamper
{

/** Plug-in Hosting's engine side: what the engine owner wires into Tracktion. */
struct PluginHosting::Impl : private PluginSandbox::Listener,
                             private tracktion::SelectableListener,
                             private juce::ChangeListener
{
    Impl();
    ~Impl() override;

    /** Installs the engine's plug-in creation hook and listens to its catalogue. Once, when
        the engine is built: test doubles chain onto the hook, so it is never installed again. */
    void attachTo (tracktion::Engine&);

    /** Stops listening to the engine's catalogue. Before the engine goes. */
    void detach();

    /** The engine is about to create a plug-in of an Edit (EngineBehaviour::shouldLoadPlugin,
        after the default check): it is Loading. False while a sandboxed one loads into its
        host in the background (it has no instance till then; it is created again once it has). */
    bool shouldLoad (tracktion::ExternalPlugin&);

    /** Creates a plug-in's instance anew from the state saved on it (Reload, Run
        in-process), Loading till it has: at once if it runs in-process; if sandboxed,
        once its new host has loaded it in the background, the old instance (bypassed,
        if it crashed) staying till then. Never an undo step. */
    void recreate (tracktion::ExternalPlugin&);

    /** Whether the plug-in is Missing: Plug-in Hosting never started it, it has no
        instance, and the catalogue doesn't know it. */
    bool isMissing (tracktion::ExternalPlugin&) const;

    /** Has the engine look for a plug-in Plug-in Hosting never started in the catalogue
        again (Locate, or a catalogue that changed) and start it, Loading, if found there:
        the engine takes the plug-in's description from what it finds. Still Missing if not. */
    void startMissing (tracktion::ExternalPlugin&);

private:
    friend class PluginHosting;

    tracktion::Engine* engine = nullptr;
    PluginSandbox sandbox;
    juce::StringArray hostedFormats;
    juce::ListenerList<PluginHosting::Listener> listeners;

    /** A plug-in Plug-in Hosting has started, and its Hosting State. */
    struct Hosted
    {
        tracktion::SafeSelectable<tracktion::ExternalPlugin> plugin;
        tracktion::SafeSelectable<tracktion::Edit> edit;   ///< the plug-in's Edit: the entry (and its Sandbox load) goes with it
        HostingState state;
        HostingState heard;           ///< the state Listeners last heard for its id
        bool creatingAsync = false;   ///< the engine creates it asynchronously (AUv3): its end of Loading comes as a change
        bool listening = false;       ///< listening to the plug-in as a selectable
        int loads = 0;                ///< counts the times it started Loading: a timeout is the newest one's only
    };

    std::map<juce::String, Hosted> hosted;   ///< by plug-in id; on the message thread

    /** What shouldLoad decided about the plug-in the engine creates next with an
        identifier (PluginDescription::createIdentifierString), for the creation hook. */
    struct Loading
    {
        juce::String identifier, pluginId;
        bool sandboxed = true;
    };

    std::mutex loadingLock;
    std::vector<Loading> loading;

    bool runsSandboxed (tracktion::ExternalPlugin&) const;
    bool loadInSandbox (tracktion::ExternalPlugin&);
    void willLoad (const juce::String& identifier, const juce::String& pluginId, bool sandboxed);
    bool takeLoading (const juce::String& identifier, juce::String& pluginId, bool& sandboxed);

    /** Forgets the plug-ins of Edits that have gone, and the one of another Edit under joining's id
        (Open makes the new Edit before the old one goes), dropping any Sandbox load they left: loads
        go by plug-in id, and a new Edit numbers its ids afresh. A plug-in gone from an Edit still
        there (undone) stays, its load kept for its Redo. */
    void forgetGoneEdits (const tracktion::ExternalPlugin* joining = nullptr);

    /** The plug-in, if Plug-in Hosting started it and it is still there. */
    tracktion::ExternalPlugin* find (const juce::String& pluginId) const;

    /** The plug-in with this id in the newest Edit that has one (Open makes the new Edit
        before the old one goes, and a new Edit numbers its ids afresh), or nullptr. */
    tracktion::Plugin* findInEdits (const juce::String& pluginId) const;

    /** findInEdits's plug-in, if it is the one Plug-in Hosting started under its id. */
    tracktion::ExternalPlugin* findStarted (const juce::String& pluginId) const;

    /** Whether Plug-in Hosting started this plug-in object. */
    bool isStarted (const tracktion::ExternalPlugin&) const;

    /** The plug-in is Loading (again); its entry follows the plug-in object. Listeners hear at
        once, or, inside the engine's creation of it (deferred), once that has returned. */
    void startLoading (tracktion::ExternalPlugin&, bool deferred);

    /** Calls fn from the message loop (after delayMs), if the plug-in is still the one Plug-in
        Hosting has under its id. */
    void later (tracktion::ExternalPlugin&, std::function<void (tracktion::ExternalPlugin&)> fn, int delayMs = 0);

    /** The engine has created the plug-in (or failed to): Sandboxed or In-process if it has
        an instance, Failed (with the engine's reason) if not, unless it still creates one. */
    void settle (tracktion::ExternalPlugin&);

    /** Sets the plug-in's state. Listeners hear of a change at once, or (deferred) from the
        message loop, through tell: a deferred change overtaken before then is heard as the newer one. */
    void setState (tracktion::ExternalPlugin&, const HostingState&, bool deferred = false);

    /** Tells Listeners the plug-in's state, unless it is the one they heard last. */
    void tell (const juce::String& pluginId);

    void stopListening (Hosted&);

    void pluginCrashed (const juce::String& pluginId) override;
    void pluginUiClicked (const juce::String& pluginId) override;

    void selectableObjectChanged (tracktion::Selectable*) override;
    void selectableObjectAboutToBeDeleted (tracktion::Selectable*) override;

    /** The catalogue changed (a scan found plug-ins, or Locate): Missing plug-ins it knows now start. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Impl)
    JUCE_DECLARE_NON_COPYABLE (Impl)
};

} // namespace resamper
#endif
