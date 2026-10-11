#pragma once

#include "CommandRegistry.h"
#include "Engine/ApplicationModel.h"

namespace resamper
{

struct AppCommandHost;

/** How Rec starts: countIn false records at once (Shift-click Rec). */
struct RecordArgs
{
    bool countIn = true;
};

/** A new tempo; continuesGesture joins a drag into one undo step. */
struct TempoArgs
{
    double bpm = 0;
    bool continuesGesture = false;
};

namespace cmd
{
    inline constexpr CommandRef<> transportPlay { "transport.play" };
    inline constexpr CommandRef<> transportStop { "transport.stop" };
    inline constexpr CommandRef<> transportTogglePlay { "transport.togglePlay" };
    inline constexpr CommandRef<> transportReturnToStart { "transport.returnToStart" };
    inline constexpr CommandRef<double> transportSetPosition { "transport.setPosition" };       ///< seconds
    inline constexpr CommandRef<RecordArgs> transportRecord { "transport.record" };
    inline constexpr CommandRef<> transportToggleLoop { "transport.toggleLoop" };
    inline constexpr CommandRef<TimeRangeSeconds> transportSetLoopRange { "transport.setLoopRange" }; ///< also turns looping on
    inline constexpr CommandRef<TimeRangeSeconds> transportMoveLoop { "transport.moveLoop" };         ///< leaves looping as it is
    inline constexpr CommandRef<> transportLoopSelection { "transport.loopSelection" };
    inline constexpr CommandRef<> transportPlayFromSelection { "transport.playFromSelection" };
    inline constexpr CommandRef<TempoArgs> transportSetTempo { "transport.setTempo" };
    inline constexpr CommandRef<> transportTapTempo { "transport.tapTempo" };
    inline constexpr CommandRef<TimeSignature> transportSetTimeSignature { "transport.setTimeSignature" };
    inline constexpr CommandRef<> transportToggleMetronome { "transport.toggleMetronome" };
    inline constexpr CommandRef<> transportToggleCountIn { "transport.toggleCountIn" };
}

/** Registers the transport Commands above. */
void registerTransportCommands (CommandRegistry&, ApplicationModel&, AppCommandHost&);

} // namespace resamper
