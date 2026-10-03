# UI State lives in an app-owned store, not in components

Hot Reload destroys and recreates component subtrees, and the brief §16 requires scroll/zoom/focus to survive. Rather than scraping state out of dying components (capture/restore), UI State lives in an app-owned ValueTree store keyed by component ID; components bind to it on construction. The structural invariant: **components are disposable, state is not.**

**Considered options:** §16's literal capture/restore snapshots at reload time — rejected because it only works at reload boundaries and can't express state shared between components (TimelineHeader, TrackLanes, and Playhead all need the same zoom/scroll to implement `timeToX`/`xToTime`).

**Consequences:** Selection is *not* UI State — clip and track selection live in the engine's SelectionManager and survive reload for free, so they drop off brief §16's preservation list. A MIDI note is not a Selectable, and the engine's selected-event list holds pointers that die on undo, so note selection is an id list on the Application Model: still not UI State, and still not an undo step. Arrangement coordinate conversion (`timeToX`/`xToTime`) has a single owner: the view-state object holding zoom and scroll, not free functions.

**Update (2026-10-03):** The JSON layout system was removed, so Hot Reload no longer rebuilds component subtrees; it only re-styles. The store stays: several components share zoom and scroll, and UI State is saved with the Edit.
