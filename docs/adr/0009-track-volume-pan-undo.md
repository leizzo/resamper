# Track volume and pan are undoable per gesture

The brief asks for parameter changes to be undoable (§6, `ChangeParameterCommand`). Track volume and pan are parameters of the track's `VolumeAndPanPlugin`. Setting such a parameter the engine's way, through the parameter rather than its state, already records the state change in the Edit's UndoManager. But undo and redo restore only the state: a parameter never re-reads its state by itself, so playback would keep the old value. We decided:

- The Application Model sets volume and pan through the parameter (`track.setVolume` and `track.setPan` play the role of `ChangeParameterCommand`), so Engine Undo records them (ADR-0003).
- After every undo and redo, the model re-syncs any volume/pan parameter that differs from its state. It never writes a parameter whose value already matches: re-reading writes the state back, and a write right after an undo would clear the redo history. Before the first change it makes the default value explicit in state, without undo, so undoing that change restores a value rather than removing it.
- A fader drag is one undo step. Every value after the first carries `continuesGesture`, and the model joins that value to the open undo step only if the previous call was the same gesture with nothing undoable in between. The engine coalesces consecutive writes to one property within a step.

~~Mute and solo follow the engine, which keeps them out of the UndoManager on purpose: they are performance toggles, like transport.~~ Superseded (2026-10-10, #81): the spec makes every discrete action one undo step, mute and solo included. The Application Model writes the track's `mute` and `solo` properties through the UndoManager, one step per toggle, because `Track::setMute` and `setSolo` bypass it.

**Considered options:**
- A custom `juce::UndoableAction` that sets the parameter on perform and undo. Rejected: the parameter's own state write is already recorded, so each change was recorded twice, and setting the parameter inside undo triggers a recursive `UndoManager::perform`, which JUCE refuses.
- Committing only on mouse-up, like clip drags. Rejected: a fader must be heard while it moves.

**Consequences:** ADR-0003's undoable set now includes track volume and pan. Future parameters, such as plugin parameters and sends, need the same re-sync after undo/redo; `syncVolumeParametersFromState` is where it should grow.
