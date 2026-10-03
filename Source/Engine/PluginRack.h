#pragma once

#include "NativeDevices.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace resamper
{

class ProjectManager;

namespace test { struct PluginRackTests; }

/** A track's two plug-in chains (PRD §4.2). The device chain is the track's
    sound (instrument, racks, creative effects), edited in the detail view. The
    mixer inserts are console processing after it, edited in the mixer strip. */
enum class PluginChain { device, mixer };

/** How much of a native device's card shows (PRD §9.2.1a): a 28 px strip,
    the 164 px card that never scrolls, or every parameter docked across the
    detail view. Saved with the device (§20 NativeDevice.collapsed). A
    plug-in's card has one size. */
enum class DeviceSize { folded, compact, expanded };

/** One plug-in in the catalogue, or one on a track.

    In the catalogue, id is empty. Once inserted, id is the plug-in's EditItemID.
    path is a built-in type name or an external plug-in's file / identifier —
    the string plugin.insert passes through.
*/
struct PluginInfo
{
    juce::String id;
    juce::String name, manufacturer, format, path, category;
    juce::String version;                       ///< an external plug-in's own version; empty for a built-in
    bool instrument = false;
    bool midiEffect = false;
    bool external = false;                      ///< a scanned plug-in (VST3, AU), not a built-in
    PluginChain chain = PluginChain::device;   ///< on a track: which chain it is on
    bool enabled = true;                        ///< false when bypassed
    bool failedScan = false;                    ///< in the catalogue: its scan crashed or timed out; it can only be retried
    int latencySamples = 0;                     ///< the latency the plug-in reports
    juce::StringArray pinnedParameters;         ///< parameter ids shown on a plug-in's card, in pin order
    DeviceSize size = DeviceSize::compact;      ///< a native device's card
    juce::String trackId;                       ///< on a track: the track it is on
    juce::String presetName;                    ///< the preset last chosen or saved in its window; empty for none
    int abSlot = 0;                             ///< the A/B compare slot in use: 0 = A, 1 = B

    /** An external plug-in's format as its badge reads: VST3, AU, CLAP. */
    juce::String formatBadge() const   { return format == "AudioUnit" ? juce::String ("AU") : format; }
};

/** Where a plug-in's window is and how it shows (PRD §20 Plugin.window).
    Saved with the project on the plug-in; changing it is never an undo step. */
struct PluginWindowState
{
    bool open = false;
    bool pinned = false;
    bool placed = false;          ///< false until the window has a remembered position
    juce::Point<int> position;    ///< the window's top-left on the desktop, when placed
    int uiScale = 100;            ///< percent: 100, 150 or 200

    bool operator== (const PluginWindowState&) const = default;
};

/** One automatable parameter of a plug-in on a track, in its own units. */
struct PluginParameter
{
    juce::String id, name;
    float minimum = 0, maximum = 1, value = 0, defaultValue = 0;
    bool automated = false;   ///< has an automation curve
    bool output = false;      ///< a native device's Mix / Out: its card's last zone

    /** How a value maps to a control's travel (a frequency's is logarithmic)
        and which values are legal (a choice steps by 1). */
    juce::NormalisableRange<float> range { 0.0f, 1.0f };
};

/** One value of a multi-parameter gesture (PluginRack::setParameters). */
struct ParameterValue
{
    juce::String parameterId;
    float value = 0;
};

/** Facade over the current Edit's plug-ins.

    Owns no plug-in state. Every call re-reads ProjectManager::getEdit(), because
    a new or opened Project replaces the Edit. Nothing above this layer includes
    a Tracktion header.

    A track's plug-ins run in this order, ahead of its volume plug-in (the fader):
    device chain, mixer inserts, aux sends. A mixer insert carries the
    resamperChain = "mixer" property on its state; anything else before the
    fader (aux sends and returns and the level meter aside) is on the device
    chain, so a project saved before the split opens with its old inserts as
    the device chain, in the same order, sounding the same. The Pre-FX send tap
    sits at the boundary: after the device chain, before the mixer inserts.

    A new plug-in goes at the end of its chain. On a MIDI track, inserting an
    instrument into the device chain removes the built-in synth in the same
    undo step and keeps the MIDI Track Kind. Mixer inserts take
    effects only (no instruments, no MIDI effects), at most maxMixerInserts.

    Undo is Engine Undo: Ctrl+Z is edit.undo(). A call that would change nothing
    does not start a transaction.
*/
class PluginRack
{
public:
    explicit PluginRack (ProjectManager&);
    ~PluginRack();

    /** Built-in engine plug-ins, plus whatever the scan has already found, plus
        the plug-ins that failed to scan (failedScan; their name is the file's). */
    juce::Array<PluginInfo> getCatalogue() const;

    /** Returns immediately. The disk scan runs on a juce::Thread, each plug-in
        file in its own scan worker process (PluginScanner) with a timeout, so
        one that hangs or crashes fails alone and the scan goes on. No-op if one
        is running. A file that failed before is not tried again until retried.
        Plug-in Hosting starts the Missing plug-ins it finds. */
    void startScan();
    bool isScanning() const;

    /** Scans one plug-in that failed to scan again, in the background like
        startScan; nothing else is rescanned. path is its catalogue path. Fails
        if it didn't fail to scan or a scan is running. */
    juce::Result retryScan (const juce::String& path);

    /** Names from the engine format manager (VST3, AudioUnit, ...). */
    juce::StringArray getHostedFormats() const;

    static constexpr int maxMixerInserts = 8;
    static constexpr int maxPinnedParameters = 4;

    /** Adds a plug-in at the end of a chain. typeOrIdentifier is a built-in type
        name (ReverbPlugin::xmlTypeName, ...) or a catalogue path / identifier.
        A plug-in that failed to scan can't be inserted. On success, addedId
        (if given) receives the new plug-in's id. */
    juce::Result insert (const juce::String& trackId, const juce::String& typeOrIdentifier,
                         PluginChain = PluginChain::device, juce::String* addedId = nullptr);

    /** Puts a new plug-in where pluginId is, on the same chain, removing the
        old one: one undo step. A mixer insert is still effects only. On
        success, addedId (if given) receives the new plug-in's id. */
    juce::Result replace (const juce::String& trackId, const juce::String& pluginId, const juce::String& typeOrIdentifier,
                          juce::String* addedId = nullptr);

    /** Whether the newest undo step is the insert (or replace) that added
        pluginId, so undoing it takes back exactly that plug-in. */
    bool isNewestStepInsertOf (const juce::String& pluginId) const;

    /** Removes a plug-in from either chain. */
    bool remove (const juce::String& trackId, const juce::String& pluginId);

    /** Moves a plug-in within its own chain; newIndex counts that chain only. */
    bool move (const juce::String& trackId, const juce::String& pluginId, int newIndex);

    /** Bypasses (or re-enables) a plug-in. One undo step. */
    bool setBypassed (const juce::String& trackId, const juce::String& pluginId, bool bypassed);

    /** Moves a mixer insert to the end of the track's device chain, one undo step (§10.6). */
    juce::Result moveToDeviceChain (const juce::String& trackId, const juce::String& pluginId);

    /** Puts a copy of a plug-in on another track's mixer chain at index (clamped). */
    juce::Result copyInsert (const juce::String& fromTrackId, const juce::String& pluginId,
                             const juce::String& toTrackId, int index);

    /** One chain of the track, in signal order. Never lists the fader, the
        level meter, aux sends or aux returns. */
    std::vector<PluginInfo> getChain (const juce::String& trackId, PluginChain) const;

    /** Whether the Edit still holds the plug-in (on any track's chain). */
    bool contains (const juce::String& pluginId) const;

    /** Every plug-in on every track's two chains, track by track. */
    std::vector<PluginInfo> getAllPlugins() const;

    /** One plug-in on a track's chain, or nothing for an unknown id. */
    std::optional<PluginInfo> getPlugin (const juce::String& pluginId) const;

    /** A plug-in's parameters, in its own order. Empty for an unknown id. */
    std::vector<PluginParameter> getParameters (const juce::String& pluginId) const;

    /** How the plug-in shows a value of one of its parameters (e.g. "2.4 kHz"). */
    juce::String getParameterText (const juce::String& pluginId, const juce::String& parameterId, float value) const;

    /** Sets a parameter (clamped to its range). continuesGesture joins the
        previous call's undo step when that set the same parameter with nothing
        undoable in between: a knob drag is one step (see EngineUndo). */
    bool setParameter (const juce::String& pluginId, const juce::String& parameterId, float value, bool continuesGesture = false);

    /** Sets several parameters of one plug-in as one change: dragging an EQ
        node moves its frequency and gain together. continuesGesture joins the
        previous call's undo step when that set the same parameters with
        nothing undoable in between. False when nothing changed. */
    bool setParameters (const juce::String& pluginId, const std::vector<ParameterValue>& values, bool continuesGesture = false);

    /** Pins a parameter of an external plug-in to its card (at the end), or
        unpins it. At most maxPinnedParameters; a built-in edits every parameter
        on its card, so it pins none. Saved with the project; one undo step. */
    juce::Result setPinned (const juce::String& pluginId, const juce::String& parameterId, bool pinned);

    /** Watches a plug-in while alive; see watchTouches. */
    struct TouchWatch
    {
        virtual ~TouchWatch() = default;
    };

    /** Calls onTouch with a parameter's id each time the user takes hold of it
        in the plug-in's own window (its change gesture begins): how a card
        learns what to pin. Empty for an unknown plug-in. */
    std::unique_ptr<TouchWatch> watchTouches (const juce::String& pluginId,
                                              std::function<void (const juce::String& parameterId)> onTouch) const;

    /** Folds, unfolds or expands a native device's card. A view of the device,
        so not an undo step, but saved with the project. */
    juce::Result setSize (const juce::String& pluginId, DeviceSize);

    /** Finds a Missing plug-in in file (a bundle or plug-in file the user
        points at): scans it, then Plug-in Hosting starts the plug-in from it,
        taking its description from the file. Fails if the plug-in isn't
        Missing or the file doesn't hold it. */
    juce::Result locate (const juce::String& pluginId, const juce::File&);

    /** The share of the audio callback the plug-in last took, 0..1. */
    double getCpuLoad (const juce::String& pluginId) const;

    /** Hosted JUCE editor for an inserted plug-in. Empty if it has none, or the id is unknown. */
    std::unique_ptr<juce::Component> createEditor (const juce::String& pluginId) const;

    /** A host-drawn panel of an external plug-in's parameters (the window's
        Parameters). Empty if the id is unknown or isn't an external plug-in. */
    std::unique_ptr<juce::Component> createParameterEditor (const juce::String& pluginId) const;

    //==============================================================================
    // The plug-in window (PRD §9.6)

    /** The window's saved state; the default for an unknown id. */
    PluginWindowState getWindowState (const juce::String& pluginId) const;

    /** Saves the window's state on the plug-in. A view: never an undo step. */
    juce::Result setWindowState (const juce::String& pluginId, const PluginWindowState&);

    /** The presets the window's menu offers: the plug-in's own programs, then
        those saved with savePreset, in that order. */
    juce::StringArray getPresetNames (const juce::String& pluginId) const;

    /** Loads preset index of getPresetNames and remembers its name (saved with
        the project). Not an undo step: the plug-in keeps its state outside the Edit. */
    juce::Result selectPreset (const juce::String& pluginId, int index);

    /** Saves the plug-in's state as a named preset, offered by every instance
        of the same plug-in afterwards, and makes it the current preset. */
    juce::Result savePreset (const juce::String& pluginId, const juce::String& name);

    /** Where savePreset writes, one folder per plug-in. Defaults to the user's
        application data folder. */
    void setPresetFolder (const juce::File& folder)     { presetFolder = folder; }

    /** A/B compare: switches the plug-in to slot (0 = A, 1 = B), keeping the
        slot it leaves to come back to. B starts as a copy of A. Saved with the
        project; not an undo step. External plug-ins only. */
    juce::Result selectABSlot (const juce::String& pluginId, int slot);

    /** Copies A over B (the window's Copy A→B). */
    juce::Result copyAToB (const juce::String& pluginId);

    /** The v2 native devices' curves, spectra and meters. */
    NativeDevices& getNativeDevices() noexcept               { return nativeDevices; }
    const NativeDevices& getNativeDevices() const noexcept   { return nativeDevices; }

private:
    struct ScanThread;
    friend struct test::PluginRackTests;

    ProjectManager& projectManager;
    NativeDevices nativeDevices { projectManager };
    std::unique_ptr<ScanThread> scanThread;
    std::atomic<bool> scanning { false };

    /** Set: the running scan is a retry of this one file only. */
    juce::String retryPath;

    /** The formats a scan walks: what the engine hosts, by format name. */
    juce::StringArray scanFormats { "VST3", "AudioUnit", "CLAP" };

    /** Set from the scan thread when its body starts on a thread other than startScan's caller. */
    std::atomic<bool> scanBodyRanOffCaller { false };
    juce::Thread::ThreadID scanCallerId = nullptr;

    mutable juce::CriticalSection snapshotLock;
    juce::Array<PluginInfo> externalSnapshot;

    juce::File presetFolder;

    /** The plug-in the newest insert added, and the undo history's depth right after it. */
    juce::String lastInsertedId;
    int lastInsertDepth = -1;

    static constexpr int scanStopTimeoutMs = 120000;

    void runScan();
    void startScanThread (const juce::String& onlyPath);

    /** Gives the engine's plug-in list a scanner that times each file out after timeoutMs. */
    void installScanner (int timeoutMs);

    /** Whether path is in the plug-in list's blacklist: a scan of it failed. */
    bool failedToScan (const juce::String& path) const;

    /** Asks a running scan to stop and waits for it, keeping the message loop
        running meanwhile when called on the message thread. */
    void stopScan();
    void publishExternalSnapshot();

    JUCE_DECLARE_NON_COPYABLE (PluginRack)
};

} // namespace resamper
