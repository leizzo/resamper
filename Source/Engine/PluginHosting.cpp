#include "PluginHostingImpl.h"

#include <algorithm>
#include <utility>

namespace te = tracktion;

namespace resamper
{

namespace
{
    /** Whether the engine creates this plug-in asynchronously (AUv3), past the
        createPluginInstance hook, where no sandboxed stand-in can take its place.
        Tracktion's own check (ExternalPlugin::requiresAsyncInstantiation) is private:
        this is a copy of it, to keep in step with it when the submodule moves. */
    bool createsAsynchronously (te::Engine& engine, const juce::PluginDescription& desc)
    {
        for (auto* format : engine.getPluginManager().pluginFormatManager.getFormats())
            if (format->getName() == desc.pluginFormatName && format->fileMightContainThisPluginType (desc.fileOrIdentifier)
                && format->requiresUnblockedMessageThreadDuringCreation (desc))
                return true;

        return false;
    }

    /** Whether the catalogue knows the plug-in, by its identifier or its file. */
    bool isKnown (te::ExternalPlugin& plugin)
    {
        auto& known = plugin.engine.getPluginManager().knownPluginList;
        return known.getTypeForIdentifierString (plugin.desc.createIdentifierString()) != nullptr
            || known.getTypeForFile (plugin.desc.fileOrIdentifier) != nullptr;
    }

