# Commands delegate undo to the engine

`te::Edit` owns a `juce::UndoManager` and every mutation of the Edit's ValueTree is automatically undoable, with transaction batching built in. The brief's original §6 design (each Command implements its own `undo()`) would fight that machinery. We decided: named Command classes are the **only mutation path from UI**, but their job is to call the Application Model — Ctrl+Z goes straight to `edit.undo()`. Commands exist to be the single choke point (gesture coalescing, a future macro/script surface, invocation by string ID from JSON buttons, menus, and shortcuts), not to be undo mechanisms.

**Considered options:** (b) No Command classes, UI calls the model directly — rejected because retrofitting a choke point after the UI has grown is painful. (c) Custom per-command undo stack per brief §6 — rejected as duplicating engine machinery.

**Consequences:** The undoable set is exactly the engine's model mutations (add/remove track, insert/move/resize/split clip; track volume and pan per ADR-0009; each recording and take switch per ADR-0010; a MIDI track and its built-in instrument per ADR-0011; adding, deleting, moving, resizing, changing the velocity of, and quantizing MIDI notes). A clip only moves onto a track of its own kind. A velocity drag is one undo step, like a fader drag. Transport, selection (including which notes are selected), zoom, and scroll are never undoable and must not go through Commands in a way that touches the UndoManager.

**Update (2026-10-03):** The JSON layout system was removed; buttons are C++ components that invoke Commands by the same string IDs. The choke point is unchanged.
