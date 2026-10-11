#include "TransportCommands.h"
#include "AppCommandHost.h"
#include "TapTempo.h"

#include <optional>

namespace resamper
{

namespace
{
    /** The Time Selection, else the span of the selected clips, if any. */
    std::optional<TimeRangeSeconds> selectionSpan (const ApplicationModel& model)
    {
        auto span = model.getTimeSelection();

        if (span)
            return span;

        for (auto& track : model.getTracks())
            for (auto& clip : track.clips)
                if (clip.selected)
                {
                    const auto end = clip.startSeconds + clip.lengthSeconds;
                    span = span ? TimeRangeSeconds { std::min (span->start, clip.startSeconds), std::max (span->end, end) }
                                : TimeRangeSeconds { clip.startSeconds, end };
                }

        return span;
    }
}

void registerTransportCommands (CommandRegistry& registry, ApplicationModel& model, AppCommandHost& host)
{
    registry.add (cmd::transportPlay, { "Play" }, [&model] { model.play(); });
    registry.add (cmd::transportStop, { "Stop" }, [&model] { model.stop(); });

    // The spacebar: dispatches to transport.play or transport.stop, so both
    // surfaces run the very same Commands.
    registry.add (cmd::transportTogglePlay, { "Play/Stop" }, [&model, &registry]
    {
        registry.invoke (model.isPlaying() ? cmd::transportStop : cmd::transportPlay);
    });

    registry.add (cmd::transportSetTempo, { "Set Tempo" }, [&model] (const TempoArgs& a)
    {
        model.setTempo (a.bpm, a.continuesGesture);
    });

    // `T`: sets the tempo from the last few taps.
    registry.add (cmd::transportTapTempo, { "Tap Tempo" }, [&model, taps = TapTempo()] () mutable
    {
        if (auto bpm = taps.tap (juce::Time::getMillisecondCounterHiRes() / 1000.0); bpm > 0)
            model.setTempo (bpm);
    });

    registry.add (cmd::transportSetTimeSignature, { "Set Time Signature" }, [&model] (const TimeSignature& a)
    {
        model.setTimeSignature (a.numerator, a.denominator);
    });

    registry.add (cmd::transportToggleMetronome, { "Metronome", {}, [&model] { return model.isMetronomeOn(); } },
                  [&model] { model.setMetronomeOn (! model.isMetronomeOn()); });

    registry.add (cmd::transportToggleCountIn, { "Count-in (2 Bars)", {}, [&model] { return model.isCountInOn(); } },
                  [&model] { model.setCountInOn (! model.isCountInOn()); });

    registry.add (cmd::transportReturnToStart, { "Return to Start" }, [&model] { model.returnToStart(); });

    // A click on the timeline ruler.
    registry.add (cmd::transportSetPosition, { "Set Playhead" }, [&model] (const double& seconds)
    {
        model.setTransportPosition (seconds);
    });

    // Counts in when the count-in is on, unless args say not to.
    registry.add (cmd::transportRecord, { "Record" }, [&model, &host] (const RecordArgs& a)
    {
        host.report (model.record (a.countIn));
    });

    registry.add (cmd::transportToggleLoop, { "Loop" }, [&model] { model.setLooping (! model.isLooping()); });

    // A drag along the timeline ruler.
    registry.add (cmd::transportSetLoopRange, { "Set Loop" }, [&model] (const TimeRangeSeconds& range)
    {
        if (model.setLoopRange (range.start, range.end))
            model.setLooping (true);
    });

    // The loop brace dragged by its body or an end.
    registry.add (cmd::transportMoveLoop, { "Move Loop" }, [&model] (const TimeRangeSeconds& range)
    {
        model.setLoopRange (range.start, range.end);
    });

    // Mod+L: loop the selection, or with nothing selected, toggle the loop.
    registry.add (cmd::transportLoopSelection, { "Loop Selection" }, [&model]
    {
        if (auto span = selectionSpan (model); span && model.setLoopRange (span->start, span->end))
            model.setLooping (true);
        else
            model.setLooping (! model.isLooping());
    });

    // Shift+Space: play from the start of the selection (else as Play).
    registry.add (cmd::transportPlayFromSelection, { "Play from Selection" }, [&model]
    {
        if (auto span = selectionSpan (model))
            model.setTransportPosition (span->start);

        model.play();
    });
}

} // namespace resamper