    /** Creates the plug-in's instance anew, now, from the state saved on it; its old one goes first. */
    void createAgain (te::ExternalPlugin& plugin)
    {
        const auto hadInstance = plugin.getAudioPluginInstance() != nullptr;

        // Processing off deletes the instance; neither change is an undo step.
        if (hadInstance)
        {
            plugin.state.setProperty (te::IDs::process, false, nullptr);
            plugin.processingChanged();
        }

        // The engine still counts a deleted instance as prepared, and would then read
        // the new one without checking it could be created. Initialised with none, it doesn't.
        auto& devices = plugin.engine.getDeviceManager();
        plugin.initialise ({ {}, devices.getSampleRate(), devices.getBlockSize() });

        if (hadInstance)
        {
            plugin.state.setProperty (te::IDs::process, true, nullptr);
            plugin.processingChanged();
        }
        else
        {
            plugin.forceFullReinitialise();
        }
    }
}

//==============================================================================
PluginHosting::Impl::Impl()
    : hostedFormats (PluginSandbox::getDefaultFormatNames())
{
    sandbox.addListener (this);
}

PluginHosting::Impl::~Impl()
{
    for (auto& [id, entry] : hosted)
        stopListening (entry);

    sandbox.removeListener (this);
}

void PluginHosting::Impl::attachTo (te::Engine& e)
{
    engine = &e;

    // A plug-in of an Edit runs in its sandbox when shouldLoad said so (its host
    // has loaded it by now); anything else, as the engine would.
    auto& plugins = e.getPluginManager();
    plugins.knownPluginList.addChangeListener (this);
    plugins.createPluginInstance = [this, inProcess = plugins.createPluginInstance]
                                   (const juce::PluginDescription& desc, double rate, int blockSize, juce::String& error)
    {
        juce::String pluginId;
        bool sandboxed = false;

        if (takeLoading (desc.createIdentifierString(), pluginId, sandboxed) && sandboxed)
            return sandbox.createInstance (desc, pluginId, error);

        return inProcess (desc, rate, blockSize, error);
    };
}

void PluginHosting::Impl::detach()
{
    if (engine != nullptr)
        engine->getPluginManager().knownPluginList.removeChangeListener (this);

    engine = nullptr;
}

bool PluginHosting::Impl::shouldLoad (te::ExternalPlugin& plugin)
{
    const auto sandboxed = runsSandboxed (plugin);

    // Inside the engine's creation of the plug-in: Listeners hear once it has returned.
    startLoading (plugin, true);

    if (sandboxed && ! loadInSandbox (plugin))
        return false;

    willLoad (plugin.desc.createIdentifierString(), plugin.itemID.toString(), sandboxed);

    // The engine creates it as soon as this returns, on the message thread (ExternalPlugin
    // does through callBlocking), whatever creation hook a test chains on: then it settles.
    later (plugin, [this] (te::ExternalPlugin& p) { settle (p); });

    return true;
}

void PluginHosting::Impl::recreate (te::ExternalPlugin& plugin)
{
    // Listeners hear before the old instance goes: a window lets go of its editor first.
    startLoading (plugin, false);

    if (runsSandboxed (plugin) && ! loadInSandbox (plugin))
        return;

    createAgain (plugin);
}

bool PluginHosting::Impl::isMissing (te::ExternalPlugin& plugin) const
{
    return ! isStarted (plugin) && plugin.isMissing() && ! isKnown (plugin);
}

void PluginHosting::Impl::startMissing (te::ExternalPlugin& plugin)
{
    // The engine looks for it again; found, it asks shouldLoad, and so it is Loading.
    if (! isStarted (plugin))
        plugin.forceFullReinitialise();
}

bool PluginHosting::Impl::runsSandboxed (te::ExternalPlugin& plugin) const
{
    return ! (bool) plugin.state[PluginHosting::inProcessProperty] && hostedFormats.contains (plugin.desc.pluginFormatName)
        && ! createsAsynchronously (plugin.engine, plugin.desc);
}

bool PluginHosting::Impl::loadInSandbox (te::ExternalPlugin& plugin)
{
    auto& devices = plugin.engine.getDeviceManager();

    // Once it has loaded, the plug-in is created again with it (if it still runs sandboxed).
    return sandbox.loadInBackground (plugin.desc, plugin.itemID.toString(), devices.getSampleRate(),
                                     devices.getBlockSize(), [this, ref = te::makeSafeRef (plugin)]
    {
        if (ref != nullptr && runsSandboxed (*ref))
            createAgain (*ref);
    });
}

void PluginHosting::Impl::willLoad (const juce::String& identifier, const juce::String& pluginId, bool sandboxed)
{
    constexpr size_t maxRemembered = 64;
    const std::scoped_lock lock (loadingLock);

    loading.erase (std::remove_if (loading.begin(), loading.end(), [&] (const Loading& l) { return l.identifier == identifier; }),
                   loading.end());

    // A load the engine took elsewhere (asynchronously) never comes back for its entry.
    if (loading.size() >= maxRemembered)
        loading.erase (loading.begin());

    loading.push_back ({ identifier, pluginId, sandboxed });
}

bool PluginHosting::Impl::takeLoading (const juce::String& identifier, juce::String& pluginId, bool& sandboxed)
{
    const std::scoped_lock lock (loadingLock);

    for (auto it = loading.begin(); it != loading.end(); ++it)
    {
        if (it->identifier == identifier)
        {
            pluginId = it->pluginId;
            sandboxed = it->sandboxed;
            loading.erase (it);
            return true;
        }
    }

    return false;
}

void PluginHosting::Impl::forgetGoneEdits (const te::ExternalPlugin* joining)
{
    const auto joiningId = joining != nullptr ? joining->itemID.toString() : juce::String();

    for (auto it = hosted.begin(); it != hosted.end();)
    {
        auto& [id, entry] = *it;
        const auto* edit = entry.edit.get();

        if (edit == nullptr || (joining != nullptr && id == joiningId && edit != &joining->edit))
        {
            sandbox.dropLoad (id);
            stopListening (entry);
            it = hosted.erase (it);
        }
        else
        {
            ++it;
        }
    }
}

te::ExternalPlugin* PluginHosting::Impl::find (const juce::String& pluginId) const
{
    const auto found = hosted.find (pluginId);
    return found != hosted.end() ? found->second.plugin.get() : nullptr;
}

te::Plugin* PluginHosting::Impl::findInEdits (const juce::String& pluginId) const
{
    if (engine == nullptr)
        return nullptr;

    const auto id = te::EditItemID::fromString (pluginId);
    const auto edits = engine->getActiveEdits().getEdits();

    for (int i = edits.size(); --i >= 0;)
        if (auto plugin = te::findPluginForID (*edits.getUnchecked (i), id))
            return plugin.get();

    return nullptr;
}

te::ExternalPlugin* PluginHosting::Impl::findStarted (const juce::String& pluginId) const
{
    auto* started = find (pluginId);
    return started != nullptr && findInEdits (pluginId) == started ? started : nullptr;
}

bool PluginHosting::Impl::isStarted (const te::ExternalPlugin& plugin) const
{
    return find (plugin.itemID.toString()) == &plugin;
}

void PluginHosting::Impl::startLoading (te::ExternalPlugin& plugin, bool deferred)
{
    const auto pluginId = plugin.itemID.toString();
    forgetGoneEdits (&plugin);

    auto& entry = hosted[pluginId];

    // A plug-in undone and redone is a new object under the same id.
    if (entry.plugin.get() != &plugin)
    {
        stopListening (entry);
        entry.plugin = te::makeSafeRef (plugin);
        entry.edit = te::makeSafeRef (plugin.edit);
    }

    const auto load = ++entry.loads;
    entry.creatingAsync = ! runsSandboxed (plugin) && createsAsynchronously (plugin.engine, plugin.desc);

    if (entry.creatingAsync)
    {
        // The engine creates it past the creation hook and tells only its selectable listeners
        // when it has. It never says it failed: a load that takes the Sandbox's time has.
        if (! std::exchange (entry.listening, true))
            plugin.addSelectableListener (this);

        later (plugin, [this, load] (te::ExternalPlugin& p)
        {
            if (const auto& e = hosted[p.itemID.toString()]; e.loads == load && e.state.kind == HostingState::Kind::loading
                                                              && p.getAudioPluginInstance() == nullptr)
                setState (p, { HostingState::Kind::failed, p.desc.name + " didn't load within "
                                                               + juce::String (PluginSandbox::loadTimeoutMs / 1000) + " s" });
        }, PluginSandbox::loadTimeoutMs);
    }

    setState (plugin, { HostingState::Kind::loading, {} }, deferred);
}

void PluginHosting::Impl::later (te::ExternalPlugin& plugin, std::function<void (te::ExternalPlugin&)> fn, int delayMs)
{
    // Ids repeat (an Edit opened after another starts numbering afresh): only the same plug-in object counts.
    auto call = [self = juce::WeakReference<Impl> (this), ref = te::makeSafeRef (plugin), fn = std::move (fn)]
    {
        if (auto* p = ref.get(); self != nullptr && p != nullptr && self->find (p->itemID.toString()) == p)
            fn (*p);
    };

    if (delayMs > 0)
        juce::Timer::callAfterDelay (delayMs, std::move (call));
    else
        juce::MessageManager::callAsync (std::move (call));
}

void PluginHosting::Impl::settle (te::ExternalPlugin& plugin)
{
    // Loading into its sandbox again since.
    if (sandbox.isLoading (plugin.itemID.toString()))
        return;

    auto& entry = hosted[plugin.itemID.toString()];

    if (auto* instance = plugin.getAudioPluginInstance())
    {
        entry.creatingAsync = false;
        setState (plugin, { PluginSandbox::isSandboxed (instance) ? HostingState::Kind::sandboxed
                                                                  : HostingState::Kind::inProcess, {} });
    }
    else if (! plugin.isInitialisingAsync())
    {
        entry.creatingAsync = false;
        setState (plugin, { HostingState::Kind::failed, plugin.getLoadError() });
    }
}

void PluginHosting::Impl::setState (te::ExternalPlugin& plugin, const HostingState& state, bool deferred)
{
    auto& entry = hosted[plugin.itemID.toString()];

    if (entry.state == state)
        return;

    entry.state = state;

    if (deferred)
        later (plugin, [this] (te::ExternalPlugin& p) { tell (p.itemID.toString()); });
    else
        tell (plugin.itemID.toString());
}

void PluginHosting::Impl::tell (const juce::String& pluginId)
{
    auto& entry = hosted[pluginId];

    if (entry.heard == entry.state)
        return;

    entry.heard = entry.state;
    const auto state = entry.state;
    listeners.call ([&] (PluginHosting::Listener& l) { l.hostingStateChanged (pluginId, state); });
}

void PluginHosting::Impl::stopListening (Hosted& entry)
{
    if (auto* plugin = entry.plugin.get(); plugin != nullptr && entry.listening)
        plugin->removeSelectableListener (this);

    entry.listening = false;
}

void PluginHosting::Impl::pluginCrashed (const juce::String& pluginId)
{
    auto* plugin = find (pluginId);

    // Only its current instance's death counts, and a plug-in being reloaded is Loading.
    if (plugin != nullptr && PluginSandbox::hasCrashed (plugin->getAudioPluginInstance())
        && hosted[pluginId].state.kind != HostingState::Kind::loading)
        setState (*plugin, { HostingState::Kind::crashed, {} });
}

void PluginHosting::Impl::pluginUiClicked (const juce::String& pluginId)
{
    listeners.call ([&] (PluginHosting::Listener& l) { l.pluginUiClicked (pluginId); });
}

void PluginHosting::Impl::selectableObjectChanged (te::Selectable* selectable)
{
    // An asynchronously created plug-in has its instance now, or the engine gave up on it.
    if (auto* plugin = dynamic_cast<te::ExternalPlugin*> (selectable))
    {
        const auto pluginId = plugin->itemID.toString();

        if (auto found = hosted.find (pluginId); found != hosted.end() && found->second.plugin.get() == plugin
                                                 && found->second.creatingAsync)
            settle (*plugin);
    }
}

void PluginHosting::Impl::selectableObjectAboutToBeDeleted (te::Selectable* selectable)
{
    for (auto& [id, entry] : hosted)
        if (entry.plugin.get() == selectable)
            entry.listening = false;
}

void PluginHosting::Impl::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (engine == nullptr)
        return;

