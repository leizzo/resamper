# Phases 6–10 are facades beside the Application Model; a Shaper is an engine modifier

Phases 6–10 add plug-ins, the mixer, automation, Session View, and production operations. They follow ADR-0001: state stays in the Edit, and nothing above the engine layer includes a Tracktion header. They do not grow `ApplicationModel` into a second god object. Each area is its own facade over the same `ProjectManager::getEdit()` — `PluginRack`, `Mixer`, `Automation`, `Shaper`, `Session`, `Production` — and UI reaches them only through Commands (ADR-0003, ADR-0006).

A **Shaper** is not a new DSP graph. Loop mode is Tracktion's breakpoint-oscillator modifier, transport-synced, assigned to the target parameter. Audio-trigger mode is Tracktion's envelope-follower modifier on that same parameter, listening to the track. Both are undoable Edit state.

**Considered options:**
- Folding every new operation into `ApplicationModel`. Rejected: one translation unit would have to change for every phase, and parallel work would collide.
- A custom audio-thread shaper plug-in. Rejected: the engine already modulates parameters safely; a second writer would fight it.
- Inferring a MIDI track's kind from whichever instrument is loaded. Already rejected by ADR-0011. Inserting an instrument on a MIDI track replaces the built-in synth in the same undo step and leaves `resamperKind` as `midi`.

**Consequences:** `ApplicationModel::getTracks()` still lists audio and MIDI tracks only. Returns are audio tracks. Buses are folder tracks and show up through the Mixer, not as a new track kind. Undo for these mutations is Engine Undo, the same `edit.undo()` path as every other model change.