    // Starting one may change the Edit's plug-ins (the engine re-reads them): collect first.
    // One not processing the engine wouldn't load anyway.
    std::vector<te::SafeSelectable<te::ExternalPlugin>> found;

    for (auto* edit : engine->getActiveEdits().getEdits())
        for (auto* plugin : te::getAllPlugins (*edit, true))
            if (auto* external = dynamic_cast<te::ExternalPlugin*> (plugin);
                external != nullptr && external->isProcessingEnabled() && ! isStarted (*external)
                && external->isMissing() && isKnown (*external))
                found.push_back (te::makeSafeRef (*external));

    for (auto& ref : found)
        if (auto* plugin = ref.get())
            startMissing (*plugin);
}

//==============================================================================
PluginHosting::PluginHosting() : impl (std::make_unique<Impl>()) {}
PluginHosting::~PluginHosting() = default;

void PluginHosting::addHostedFormat (const juce::String& formatName)
{
    if (PluginSandbox::isAvailable())
        impl->hostedFormats.addIfNotAlreadyThere (formatName);
}

void PluginHosting::removeHostedFormat (const juce::String& formatName)
{
    impl->hostedFormats.removeString (formatName);
}

HostingState PluginHosting::getState (const juce::String& pluginId) const
{
    auto* external = dynamic_cast<te::ExternalPlugin*> (impl->findInEdits (pluginId));

    if (external == nullptr)
        return {};

    if (impl->isStarted (*external))
        return impl->hosted.at (pluginId).state;

    return impl->isMissing (*external) ? HostingState { HostingState::Kind::missing, {} } : HostingState();
}

juce::Result PluginHosting::reload (const juce::String& pluginId)
{
    auto* plugin = impl->findStarted (pluginId);

    if (plugin == nullptr)
        return juce::Result::fail ("No plug-in to reload: it isn't there, or isn't one that loads");

    impl->recreate (*plugin);
    return juce::Result::ok();
}

juce::Result PluginHosting::setRunInProcess (const juce::String& pluginId, bool runInProcess)
{
    auto* plugin = impl->findStarted (pluginId);

    if (plugin == nullptr)
        return juce::Result::fail ("No plug-in to set Run in-process: it isn't there, or isn't one that loads");

    // Saved on the plug-in, as how it runs, not what it is: never an undo step.
    const juce::Identifier inProcess (inProcessProperty);

    if (runInProcess)
        plugin->state.setProperty (inProcess, true, nullptr);
    else
        plugin->state.removeProperty (inProcess, nullptr);

    impl->recreate (*plugin);
    return juce::Result::ok();
}

bool PluginHosting::waitForLoads()
{
    // Not for a load of an Edit that has gone.
    impl->forgetGoneEdits();
    return impl->sandbox.waitForLoads();
}

void PluginHosting::addListener (Listener* l) const      { impl->listeners.add (l); }
void PluginHosting::removeListener (Listener* l) const   { impl->listeners.remove (l); }

PluginHosting::Impl& PluginHosting::getImpl() noexcept
{
    return *impl;
}

} // namespace resamper
